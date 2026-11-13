/**
 * @file    gpu/render/shader_library.h
 * @brief   The XenosRecomp shader cache at runtime, and capture of shaders it lacks.
 */
#pragma once

#include <cstdint>
#include <memory>

#include <plume_render_interface.h>

#include "shader_cache.h"

namespace renut::gpu {

// XXH3_64 of a whole guest shader container (virtual + physical), the key the
// cache and tools/dump_shaders.py use.
uint64_t HashShaderContainer(const uint8_t *container);

const ShaderCacheEntry *FindShaderEntry(uint64_t hash);

// opdarkness (2026-10-27, docs/DEKO3D.md « installation sur la console sans PC ») : entries the console made itself.
// FindShaderEntry looks in the built-in table first (none with od_renut_shaders_integres = false), then in these:
// renut_fiches.bin beside the NRO (kFichesMagic, read at the first lookup), then those added by
// AddRuntimeShaderEntry. The entry is copied (offsets unused, kShaderFlagRuntime set) with its inputs and literals;
// the pointer stays valid until exit. An entry already there (same hash) is kept and returned.
const ShaderCacheEntry *AddRuntimeShaderEntry(const ShaderCacheEntry &entry, const ShaderVertexInput *inputs,
                                              const ShaderLiteral *literals);
// opdarkness (2026-10-27, point 5): a shader the game made that no entry has (D3DX compiles a few during play). Called
// once per hash, from the game's thread, with its container (virtual + physical parts, guest order); the handler
// translates it in the background, adds its entry (AddRuntimeShaderEntry) and its GLSL, then calls
// RuntimeShaderReady. None set: the shader is only captured (CaptureMissingShader), as before.
using MissingShaderHandler = void (*)(const uint8_t *container, size_t size, bool pixel_shader, uint64_t hash);
void SetMissingShaderHandler(MissingShaderHandler handler);
// The entry of 'hash' exists now: the shader objects made without it take it, on the render thread, and the
// pipelines waiting for it are built (d3d/hooks/shader.cpp).
void RuntimeShaderReady(uint64_t hash);
// The built-in table only, whatever od_renut_shaders_integres says: for comparisons.
const ShaderCacheEntry *FindBuiltInShaderEntry(uint64_t hash);
// Entries added at run time or read from renut_fiches.bin.
size_t RuntimeShaderEntryCount();

// renut_fiches.bin, little endian: header {"ODFC", version 1, kShaderCacheLayout, sizeof(ShaderCacheEntry), count},
// then per shader its ShaderCacheEntry (offsets 0), ShaderVertexInput[vertexInputCount], ShaderLiteral[literalCount].
constexpr char kFichesMagic[4] = {'O', 'D', 'F', 'C'};
constexpr uint32_t kFichesVersion = 1;
constexpr const char *kFichesFile = "renut_fiches.bin";

// Builds the active backend's shader from a cache entry (decompressing the
// cache on first use). nullptr on failure.
std::unique_ptr<plume::RenderShader> CreateCachedShader(plume::RenderDevice *device,
                                                        const ShaderCacheEntry &entry);

// For a shader FindShaderEntry did not find (the game compiles a few at
// runtime, which no dump of its files has): writes its container to the
// capture directory (renut_shader_capture_dir), named as tools/dump_shaders.py
// names them, and logs it. The next build copies the captures into the shader
// dump directory and recompiles them into the cache. Once per hash.
void CaptureMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash);
// opdarkness: CaptureMissingShader, then the MissingShaderHandler (once per hash).
void HandleMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash);

// For the memory report: the decompressed shader cache kept in memory.
uint64_t ShaderCacheHostBytes();

// banjo-nx (banjo_gpu_bool_spec): the bools whose specialization constants (SpecId 1000 + N) the shader's SPIR-V
// declares, bit N of mask (8 words). All zero on D3D12, for a shader without, or one not in the cache.
void ShaderBoolSpecMask(const ShaderCacheEntry &entry, uint32_t mask[8]);

} // namespace renut::gpu
