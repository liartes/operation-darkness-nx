// opdarkness - diagnostic : appels de setjmp (sub_826A6360) et de longjmp (sub_826A5E10) du jeu (2026-10-09).
//
// Déclarés au générateur (setjmp_address / longjmp_address), ils brouillaient la vidéo d'introduction (décodeur vidéo
// du jeu) ; sans déclaration, la vidéo est juste. --od_setjmp_journal compte les appels et écrit les premiers (avec
// l'adresse de retour) pour savoir si le jeu se sert de longjmp en temps normal.

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(od_setjmp_journal, false, "OpDarkness",
                    "Diagnostic : journal des appels de setjmp et de longjmp du jeu");

namespace {
std::atomic<uint64_t> g_setjmp{0}, g_longjmp{0};
}  // namespace

REX_EXTERN(__imp__sub_826A6360);
REX_HOOK_RAW(sub_826A6360) {  // setjmp(r3 = tampon)
  if (REXCVAR_GET(od_setjmp_journal)) {
    const uint64_t n = g_setjmp.fetch_add(1, std::memory_order_relaxed) + 1;
    if (n <= 10 || (n & (n - 1)) == 0) {
      REXLOG_INFO("[setjmp] appel n°{} depuis {:08X}, tampon {:08X}", n, uint32_t(ctx.lr), ctx.r3.u32);
    }
  }
  __imp__sub_826A6360(ctx, base);
}

REX_EXTERN(__imp__sub_826A5E10);
REX_HOOK_RAW(sub_826A5E10) {  // longjmp(r3 = tampon, r4 = valeur)
  if (REXCVAR_GET(od_setjmp_journal)) {
    const uint64_t n = g_longjmp.fetch_add(1, std::memory_order_relaxed) + 1;
    REXLOG_INFO("[setjmp] longjmp n°{} depuis {:08X}, tampon {:08X}, valeur {}", n, uint32_t(ctx.lr), ctx.r3.u32,
                ctx.r4.s32);
  }
  __imp__sub_826A5E10(ctx, base);
}
