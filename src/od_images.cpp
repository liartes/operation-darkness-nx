// opdarkness - compteur d'images : chaque appel de D3DDevice_Swap (sub_8234A5C8, docs/D3D.md) est une image du jeu.
// Toutes les 10 s (--od_images_s), le journal reçoit « [images] N images/s » : même mesure avec le GPU émulé et avec
// le rendu natif. L'original est toujours appelé, aucun registre n'est touché.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#include "renut_engine/gpu/sa2_renut_yield.h"

#include "od_images.h"

REXCVAR_DEFINE_INT32(od_images_s, 10, "OpDarkness",
                     "Période du bilan « [images] » dans le journal, en secondes (0 = pas de bilan)");

REXCVAR_DEFINE_STRING(od_pause_images, "", "OpDarkness",
                      "Images (numéros de D3DDevice_Swap, séparés par des virgules) où le jeu s'arrête le temps d'une "
                      "capture : il écrit le numéro dans od_pause_fichier et attend que ce fichier disparaisse "
                      "(tools/captures_images.sh)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_pause_fichier, "", "OpDarkness", "Fichier témoin des pauses de od_pause_images")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

// --od_pause_images : arrêt du thread qui dessine à ces images, le temps qu'un outil capture la fenêtre. Le rendu
// (anneau du rendu natif, ou émulation) finit l'image en cours pendant ce temps : la capture montre l'image n-1, la
// même pour tous les rendus.
void PauseSiDemandee(uint64_t n) {
  static const std::vector<uint64_t> images = [] {
    std::vector<uint64_t> v;
    const std::string liste = REXCVAR_GET(od_pause_images);
    size_t i = 0;
    while (i < liste.size()) {
      size_t j = liste.find(',', i);
      if (j == std::string::npos) j = liste.size();
      if (j > i) v.push_back(std::stoull(liste.substr(i, j - i)));
      i = j + 1;
    }
    return v;
  }();
  if (images.empty() || std::find(images.begin(), images.end(), n) == images.end()) return;
  const std::filesystem::path temoin = REXCVAR_GET(od_pause_fichier);
  if (temoin.empty()) return;
  if (std::FILE* f = std::fopen(temoin.string().c_str(), "w")) {
    std::fprintf(f, "%llu\n", static_cast<unsigned long long>(n));
    std::fclose(f);
  }
  REXLOG_INFO("[images] pause à l'image {} pour une capture", n);
  std::error_code ec;
  for (int i = 0; i < 600 && std::filesystem::exists(temoin, ec); ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

using Clock = std::chrono::steady_clock;
std::atomic<uint64_t> g_images{0};

void Compter() {
  static Clock::time_point debut = Clock::now();
  static uint64_t images_debut = 0;
  const uint64_t n = g_images.fetch_add(1, std::memory_order_relaxed) + 1;
  PauseSiDemandee(n);
  const int32_t periode = REXCVAR_GET(od_images_s);
  if (periode <= 0) return;
  const auto maintenant = Clock::now();
  const double ecoule = std::chrono::duration<double>(maintenant - debut).count();
  if (ecoule < periode) return;
  REXLOG_INFO("[images] {:.1f} images/s ({} images en {:.1f} s, {} au total)", double(n - images_debut) / ecoule,
              n - images_debut, ecoule, n);
#if defined(REX_OD_DENORMAUX_ON)
  // Diagnostic de CG-13 (SDK, rex/ppc/context.h) : régions scalaires du jeu qui ont lu un dénormal ou fait un
  // dépassement par le bas pendant ces 10 s.
  {
    static uint64_t r0 = 0, d0 = 0;
    const uint64_t r = rex::ppc::g_od_regions_scalaires.load(std::memory_order_relaxed);
    const uint64_t d = rex::ppc::g_od_regions_denormaux.load(std::memory_order_relaxed);
    REXLOG_INFO("[denormaux] {} régions scalaires, {} avec un dénormal ou un dépassement par le bas", r - r0, d - d0);
    r0 = r;
    d0 = d;
  }
#endif
  debut = maintenant;
  images_debut = n;
}

}  // namespace

uint64_t opdarkness::ImagesPresentees() { return g_images.load(std::memory_order_relaxed); }

REX_EXTERN(__imp__sub_8234A5C8);
REX_HOOK_RAW(sub_8234A5C8) {  // D3DDevice_Swap, appelée par le thread qui dessine
  Compter();
  SA2_RENUT_YIELD_SHARED(sub_8234A5C8)  // rendu reNut (od_renderer = "renut") : son Swap
  __imp__sub_8234A5C8(ctx, base);
}
