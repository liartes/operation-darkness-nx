// opdarkness - chaîne de traduction des shaders pour deko3d sur la console (od_chaine_deko3d.h).

#include "od_chaine_deko3d.h"

#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv-tools/optimizer.hpp>
#include <spirv_glsl.hpp>

#include "ResourceLimits.h"  // glslang/StandAlone : limites par défaut (DefaultTBuiltInResource)

#include <pthread.h>
#if defined(__SWITCH__)
#include <malloc.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <system_error>
#include <tuple>
#include <unistd.h>

#include <fmt/format.h>

#define XXH_INLINE_ALL
#include <xxhash.h>

#ifndef OD_CHAINE_UAM
#define OD_CHAINE_UAM 0  // jeu sur PC : pas d'uam (celui de plume n'est lié que sur la Switch)
#endif
#if OD_CHAINE_UAM
#include <uam_api.h>
#endif

#if !OD_CHAINE_OUTIL
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#endif

#include "od_conteneurs.h"
#include "od_deko3d_glsl.h"
#include "od_alloc_fils.h"
#include "od_passes_glsl.h"
#include "od_prep_glslang.h"
#include "od_switch_cpu.h"

#if !OD_CHAINE_OUTIL
REXCVAR_DEFINE_BOOL(od_chaine_passes, true, "OpDarkness",
                    "Traduction des shaders pour deko3d : passes « alpha » (constante 903) et « earlyz » (909) sur le "
                    "GLSL des pixel shaders (rejet précoce des pixels cachés), puis « vsout » (905), « pred », « sel » et "
                    "« ifmerge » de banjo-nx (od_passes_glsl.cpp ; -21 % d'instructions uam pour les pixel shaders, "
                    "-30 % pour les vertex shaders, pondéré par le temps GPU). Change le texte : à réinstaller");
REXCVAR_DEFINE_STRING(od_essai_deko3d, "", "OpDarkness",
                      "Essai de la traduction des shaders pour deko3d au démarrage (docs/DEKO3D.md) : dossier (relatif : "
                      "à côté de l'exécutable) dont conteneurs/ est traduit dans sortie/ ; vide : pas d'essai")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_essai_deko3d_uam, true, "OpDarkness", "Essai deko3d : aussi la phase 2 (uam, DKSH)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_essai_deko3d_boost, true, "OpDarkness",
                    "Essai deko3d : mode boost du processeur pendant l'essai (Switch : 1 785 MHz, GPU au plus bas)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(od_essai_deko3d_fils, 3, "OpDarkness",
                     "Essai deko3d : fils de la phase 1 (jusqu'au GLSL), placés sur les cœurs 0, 1, 2 tour à tour")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_BOOL(od_essai_deko3d_allocateur, true, "OpDarkness",
                    "Essai deko3d : allocateur à cache par fil (rpmalloc) pour les fils de la phase 1 (Switch)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_STRING(od_essai_conteneurs, "", "OpDarkness",
                      "Essai de la recherche des conteneurs de shaders par la console (od_conteneurs.cpp), après le "
                      "chargement de default.xex : dossier (relatif : à côté de l'exécutable) où écrire les conteneurs "
                      "trouvés et fichiers.csv ; boost, fils et allocateur de l'essai deko3d ; vide : pas d'essai")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(od_essai_deko3d_coeur_uam, 2, "OpDarkness", "Essai deko3d : cœur du fil d'uam (phase 2)")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
#define OD_JOURNAL(...) REXLOG_INFO(__VA_ARGS__)
#else
#define OD_JOURNAL(...) fmt::print(stderr, "{}\n", fmt::format(__VA_ARGS__))
#endif

namespace opdarkness::chaine_deko3d {
namespace {

const retouches::Reglages* g_retouches = nullptr;  // FixerRetouches

using Horloge = std::chrono::steady_clock;

double Depuis(Horloge::time_point& t) {
  const auto maintenant = Horloge::now();
  const double ms = std::chrono::duration<double, std::milli>(maintenant - t).count();
  t = maintenant;
  return ms;
}

// glslang, entrée HLSL : comme glslangValidator -D -V -e main -DBANJO_DEKO3D=1 -DRENUT_RECOMP=1 (Vulkan 1.0,
// SPIR-V 1.0 : en 1.5, glslang décore encore les tampons de stockage en BufferBlock, que le validateur refuse), sans son optimiseur (la légalisation suit, comme spirv-opt --legalize-hlsl). Pas d'inversion de y :
// deko3d prend l'espace de découpe de Direct3D tel quel (dxc_compiler.cpp de XenosRecomp, cas deko3d).
bool Glslang(const std::string& hlsl, bool vertex, std::vector<uint32_t>& spirv, std::string& erreur) {
  const EShLanguage etape = vertex ? EShLangVertex : EShLangFragment;
  glslang::TShader shader(etape);
  const char* texte = hlsl.c_str();
  const int longueur = int(hlsl.size());
  shader.setStringsWithLengths(&texte, &longueur, 1);
  shader.setPreamble("#define BANJO_DEKO3D 1\n#define RENUT_RECOMP 1\n");
  shader.setEntryPoint("main");
  shader.setSourceEntryPoint("main");
  shader.setEnvInput(glslang::EShSourceHlsl, etape, glslang::EShClientVulkan, 100);
  shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
  shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
  const EShMessages messages = EShMessages(EShMsgSpvRules | EShMsgVulkanRules | EShMsgReadHlsl);
  if (!shader.parse(&glslang::DefaultTBuiltInResource, 100, false, messages)) {
    erreur = shader.getInfoLog();
    return false;
  }
  glslang::TProgram programme;
  programme.addShader(&shader);
  if (!programme.link(messages)) {
    erreur = std::string("liens : ") + programme.getInfoLog();
    return false;
  }
  spv::SpvBuildLogger journal;
  glslang::SpvOptions options;
  options.disableOptimizer = true;
  glslang::GlslangToSpv(*programme.getIntermediate(etape), spirv, &journal, &options);
  const std::string messages_spirv = journal.getAllMessages();
  if (spirv.empty() || messages_spirv.find("error") != std::string::npos) {
    erreur = "SPIR-V : " + (messages_spirv.empty() ? std::string("vide") : messages_spirv);
    return false;
  }
  return true;
}

// spirv-opt --legalize-hlsl : sans elle, des fonctions à paramètre texture restent, qu'uam refuse.
bool Legaliser(std::vector<uint32_t>& spirv, std::string& erreur) {
  spvtools::Optimizer optimiseur(SPV_ENV_VULKAN_1_0);
  optimiseur.SetMessageConsumer([&](spv_message_level_t niveau, const char*, const spv_position_t& p, const char* m) {
    if (niveau <= SPV_MSG_ERROR) erreur += fmt::format("{} (mot {})\n", m, p.index);
  });
  optimiseur.RegisterLegalizationPasses();
  // Comme spirv-opt avec --legalize-hlsl : la validation d'avant les passes tolère ce qu'elles vont légaliser.
  spvtools::ValidatorOptions validation;
  validation.SetBeforeHlslLegalization(true);
  spvtools::OptimizerOptions options;
  options.set_validator_options(validation);
  std::vector<uint32_t> sortie;
  if (!optimiseur.Run(spirv.data(), spirv.size(), &sortie, options)) {
    if (erreur.empty()) erreur = "échec";
    return false;
  }
  spirv = std::move(sortie);
  return true;
}

// spirv-cross x.spv --version 460 --vulkan-semantics --relax-nan-checks
bool SpirvCross(const std::vector<uint32_t>& spirv, std::string& glsl, std::string& erreur) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
#endif
    spirv_cross::CompilerGLSL compilateur(spirv);
    auto options = compilateur.get_common_options();
    options.version = 460;
    options.vulkan_semantics = true;
    options.relax_nan_checks = true;
    compilateur.set_common_options(options);
    (void)compilateur.get_shader_resources();
    glsl = compilateur.compile();
    return true;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  } catch (const std::exception& e) {
    erreur = e.what();
    return false;
  }
#endif
}

// Passes du GLSL final des pixel shaders, reprises de tools/renut/deko3d_glsl_opt.py (banjo-nx) pour la traduction par
// la console (2026-11-10, d'après rr6-nx : sasrt_gpu_alpha_spec) :
//  - « alpha » : la fonction du test alpha (SharedConstants.alpha_test_func, g_UboShared_1.v[34].w dans le texte que
//    rend la chaîne) devient la constante de spécialisation 903 (valeur + 1 ; 0 : lue à l'exécution, comme avant).
//    render/pipeline.cpp la donne quand banjo_deko3d_alpha_spec est actif : un dessin sans test alpha n'a alors ni
//    comparaison ni discard, et le GPU rejette les pixels cachés avant le shader ;
//  - « earlyz » : un shader qui garde un discard reçoit layout(early_fragment_tests) quand la constante 909 est
//    définie (banjo_deko3d_early_z : dessin qui n'écrit ni profondeur ni stencil).
// Sans définition des constantes, le texte se compile comme avant. Rend le nombre de passes faites.
int PassesPixel(std::string& glsl) {
  static constexpr char kMot[] = "g_UboShared_1.v[34].w";
  static constexpr char kMain[] = "\nvoid main()";
  int faites = 0;
  const size_t debut = glsl.find(kMain);
  if (debut == std::string::npos) return 0;
  std::string decl;
  if (glsl.find(kMot, debut) != std::string::npos && glsl.find("SPIRV_CROSS_CONSTANT_ID_903") == std::string::npos) {
    for (size_t p = glsl.find(kMot, debut); p != std::string::npos; p = glsl.find(kMot, p)) {
      glsl.replace(p, sizeof(kMot) - 1, "DK_ALPHA_FUNC");
      p += 13;
    }
    decl += "#ifndef SPIRV_CROSS_CONSTANT_ID_903\n#define SPIRV_CROSS_CONSTANT_ID_903 0u\n#endif\n"
            "const uint g_banjoAlphaFunc = SPIRV_CROSS_CONSTANT_ID_903;\n"
            "#define DK_ALPHA_FUNC ((g_banjoAlphaFunc != 0u) ? (g_banjoAlphaFunc - 1u) : g_UboShared_1.v[34].w)\n";
    ++faites;
  }
  if (glsl.find("discard") != std::string::npos && glsl.find("gl_FragDepth") == std::string::npos &&
      glsl.find("SPIRV_CROSS_CONSTANT_ID_909") == std::string::npos) {
    decl += "#ifndef SPIRV_CROSS_CONSTANT_ID_909\n#define SPIRV_CROSS_CONSTANT_ID_909 0u\n#define DK_EARLY_Z 0\n"
            "#else\n#define DK_EARLY_Z 1\n#endif\n#if DK_EARLY_Z\nlayout(early_fragment_tests) in;\n#endif\n";
    ++faites;
  }
  if (!decl.empty()) glsl.insert(debut + 1, decl);
  return faites;
}

void Echec(Resultat& r, const char* etape, std::string erreur) {
  r.ok = false;
  r.etape = etape;
  r.erreur = std::move(erreur);
}

}  // namespace

uint32_t VersionTexte() { return REXCVAR_GET(od_chaine_passes) ? 3u : 0u; }

void TraduireGlsl(const uint8_t* conteneur, Resultat& r) {
  r = {};
  auto t = Horloge::now();
  xenosrecomp_renut::Recompiler(conteneur, xenosrecomp_renut::Prelude(), r.shader);
  if (const retouches::Reglages* rg = g_retouches) {
    const size_t taille = xenosrecomp_renut::TailleConteneur(conteneur, size_t(1) << 24);
    const std::string nom =
        fmt::format("{}_{:016x}", r.shader.pixel ? "ps" : "vs", XXH3_64bits(conteneur, taille));
    r.retouches = retouches::Appliquer(nom, r.shader.hlsl, *rg);
  }
  r.ms[0] = Depuis(t);
  const bool vertex = !r.shader.pixel;
  r.hlsl_prepare = glslang_prep::PreparerRenut(r.shader.hlsl);
  r.ms[1] = Depuis(t);
  std::string erreur;
  const bool ok_glslang = Glslang(r.hlsl_prepare, vertex, r.spirv, erreur);
  r.ms[2] = Depuis(t);
  if (!ok_glslang) return Echec(r, "glslang", erreur);
  const bool ok_legal = Legaliser(r.spirv, erreur);
  r.ms[3] = Depuis(t);
  if (!ok_legal) return Echec(r, "légalisation", erreur);
  const bool ok_cross = SpirvCross(r.spirv, r.glsl_vulkan, erreur);
  r.ms[4] = Depuis(t);
  if (!ok_cross) return Echec(r, "SPIRV-Cross", erreur);
  const bool ok_conv = deko3d_glsl::Convertir(r.glsl_vulkan, vertex, r.glsl, erreur);
  r.ms[5] = Depuis(t);
  if (!ok_conv) return Echec(r, "conversion", erreur);
  if (REXCVAR_GET(od_chaine_passes)) {
    if (r.shader.pixel) PassesPixel(r.glsl);
    passes_glsl::Appliquer(r.glsl, vertex);
  }
  r.ok = true;
}

void FixerRetouches(const retouches::Reglages* reglages) {
  static retouches::Reglages copie;
  if (reglages) copie = *reglages;
  g_retouches = reglages ? &copie : nullptr;
}

void InitialiserGlslang() { glslang::InitializeProcess(); }
void LibererGlslang() { glslang::FinalizeProcess(); }

void CompilerDksh(Resultat& r) {
  if (!r.ok) return;
#if OD_CHAINE_UAM
  auto t = Horloge::now();
  uam::Result u = uam::compile(r.shader.pixel ? uam::Stage::Fragment : uam::Stage::Vertex, r.glsl.c_str());
  r.ms[6] = Depuis(t);
  if (!u.ok) return Echec(r, "uam", u.log);
  r.dksh = std::move(u.dksh);
#endif
}

void Traduire(const uint8_t* conteneur, bool avec_uam, Resultat& r) {
  TraduireGlsl(conteneur, r);
  if (avec_uam) CompilerDksh(r);
}

namespace {

bool Ecrire(const std::filesystem::path& chemin, const void* donnees, size_t taille) {
  FILE* f = std::fopen(chemin.string().c_str(), "wb");
  if (!f) return false;
  const bool ok = std::fwrite(donnees, 1, taille, f) == taille;
  std::fflush(f);
  fsync(fileno(f));
  std::fclose(f);
  return ok;
}

bool Ecrire(const std::filesystem::path& chemin, const std::string& texte) {
  return Ecrire(chemin, texte.data(), texte.size());
}

// Mémoire du tas en cours d'emploi (Mo) : sur la Switch seulement (mallinfo de newlib).
double MemoireMo() {
#if defined(__SWITCH__)
  return mallinfo().uordblks / 1048576.0;
#else
  return 0;
#endif
}

std::vector<uint8_t> Lire(const std::filesystem::path& chemin) {
  std::ifstream f(chemin, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Les conteneurs d'un fichier, comme main.cpp de XenosRecomp : (empreinte XXH3 du conteneur, décalage).
void Conteneurs(const std::vector<uint8_t>& f, std::map<uint64_t, std::pair<size_t, size_t>>& vus,
                size_t numero_fichier) {
  constexpr size_t kEntete = 36;  // sizeof(ShaderContainer)
  for (size_t i = 0; f.size() > kEntete && i < f.size() - kEntete - 1;) {
    const size_t taille = xenosrecomp_renut::TailleConteneur(f.data() + i, f.size() - i);
    if (taille) {
      vus.try_emplace(XXH3_64bits(f.data() + i, taille), numero_fichier, i);
      i += taille;
    } else {
      i += 4;
    }
  }
}

double MsDepuis(Horloge::time_point debut) {
  return std::chrono::duration<double, std::milli>(Horloge::now() - debut).count();
}

// Un shader de l'essai : son conteneur et ce qu'en fait la chaîne.
struct Travail {
  size_t taille;
  uint64_t empreinte;
  const uint8_t* conteneur;
  Resultat r;
  int fil = -1;
};

struct Arguments {
  std::filesystem::path entree, sortie;
  Reglages reglages;
  std::vector<Travail>* travaux = nullptr;
  std::atomic<size_t>* suivant = nullptr;
  // TraduireGlslTous : progression(faits, total) après chaque shader, sous le verrou.
  std::atomic<size_t>* faits = nullptr;
  std::mutex* verrou = nullptr;
  const std::function<void(size_t, size_t)>* progression = nullptr;
  int fil = 0;
  int coeur = -1;
  int traduits = 0;
};

void* FilGlsl(void* p) {
  auto& a = *static_cast<Arguments*>(p);
  if (a.coeur >= 0) switch_cpu::FixerCoeur(a.coeur);
  const bool allocateur = a.reglages.allocateur_par_fil && alloc_fils::Activer();
  auto& travaux = *a.travaux;
  for (size_t i; (i = a.suivant->fetch_add(1)) < travaux.size();) {
    TraduireGlsl(travaux[i].conteneur, travaux[i].r);
    travaux[i].fil = a.fil;
    if (a.faits) {
      const size_t n = a.faits->fetch_add(1) + 1;
      if (a.progression && *a.progression) {
        std::lock_guard<std::mutex> l(*a.verrou);
        (*a.progression)(n, travaux.size());
      }
    }
  }
  if (allocateur) alloc_fils::Desactiver();
  return nullptr;
}

// Un fil à grande pile : 8 Mo (l'analyseur de Mesa et nv50_ir dans uam récursent profondément, uam_api.h ;
// glslang aussi).
bool LancerFil(pthread_t& fil, void* (*fn)(void*), void* arg) {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 8u << 20);
  const bool ok = pthread_create(&fil, &attr, fn, arg) == 0;
  pthread_attr_destroy(&attr);
  return ok;
}

// Les deux phases de l'essai, sur un fil à grande pile (celui d'uam) : 1. jusqu'au GLSL, sur `reglages.fils` fils
// placés sur les cœurs ; 2. uam, sur ce fil seul (uam n'est pas réentrant).
void* EssaiFil(void* p) {
  auto& a = *static_cast<Arguments*>(p);
  const Reglages& rg = a.reglages;
  a.traduits = 0;
  std::error_code ec;
  std::vector<std::filesystem::path> fichiers;
  for (const auto& e : std::filesystem::directory_iterator(a.entree, ec))
    if (e.is_regular_file(ec)) fichiers.push_back(e.path());
  std::sort(fichiers.begin(), fichiers.end());
  std::vector<std::vector<uint8_t>> donnees;
  std::map<uint64_t, std::pair<size_t, size_t>> shaders;
  for (const auto& chemin : fichiers) {
    donnees.push_back(Lire(chemin));
    Conteneurs(donnees.back(), shaders, donnees.size() - 1);
  }
  OD_JOURNAL("[essai deko3d] {} fichiers, {} conteneurs dans {}", fichiers.size(), shaders.size(), a.entree.string());
  for (const char* d : {"hlsl", "prep", "spv", "vk", "glsl", "dksh"})
    std::filesystem::create_directories(a.sortie / d, ec);

  // Les plus gros d'abord : les longs travaux commencent tôt et les fils finissent ensemble.
  std::vector<Travail> travaux;
  for (const auto& [empreinte, place] : shaders) {
    const uint8_t* c = donnees[place.first].data() + place.second;
    travaux.push_back({xenosrecomp_renut::TailleConteneur(c, donnees[place.first].size() - place.second), empreinte,
                       c, {}});
  }
  std::sort(travaux.begin(), travaux.end(), [](const Travail& x, const Travail& y) { return x.taille > y.taille; });

  const bool boost = rg.boost && switch_cpu::Boost(true);
  if (rg.boost) OD_JOURNAL("[essai deko3d] mode boost du processeur : {}", boost ? "oui" : "refusé");
  const int fils = std::max(1, rg.fils);
  OD_JOURNAL("[essai deko3d] phase 1 (jusqu'au GLSL) : {} fils", fils);

  glslang::InitializeProcess();
  const auto debut1 = Horloge::now();
  std::atomic<size_t> suivant{0};
  std::vector<Arguments> args(fils);
  std::vector<pthread_t> ids(fils);
  std::vector<bool> lances(fils, false);
  for (int k = 0; k < fils; ++k) {
    args[k].travaux = &travaux;
    args[k].suivant = &suivant;
    args[k].fil = k;
    args[k].reglages = rg;
    // Sur la Switch : les cœurs 0 à 2 tour à tour (le 3 est au système).
    args[k].coeur = rg.placer ? k % 3 : -1;
    lances[k] = LancerFil(ids[k], FilGlsl, &args[k]);
    if (!lances[k]) OD_JOURNAL("[essai deko3d] fil {} non créé", k);
  }
  if (std::find(lances.begin(), lances.end(), true) == lances.end()) FilGlsl(&args[0]);
  for (int k = 0; k < fils; ++k)
    if (lances[k]) pthread_join(ids[k], nullptr);
  const double ms1 = MsDepuis(debut1);
  glslang::FinalizeProcess();
  OD_JOURNAL("[essai deko3d] phase 1 finie en {:.0f} ms", ms1);

  double ms2 = 0;
#if OD_CHAINE_UAM
  if (rg.uam) {
    if (rg.placer) switch_cpu::FixerCoeur(rg.coeur_uam);
    const auto debut2 = Horloge::now();
    uam::init(true);
    size_t n = 0;
    for (auto& w : travaux) {
      CompilerDksh(w.r);
      if (++n % 20 == 0) OD_JOURNAL("[essai deko3d] phase 2 : {}/{} ({:.0f} ms)", n, travaux.size(), MsDepuis(debut2));
    }
    uam::shutdown();
    ms2 = MsDepuis(debut2);
    OD_JOURNAL("[essai deko3d] phase 2 (uam) finie en {:.0f} ms", ms2);
  }
#endif
  if (boost) switch_cpu::Boost(false);

  // Les fichiers et le rapport, une fois les mesures faites (l'écriture sur la carte n'entre pas dans les temps).
  std::string rapport =
      "# nom ok étape | ms : xenosrecomp prep glslang légalisation spirv-cross conversion uam total | tailles : hlsl "
      "spirv glsl dksh | fil | erreur\n";
  double totaux[7] = {};
  std::map<std::string, int> echecs;
  std::vector<double> par_shader;
  for (auto& w : travaux) {
    const Resultat& r = w.r;
    const std::string nom = fmt::format("{}_{:016x}", r.shader.pixel ? "ps" : "vs", w.empreinte);
    Ecrire(a.sortie / "hlsl" / (nom + ".hlsl"), r.shader.hlsl);
    Ecrire(a.sortie / "prep" / (nom + ".hlsl"), r.hlsl_prepare);
    if (!r.spirv.empty()) Ecrire(a.sortie / "spv" / (nom + ".spv"), r.spirv.data(), r.spirv.size() * 4);
    if (!r.glsl_vulkan.empty()) Ecrire(a.sortie / "vk" / (nom + ".vk.glsl"), r.glsl_vulkan);
    if (!r.glsl.empty()) Ecrire(a.sortie / "glsl" / (nom + ".glsl"), r.glsl);
    if (!r.dksh.empty()) Ecrire(a.sortie / "dksh" / (nom + ".dksh"), r.dksh.data(), r.dksh.size());
    double total = 0;
    for (int k = 0; k < 7; ++k) {
      totaux[k] += r.ms[k];
      total += r.ms[k];
    }
    par_shader.push_back(total);
    const bool ok = r.ok && (!rg.uam || !OD_CHAINE_UAM || !r.dksh.empty());
    if (ok) {
      ++a.traduits;
    } else {
      ++echecs[r.etape];
      OD_JOURNAL("[essai deko3d] {} : échec ({}) {}", nom, r.etape, r.erreur.substr(0, 300));
    }
    std::string erreur = r.erreur.substr(0, 300);
    std::replace(erreur.begin(), erreur.end(), '\n', ' ');
    rapport += fmt::format("{} {} {} | {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} {:.1f} | {} {} {} {} | {} | {}\n",
                           nom, ok ? 1 : 0, r.etape.empty() ? "-" : r.etape, r.ms[0], r.ms[1], r.ms[2], r.ms[3],
                           r.ms[4], r.ms[5], r.ms[6], total, r.shader.hlsl.size(), r.spirv.size() * 4, r.glsl.size(),
                           r.dksh.size(), w.fil, erreur);
  }

  std::sort(par_shader.begin(), par_shader.end());
  const auto quantile = [&](double q) {
    return par_shader.empty() ? 0.0 : par_shader[std::min(par_shader.size() - 1, size_t(q * par_shader.size()))];
  };
  std::string bilan = fmt::format(
      "# {} shaders, {} traduits{} ; boost {}, {} fils{}{}\n"
      "# durée : phase 1 (jusqu'au GLSL) {:.0f} ms, phase 2 (uam) {:.0f} ms\n"
      "# par shader : médiane {:.1f} ms, 90 % {:.1f} ms, max {:.1f} ms ; tas : {:.1f} Mo à la fin des phases\n"
      "# par étape (ms, tous fils) : xenosrecomp {:.0f}, prep {:.0f}, glslang {:.0f}, légalisation {:.0f}, "
      "spirv-cross {:.0f}, conversion {:.0f}, uam {:.0f}\n",
      travaux.size(), a.traduits, rg.uam && OD_CHAINE_UAM ? " jusqu'au DKSH" : " jusqu'au GLSL",
      boost ? "oui" : "non", fils, rg.placer ? " placés sur les cœurs" : "",
      rg.allocateur_par_fil ? ", allocateur par fil (rpmalloc)" : "", ms1, ms2, quantile(0.5), quantile(0.9),
      par_shader.empty() ? 0.0 : par_shader.back(), MemoireMo(), totaux[0], totaux[1], totaux[2], totaux[3],
      totaux[4], totaux[5], totaux[6]);
  for (const auto& [etape, n] : echecs) bilan += fmt::format("# échecs à l'étape {} : {}\n", etape, n);
  Ecrire(a.sortie / "rapport.txt", bilan + rapport);
  OD_JOURNAL("[essai deko3d] {}", bilan);
  return nullptr;
}

}  // namespace

void TraduireGlslTous(const std::vector<const uint8_t*>& conteneurs, std::vector<Resultat>& resultats,
                      const Reglages& rg, const std::function<void(size_t, size_t)>& progression) {
  std::vector<Travail> travaux;
  for (const uint8_t* c : conteneurs)
    travaux.push_back({xenosrecomp_renut::TailleConteneur(c, size_t(1) << 24), 0, c, {}});
  // Les plus gros d'abord (les longs travaux commencent tôt et les fils finissent ensemble) ; l'ordre d'origine gardé.
  std::vector<size_t> ordre(travaux.size());
  for (size_t i = 0; i < ordre.size(); ++i) ordre[i] = i;
  std::stable_sort(ordre.begin(), ordre.end(), [&](size_t x, size_t y) { return travaux[x].taille > travaux[y].taille; });
  std::vector<Travail> tries;
  for (size_t i : ordre) tries.push_back(travaux[i]);
  const int fils = std::max(1, rg.fils);
  std::atomic<size_t> suivant{0}, faits{0};
  std::mutex verrou;
  std::vector<Arguments> args(fils);
  std::vector<pthread_t> ids(fils);
  std::vector<bool> lances(fils, false);
  glslang::InitializeProcess();
  for (int k = 0; k < fils; ++k) {
    args[k].travaux = &tries;
    args[k].suivant = &suivant;
    args[k].faits = &faits;
    args[k].verrou = &verrou;
    args[k].progression = &progression;
    args[k].fil = k;
    args[k].reglages = rg;
    args[k].coeur = rg.placer ? k % 3 : -1;
    lances[k] = LancerFil(ids[k], FilGlsl, &args[k]);
  }
  if (std::find(lances.begin(), lances.end(), true) == lances.end()) FilGlsl(&args[0]);
  for (int k = 0; k < fils; ++k)
    if (lances[k]) pthread_join(ids[k], nullptr);
  glslang::FinalizeProcess();
  resultats.assign(travaux.size(), {});
  for (size_t i = 0; i < ordre.size(); ++i) resultats[ordre[i]] = std::move(tries[i].r);
}

int Essai(const std::filesystem::path& entree, const std::filesystem::path& sortie, const Reglages& reglages) {
  Arguments a;
  a.entree = entree;
  a.sortie = sortie;
  a.reglages = reglages;
  pthread_t fil;
  if (!LancerFil(fil, EssaiFil, &a)) {
    OD_JOURNAL("[essai deko3d] fil non créé");
    return 0;
  }
  pthread_join(fil, nullptr);
  return a.traduits;
}

#if !OD_CHAINE_OUTIL
void EssaiAuDemarrage() {
  const std::string dossier = REXCVAR_GET(od_essai_deko3d);
  if (dossier.empty()) return;
  std::filesystem::path racine(dossier);
  if (racine.is_relative()) racine = rex::filesystem::GetExecutableFolder() / racine;
  OD_JOURNAL("[essai deko3d] début ({})", racine.string());
  Reglages rg;
  rg.uam = OD_CHAINE_UAM != 0 && REXCVAR_GET(od_essai_deko3d_uam);
  rg.boost = REXCVAR_GET(od_essai_deko3d_boost);
  rg.fils = REXCVAR_GET(od_essai_deko3d_fils);
  rg.placer = true;
  rg.coeur_uam = REXCVAR_GET(od_essai_deko3d_coeur_uam);
  rg.allocateur_par_fil = REXCVAR_GET(od_essai_deko3d_allocateur);
  Essai(racine / "conteneurs", racine / "sortie", rg);
}

void EssaiConteneurs(const uint8_t* image, size_t taille_image, const std::filesystem::path& racine_jeu) {
  const std::string dossier = REXCVAR_GET(od_essai_conteneurs);
  if (dossier.empty()) return;
  std::filesystem::path sortie(dossier);
  if (sortie.is_relative()) sortie = rex::filesystem::GetExecutableFolder() / sortie;
  OD_JOURNAL("[conteneurs] recherche dans l'image ({} octets) et les .igb de {} -> {}", taille_image,
             racine_jeu.string(), sortie.string());
  conteneurs::Reglages rg;
  rg.fils = REXCVAR_GET(od_essai_deko3d_fils);
  rg.placer = true;
  rg.allocateur_par_fil = REXCVAR_GET(od_essai_deko3d_allocateur);
  const bool boost = REXCVAR_GET(od_essai_deko3d_boost) && switch_cpu::Boost(true);
  conteneurs::Liste liste;
  size_t dernier = 0;
  const auto b = conteneurs::ChercherDansLeJeu(image, taille_image, racine_jeu, liste, rg, [&](size_t fait, size_t total) {
    if (fait * 10 / total != dernier) {
      dernier = fait * 10 / total;
      OD_JOURNAL("[conteneurs] {} / {} fichiers", fait, total);
    }
  });
  if (boost) switch_cpu::Boost(false);
  std::error_code ec;
  std::filesystem::create_directories(sortie, ec);
  std::ofstream csv(sortie / "fichiers.csv");
  csv << "shader,fichier\n";
  size_t vs = 0;
  for (const auto& [empreinte, c] : liste) {
    const std::string nom = conteneurs::Nom(empreinte, c);
    csv << nom << ',' << c.fichier << '\n';
    std::ofstream(sortie / (nom + ".bin"), std::ios::binary)
        .write(reinterpret_cast<const char*>(c.donnees.data()), std::streamsize(c.donnees.size()));
    vs += c.vertex;
  }
  OD_JOURNAL("[conteneurs] {} fils{} : {} .igb lus ({} compressés, {} échecs, {:.1f} Mo) ; {} shaders ({} vs) ; "
             "total {:.0f} ms ; tous fils : lecture {:.0f} ms, LZX {:.0f} ms, recherche {:.0f} ms",
             rg.fils, boost ? ", boost" : "", b.fichiers, b.compresses, b.echecs, b.octets / 1e6, liste.size(), vs,
             b.ms_total, b.ms_lecture, b.ms_lzx, b.ms_recherche);
}
#endif

}  // namespace opdarkness::chaine_deko3d
