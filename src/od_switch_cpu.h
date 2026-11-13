// opdarkness - processeur de la Switch pour les gros travaux sans affichage (traduction des shaders) : mode boost
// (appletSetCpuBoostMode : processeur à 1 785 MHz, GPU au plus bas ; seulement quand rien ne s'affiche) et fil placé
// sur un cœur (un fil de libnx reste sinon sur le cœur par défaut du processus). Sans effet hors de la Switch.
// Un fichier à part : <switch.h> remplit l'espace global (Thread, Mutex, Event...).
#pragma once

namespace opdarkness::switch_cpu {

// true : boost (ApmCpuBoostMode_FastLoad) ; false : vitesse normale. Rend false si le système refuse.
bool Boost(bool actif);

// Le fil appelant sur le seul cœur `coeur` (0 à 2, ceux de l'application). Rend false si le système refuse.
bool FixerCoeur(int coeur);

// Mode quatre cœurs (2026-11-01, SDK : threading_switch.cpp, repris de renut-nx) : le cœur gardé aux fils de fond (3)
// quand le processus a les quatre cœurs, sinon -1 ; le nombre de cœurs des fils du jeu (3 ; 3 aussi en mode quatre
// cœurs) ; le fil appelant seul sur le cœur gardé, à la priorité `priorite` si >= 0 (false sans ce cœur).
int CoeurSysteme();
bool VersCoeurSysteme(int priorite);
int PrioriteMixage();

}  // namespace opdarkness::switch_cpu
