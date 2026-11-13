# Traduction des shaders au fil du jeu (étude du 2026-10-09)

Demande de l'utilisateur : le jeu est trop long pour tout traduire à l'avance (paquet sa2_shaders.mesp rempli
session après session) ; les shaders doivent être traduits pendant la partie, sur PC et si possible sur la Switch.

## Aujourd'hui
- Le rendu natif de sa2-nx ne traduit rien en jeu : il cherche chaque shader chargé par le jeu (IM_LOAD de l'anneau)
  dans le paquet ; s'il n'y est pas, les dessins qui l'utilisent sont sautés (objets invisibles) et le shader est
  vidé (--sa2_shader_misses) pour le paquet suivant (tools/paquet_shaders.sh).
- Chaîne hors ligne (sa2-nx tools/build_shader_library.sh), par conteneur :
  1. XenosRecomp (sa2-nx shaders/XenosRecomp/shader_recompiler.cpp, ~3 000 lignes de C++ sans dépendance lourde,
     front masseffect_hlsl.cpp) : conteneur -> HLSL, avec le prélude shader_common.h (540 lignes) ;
  2. DXC (-spirv -T vs_6_6/ps_6_6 -HV 2021 -fvk-use-dx-layout, -fvk-invert-y pour les VS) : HLSL -> SPIR-V ;
  3. me_pack_shaders : entrées du paquet (conteneur + SPIR-V).
  Ajouts de sa2-nx pour les shaders sans conteneur : enveloppe des shaders propres à Direct3D (wrap_discovered.py,
  ensemble fini, peut rester préparé à l'avance) et réparation des variantes de vertex shaders
  (repair_vertex_variant_declarations, C++ autonome).
- Côté rendu : une entrée (ShaderEntry, src/me/native/sa2_native_shaders.h) se construit à partir du conteneur et du
  SPIR-V seulement (éléments de sommets, échantillonneurs, sorties, kills, taille des constantes). La bibliothèque
  est chargée une fois au démarrage et lue sans verrou par le fil de l'anneau.

## Ce qui est faisable
### PC (Linux) : direct
- XenosRecomp compilé dans le jeu (bibliothèque statique, sources de sa2-nx).
- DXC en bibliothèque : libdxcompiler.so (~/workspace/_tools/dxc-1.8.2505.1/lib, release Microsoft), chargé par
  dlopen, API IDxcCompiler3 : mêmes options que la chaîne hors ligne, donc SPIR-V identique au paquet.
- Quand traduire : dès la création du shader par le jeu (interceptions de CreateVertexShader/CreatePixelShader,
  src/od_me_hooks.cpp : le conteneur complet y est), sur un fil de fond, bien avant le premier dessin dans la plupart
  des cas ; en secours, au premier IM_LOAD inconnu (conteneur reconstruit comme le fait déjà SA2_SHADER_MISSING).
- Rendu : table « supplément » consultée quand le paquet ne connaît pas un shader (ajouts seulement, publiés
  atomiquement pour le fil de l'anneau) ; les dessins sont sautés le temps de la traduction (quelques dizaines de ms)
  puis dessinés.
- Cache persistant : chaque traduction ajoutée à un fichier à côté du paquet (même format d'entrée), relu au
  démarrage : une zone n'est traduite qu'une fois par machine. Les pipelines Vulkan suivent déjà (cache de
  pipelines du rendu).

### Switch : à décider
DXC n'existe pas pour la Switch. Deux voies :
1. **glslang** (déjà dans le SDK, et utilisé sur la console par torchlight-nx pour son HLSL). Essai du 2026-10-09
   (glslangValidator avec HLSL, sur les 109 HLSL du paquet) : 0 sur 109 ne compile tel quel. Incompatibilités
   rencontrées dans l'ordre (chacune bloquait les 109) :
   - mots réservés de glslang utilisés comme noms : `dword`, `texture` (renommage mécanique) ;
   - `select()` de HLSL 2021 (macro vers l'opérateur ?: composante par composante) ;
   - littéraux et calculs 64 bits (`1ull`, `uint64_t`) : « unsupported construction » ;
   - plus loin dans shader_common.h : `vk::RawBufferLoad` (lecture des sommets par adresse GPU 64 bits) et tableaux
     de descripteurs sans taille : à réécrire pour glslang (tampons de stockage), donc un second prélude
     shader_common.h à maintenir et à valider image par image contre la version DXC.
   Travail réel mais borné (le prélude est à nous ; le code de chaque shader généré par XenosRecomp change peu).
2. **DXC compilé pour la Switch** : sa2-nx l'a compilé en WebAssembly pour son installateur (shaders/wasm), donc
   le code est portable ; mais c'est LLVM entier (dizaines de Mo de code dans le .nro, mémoire), sans dlopen sur la
   console (lien statique).
Dans les deux cas, la traduction d'un shader prendra plus longtemps sur l'A57 (à mesurer) : d'où la traduction dès
la création du shader, sur un cœur libre, et le cache persistant sur la carte SD.

## Proposition d'ordre de travail
1. PC : XenosRecomp + DXC (dlopen) dans le jeu, table supplément dans le rendu, cache persistant. Résultat : plus
   d'objets manquants sur PC, plus besoin de refaire le paquet à la main.
2. Switch : prélude glslang (voie 1), validé sur PC en comparant le SPIR-V glslang au SPIR-V DXC par des captures à
   image exacte (tools/captures_images.sh). Voie 2 seulement si la voie 1 bute.
Le paquet préparé à l'avance reste utile au démarrage (shaders déjà connus, ceux de Direct3D et les variantes).

## Réalisé sur PC (2026-10-09)
- src/od_traducteur.cpp : XenosRecomp (bibliothèque od_xenosrecomp, sources de sa2-nx, cmake/rendu_natif.cmake) +
  DXC par dlopen (--od_dxc, défaut ~/workspace/_tools/dxc-1.8.2505.1/lib/libdxcompiler.so), appelé par les
  interceptions de CreateVertexShader / CreatePixelShader (src/od_me_hooks.cpp) avant la création de l'objet, de
  façon synchrone ; --od_traduire_shaders=false pour couper. Cache : <exécutable>/cache_shaders/*.odsh
  (--od_cache_shaders), relu au premier appel.
- Rendu : ShadersNative::Add et me::native::AddShader (écarts listés dans docs/RENDU_NATIF.md).
- Mesures : sans paquet, 44 shaders traduits jusqu'à l'écran titre, ~64 ms chacun, 0 échec ; SPIR-V identique
  octet pour octet à celui du paquet (même DXC, mêmes options).
- Variantes en jeu aussi (2026-10-09, src/od_variantes.cpp, fil de fond) : le rendu confie chaque variante de
  vertex shader réécrite par Direct3D (SetVariantHandler) ; réparation par src/od_reparation_variantes.cpp
  (généré par tools/import_reparation.py depuis l'outil de sa2-nx, mêmes preuves), puis traduction. Même file pour
  les chargements immédiats de shaders de Direct3D réécrits (SetImmediateHandler) : le rendu reconstruit le
  conteneur depuis le shader interne le plus proche, traduit tel quel ; s'il n'a pas encore de modèle (le jeu crée
  le modèle juste après), nouvel essai dès que la bibliothèque grandit.
- Essai avec un paquet réduit aux 17 shaders de Direct3D (out/paquet-minimal) et un cache vide, jusqu'au combat :
  80 shaders traduits, 6 variantes réparées, 2 chargements immédiats ; 2 305 dessins sautés en tout, tous dans les
  premières secondes (le temps des traductions), puis aucun ; combat à 60 images/s. Les 2 variantes refusées par la
  preuve n'empêchent aucun dessin (le bon shader est reconnu autrement).
- Plus rien à préparer (2026-10-09) : un vertex shader resté inconnu 2 s (ni shader du jeu, ni variante réparée, ni
  chargement immédiat avec modèle) est enveloppé par registres en jeu (WrapUnknownVertexShader dans
  me_native_system.cpp, comme wrap_raw_shader.py de sa2-nx), puis traduit. Le cache est relu dès le démarrage du rendu
  (SetLibraryReadyHandler), avant les premiers dessins de Direct3D.
- Essai sans aucun paquet, cache vide, jusqu'au combat : 1er lancement 84 shaders traduits, 4 enveloppés, 2 951
  dessins sautés (par rafales, le temps des traductions) ; 2e lancement : 0 traduction, 0 dessin sauté.

## Switch : glslang (2026-10-09, voie 1 réalisée)
Demande de l'utilisateur : la Switch traduit elle-même ses shaders, comme torchlight-nx.
- Prélude shaders/shader_common_glslang.h (copie de celui de sa2-nx) : constantes seulement par tampons uniformes
  (MASSEFFECT_UBO fixé à 1 ; le rendu met toujours le bit, sa2_native_constants_ubo), plus de constantes poussées
  en 64 bits.
- Préparation du texte (tools/prep_glslang.py, porté à l'identique en C++ : src/od_prep_glslang.cpp, vérifié octet
  pour octet sur les 84 shaders du cache par od_hlsl --glslang) : `__spirv__` défini (DXC le définit), lectures
  vk::RawBufferLoad<T>(...) -> ((T)0) (branche « pointeur » jamais prise), mots réservés `dword` / `texture` renommés,
  `select()` -> `?:`, `(1ull << n)` -> `(n < 32 ? (1u << n) : 0u)`.
- Compilation (src/od_traducteur.cpp, CompilerGlslang) : glslang du SDK avec l'entrée HLSL (option
  REXGLUE_GLSLANG_HLSL ajoutée au SDK, patches/sa2-nx-sdk.patch), Vulkan 1.2 / SPIR-V 1.5, y inversé pour les VS,
  sans l'optimiseur SPIR-V (le SDK ne compile pas SPIRV-Tools-opt). --od_compilateur=dxc|glslang sur PC ; glslang
  seul sur la Switch. Verrou global de glslang initialisé une fois (patches/glslang-verrou.patch, de torchlight).
- Banc d'essai : tools/glslang_essai.sh (84/84 compilent) ; tools/spirv_interface.py compare l'interface des deux
  SPIR-V : glslang déclare en plus les entrées et ressources inutilisées (DXC les élimine), tolérées par les pilotes.
- Validation sur PC (paquet minimal, caches vides, scénario rapide) : 80 shaders traduits par chaque compilateur,
  0 échec ; sur l'écran fixe du tutoriel, image identique à celle de DXC sauf la barre de boutons qui clignote.
- Cache sur la Switch : <cache>.odsc, un seul fichier ; les traductions faites sur la console y sont ajoutées à la
  fin (pas de liste de dossier, peu fiable sur la console).
- Variantes : fil de fond créé par pthread avec une pile de 4 Mo (std::thread lève une exception sur la console).

### Champ de bataille noir sans paquet (2026-10-09, trouvé sur la Switch, reproduit sur PC)
- Symptôme : sans paquet, tour d'Edward et cinématiques du combat sur fond noir (menus et portraits dessinés).
  Pas propre à la Switch : rejeu à image exacte sur PC (tools/captures_images.sh + out/entrees/lockstep-test.txt,
  images 12200-12600) avec paquet = champ de bataille, sans paquet + glslang = noir, sans paquet + DXC = bon.
- Bissection des 84 shaders du cache (cache DXC, sous-ensembles en version glslang) : un seul vertex shader,
  vs_2b35340fa7c1052a (quad dessiné en liste de rectangles, qui affiche la scène 3D).
- Causes : (1) SPIR-V de glslang non optimisé (le SDK de sa2-nx compilait glslang sans SPIRV-Tools) : entrée
  SV_VertexID et code mort gardés, refusés par la conversion des rectangles (src/me/native/me_rectangle_spirv.h,
  « rectangle VS interface unsupported ») ; corrigé par l'optimiseur (REXGLUE_GLSLANG_HLSL ajoute SPIRV-Tools,
  options.disableOptimizer = false, comme DXC et torchlight-nx). (2) La conversion insérait ses déclarations avant
  la première variable globale ; chez glslang le type entier vient après des variables : SPIR-V refusé par le
  pilote (« expected 'type' but got 'invalid' »), pipeline non créé. Corrigé : types de pointeur privé avant la
  première variable, le reste avant la première fonction (docs/RENDU_NATIF.md). Vérifié hors du jeu : conversion
  + validation SPIRV-Tools de tous les vertex shaders des caches (aucun invalide ; aucun shader accepté avec DXC et
  refusé avec glslang).
- Leçon : comparer avec et sans paquet à image exacte (rejeu en lockstep), jamais à des instants du scénario
  rapide (les vidéos décalent tout).

### Personnages absents sans paquet (2026-10-09, signalé par l'utilisateur : Edward absent à son tour)
- Avec les 109 entrées du paquet passées par le cache (ajout en jeu) : Edward absent. Deux différences de l'ajout en
  jeu corrigées dans le rendu (docs/RENDU_NATIF.md) : ordre des candidats et choix de l'index par code identiques au
  chargement du paquet. Puis bissection des 30 entrées propres au paquet : une seule nécessaire,
  wrapped/vs_4a9a0a4ef2f4b60f (microcode de la variante du vertex shader d'Edward, modèle C08A1416, enveloppé par
  registres par wrap_discovered.py).
- En jeu, cette variante est réparée (od_reparation_variantes) mais la version réparée ne dessine rien ; le paquet
  avait l'enveloppe de chacune de ces variantes (4a9a, d7bc, ac9c, 91c3, 297e, b904...) et l'index par code préfère
  l'entrée liée par registres. Corrigé : src/od_variantes.cpp enveloppe aussi chaque variante (me::native::
  WrapVertexMicrocode, avec les en-têtes de la variante elle-même : même conteneur d'un lancement à l'autre, retrouvé
  dans le cache). Vérifié : sans paquet, glslang, cache vide puis plein : Edward et les soldats dessinés, 0
  retraduction au second passage.
- Les caches faits avec le scénario rapide n'avaient pas ces enveloppes : en refaire depuis zéro pour comparer.

