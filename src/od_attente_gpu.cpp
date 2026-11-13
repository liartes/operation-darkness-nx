// opdarkness - l'attente du GPU par Direct3D dort au lieu de tourner (guide de sa2-nx §9.5, comme
// sa2-nx app/src/cpu/sa2_gpu_wait.cpp et le principe de src/me/me_ring_wait.cpp).
//
// sub_82349A30 (r3 = structure d'attente : [0] device, [8] dernier pointeur de lecture vu, [12] date de son dernier
// changement) est le test de Direct3D « le GPU a-t-il avancé ? » : quatre attentes actives (mr r31,r31), puis, sauf si
// le GPU est déclaré planté (bit 0x02 de device+10941), relecture du pointeur de lecture que le GPU recopie en mémoire
// (mot dont l'adresse est à device+10896), et contrôle du délai de 5 s (GetTickCount, sub_8239A638). Ses appelants
// (sub_82345720 : attente de place dans le tampon de commandes ; D3DDevice_Swap ; fins d'image) bouclent dessus sans
// pause : avec le rendu natif, le fil qui dessine passait ~18 % du temps du processus à tourner là
// (perf, 2026-10-09, combat).
//
// --od_attente_gpu (vrai par défaut, rendu natif seulement) : si le pointeur n'a pas bougé depuis le dernier appel,
// attendre que le fil de l'anneau signale un progrès (me::native::WaitRingProgress, au plus --od_attente_gpu_us µs),
// puis appeler l'original, qui décide comme avant : le jeu voit les mêmes valeurs, seulement plus tard.

#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/ppc.h>

#include "me/me_native_system.h"
#include "renut_engine/gpu/sa2_renut_yield.h"

REXCVAR_DEFINE_BOOL(od_attente_gpu, true, "OpDarkness",
                    "Rendu natif : l'attente du GPU de Direct3D (sub_82349A30) dort jusqu'au progrès de l'anneau au "
                    "lieu de tourner");
REXCVAR_DEFINE_INT32(od_attente_gpu_us, 2000, "OpDarkness",
                     "Sommeil le plus long par tour de l'attente du GPU (µs)")
    .range(50, 100000);

namespace {

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

inline bool Guest(uint32_t a) { return a >= 0x40000000u && a < 0xA0000000u; }

}  // namespace

REX_EXTERN(__imp__sub_82349A30);
REX_HOOK_RAW(sub_82349A30) {
  SA2_RENUT_YIELD_SHARED(sub_82349A30)  // rendu reNut : son CBlocker::Check
  const uint32_t attente = ctx.r3.u32;
  if (REXCVAR_GET(od_attente_gpu) && me::native::NativeActive() && Guest(attente)) {
    const uint32_t device = Load32(base, attente);
    if (Guest(device) && !(base[device + 10941] & 0x02)) {
      const uint32_t mot = Load32(base, device + 10896);
      if (mot) {
        const uint32_t vu = Load32(base, attente + 8);
        if (Load32(base, mot) == vu) {
          // Progrès lu avant la relecture : un progrès qui arriverait entre les deux réveille l'attente.
          const uint32_t progres = me::native::RingProgress();
          if (Load32(base, mot) == vu) {
            me::native::WaitRingProgress(progres, uint32_t(REXCVAR_GET(od_attente_gpu_us)));
          }
        }
      }
    }
  }
  __imp__sub_82349A30(ctx, base);
}
