# Direct3D d'Operation Darkness (fonctions identifiées)

Pour le rendu natif (voie A, docs/PLAN.md étape 3) : les interceptions dont le rendu de sa2-nx a besoin
(~/workspace/sa2-nx/app/src/me/sa2_me_hooks.cpp). Outil : `tools/fonctions.py` (index des fonctions générées :
`index`, puis `info`, `appelants`, `cherche`). Pas de jeu de référence aux fonctions nommées sous la main (le
d3d_match.py de sa2-nx compare avec Banjo) : identification par les signatures de chaque fonction.

## Version du XDK
Fin 2007 / début 2008 (XEX du 2008-04-26) : disposition du device de **Banjo (XDK 2008)**, confirmée par les
décalages des shaders liés ci-dessous (sa2-nx docs/sa2-notes.md : 2008 = +12696 / +12692, 2012 = +13072 / +13068).
Code de Direct3D : vers 0x82330000-0x823A0000, plus 6 fonctions de dessin vers 0x826BD808-0x826BEF88.

## Fonctions (2026-10-07)
| Fonction | Adresse | Preuve |
|---|---|---|
| `D3DDevice_Swap` | `sub_8234A5C8` | seul appelant de l'import `VdSwap` |
| chargement des shaders au dessin (part de FlushState) | `sub_8234CC58` | lit le VS lié à `device+12696` et le PS à `+12692`, puis `+896`, `+872`, `+904` de l'objet VS (mêmes champs que dans SA2) ; 3e appel de chaque fonction de dessin |
| vidage des constantes de shader (16 registres par bit) | `sub_8234BEC0` | 2 appels en tête de chaque dessin, r5 = 0x4000 (VS) / 0x4400 (PS), r6 = device+1920 / +6016 |
| vidage des groupes de registres (1 registre par bit) | `sub_8234BB20` | jusqu'à 9 appels par dessin, comme `sub_82555930` de SA2 |
| réservation dans le tampon de commandes | `sub_82345D60` | appelée quand `device+48 > device+56` (pointeur d'écriture / limite) |
| fonctions de dessin (écrivent DRAW_INDX, `li r5,8704` = 0x2200) | `sub_826BD808`, `sub_826BDD00`, `sub_826BE2D0`, `sub_826BE6B8`, `sub_826BEB10`, `sub_826BEF88` | même suite d'appels que DrawVertices de SA2 ; à nommer (DrawVertices, DrawIndexedVertices, variantes UP : `sub_826BD808` prend r5 = nombre, r6 = pas en octets) |
| `D3DDevice_CreateVertexShader` | `sub_823415A8` | r3 = conteneur ([1] taille de l'en-tête, [2] du microcode) ; objet de 872 octets + copie du conteneur à `+872` (le champ lu par le chargement des shaders) ; appelée par paires avec la suivante (`823C1B50`, `82774740`...) |
| `D3DDevice_CreatePixelShader` | `sub_82341498` | même forme, tête de 40 octets |
| autres écrivains de DRAW_INDX dans Direct3D | `sub_823554C8` (appelé par Swap via `sub_82356190`), `sub_823569F0` (DRAW_INDX_2 aussi ; 13 appels : Clear ou Resolve ?), `sub_823578C8`, `sub_823583A8`, `sub_8233F1A8`/`sub_8233F4F0` (initialisation du device) | à nommer |

## À trouver
- `D3DDevice_Clear`, `Resolve` (couches « directes » de sa2-nx, facultatives au début).
