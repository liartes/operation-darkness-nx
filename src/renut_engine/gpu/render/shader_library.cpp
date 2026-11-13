/**
 * @file    gpu/render/shader_library.cpp
 */
#include "renut_engine/gpu/render/shader_library.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <smolv.h>
#include <xxhash.h>
#include <zstd.h>

#include "renut_engine/gpu/backend/backend.h"
#include "renut_engine/gpu/d3d/d3d.h"
#include "renut_engine/gpu/log.h"
#include "renut_engine/gpu/banjo_renut_tuning.h"
#if defined(PLUME_DEKO3D)
#include "renut_engine/gpu/banjo_renut_deko3d.h"  // banjo-nx (2026-10-17): the deko3d back end's GLSL package
#endif

REXCVAR_DEFINE_STRING(renut_shader_capture_dir, "shader_capture", "Nuts&Bolts/Graphics",
                      "Where shaders missing from the shader cache are written; the next build "
                      "recompiles them into the cache");

REXCVAR_DEFINE_BOOL(od_renut_shaders_integres, true, "OpDarkness",
                    "reNut : la table des shaders et le paquet GLSL compilés dans l'exécutable ; false : seulement ceux "
                    "que la console a faits (renut_fiches.bin et deko3d_glsl.bin à côté du .nro, docs/DEKO3D.md)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace renut::gpu {

namespace {

struct DecompressedCache {
  std::once_flag once;
  std::vector<uint8_t> bytes;
};

DecompressedCache g_spirv;
#if defined(_WIN32)
DecompressedCache g_dxil;
#endif
std::atomic<uint64_t> g_decompressed_bytes{0};  // ShaderCacheHostBytes

const uint8_t *Decompress(DecompressedCache &cache, const uint8_t *compressed,
                          size_t compressed_size, size_t decompressed_size, const char *name) {
  std::call_once(cache.once, [&] {
    cache.bytes.resize(decompressed_size);
    const size_t n = ZSTD_decompress(cache.bytes.data(), decompressed_size, compressed,
                                     compressed_size);
    if (ZSTD_isError(n) || n != decompressed_size) {
      RGPU_ERROR("{} shader cache decompression failed", name);
      cache.bytes.clear();
      cache.bytes.shrink_to_fit();
    }
    g_decompressed_bytes.fetch_add(cache.bytes.capacity(), std::memory_order_relaxed);
  });
  return cache.bytes.empty() ? nullptr : cache.bytes.data();
}

std::mutex g_capture_mutex;
std::unordered_set<uint64_t> g_captured;  // CaptureMissingShader

// opdarkness (2026-10-27): entries made at run time (AddRuntimeShaderEntry, renut_fiches.bin).
struct RuntimeRecord {
  RuntimeShaderEntry runtime;
  std::vector<ShaderVertexInput> inputs;
  std::vector<ShaderLiteral> literals;
};
std::atomic<MissingShaderHandler> g_missing_handler{nullptr};
struct RuntimeEntries {
  std::once_flag once;  // renut_fiches.bin read
  std::mutex mutex;
  std::deque<RuntimeRecord> records;  // stable addresses
  std::unordered_map<uint64_t, const ShaderCacheEntry *> by_hash;
};
RuntimeEntries g_runtime;

const ShaderCacheEntry *AddLocked(RuntimeEntries &r, const ShaderCacheEntry &entry, const ShaderVertexInput *inputs,
                                  const ShaderLiteral *literals) {
  auto it = r.by_hash.find(entry.hash);
  if (it != r.by_hash.end())
    return it->second;
  RuntimeRecord &rec = r.records.emplace_back();
  rec.inputs.assign(inputs, inputs + entry.vertexInputCount);
  rec.literals.assign(literals, literals + entry.literalCount);
  rec.runtime.entry = entry;
  rec.runtime.entry.vertexInputOffset = 0;
  rec.runtime.entry.literalOffset = 0;
  rec.runtime.entry.shaderFlags |= kShaderFlagRuntime;
  rec.runtime.inputs = rec.inputs.data();
  rec.runtime.literals = rec.literals.data();
  r.by_hash.emplace(entry.hash, &rec.runtime.entry);
  return &rec.runtime.entry;
}

void LoadFiches(RuntimeEntries &r) {
  const std::filesystem::path path = banjo::renut_gpu::NextToExecutable(kFichesFile);
  FILE *f = std::fopen(path.string().c_str(), "rb");
  if (!f)
    return;
  std::vector<uint8_t> data;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n > 0) {
    data.resize(size_t(n));
    if (std::fread(data.data(), 1, data.size(), f) != data.size())
      data.clear();
  }
  std::fclose(f);
  uint32_t head[5] = {};
  if (data.size() < sizeof(head) || (std::memcpy(head, data.data(), sizeof(head)), std::memcmp(head, kFichesMagic, 4)) ||
      head[1] != kFichesVersion || head[2] != kShaderCacheLayout || head[3] != sizeof(ShaderCacheEntry)) {
    RGPU_ERROR("{}: not a shader table this build reads (made by another version: install again)", path.string());
    return;
  }
  size_t at = sizeof(head);
  std::lock_guard lock(r.mutex);
  for (uint32_t i = 0; i < head[4]; ++i) {
    ShaderCacheEntry e;
    if (at + sizeof(e) > data.size())
      break;
    std::memcpy(&e, data.data() + at, sizeof(e));
    at += sizeof(e);
    const size_t inputs = size_t(e.vertexInputCount) * sizeof(ShaderVertexInput);
    const size_t literals = size_t(e.literalCount) * sizeof(ShaderLiteral);
    if (at + inputs + literals > data.size()) {
      RGPU_ERROR("{}: cut short at shader {} of {}", path.string(), i, head[4]);
      break;
    }
    std::vector<ShaderVertexInput> in(e.vertexInputCount);
    std::vector<ShaderLiteral> lit(e.literalCount);
    std::memcpy(in.data(), data.data() + at, inputs);
    std::memcpy(lit.data(), data.data() + at + inputs, literals);
    at += inputs + literals;
    e.shaderFlags &= ~kShaderFlagRuntime;
    AddLocked(r, e, in.data(), lit.data());
  }
  RGPU_INFO("Shader table {}: {} shaders", path.string(), r.records.size());
}

RuntimeEntries &Runtime() {
  std::call_once(g_runtime.once, [] { LoadFiches(g_runtime); });
  return g_runtime;
}

} // namespace

uint64_t HashShaderContainer(const uint8_t *container) {
  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  return XXH3_64bits(container, size);
}

const ShaderCacheEntry *FindShaderEntry(uint64_t hash) {
  if (REXCVAR_GET(od_renut_shaders_integres))
    if (const ShaderCacheEntry *e = FindBuiltInShaderEntry(hash))
      return e;
  RuntimeEntries &r = Runtime();
  std::lock_guard lock(r.mutex);
  auto it = r.by_hash.find(hash);
  return it != r.by_hash.end() ? it->second : nullptr;
}

const ShaderCacheEntry *AddRuntimeShaderEntry(const ShaderCacheEntry &entry, const ShaderVertexInput *inputs,
                                              const ShaderLiteral *literals) {
  RuntimeEntries &r = Runtime();
  std::lock_guard lock(r.mutex);
  return AddLocked(r, entry, inputs, literals);
}

void SetMissingShaderHandler(MissingShaderHandler handler) {
  g_missing_handler.store(handler, std::memory_order_release);
}

size_t RuntimeShaderEntryCount() {
  RuntimeEntries &r = Runtime();
  std::lock_guard lock(r.mutex);
  return r.records.size();
}

const ShaderCacheEntry *FindBuiltInShaderEntry(uint64_t hash) {
  if (g_shaderCacheLayout != kShaderCacheLayout) {
    static bool logged = false;
    if (!logged) {
      RGPU_ERROR("Shader cache layout {} does not match the renderer's {}; rebuild it",
                 g_shaderCacheLayout, kShaderCacheLayout);
      logged = true;
    }
    return nullptr;
  }
  const ShaderCacheEntry *begin = g_shaderCacheEntries;
  const ShaderCacheEntry *end = begin + g_shaderCacheEntryCount;
  const ShaderCacheEntry *it = std::lower_bound(
      begin, end, hash, [](const ShaderCacheEntry &e, uint64_t h) { return e.hash < h; });
  return (it != end && it->hash == hash) ? it : nullptr;
}

void ShaderBoolSpecMask(const ShaderCacheEntry &entry, uint32_t mask[8]) {
  std::fill(mask, mask + 8, 0u);
#if defined(PLUME_DEKO3D)
  // banjo-nx: the GLSL package lists them (the SPIR-V cache need not be unpacked for deko3d).
  if (IsDeko3D()) {
    banjo::renut_gpu::deko3d::BoolSpecMask(entry.hash, mask);
    return;
  }
#endif
  if (!IsVulkan() || entry.spirvSize == 0)
    return;
  const uint8_t *cache = Decompress(g_spirv, g_compressedSpirvCache, g_spirvCacheCompressedSize,
                                    g_spirvCacheDecompressedSize, "SPIR-V");
  if (!cache)
    return;
  const uint8_t *smol = cache + entry.spirvOffset;
  std::vector<uint8_t> spirv(smolv::GetDecodedBufferSize(smol, entry.spirvSize));
  if (spirv.size() < 20 || !smolv::Decode(smol, entry.spirvSize, spirv.data(), spirv.size()))
    return;
  // After the five header words, each instruction's first word is (word count << 16) | opcode. OpDecorate (71):
  // target, decoration, literals; SpecId is decoration 1.
  const auto *words = reinterpret_cast<const uint32_t *>(spirv.data());
  const size_t count = spirv.size() / 4;
  for (size_t i = 5; i < count;) {
    const uint32_t length = words[i] >> 16, opcode = words[i] & 0xFFFF;
    if (length == 0)
      break;
    if (opcode == 71 && length >= 4 && i + 3 < count && words[i + 2] == 1) {
      const uint32_t id = words[i + 3];
      if (id >= 1000 && id < 1256)
        mask[(id - 1000) / 32] |= 1u << ((id - 1000) % 32);
    }
    i += length;
  }
}

std::unique_ptr<plume::RenderShader> CreateCachedShader(plume::RenderDevice *device,
                                                        const ShaderCacheEntry &entry) {
  if (!device)
    return nullptr;

#if defined(PLUME_DEKO3D)
  // banjo-nx (2026-10-17): the deko3d back end's GLSL for uam, from the package in the NRO.
  if (IsDeko3D())
    return banjo::renut_gpu::deko3d::CreateGameShader(device, entry.hash);
#endif

  if (IsVulkan()) {
    const uint8_t *cache = Decompress(g_spirv, g_compressedSpirvCache, g_spirvCacheCompressedSize,
                                      g_spirvCacheDecompressedSize, "SPIR-V");
    if (!cache || entry.spirvSize == 0)
      return nullptr;
    const uint8_t *smol = cache + entry.spirvOffset;
    std::vector<uint8_t> spirv(smolv::GetDecodedBufferSize(smol, entry.spirvSize));
    if (spirv.empty() || !smolv::Decode(smol, entry.spirvSize, spirv.data(), spirv.size())) {
      RGPU_ERROR("SPIR-V decode failed for shader {:016X}", entry.hash);
      return nullptr;
    }
    return device->createShader(spirv.data(), spirv.size(), "main",
                                plume::RenderShaderFormat::SPIRV);
  }

#if defined(_WIN32)
  const uint8_t *cache = Decompress(g_dxil, g_compressedDxilCache, g_dxilCacheCompressedSize,
                                    g_dxilCacheDecompressedSize, "DXIL");
  if (!cache || entry.dxilSize == 0)
    return nullptr;
  return device->createShader(cache + entry.dxilOffset, entry.dxilSize, "main",
                              plume::RenderShaderFormat::DXIL);
#else
  return nullptr;
#endif
}

uint64_t ShaderCacheHostBytes() {
  return g_decompressed_bytes.load(std::memory_order_relaxed);
}

void CaptureMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash) {
  std::lock_guard lock(g_capture_mutex);
  if (!g_captured.insert(hash).second)
    return;

  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  const size_t size = size_t(u32(header->virtualSize)) + u32(header->physicalSize);
  // banjo-nx: next to the executable (on the Switch, the NRO's folder), not the working directory.
  const std::filesystem::path dir = banjo::renut_gpu::NextToExecutable(REXCVAR_GET(renut_shader_capture_dir));
  const std::filesystem::path path =
      dir / fmt::format("{}_{:016x}.bin", pixel_shader ? "ps" : "vs", hash);

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::ofstream out(path, std::ios::binary);
  if (out)
    out.write(reinterpret_cast<const char *>(container), std::streamsize(size));
  if (!out) {
    RGPU_WARN("Shader {:016X} is not in the shader cache and could not be captured to {}", hash,
              path.string());
    return;
  }
  RGPU_WARN("Shader {:016X} is not in the shader cache: captured to {}, the next build adds it",
            hash, path.string());
}

void HandleMissingShader(const uint8_t *container, bool pixel_shader, uint64_t hash) {
  CaptureMissingShader(container, pixel_shader, hash);
  static std::mutex mutex;
  static std::unordered_set<uint64_t> handled;
  MissingShaderHandler handler = g_missing_handler.load(std::memory_order_acquire);
  if (!handler)
    return;
  {
    std::lock_guard lock(mutex);
    if (!handled.insert(hash).second)
      return;
  }
  const auto *header = reinterpret_cast<const d3d::ShaderContainer *>(container);
  handler(container, size_t(u32(header->virtualSize)) + u32(header->physicalSize), pixel_shader, hash);
}

} // namespace renut::gpu
