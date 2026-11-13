/**
 * opdarkness (2026-10-20): sa2-nx's SDK hook (sdk/include/rex/input/read_hook.h of sa2-nx 1.0.3), which our SDK
 * (sa2-nx fdd6f14) does not have. Renut's late input and latency probe install it; Operation Darkness reads the pad
 * through its own interception of XInputGetState (sub_82399730, src/opdarkness_manette.cpp), which calls
 * od::renut::CallReadHook() (src/renut_engine/od_compat/od_compat.cpp).
 */
#pragma once

namespace rex::input {

using ReadHook = void (*)();
void SetReadHook(ReadHook hook);

}  // namespace rex::input

namespace od::renut {
void CallReadHook();
}  // namespace od::renut
