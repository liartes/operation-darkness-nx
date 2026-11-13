# Plan d'action du portage

Deux références :
- **torchlight-nx** (~/workspace/torchlight, notes complètes dans son CLAUDE.md) : même SDK ReXGlue 0.10.0 sur
  Linux, même méthode de manifeste, rendu natif **au niveau du moteur** (interceptions du RenderSystem D3D9 d'Ogre,
  shaders HLSL interceptés à la compilation et compilés par glslang), build Switch sur le SDK de nfsmw-nx.
- **Guide de portage de sa2-nx** (~/workspace/sa2-nx/docs/porting-guide.md, GPL-3.0, cloné le 2026-10-07) : méthode
  générique issue de nfsmw-nx / masseffect-nx, rendu natif **au niveau du GPU** (lecture des paquets PM4 de l'anneau,
  shaders Xenos traduits à l'avance par XenosRecomp -> HLSL -> DXC -> SPIR-V), puis remplacement progressif de
  Direct3D, optimisations CPU vérifiées au bit près, PGO. Outils Python dans ~/workspace/sa2-nx/tools (souvent
  câblés pour SA2 : chemins et plages à adapter).

Règles du guide reprises ici (identiques à celles de torchlight) : PC d'abord ; aucune donnée du jeu dans le dépôt ;
une modification à la fois, chacune derrière son option ; chaque optimisation prouvée (comparaison avec l'original) ;
les mesures qui comptent sont celles de la console.

## Étape 1 — Code complet (guide §3) — fait (2026-10-07)
- Codegen sans erreur, 30 077 fonctions (générateur du SDK de sa2-nx depuis le 2026-10-07, même résultat).
- Fonctions pointées (données + lis/addi) : tools/candidats.py, 118 entrées.
- Tables de saut (switch_check.py de sa2-nx) : 647 switch, 0 tronqué / faux / manqué. Revue à la main des 6
  « sans borne » : bornes présentes mais hors de portée de l'outil (après un bl, ou sur un chemin en amont), tailles
  générées confirmées par le contenu des tables. Les 4 « formes atypiques » sont des appels par tables de pointeurs
  de fonctions (cibles toutes enregistrées, ou tables remplies à l'exécution).
- Trous (tools/trous.py, adresses recalées sur les labels : le find_gaps.py de sa2-nx compte les tables de saut
  placées dans le code comme des trous, 626 faux trous) : 36 blr isolés et 1 fonction d'appel ajoutés au manifeste ;
  le reste = enregistrements SEH, table des imports, fonctions de sauvegarde VMX.
- setjmp/longjmp : la recherche du 2026-10-07 (stockages de r14-r31 d'abord) les avait manqués ; trouvés le
  2026-10-09 par read_before_write.py : setjmp 0x826A6360, longjmp 0x826A5E10, déclarés au générateur
  (setjmp_address / longjmp_address en tête du manifeste). Une restauration de contexte CPU (sub_826BC61C, structure
  CONTEXT, appelée par sub_826B80A8) sert au déroulement des exceptions (SEH/C++).
- Fonctions sans retour, read_before_write.py : seulement utiles avec les options du générateur (§9.1), plus tard.

## Étape 2 — Version PC jouable (guide §4) — en cours
Fait : démarrage, logos (WMV), écran titre, menus XUI, nouvelle partie, saisie du nom, introduction, chargement,
mission 1 en 3D (combat au tour par tour), avec le GPU émulé (xenos) du SDK sur Intel UHD 620.
À faire :
- [x] Mesure des images/s : src/od_images.cpp (D3DDevice_Swap, ligne « [images] » toutes les 10 s).
- [x] ~8 images/s sur PC avec les deux rendus (2026-10-07) : le moteur (threads « vms ») se passe la main par
      « TryEnterCriticalSection sinon Sleep(0) », et le SDK dort 100 µs à chaque Sleep(0) des threads de basse
      priorité. src/od_sommeil.cpp (--od_sleep0_cede ; version de l'époque : N cessions puis sommeil) : titre à 30 images/s
      (GPU émulé) / 27-28 (rendu natif), tutoriel de combat ~20 avec le rendu natif (1-2 avant). Trouvé par
      échantillonnage des piles sous gdb (tools/piles.sh, tools/piles_resume.py).
- [x] Fil de l'anneau ~180 µs par dessin : QueryProtect du SDK sous Linux (relit /proc/self/maps), appelé par
      l'estimateur d'étendue du rendu (docs/RENDU_NATIF.md, écarts) : 18 µs par dessin, 2,1 ms par image.
- [x] Tutoriel de combat ~25 images/s : Sleep(0) de courtoisie du moteur (après chaque RtlLeaveCriticalSection des
      comptes de références) qui dormaient ; politique par site d'appel (src/od_sommeil.cpp) : **60 images/s en
      combat, 1,8 cœur** (tools/mesure_combat.sh). Écran titre plafonné à 30 par le jeu.
- [x] 60 images/s en combat = cadence voulue par le jeu : identique avec la synchronisation verticale fixe à 60 Hz
      (--sa2_vsync_relaxed=false) ; le jeu présente à chaque vblank en combat, tous les 2 sur l'écran titre. Avec le
      GPU émulé : 27 images/s (limite de l'émulation). Une vidéo de la console reste utile pour la comparaison
      visuelle.
- [ ] Jouer une mission entière (manette de script ou à la main), cinématiques MOVIE_SCN, sauvegarde et chargement
      (XAM du SDK), options du son.
- [ ] Comparer avec des vidéos de la console (sa2-nx tools/compare_video.py, make_video.sh) : défauts de rendu de
      l'émulation à noter avant le rendu natif.
- Critère d'arrêt du guide : le jeu va du démarrage aux menus, au jeu et aux cinématiques comme sur la console.

## Étape 3 — Rendu natif (guide §5-6) — en cours (voie A choisie le 2026-10-07)
Rendu de sa2-nx au niveau du GPU (lecture de l'anneau PM4, Vulkan), voie B (niveau du moteur Alchemy) écartée.
- [x] SDK de sa2-nx compilé sous Linux (générateur : ~/workspace/sa2-nx/sdk/out/linux-amd64/rexglue ; runtime
      compilé avec le jeu par REXSDK_DIR) ; glslangValidator dans ~/workspace/sa2-nx/out/glslang.
- [x] Fonctions Direct3D d'Operation Darkness (docs/D3D.md) : Swap, chargement des shaders au dessin, VS/PS liés à
      device+12696/+12692 (XDK 2008), 6 fonctions de dessin, CreateVertexShader/CreatePixelShader.
- [x] Rendu importé (tools/import_rendu_sa2.sh -> src/me, src/cpu), interceptions src/od_me_hooks.cpp, couche
      Direct3D native de SA2 remplacée par src/od_api_stub.cpp ; option CMake OD_NATIF (cmake/rendu_natif.cmake).
- [x] Compilé avec OD_NATIF (build out/build/sa2sdk) ; GPU émulé : identique au SDK d'origine.
- [x] Paquet de shaders (tools/paquet_shaders.sh ; DXC 1.8.2505.1 Linux dans ~/workspace/_tools) : 89 shaders
      (71 créés par le jeu, 2 reconstruits, 10 propres à Direct3D enveloppés, 6 variantes réparées).
      Rendu natif 2026-10-07 : logos, écran titre, menu, introduction et mission 1 en 3D dessinés.
- [ ] Collecter les shaders sur tout le jeu (missions, cinématiques, menus de préparation) ; 2 variantes refusées
      par repair_vertex_variant_declarations (modèle de même taille mais autre shader : vs_from_n18, vs_from_n46).
- [ ] Draws non reliés à un relevé Draw* (relevés 43 519 pour 216 601 dessins) : d'autres chemins de dessin de
      Direct3D à intercepter (sub_823569F0 et voisins, docs/D3D.md).
- [ ] Comparer image par image avec le GPU émulé (captures en lockstep, guide §12).
Les shaders sont produits à l'exécution (D3DX9 intégré à la XEX) : les intercepter à la création les voit tous.

## Étape 4 — Switch (guide §7-8, torchlight « Version Switch »)
- SDK avec couche Horizon : celui de sa2-nx (`sdk/`, ReXGlue 0.10 + correctifs de tous les portages, générateur aux
  tables de saut corrigées) plutôt que celui de nfsmw-nx utilisé par torchlight ; NVK (mesa-switch) déjà construit
  dans ~/workspace/mesa-switch.
- Paquet de données unique (build_asset_pack.py) : 5 300 fichiers / 4,6 Go, dont 3 Go de vidéos WMV ; la taille sur
  la carte SD et le décodage WMV sur la console sont à évaluer tôt.
- Mesure sur la console : profileur du SDK (rex_profile.log, échantillons de piles), outils de torchlight (console.sh).

## Étape 5 — Performances (guide §9-11), puis distribution (§13)
Dans l'ordre de rendement du guide : options du générateur (registres en locales, appels directs, sans volatile),
-O3 -ffp-contract=off -fno-strict-aliasing, LTO, ordre des fonctions chaudes, PGO ; natives vérifiées des fonctions
chaudes ; coût CPU du rendu ; attente GPU qui dort ; puis remplacement de Direct3D (§10) si le CPU limite encore.
Jeu au tour par tour : 30 images/s stables suffiront sans doute, à confirmer.
Distribution : paramètres intégrés, journaux coupés, aucune donnée du jeu (shaders générés chez le joueur).
