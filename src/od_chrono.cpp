// opdarkness - chronomètre des statistiques du moteur coupé (sub_82576F40, 2026-11-02).
//
// sub_82576F40 écrit l'heure de l'horloge du moteur dans *r3. Elle est appelée 51 fois dans 28 fonctions du rendu
// (~2 400 appels par image en moyenne, jusqu'au fil de dessin : ~5,5 % de ce fil avec l'horloge en dessous, profil
// console du 2026-11-02). Partout, deux relevés encadrent un travail et leur différence s'additionne à un compteur
// global (829B20B8, 829B5710...), enregistré par adresse (825F9EE4, 82654614, 8267EBC8...) dans la liste des
// statistiques du moteur. Aucune lecture de ces compteurs dans le code hors de leurs accumulations : ils ne pilotent
// pas le jeu. Avec od_chrono_moteur = false (défaut), les relevés valent 0 et l'horloge n'est plus lue.
// L'horloge du jeu elle-même (animations, cadence) passe par d'autres appels : elle n'est pas touchée.

#include "opdarkness_pch.h"

#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>

REXCVAR_DEFINE_BOOL(od_chrono_moteur, false, "OpDarkness",
                    "Garder les relevés d'heure des statistiques du moteur (sub_82576F40) ; false = relevés à 0");

REX_EXTERN(__imp__sub_82576F40);
REX_HOOK_RAW(sub_82576F40) {
  if (REXCVAR_GET(od_chrono_moteur)) {
    __imp__sub_82576F40(ctx, base);
    return;
  }
  std::memset(base + ctx.r3.u32, 0, 8);
  ctx.r3.u64 = 0;
}
