// opdarkness - réparation d'une variante de vertex shader (od_reparation_variantes.cpp, généré depuis sa2-nx).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace opdarkness {

// modele : le conteneur du shader de la bibliothèque ; chargee : le même conteneur avec le microcode tel que Direct3D
// l'a réécrit pour une déclaration de sommets. Si la réécriture n'est qu'une permutation prouvée des lectures de
// sommets, sortie = le conteneur réparé (déclaration ajustée) et true ; sinon false et la raison.
bool ReparerVariante(const std::vector<uint8_t>& modele, const std::vector<uint8_t>& chargee,
                     std::vector<uint8_t>& sortie, std::string& raison);

}  // namespace opdarkness
