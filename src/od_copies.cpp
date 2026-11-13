// opdarkness - copies localisées de fonctions chaudes du jeu (src/copies_localisees.txt, produites dans generated/hot/
// par tools/apres_codegen.py avec tools/localize_native.py de sa2-nx). Chaque copie exécute les opérations de la fonction
// recompilée, registres du jeu en variables locales, et rend au contexte tous ses registres : même résultat, moins
// d'accès au contexte. od_copies_localisees = false revient au code recompilé.

#include "opdarkness_pch.h"

#include <rex/cvar.h>
#include <rex/hook.h>

REXCVAR_DEFINE_BOOL(od_copies_localisees, true, "OpDarkness",
                    "Fonctions chaudes du jeu par leurs copies localisées (src/copies_localisees.txt ; même résultat)");

REXCVAR_DEFINE_BOOL(od_copies_verif, false, "OpDarkness",
                    "Diagnostic : les copies des fonctions pures (log, log10) sont comparées à l'original à chaque appel "
                    "(contexte entier) ; écarts comptés dans le journal");

#include <atomic>
#include <cmath>
#include <cstring>

#include <rex/logging.h>

#include "generated/hot/n_826A6900.h"
#include "generated/hot/n_826A6A80.h"
#include "generated/hot/n_8289FF20.h"
#include "generated/hot/n_82335ED0.h"
#include "generated/hot/n_82336B78.h"
#include "generated/hot/n_823ACAA8.h"
#include "generated/hot/n_823ACE18.h"
#include "generated/hot/n_823ABB38.h"
#include "generated/hot/n_823AA7F8.h"
#include "generated/hot/n_823A7D10.h"
#include "generated/hot/n_828A5210.h"
#include "generated/hot/n_8288F270.h"
#include "generated/hot/n_8261CCA8.h"  // test de visibilité, pile locale (tools/pile_locale.py)

#define OD_COPIE(ADDR)                                                  \
  REX_EXTERN(__imp__sub_##ADDR);                                        \
  REX_HOOK_RAW(sub_##ADDR) {                                            \
    if (REXCVAR_GET(od_copies_localisees)) sa2::hot::n_##ADDR::Native(ctx, base); \
    else __imp__sub_##ADDR(ctx, base);                                  \
  }

// Fonctions pures (registres et pile seulement) : vérifiables en exécutant les deux versions.
namespace {
std::atomic<uint64_t> g_verifs{0}, g_ecarts{0};
template <void (*Copie)(PPCContext&, uint8_t*), void (*Original)(PPCContext&, uint8_t*)>
void Verifie(PPCContext& ctx, uint8_t* base, uint32_t adresse) {
  PPCContext a = ctx;
  Copie(a, base);
  Original(ctx, base);
  const uint64_t n = ++g_verifs;
  if (std::memcmp(&a, &ctx, sizeof(ctx)) != 0) {
    const uint64_t e = ++g_ecarts;
    if (e <= 10 || (e & (e - 1)) == 0)
      REXLOG_WARN("[copies] écart sur sub_{:08X} ({} écarts / {} vérifications) : f1 {:016X} contre {:016X}",
                  adresse, e, n, a.f1.u64, ctx.f1.u64);
  } else if ((n & (n - 1)) == 0 && n >= 1024) {
    REXLOG_INFO("[copies] {} vérifications, {} écarts", n, g_ecarts.load());
  }
}
}  // namespace

#define OD_COPIE_PURE(ADDR)                                             \
  REX_EXTERN(__imp__sub_##ADDR);                                        \
  REX_HOOK_RAW(sub_##ADDR) {                                            \
    if (!REXCVAR_GET(od_copies_localisees)) __imp__sub_##ADDR(ctx, base); \
    else if (REXCVAR_GET(od_copies_verif))                              \
      Verifie<sa2::hot::n_##ADDR::Native, __imp__sub_##ADDR>(ctx, base, 0x##ADDR); \
    else sa2::hot::n_##ADDR::Native(ctx, base);                         \
  }

OD_COPIE_PURE(826A6900)

// log10 (sub_826A6A80 = log(x) x 1/ln 10, la constante à 82093068) appelé par le compresseur audio sub_82335928 pour
// chaque échantillon : avec le log de la bibliothèque C du jeu (sub_826A6900, 95 instructions), ~50 % du fil « Audio
// Worker » sur la console (profil du 2026-10-16). Pour cet appelant seulement (retour 823359D0) et x > 0 fini : le log
// de l'hôte. Il diffère du log du jeu d'un ulp sur ~2 % des valeurs en double, jamais sur le float que le compresseur
// en tire (frsp juste après l'appel ; vérifié sur 23 millions d'appels sur PC). Les autres appelants de log et log10
// (logique du jeu comprise) gardent la copie exacte. od_log_verif : comparaison avec la copie exacte à chaque appel.
REXCVAR_DEFINE_BOOL(od_log_natif, true, "OpDarkness",
                    "log10 du compresseur audio (sub_82335928 -> sub_826A6A80) par le log de l'hôte (même float)");
REXCVAR_DEFINE_BOOL(od_log_verif, false, "OpDarkness",
                    "Diagnostic : log10 natif du compresseur comparé à la copie exacte (float vu par le compresseur)");

namespace {
std::atomic<uint64_t> g_log_appels{0}, g_log_ecarts{0};
constexpr uint32_t kRetourCompresseur = 0x823359D0;
}  // namespace

REX_EXTERN(__imp__sub_826A6A80);
REX_HOOK_RAW(sub_826A6A80) {
  const double x = ctx.f1.f64;
  if (!REXCVAR_GET(od_copies_localisees)) {
    __imp__sub_826A6A80(ctx, base);
    return;
  }
  if (!REXCVAR_GET(od_log_natif) || uint32_t(ctx.lr) != kRetourCompresseur || !(x > 0.0) || !std::isfinite(x)) {
    sa2::hot::n_826A6A80::Native(ctx, base);
    return;
  }
  uint64_t brut;
  std::memcpy(&brut, base + 0x82093068u, 8);
  brut = __builtin_bswap64(brut);
  PPCRegister c;
  c.u64 = brut;
  const double resultat = std::log(x) * c.f64;
  if (REXCVAR_GET(od_log_verif)) {
    sa2::hot::n_826A6A80::Native(ctx, base);
    const uint64_t n = ++g_log_appels;
    if (float(ctx.f1.f64) != float(resultat)) {
      const uint64_t e = ++g_log_ecarts;
      if (e <= 10 || (e & (e - 1)) == 0)
        REXLOG_WARN("[log] écart en float ({} / {}) : log10({:.17g}) = {:.9g} (jeu) contre {:.9g} (hôte)", e, n, x,
                    float(ctx.f1.f64), float(resultat));
    } else if ((n & (n - 1)) == 0 && n >= 1024) {
      REXLOG_INFO("[log] {} appels vérifiés, {} écarts en float", n, g_log_ecarts.load());
    }
  }
  // Ce que log10 laisse à son appelant : f1, f0 = la constante, r11 = 0x82090000 (lis r11,-32247).
  ctx.f1.f64 = resultat;
  ctx.f0.u64 = brut;
  ctx.r11.u64 = 0xFFFFFFFF82090000ull;
}

OD_COPIE(8289FF20)
OD_COPIE(82335ED0)
OD_COPIE(82336B78)
OD_COPIE(823ACAA8)
OD_COPIE(823ACE18)
OD_COPIE(823ABB38)
OD_COPIE(823AA7F8)
OD_COPIE(823A7D10)
OD_COPIE(828A5210)
OD_COPIE(8288F270)
OD_COPIE_PURE(8261CCA8)  // test de visibilité (fil de dessin) : registres et pile locaux, vérifiable (od_copies_verif)
// sub_82335928 (compresseur) : src/od_compresseur.cpp (natif, repli sur la copie localisée).
