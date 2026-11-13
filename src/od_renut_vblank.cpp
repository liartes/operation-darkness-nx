// opdarkness - la vblank du jeu avec le rendu reNut (od_renderer = "renut" ; docs/DEKO3D.md), repris de futari-nx
// (app/src/renut_engine/futari_renut_vblank.cpp).
//
// Sans GPU, le SDK n'envoie plus d'interruption graphique : le répartiteur de Direct3D (sub_82344CF8, enregistré par
// VdSetGraphicsInterruptCallback dans le chemin matériel, que reNut n'emprunte pas) n'est plus appelé. Or le moteur
// avance sur les vblanks : le gestionnaire de vblank de Direct3D, sub_82349E88 (device ; compteur +16548, file des
// retours d'écran +16700, puis le rappel du jeu +16544), appelle le rappel du moteur sub_8232C7E0 (installé par
// sub_8232C840), qui cadence la tâche de la manette (docs « Cadence du jeu »). Ce fil invité appelle donc lui-même le
// gestionnaire, comme le fil des vblanks du SDK appelle le répartiteur : à 60 Hz, ou une fois par image présentée en
// lockstep (vblank_per_swap, mêmes réglages que le SDK : vblank_per_swap_min_us, vblank_per_swap_timeout_ms ; les
// images sont comptées par l'interception de Swap, src/od_images.cpp, puisque reNut n'appelle plus VdSwap).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <rex/cvar.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/logging.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>
#include <rex/system/xvideo.h>
#include <rex/thread.h>

#include "od_images.h"
#include "renut_engine/gpu/d3d/d3d_native.h"

REXCVAR_DECLARE(int32_t, vblank_per_swap_min_us);      // SDK, xboxkrnl_video.cpp
REXCVAR_DECLARE(int32_t, vblank_per_swap_timeout_ms);  // likewise

namespace od::renut {
namespace {

constexpr uint32_t kGestionnaireVblank = 0x82349E88u;  // Direct3D : gestionnaire de vblank (r3 = device)
constexpr uint32_t kMflrR12 = 0x7D8802A6u;              // sa première instruction

std::atomic<bool> g_actif{false};
rex::system::object_ref<rex::system::XHostThread> g_fil;
std::atomic<uint64_t> g_vblanks{0};

void Vblank() {
  const uint32_t device = ::renut::gpu::native::GlobalDevice();
  if (!device) return;
  auto* fil = rex::system::XThread::GetCurrentThread();
  auto* noyau = rex::system::kernel_state();
  if (!fil || !noyau || !noyau->function_dispatcher()) return;
  static const bool bon_code = [noyau] {
    const auto* mot = noyau->memory()->TranslateVirtual<const rex::be<uint32_t>*>(kGestionnaireVblank);
    const bool bon = mot && uint32_t(*mot) == kMflrR12;
    if (!bon) REXLOG_WARN("[renut] vblank : sub_82349E88 n'a pas le code attendu, pas de vblank du jeu");
    return bon;
  }();
  if (!bon_code) return;
  uint64_t args[] = {device};
  noyau->function_dispatcher()->ExecuteInterrupt(fil->thread_state(), kGestionnaireVblank, args, 1);
  g_vblanks.fetch_add(1, std::memory_order_relaxed);
}

// vblank_per_swap : une vblank par image présentée depuis la précédente, au plus tôt vblank_per_swap_min_us après
// elle ; une quand même après vblank_per_swap_timeout_ms sans image (chargements). Comme WaitSwapForVblank du SDK.
bool AttendreImage(uint64_t& derniere) {
  static auto precedente = std::chrono::steady_clock::now();
  const auto au_plus_tot = precedente + std::chrono::microseconds(REXCVAR_GET(vblank_per_swap_min_us));
  const auto limite = std::chrono::steady_clock::now() + std::chrono::milliseconds(REXCVAR_GET(vblank_per_swap_timeout_ms));
  while (g_actif.load(std::memory_order_acquire)) {
    const auto maintenant = std::chrono::steady_clock::now();
    const uint64_t images = opdarkness::ImagesPresentees();
    if ((images != derniere && maintenant >= au_plus_tot) || maintenant >= limite) {
      derniere = images;
      precedente = maintenant;
      return true;
    }
    if (images != derniere && au_plus_tot - maintenant > std::chrono::milliseconds(2))
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    else
      std::this_thread::yield();
  }
  return false;
}

int Boucle() {
  using Horloge = std::chrono::steady_clock;
  rex::system::X_VIDEO_MODE mode;
  rex::kernel::xboxkrnl::VdQueryVideoMode(&mode);
  const double hz = std::max(1.0, double(float(mode.refresh_rate)));
  const auto intervalle = std::chrono::duration_cast<Horloge::duration>(std::chrono::duration<double>(1.0 / hz));
  const bool par_image = rex::kernel::xboxkrnl::VblankPerSwap();
  REXLOG_INFO("[renut] vblank du jeu : {}", par_image ? "une par image présentée (vblank_per_swap)"
                                                       : fmt::format("minuterie à {:.2f} Hz", hz));
  uint64_t derniere = opdarkness::ImagesPresentees();
  auto prochaine = Horloge::now() + intervalle;
  while (g_actif.load(std::memory_order_acquire)) {
    if (par_image) {
      if (AttendreImage(derniere)) Vblank();
      continue;
    }
    const auto maintenant = Horloge::now();
    if (maintenant - prochaine > std::chrono::milliseconds(250)) prochaine = maintenant;
    while (maintenant >= prochaine) {
      Vblank();
      prochaine += intervalle;
    }
    const auto attente = std::chrono::duration_cast<std::chrono::microseconds>(prochaine - Horloge::now());
    rex::thread::Sleep(std::clamp(attente, std::chrono::microseconds(200), std::chrono::microseconds(50000)));
  }
  return 0;
}

}  // namespace

// Appelée à la création du device du titre (src/renut_engine/gpu/d3d/hooks/device.cpp).
void DemarrerVblank() {
  if (g_actif.exchange(true, std::memory_order_acq_rel)) return;
  auto* noyau = rex::system::kernel_state();
  if (!noyau) {
    g_actif.store(false, std::memory_order_release);
    return;
  }
  g_fil = rex::system::object_ref<rex::system::XHostThread>(
      new rex::system::XHostThread(noyau, 128 * 1024, 0, [] { return Boucle(); }));
  g_fil->set_name("Renut VBlank");
  g_fil->Create();
}

uint64_t Vblanks() { return g_vblanks.load(std::memory_order_relaxed); }

}  // namespace od::renut
