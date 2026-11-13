// opdarkness - XenosRecomp de reNut dans le jeu (od_xenosrecomp_renut.h). Ses sources sont incluses telles quelles dans
// l'espace de noms od_renut_xr : le XenosRecomp de sa2-nx (rendu natif, od_xenosrecomp) définit les mêmes classes
// (ShaderRecompiler, StringBuffer, ShaderContainer...) autrement, et l'éditeur de liens garderait une seule copie de
// leurs fonctions en ligne. Les en-têtes système et tiers sont inclus avant, hors de l'espace de noms (leurs gardes
// rendent ceux des sources sans effet) ; le contenu de pch.h dont les sources ont besoin (byteSwap, be) est repris
// dans l'espace de noms.

#include "od_xenosrecomp_renut.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fmt/core.h>
#include <fmt/format.h>
#include <xxhash.h>

#ifndef RENUT_RECOMP
#define RENUT_RECOMP 1
#endif

namespace od_renut_xr {

// pch.h de XenosRecomp, sans ses inclusions.
template <typename T>
static T byteSwap(T value) {
  if constexpr (sizeof(T) == 1)
    return value;
  else if constexpr (sizeof(T) == 2)
    return static_cast<T>(__builtin_bswap16(static_cast<uint16_t>(value)));
  else if constexpr (sizeof(T) == 4)
    return static_cast<T>(__builtin_bswap32(static_cast<uint32_t>(value)));
  else if constexpr (sizeof(T) == 8)
    return static_cast<T>(__builtin_bswap64(static_cast<uint64_t>(value)));
  assert(false && "Unexpected byte size.");
  return value;
}

template <typename T>
struct be {
  T value;
  T get() const {
    if constexpr (std::is_enum_v<T>)
      return T(byteSwap(std::underlying_type_t<T>(value)));
    else
      return byteSwap(value);
  }
  operator T() const { return get(); }
};

#include "shader_recompiler.cpp"

}  // namespace od_renut_xr

namespace opdarkness::xenosrecomp_renut {

namespace {
constexpr char kPrelude[] =
#include "od_renut_shader_common.inc"
    ;
}  // namespace

std::string_view Prelude() { return std::string_view(kPrelude, sizeof(kPrelude) - 1); }

size_t TailleConteneur(const uint8_t* donnees, size_t reste) {
  if (reste <= sizeof(od_renut_xr::ShaderContainer)) return 0;
  const auto* c = reinterpret_cast<const od_renut_xr::ShaderContainer*>(donnees);
  const size_t taille = size_t(c->virtualSize) + c->physicalSize;
  if ((c->flags & 0xFFFFFF00u) != 0x102A1100u || taille > reste || c->field1C != 0 || c->field20 != 0) return 0;
  return taille;
}

void Recompiler(const uint8_t* conteneur, std::string_view prelude, Shader& sortie) {
  od_renut_xr::ShaderRecompiler r;
  r.recompile(conteneur, prelude);
  sortie.pixel = r.isPixelShader;
  sortie.spec_constants_mask = r.specConstantsMask;
  sortie.vertex_fetch_memory_mask = r.vertexFetchMemoryMask;
  sortie.texture_fetch_mask = r.textureFetchMask;
  std::copy(std::begin(r.memExportConstants), std::end(r.memExportConstants), sortie.mem_export_constants);
  std::copy(std::begin(r.floatConstants), std::end(r.floatConstants), sortie.float_constants);
  sortie.shader_flags = r.shaderFlags;
  sortie.literaux = r.cbufLiterals;
  sortie.entrees.clear();
  for (const auto& e : r.vertexInputs)
    sortie.entrees.push_back({e.usage, e.usageIndex, e.fetchIndex, e.format, e.flags, e.strideDwords, e.offsetDwords});
  sortie.hlsl = std::move(r.out);
}

}  // namespace opdarkness::xenosrecomp_renut
