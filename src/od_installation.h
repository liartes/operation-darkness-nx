// opdarkness - installation des shaders du jeu par la console, sans PC (docs/DEKO3D.md, « Prochaine session :
// installation sur la console sans PC », points 1 à 3) : conteneurs des fichiers du jeu (od_conteneurs.h), traduits
// jusqu'au GLSL pour uam (od_chaine_deko3d.h), puis deux fichiers à côté du .nro que reNut lit au lieu de ce qui est
// compilé dans l'exécutable :
//   renut_fiches.bin  la table des shaders (masques, entrées de sommets, littéraux : render/shader_library.h) ;
//   deko3d_glsl.bin   le paquet GLSL (format BDKG de banjo_renut_deko3d.cpp, trames zstd sans dictionnaire).
// Le DKSH (uam) reste fait au fil du jeu, par variante, comme avec le paquet intégré (cache deko3d_shaders.bin).
// Sans les retouches du paquet du PC (flou, ombres, early alpha, passes de banjo-nx : point 4, à venir).
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

namespace rex::ui {
class ImGuiDrawer;
}

namespace opdarkness::installation {

// Après le chargement de default.xex (OnPostLoadXexImage), avant le démarrage du jeu : l'installation si
// --od_installer vaut « toujours » ou « si_absent » (un des deux fichiers manque ; essai : sans écran) ; avec od_installer_verif, chaque fiche est comparée
// à la table compilée dans l'exécutable (journal « [installation] écart »). Le travail se fait sur un fil à lui ; le fil
// appelant (celui de l'interface) dessine pendant ce temps l'écran d'installation, une fenêtre de `imgui` présentée par
// `presenter` (renut::gpu::PresentOverlayNow) toutes les 33 ms ; sans eux, rien n'est affiché.
void AuChargement(const uint8_t* image, size_t taille_image, const std::filesystem::path& racine_jeu,
                  rex::ui::ImGuiDrawer* imgui = nullptr, const std::function<void()>& presenter = {});

}  // namespace opdarkness::installation
