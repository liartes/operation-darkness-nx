// opdarkness (2026-10-20): sa2-nx's lockstep captures through Renut's frame (app/src/sa2_capture.h). Operation
// Darkness takes its captures elsewhere (src/od_ecrans.cpp, tools/captures_images.sh): none here yet.
#pragma once

#include <cstdint>

namespace rex::ui {
struct RawImage;
}  // namespace rex::ui

namespace sa2::capture {
inline bool RenutLockstepOn() { return false; }
inline void RenutLockstepFrame(uint64_t, const rex::ui::RawImage*) {}
}  // namespace sa2::capture
