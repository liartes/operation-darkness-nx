// opdarkness - retouches du HLSL de reNut que le paquet du PC fait entre les deux passages de XenosRecomp (gpu.cmake),
// reprises pour la traduction par la console (docs/DEKO3D.md, « installation sur la console sans PC », point 4) :
//   - flou de profondeur de champ allégé (tools/renut/od_flou_profondeur.py, OD_RENUT_FLOU : 1 par défaut) ;
//   - ombres portées arrêtées hors de la zone de la carte d'ombres (tools/renut/od_ombres.py ; od_renut_ombres_decoupe
//     choisit le mode au dessin).
// Le même texte que les scripts, octet pour octet (vérifié par l'outil PC od_chaine_deko3d --retouches).
#pragma once

#include <cstdint>
#include <string>

namespace opdarkness::retouches {

struct Reglages {
  int flou = 1;  // 0 : le flou du jeu ; 1 : allégé ; 2 : comparaison (moitié gauche du jeu, droite allégée)
  bool ombres = true;
};

// Le shader `nom` (vs_/ps_<empreinte en minuscules>, comme les fichiers du vidage) : son HLSL (prélude compris, tel que
// XenosRecomp le rend) retouché en place. Rend le nombre de retouches faites.
int Appliquer(const std::string& nom, std::string& hlsl, const Reglages& reglages);

}  // namespace opdarkness::retouches
