// opdarkness - captures d'écran aux instants donnés (--od_ecrans, od_ecrans.cpp).
#pragma once

#include <functional>

namespace rex::ui {
class Presenter;
struct RawImage;
}

namespace opdarkness::ecrans {

void Demarrer(std::function<rex::ui::Presenter*()> presentateur);
// La même chose avec une autre source de l'image présentée (rendu reNut : renut::gpu::CaptureFrame).
void DemarrerAvecSource(std::function<bool(rex::ui::RawImage&)> source);

}  // namespace opdarkness::ecrans
