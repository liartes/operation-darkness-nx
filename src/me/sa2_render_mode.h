// Sonic Adventure 2: render resolution per console mode and MSAA (sa2_render_height_handheld, sa2_render_height_docked,
// sa2_msaa; docs/enhancements.md "Render resolution and MSAA").
//
// The native renderer asks WantedScaleQuarters() once per Swap (ring thread) and, when it changed, rescales every
// render target and resolved texture at that frame boundary (sa2_native_targets.cpp, RescaleAll). On the Switch
// the mode is the console's (docked / handheld, Reverse-NX respected), read from libnx's cached operation mode, which
// the SDK's applet loop keeps current. On the PC it is simulated: sa2_render_docked picks the starting mode, the
// bind_sa2_docked key (F9) toggles it, and sa2_render_test_toggle_at / _every toggle it at presented swaps (tests).
#pragma once

#include <cstdint>

namespace sa2::render {

// The render scale for the current mode, in quarters of the game's 1280x720 (q = height / 180: 4 = 720p, 6 = 1080p).
// 4 when sa2_gpu_render_scale (or the non-CPU master) is off. Cheap: called once per Swap.
uint32_t WantedScaleQuarters();

// operation-darkness-nx (2026-10-22): reNut's render height in lines, the dynamic step's included (648: no multiple of
// 180, so not in WantedScaleQuarters, which rounds it).
int32_t WantedRenderHeight();

// The render scale of one mode (as WantedScaleQuarters, without the log line): the pipeline prewarm list keeps the
// line pipelines of both modes.
uint32_t ScaleQuartersOf(bool docked);

// The current mode (the console's on the Switch, the simulated one on the PC).
bool Docked();

// PC simulation of docking: flips the simulated mode (the hotkey). No effect on the Switch.
void ToggleDocked();

// Ring thread, after each presented swap: the PC test toggles (sa2_render_test_toggle_at / _every).
void OnPresented(uint64_t presented);

// operation-darkness-nx (sa2_render_dynamic): after each presented swap, the GPU busy time so far (milliseconds,
// cumulative, real time). When the GPU cannot keep 60 frames per second the render height drops to
// sa2_render_height_dynamic (540) and comes back when the full height would fit again (WantedScaleQuarters).
void NoteFrameGpu(double gpu_ms_total);

// Host samples per pixel for the game's single-sample render targets: 1 (the game's own, default) or 2 / 4
// (sa2_msaa with sa2_gpu_msaa on). Read once by the renderer when it starts.
uint32_t MsaaSamples();

// Counted in the enhancement report: a rescale (live switch or start-up) and the MSAA resolves.
void NoteRescale();
void NoteMsaaUse(uint64_t n = 1);

// PC: registers the dock-toggle key (bind_sa2_docked). Called once by the app after setup.
void RegisterKeys();

}  // namespace sa2::render
