// opdarkness - traduction des shaders au fil du jeu, sur PC (docs/SHADERS_EN_JEU.md).
//
// Le rendu natif de sa2-nx ne dessine qu'avec des shaders traduits d'avance (paquet sa2_shaders.mesp). Ici, chaque
// shader que le jeu crée (CreateVertexShader / CreatePixelShader, src/od_me_hooks.cpp) et que la bibliothèque ne
// connaît pas encore est traduit tout de suite, sur le fil du jeu, avec la même chaîne que le paquet :
//   XenosRecomp (sa2-nx shaders/XenosRecomp, compilé dans le jeu) : conteneur -> HLSL (prélude shader_common.h) ;
//   DXC (libdxcompiler.so, chargée par dlopen) : HLSL -> SPIR-V, mêmes options que tools/build_shader_library.sh.
// puis ajouté à la bibliothèque du rendu (me::native::AddShader). Le jeu crée ses shaders surtout pendant les
// chargements : l'attente tombe là, et une seule fois par machine grâce au cache sur disque (--od_cache_shaders) :
// un fichier par shader (en-tête, conteneur, SPIR-V), relu au premier appel.
//
// Deux compilateurs HLSL -> SPIR-V (--od_compilateur) :
//   dxc     (PC seulement) : SPIR-V identique au paquet ;
//   glslang (PC et Switch, comme torchlight-nx) : glslang du SDK avec l'entrée HLSL (REXGLUE_GLSLANG_HLSL), prélude
//           shaders/shader_common_glslang.h et préparation du texte (src/od_prep_glslang.cpp). Sur la Switch, c'est
//           le seul (OD_TRADUCTEUR_DXC=0).
// Le cache est aussi lu depuis un fichier unique <cache>.odsc (tools/switch/regrouper_cache.py : les .odsh mis bout à
// bout) : sur la Switch, lister un dossier est peu fiable (leçon de torchlight-nx), et les traductions faites sur la
// console sont ajoutées à la fin de ce fichier (un enregistrement tronqué par une coupure arrête la lecture là).

#include "od_traducteur.h"

#ifndef OD_TRADUCTEUR_DXC
#define OD_TRADUCTEUR_DXC 1
#endif

#if OD_TRADUCTEUR_DXC
#include <dlfcn.h>
#endif

#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

#include "ResourceLimits.h"  // glslang/StandAlone : limites par défaut (DefaultTBuiltInResource)
#include "od_prep_glslang.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#include "me/me_native_system.h"

#include "XenosRecomp/shader_recompiler.h"

#if OD_TRADUCTEUR_DXC
#include <dxc/dxcapi.h>
#endif

REXCVAR_DEFINE_BOOL(od_traduire_shaders, true, "OpDarkness",
                    "Rendu natif : traduire en jeu (XenosRecomp + DXC) les shaders que le paquet ne connaît pas")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_compilateur, OD_TRADUCTEUR_DXC ? "dxc" : "glslang", "OpDarkness",
                      "Compilateur HLSL -> SPIR-V de la traduction en jeu : dxc (PC) ou glslang (PC et Switch)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_dxc, "", "OpDarkness",
                      "Chemin de libdxcompiler.so (vide : ~/workspace/_tools/dxc-1.8.2505.1/lib/libdxcompiler.so)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(od_flou_profondeur, 0, "OpDarkness",
                     "Flou de profondeur de champ du jeu (pixel shader 08fb9abd43fe7c0c, 15 lectures de texture par "
                     "pixel) : 0 = celui du jeu, 1 = point central seul (3 lectures ; même flou, transitions plus "
                     "franches au bord des objets), 2 = comparaison (moitié gauche du jeu, moitié droite allégée)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_cache_shaders, "cache_shaders", "OpDarkness",
                      "Dossier du cache des shaders traduits en jeu (relatif : à côté de l'exécutable ; vide : pas "
                      "de cache)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

namespace {

#if OD_TRADUCTEUR_DXC
// Prélude HLSL de XenosRecomp (sa2-nx shaders/XenosRecomp/shader_common.h), copié à la compilation.
constexpr char kPrelude[] =
#include "od_shader_common.inc"
    ;

using DxcCreateInstanceFn = HRESULT (*)(REFCLSID, REFIID, LPVOID*);
#endif

// Prélude pour glslang (shaders/shader_common_glslang.h), copié à la compilation.
constexpr char kPreludeGlslang[] =
#include "od_shader_common_glslang.inc"
    ;

// Le flou de profondeur de champ du jeu (docs/OPTIMISATION.md, « Flou de profondeur de champ ») : pour 5 points (le
// pixel et 4 voisins écartés selon la profondeur), profondeur (s3), image nette (s1) et image floue (s2), mélange
// net -> flou selon |2 z - 1|, puis moyenne pondérée. Le coût sur la console : ~4,7 ms par image en 720p à 307 MHz.
constexpr uint64_t kFlouProfondeur = 0x08fb9abd43fe7c0cull;

bool FlouRemplace(uint64_t h) { return h == kFlouProfondeur && REXCVAR_GET(od_flou_profondeur) != 0; }

// Mode 1 : le mélange du seul point central, la formule exacte du shader pour ce point (sa moyenne pondérée sur un
// point est le point lui-même). Remplace le corps entre la première lecture et l'écriture de oC0.w.
// Mode 2 : les deux sur la même image (comparaison, les plans des cinématiques changent d'un lancement à l'autre).
bool RemplacerFlou(std::string& hlsl) {
  const auto debut = hlsl.find("\tr6.y = tfetch2D(nSampler3_Texture2DDescriptorIndex");
  const auto fin = hlsl.find("\toC0.w = 1.0;");
  if (debut == std::string::npos || fin == std::string::npos || fin < debut) return false;
  if (REXCVAR_GET(od_flou_profondeur) == 2) {
    hlsl.insert(fin,
                "\t[branch] if (iPos.x >= 640.0) {\n"
                "\t\tfloat zc = tfetch2D(nSampler3_Texture2DDescriptorIndex, nSampler3_SamplerDescriptorIndex, "
                "iTexCoord0.xy, float2(0, 0), nSampler3_InvSize).x;\n"
                "\t\tfloat3 bc = tfetch2D(nSampler2_Texture2DDescriptorIndex, nSampler2_SamplerDescriptorIndex, "
                "iTexCoord0.xy, float2(0, 0), nSampler2_InvSize).xyz;\n"
                "\t\tfloat3 sc = tfetch2D(nSampler1_Texture2DDescriptorIndex, nSampler1_SamplerDescriptorIndex, "
                "iTexCoord0.xy, float2(0, 0), nSampler1_InvSize).xyz;\n"
                "\t\toC0.xyz = (bc * c252.yyy + -sc) * abs(zc * 2.0 + -1.0) + sc;\n"
                "\t}\n"
                "\tif (abs(iPos.x - 640.0) < 1.0) oC0.xyz = float3(1.0, 0.85, 0.0);\n");
    return true;
  }
  hlsl.replace(debut, fin - debut,
               "\tr6.y = tfetch2D(nSampler3_Texture2DDescriptorIndex, nSampler3_SamplerDescriptorIndex, r0.xy, "
               "float2(0, 0), nSampler3_InvSize).x;\n"
               "\tr4.xyz = tfetch2D(nSampler2_Texture2DDescriptorIndex, nSampler2_SamplerDescriptorIndex, r0.xy, "
               "float2(0, 0), nSampler2_InvSize).xyz;\n"
               "\tr8.xyz = tfetch2D(nSampler1_Texture2DDescriptorIndex, nSampler1_SamplerDescriptorIndex, r0.xy, "
               "float2(0, 0), nSampler1_InvSize).xyz;\n"
               "\toC0.xyz = (r4.xyz * c252.yyy + -r8.xyz) * abs(r6.y * 2.0 + -1.0) + r8.xyz;\n");
  return true;
}

constexpr char kMagie[8] = {'O', 'D', 'S', 'H', 'A', 'D', '0', '1'};

struct Etat {
  std::mutex mutex;  // une traduction à la fois (DXC, compteurs, cache)
  bool essaye = false;
  bool pret = false;
  bool glslang = false;  // compilateur choisi (od_compilateur)
#if OD_TRADUCTEUR_DXC
  DxcCreateInstanceFn creer = nullptr;
  IDxcCompiler3* compilateur = nullptr;
#endif
  std::filesystem::path cache;
  uint32_t traduits = 0, echecs = 0, du_cache = 0, inconnus = 0;
  double ms_total = 0;
};

Etat& G() {
  static Etat e;
  return e;
}

std::filesystem::path Dossier(const std::string& valeur) {
  if (valeur.empty()) return {};
  std::filesystem::path p(valeur);
  return p.is_relative() ? rex::filesystem::GetExecutableFolder() / p : p;
}

// Enregistrements d'un fichier .odsh (un seul) ou .odsc (plusieurs, bout à bout) : magie, octets, mots, conteneur,
// SPIR-V. Lecture d'un bloc (std::ifstream lève des exceptions sur la console : fopen / fread).
uint32_t LireEnregistrements(Etat& e, const std::filesystem::path& chemin) {
  std::FILE* f = std::fopen(chemin.string().c_str(), "rb");
  if (!f) return 0;
  std::vector<uint8_t> tout;
  uint8_t bloc[1 << 16];
  for (size_t n; (n = std::fread(bloc, 1, sizeof(bloc), f)) > 0;) tout.insert(tout.end(), bloc, bloc + n);
  std::fclose(f);
  uint32_t ajoutes = 0;
  size_t p = 0;
  while (p + 16 <= tout.size()) {
    uint32_t octets = 0, mots = 0;
    std::memcpy(&octets, &tout[p + 8], 4);
    std::memcpy(&mots, &tout[p + 12], 4);
    if (std::memcmp(&tout[p], kMagie, 8) != 0 || octets > (1u << 20) || mots > (1u << 22) ||
        tout.size() - p - 16 < octets + size_t(mots) * 4) {
      REXLOG_WARN("[traducteur] cache illisible ou tronqué : {} (position {})", chemin.string(), p);
      break;
    }
    std::vector<uint8_t> conteneur(&tout[p + 16], &tout[p + 16] + octets);
    std::vector<uint32_t> spirv(mots);
    std::memcpy(spirv.data(), &tout[p + 16 + octets], size_t(mots) * 4);
    p += 16 + octets + size_t(mots) * 4;
    if (FlouRemplace(XXH3_64bits(conteneur.data(), conteneur.size()))) continue;  // retraduit, remplacé
    if (me::native::AddShader(std::move(conteneur), std::move(spirv))) ++ajoutes;
  }
  return ajoutes;
}

void ChargerCache(Etat& e) {
  if (e.cache.empty()) return;
  auto regroupe = e.cache;
  regroupe += ".odsc";
  e.du_cache += LireEnregistrements(e, regroupe);
#if OD_TRADUCTEUR_DXC
  std::error_code ec;
  std::filesystem::create_directories(e.cache, ec);
  for (const auto& f : std::filesystem::directory_iterator(e.cache, ec)) {
    if (f.path().extension() == ".odsh") e.du_cache += LireEnregistrements(e, f.path());
  }
#endif
  REXLOG_INFO("[traducteur] {} shaders relus du cache {}", e.du_cache, e.cache.string());
}

void EcrireCache(const Etat& e, const std::vector<uint8_t>& conteneur, const std::vector<uint32_t>& spirv) {
  if (e.cache.empty()) return;
  const uint32_t octets = uint32_t(conteneur.size()), mots = uint32_t(spirv.size());
#if !OD_TRADUCTEUR_DXC
  // Switch : à la fin du fichier unique, d'un seul fwrite.
  std::vector<uint8_t> enregistrement(16 + octets + size_t(mots) * 4);
  std::memcpy(&enregistrement[0], kMagie, 8);
  std::memcpy(&enregistrement[8], &octets, 4);
  std::memcpy(&enregistrement[12], &mots, 4);
  std::memcpy(&enregistrement[16], conteneur.data(), octets);
  std::memcpy(&enregistrement[16 + octets], spirv.data(), size_t(mots) * 4);
  auto regroupe = e.cache;
  regroupe += ".odsc";
  if (std::FILE* f = std::fopen(regroupe.string().c_str(), "ab")) {
    std::fwrite(enregistrement.data(), 1, enregistrement.size(), f);
    std::fclose(f);
  }
#else
  std::error_code ec;
  std::filesystem::create_directories(e.cache, ec);
  const uint64_t h = XXH3_64bits(conteneur.data(), conteneur.size());
  const bool vertex = conteneur.size() > 3 && (conteneur[3] & 1);
  const auto chemin = e.cache / fmt::format("{}_{:016x}.odsh", vertex ? "vs" : "ps", h);
  const auto temporaire = chemin.string() + ".tmp";
  {
    std::ofstream out(temporaire, std::ios::binary | std::ios::trunc);
    out.write(kMagie, 8);
    out.write(reinterpret_cast<const char*>(&octets), 4);
    out.write(reinterpret_cast<const char*>(&mots), 4);
    out.write(reinterpret_cast<const char*>(conteneur.data()), octets);
    out.write(reinterpret_cast<const char*>(spirv.data()), std::streamsize(mots) * 4);
    if (!out) return;
  }
  std::filesystem::rename(temporaire, chemin, ec);
#endif
}

// Premier appel : DXC (dlopen) et cache. false si la traduction est impossible (le message dit pourquoi, une fois).
bool Preparer(Etat& e) {
  if (e.essaye) return e.pret;
  e.essaye = true;
  e.cache = Dossier(REXCVAR_GET(od_cache_shaders));
  if (!REXCVAR_GET(od_traduire_shaders)) {
    ChargerCache(e);
    return false;
  }
  e.glslang = !OD_TRADUCTEUR_DXC || REXCVAR_GET(od_compilateur) == "glslang";
  if (e.glslang) {
    static std::once_flag une_fois;
    std::call_once(une_fois, [] { glslang::InitializeProcess(); });
    REXLOG_INFO("[traducteur] compilateur : glslang ; cache : {}", e.cache.empty() ? "aucun" : e.cache.string());
    e.pret = true;
    ChargerCache(e);
    return true;
  }
#if OD_TRADUCTEUR_DXC
  std::string chemin = REXCVAR_GET(od_dxc);
  if (chemin.empty()) {
    const char* home = std::getenv("HOME");
    chemin = std::string(home ? home : "") + "/workspace/_tools/dxc-1.8.2505.1/lib/libdxcompiler.so";
  }
  void* bibliotheque = dlopen(chemin.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!bibliotheque) {
    REXLOG_ERROR("[traducteur] DXC introuvable ({}) : {} ; pas de traduction en jeu", chemin, dlerror());
    return false;
  }
  e.creer = reinterpret_cast<DxcCreateInstanceFn>(dlsym(bibliotheque, "DxcCreateInstance"));
  if (!e.creer || FAILED(e.creer(CLSID_DxcCompiler, IID_PPV_ARGS(&e.compilateur))) || !e.compilateur) {
    REXLOG_ERROR("[traducteur] DXC ({}) : DxcCreateInstance indisponible", chemin);
    return false;
  }
  REXLOG_INFO("[traducteur] DXC chargé ({}) ; cache : {}", chemin, e.cache.empty() ? "aucun" : e.cache.string());
  e.pret = true;
  ChargerCache(e);
  return true;
#else
  return false;
#endif
}

// HLSL -> SPIR-V par glslang : options équivalentes à celles de DXC (point d'entrée main, Vulkan 1.2, y inversé pour
// les vertex shaders).
bool CompilerGlslang(const std::string& hlsl, bool vertex, std::vector<uint32_t>& spirv, std::string& erreur) {
  const EShLanguage etape = vertex ? EShLangVertex : EShLangFragment;
  const std::string texte_prepare = opdarkness::glslang_prep::Preparer(hlsl);
  glslang::TShader shader(etape);
  const char* texte = texte_prepare.c_str();
  const int longueur = int(texte_prepare.size());
  shader.setStringsWithLengths(&texte, &longueur, 1);
  shader.setEntryPoint("main");
  shader.setSourceEntryPoint("main");
  shader.setEnvInput(glslang::EShSourceHlsl, etape, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_2);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_5);
  if (vertex) shader.setInvertY(true);
  const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);
  if (!shader.parse(&glslang::DefaultTBuiltInResource, 100, false, messages)) {
    erreur = std::string("glslang : ") + shader.getInfoLog();
    return false;
  }
  glslang::TProgram programme;
  programme.addShader(&shader);
  if (!programme.link(messages)) {
    erreur = std::string("glslang (liens) : ") + programme.getInfoLog();
    return false;
  }
  spv::SpvBuildLogger journal;
  glslang::SpvOptions options;
  // Légalisation et optimisation par SPIRV-Tools, comme DXC par défaut (et torchlight-nx) : sans elles, le code mort
  // et les variables privées restent, et le rendu natif refuse certains vertex shaders (listes de rectangles,
  // me_rectangle_spirv.h : vertex shader du quad qui affiche la scène 3D, champ de bataille noir, 2026-10-09).
  options.disableOptimizer = false;
  glslang::GlslangToSpv(*programme.getIntermediate(etape), spirv, &journal, &options);
  const std::string messages_spirv = journal.getAllMessages();
  if (spirv.empty() || messages_spirv.find("error") != std::string::npos) {
    erreur = "glslang (SPIR-V) : " + (messages_spirv.empty() ? std::string("vide") : messages_spirv);
    return false;
  }
  return true;
}

#if OD_TRADUCTEUR_DXC
// HLSL -> SPIR-V, mêmes options que sa2-nx tools/build_shader_library.sh.
bool Compiler(Etat& e, const std::string& hlsl, bool vertex, std::vector<uint32_t>& spirv, std::string& erreur) {
  DxcBuffer source{};
  source.Ptr = hlsl.data();
  source.Size = hlsl.size();
  source.Encoding = DXC_CP_UTF8;
  std::vector<const wchar_t*> args = {L"-spirv", L"-T", vertex ? L"vs_6_6" : L"ps_6_6", L"-E", L"main", L"-HV",
                                      L"2021", L"-fspv-target-env=vulkan1.2", L"-fvk-use-dx-layout",
                                      L"-Werror=parameter-usage"};
  if (vertex) args.push_back(L"-fvk-invert-y");
  IDxcResult* resultat = nullptr;
  if (FAILED(e.compilateur->Compile(&source, args.data(), uint32_t(args.size()), nullptr, IID_PPV_ARGS(&resultat))) ||
      !resultat) {
    erreur = "appel de DXC échoué";
    return false;
  }
  HRESULT etat = E_FAIL;
  resultat->GetStatus(&etat);
  if (FAILED(etat)) {
    IDxcBlobUtf8* erreurs = nullptr;
    if (SUCCEEDED(resultat->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&erreurs), nullptr)) && erreurs) {
      erreur.assign(erreurs->GetStringPointer(), erreurs->GetStringLength());
      erreurs->Release();
    }
    resultat->Release();
    return false;
  }
  IDxcBlob* objet = nullptr;
  if (FAILED(resultat->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&objet), nullptr)) || !objet ||
      objet->GetBufferSize() % 4) {
    erreur = "pas de SPIR-V";
    resultat->Release();
    return false;
  }
  spirv.resize(objet->GetBufferSize() / 4);
  std::memcpy(spirv.data(), objet->GetBufferPointer(), objet->GetBufferSize());
  objet->Release();
  resultat->Release();
  return true;
}

#endif

// Bibliothèque du rendu prête (avant le premier dessin) : DXC et cache tout de suite, pour que les shaders déjà
// traduits servent dès les premiers dessins de Direct3D, avant que le jeu ne crée les siens.
void BibliothequePrete() {
  Etat& e = G();
  std::lock_guard<std::mutex> lock(e.mutex);
  Preparer(e);
}

const bool g_enregistre = [] {
  me::native::SetLibraryReadyHandler(&BibliothequePrete);
  return true;
}();

}  // namespace

namespace opdarkness::traducteur {

void AssurerShader(const uint8_t* conteneur, size_t taille) {
  if (taille < 24 || taille > (1u << 20)) return;
  // Signature 0x102A11xx (XDK 2008), gros-boutiste.
  if (conteneur[0] != 0x10 || conteneur[1] != 0x2A || conteneur[2] != 0x11) return;
  const std::span<const uint8_t> vue(conteneur, taille);
  Etat& e = G();
  std::lock_guard<std::mutex> lock(e.mutex);
  const bool pret = Preparer(e);
  const bool vertex = (conteneur[3] & 1) != 0;
  const uint64_t h = XXH3_64bits(conteneur, taille);
  if (me::native::ShaderKnown(vue)) return;
  if (!pret) {
    if (++e.inconnus <= 20 || (e.inconnus & (e.inconnus - 1)) == 0) {
      REXLOG_WARN("[traducteur] {} {:016x} inconnu, non traduit ({} inconnus)", vertex ? "VS" : "PS", h, e.inconnus);
    }
    return;
  }
  const auto debut = std::chrono::steady_clock::now();
  std::string hlsl, erreur;
  try {
    ShaderRecompiler traducteur;
#if OD_TRADUCTEUR_DXC
    const std::string_view prelude = e.glslang ? std::string_view(kPreludeGlslang, sizeof(kPreludeGlslang) - 1)
                                               : std::string_view(kPrelude, sizeof(kPrelude) - 1);
#else
    const std::string_view prelude(kPreludeGlslang, sizeof(kPreludeGlslang) - 1);
#endif
    traducteur.recompile(conteneur, prelude);
    hlsl.assign(traducteur.out.data(), traducteur.out.size());
  } catch (const std::exception& x) {
    erreur = std::string("XenosRecomp : ") + x.what();
  }
  const bool remplace = FlouRemplace(h);
  if (remplace && !hlsl.empty() && !RemplacerFlou(hlsl)) {
    REXLOG_WARN("[traducteur] flou de profondeur : forme du HLSL inattendue, shader du jeu gardé");
  }
  std::vector<uint32_t> spirv;
  bool ok = false;
  if (!hlsl.empty()) {
#if OD_TRADUCTEUR_DXC
    ok = e.glslang ? CompilerGlslang(hlsl, vertex, spirv, erreur) : Compiler(e, hlsl, vertex, spirv, erreur);
#else
    ok = CompilerGlslang(hlsl, vertex, spirv, erreur);
#endif
  }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - debut).count();
  if (!ok) {
    ++e.echecs;
    REXLOG_WARN("[traducteur] {} {:016x} non traduit ({:.0f} ms) : {}", vertex ? "VS" : "PS", h, ms,
                erreur.substr(0, 300));
    return;
  }
  std::vector<uint8_t> copie(conteneur, conteneur + taille);
  if (!remplace) EcrireCache(e, copie, spirv);  // le cache ne garde que les shaders du jeu
  else REXLOG_INFO("[traducteur] flou de profondeur : version allégée (od_flou_profondeur = {})",
                   REXCVAR_GET(od_flou_profondeur));
  if (!me::native::AddShader(std::move(copie), std::move(spirv))) {
    ++e.echecs;
    REXLOG_WARN("[traducteur] {} {:016x} traduit mais refusé par la bibliothèque", vertex ? "VS" : "PS", h);
    return;
  }
  ++e.traduits;
  e.ms_total += ms;
  REXLOG_INFO("[traducteur] {} {:016x} traduit en {:.0f} ms ({} traduits en jeu, {:.0f} ms au total ; {} échecs)",
              vertex ? "VS" : "PS", h, ms, e.traduits, e.ms_total, e.echecs);
}

}  // namespace opdarkness::traducteur
