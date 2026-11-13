// opdarkness - XenosRecomp de reNut (out/renut-tiers/reblue-XenosRecomp, RENUT_RECOMP) compilé dans le jeu, pour la
// traduction des shaders par la console (docs/DEKO3D.md, « Traduction des shaders par la console »). Le rendu natif lie
// déjà le XenosRecomp de sa2-nx (mêmes noms de classes, autre code) : celui-ci est enfermé dans un espace de noms
// (src/od_xenosrecomp_renut.cpp) et n'est vu que par cette interface.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace opdarkness::xenosrecomp_renut {

// Ce que XenosRecomp écrit pour un shader dans le cache de reNut (main.cpp : ShaderCacheEntry, g_shaderVertexInputs,
// g_shaderLiterals), sauf le SPIR-V.
struct EntreeVertex {
  uint8_t usage, usage_index, fetch_index, format, flags, stride_dwords;
  uint16_t offset_dwords;
};

struct Shader {
  bool pixel = false;
  std::string hlsl;  // le prélude (shader_common.h) puis le shader, comme les fichiers du vidage de XenosRecomp
  uint32_t spec_constants_mask = 0;
  uint32_t vertex_fetch_memory_mask = 0;
  uint32_t texture_fetch_mask = 0;
  uint32_t mem_export_constants[8] = {};
  uint32_t float_constants[8] = {};
  uint32_t shader_flags = 0;
  std::vector<std::array<uint32_t, 5>> literaux;
  std::vector<EntreeVertex> entrees;
};

// Le conteneur commence-t-il ici (même vérification que main.cpp de XenosRecomp) ? Rend sa taille, 0 sinon.
size_t TailleConteneur(const uint8_t* donnees, size_t reste);

// Conteneur Xenos (gros-boutiste, tel que le jeu le donne) -> HLSL de reNut. `prelude` : shader_common.h de reNut.
// Un fil à la fois par appel (l'objet du traducteur est local à l'appel).
void Recompiler(const uint8_t* conteneur, std::string_view prelude, Shader& sortie);

// Le prélude de reNut (shader_common.h de son XenosRecomp), copié dans le .nro à la compilation.
std::string_view Prelude();

}  // namespace opdarkness::xenosrecomp_renut
