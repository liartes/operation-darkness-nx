// opdarkness - installation des shaders du jeu par la console (od_installation.h).

#include "od_installation.h"

#include <pthread.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <thread>
#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <imgui.h>
#include <rex/logging.h>
#include <rex/ui/imgui_dialog.h>
#include <zstd.h>

#include "od_chaine_deko3d.h"
#include "od_conteneurs.h"
#include "od_switch_cpu.h"
#if defined(__SWITCH__)
#include <plume_deko3d.h>

#include "renut_engine/gpu/backend/video.h"
#include "renut_engine/gpu/banjo_renut_deko3d.h"
#endif
#include "renut_engine/gpu/banjo_renut_tuning.h"
#include "renut_engine/gpu/render/shader_library.h"

#if defined(OD_RENUT_SANS_DONNEES)
#define OD_INSTALLER_DEFAUT "si_absent"  // .nro sans donnée du jeu : la console fait ses shaders
#else
#define OD_INSTALLER_DEFAUT "non"
#endif
REXCVAR_DEFINE_STRING(od_installer, OD_INSTALLER_DEFAUT, "OpDarkness",
                      "Installation des shaders par la console (docs/DEKO3D.md) au démarrage : renut_fiches.bin et "
                      "deko3d_glsl.bin à côté du .nro, depuis les fichiers du jeu ; « si_absent » : quand l'un des "
                      "deux manque, « toujours », « non »")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_installer_verif, true, "OpDarkness",
                    "Installation : chaque fiche comparée à la table compilée dans l'exécutable (s'il en a une)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_installer_spirv_integre, false, "OpDarkness",
                    "Essai sur PC (Vulkan) de l'installation avec od_renut_shaders_integres = false : les fiches faites "
                    "prennent le SPIR-V de la table de l'exécutable (le rendu passe par elles, mêmes images)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_shaders_en_jeu, true, "OpDarkness",
                    "reNut : les shaders que le jeu compile pendant la partie (absents de la table) traduits sur un fil "
                    "de fond (chaîne de la console), puis ajoutés au paquet au lancement suivant (shader_capture/)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(od_installer_flou, 1, "OpDarkness",
                     "Installation : flou de profondeur de champ (0 celui du jeu, 1 allégé, 2 comparaison ; "
                     "tools/renut/od_flou_profondeur.py)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_installer_ombres, true, "OpDarkness",
                    "Installation : ombres portées arrêtées hors de la carte d'ombres (tools/renut/od_ombres.py ; "
                    "od_renut_ombres_decoupe au dessin)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_installer_uam, true, "OpDarkness",
                    "Installation (Switch) : les variantes courantes des shaders compilées aussi par uam (jusqu'à ~9 min, "
                    "une fois), dans le cache deko3d_shaders.bin ; sinon au fil du jeu")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_releve_variantes, true, "OpDarkness",
                    "Switch : chaque variante de shader prise en partie notée dans variantes_jeu.txt à côté du .nro "
                    "(règles de l'installation ; recompilées à l'installation suivante sur cette console)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DECLARE(std::string, renut_shader_capture_dir);
REXCVAR_DECLARE(int32_t, od_essai_deko3d_fils);
REXCVAR_DECLARE(bool, od_essai_deko3d_allocateur);
REXCVAR_DECLARE(bool, od_essai_deko3d_boost);

namespace opdarkness::installation {
namespace {

// Avancement de l'écran (pour mille) : fins de la recherche et de la traduction ; uam ensuite (Switch).
constexpr int kFinRecherche = 60, kFinTraduction = 150;
bool g_avec_uam = false;  // Switch et od_installer_uam : fixé au début de l'installation

using Horloge = std::chrono::steady_clock;

double Ms(Horloge::time_point debut) {
  return std::chrono::duration<double, std::milli>(Horloge::now() - debut).count();
}

struct Fiche {
  ShaderCacheEntry entree{};
  std::vector<ShaderVertexInput> entrees;
  std::vector<ShaderLiteral> literaux;
};

// Ce que main.cpp de XenosRecomp (RENUT_RECOMP) écrit dans le cache pour ce shader, sans DXIL ni SPIR-V.
Fiche FicheDe(uint64_t empreinte, const xenosrecomp_renut::Shader& s) {
  Fiche f;
  ShaderCacheEntry& e = f.entree;
  e.hash = empreinte;
  e.specConstantsMask = s.spec_constants_mask;
  e.vertexFetchMemoryMask = s.vertex_fetch_memory_mask;
  e.textureFetchMask = s.texture_fetch_mask;
  std::copy(std::begin(s.mem_export_constants), std::end(s.mem_export_constants), e.memExportConstants);
  std::copy(std::begin(s.float_constants), std::end(s.float_constants), e.floatConstants);
  e.vertexInputCount = uint32_t(s.entrees.size());
  e.literalCount = uint32_t(s.literaux.size());
  e.shaderFlags = s.shader_flags;
  for (const auto& v : s.entrees)
    f.entrees.push_back({v.usage, v.usage_index, v.fetch_index, v.format, v.flags, v.stride_dwords, v.offset_dwords});
  for (const auto& l : s.literaux) f.literaux.push_back({l[0], {l[1], l[2], l[3], l[4]}});
  return f;
}

// Les écarts d'une fiche avec l'entrée de la table de l'exécutable (texte vide : identiques).
std::string Ecarts(const Fiche& f, const ShaderCacheEntry& b) {
  const ShaderCacheEntry& e = f.entree;
  std::string r;
  auto champ = [&](const char* nom, bool egal) {
    if (!egal) r += fmt::format(" {}", nom);
  };
  champ("specConstantsMask", e.specConstantsMask == b.specConstantsMask);
  champ("vertexFetchMemoryMask", e.vertexFetchMemoryMask == b.vertexFetchMemoryMask);
  champ("textureFetchMask", e.textureFetchMask == b.textureFetchMask);
  champ("memExportConstants", !std::memcmp(e.memExportConstants, b.memExportConstants, sizeof(e.memExportConstants)));
  champ("floatConstants", !std::memcmp(e.floatConstants, b.floatConstants, sizeof(e.floatConstants)));
  champ("shaderFlags", e.shaderFlags == (b.shaderFlags & ~kShaderFlagRuntime));
  bool entrees = e.vertexInputCount == b.vertexInputCount;
  for (uint32_t i = 0; entrees && i < e.vertexInputCount; ++i)
    entrees = !std::memcmp(&f.entrees[i], &ShaderVertexInputsOf(b)[i], sizeof(ShaderVertexInput));
  champ("entrées", entrees);
  bool literaux = e.literalCount == b.literalCount;
  for (uint32_t i = 0; literaux && i < e.literalCount; ++i)
    literaux = !std::memcmp(&f.literaux[i], &ShaderLiteralsOf(b)[i], sizeof(ShaderLiteral));
  champ("littéraux", literaux);
  return r;
}

// Écrit d'abord dans <chemin>.tmp, puis remplace : un fichier coupé (console éteinte en route) n'est jamais lu.
bool EcrireFichier(const std::filesystem::path& chemin, const std::vector<uint8_t>& octets) {
  const std::string tmp = chemin.string() + ".tmp";
  std::FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(octets.data(), 1, octets.size(), f) == octets.size() && std::fflush(f) == 0;
  std::fclose(f);
  if (!ok) return false;
  std::error_code ec;
  std::filesystem::rename(tmp, chemin, ec);
  if (ec) {
    // Carte SD de la Switch : un renommage ne remplace pas un fichier qui existe (complément de l'installation). Un
    // arrêt entre les deux laisse le fichier absent : le lancement suivant refait l'installation (si_absent).
    std::error_code ec2;
    std::filesystem::remove(chemin, ec2);
    ec.clear();
    std::filesystem::rename(tmp, chemin, ec);
  }
  if (ec) REXLOG_WARN("[installation] {} non écrit : {}", chemin.string(), ec.message());
  return !ec;
}

template <typename T>
void Ajouter(std::vector<uint8_t>& o, const T* p, size_t n) {
  const auto* b = reinterpret_cast<const uint8_t*>(p);
  o.insert(o.end(), b, b + n * sizeof(T));
}

std::vector<uint8_t> Fiches(const std::vector<Fiche>& fiches) {
  std::vector<uint8_t> o;
  uint32_t tete[5];
  std::memcpy(tete, renut::gpu::kFichesMagic, 4);
  tete[1] = renut::gpu::kFichesVersion;
  tete[2] = kShaderCacheLayout;
  tete[3] = sizeof(ShaderCacheEntry);
  tete[4] = uint32_t(fiches.size());
  Ajouter(o, tete, 5);
  for (const Fiche& f : fiches) {
    Ajouter(o, &f.entree, 1);
    Ajouter(o, f.entrees.data(), f.entrees.size());
    Ajouter(o, f.literaux.data(), f.literaux.size());
  }
  return o;
}

// Le paquet GLSL de tools/renut/deko3d_shader_package.py, sans dictionnaire (une trame zstd par shader, comme celui
// de l'installateur de sa2-nx) : en-tête de 32 octets, table triée par empreinte (56 octets l'entrée), données.
#pragma pack(push, 1)
struct EntreePaquet {
  uint64_t hash;
  uint32_t offset, compressedSize, size, stage;
  uint32_t boolMask[8];
};
#pragma pack(pop)
static_assert(sizeof(EntreePaquet) == 56);

// Un shader du paquet : son entrée (offset recalculé à l'écriture) et sa trame zstd.
struct ElementPaquet {
  EntreePaquet e{};
  std::vector<uint8_t> trame;
};

// Les constantes de spécialisation booléennes (1000 à 1255), comme le script : « #define ..._<n> ».
void MasqueBooleens(const std::string& texte, uint32_t masque[8]) {
  static constexpr char kCle[] = "#define SPIRV_CROSS_CONSTANT_ID_";
  std::fill(masque, masque + 8, 0u);
  for (size_t p = texte.find(kCle); p != std::string::npos; p = texte.find(kCle, p + 1)) {
    size_t q = p + sizeof(kCle) - 1;
    unsigned id = 0;
    bool chiffres = false;
    while (q < texte.size() && texte[q] >= '0' && texte[q] <= '9') {
      id = id * 10 + unsigned(texte[q++] - '0');
      chiffres = true;
    }
    if (chiffres && q < texte.size() && texte[q] == ' ' && id >= 1000 && id < 1256)
      masque[(id - 1000) / 32] |= 1u << ((id - 1000) % 32);
  }
}

// Version du texte GLSL que rend la chaîne (mot 6 de l'en-tête du paquet, que le lecteur de reNut ignore) : un paquet
// d'une autre version est refait (od_installer = si_absent). 2 : passes « alpha » et « earlyz » (2026-11-10).
uint32_t VersionChaine() { return chaine_deko3d::VersionTexte(); }

bool Compresser(uint64_t empreinte, bool pixel, const std::string& texte, int niveau, ElementPaquet& el) {
  el.e = {};
  el.e.hash = empreinte;
  el.e.size = uint32_t(texte.size());
  el.e.stage = pixel ? 4 : 0;
  MasqueBooleens(texte, el.e.boolMask);
  el.trame.resize(ZSTD_compressBound(texte.size()));
  const size_t n = ZSTD_compress(el.trame.data(), el.trame.size(), texte.data(), texte.size(), niveau);
  if (ZSTD_isError(n)) return false;
  el.trame.resize(n);
  el.e.compressedSize = uint32_t(n);
  return true;
}

std::vector<uint8_t> Paquet(std::vector<ElementPaquet>& elements) {
  std::sort(elements.begin(), elements.end(),
            [](const ElementPaquet& a, const ElementPaquet& b) { return a.e.hash < b.e.hash; });
  std::vector<EntreePaquet> table;
  std::vector<uint8_t> donnees;
  for (const ElementPaquet& el : elements) {
    EntreePaquet e = el.e;
    e.offset = uint32_t(donnees.size());
    donnees.insert(donnees.end(), el.trame.begin(), el.trame.end());
    donnees.resize((donnees.size() + 7) & ~size_t(7));
    table.push_back(e);
  }
  const uint32_t table_off = 32, data_off = table_off + uint32_t(table.size() * sizeof(EntreePaquet));
  std::vector<uint8_t> o(32, 0);
  const uint32_t tete[8] = {0, 1, uint32_t(table.size()), 0, table_off, data_off, VersionChaine(), 0};
  std::memcpy(o.data(), tete, sizeof(tete));
  std::memcpy(o.data(), "BDKG", 4);
  Ajouter(o, table.data(), table.size());
  o.insert(o.end(), donnees.begin(), donnees.end());
  return o;
}

bool LireFichier(const std::filesystem::path& chemin, std::vector<uint8_t>& o) {
  std::FILE* f = std::fopen(chemin.string().c_str(), "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  o.resize(n > 0 ? size_t(n) : 0);
  const bool ok = n > 0 && std::fread(o.data(), 1, o.size(), f) == o.size();
  std::fclose(f);
  return ok;
}

// Un paquet de l'installation (sans dictionnaire) ; faux pour un autre.
bool LirePaquet(const std::vector<uint8_t>& o, std::vector<ElementPaquet>& elements) {
  uint32_t tete[8];
  if (o.size() < sizeof(tete)) return false;
  std::memcpy(tete, o.data(), sizeof(tete));
  if (std::memcmp(o.data(), "BDKG", 4) || tete[1] != 1 || tete[3] != 0 || tete[6] != VersionChaine() ||
      uint64_t(tete[4]) + uint64_t(tete[2]) * sizeof(EntreePaquet) > o.size() || tete[5] > o.size())
    return false;
  for (uint32_t i = 0; i < tete[2]; ++i) {
    ElementPaquet el;
    std::memcpy(&el.e, o.data() + tete[4] + i * sizeof(EntreePaquet), sizeof(EntreePaquet));
    const uint64_t debut = uint64_t(tete[5]) + el.e.offset;
    if (debut + el.e.compressedSize > o.size()) return false;
    el.trame.assign(o.begin() + long(debut), o.begin() + long(debut + el.e.compressedSize));
    elements.push_back(std::move(el));
  }
  return true;
}

bool LireFiches(const std::vector<uint8_t>& o, std::vector<Fiche>& fiches) {
  uint32_t tete[5];
  if (o.size() < sizeof(tete)) return false;
  std::memcpy(tete, o.data(), sizeof(tete));
  if (std::memcmp(o.data(), renut::gpu::kFichesMagic, 4) || tete[1] != renut::gpu::kFichesVersion ||
      tete[2] != kShaderCacheLayout || tete[3] != sizeof(ShaderCacheEntry))
    return false;
  size_t at = sizeof(tete);
  for (uint32_t i = 0; i < tete[4]; ++i) {
    Fiche f;
    if (at + sizeof(f.entree) > o.size()) return false;
    std::memcpy(&f.entree, o.data() + at, sizeof(f.entree));
    at += sizeof(f.entree);
    f.entrees.resize(f.entree.vertexInputCount);
    f.literaux.resize(f.entree.literalCount);
    const size_t a = f.entrees.size() * sizeof(ShaderVertexInput), l = f.literaux.size() * sizeof(ShaderLiteral);
    if (at + a + l > o.size()) return false;
    std::memcpy(f.entrees.data(), o.data() + at, a);
    std::memcpy(f.literaux.data(), o.data() + at + a, l);
    at += a + l;
    fiches.push_back(std::move(f));
  }
  return true;
}

// Les conteneurs que reNut a copiés dans shader_capture/ (shaders que le jeu compile pendant la partie).
void Captures(conteneurs::Liste& liste) {
  const auto dossier = banjo::renut_gpu::NextToExecutable(REXCVAR_GET(renut_shader_capture_dir));
  std::error_code ec;
  std::vector<uint8_t> o;
  for (const auto& e : std::filesystem::directory_iterator(dossier, ec)) {
    if (!e.is_regular_file(ec) || e.path().extension() != ".bin" || !LireFichier(e.path(), o)) continue;
    conteneurs::Chercher(o.data(), o.size(), "shader_capture/" + e.path().filename().string(), liste);
  }
}

// Fil de fond des shaders que le jeu compile pendant la partie (point 5) : demandes de reNut (MissingShaderHandler).
struct Demande {
  std::vector<uint8_t> conteneur;
  uint64_t empreinte;
};
struct EnJeu {
  std::mutex verrou;
  std::condition_variable reveil;
  std::deque<Demande> file;
  bool lance = false;
} g_en_jeu;

void* FilEnJeu(void*) {
  // Mode quatre cœurs (2026-11-01) : les shaders que le jeu compile en partie, sur le cœur gardé à la plus basse
  // priorité de l'application (0x3B : l'audio de ce cœur passe d'abord). Sans effet avec trois cœurs.
  if (switch_cpu::VersCoeurSysteme(0x3B)) REXLOG_INFO("[shaders en jeu] fil de traduction sur le cœur du système");
  chaine_deko3d::InitialiserGlslang();
  for (;;) {
    Demande d;
    {
      std::unique_lock<std::mutex> l(g_en_jeu.verrou);
      g_en_jeu.reveil.wait(l, [] { return !g_en_jeu.file.empty(); });
      d = std::move(g_en_jeu.file.front());
      g_en_jeu.file.pop_front();
    }
    const auto t = Horloge::now();
    chaine_deko3d::Resultat r;
    chaine_deko3d::TraduireGlsl(d.conteneur.data(), r);
    if (!r.ok) {
      REXLOG_WARN("[shaders en jeu] {:016X} : échec à l'étape {} : {}", d.empreinte, r.etape, r.erreur.substr(0, 300));
      continue;
    }
    Fiche f = FicheDe(d.empreinte, r.shader);
    // Le GLSL pour uam (deko3d, Switch). Sur PC, rien à dessiner : le SPIR-V de cette chaîne est fait pour les liaisons
    // de deko3d (BANJO_DEKO3D), que le Vulkan de reNut n'a pas ; la fiche sert aux essais.
#if defined(__SWITCH__)
    uint32_t masque[8];
    MasqueBooleens(r.glsl, masque);
    banjo::renut_gpu::deko3d::AddRuntimeGlsl(d.empreinte, r.shader.pixel, r.glsl, masque);
#endif
    renut::gpu::AddRuntimeShaderEntry(f.entree, f.entrees.data(), f.literaux.data());
    renut::gpu::RuntimeShaderReady(d.empreinte);
    REXLOG_INFO("[shaders en jeu] {}_{:016X} traduit en {:.0f} ms (ajouté au paquet au prochain lancement)",
                r.shader.pixel ? "ps" : "vs", d.empreinte, Ms(t));
  }
  return nullptr;
}

void Manquant(const uint8_t* conteneur, size_t taille, bool /*pixel*/, uint64_t empreinte) {
  std::lock_guard<std::mutex> l(g_en_jeu.verrou);
  if (!g_en_jeu.lance) {
    // Grande pile : glslang et les passes de SPIRV-Tools récursent profondément (comme les fils de l'installation).
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 8u << 20);
    pthread_t fil;
    g_en_jeu.lance = pthread_create(&fil, &attr, FilEnJeu, nullptr) == 0;
    pthread_attr_destroy(&attr);
    if (!g_en_jeu.lance) {
      REXLOG_ERROR("[shaders en jeu] fil de traduction non créé : {:016X} reste sans shader", empreinte);
      return;
    }
    pthread_detach(fil);
  }
  g_en_jeu.file.push_back({std::vector<uint8_t>(conteneur, conteneur + taille), empreinte});
  g_en_jeu.reveil.notify_one();
}

// Traduit `liste` (sur plusieurs fils) et l'ajoute à `fiches` et `paquet` ; rend le nombre d'échecs.
size_t TraduireEtAjouter(const conteneurs::Liste& liste, std::vector<Fiche>& fiches,
                         std::vector<ElementPaquet>& paquet, size_t& compares, size_t& ecarts,
                         const std::function<void(size_t, size_t)>& progression,
                         std::vector<std::tuple<bool, std::string, std::string>>* textes = nullptr) {
  std::vector<uint64_t> empreintes;
  std::vector<const uint8_t*> blocs;
  for (const auto& [empreinte, c] : liste) {
    empreintes.push_back(empreinte);
    blocs.push_back(c.donnees.data());
  }
  chaine_deko3d::Reglages rg;
  rg.fils = REXCVAR_GET(od_essai_deko3d_fils);
  rg.placer = true;
  rg.allocateur_par_fil = REXCVAR_GET(od_essai_deko3d_allocateur);
  std::vector<chaine_deko3d::Resultat> resultats;
  size_t dixieme = 0;
  chaine_deko3d::TraduireGlslTous(blocs, resultats, rg, [&](size_t fait, size_t total) {
    progression(fait, total);
    if (total >= 20 && fait * 10 / total != dixieme) {
      dixieme = fait * 10 / total;
      REXLOG_INFO("[installation] traduction : {} / {}", fait, total);
    }
  });
  size_t echecs = 0;
  for (size_t i = 0; i < resultats.size(); ++i) {
    const auto& r = resultats[i];
    ElementPaquet el;
    if (!r.ok || !Compresser(empreintes[i], r.shader.pixel, r.glsl, 3, el)) {
      ++echecs;
      REXLOG_WARN("[installation] {:016X} : échec à l'étape {} : {}", empreintes[i], r.ok ? "zstd" : r.etape,
                  r.erreur.substr(0, 300));
      continue;
    }
    fiches.push_back(FicheDe(empreintes[i], r.shader));
    paquet.push_back(std::move(el));
    if (textes)
      textes->emplace_back(r.shader.pixel, r.glsl, fmt::format("{}_{:016x}", r.shader.pixel ? "ps" : "vs", empreintes[i]));
    if (REXCVAR_GET(od_installer_verif)) {
      if (const ShaderCacheEntry* integre = renut::gpu::FindBuiltInShaderEntry(empreintes[i])) {
        ++compares;
        const std::string e = Ecarts(fiches.back(), *integre);
        if (!e.empty()) {
          ++ecarts;
          REXLOG_WARN("[installation] écart avec la table de l'exécutable pour {:016X} :{}", empreintes[i], e);
        }
      }
    }
  }
  return echecs;
}

// Une variante comme Deko3DDevice::loadVariant (plume_deko3d.cpp) en fait le texte : les constantes qui diffèrent de
// leur valeur dans le GLSL (« #define SPIRV_CROSS_CONSTANT_ID_<id> <valeur>u » par ligne), après la première ligne.
std::string Variante(const std::string& glsl, const std::string& definitions) {
  const size_t nl = glsl.find('\n');
  if (definitions.empty() || nl == std::string::npos) return glsl;
  std::string v;
  v.reserve(glsl.size() + definitions.size());
  v.append(glsl, 0, nl + 1);
  v += definitions;
  v.append(glsl, nl + 1, std::string::npos);
  return v;
}

// Les variantes que plume compilera pour ce shader en partie, d'après les parties relevées (règles générales, valables
// pour tout shader : pas de donnée du jeu) : un vertex shader sans spécialisation ; un pixel shader avec la constante
// 901 (g_banjoOutputTrim, render/pipeline.cpp) à 0xFFF0 (une cible, ses 4 canaux), 0xFFFE (le rouge seul) ou 0xFFFF
// (aucun canal) : 54 + 8 des 62 variantes de la partie du 2026-10-27.
std::vector<std::string> VariantesCourantes(bool pixel, const std::string& glsl) {
  static constexpr char kCle[] = "#define SPIRV_CROSS_CONSTANT_ID_901 ";
  const size_t p = glsl.find(kCle);
  if (!pixel || p == std::string::npos) return {glsl};
  const unsigned long defaut = std::strtoul(glsl.c_str() + p + sizeof(kCle) - 1, nullptr, 0);
  std::vector<std::string> v;
  for (unsigned long trim : {0xFFF0ul, 0xFFFEul, 0xFFFFul})
    v.push_back(trim == defaut ? glsl : Variante(glsl, fmt::format("#define SPIRV_CROSS_CONSTANT_ID_901 {}u\n", trim)));
  return v;
}

// Relevé des variantes que le jeu prend en partie (od_releve_variantes) : variantes_jeu.txt à côté du .nro, une ligne
// par variante neuve du lancement, « <nom du shader>\t<1 : compilée par uam, 0 : du cache>\t<constantes> » (les
// définitions de plume, « id valeur » séparés par « ; »). Sert à trouver de nouvelles règles (VariantesCourantes) et,
// sur cette console, à recompiler à l'installation suivante celles de ses parties (le fichier reste sur la carte : il
// nomme des shaders du jeu par leur empreinte, il ne va jamais dans le .nro).
constexpr const char* kReleve = "variantes_jeu.txt";

std::string DefinitionsEnTexte(const std::string& definitions) {
  static constexpr char kCle[] = "#define SPIRV_CROSS_CONSTANT_ID_";
  std::string o;
  for (size_t p = definitions.find(kCle); p != std::string::npos; p = definitions.find(kCle, p + 1)) {
    const size_t debut = p + sizeof(kCle) - 1, fin = definitions.find('\n', debut);
    std::string d = definitions.substr(debut, fin == std::string::npos ? std::string::npos : fin - debut);
    if (!d.empty() && d.back() == 'u') d.pop_back();
    if (!o.empty()) o += ';';
    o += d;
  }
  return o;
}

std::string DefinitionsDepuisTexte(const std::string& texte) {
  std::string o;
  size_t p = 0;
  while (p < texte.size()) {
    const size_t fin = std::min(texte.find(';', p), texte.size());
    const std::string d = texte.substr(p, fin - p);
    if (!d.empty()) o += "#define SPIRV_CROSS_CONSTANT_ID_" + d + "u\n";
    p = fin + 1;
  }
  return o;
}

void Relever(const char* nom, const std::string& definitions, bool compilee) {
  static std::mutex verrou;
  static std::FILE* f = nullptr;
  std::lock_guard<std::mutex> l(verrou);
  if (!f) f = std::fopen(banjo::renut_gpu::NextToExecutable(kReleve).string().c_str(), "a");
  if (!f) return;
  std::fprintf(f, "%s\t%d\t%s\n", nom, compilee ? 1 : 0, DefinitionsEnTexte(definitions).c_str());
  std::fflush(f);
}

// Les variantes du relevé de cette console, par nom de shader (sans doublons).
std::map<std::string, std::set<std::string>> LireReleve() {
  std::map<std::string, std::set<std::string>> r;
  std::FILE* f = std::fopen(banjo::renut_gpu::NextToExecutable(kReleve).string().c_str(), "r");
  if (!f) return r;
  char ligne[2048];
  while (std::fgets(ligne, sizeof ligne, f)) {
    std::string l(ligne);
    while (!l.empty() && (l.back() == '\n' || l.back() == '\r')) l.pop_back();
    const size_t t1 = l.find('\t'), t2 = t1 == std::string::npos ? t1 : l.find('\t', t1 + 1);
    if (t2 == std::string::npos) continue;
    std::string nom = l.substr(0, t1);
    std::transform(nom.begin(), nom.end(), nom.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    r[nom].insert(DefinitionsDepuisTexte(l.substr(t2 + 1)));
  }
  std::fclose(f);
  return r;
}

// L'écran d'installation : ce que le fil de travail en est (pour mille) et l'étape, lus par le dessin.
struct Progression {
  std::atomic<int> pour_mille{0};
  std::atomic<bool> fini{false};
  std::mutex verrou;
  std::string etape;
} g_progression;

void Avancer(const char* etape, int pour_mille) {
  g_progression.pour_mille.store(pour_mille, std::memory_order_relaxed);
  std::lock_guard<std::mutex> l(g_progression.verrou);
  g_progression.etape = etape;
}

// Une fenêtre ImGui sur tout l'écran, dessinée par l'overlay de reNut tant que le jeu ne présente rien. Textes vus par
// les joueurs : en anglais.
class Ecran : public rex::ui::ImGuiDialog {
 public:
  explicit Ecran(rex::ui::ImGuiDrawer* d) : rex::ui::ImGuiDialog(d) {}

 protected:
  void OnDraw(ImGuiIO& io) override {
    const float w = io.DisplaySize.x, h = io.DisplaySize.y, echelle = h / 720.0f;
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.04f, 0.05f, 1.0f));
    const ImGuiWindowFlags drapeaux = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                                      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("##od_installation", nullptr, drapeaux)) {
      std::string etape;
      {
        std::lock_guard<std::mutex> l(g_progression.verrou);
        etape = g_progression.etape;
      }
      const float p = std::clamp(g_progression.pour_mille.load(std::memory_order_relaxed) / 1000.0f, 0.0f, 1.0f);
      auto centre = [&](const char* texte, float y, float taille, ImVec4 couleur) {
        ImGui::SetWindowFontScale(taille * echelle);
        const float l = ImGui::CalcTextSize(texte).x;
        ImGui::SetCursorPos(ImVec2((w - l) * 0.5f, y * echelle));
        ImGui::TextColored(couleur, "%s", texte);
      };
      centre("Operation Darkness", 250, 2.6f, ImVec4(0.85f, 0.18f, 0.12f, 1.0f));
      centre("Preparing the game's shaders for the Switch (first launch only)", 320, 1.3f,
             ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
      if (g_avec_uam)
        centre("This can take up to 9 minutes. Please do not turn the console off.", 350, 1.1f,
               ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
      ImGui::SetWindowFontScale(1.2f * echelle);
      const float largeur = 760.0f * echelle;
      ImGui::SetCursorPos(ImVec2((w - largeur) * 0.5f, 400 * echelle));
      ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.75f, 0.16f, 0.10f, 1.0f));
      const std::string pct = fmt::format("{}%", int(p * 100.0f));
      ImGui::ProgressBar(p, ImVec2(largeur, 30.0f * echelle), pct.c_str());
      ImGui::PopStyleColor();
      centre(etape.c_str(), 455, 1.1f, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
      ImGui::SetWindowFontScale(1.0f);
    }
    ImGui::End();
    ImGui::PopStyleColor();
  }
};

}  // namespace

void AuChargement(const uint8_t* image, size_t taille_image, const std::filesystem::path& racine_jeu,
                  rex::ui::ImGuiDrawer* imgui, const std::function<void()>& presenter) {
  // Les retouches du paquet du PC, pour l'installation comme pour les shaders traduits en jeu.
  static retouches::Reglages retouches;
  retouches.flou = REXCVAR_GET(od_installer_flou);
  retouches.ombres = REXCVAR_GET(od_installer_ombres);
  chaine_deko3d::FixerRetouches(&retouches);

#if defined(__SWITCH__)
  // Le relevé des variantes prises en partie (variantes_jeu.txt).
  if (REXCVAR_GET(od_releve_variantes))
    plume::SetDeko3DVariantNote([](const char* nom, const std::string& definitions, bool compilee) {
      Relever(nom, definitions, compilee);
    });
#endif
  // Les shaders que le jeu compile pendant la partie, traduits sur un fil de fond (point 5).
  if (REXCVAR_GET(od_shaders_en_jeu)) renut::gpu::SetMissingShaderHandler(Manquant);

  const std::string mode = REXCVAR_GET(od_installer);
  if (mode != "toujours" && mode != "si_absent") return;
  const auto chemin_fiches = banjo::renut_gpu::NextToExecutable(renut::gpu::kFichesFile);
  const auto chemin_paquet = banjo::renut_gpu::NextToExecutable("deko3d_glsl.bin");
  const auto debut = Horloge::now();

  // Déjà installé : seulement les shaders capturés pendant les parties d'avant qui n'y sont pas encore.
  std::vector<Fiche> fiches;
  std::vector<ElementPaquet> paquet;
  std::vector<uint8_t> octets;
  bool complement = false;
  if (mode == "si_absent") {
    fiches.clear();
    paquet.clear();
    complement = LireFichier(chemin_fiches, octets) && LireFiches(octets, fiches) &&
                 LireFichier(chemin_paquet, octets) && LirePaquet(octets, paquet);
    if (!complement) {
      fiches.clear();
      paquet.clear();
    }
  }
  conteneurs::Liste liste;
  Captures(liste);
  const size_t captures = liste.size();
  if (complement) {
    for (const Fiche& f : fiches) liste.erase(f.entree.hash);
    if (liste.empty()) {
      REXLOG_INFO("[installation] déjà faite : {} shaders (renut_fiches.bin et deko3d_glsl.bin à côté du .nro)",
                  fiches.size());
      return;
    }
    REXLOG_INFO("[installation] {} shaders compilés par le jeu pendant les parties d'avant à ajouter", liste.size());
  }

#if defined(__SWITCH__)
  g_avec_uam = REXCVAR_GET(od_installer_uam);
#endif
  auto travail = [&] {
  const bool boost = REXCVAR_GET(od_essai_deko3d_boost) && switch_cpu::Boost(true);
  double ms_recherche = 0;
  if (!complement) {
    Avancer("Looking for the shaders in the game files", 0);
    REXLOG_INFO("[installation] début : shaders de {}, de default.xex et {} capturés", racine_jeu.string(), captures);
    conteneurs::Reglages rc;
    rc.fils = REXCVAR_GET(od_essai_deko3d_fils);
    rc.placer = true;
    rc.allocateur_par_fil = REXCVAR_GET(od_essai_deko3d_allocateur);
    // Console : recherche 12 s, traduction 19 s, uam ~8 min ; sans uam (PC), la recherche fait ~40 %.
    const auto b = conteneurs::ChercherDansLeJeu(image, taille_image, racine_jeu, liste, rc, [](size_t fait, size_t total) {
      g_progression.pour_mille.store(int((g_avec_uam ? kFinRecherche : 400) * fait / std::max<size_t>(total, 1)),
                                     std::memory_order_relaxed);
    });
    ms_recherche = Ms(debut);
    REXLOG_INFO("[installation] {} shaders trouvés ({} .igb, default.xex, capturés) en {:.0f} ms ({} échecs de lecture)",
                liste.size(), b.fichiers, ms_recherche, b.echecs);
  }
  const auto t2 = Horloge::now();
  const size_t avant = fiches.size();
  size_t compares = 0, ecarts = 0;
  const int base = complement ? 0 : (g_avec_uam ? kFinRecherche : 400);
  const int fin_traduction = g_avec_uam ? (complement ? 300 : kFinTraduction) : 980;
  Avancer("Translating the shaders", base);
  std::vector<std::tuple<bool, std::string, std::string>> textes;
  const size_t echecs = TraduireEtAjouter(liste, fiches, paquet, compares, ecarts, [&](size_t fait, size_t total) {
    g_progression.pour_mille.store(base + int((fin_traduction - base) * fait / std::max<size_t>(total, 1)),
                                   std::memory_order_relaxed);
  }, &textes);
  const double ms_traduction = Ms(t2);

  Avancer("Writing the shader files", fin_traduction);
  const auto t3 = Horloge::now();
  const auto fichier_fiches = Fiches(fiches);
  const auto fichier_paquet = Paquet(paquet);
  const double ms_paquet = Ms(t3);
  // Essai PC avec le SPIR-V intégré : rien d'écrit (renut_fiches.bin, lu au premier usage de la table, garderait
  // ses fiches sans SPIR-V à la place de celles d'ici).
  const bool essai_pc = REXCVAR_GET(od_installer_spirv_integre);
  const bool ok = essai_pc || (EcrireFichier(chemin_fiches, fichier_fiches) && EcrireFichier(chemin_paquet, fichier_paquet));
  // Pour ce lancement aussi (la table a pu être lue avant, à la préparation des pipelines).
  if (ok)
    for (Fiche& f : fiches) {
      if (essai_pc)
        if (const ShaderCacheEntry* integre = renut::gpu::FindBuiltInShaderEntry(f.entree.hash)) {
          f.entree.spirvOffset = integre->spirvOffset;
          f.entree.spirvSize = integre->spirvSize;
        }
      renut::gpu::AddRuntimeShaderEntry(f.entree, f.entrees.data(), f.literaux.data());
    }
  REXLOG_INFO("[installation] {}{} : {} shaders dont {} nouveaux ({} échecs) ; recherche {:.0f} ms, traduction {:.0f} ms, "
              "paquet {:.0f} ms, total {:.0f} ms{} ; {} ({} Ko), {} ({} Ko) ; comparés à l'exécutable : {}, écarts : {}",
              ok ? "finie" : "ÉCHEC de l'écriture", complement ? " (complément)" : "", fiches.size(),
              fiches.size() - avant, echecs, ms_recherche, ms_traduction, ms_paquet, Ms(debut),
              boost ? " (boost)" : "", chemin_fiches.string(), fichier_fiches.size() >> 10, chemin_paquet.string(),
              fichier_paquet.size() >> 10, compares, ecarts);
  // uam : les variantes courantes de chaque shader traduit ici, dans le cache DKSH (au fil du jeu sinon).
  double ms_uam = 0;
  size_t compiles = 0, deja = 0, rates = 0;
#if defined(__SWITCH__)
  if (g_avec_uam) {
    const auto t4 = Horloge::now();
    plume::RenderDevice* device = renut::gpu::Video::Device();
    // Les variantes courantes de chaque shader traduit ici, et celles que le relevé de cette console a vues.
    const auto releve = LireReleve();
    std::vector<std::pair<bool, std::string>> sources;
    size_t du_releve = 0;
    for (const auto& [pixel, glsl, nom] : textes) {
      std::set<std::string> deja_la;
      for (std::string& v : VariantesCourantes(pixel, glsl))
        if (deja_la.insert(v).second) sources.emplace_back(pixel, std::move(v));
      auto it = releve.find(nom);
      if (it == releve.end()) continue;
      for (const std::string& d : it->second) {
        std::string v = Variante(glsl, d);
        if (deja_la.insert(v).second) {
          sources.emplace_back(pixel, std::move(v));
          ++du_releve;
        }
      }
    }
    // Les plus petits d'abord : la barre avance régulièrement (les vertex shaders géants à la fin).
    std::stable_sort(sources.begin(), sources.end(),
                     [](const auto& a, const auto& b) { return a.second.size() < b.second.size(); });
    for (size_t i = 0; i < sources.size(); ++i) {
      Avancer(fmt::format("Compiling the shaders for the Switch GPU ({} / {})", i + 1, sources.size()).c_str(),
              fin_traduction + int((990 - fin_traduction) * i / std::max<size_t>(sources.size(), 1)));
      switch (banjo::renut_gpu::deko3d::PrecompileVariant(device, sources[i].first, sources[i].second)) {
        case banjo::renut_gpu::deko3d::PrecompileResult::Compiled: ++compiles; break;
        case banjo::renut_gpu::deko3d::PrecompileResult::Cached: ++deja; break;
        case banjo::renut_gpu::deko3d::PrecompileResult::Failed: ++rates; break;
      }
    }
    banjo::renut_gpu::deko3d::SaveShaderCache();
    ms_uam = Ms(t4);
    REXLOG_INFO("[installation] uam : {} variantes ({} du relevé de la console) : {} compilées, {} déjà dans le cache, "
                "{} en échec, en {:.0f} ms", sources.size(), du_releve, compiles, deja, rates, ms_uam);
  }
#endif
  if (boost) switch_cpu::Boost(false);
  Avancer("Starting the game", 1000);
  };

  // Le travail sur un fil à lui ; ce fil-ci (celui de l'interface, que l'overlay de reNut attend pour présenter)
  // dessine l'écran d'installation pendant ce temps.
  g_progression.fini.store(false);
  std::unique_ptr<Ecran> ecran;
  if (imgui && presenter) ecran = std::make_unique<Ecran>(imgui);
  std::thread fil([&] {
    travail();
    g_progression.fini.store(true, std::memory_order_release);
  });
  while (!g_progression.fini.load(std::memory_order_acquire)) {
    if (ecran) presenter();
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }
  fil.join();
  if (ecran) presenter();  // « Starting the game » à 100 %
  ecran.reset();
}

}  // namespace opdarkness::installation
