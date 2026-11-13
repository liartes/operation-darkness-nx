// opdarkness (2026-10-20): diagnostic of the layout transitions that cost a barrier each (od_renut_diag_barrieres):
// on the console, combat drew ~440 deko3d barriers invalidating the texture cache a frame (one per draw). Called by
// TransitionSurfaceLocked / TransitionTextureLocked (render/surfaces.cpp, render/textures.cpp) under the renderer's
// mutex; every 10 s the most frequent (kind, guest address, size, from -> to) go to the log.
#pragma once

#include <cstdint>

namespace od::renut {
bool DiagBarrieresOn();
void NoteBarriere(const char *kind, uint32_t guest_va, uint32_t width, uint32_t height, int from, int to);
}  // namespace od::renut
