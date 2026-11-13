// opdarkness - clavier virtuel de la Switch pour XamShowKeyboardUI (nom du joueur, demande de l'utilisateur du
// 2026-10-16).
//
// Le SDK affiche une fenêtre ImGui pour ce clavier, qui ne sert pas sur la console. Il accepte un clavier fourni par
// l'application (rex::kernel::xam::SetKeyboardProvider, ajouté par patches/sa2-nx-sdk.patch) et l'appelle hors du fil
// du jeu (file des fenêtres du système, résultat rendu au jeu par la structure OVERLAPPED). Ici : l'applet clavier de
// la Switch (swkbd de libnx). Ce fichier n'inclut que switch.h et des en-têtes standard : switch.h entre en conflit
// avec les types du SDK.

#include "od_clavier.h"

#include <cstdint>
#include <string>
#include <vector>

#include <rex/kernel/xam/keyboard.h>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace opdarkness::clavier {

#if defined(__SWITCH__)
namespace {

bool Afficher(const std::string& titre, const std::string& description, const std::string& defaut,
              uint32_t max_caracteres, std::string& texte, bool& annule) {
  SwkbdConfig config;
  if (R_FAILED(swkbdCreate(&config, 0))) return false;
  swkbdConfigMakePresetDefault(&config);
  if (!titre.empty()) swkbdConfigSetHeaderText(&config, titre.c_str());
  if (!description.empty()) swkbdConfigSetSubText(&config, description.c_str());
  swkbdConfigSetGuideText(&config, titre.empty() ? "Name" : titre.c_str());
  if (!defaut.empty()) swkbdConfigSetInitialText(&config, defaut.c_str());
  if (max_caracteres) swkbdConfigSetStringLenMax(&config, max_caracteres);
  std::vector<char> sortie(max_caracteres * 4 + 16, 0);  // UTF-8 : jusqu'à 4 octets par caractère
  const Result resultat = swkbdShow(&config, sortie.data(), sortie.size());
  swkbdClose(&config);
  if (R_FAILED(resultat)) {
    // L'applet rend une erreur quand le joueur annule (bouton B ou Annuler).
    annule = true;
    texte.clear();
    return true;
  }
  annule = false;
  texte = sortie.data();
  return true;
}

}  // namespace

void Installer() { rex::kernel::xam::SetKeyboardProvider(Afficher); }
#else
void Installer() {}  // PC : la fenêtre ImGui du SDK
#endif

}  // namespace opdarkness::clavier
