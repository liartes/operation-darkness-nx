// opdarkness - traduction des shaders au fil du jeu (od_traducteur.cpp, docs/SHADERS_EN_JEU.md).
#pragma once

#include <cstddef>
#include <cstdint>

namespace opdarkness::traducteur {

// Conteneur d'un shader que le jeu crée (tel que CreateVertexShader / CreatePixelShader le reçoivent) : s'il est
// inconnu du rendu natif, le traduit maintenant (fil appelant) et l'ajoute à la bibliothèque et au cache.
void AssurerShader(const uint8_t* conteneur, size_t taille);

}  // namespace opdarkness::traducteur
