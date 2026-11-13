// opdarkness - memcpy et memset de la bibliothèque C du jeu remplacés par ceux de l'hôte (guide de sa2-nx §9.3,
// « CRT functions » ; méthode de torchlight-nx src/torchlight_crt.cpp et de nfsmw-nx).
//
// Identifiés en lisant le PowerPC (docs/OPTIMISATION.md) :
//  - sub_826A4FC0 : memcpy (r3 destination, r4 source, r5 taille) : r3 gardé sur la pile (std r3,-8(r1)) et rendu
//    tel quel ; copie vers l'avant (octets jusqu'à l'alignement, puis mots et doubles mots). 1 645 appels dans le jeu.
//  - sub_826A54F0 : memset (r3 destination, r4 octet, r5 taille) : travaille sur une copie de r3 (r6), r3 rendu
//    tel quel ; octets jusqu'à l'alignement, mots de l'octet répété, octets de fin. 1 040 appels.
//  - sub_826A49E0 : memmove : rend tout de suite si destination = source, confie à memcpy (ci-dessus) si
//    destination < source, sinon copie à rebours ; r3 vaut la destination à chaque sortie. 84 appels. std::memmove a
//    exactement ce résultat, chevauchement compris.
// Non remplacées : floor (sub_826A42E8) et ceil (sub_826A7448), une quinzaine d'instructions sans branchement ; les
// fonctions mathématiques (sub_826A4900, sub_826A6900) : la bibliothèque de l'hôte ne donne pas le même dernier bit
// que celle de la Xbox, et un remplacement doit être exact.
// Le code traduit écrit les mêmes octets ; la mémoire finit identique, seuls des registres volatils de travail
// (r0, r4-r6, ctr, cr0) diffèrent, que les appelants ne relisent pas. Exception : une copie vers l'avant dont la
// destination chevauche la source plus loin (destination dans ]source, source + taille[) ne donne pas le même résultat
// que memmove : ce cas, que le jeu ne devrait jamais faire, est laissé à l'original (exact par construction).
// Une écriture sur une page surveillée par le SDK est rattrapée par son gestionnaire d'exceptions comme pour
// l'original. --od_crt_natif=false rend les originaux.

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(od_crt_natif, true, "OpDarkness",
                    "memcpy (sub_826A4FC0), memset (sub_826A54F0) et memmove (sub_826A49E0) du jeu par ceux de l'hôte")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace {

bool Natif() {
  static const bool actif = REXCVAR_GET(od_crt_natif);
  return actif;
}

std::atomic<uint32_t> g_chevauchements{0};

}  // namespace

REX_EXTERN(__imp__sub_826A4FC0);
REX_HOOK_RAW(sub_826A4FC0) {  // memcpy
  const uint32_t destination = ctx.r3.u32, source = ctx.r4.u32, taille = ctx.r5.u32;
  if (!Natif() || (destination > source && destination - source < taille)) {
    if (Natif() && g_chevauchements.fetch_add(1, std::memory_order_relaxed) < 8) {
      REXLOG_INFO("[crt] memcpy avec chevauchement ({:08X} <- {:08X}, {} octets) : laissé à l'original", destination,
                  source, taille);
    }
    __imp__sub_826A4FC0(ctx, base);
    return;
  }
  if (taille) std::memmove(base + destination, base + source, taille);
}

REX_EXTERN(__imp__sub_826A54F0);
REX_HOOK_RAW(sub_826A54F0) {  // memset
  if (!Natif()) {
    __imp__sub_826A54F0(ctx, base);
    return;
  }
  if (const uint32_t taille = ctx.r5.u32) std::memset(base + ctx.r3.u32, ctx.r4.u8, taille);
}

REX_EXTERN(__imp__sub_826A49E0);
REX_HOOK_RAW(sub_826A49E0) {  // memmove
  if (!Natif()) {
    __imp__sub_826A49E0(ctx, base);
    return;
  }
  if (const uint32_t taille = ctx.r5.u32) std::memmove(base + ctx.r3.u32, base + ctx.r4.u32, taille);
}
