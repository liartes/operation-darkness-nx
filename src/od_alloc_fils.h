// opdarkness - allocateur à cache par fil pour les fils de traduction des shaders, sur la Switch.
//
// L'allocateur de newlib n'a qu'un verrou pour tout le processus : sur la console, trois fils de traduction
// (glslang, légalisation de SPIRV-Tools, SPIRV-Cross : beaucoup de petites allocations C++) y passaient l'essentiel de
// leur temps (profil de l'essai du 2026-10-23 : _free_r, _malloc_r, mutexLock ; 125 % de charge pour 3 fils).
// Un fil qui appelle Activer() prend ses `new` dans rpmalloc (~/workspace/_tools/rpmalloc-1.4.5, domaine public), dans
// une zone de 256 Mo réservée une fois à newlib ; les autres fils gardent newlib. `delete` choisit d'après l'adresse,
// depuis n'importe quel fil (rpmalloc accepte les libérations venues d'un autre fil). Zone pleine : repli sur newlib.
// Hors de la Switch, sans effet (glibc a déjà des arènes par fil).
#pragma once

namespace opdarkness::alloc_fils {

// Le fil appelant prend ses allocations C++ dans rpmalloc jusqu'à Desactiver(). Rend false si la zone n'a pas pu
// être réservée (le fil garde newlib).
bool Activer();
void Desactiver();

}  // namespace opdarkness::alloc_fils
