// opdarkness - interceptions du rendu natif (src/me, importé de sa2-nx) avec les adresses d'Operation Darkness
// (docs/D3D.md). Le rendu lit tout seul l'anneau PM4 du jeu ; ces interceptions lui donnent ce que l'anneau ne dit pas
// (d'après sa2-nx app/src/me/sa2_me_hooks.cpp) :
//
//   - les objets shaders liés au moment du dessin : le chargement des shaders au dessin (sub_8234CC58, r3 = device)
//     lit le VS lié à device+12696 et le PS à device+12692 (disposition du XDK 2008, comme Banjo) ; on les note avant
//     que l'original ne retouche le VS pour la déclaration de sommets ;
//   - un relevé par appel de dessin (NoteDrawCall : device, type, puis r5..r7 tels que Direct3D les reçoit), qui
//     associe les DRAW_INDX de l'anneau aux shaders liés, par la position dans le tampon de commandes (device+48) ;
//   - les conteneurs de shaders pour la bibliothèque (od_dump_shaders = true) : le conteneur que reçoivent
//     CreateVertexShader (sub_823415A8) et CreatePixelShader (sub_82341498) en r3 ([0] drapeaux, [1] taille de
//     l'en-tête, [2] taille du microcode), écrit dans shaders/ à côté de l'exécutable, une fois chacun.
//
// Les six fonctions de dessin ne sont pas encore nommées une à une (DrawVertices, DrawIndexedVertices, variantes UP) :
// toutes reçoivent le device en r3, ce qui suffit au relevé. Chaque interception appelle l'original et ne touche à
// aucun registre PPC.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <system_error>
#include <unordered_set>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "me/me_native_system.h"
#include "renut_engine/gpu/sa2_renut_yield.h"
extern "C" void od_renut_pixel_shader_created(uint32_t shader);  // src/renut_engine/gpu/d3d/hooks/shader.cpp
#include "od_traducteur.h"

REXCVAR_DEFINE_BOOL(od_dump_shaders, false, "OpDarkness",
                    "Écrit chaque conteneur de shader créé par le jeu dans shaders/ à côté de l'exécutable "
                    "(vs_<empreinte>.bin / ps_<empreinte>.bin), pour la bibliothèque de shaders du rendu natif");

namespace {

inline uint32_t Load32(const uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}

constexpr uint32_t kDeviceVertexShader = 12696;  // 0x3198
constexpr uint32_t kDevicePixelShader = 12692;   // 0x3194

// Les relevés de dessin (NoteDrawCall) lisent les shaders liés aux mêmes champs du device.
const bool g_device_layout_set = [] {
  me::native::SetDeviceShaderOffsets(kDeviceVertexShader, kDevicePixelShader);
  return true;
}();

std::mutex g_dump_mutex;
std::unordered_set<uint64_t> g_dumped;

// Taille du conteneur (en-tête + microcode) que reçoit Create*Shader, 0 s'il est invraisemblable.
size_t ContainerSize(const uint8_t* base, uint32_t blob_address) {
  if (!blob_address) return 0;
  const uint32_t header = Load32(base, blob_address + 4);
  const uint32_t ucode = Load32(base, blob_address + 8);
  if (header < 12 || header > (1u << 20) || !ucode || ucode > (1u << 20)) return 0;
  return size_t(header) + ucode;
}

// Traduction au fil du jeu (od_traducteur.cpp) : avant que le jeu ne crée l'objet, donc avant tout dessin avec lui.
void TranslateIfNeeded(const uint8_t* base, uint32_t blob_address) {
  if (!me::native::NativeActive()) return;
  if (const size_t size = ContainerSize(base, blob_address)) {
    opdarkness::traducteur::AssurerShader(base + blob_address, size);
  }
}

void DumpShaderBlob(const uint8_t* base, uint32_t blob_address, bool vertex) {
  if (!REXCVAR_GET(od_dump_shaders) || !blob_address) {
    return;
  }
  const uint32_t header = Load32(base, blob_address + 4);
  const uint32_t ucode = Load32(base, blob_address + 8);
  if (header < 12 || header > (1u << 20) || !ucode || ucode > (1u << 20)) {
    REXLOG_WARN("[shaders] conteneur {} invraisemblable à {:08X} (en-tête {}, microcode {}), ignoré",
                vertex ? "VS" : "PS", blob_address, header, ucode);
    return;
  }
  const uint8_t* blob = base + blob_address;
  const size_t size = size_t(header) + ucode;
  const uint64_t hash = XXH3_64bits(blob, size);
  {
    std::lock_guard lock(g_dump_mutex);
    if (!g_dumped.insert(hash).second) {
      return;
    }
  }
  const auto folder = rex::filesystem::GetExecutableFolder() / "shaders";
  std::error_code ec;
  std::filesystem::create_directories(folder, ec);
  const auto path = folder / fmt::format("{}_{:016x}.bin", vertex ? "vs" : "ps", hash);
  std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(blob), std::streamsize(size));
}

inline void NoteDraw(const uint8_t* base, const PPCContext& ctx) {
  me::native::NoteDrawCall(base, ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32);
}

}  // namespace

// Chargement des shaders au dessin (partie shaders de FlushState, 3e appel de chaque fonction de dessin).
REX_EXTERN(__imp__sub_8234CC58);
REX_HOOK_RAW(sub_8234CC58) {
  const uint32_t device = ctx.r3.u32;
  if (device >= 0x40000000 && device < 0x7F000000) {
    const uint32_t vs = Load32(base, device + kDeviceVertexShader), ps = Load32(base, device + kDevicePixelShader);
    if (!me::native::ShaderObjectsFinished(vs, ps)) {
      me::native::NoteShaderObject(base, vs, true);
      me::native::NoteShaderObject(base, ps, false);
    }
  }
  __imp__sub_8234CC58(ctx, base);
}

// Les six fonctions de dessin (DRAW_INDX ; docs/D3D.md). Rendu reNut choisi (od_renderer = "renut") : ses
// interceptions (SA2_RENUT_YIELD_SHARED), ou l'original pour les deux dessins tessellés qu'il ne traite pas encore.
#define OD_DRAW_HOOK(ADDR, YIELD)  \
  REX_EXTERN(__imp__sub_##ADDR);   \
  REX_HOOK_RAW(sub_##ADDR) {       \
    YIELD(sub_##ADDR)              \
    NoteDraw(base, ctx);           \
    __imp__sub_##ADDR(ctx, base);  \
  }
OD_DRAW_HOOK(826BD808, SA2_RENUT_YIELD_SHARED)  // BeginVertices
OD_DRAW_HOOK(826BDD00, SA2_RENUT_YIELD_SHARED)  // BeginIndexedVertices
OD_DRAW_HOOK(826BE2D0, SA2_RENUT_YIELD_SHARED)  // DrawVertices
OD_DRAW_HOOK(826BE6B8, SA2_RENUT_YIELD_SHARED)  // DrawIndexedVertices
OD_DRAW_HOOK(826BEB10, SA2_RENUT_YIELD)         // DrawTessellatedVertices ?
OD_DRAW_HOOK(826BEF88, SA2_RENUT_YIELD)         // DrawIndexedTessellatedVertices ?
#undef OD_DRAW_HOOK

REX_EXTERN(__imp__sub_823415A8);
REX_HOOK_RAW(sub_823415A8) {  // D3DDevice_CreateVertexShader (r3 = le conteneur)
  DumpShaderBlob(base, ctx.r3.u32, true);
  TranslateIfNeeded(base, ctx.r3.u32);
  __imp__sub_823415A8(ctx, base);
}

REX_EXTERN(__imp__sub_82341498);
REX_HOOK_RAW(sub_82341498) {  // D3DDevice_CreatePixelShader (r3 = le conteneur)
  DumpShaderBlob(base, ctx.r3.u32, false);
#if SA2_RENUT_RENDERER
  // Rendu reNut : XGRegisterPixelShader est recopiée dans cette fonction ; le shader créé lui est donné après.
  if (banjo::renut_gpu::Active()) {
    __imp__sub_82341498(ctx, base);
    od_renut_pixel_shader_created(ctx.r3.u32);
    return;
  }
#endif
  TranslateIfNeeded(base, ctx.r3.u32);
  __imp__sub_82341498(ctx, base);
}
