// Sonic Adventure 2: render resolution per console mode and MSAA (see sa2_render_mode.h).
#include "sa2_render_mode.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/platform.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/switch_saltynx.h>

#include "cpu/sa2_enhancements.h"

#if REX_PLATFORM_SWITCH
#include <switch.h>
#endif

REXCVAR_DEFINE_INT32(sa2_render_height_handheld, 720, "SA2",
                     "Render height in handheld mode (the PC's starting mode unless sa2_render_docked): 720 = the "
                     "game's own 1280x720. Any multiple of 180 from 360 to 2160 (others are rounded to the nearest); "
                     "the width follows at 16:9")
    .range(180, 4320)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(sa2_render_height_docked, 1080, "SA2",
                     "Render height in docked mode (the TV): 1080 = 1920x1080. Any multiple of 180 from 360 to "
                     "2160; switching between docked and handheld changes it at the next frame")
    .range(180, 4320)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(sa2_render_docked, false, "SA2",
                    "PC only: start in the simulated docked mode (sa2_render_height_docked). The bind_sa2_docked key "
                    "(F9) switches between docked and handheld while the game runs, like docking the Switch. The "
                    "Switch itself always follows the console");
REXCVAR_DEFINE_INT32(sa2_msaa, 0, "SA2",
                     "Multisample anti-aliasing of the game's own (single-sample) render targets: 0 = off, as the "
                     "game and as before (default); 2 or 4 = that many samples per pixel, resolved when the game "
                     "copies the image. The game's 4x surfaces (Direct3D's fast Clear aliases) stay as they are. Read "
                     "at start")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(sa2_render_test_toggle_at, 0, "SA2",
                     "Test (PC): toggle the simulated docked mode after this presented swap (0 = never)")
    .range(0, 2000000000);
REXCVAR_DEFINE_INT32(sa2_render_test_toggle_every, 0, "SA2",
                     "Test (PC): after sa2_render_test_toggle_at, toggle again every this many presented swaps "
                     "(0 = once)")
    .range(0, 2000000000);

// Non-CPU group: the new behaviours have their own keys like every other change (docs/enhancements.md).
SA2_ENHANCEMENT(g_render_scale, sa2_gpu_render_scale, sa2::enh::kNonCpu, true,
                "GPU: render at sa2_render_height_handheld / sa2_render_height_docked (720 / 1080 by default), "
                "following docked / handheld live; off = always the game's 1280x720");
SA2_ENHANCEMENT(g_msaa, sa2_gpu_msaa, sa2::enh::kNonCpu, true,
                "GPU: honour sa2_msaa (2x / 4x host multisampling of the game's single-sample render targets, "
                "resolved at the game's copies); with sa2_msaa = 0 (default) nothing changes");

REXCVAR_DEFINE_BOOL(sa2_render_dynamic, false, "SA2",
                    "operation-darkness-nx: dynamic render height: drops to sa2_render_height_dynamic while the GPU "
                    "cannot keep 60 frames per second (the game's cinematics), back to the mode's height when it fits "
                    "again");
REXCVAR_DEFINE_DOUBLE(od_render_descente_ms, 0.0, "OpDarkness",
                      "Définition dynamique, sous la définition du mode : temps GPU par image à partir duquel on descend "
                      "encore d'un palier (0 : dès que le GPU est occupé à plus de 90 % sous 57 images/s). Le processeur "
                      "limite le combat vers 19 ms par image : une pointe un peu au-dessus coûte peu d'images/s");
REXCVAR_DEFINE_DOUBLE(od_render_montee_ms, 0.0, "OpDarkness",
                      "Définition dynamique, sous la définition du mode : remonte d'un palier (jamais vers celle du mode) "
                      "si le temps GPU prévu y reste sous cette valeur (0 : règle des 80 % d'une image à 60 Hz)");
REXCVAR_DEFINE_INT32(sa2_render_height_dynamic_first, 0, "SA2",
                     "operation-darkness-nx (2026-10-22): a first step down above sa2_render_height_dynamic (648: 9/10 "
                     "of 720p; reNut only, the native renderer scales by multiples of 180 lines; 0 = none)");
REXCVAR_DEFINE_INT32(sa2_render_height_dynamic, 540, "SA2",
                     "operation-darkness-nx: first step down of the dynamic render height (sa2_render_dynamic)");
REXCVAR_DEFINE_INT32(sa2_render_height_dynamic_min, 360, "SA2",
                     "operation-darkness-nx: second step down, when the first one is still too heavy (0 or not below "
                     "the first step = no second step)");

namespace sa2::render {
namespace {

std::atomic<bool> g_docked_pc{false};
std::atomic<bool> g_docked_pc_set{false};
std::atomic<uint32_t> g_last_logged_q{0};

// q = height / 180, from 2 (360p) to 12 (2160p); other heights go to the nearest multiple of 180.
uint32_t QuartersOf(int32_t height) {
  const int32_t q = (std::clamp(height, 360, 2160) + 90) / 180;
  return uint32_t(std::clamp(q, 2, 12));
}

bool DockedPc() {
  if (!g_docked_pc_set.load(std::memory_order_acquire)) {
    bool expected = false;
    if (g_docked_pc_set.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
      g_docked_pc.store(REXCVAR_GET(sa2_render_docked), std::memory_order_release);
  }
  return g_docked_pc.load(std::memory_order_acquire);
}

}  // namespace

bool Docked() {
#if REX_PLATFORM_SWITCH
  // libnx keeps the operation mode cached; the SDK's applet loop (windowed_app_context_switch.cpp) processes the
  // OperationModeChanged message that updates it. Reverse-NX's "fake" mode is obeyed, as the window does.
  return rex::ui::switch_saltynx::BaseMode(appletGetOperationMode() == AppletOperationMode_Console);
#else
  return DockedPc();
#endif
}

// operation-darkness-nx: dynamic render height (sa2_render_dynamic), in steps: the mode's height, then
// sa2_render_height_dynamic (540), then sa2_render_height_dynamic_min (360). Windows of 30 presented frames: a window is
// "overloaded" below 57 frames per second with the GPU busy more than 90 % of the wall time (a CPU-bound slowdown, a
// loading screen for instance, does not count): one step down. "Fits" when the GPU time per frame, scaled by the area
// ratio of the step above, would use less than 80 % of a 60 Hz frame (an overestimate: not every pass scales with the
// area). Per frame, not per wall time: a window slowed by something else (a hitch, a loading) leaves the GPU idle
// without making room (2026-10-16 on the console: back to 720p at 24.7 frames/s with the GPU 50 % busy at 540p, then
// 46 frames/s). Two such windows in a row, one step up. At least 2 s at a height between changes. The game's logic advances
// one step per frame: smoothness first, so that sound and picture stay together.
namespace {
// operation-darkness-nx (2026-10-22): heights in lines (648 is no multiple of 180).
std::atomic<int32_t> g_dyn_h{0};  // 0 = the mode's height, else the current step's height in lines
double g_dyn_window_gpu = 0.0, g_dyn_last_change = -1e9;
std::chrono::steady_clock::time_point g_dyn_window_start;
uint32_t g_dyn_frames = 0, g_dyn_fits = 0;
// operation-darkness-nx (2026-10-22): windows that must fit in a row before a step up, doubled each time a step up is
// undone within kDynRetreatS seconds (a lighter view passed, the heavy one came back: 360p <-> 540p every few seconds
// in combat with reNut), back to 2 once a step up has held kDynHoldS seconds.
uint32_t g_dyn_need = 2;
double g_dyn_last_up = -1e9;
constexpr double kDynRetreatS = 10.0, kDynHoldS = 30.0;
constexpr uint32_t kDynNeedMax = 32;
bool g_dyn_started = false;
}  // namespace

void NoteFrameGpu(double gpu_ms_total) {
  if (!REXCVAR_GET(sa2_render_dynamic) || !g_render_scale.active) return;
  const auto now = std::chrono::steady_clock::now();
  if (!g_dyn_started) {
    g_dyn_started = true;
    g_dyn_window_start = now;
    g_dyn_window_gpu = gpu_ms_total;
    return;
  }
  // Shorter windows (15 frames) while at the full height, so a cinematic drops quickly; 30 frames otherwise.
  const uint32_t window = g_dyn_h.load(std::memory_order_relaxed) ? 30u : 15u;
  if (++g_dyn_frames < window) return;
  const double wall_ms = std::chrono::duration<double, std::milli>(now - g_dyn_window_start).count();
  const double gpu_ms = gpu_ms_total - g_dyn_window_gpu;
  const double fps = wall_ms > 0 ? 1000.0 * g_dyn_frames / wall_ms : 60.0;
  const double busy = wall_ms > 0 ? gpu_ms / wall_ms : 0.0;
  const double gpu_per_frame_ms = gpu_ms / g_dyn_frames;
  g_dyn_frames = 0;
  g_dyn_window_start = now;
  g_dyn_window_gpu = gpu_ms_total;
  const double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
  if (seconds - g_dyn_last_change < 2.0) return;
  const bool docked = Docked();
  const int32_t full =
      std::clamp<int32_t>(docked ? REXCVAR_GET(sa2_render_height_docked) : REXCVAR_GET(sa2_render_height_handheld), 360, 2160);
  // The steps below the mode's height, highest first.
  int32_t steps[3];
  uint32_t n = 0;
  for (const int32_t h : {REXCVAR_GET(sa2_render_height_dynamic_first), REXCVAR_GET(sa2_render_height_dynamic),
                          REXCVAR_GET(sa2_render_height_dynamic_min)}) {
    if (h <= 0) continue;
    if (h < (n ? steps[n - 1] : full)) steps[n++] = h;
  }
  if (!n) return;
  const int32_t current = g_dyn_h.load(std::memory_order_relaxed);
  const int32_t cur_h = current ? current : full;
  uint32_t index = 0;  // 0 = full, i + 1 = steps[i]
  for (uint32_t i = 0; i < n; ++i)
    if (steps[i] == cur_h) index = i + 1;
  if (g_dyn_need > 2 && seconds - g_dyn_last_up >= kDynHoldS && g_dyn_last_up > g_dyn_last_change - 0.001) {
    g_dyn_need = 2;  // the last step up has held
    REXLOG_INFO("[res] dynamic: the last step up held {:.0f} s, steps up after 2 windows again", kDynHoldS);
  }
  const auto set = [&](uint32_t new_index, const char* why) {
    const int32_t h = new_index ? steps[new_index - 1] : full;
    if (new_index < index) {
      g_dyn_last_up = seconds;
    } else if (seconds - g_dyn_last_up < kDynRetreatS) {
      g_dyn_need = std::min(g_dyn_need * 2, kDynNeedMax);  // the step up did not hold
      g_dyn_last_up = -1e9;
    }
    g_dyn_h.store(new_index ? h : 0, std::memory_order_relaxed);
    g_dyn_last_change = seconds;
    g_dyn_fits = 0;
    REXLOG_INFO("[res] dynamic: {} ({:.1f} frames/s, GPU busy {:.0f} %, {:.1f} ms per frame at {}p): {}p (step up after "
                "{} windows)", why, fps, busy * 100.0, gpu_per_frame_ms, cur_h, h, g_dyn_need);
  };
  // opdarkness (2026-11-03, console, 720p / 540p / 360p tour à tour dans les mêmes combats) : à 450-550 dessins, 41 / 52 /
  // 55 images/s pour 24 / 17 / 10 ms de GPU : le processeur limite vers 19 ms, le 540p ne coûte presque rien, le 720p si.
  // Les fenêtres à 17-20 ms faisaient descendre (55 changements en 20 min, combat surtout en 360-480p).
  static const double descente_ms = REXCVAR_GET(od_render_descente_ms);
  static const double montee_ms = REXCVAR_GET(od_render_montee_ms);
  if (fps < 57.0 && busy > 0.90 && (index == 0 || descente_ms <= 0.0 || gpu_per_frame_ms >= descente_ms)) {
    if (index < n) set(index + 1, "GPU overloaded");
    return;
  }
  if (index == 0) return;
  const int32_t above = index >= 2 ? steps[index - 2] : full;
  const double ratio = double(above) * double(above) / (double(cur_h) * double(cur_h));
  // Le même relevé : environ 3 ms par image ne dépendent pas de la définition (présentation, lignes et cibles fixes).
  constexpr double kPartFixeMs = 3.0;
  const bool place =
      index >= 2 && montee_ms > 0.0
          ? kPartFixeMs + std::max(gpu_per_frame_ms - kPartFixeMs, 0.0) * ratio < montee_ms
          : gpu_per_frame_ms * ratio < 0.80 * 1000.0 / 60.0;
  g_dyn_fits = place ? g_dyn_fits + 1 : 0;
  if (g_dyn_fits >= g_dyn_need) set(index - 1, "room on the GPU");
}

uint32_t WantedScaleQuarters() {
  if (!g_render_scale.active) return 4;
  const bool docked = Docked();
  const int32_t height = docked ? REXCVAR_GET(sa2_render_height_docked) : REXCVAR_GET(sa2_render_height_handheld);
  uint32_t q = QuartersOf(height);
  if (const int32_t dyn = g_dyn_h.load(std::memory_order_relaxed)) q = std::min(q, QuartersOf(dyn));
  if (g_last_logged_q.exchange(q | (docked ? 0x100u : 0u), std::memory_order_relaxed) != (q | (docked ? 0x100u : 0u))) {
    REXLOG_INFO("[res] {} mode: render {}x{} (sa2_render_height_{} = {}{})", docked ? "docked" : "handheld",
                320 * q, 180 * q, docked ? "docked" : "handheld", height,
                int32_t(180 * q) != height ? fmt::format(", rounded to a multiple of 180") : std::string());
  }
  return q;
}

// operation-darkness-nx (2026-10-22): reNut's render height in lines (render/surfaces.cpp): the mode's, or the dynamic
// step's when lower (648 included).
int32_t WantedRenderHeight() {
  const bool docked = Docked();
  int32_t height = docked ? REXCVAR_GET(sa2_render_height_docked) : REXCVAR_GET(sa2_render_height_handheld);
  if (const int32_t dyn = g_dyn_h.load(std::memory_order_relaxed)) height = std::min(height, dyn);
  return height;
}

uint32_t ScaleQuartersOf(bool docked) {
  if (!g_render_scale.active) return 4;
  return QuartersOf(docked ? REXCVAR_GET(sa2_render_height_docked) : REXCVAR_GET(sa2_render_height_handheld));
}

void ToggleDocked() {
#if !REX_PLATFORM_SWITCH
  const bool docked = !DockedPc();
  g_docked_pc.store(docked, std::memory_order_release);
  REXLOG_INFO("[res] simulated {} (PC): render height {}", docked ? "docking" : "undocking",
              docked ? REXCVAR_GET(sa2_render_height_docked) : REXCVAR_GET(sa2_render_height_handheld));
#endif
}

void OnPresented(uint64_t presented) {
#if !REX_PLATFORM_SWITCH
  const int32_t at = REXCVAR_GET(sa2_render_test_toggle_at);
  if (at <= 0 || presented < uint64_t(at)) return;
  const int32_t every = REXCVAR_GET(sa2_render_test_toggle_every);
  if (presented == uint64_t(at) || (every > 0 && (presented - uint64_t(at)) % uint64_t(every) == 0)) {
    REXLOG_INFO("[res] test toggle at presented swap {}", presented);
    ToggleDocked();
  }
#else
  (void)presented;
#endif
}

uint32_t MsaaSamples() {
  if (!g_msaa.active) return 1;
  const int32_t samples = REXCVAR_GET(sa2_msaa);
  return samples >= 4 ? 4u : samples >= 2 ? 2u : 1u;
}

void NoteRescale() { g_render_scale.Use(); }
void NoteMsaaUse(uint64_t n) { g_msaa.UseSampled(); (void)n; }

void RegisterKeys() {
#if !REX_PLATFORM_SWITCH
  rex::ui::RegisterBind("bind_sa2_docked", "F9",
                        "Switch between the simulated docked and handheld modes (render resolution)",
                        [] { ToggleDocked(); });
#endif
}

}  // namespace sa2::render
