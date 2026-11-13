// opdarkness - chaîne de traduction des shaders du jeu pour deko3d, sans DXC, telle que la console peut la faire
// (docs/DEKO3D.md, « Traduction des shaders par la console ») :
//   conteneur Xenos -> XenosRecomp de reNut (HLSL) -> préparation pour glslang (od_prep_glslang) -> glslang (HLSL ->
//   SPIR-V) -> passes de légalisation de SPIRV-Tools (spirv-opt --legalize-hlsl) -> SPIRV-Cross (GLSL Vulkan) ->
//   conversion pour uam (od_deko3d_glsl) -> uam (DKSH, facultatif).
// Sans les passes de banjo-nx (deko3d_glsl_opt.py) ni les retouches du HLSL (early alpha, flou, ombres) du paquet.
//
// Essai (2026-10-23) : --od_essai_deko3d=<dossier> traduit au démarrage tous les conteneurs des fichiers de
// <dossier>/conteneurs et écrit dans <dossier>/sortie le résultat de chaque étape et les temps (rapport.txt), puis le
// jeu démarre normalement ; sous boost et sur plusieurs cœurs (od_essai_deko3d_boost, _fils, _coeur_uam, _uam).
// L'outil PC od_chaine_deko3d (src/outils) fait la même chose, pour comparer.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "od_retouches.h"
#include "od_xenosrecomp_renut.h"

namespace opdarkness::chaine_deko3d {

struct Resultat {
  bool ok = false;
  std::string etape;   // celle qui a échoué (vide si ok)
  std::string erreur;
  xenosrecomp_renut::Shader shader;  // shader.hlsl : après les retouches (FixerRetouches)
  int retouches = 0;
  std::string hlsl_prepare;
  std::vector<uint32_t> spirv;  // après légalisation
  std::string glsl_vulkan;      // SPIRV-Cross
  std::string glsl;             // pour uam
  std::vector<uint8_t> dksh;
  // Durée de chaque étape en ms : XenosRecomp, préparation, glslang, légalisation, SPIRV-Cross, conversion, uam.
  double ms[7] = {};
};

// Un appel par shader ; glslang doit être initialisé (glslang::InitializeProcess). TraduireGlsl s'arrête au GLSL pour
// uam et peut tourner sur plusieurs fils à la fois ; CompilerDksh (uam) ne tourne que sur un fil, celui qui a fait
// uam::init(), à grande pile (8 Mo ; uam_api.h). Traduire fait les deux.
void TraduireGlsl(const uint8_t* conteneur, Resultat& r);
// Version du texte GLSL rendu (3 : passes « alpha », « earlyz », puis od_passes_glsl, od_chaine_passes ; 0 : sans) : l'installation refait
// un paquet d'une autre version.
uint32_t VersionTexte();
// Les retouches du paquet du PC (od_retouches.h) faites par TraduireGlsl juste après XenosRecomp ; nullptr (par
// défaut) : aucune. À fixer avant toute traduction (lu par tous les fils sans verrou).
void FixerRetouches(const retouches::Reglages* reglages);

// glslang::InitializeProcess / FinalizeProcess (comptés par glslang), pour un fil qui traduit longtemps.
void InitialiserGlslang();
void LibererGlslang();
void CompilerDksh(Resultat& r);
void Traduire(const uint8_t* conteneur, bool avec_uam, Resultat& r);

struct Reglages;

// Jusqu'au GLSL, tous les conteneurs (gros-boutistes, tels que dans les fichiers) sur `reglages.fils` fils (placés et
// avec l'allocateur par fil selon les réglages) ; `resultats[i]` pour `conteneurs[i]`. glslang initialisé et libéré
// ici. `progression(faits, total)` après chaque shader, depuis les fils, un appel à la fois.
void TraduireGlslTous(const std::vector<const uint8_t*>& conteneurs, std::vector<Resultat>& resultats,
                      const Reglages& reglages, const std::function<void(size_t, size_t)>& progression = {});

struct Reglages {
  bool uam = true;     // phase 2 : DKSH par uam
  bool boost = false;  // Switch : mode boost du processeur pendant l'essai
  int fils = 1;        // phase 1 : fils jusqu'au GLSL
  bool placer = false; // Switch : chaque fil sur son cœur (0, 1, 2 tour à tour ; uam sur coeur_uam)
  int coeur_uam = 2;
  bool allocateur_par_fil = false;  // Switch : rpmalloc dans les fils de la phase 1 (od_alloc_fils.h)
};

// Les conteneurs de tous les fichiers de `entree` (même recherche que XenosRecomp, sans doublons) : 1. traduits
// jusqu'au GLSL sur `fils` fils, 2. uam sur un fil ; les fichiers de chaque étape et rapport.txt (durée de chaque
// phase) dans `sortie`. glslang et uam initialisés et libérés ici. Rend le nombre de shaders menés au bout.
int Essai(const std::filesystem::path& entree, const std::filesystem::path& sortie, const Reglages& reglages);

// Au démarrage du jeu (OnPreSetup) : l'essai si --od_essai_deko3d est donné.
void EssaiAuDemarrage();

// Après le chargement de default.xex (OnPostLoadXexImage) : la recherche des conteneurs (od_conteneurs.h) si
// --od_essai_conteneurs est donné.
void EssaiConteneurs(const uint8_t* image, size_t taille_image, const std::filesystem::path& racine_jeu);

}  // namespace opdarkness::chaine_deko3d
