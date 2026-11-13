// opdarkness - GLSL Vulkan de SPIRV-Cross -> GLSL qu'uam accepte, pour les shaders du jeu traduits par la console
// (docs/DEKO3D.md, « Traduction des shaders par la console »). Portage à l'identique de tools/renut/deko3d_glsl.py
// (banjo-nx ; le script dit pourquoi chaque transformation) dans le cas des shaders du jeu : --ubo-map 5:0,6:1,7:2 et
// --sampler-words handle. Vérifié contre le script par l'outil od_chaine_deko3d (src/outils/od_chaine_deko3d.cpp).
#pragma once

#include <string>
#include <string_view>

namespace opdarkness::deko3d_glsl {

// `vk` : sortie de SPIRV-Cross (--version 460 --vulkan-semantics --relax-nan-checks). Rend false avec la raison dans
// `erreur` quand le shader a ce que la conversion ne sait pas traiter (exception Unsupported du script).
bool Convertir(std::string_view vk, bool vertex, std::string& sortie, std::string& erreur);

}  // namespace opdarkness::deko3d_glsl
