// opdarkness - collecte du profil du .nro instrumenté pour le PGO (OD_PGO=generate), voir od_pgo.cpp.
// Dans toute autre compilation, ces fonctions ne font rien.

#pragma once

namespace opdarkness::pgo {

// Choisit le fichier de la session (pgo/od_pgo_<N>.stream à côté de l'exécutable) et lance les écritures toutes les
// 2 minutes.
void Start();
// Écrit tous les compteurs jusqu'ici dans le fichier de la session (toute la session à chaque fois).
void DumpNow();
// Dernière écriture, puis fin du fil d'écriture.
void Stop();

}  // namespace opdarkness::pgo
