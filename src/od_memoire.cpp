// opdarkness - mesure des allocations de mémoire physique du jeu (sub_823999D0 -> MmAllocatePhysicalMemoryEx).
//
// Pendant les à-coups du début de la cinématique du combat sur la console (2026-10-16), le fil de dessin du jeu
// passait une partie de son temps dans BaseHeap::AllocRange du SDK, qui cherche une place libre en parcourant la
// table des pages une à une. Ici : nombre d'allocations, temps total et pire temps, toutes les 10 s (seulement s'il y
// en a eu), pour savoir si ça compte.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

REXCVAR_DEFINE_BOOL(od_memoire_stats, true, "OpDarkness",
                    "Journal des allocations de mémoire physique du jeu (sub_823999D0) toutes les 10 s : nombre, temps "
                    "total et pire temps");

REX_EXTERN(__imp__sub_823999D0);
REX_HOOK_RAW(sub_823999D0) {
  if (!REXCVAR_GET(od_memoire_stats)) {
    __imp__sub_823999D0(ctx, base);
    return;
  }
  const uint32_t taille = ctx.r3.u32;
  const auto debut = std::chrono::steady_clock::now();
  __imp__sub_823999D0(ctx, base);
  const auto fin = std::chrono::steady_clock::now();
  const uint64_t ns = uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(fin - debut).count());

  static std::mutex verrou;
  static uint64_t n = 0, ns_total = 0, ns_pire = 0, octets = 0, taille_pire = 0;
  static auto bilan = fin;
  std::lock_guard<std::mutex> g(verrou);
  ++n;
  ns_total += ns;
  octets += taille;
  if (ns > ns_pire) {
    ns_pire = ns;
    taille_pire = taille;
  }
  if (fin - bilan >= std::chrono::seconds(10)) {
    REXLOG_INFO("[mémoire] allocations physiques du jeu, depuis le dernier bilan : {} ({:.1f} MB) en {:.1f} ms, pire "
                "{:.2f} ms ({} octets)",
                n, double(octets) / 1048576.0, double(ns_total) / 1e6, double(ns_pire) / 1e6, taille_pire);
    n = ns_total = ns_pire = octets = taille_pire = 0;
    bilan = fin;
  }
}
