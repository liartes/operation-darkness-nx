// opdarkness (2026-10-20): what Renut's renderer, as sa2-nx 1.0.3 carries it, expects from sa2-nx's newer SDK and app
// and that Operation Darkness's (sa2-nx fdd6f14) does not have.
#include <atomic>
#include <cstdint>

#include <rex/cvar.h>

#include "rex/input/read_hook.h"

REXCVAR_DECLARE(int32_t, sa2_render_height_docked);    // src/me/sa2_render_mode.cpp
REXCVAR_DECLARE(int32_t, sa2_render_height_handheld);  // likewise

namespace sa2::render {
// sa2-nx 1.0.3.1 (app/src/me/sa2_render_mode.cpp).
int32_t RenderHeightOf(bool docked) {
  return docked ? REXCVAR_GET(sa2_render_height_docked) : REXCVAR_GET(sa2_render_height_handheld);
}
}  // namespace sa2::render

// sa2-nx's game frame count (its src/sa2_d3d_trace.cpp): the Swaps Renut's Swap hook has seen.
std::atomic<uint64_t> g_sa2_game_frames{0};

namespace {
std::atomic<rex::input::ReadHook> g_read_hook{nullptr};
}  // namespace

void rex::input::SetReadHook(ReadHook hook) {
  g_read_hook.store(hook, std::memory_order_release);
}

void od::renut::CallReadHook() {
  if (const rex::input::ReadHook hook = g_read_hook.load(std::memory_order_acquire))
    hook();
}

// Whether the generated code marks the store cards (banjo-nx's tools/store_cards.py; sa2-nx's src/cpu/sa2_hot_guest.cpp
// sees its generated header): not Operation Darkness's, so Renut hashes its cached copies instead.
extern "C" const bool g_banjo_store_cards = false;

// sa2-nx's core layout (its SDK's system/xthread.cpp, sa2_renut_core_layout: SA2's main thread is its drawing thread):
// our SDK has no per-thread layout applied at waits, and Operation Darkness draws on vmsDrawThread, not the main
// thread. Nothing moved (0 threads).
extern "C" uint32_t RexSetGuestThreadCoreLayout(int, uint32_t, int) { return 0; }

// opdarkness (2026-10-21) : barrière complète avant chaque dessin de deko3d (diagnostic des courses sur le GPU).
#if !defined(PLUME_DEKO3D)
bool g_odBarriereUnique = false;  // sans deko3d : sans effet
unsigned g_odDessinsAbandonnes = 0;
#endif
#if defined(PLUME_DEKO3D)
extern bool g_odBarriereTotale;
REXCVAR_DEFINE_BOOL(od_renut_diag_barriere_totale, false, "OpDarkness",
                    "Diagnostic deko3d : barrière complète (attente du travail précédent, caches vidés) avant chaque "
                    "dessin")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
bool OdBarriereTotaleLue() {
  g_odBarriereTotale = REXCVAR_GET(od_renut_diag_barriere_totale);
  return g_odBarriereTotale;
}
#endif
