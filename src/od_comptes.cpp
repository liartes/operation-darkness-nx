// opdarkness - comptes de références du moteur, en natif (sub_8232CEC0 : AddRef, sub_8232CF90 : Release).
//
// Profil de la console (rendu reNut, 2026-10-22) : le fil de dessin du jeu est le goulot (100 % d'un cœur) ; la famille
// « verrou du moteur » (passerelles vers RtlTryEnter / RtlLeaveCriticalSection, opérations atomiques, Sleep(0)
// intercepté) en fait ~7 %. Chaque compte de références :
//   sub_8232CCC8(objet, fichier, ligne) : RtlTryEnterCriticalSection(objet + 4), sinon Sleep(0) et recommencer ; puis
//     objet+32 = fichier, objet+36 = ligne, objet+40 = 1 (qui tient le verrou, pour le débogage du moteur)
//   AddRef  : verrou (fichier 0x8211A4B0, ligne 207) ; n = u16 objet+44 + 1 ; objet+44 = n ; objet+40 = 0 ;
//             RtlLeaveCriticalSection(objet + 4) ; Sleep(0) ; renvoie n
//   Release : verrou (ligne 222) ; n = u16 objet+44 ; si n : n - 1 écrit en objet+44 (sinon rien, renvoie 0) ; ...
// Ici : les mêmes écritures, les fonctions du SDK appelées directement (sans la passerelle du code du jeu), et sans le
// Sleep(0) final, qui suit un RtlLeaveCriticalSection et ne fait déjà rien (od_sleep0_courtoisie, src/od_sommeil.cpp).
// Verrou pris par un autre fil : l'original, qui attend comme avant.
// od_comptes_verif : l'original est exécuté et son résultat comparé à celui que le code natif aurait rendu.

#include "opdarkness_pch.h"

#include <atomic>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/types.h>

REXCVAR_DEFINE_BOOL(od_comptes, true, "OpDarkness",
                    "Comptes de références du moteur (sub_8232CEC0, sub_8232CF90) en natif (mêmes écritures)");
REXCVAR_DEFINE_BOOL(od_comptes_verif, false, "OpDarkness",
                    "Diagnostic : l'original est exécuté et comparé à ce que le code natif aurait rendu");
REXCVAR_DECLARE(bool, od_sleep0_cede);
REXCVAR_DECLARE(bool, od_sleep0_courtoisie);
REXCVAR_DECLARE(bool, od_sleep0_stats);

namespace rex::kernel::xboxkrnl {
struct X_RTL_CRITICAL_SECTION;
// SDK (patches/sa2-nx-sdk.patch, 2026-11-13) : sans passer par XThread::GetCurrentThread (propriétaire lu dans le PCR).
uint32_t RtlTryEnterCriticalSectionFast(PPCContext& ctx, uint8_t* base, uint32_t cs);
void RtlLeaveCriticalSectionFast(uint8_t* base, uint32_t cs);
}  // namespace rex::kernel::xboxkrnl

namespace {

using rex::kernel::xboxkrnl::X_RTL_CRITICAL_SECTION;

constexpr uint32_t kFichier = 0x8211A4B0u;  // lis r11,-32238 ; addi r4,r11,-23376

// Le Sleep(0) qui suit le RtlLeaveCriticalSection ne fait rien (od_sommeil.cpp) : on peut s'en passer.
bool Actif() {
  static const bool actif = REXCVAR_GET(od_comptes) && REXCVAR_GET(od_sleep0_cede) &&
                            REXCVAR_GET(od_sleep0_courtoisie) && !REXCVAR_GET(od_sleep0_stats);
  return actif;
}

// Le verrou de sub_8232CCC8, s'il est libre (ou déjà tenu par ce fil) : false sinon, rien d'écrit.
bool Prendre(PPCContext& ctx, uint8_t* base, uint32_t objet, uint32_t ligne) {
  if (!rex::kernel::xboxkrnl::RtlTryEnterCriticalSectionFast(ctx, base, objet + 4)) return false;
  REX_STORE_U32(objet + 32, kFichier);
  REX_STORE_U32(objet + 36, ligne);
  REX_STORE_U8(objet + 40, 1);
  return true;
}

void Rendre(uint8_t* base, uint32_t objet) {
  REX_STORE_U8(objet + 40, 0);
  rex::kernel::xboxkrnl::RtlLeaveCriticalSectionFast(base, objet + 4);
}

std::atomic<uint64_t> g_verifs{0}, g_ecarts{0};

// od_comptes_verif : l'original, puis le compte attendu comparé au sien.
template <void (*Original)(PPCContext&, uint8_t*)>
void Verifier(PPCContext& ctx, uint8_t* base, int delta, uint32_t adresse) {
  const uint32_t objet = ctx.r3.u32;
  const uint32_t avant = REX_LOAD_U16(objet + 44);
  Original(ctx, base);
  const uint32_t apres = REX_LOAD_U16(objet + 44);
  const uint32_t attendu = delta > 0 ? (avant + 1) & 0xFFFF : (avant ? avant - 1 : 0);
  const uint64_t n = ++g_verifs;
  // Un autre fil a pu changer le compte entre la lecture et la prise du verrou : seul le résultat rendu fait foi
  // quand l'écart vient de là (rare) ; le relevé le compte quand même.
  if (ctx.r3.u32 != attendu || apres != attendu) {
    const uint64_t e = ++g_ecarts;
    if (e <= 10)
      REXLOG_WARN("[comptes] {:08X} objet {:08X} : avant {} rendu {} écrit {} attendu {}", adresse, objet, avant,
                  ctx.r3.u32, apres, attendu);
  }
  if ((n & ((1u << 22) - 1)) == 0)
    REXLOG_INFO("[comptes] vérification : {} appels, {} écarts", n, g_ecarts.load());
}

}  // namespace

REX_EXTERN(__imp__sub_8232CEC0);
REX_HOOK_RAW(sub_8232CEC0) {  // AddRef(r3 = objet) -> r3 = nouveau compte
  if (!Actif()) return __imp__sub_8232CEC0(ctx, base);
  if (REXCVAR_GET(od_comptes_verif)) return Verifier<__imp__sub_8232CEC0>(ctx, base, 1, 0x8232CEC0);
  const uint32_t objet = ctx.r3.u32;
  if (!Prendre(ctx, base, objet, 207)) return __imp__sub_8232CEC0(ctx, base);
  const uint32_t n = (REX_LOAD_U16(objet + 44) + 1) & 0xFFFF;
  REX_STORE_U16(objet + 44, uint16_t(n));
  Rendre(base, objet);
  ctx.r3.u64 = n;
}

REX_EXTERN(__imp__sub_8232CF90);
REX_HOOK_RAW(sub_8232CF90) {  // Release(r3 = objet) -> r3 = nouveau compte (0 s'il l'était déjà)
  if (!Actif()) return __imp__sub_8232CF90(ctx, base);
  if (REXCVAR_GET(od_comptes_verif)) return Verifier<__imp__sub_8232CF90>(ctx, base, -1, 0x8232CF90);
  const uint32_t objet = ctx.r3.u32;
  if (!Prendre(ctx, base, objet, 222)) return __imp__sub_8232CF90(ctx, base);
  uint32_t n = REX_LOAD_U16(objet + 44);
  if (n) {
    n = (n - 1) & 0xFFFF;
    REX_STORE_U16(objet + 44, uint16_t(n));
  }
  Rendre(base, objet);
  ctx.r3.u64 = n;
}
