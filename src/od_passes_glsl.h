// opdarkness - passes du GLSL final pour uam, reprises de tools/renut/deko3d_glsl_opt.py (banjo-nx, repris par rr6-nx)
// pour la traduction par la console (2026-11-10, docs/DEKO3D.md de la branche, « Passes GLSL de banjo-nx sur la
// console ») : « vsout », « pred », « sel » et « ifmerge », sur le texte que rend la chaîne (od_chaine_deko3d.cpp, après
// les passes « alpha » et « earlyz »). Chacune rend le même texte que la passe Python du même nom (vérifié octet pour
// octet sur les 176 shaders par src/outils/od_passes_glsl.cpp) ; écrites sans std::regex (lent, et récursif : la pile
// des fils de la console).
//  - vsout (vertex shaders) : chaque écriture d'une sortie (layout location N) sous `(g_banjoVsOutputs & (1u << N))`,
//    constante 905 que plume donne d'après ce que lit le pixel shader (banjo_deko3d_vs_outputs) ;
//  - pred : dans `if (X) { ... }` (X un bool que le bloc n'écrit pas), X vaut vrai (et faux dans le else) ;
//  - sel : chaque `c ? a : b` devient dk_sel(c, a, b) (un mix() : SEL au lieu de branches dans uam) ;
//  - ifmerge : deux if de suite sur la même variable non modifiée n'en font qu'un.
#pragma once

#include <string>

namespace opdarkness::passes_glsl {

std::string VsOut(const std::string& texte);
std::string Pred(const std::string& texte);
std::string Sel(const std::string& texte);
std::string IfMerge(const std::string& texte);

// Les passes du stage, dans l'ordre de deko3d_glsl_opt.py ; rend le nombre de passes qui ont changé le texte.
int Appliquer(std::string& glsl, bool vertex);

}  // namespace opdarkness::passes_glsl
