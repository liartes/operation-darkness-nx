// opdarkness - la table des shaders de reNut sans aucun shader du jeu, pour un .nro sans donnée du jeu
// (OD_RENUT_SANS_DONNEES, cmake/rendu_renut.cmake) : la console fait la sienne à l'installation (renut_fiches.bin et
// deko3d_glsl.bin à côté du .nro, src/od_installation.cpp). Mêmes symboles que tools/renut/renut_switch_shader_cache.py.
#include <cstddef>
#include <cstdint>

#include "renut_engine/gpu/shaders/shader_cache.h"

ShaderCacheEntry g_shaderCacheEntries[1] = {};
extern const size_t g_shaderCacheEntryCount = 0;
ShaderVertexInput g_shaderVertexInputs[1] = {};
ShaderLiteral g_shaderLiterals[1] = {};
extern const uint32_t g_shaderCacheLayout = kShaderCacheLayout;

extern const uint8_t g_compressedDxilCache[] = {0};
extern const size_t g_dxilCacheCompressedSize = 0;
extern const size_t g_dxilCacheDecompressedSize = 0;
extern const uint8_t g_compressedSpirvCache[] = {0};
extern const size_t g_spirvCacheCompressedSize = 0;
extern const size_t g_spirvCacheDecompressedSize = 0;
