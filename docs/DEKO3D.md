# deko3d : le rendu reNut au niveau de Direct3D (étude du 2026-10-20)

Demande de l'utilisateur : reprendre l'usage de deko3d de futari-nx (github.com/ChanseyIsTheBest/futari-nx) et
l'essayer ici, à cause de la poussée de la communauté et des défauts de shaders (arme invisible du tireur, docs
OPTIMISATION.md « deko3d » pour l'étude du 2026-10-16, qui ne comparait que les compilateurs de shaders).

## Ce que font les ports voisins (futari-nx 1.0.1, sa2-nx 1.0.3, banjo-nx, peggle2-nx)
Ce n'est pas deko3d branché sous notre rendu : c'est **un autre rendu**, celui de reNut (github.com/masterspike52/reNut,
recompilation de Banjo-Kazooie: Nuts & Bolts ; réutilisation permise par son auteur, à créditer), adapté par banjo-nx.
- Les appels Direct3D du jeu sont interceptés (une vingtaine de fonctions : création du device, KickOff, barrières,
  Swap, dessins, ClearF, Resolve, enregistrement des shaders, déclarations de sommets, destruction des ressources).
  Les fonctions d'état du jeu restent les siennes : elles tiennent les copies d'état du device que les dessins lisent.
- **Pas d'anneau PM4 écrit ni relu, pas de modèle de l'EDRAM, pas de registres Xenos** : l'anneau devient un puits
  que KickOff rembobine ; les barrières se terminent quand le fil du rendu a lu le travail.
- Un fil du rendu rejoue chaque dessin sur plume (github.com/renderbag/plume, d'UnleashedRecomp) : Vulkan ou D3D12 sur
  PC, deko3d (dorsale de banjo-nx) sur la Switch. Plus de pilote Vulkan dans le .nro (NRO -14 Mo).
- Shaders : conteneurs du jeu -> XenosRecomp de reNut (app/thirdparty/reblue-XenosRecomp) -> HLSL -> **DXC** -> SPIR-V
  -> SPIRV-Cross -> GLSL -> passes de banjo-nx (tools/deko3d_glsl_opt.py) -> paquet ; **uam** compile chaque variante
  sur la console à la première utilisation (cache DKSH deko3d_shaders.bin à côté du .nro). Plus de glslang sur la
  console : la chaîne de traduction est celle du PC (DXC), dont le fusil du tireur est visible.
- Mesures publiées : banjo-nx, coût de NVK sur l'A57 ~3 µs par changement de pipeline, 2,4-2,9 µs par dessin ;
  sa2-nx (City Escape, ~1 400 dessins par image) : fil du rendu 17,6 -> 11,1 ms après le travail sur le fil du rendu,
  GPU 5-6 ms ; futari-nx : 1-4 ms de fil du rendu, 4-10 ms de GPU par image.
- Guide de portage : sa2-nx docs/deko3d-renderer-guide.md (amont, version 1.0.3.1), compte rendu
  docs/renut-renderer.md ; futari-nx docs/futari-notes.md « The deko3d renderer ». Copie de travail : `git -C
  ~/workspace/sa2-nx show origin/main:<chemin>` (récupéré le 2026-10-20, sans toucher à l'arbre de travail patché).

## Notre Direct3D contre celui de Banjo (base de reNut)
Bibliothèques statiques du XEX (en-tête 0x000200FF) : **D3D9 2.0.6534** (Banjo 2.0.7645, SA2 2.0.21076, futari 2.0.9328).
Relevé du 2026-10-20 dans le code du jeu (tools/fonctions.py) : le device suit **Banjo** pour tout ce que lit reNut,
contrairement à SA2 (+128 / +376 / +400) et futari (+0xAC... dans la partie privée).

| Champ (reNut, valeur Banjo) | Operation Darkness | Lu dans |
|---|---|---|
| drapeaux 10940 / 10941 (0x2ABC / 0x2ABD) | 10940 / 10941 | sub_82349D40 (type 2 : `ori 128` à 10940), sub_82344EA0 (`ori 32` à 10941) |
| mot de fin des barrières 10896, barrière courante 10908 | 10896, 10908 | Swap sub_8234A5C8 |
| qword global 10880, fil créateur 10888, processus titre 10892 | 10880, 10888, 10892 | sub_8234F488 |
| tampon annexe 16728 (0x12C0 octets) | 16728, 4800 octets | sub_8234F488, sub_82344EA0 |
| shaders liés PS / VS 0x3194 / 0x3198 | 12692 / 12696 | docs/D3D.md |
| compteur des Swap 0x40B0, barrière présentée 0x3A5C | 16560, 14940 | Swap |
| sections critiques 14944 / 14972 | 14944 / 14972 | sub_8234F4F8 |
| mots à -1 (21560, 21564, 14936) | **21556, 21560**, 14936 | sub_8234F700, sub_8234F4F8 |
| fréquence du temps 21588 | **21584** (+4 : la période) | sub_8234F4F8 |
| drapeaux de comportement 23980 (0x5DAC) | **22300** | sub_82349D40 |
| taille du device 0x5E00 (24064) | **22400** (0x5780), alignement 128 | sub_82349D40 |

Écarts seulement en fin de device (vers 21 500 et au-delà) : chaque décalage de la fin est à relire dans notre code.
Vérifiés aussi aux places de Banjo : barrière du dernier Swap 10928 (0x2AB0), drapeau « dans KickOff » 13232,
tampon de commandes enregistré 13516 (0x34CC), fin d'anneau des sommets en ligne 13444 (0x3484), déclaration de
sommets 11820 (0x2E2C, SetVertexDeclaration sub_8233FEF0), tampon d'index 12436, cibles 12440 (3110 x 4), profondeur
12456, flux 12460 (3115 x 4), pas des flux 12528, textures 12544 (3136 x 4), rampe gamma 15004 (0x3A9C, lue par Swap),
rappel de vblank du jeu 16544, compteur de vblanks 16548, file des retours d'écran 16700 / 16704.

## Recensement des fonctions (étape 1, terminée le 2026-10-20)
Méthode : rôle de chaque fonction dans le graphe d'appels (tools/fonctions.py info / appelants / cherche) et champs
du device qu'elle lit, contre la description des fonctions de Banjo dans les interceptions de reNut (copie de sa2-nx,
d3d/hooks/*.cpp : signature et rôle de chacune, adresse de Banjo entre crochets).

**À intercepter** (liste de sa2-nx, notre adresse) :
| Fonction | OD | Banjo | Preuve |
|---|---|---|---|
| Direct3D_CreateDevice | sub_82349D40 | 8264DF28 | alloue 22400 (align. 128), type 2 -> chemin sans matériel |
| CDevice::KickOff | sub_82345D60 | 82210AB0 | 81 appels ; teste 13232 et 10940 & 0x80 ; rend m_pRing |
| CDevice::KickOffSegment | sub_82345BD8 | 822106C8 | appelée par KickOff seulement ; chaîne le segment (13516, 13536) |
| CDevice::BeginRingBig | sub_82345F78 | 821F7C18 | (device, nombre) : KickOff si m_pRing + 4n > limite, puis l'allocateur de segments |
| allocateur de segments (sa2 sub_8254DA90) | sub_82345A60 | | appelé par KickOffSegment, BeginRingBig, Swap ; anneau 14928, limites 52 / 56 |
| RingBufferDeviceAllocate | sub_82344EC8 | 8222D3E0 | (device, drapeaux, *taille, alignement) ; 14904-14928 |
| AllocateInlineData | sub_823450D0 | 8222D240 | (device, mots, alignement) ; tampon de commandes 13516 sinon RingBufferDeviceAllocate |
| CDevice::BlockOnFence | sub_82345720 | 8228F558 | (device, barrière, type, sans KickOff) ; 10896 / 10908 ; CBlocker |
| CBlocker::Check | sub_82349A30 | 8264DCD0 | entre sub_82349880 (début) et sub_823498B0 (fin), 6 boucles d'attente |
| D3DDevice_IsFencePending | sub_82345E98 | 8228F650 | identique à Banjo (VdGlobalDevice, KickOff si barrière courante) |
| D3DDevice_InsertCallback | sub_82346058 | 821FB688 | (device, type, rappel, contexte) -> sub_823457E8 (écriture du paquet) |
| SynchronizeToPresentationInterval | sub_8234A120 | 8228A748 | (device, immédiat) ; pose 0x10 à 10941 ; rappel de Swap 0x82349F88 |
| D3DDevice_Swap | sub_8234A5C8 | 82287590 | seul appelant de VdSwap ; 16560, 14940 |
| BeginVertices / BeginIndexedVertices | sub_826BD808 / sub_826BDD00 | 8222CC50 | rangent la fin d'anneau à 13444 ; fins sub_826BDCA8 / sub_826BE230 |
| DrawVertices | sub_826BE2D0 | 8222C7A0 | (device, primitive, début, nombre) |
| DrawIndexedVertices | sub_826BE6B8 | 826567C8 | (device, primitive, sommet de base, premier index, nombre) |
| DrawVerticesUP | sub_826BDCB8 | 8222E520 | BeginVertices + copie (nombre x pas) + fin |
| DrawIndexedVerticesUP | sub_826BE230 | | BeginIndexedVertices + deux copies + fin (reNut : par BeginIndexedVertices, comme futari) |
| Draw(Indexed)TessellatedVertices ? | sub_826BEB10 / sub_826BEF88 | 82230838 (indexée) | 10612 & 3 = 1, bit de m_Pending[3], tables 0x82095030 ; à confirmer à l'exécution |
| D3DDevice_ClearF | sub_82358A38 | 822811A0 | (device, drapeaux, rect, *couleur, Z en f1, stencil) ; sans rect : taille de la cible |
| D3DDevice_Clear | sub_82358B60 | 822812C8 | D3DCOLOR -> 4 flottants, ClearF par rectangle (pas à intercepter : passe par ClearF) |
| D3DDevice_Resolve | sub_823569F0 | 8222D6C8 | (device, drapeaux, rect, texture, point, niveau, tranche, *couleur, Z f1, ...) ; 13 appels |
| XGRegisterVertexShader | sub_8233FBE0 | 8264E7B8 | (shader, partie physique) ; seul appelant CreateVertexShader |
| CreatePixelShader (XGRegisterPixelShader copiée dedans) | sub_82341498 | 8264E670 | type 0x100007, partie physique à +24, conteneur à +40 : intercepter la création |
| CreateVertexShader | sub_823415A8 | 8264E8B0 | objet de 872 octets + conteneur à +872 |
| CreateVertexDeclaration | sub_8233FFF8 | 8264EA90 | compte les éléments jusqu'à 0xFF, alloue 56 + 12 n |
| D3DResource_Destroy | sub_8233B160 | 822084F0 | aiguillage sur Common & 0xF, BlockOnFence, libérations ; appelée par Release sub_8233B708 |
| écriture du paquet de fenêtre (sa2 sub_8254FCE8) | sub_823379C0 | | n'écrit que dans l'anneau (ciseaux en arguments) : l'interception remplit +0x28C0 comme sa2-nx |

**Utilisées par l'interception de la création** (le chemin de SA2 : la création du jeu en type 2, puis ce que le
chemin matériel donne au device titre, sauf le GPU) :
| Fonction | OD | Banjo | Preuve |
|---|---|---|---|
| initialisation commune (sa2 sub_8255C808) | sub_8234F488 | | qword 10880, fil 10888, tampon annexe 16728 |
| chemin sans matériel (sa2 sub_8255C878) | sub_8234F700 | | -1 à 21556/21560/14936, puis sub_8234EBA8 et sub_8234EDC0 |
| anneau = tampon annexe plein (sa2 sub_8254D040) | sub_82344EA0 | | +48/+52/+56 sur le tampon annexe, `ori 32` à 10941 |
| chemin matériel (sa2 sub_8255CDA0) | sub_8234F4F8 | 82659F30 | sections critiques 14944 / 14972, VdGlobalDevice, anneau, fréquence 21584 / 21588 |
| InitializePresentationParameters | sub_8234F028 | 82659AA8 | VdQueryVideoMode ; appelée par le chemin matériel |
| XenonGpu_Initialize ? / InitializeApiState ? | sub_8234EBA8 / sub_8234EDC0 | 82663208 / 826597C8 | appelées par les deux chemins, dans cet ordre (à confirmer en les lisant) |
| emplacement de VdGlobalDevice | 0x820009AC | 0x820007FC | `lwz r11,2476(0x82000000) ; stw r31,0(r11)` |

**Le vblank** (comme futari-nx, futari_renut_vblank.cpp) : sans matériel, VdSetGraphicsInterruptCallback n'est pas
appelé. Notre Direct3D enregistre le répartiteur sub_82344CF8 (sub_8234F408) ; type 0 = vblank : il lit un registre
du GPU (0x7FC86544) puis appelle le gestionnaire **sub_82349E88** (compteur 16548, file des retours d'écran, puis le
rappel du jeu à 16544) ; type 1 = rappels de l'anneau. Le rappel du jeu est **sub_8232C7E0** (moteur, installé par
sub_8232C840 via SetVerticalBlankCallback sub_8234AC90) : c'est lui qui cadence la tâche de la manette (60 Hz). Il
faut donc un fil qui appelle sub_82349E88 à 60 Hz (ou une fois par Swap en lockstep, --vblank_per_swap).

**Autres, à surveiller** : D3DDevice_Reset sub_82349B60 (relance l'anneau avec les paramètres de présentation +72,
appelée par le jeu sub_8255FB78 : à intercepter si elle sert), D3DDevice_BlockOnFence public sub_82345940,
InsertFence sub_82345E60, BlockUntilIdle sub_82345FE0, pose d'une barrière sub_82345560, CreateTexture sub_8233D6B0
(en-tête 52 octets), CreateSurface ? sub_8233D7D0, SetGammaRamp / SetPWLGamma ? sub_8233A3E8 / sub_8233A4C8 (+15004),
table gamma de l'affichage sub_82346A48 (VdGetCurrentDisplayGamma), ciseaux du device sub_82339FD8 (10436 / 10440).
Pas de SetRingBufferParameters trouvée (sans doute pas liée). Les fonctions d'état du jeu restent les siennes.

## Plan (guide de sa2-nx, adapté)
1. ~~Recensement : les ~22 fonctions et les décalages de la fin du device, chacun avec sa source~~ (fait, ci-dessus).
2. reNut sur PC (Linux, plume Vulkan), derrière une option de démarrage (`od_renderer = "renut"`), l'actuel rendu natif
   gardé comme référence ; comparaison à image exacte (rejeu lockstep). Partir de la copie de sa2-nx (amont du SDK et
   du rendu que nous utilisons déjà), décalages ramenés à ceux de Banjo puis aux nôtres. Points connus : direct_calls
   doit connaître les nouvelles interceptions (tools/apres_codegen.py) ; rampe gamma ; ciseaux de fenêtre ; cadence
   (le jeu suit-il les vblanks ? oui : tâche de la manette à 60 Hz, docs « Cadence du jeu »).
   Notre SDK (sa2-nx fdd6f14 + patches/sa2-nx-sdk.patch) est en retard de 17 commits sur sa2-nx (56 fichiers du SDK,
   dont context.cpp et xboxkrnl_rtl.cpp que notre correctif touche) : d'abord essayer reNut contre notre SDK, avec une
   couche de compatibilité comme leur sa2_compat.
3. Shaders : nos conteneurs (--od_dump_shaders, D3DX les compile à l'exécution ?) -> XenosRecomp de reNut -> DXC Linux
   (~/workspace/_tools/dxc-1.8.2505.1) -> SPIR-V -> SPIRV-Cross -> GLSL -> passes -> paquet ; uam pour PC.
4. Switch : dorsale deko3d de plume, uam dans le .nro, cache DKSH ; essai sur la console (pas de Ryujinx ici).
5. Mesures sur la console contre le .nro LTO + PGO de référence ; puis retrait de NVK.

## Étape 2 : reNut sur PC (en cours, 2026-10-20)
Travail sur la branche `renut`, dans un second arbre de travail **~/workspace/operation-darkness-nx-renut** (git
worktree) : son propre code généré (copie de generated/default, appels directs refaits par tools/apres_codegen.py pour
les nouvelles interceptions), pour ne pas toucher au code généré du dépôt principal, dont dépend l'analyse de la
collecte PGO n°3 (CLAUDE.md : rien à régénérer avant). assets/ y est un lien vers celui du dépôt principal.
- Import : tools/import_renut_sa2.sh (git archive de ~/workspace/sa2-nx origin/main, adf3a4e, version 1.0.3.1 : l'arbre
  patché n'est pas touché) -> src/renut_engine (versionné, écarts notés « opdarkness »), tools/renut (outils des
  shaders), out/renut-tiers (plume avec la dorsale deko3d, reblue-XenosRecomp, uam, imgui ; non versionné ; DXC Linux
  de ~/workspace/_tools lié aux places que lisent XenosRecomp et gpu.cmake).
- Compilation : cmake/rendu_renut.cmake (option OD_RENUT, demande OD_NATIF ; bibliothèque OBJECT od_renut_gpu) ;
  build out/build/renut de l'arbre renut (mêmes options que sa2sdk + -DOD_RENUT=ON). Choix au démarrage :
  --od_renderer=renut (sinon les interceptions rendent la main au jeu ou au rendu natif).
- Conteneurs de shaders : out/renut_shader_dump (les 83 de --od_dump_shaders) ; XenosRecomp de reNut + DXC -> cache
  compilé dans l'exécutable (renut_gpu/shader_cache.cpp) ; reNut capture à l'exécution ceux qui manquent
  (renut_shader_capture_dir), ajoutés à la compilation suivante.
- Adaptations du moteur (copie de sa2-nx, déjà portée à un second Direct3D) : décalages de SA2 ramenés à ceux de
  Banjo (d3d.h, d3d_native.h, device.cpp, queued.cpp : plage privée non lue 10880-11776, 39 blocs comparés), sauf les
  nôtres (taille 22400, drapeaux de comportement 22300, fréquence 21584, mots à -1 21556 / 21560, qword global
  0x82940E48, VdGlobalDevice 0x820009AC, intervalle de présentation 13596) ; adresses des interceptions (tableau
  ci-dessus) ; création du device comme sa2-nx (celle du jeu en type 2, puis sections critiques, VdGlobalDevice,
  fréquence, InitializePresentationParameters sub_8234F028, InitializeApiState sub_8234EDC0).
- Interceptions communes avec le rendu natif et nos outils (macros SA2_RENUT_YIELD* de sa2-nx) : Swap (compteur
  d'images, src/od_images.cpp), CBlocker::Check (src/od_attente_gpu.cpp), les dessins (src/od_me_hooks.cpp : les deux
  tessellés rendent la main à l'original, non dessinés), CreatePixelShader (od_me_hooks.cpp appelle
  od_renut_pixel_shader_created : XGRegisterPixelShader est recopiée dans la création).
- Non traité encore : BeginIndexedVertices (sub_826BDD00 : journalisé, non dessiné), dessins tessellés, D3DDevice_Reset,
  lecture tardive de la manette (rex/input/read_hook.h de compatibilité : à appeler depuis notre XInputGetState),
  captures en lockstep par l'image de reNut (sa2_capture.h : vide).
- Fil de vblank (src/od_renut_vblank.cpp, de futari-nx) : appelle le gestionnaire de vblank de Direct3D sub_82349E88 à
  60 Hz, ou une fois par image en lockstep (images comptées par od_images.cpp) ; démarré à la création du device du
  titre. La cadence des Swap de sa2-nx (sa2_renut_pacing.cpp, grille de 60 Hz) est gardée.
- Couche de compatibilité (src/renut_engine/od_compat, sa2_compat/rex/input/read_hook.h, sa2_compat/sa2_capture.h) :
  ce que le SDK et l'application de sa2-nx 1.0.3 ont et pas les nôtres (fdd6f14).
- À resserrer : tools/apres_codegen.py prend toutes les adresses 82xxxxxx de src/, y compris celles de Banjo citées
  par reNut (361 interceptions supposées au lieu de 55 : moins d'appels directs, sans danger) ; sa2-nx ne lit que les
  macros BANJO_RENUT_HOOK*(sub_X) de src/renut_engine.

### Résultats sur PC (2026-10-20 soir, Vulkan Intel UHD 620)
- Première image du premier lancement : écran titre ; scénario rapide (tools/scenario_combat.sh) jusqu'au menu d'action
  d'Edward et aux tutoriels, sans erreur propre à reNut (les « BaseHeap::Release failed » du chargement de la bataille
  sont aussi dans le journal du rendu natif).
- **À image exacte** (rejeu lockstep out/entrees/lockstep-test.txt, tools/captures_images.sh, images 9000, 12200,
  12400, 12600 ; out/captures/cmp-natif et cmp-renut) : images 9000 (choix du support de sauvegarde) et 12600
  (tutoriel « Move ») **identiques au pixel près** au rendu natif ; 12200 (bataille, menu d'action) même scène, écart
  dû à l'animation (bannière « MAIN ») ; 12400 : le rejeu a quelques images d'avance avec reNut. La brume et les
  personnages sombres de la bataille sont ceux du jeu.
- Cadence comparable : démarrage ~38 images/s comme le rendu natif (logos et vidéos), combat 53-58 (natif 54-58) ;
  fil du rendu 1,6-2,2 ms par image, GPU 7,6-8,6 ms (écran titre). sa2_renut_vblank_pacing sans effet ici.
- BeginIndexedVertices jamais appelée dans ce parcours.
- À voir : avertissements « Draw binds a depth target smaller than its colour target » avec des tailles de cible
  absurdes (1x16387, 4x31792, 4160x21419 ; au démarrage) : description de surface lue de travers par reNut ? (sans
  effet visible sur les captures).
- Lancement : BIN=out/build/renut/opdarkness, LIBS=~/workspace/sa2-nx/sdk/out/linux-amd64, options
  --od_renderer=renut (tools/captures.sh, tools/captures_images.sh de l'arbre renut ; out/entrees y est un lien).

## Étape 3 : la Switch (2026-10-20 / 21)
- .nro reNut sur deko3d (NVK encore lié), sous le nom operation-darkness-nx-renut.nro à côté du .nro habituel (la
  collecte PGO et les réglages du joueur ne sont pas touchés). Chaîne des shaders : tools/renut (XenosRecomp avec
  BANJO_DEKO3D_SPIRV_DIR, deko3d_shaders.py, deko3d_glsl_opt.py, deko3d_shader_package.py, renut_switch_shader_cache.py),
  outils PC tools/deko3d_outils.sh (SPIRV-Cross vulkan-sdk-1.4.304.0, uam de banjo-nx ; copie du conteneur pour la
  compilation Switch, glibc 2.36). 83 shaders, tous compilés par uam.
- Défaut trouvé : le jeu laisse liées des surfaces détruites (et d'autres jamais détruites par D3DResource_Destroy) ;
  le fil du rendu lisait leurs en-têtes dans de la mémoire réutilisée -> cibles géantes (jusqu'à 4227x29957), rochers
  étirés, images fantômes, GPU saturé. Correctif : en-tête capturé quand la surface est fixée (SetRenderTarget
  sub_8233AA88, SetDepthStencilSurface sub_8233ADF0, appelées par pointeur) ou détruite, comme le fait Direct3D
  (render/surfaces.cpp KeepSurfaceHeader). Vérifié sur la console (essai du 2026-10-20 23 h 30) : plus de surface
  aberrante, cibles 11 Mo.
- **Ouvert : GPU 10 fois plus lent que le rendu natif** dans le tutoriel de combat du scénario : 44-48 ms par image
  (22 images/s) contre 4,3 ms (natif NVK, archive 1940-parade, même scène, 60 images/s). Profil par shader
  (renut_gpu_profile + banjo_renut_diag_gpu_shaders) : PS 4C5CFD54864D90BF (blended, 40-65 dessins par image) 12-14 ms,
  EC78065CF82CFB45 6-7 ms, 1DBD799BC15740EB 5 ms, flou de profondeur 08FB9ABD43FE7C0C 2,6 ms (version d'origine : la
  version allégée od_flou_profondeur est celle du traducteur du rendu natif). Écartés : passes de banjo-nx (paquet sans
  passes : 48 ms), barrières (~4 000 / 10 s), taille des shaders uam (au plus 350 instructions, pas de scratch),
  horloge (sa2_gpu_boost actif), format de la cible (RGBA8), textures compressées (BC1-3 gardés).
- **Ouvert : « star glare » parasite** (point lumineux étalé en 4-6 traînées au-dessus et à gauche d'Edward, caméra
  dans son dos) et clignotements, console seulement ; pas les passes (même défaut sans elles) : SPIRV-Cross, uam, ou
  un écart de deko3d (NaN / infini ?). Le PC (SPIR-V de DXC directement) ne le montre pas.

### Essais du 2026-10-21 (suite)
- Lu : nfsmw-nx (branche deko3d-implementation) docs/HowToDeko3d.md et docs/deko3d-devlog.md. Déjà couverts chez nous :
  patchs 7 (pas de dual issue) et 8 d'uam (dans out/renut-tiers/uam), correctif de hauteur de tuile (plume).
- blend_skip en A/B (banjo_renut_ab_target = 8) : aucun effet (22 images/s des deux côtés).
- **reNut sur NVK** (renut_gpu_backend = "vulkan", même .nro) : même GPU (48 ms), même star glare, mêmes triangles
  étirés : ni deko3d, ni uam, ni SPIRV-Cross ne sont en cause.
- Retard du fil du rendu simulé sur PC (od_renut_diag_retard_ms, od_renut_diag_retard_des ; scénario rapide, ralenti à
  partir de l'image 6600) : ni triangles étirés ni star glare sur PC. Les rejeux lockstep ralentis dérivent (vidéos en
  temps réel) : inutilisables pour ça.
- Piège PC : une fenêtre cachée (autre bureau, recouverte) bride la chaîne d'images Vulkan de reNut à 1 image/s
  (acquireTexture du pilote Intel attend 1 s) ; --renut_vsync=false pour les essais.
- **Mesure de référence corrigée** : GPU en combat (scénario rapide, PC Intel UHD 620) : reNut 25 ms par image
  (PS 4C5CFD54864D90BF 7-8 ms pour ~75 dessins, EC78065CF82CFB45 4,2 ms, 1DBD799BC15740EB 1,9 ms), rendu natif ~17 ms
  (somme de « GPU time by pass », sa2_gpu_marks_categories, 50 images/s). L'écart réel est ~1,5x, pas 10x : le
  « 4,3 ms » du natif sur la console (archive 1940-parade) ne comptait qu'une catégorie de passes. Sur la console,
  reNut 44-48 ms (deko3d ou NVK) contre un natif à 60 images/s (< 16,7 ms).
- Restent inexpliqués : star glare et triangles étirés sur la console seulement (deko3d comme NVK), pas sur PC.

## Essais dans Ryujinx (2026-10-21, machine distante, voir le CLAUDE.md du dépôt principal)
- Le .nro reNut / deko3d tourne dans Ryujinx (fork banjo-nx) sur la RX 9070 XT : 60 images/s du démarrage au combat
  (ne dit rien de la console). NVK s'y affiche en noir (le jeu dessine pourtant normalement) : pas de comparaison
  NVK / deko3d dans l'émulateur.
- Taches blanches en gros blocs sur une image isolée en combat (0 à 2,5 % des images selon les essais), comme une
  petite texture étirée sur l'écran. Elles disparaissent quand les dessins des 4 pixel shaders de fumée et de brume
  sont sautés (6DAED42B6F8D97E2, FD5FE7E3024732B9, 56CA3D912DA54EC8, 95A8928D32DF24B6 ; option
  od_renut_diag_sauter_ps, avec od_renut_diag_sauter_des pour ne commencer qu'en combat ; od_renut_diag_sauter_bits
  pour une bissection par bits de l'empreinte). Hors de cause : les optimisations de profondeur de deko3d
  (compression, Zcull, D24, transmission des résolutions, barrières allégées). Non tranché : la fréquence varie trop
  d'un essai à l'autre (et selon la charge de la machine) pour isoler un seul shader ou réglage en 200 images ; un
  essai dans le dossier principal de Ryujinx en avait, un essai simultané dans un dossier copié non, sans qu'un fichier
  (pipeline_list.bin) l'explique. Même famille possible que les « triangles étirés » de la console : non prouvé.
- Défaut de lecture du démarrage (tampon nul) reproduit dans l'émulateur chargé et corrigé dans le SDK (voir le
  CLAUDE.md du dépôt principal) ; relevés dans src/od_lecture.cpp (sub_82330990, sub_82330018).
- Flou de profondeur allégé reporté (2026-10-21) : tools/renut/od_flou_profondeur.py, la même réécriture que
  od_flou_profondeur du rendu natif (shader 08fb9abd43fe7c0c, 3 lectures de texture au lieu de 15), appliquée entre les
  deux passages de XenosRecomp (gpu.cmake, après renut_early_alpha.py) ; option CMake OD_RENUT_FLOU (1 par défaut,
  0 d'origine, 2 côte à côte). Le profil console lui donnait 2,6 ms par image sur 45-48. Paquet Switch refait par
  tools/renut/paquet_switch.sh (build PC, SPIR-V, GLSL, passes de banjo-nx, paquet ; ~10 min sur la machine distante).
- od_renut_images_avance (render_thread.cpp, 1 d'origine) : 0 ne supprime pas les taches de fumée (14 sur 600 contre
  6 au témoin) : pas une course entre le jeu et le rendu. Écartés aussi : saut des résolutions répétées, fusion des
  barrières, emplacements de textures rendus (banjo_renut_diag_poison_slots : aucun magenta), mémos de textures,
  reprise des textures d'un dessin à l'autre (sa2_renut_chain_reuse ; vérification sans écart). Les taches tombent
  toutes sur l'écran assombri du tutoriel (« Cannot attack when Hit Rate is 0 ») et ressemblent à une autre vue du
  décor en noir et blanc (carte d'ombres ?) lue à la place de la texture de la fumée.

## Performance : définition dynamique de reNut (2026-10-22)
- Les 3 shaders les plus coûteux en combat (4C5CFD54864D90BF, EC78065CF82CFB45, 1DBD799BC15740EB) sont courts
  (4-5 lectures de texture, éclairage simple : feuillage, sol, personnages) : leur coût est la surface couverte, pas
  leur code. Rien à gagner à les réécrire sans changer l'image : levier = nombre de pixels.
- Définition dynamique (commit e3453d6) : codes d'échelle 5 et 6 = 3/4 et 1/2 (render_scale.h et renutTextureScale,
  patches/xenosrecomp-echelles.patch) ; CollectGpuTime nourrit le régulateur du rendu natif (sa2::render::NoteFrameGpu,
  WantedScaleQuarters : sa2_render_dynamic, sa2_render_height_dynamic 540, _min 360, mêmes règles) ; paquet deko3d sans
  scale1 ni fragscale (tools/renut/paquet_switch.sh) ; od_renut_paquet_echelles (aucun des 83 shaders du jeu ne lit le
  code d'échelle, la recherche de sa2-nx concluait « 720p seul »). Vérifié : PC et Ryujinx en 540p et 360p fixes.
- Console (2026-10-22, cycle 720/540/360 dans une vue chargée) : 46 / 32 / 17 ms de GPU par image. Deux shaders font
  60 % du temps : les **ombres portées** (PS 4C5CFD54864D90BF + VS E45C1440418E62B8, PS EC78065CF82CFB45 + VS
  A75DAE485A8C8B7B ; mélange dst * (1 - couleur), un dessin par personnage et par morceau de terrain). RenderDoc
  (PC) : 98 % des pixels tramés par ces dessins ne changeaient rien. **Découpe** (commit 38ab670,
  tools/renut/od_ombres.py, od_renut_ombres_decoupe = 1) : distances de découpe des deux vertex shaders à la zone de
  la carte d'ombres (marge 0,03), commandées par dessin (constante partagée 1352). PC : pixels tramés -92 %, écart
  1/255 sur ~9 000 pixels. **Console : 44-48 -> 22-25 ms de GPU par image en 720p dans les mêmes vues.** Les ombres
  qui s'estompent et reviennent (« passage de nuage ») sont un effet du jeu : l'utilisateur les voit aussi avec le
  rendu natif (NVK) au même endroit. Ni la découpe ni reNut n'y sont pour rien.
- Idée de l'utilisateur : palier 648p (9/10, code 7 libre sur la console) ; à décider d'après les temps GPU en combat
  à 540p mesurés sur la console (régulateur à adapter : multiples de 180 lignes seulement).

## Shaders des fichiers du jeu traduits d'avance (2026-10-22)
- Comme sa2-nx 1.0.3 (page d'installation : tous les conteneurs trouvés dans les fichiers, traduits d'avance) :
  tools/renut/shaders_fichiers.py cherche les conteneurs (vérification de reNut) dans l'image de l'exécutable et dans
  assets/. Les .igb (486 Mo) sont presque tous compressés en XMemCompress (signature 0x0FF512EE, LZX) :
  tools/renut/xmem_decompresser.c les décompresse avec lzxd de libmspack (982 fichiers, aucun échec).
- Résultat : 166 shaders dans les fichiers (121 dans l'exécutable, le reste dans les .igb), dont 78 des 83 vus en jeu
  avant la mission 2 ; paquet porté à 176 shaders (avec les captures), tous compilés par uam avec les passes.
- Ne sont dans aucun fichier : les 4 shaders d'ombres, 5 des 9 de la mission 2, ps_6DAED42B6F8D97E2. L'exécutable
  contient des modèles de HLSL à trous (« tex2D(TextureSampler[%d], Input.uvPosition[%d])*Param[%d] »...) que le jeu
  remplit et compile avec son D3DX pendant la partie ; d'autres sont des shaders des fichiers retouchés de 1 ou
  2 octets (ps_87d7E681 = ps_CD0082D2 à 2 octets près). Leur nombre n'est pas borné d'avance : la traduction par la
  console reste nécessaire pour eux (section suivante).

## Traduction des shaders par la console (deko3d) : faisabilité (2026-10-22)
- Besoin : un shader absent du paquet n'est que capturé (shader_capture/), ses dessins manquent (mission 2 : personnage
  incomplet, bâtiments en trame ; 9 shaders ajoutés à la main). Le jeu dure ~40 h : la console doit traduire elle-même.
- Chaîne validée sur PC sans DXC (92 shaders sur 92, images identiques au paquet DXC dans Ryujinx) :
  HLSL de XenosRecomp -> tools/renut/prep_glslang_renut.py (prep_glslang.py du rendu natif + fonctions qui renvoient une
  texture en macros, and()/or(), « const » des locales retiré) -> glslang HLSL (-D -V, BANJO_DEKO3D, RENUT_RECOMP) ->
  spirv-opt --legalize-hlsl (indispensable : sinon des fonctions à paramètre texture2D qu'uam refuse) -> SPIRV-Cross ->
  deko3d_glsl.py (motif SHARED_WORD_RE élargi : glslang nomme l'instance g_UboShared_1) -> uam. Sans les passes de
  banjo-nx (facultatives).
- Reste pour la console : XenosRecomp (variante reNut) dans le .nro et ses entrées de cache (ShaderCacheEntry) ;
  préparation en C++ (src/od_prep_glslang.cpp à étendre) ; glslang et l'optimiseur SPIRV-Tools (déjà liés pour le rendu
  natif) ; SPIRV-Cross pour la Switch (nfsmw-nx l'a : branche deko3d-implementation, app/src/nfsmw_dk_glsl.*) ;
  deko3d_glsl.py en C++ ; à l'exécution, traduction sur un fil d'arrière-plan au premier dessin d'un shader inconnu,
  ajout au paquet en mémoire et sur la carte.

### Chaîne en C++ et essai sur la console (2026-10-23)
- Doute de l'utilisateur après le guide de sa2-nx (docs/deko3d-renderer-guide.md, section 10) : sa2-nx ne fait sur la
  console que GLSL -> DKSH (uam) ; tout le reste (XenosRecomp, DXC, SPIRV-Cross, passes) se fait d'avance, sur la page
  d'installation, parce que ses 103 shaders sont tous dans ses fichiers. Pas nous : les shaders que le jeu compile avec
  D3DX pendant la partie (ombres, mission 2) ne sont connus qu'à l'exécution, donc toute la chaîne doit tourner sur la
  console, sans DXC. nfsmw-nx fait déjà tourner SPIRV-Cross sur la Switch (fil de compilation, nfsmw_dk_glsl.cpp).
- Code (même code sur PC et console) : src/od_chaine_deko3d.{h,cpp} (la chaîne et l'essai), src/od_xenosrecomp_renut.*
  (XenosRecomp de reNut inclus dans l'espace de noms od_renut_xr : le rendu natif lie celui de sa2-nx, mêmes classes),
  src/od_prep_glslang.cpp PreparerRenut (prep_glslang_renut.py), src/od_deko3d_glsl.* (deko3d_glsl.py, cas des shaders
  du jeu, sans std::regex) ; SPIRV-Cross compilé depuis ~/workspace/_tools/SPIRV-Cross (od_spirv_cross) ; glslang et
  SPIRV-Tools-opt du SDK ; uam de plume sur la console. cmake/rendu_renut.cmake, fin.
- Réglages qui comptent : glslang en Vulkan 1.0 / SPIR-V 1.0 (comme -V ; en 1.5 il décore les tampons en BufferBlock
  et le validateur refuse) ; son optimiseur coupé puis RegisterLegalizationPasses de SPIRV-Tools avec
  ValidatorOptions::SetBeforeHlslLegalization (= spirv-opt --legalize-hlsl ; sans cette option, la validation d'avant
  les passes refuse ce qu'elles doivent légaliser). Pas d'inversion de y (deko3d).
- Trouvé en route : les shaders compilés par D3DX nomment leurs constantes avec « $ » ($diffuseMap, $matWVP), que DXC
  accepte et glslang non : « $ » -> od_S_ dans la préparation (C++ et prep_glslang_renut.py). Sans cela, 2 des 176
  shaders échouaient, justement de ceux que le jeu compile lui-même.
- GCC (Switch) refuse les structures anonymes des unions locales de shader_recompiler.cpp (Clang les accepte) : unions
  nommées et références, comme sa2-nx l'a fait dans sa copie ; dans patches/xenosrecomp-opdarkness.patch.
- Vérifié sur PC (outil od_chaine_deko3d, cible de la build PC) sur les 176 conteneurs du paquet : 176 traduits
  jusqu'au DKSH, et chaque étape identique octet pour octet à la chaîne des scripts : HLSL (= vidage du programme
  XenosRecomp), préparation (= prep_glslang_renut.py), SPIRV-Cross bibliothèque (= programme spirv-cross), conversion
  (= deko3d_glsl.py).
- Temps sur PC (un fil) : 163 s pour les 176, dont uam 127 s et légalisation 25 s ; médiane 88 ms par shader, mais une
  dizaine de vertex shaders géants (vs_1390cfb07db752d3 : 99 Ko de HLSL, 358 Ko de GLSL, DKSH de 207 Ko) prennent 3 à
  13 s, presque tout dans uam. Ce n'est pas dû à glslang : par DXC, le même shader donne 314 Ko de GLSL, un DKSH de
  207 Ko et 11 s d'uam. Coût déjà présent avec le paquet actuel (variantes compilées par uam sur la console) ; à mesurer
  sur la console.
- Essai sur la console : --od_essai_deko3d=essai_deko3d (opdarkness.toml de la carte) traduit au démarrage (OnPreSetup,
  avant que plume ne prenne uam pour son fil) les conteneurs de essai_deko3d/conteneurs/ (copie de
  out/renut_shader_dump) dans essai_deko3d/sortie/, du plus petit au plus gros, une ligne par shader dans lignes.txt
  aussitôt écrite (un essai arrêté par HOME garde ce qui est fait), bilan dans rapport.txt et le journal, puis le jeu
  démarre. Comparaison : tools/renut/comparer_essai_deko3d.sh <sortie de la console> <sortie de l'outil PC>.
- **Résultat sur la console (2026-10-23, .nro de 8 h 54, ELF out/switch-elf/opdarkness-20261023-0854-renut-essai-
  deko3d.elf, 1 020 MHz) : faisable.** 176 traduits jusqu'au DKSH, et toutes les étapes identiques octet pour octet au
  PC (HLSL, préparation, SPIR-V, GLSL Vulkan, GLSL, DKSH : 176 sur 176 chacune). Sorties : out/essai-deko3d-console/
  (journal opdarkness_033.log) et out/essai-deko3d-pc/. Tas : 35 Mo au plus.
- Temps sur la console (~6 fois le PC) : 1 013 s pour les 176, dont uam 804 s, légalisation 132 s, SPIRV-Cross 18 s,
  glslang 11 s. Par shader : médiane 0,43 s (0,27 s avant uam), 112 sous 1 s, 134 sous 3 s, mais 25 au-delà de 10 s :
  les vertex shaders géants de default.xex (vs_1390cfb07db752d3 94 s dont uam 87 s, 349 Ko de GLSL ; 5 autres de 59
  à 74 s). Les deux shaders compilés par D3DX du lot (ps_d3acc77e3e305d34, vs_9fdead3f9ba24c03) : 0,1 et 0,2 s.
- **Objectif de l'utilisateur (2026-10-23)** : installation sur la Switch sans étape de pré-traduction sur PC ; un écran
  d'installation avec barre de progression sur la console lui convient. Points retenus : mode boost et plusieurs cœurs.
- Essais suivants (deux phases : 1. jusqu'au GLSL sur 3 fils placés sur les cœurs 0-2, od_switch_cpu ; 2. uam sur un
  fil ; mode boost, qui donne 2 397 MHz sur la console de l'utilisateur, réglée plus haut que les 1 785 MHz d'origine ;
  GPU à 76,8 MHz, mémoire à 1 600 MHz) ; sorties toujours identiques au PC, 176 sur 176 à chaque étape :
  - sans rien (1 fil, 1 020 MHz) : phase 1 209 s, uam 804 s ;
  - boost + 3 fils, allocateur de newlib : phase 1 **480 s** (légalisation 1 246 s tous fils contre 132 s seule) : un
    seul verrou pour tout le tas (profil : _free_r, _malloc_r, mutexLock ; 125 % de charge) ; uam 522 s (x 1,54 pour
    une horloge x 2,35 : uam semble limité par la mémoire) ;
  - boost + 3 fils + **rpmalloc** dans les fils de la phase 1 (src/od_alloc_fils.cpp : operator new/delete du programme
    remplacés sur la Switch ; rpmalloc dans les fils activés, newlib ailleurs, delete selon l'adresse ; zone de
    256 Mo prise une fois à newlib ; essayé aussi sur PC, OD_ALLOC_FILS_FORCE dans l'outil) : phase 1 **19,7 s**,
    uam 507 s. ELF out/switch-elf/opdarkness-20261023-1008-renut-essai-deko3d-rpmalloc.elf, sorties
    out/essai-deko3d-console-4/.
- Conséquence : un écran d'installation qui fabrique le GLSL des shaders des fichiers tient en ~20 s (sans doute ~30 s à
  1 785 MHz, non mesuré) ; uam (8 à 9 min en tout, les vertex shaders géants surtout) reste à faire au fil du jeu par
  variante, comme aujourd'hui, ou pendant l'installation.
- Boost et plusieurs cœurs : faits (ci-dessus, src/od_switch_cpu.cpp : appletSetCpuBoostMode FastLoad, svcSetThreadCoreMask).
  Reste : savoir si les vertex shaders géants servent en jeu, et pourquoi uam y passe autant de temps.

### Prochaine session : installation sur la console sans PC (chantiers, dans l'ordre proposé)
Demande de l'utilisateur : plus d'étape de pré-traduction sur PC ; un écran d'installation sur la Switch avec barre
de progression. Ce qui existe : la chaîne (od_chaine_deko3d : TraduireGlsl sur plusieurs fils avec rpmalloc,
CompilerDksh sur un fil), le mode d'essai (--od_essai_deko3d), l'outil PC od_chaine_deko3d et la comparaison
(tools/renut/comparer_essai_deko3d.sh). La traduction se fait aujourd'hui dans OnPreSetup, avant le rendu (plume prend
uam pour son fil ensuite : ne jamais appeler uam depuis deux fils).
1. **Trouver les conteneurs sur la console** : porter tools/renut/shaders_fichiers.py en C++ (image de default.xex : le
   SDK l'a chargée ; .igb compressés en XMemCompress, signature 0x0FF512EE, LZX : tools/renut/xmem_decompresser.c avec
   lzxd de libmspack, déjà dans le SDK, libmspack.a). Vérifier : les 166 des fichiers retrouvés, mêmes empreintes que
   le PC.
   **Fait le 2026-10-27 (PC ; console à essayer)** : src/od_conteneurs.{h,cpp} (vérification de shaders_fichiers.py,
   décompression XMemCompress en mémoire par lzx_decompress du SDK, celui du chargeur de XEX ; seuls default.xex et
   les .igb sont lus : relevé du script sur tout assets/, 121 + 45 fichiers). Image de default.xex prise dans la mémoire
   invitée (OnPostLoadXexImage, avant le démarrage du jeu). Outil PC : od_chaine_deko3d --fichiers <image.bin> <racine>
   <sortie> [fils] ; dans le jeu : --od_essai_conteneurs=<dossier> (boost, fils et allocateur des réglages
   od_essai_deko3d_*). PC, outil et jeu : 166 shaders (99 vs), fichiers.csv identique à celui du script, conteneurs
   identiques octet pour octet. 1 067 .igb (982 compressés, 534 Mo) ; un fil 11 s (LZX 10,3 s), 3 fils 3,8 s. Essai
   console : .nro out/build/switch-renut/opdarkness-essai-conteneurs.nro (ELF
   out/switch-elf/opdarkness-20261027-1341-renut-essai-conteneurs.elf), od_essai_conteneurs = "essai_conteneurs" dans
   opdarkness.toml. **Essai console (2026-10-27, journal opdarkness_038.log, sorties out/essai-conteneurs-console/) :
   166 shaders, fichiers.csv et conteneurs identiques au PC ; 12,0 s (3 fils, boost ; lecture 23,9 s et LZX 10,4 s
   tous fils confondus : la carte SD limite, pas le LZX).** Plus 4,6 s pour écrire les 166 .bin (propre à l'essai).
2. **Entrées du cache de reNut à l'exécution** : aujourd'hui shader_cache_switch.cpp (g_shaderCacheEntries,
   g_shaderVertexInputs, g_shaderLiterals) et le paquet GLSL (deko3d_glsl_switch.cpp) sont compilés dans le .nro depuis
   le PC. Les remplir depuis xenosrecomp_renut::Shader (masques, constantes, entrées de sommets, littéraux : déjà
   rendus par Recompiler) ; écrire un paquet sur la carte (format BDKG de banjo_renut_deko3d.cpp, comme le
   deko3d_glsl.bin de sa2-nx 1.0.3) ; accesseurs dans reNut (d3d/hooks/shader.cpp CreateShaderRecord /
   RegisterGuestShader, FindShaderEntry avec repli dynamique). Plus aucune donnée du jeu dans le .nro (cf. sa2-nx
   renut_release_shaders.py).
   **Fait le 2026-10-27 (PC vérifié ; console à essayer)** :
   - reNut : entrées faites à l'exécution (render/shader_library.cpp : AddRuntimeShaderEntry, RuntimeShaderEntry de
     shaders/shader_cache.h avec ses entrées de sommets et ses littéraux, drapeau kShaderFlagRuntime) ; toutes les
     lectures passent par ShaderVertexInputsOf / ShaderLiteralsOf (draw.cpp, d3d/hooks/shader.cpp). FindShaderEntry :
     table intégrée (sauf od_renut_shaders_integres = false), puis renut_fiches.bin à côté du .nro (lu au premier
     usage), puis les entrées ajoutées. Le paquet GLSL intégré est aussi ignoré avec od_renut_shaders_integres = false
     (deko3d_glsl.bin à côté du .nro seul, que reNut lisait déjà).
   - src/od_installation.cpp (--od_installer = si_absent | toujours | non, après le chargement de default.xex) :
     conteneurs (point 1), GLSL sur 3 fils (chaine_deko3d::TraduireGlslTous), renut_fiches.bin (format ODFC :
     shader_library.h) et deko3d_glsl.bin (BDKG sans dictionnaire, une trame zstd niveau 3 par shader ; le compresseur
     de zstd ajouté à renut_zstd par cmake/rendu_renut.cmake). Chaque fiche comparée à la table de l'exécutable
     (od_installer_verif).
   - PC : 166 fiches, **0 écart** avec la table de l'exécutable ; recherche 2,7 s, traduction 13,6 s (3 fils), table
     21 Ko, paquet 1,1 Mo. Essai du rendu par les fiches : od_installer_spirv_integre (les fiches prennent le SPIR-V
     de la table intégrée, rien n'est écrit) + od_renut_shaders_integres = false.
   - Console : .nro out/build/switch-renut/opdarkness-installation.nro (ELF
     out/switch-elf/opdarkness-20261027-1440-renut-installation.elf). Sans les retouches du paquet (point 4) ni les
     shaders que le jeu compile (point 5) : image et vitesse différentes du paquet du PC, quelques dessins absents.
   - **Essai console (2026-10-27, journaux opdarkness_039/040, out/essai-installation-console/)** : installation
     31,3 s sous boost (recherche 12,1 s, traduction 19,1 s sur 3 fils, paquet 0,1 s) ; renut_fiches.bin et
     deko3d_glsl.bin **identiques octet pour octet** à ceux du PC. Second lancement : « déjà faite », table et paquet
     lus (166), menus, introduction, tutoriel et combat dessinés (captures), 59,8 images/s au tutoriel. Manquent les
     5 shaders que le jeu compile (EC78065CF82CFB45, A75DAE485A8C8B7B, 4C5CFD54864D90BF, E45C1440418E62B8,
     6DAED42B6F8D97E2 : point 5). Fichiers retirés de la carte ensuite (le .nro renut habituel lirait ce paquet sans
     retouches).
3. **Écran d'installation** avec barre de progression (premier lancement ou paquet absent / d'une autre version) :
   sous boost, 3 fils ; GLSL ~20 s chez l'utilisateur (2 397 MHz). Choix à faire avec l'utilisateur : DKSH pendant
   l'installation (+8-9 min) ou au fil du jeu (comme aujourd'hui, cache deko3d_shaders.bin).
   **Fait le 2026-10-27** (idée de space-hulk-nx : barre de progression, le nom du jeu, l'étape) : l'installation
   tourne sur un fil à elle ; le fil de l'interface, qui l'a lancée depuis OnPostLoadXexImage, dessine une fenêtre
   ImGui du SDK (classe Ecran, od_installation.cpp) et la présente toutes les 33 ms par
   renut::gpu::PresentOverlayNow (overlay/app_integration.cpp : ce que fait un tour de l'OverlayPump, qui a besoin du
   fil de l'interface). Étapes : recherche (0-40 %), traduction (40-98 %), écriture, « Starting the game ». Vu sur PC.
   uam pendant l'installation (choix de l'utilisateur, 2026-10-27 ; message « up to 9 minutes ») : variantes courantes
   (VS sans spécialisation, PS avec 901 = 0xFFF0) compilées sur le fil uam de plume (deko3d::PrecompileVariant, clé
   FNV-1a du texte comme loadVariant) dans deko3d_shaders.bin.
   **Essai final sur la console (2026-10-27, out/essai-final-console/ ; cache DKSH vidé avant)** :
   - installation (opdarkness_045) : recherche 13,6 s, traduction 19,9 s, uam 490 s (155 variantes) : ~9 min en tout ;
   - partie suivante (046) : 54 variantes du cache, 8 compilées en jeu (99 ms en moyenne : PS avec 901 = 0xFFFF ou
     0xFFFE, à ajouter si ces à-coups gênent, ~+1 min d'installation) ;
   - contre le paquet du PC compilé dans le .nro (044, même scénario) : mêmes images/s en combat (44-46, le processeur
     limite), GPU 16,5 ms par image contre 14,7 (+12 % : les passes GLSL de banjo-nx, deko3d_glsl_opt.py, et DXC contre
     glslang ; à porter seulement si le GPU limite quelque part, cinématiques par exemple).
   - Ensuite (même jour) : variantes PS 901 = 0xFFFE et 0xFFFF ajoutées (règles générales, VariantesCourantes) ;
     relevé des variantes prises en partie (plume : SetDeko3DVariantNote, patches/plume-diagnostics.patch ;
     od_releve_variantes -> variantes_jeu.txt à côté du .nro, « nom, compilée, constantes ») : sert à trouver de
     nouvelles règles, et l'installation recompile aussi celles du relevé de la console (le relevé nomme des shaders par
     empreinte : jamais dans le .nro).
   - **.nro sans donnée du jeu** : -DOD_RENUT_SANS_DONNEES=ON (cmake/rendu_renut.cmake) : table vide
     (src/od_shader_cache_vide.cpp), pas de paquet GLSL, od_installer = si_absent par
     défaut. Reste dans tout .nro le code du jeu traduit par le générateur (comme torchlight-nx et sa2-nx). Compilé
     dans out/build/switch-renut (reconfiguré avec l'option ; --reconfigurer avec CMAKE_OPTS="-DOD_RENUT=ON" pour
     revenir au paquet intégré) : opdarkness-sans-donnees.nro, ELF opdarkness-20261027-2010-renut-sans-donnees.elf.
   - Collecte PGO pendant les missions de l'utilisateur (2026-10-27 soir) : out/build/switch-renut-pgo-gen (sans
     donnée, LTO, OD_PGO=generate ; 43 min de compilation locale ; ELF
     opdarkness-20261027-2042-renut-sans-donnees-pgo-gen.elf ; commit dans COMMIT_COLLECTE du dossier). **Ne rien
     régénérer (générateur, apres_codegen.py) avant l'analyse** ; ensuite : pgo/od_pgo_*.stream de la carte ->
     out/pgo-flux-renut-<date>, tools/switch/pgo_fusion.sh, tools/pgo_classement.py, compilation OD_PGO=use + LTO,
     variantes_jeu.txt pour les règles de l'installation, rex_profile.log pour le profil du fil de dessin.
4. **Retouches du paquet** à porter en C++ (sinon image et performance différentes du paquet actuel) : flou de
   profondeur allégé (tools/renut/od_flou_profondeur.py), découpe des ombres (od_ombres.py, mode 2), early alpha
   (renut_early_alpha.py + early_alpha.py, 1 400 lignes : texte HLSL réécrit avant glslang), od_vs_nan.py ; passes GLSL
   de banjo-nx (deko3d_glsl_opt.py, 2 100 lignes, sans scale1 ni fragscale) seulement si leur gain se mesure sur la
   console. Vérifier chaque retouche par l'outil PC contre le script (octet pour octet), comme pour la chaîne.
   **Fait le 2026-10-27** : src/od_retouches.cpp (flou mode 1, ombres ; od_installer_flou, od_installer_ombres), faites
   par la chaîne juste après XenosRecomp (installation et shaders traduits en jeu). tools/renut/comparer_retouches.sh :
   HLSL identique aux scripts sur les 176 ; et le HLSL retouché de la console est identique, pour les 176, à celui d'où
   part le paquet du PC (out/build/renut/renut_gpu/early_alpha_hlsl, hors la ligne d'en-tête) : **l'early alpha ne
   retouche aucun shader d'Operation Darkness** (renut_early_alpha.txt : 65 « unsupported », 7 « little »), rien à
   porter. Reste l'écart DXC / glslang et les passes GLSL de banjo-nx (deko3d_glsl_opt.py) : à mesurer sur la console
   (temps GPU du paquet installé contre le paquet intégré).
5. **Shaders compilés par le jeu pendant la partie** (D3DX : ombres, mission 2) : même chaîne sur un fil de fond, au
   premier dessin d'un shader inconnu ; ajout au paquet en mémoire et sur la carte.
   **Fait le 2026-10-27** : reNut (shader_library.h) : SetMissingShaderHandler / HandleMissingShader (capture dans
   shader_capture/ puis le gestionnaire, une fois par empreinte), RuntimeShaderReady (d3d/hooks/shader.cpp : objets
   shaders faits sans fiche notés par empreinte, qui la prennent sur le fil du rendu ; déclaration tirée des vfetch
   et constantes booléennes recalculées ; pipelines en attente lancés) ; deko3d::AddRuntimeGlsl (GLSL en mémoire,
   cherché après les paquets). od_installation.cpp : fil de fond à grande pile (od_shaders_en_jeu, vrai par défaut),
   chaîne jusqu'au GLSL, fiche, GLSL, RuntimeShaderReady. Au lancement suivant (od_installer = si_absent), les
   conteneurs de shader_capture/ absents de la table sont traduits et ajoutés aux deux fichiers (complément : trames
   du paquet recopiées telles quelles). PC : les 5 traduits en 62-152 ms et pris ; complément 166 -> 171 en 0,19 s ;
   171 trames correctes. Le rendu de ces shaders ne se vérifie pas sur PC : le HLSL de reNut hors deko3d demande DXC
   (vk::RawBufferStore), et le SPIR-V de la chaîne (BANJO_DEKO3D) a les liaisons de deko3d, que le Vulkan de reNut
   n'a pas (essai : image à l'envers puis GPU bloqué).
   Console (journal opdarkness_041.log, out/essai-point5-console/) : installation 31,2 s ; en partie, les 5 traduits
   en 0,43 à 2,45 s (4C5CFD54864D90BF le plus long) et pris par leurs objets ; aucune erreur. Complément au lancement
   suivant : d'abord en échec d'écriture (opdarkness_042 : sur la carte de la Switch, rename ne remplace pas un fichier
   existant ; corrigé : suppression puis renommage), puis réussi (opdarkness_043) : 171 shaders en 0,38 s, paquet de
   171 relu, plus aucun shader manquant ni traduit en partie.
   Vu aussi sur PC : sans aucun shader (table absente), le jeu attend 1 s par image (attente du jeu lui-même, sans doute
   d'un résultat du GPU) ; pas vu sur la console avec 5 shaders manquants moins d'une seconde.
6. Ensuite : goulots du processeur d'après une session console (fil vmsDrawThread_1), PGO console du rendu reNut.

Sur la carte (2026-10-23) : operation-darkness-nx-essai.nro (essai rpmalloc) et essai_deko3d/conteneurs/ (176 .bin)
restent ; opdarkness.toml remis comme avant (sans od_essai_deko3d). Les autres .nro (renut = PGO instrumenté du 22,
renut-pgo = 176 shaders, NVK) n'ont pas été touchés.

## Processeur : le fil de dessin du jeu (2026-10-22)
- Une fois le GPU soulagé (découpe des ombres, mode 2), la console plafonne à 36-54 images/s en combat : le fil de dessin
  du jeu (vmsDrawThread_1) à 90-100 % d'un cœur, GPU ~12 ms par image. Profil console : 78 % code du jeu, 8 % SDK,
  6 % reNut ; temps très étalé (aucune fonction au-dessus de 4 %).
- Les fonctions chaudes (sub_8251BB48 hachage de Jenkins des chaînes, sub_82637CE8 marquage « à mettre à jour »,
  sub_826559B0 / sub_8264EF80 / sub_8263B0F8 parcours de scène, sub_82551278) sont déjà traduites presque en natif
  (registres locaux, sans sauvegarde des registres) : les réécrire ne gagnerait rien. Seules exceptions : sub_8261CCA8
  (test de boîte englobante, vectoriel, ~2 % : passe par la pile ; gain ~1 %, non fait) et sub_82609B48.
- Fait : od_comptes (src/od_comptes.cpp, comptes de références du moteur sub_8232CEC0 / sub_8232CF90 en natif,
  ~250 000 appels par seconde ; vérifié, 3 écarts sur 46 millions = course attendue) ; clock_no_scaling = true dans
  switch/defauts.toml (UpdateGuestClock prenait un mutex à chaque lecture de l'horloge : mutexTryLock / mutexUnlock du
  profil console). Gains à mesurer sur la console : le PC ne départage pas (fil de dessin à 16 % d'un cœur, ±5 %).
- Sur PC seulement : la lecture de l'horloge du jeu (QueryPerformanceCounter, sub_82398A00, appels système) fait ~15 % du
  fil de dessin ; ~21 lectures de /proc/self/maps par image (Protect avec ancienne protection, write watches).
- PGO : cycles dans Ryujinx (3 instances : scénario d'Edward, rejeu, scénario de base ; tools : ~/essais-ryujinx/
  pgo_ryu.sh et cycle_pgo.sh sur ryzen) : .nro LTO + PGO (profil de l'émulateur : menus, introduction, tutoriel). Le
  profil d'une vraie partie (collecte sur la console) reste à faire. Classement : out/pgo-classement-ryujinx2.txt.

## Triangles étirés et « star glare » : index de redémarrage des bandes (2026-10-22)
- **Défauts distincts de la brume** (l'utilisateur, 2026-10-22) : triangles étirés, textures qui débordent et étoile
  au-dessus d'Edward (console seulement, deko3d comme NVK) ne sont pas la nappe de brume qui manque (reproduite sur PC).
  Le regroupement du 21 au soir n'était qu'une hypothèse.
- **Cause probable trouvée** : le jeu dessine ses bandes de triangles indexées (prim 6) avec redémarrage, index 0xFFFF.
  reNut le prenait pour un sommet : plage de 65 536 sommets, index de l'hôte passés en 32 bits (max_index > 0xFFFE),
  où 0xFFFF ne redémarre rien, donc le sommet 65535 lu au-delà du flux copié. Sur PC (RADV, accès robustes ou bornes
  du tampon de l'AMD) ces lectures rendent zéro : triangle invisible. Sur la Switch (NVK sans robustesse,
  banjo_gpu_robustness_off ; deko3d sans contrôle) elles lisent la mémoire qui suit : sommets quelconques, traînées
  depuis un point. plume deko3d n'activait de plus jamais le redémarrage (dkCmdBufSetPrimitiveRestart).
- Relevé od_renut_diag_hors_limites = 1 (flux plus courts que la plage lue) : ~600 000 en 1 min de scénario sur PC
  avant (VS 9C36E53D99BD88A0, A75DAE485A8C8B7B... , « lus 65536 »), **0 après**.
- Correction (commit 7ec7b98) : render/draw.cpp RestartIndex / IsRestart / WriteHostIndices (0xFFFF, ou 0xFFFFFFFF /
  0x00FFFFFF en 32 bits, hors de la plage, réécrit en valeur de redémarrage de l'hôte ; bandes seulement, la règle de
  Vulkan dans plume) ; plume deko3d : redémarrage réglé avant chaque dessin indexé (patches/plume-diagnostics.patch).
  PC : image inchangée. Ryujinx : démarrage et combat à 60 images/s. **À vérifier sur la console** (.nro du
  2026-10-22 8 h 25, ELF out/switch-elf/opdarkness-20261022-0825-renut-deko3d-restart.elf du dépôt principal).
- od_renut_diag_hors_limites = 2 : complète de zéros les flux raccourcis (ce que fait le PC) ; pour un reste éventuel.

## Brume qui clignote : en-têtes de surface lus une image trop tard (2026-10-22, corrigé)
- Observation de l'utilisateur (console, .nro du 22 à 8 h 25 : étoile et triangles étirés disparus) : clignotement
  stable selon l'angle de caméra, rémanence du palmier quand on tourne pendant le clignotement. Donc pas une course
  de minutage pure : un chemin du jeu mal suivi.
- Relevé image par image (od_renut_diag_trace_images + od_renut_diag_trace_ps, analyse ~/essais-ryujinx/trames.py sur
  ryzen) : la nappe de brume se dessine dans une cible de 320x180 (objet surface 412139C0, format 20), copiée vers la
  texture 40049B70 que la scène lit. Sur certaines images, le jeu relie le même objet au format 34 pour la passe
  suivante. Les captures sombres suivaient les images où la copie partait de « 412139C0 (320x180 fmt 34) ».
- Cause : notre KeepSurfaceHeader du 2026-10-20 écrivait l'en-tête gardé aussitôt (fil du jeu), lu une image plus
  tard par le fil du rendu, qui prenait donc l'en-tête du format 34 pour les dessins et la copie de la brume.
- Correction (commit bede311) : l'en-tête gardé passe par la file du rendu (render/surfaces.cpp,
  od_renut_entetes_ordonnes = true ; Begin*Vertices en attente vidés avant). PC : 0 image sombre sur 86, luminosité
  régulière 91-95 comme le rendu natif (avant : 7 et 1 sombres, et des images trop claires à 102-107). Ryujinx :
  61 captures sans tache. **À vérifier sur la console.**
- Leçon : tout état que le fil du jeu garde pour le fil du rendu doit passer par la file (ordre des commandes), jamais
  par une table écrite aussitôt.

## Brume qui manque sur certaines images (2026-10-21 soir) : recherche d'avant la cause
- **Scène reproductible** (machine distante, Ryujinx) : scénario rapide prolongé, ~/essais-ryujinx/scenario_edward.txt
  sur ryzen (Edward se déplace, puis vue dans son dos avec le menu d'attaque, à partir de ~172 s ; sur PC sans
  émulateur : décaler de -60 s). Scripts : ~/essais-ryujinx/{fumee,long,rd_lance,rd_pc,valid}.sh, rd_trigger.py
  (captures RenderDoc par target control dans qrenderdoc --python), rd_images.py (image finale de chaque capture) ;
  bilan automatique (images sombres : luminosité de la scène < 70 ; losanges : sombre et > 300 pixels clairs) dans le
  scratchpad de la session (à refaire si besoin : 20 lignes de PIL).
- **Dessin en cause** : la nappe de brume, 2 rectangles plein écran par image (PS 95A8928D32DF24B6 qui recopie la
  couleur des sommets, VS 4DBF2FC22097F20C, liste de quads UP de 4 sommets, pas 24, couleurs 4DE6F2xx et 0AFAFFxx,
  mélange alpha 07060706, ni test de profondeur ni stencil, cible écran 1280x720). Le sauter supprime les losanges.
- **Symptôme réel** : la brume manque sur certaines images (image sombre = clignotement) ou ne couvre que des zones en
  forme de particules et feuillage (losanges, cas extrême). **reNut sur PC (Vulkan, RX 9070 XT) le fait aussi** :
  9 images sombres sur 181 contre 1 (transition) pour le rendu natif dans le même scénario. Ce n'est donc pas le GPU de
  la Switch : un défaut de reNut, plus fréquent sur Maxwell / dans l'émulateur.
- **Vérifié correct côté processeur** : sommets UP (2 variantes seulement), cible, mélange, profondeur et stencil,
  constantes poussées (banjo_deko3d_push_constants_verify : 0 écart), état reconstruit
  (sa2_renut_snapshot_deltas_verify : 0 écart sur 377 000 dessins), et les 2 dessins de brume sont envoyés au GPU à
  chaque image (relevé « [diag] envoi », aucun ignoré par plume).
- **Écarté** (essais isolés sur la scène reproductible) : sauter les 3 autres shaders de fumée, blend_skip,
  output_trim, chain_reuse, cpu_memos, deferred_chain, snapshot_deltas, push_constants, kick_submit, optimisations de
  profondeur deko3d, barrière complète avant les dessins de brume (essais longs de 600 images : 54/66 et 45/61 images
  sombres, losanges inchangés), découpage par la profondeur coupé (250/268), positions NaN, cache de l'anneau d'envoi,
  NEON, PMULL.
- **Indice fort** : sous RenderDoc le défaut disparaît (42 images Ryujinx, 60 images PC consécutives : aucune sombre),
  et avec la couche de validation Vulkan aussi (61 images, aucune sombre ; validation de synchronisation : seulement
  8 alertes WRITE_AFTER_READ sur l'acquisition de la swapchain, out/diag-brume/validation-sync-20261021.txt). Ce qui
  ralentit ou sérialise le rendu le fait disparaître : course entre le fil du jeu et le fil du rendu de reNut (ou
  entre reNut et le GPU) plutôt qu'une barrière Vulkan manquante.
- **Pistes suivantes** : sur PC (plus simple) : (1) lockstep strict (od_renut_images_avance = 0 testé seulement dans
  l'émulateur, non concluant à l'époque) ; (2) relever, côté rendu, l'état de la brume au moment de l'envoi sur une
  image sombre (journal par image, puis repérer les images sombres par captures od_ecrans au numéro d'image exact,
  od_pause_images) ; (3) comparer ce que le fil du jeu écrit (constantes de la brume, D3D SetVertexShaderConstant)
  entre images claires et sombres ; (4) regarder si la brume est dessinée deux fois ou dans le mauvais ordre
  (avant/après la composition) sur les images sombres.
- Options de diagnostic ajoutées (draw.cpp, pipeline.cpp) : od_renut_diag_sauter_ps / _bits / _prim / _des,
  od_renut_diag_points_max, od_renut_diag_trace_ps (sommets UP, mélange/profondeur/stencil, cible, envois),
  od_renut_diag_barriere_suivi, od_renut_diag_barriere_totale (fait planter, tampon de commandes), 
  od_renut_diag_sans_decoupe_z, od_renut_images_avance (render_thread.cpp). Plume modifié : patches/plume-diagnostics.patch
  (appliqué par tools/import_renut_sa2.sh).

## Optimisations reprises de renut-nx (2026-11-01, demande de l'utilisateur : points 2, 4 et 5 du bilan)
Bilan de la documentation de renut-nx (ex banjo-nx, github.com/ChanseyIsTheBest/renut-nx, docs/ à jour du 2026-11-01) :
notre reNut (sa2-nx adf3a4e du 20/10) a déjà les 19 allègements de sa2-nx ; le chemin « record-time » de Banjo (26-28/10)
ne vise que le fil du rendu, pas notre goulot (fil de dessin du jeu, 78 % de code du jeu). Repris :

### 2. Copies localisées en masse (méthode de bejeweled3-nx, outil de renut-nx)
- tools/copies_masse_liste.py (switch_hot_bulk.py de renut-nx) : profils de la console (rex_profile.log + ELF exact du
  .nro) -> src/copies_masse.txt, toute fonction encore en code généré avec >= 8 échantillons (tous les fils), sauf les
  petites feuilles (<= 12 instructions, sans appel), les formes refusées (atomiques, base de temps...) et ce que src/ ou
  le SDK cite (nos interceptions). Profils pris (ELF trouvés par appariement : part des échantillons des fils audio et du
  rendu qui tombent dans des symboles plausibles) : out/switch-logs-renut1 (ELF 20261020-1949-renut-deko3d), renut2
  (2318), renut3 à 7 (2329), switch-logs-renut-flou du dépôt principal (20261021-1424), switch-logs du dépôt principal
  (session du 22 au soir, 20261022-1657-renut-pgo-gen-final). **620 fonctions** (fil de dessin : 73 % de ses
  échantillons en code généré).
- tools/copies_masse.py (localize_native.py de renut-nx, adapté : chemins, od_stvlx / od_stvrx de vmx_ecritures.py notés
  pour la garde, opdarkness_init.cpp, pas de host_counters dans notre PPCContext) -> generated/hot/masse/n_*.h et
  od_hot_bulk_<k>.inc (12 morceaux, compilés par src/masse/od_hot_shard_<k>.cpp). Chaque copie : registres du contexte en
  variables locales, réécrits seulement s'ils ont pu changer (flot de données), fonctions appelées incluses comme copies
  quand tout leur arbre d'appels le permet (« fermeture », jusqu'à 6 000 lignes), synchronisation autour d'un appel réel
  limitée à ce que l'appelé peut lire ou changer.
- tools/apres_codegen.py : les fonctions de la liste comptent comme interceptions (leurs appelants passent par le
  crochet) ; copies faites en dernier, du texte final. Index du code généré en cache : out/copies_masse_index.json.
- Exécution (src/masse/, od_hot_guest.cpp de renut-nx) : od_masse (toutes), od_masse_sub_XXXXXXXX (chacune). Garde : sur
  PC les 64 premiers appels puis 1 sur 4096 exécutent copie et original et comparent registres et mémoire écrite
  (différence répétée 3 fois : la copie s'éteint, « [masse] DIFFÉRENCE » au journal) ; od_masse_verif = chaque appel.
  Switch : od_masse_garde_appels = od_masse_garde_periode = 0, appel direct (un octet testé, puis la copie). Une copie ne
  s'active que si le code du jeu chargé est octet pour octet celui d'où elle vient (empreinte FNV).
- Copies choisies à la main : toujours src/copies_localisees.txt (outil de sa2-nx, generated/hot/n_*.h), exclues de la
  liste.

### 4. Mode quatre cœurs (docs/four-core-mode.md de renut-nx, de NetherSX2_nx, licence MIT)
- SDK (patches/sa2-nx-sdk.patch) : threading_switch.cpp lit la dotation (svcGetInfo InfoType_CoreMask) ; avec 0xF, le
  cœur 3 est gardé aux fils de fond, __wrap_svcSetThreadCoreMask (--wrap au lien, cmake/rexglue_switch.cmake) le retire
  de tout masque de plus d'un cœur (pthread_create de libnx donne sinon le masque entier). Sur le cœur 3 : sortie audio
  (0x2A, relève aussi le repos du cœur : ligne « system core 3: idle N% » de rex_profile.log), Audio Worker (0x2B), les
  fils de mixage du jeu (au premier passage dans le rendez-vous sub_82890C30, src/od_audio.cpp ; priorité 0x2D), le fil
  d'uam (od_coeur_systeme_uam) et celui des shaders compilés en partie (0x3B). Le décodeur XMA y va seulement avec
  od_coeur_systeme_xma (renut-nx : cœur 3 plein et son haché avec lui ; ~25 % d'un cœur chez nous).
- Le .nro écrit lui-même sdmc:/atmosphere/contents/<id>/exefs/main.npdm (CPU 0-3, espace d'adressage 39 bits ;
  tools/forwarder/od_forwarder.json -> npdmtool -> tools/forwarder/npdm_vers_inc.py -> sdk/src/core/forwarder_npdm.inc)
  quand il est lancé par un forwarder Sphaira de ce .nro (identifiant tiré du SHA-256 du chemin du .nro, comme renut-nx
  1.0.0) ou sous od_forwarder_id. Premier lancement : 3 cœurs, fichier écrit ; lancements suivants : 4 cœurs. Lancement
  habituel (hbmenu, title override) : rien ne change. Journal : « Switch : cœurs du processus 0x... ».
- Réglages : od_coeur_systeme (0 = rien, 1 = disposition ci-dessus, 2 = les autres fils permis aussi sur le 3),
  od_coeur_systeme_xma, od_coeur_systeme_mixage_priorite, od_forwarder_id, od_coeur_systeme_essai (disposition
  essayée sur 3 cœurs, pour Ryujinx).

### 5. Cache des trames XMA décodées (renut-nx, sdk/src/audio/xma_context.cpp)
- Un son rejoué depuis son début (mêmes paquets depuis l'ouverture du codec) reprend les échantillons de la première fois
  au lieu du décodage par FFmpeg ; le codec rattrape les trames sautées avant la première que le cache ne connaît pas
  (au plus 256 de suite). Exact (le décodeur ne dépend que des trames depuis son ouverture). od_xma_cache (vrai),
  od_xma_cache_verify (tout décoder et comparer), od_xma_cache_mb (24). Bilan toutes les 10 s : « XMA frame cache ».

### Essais sur PC (2026-11-01, PC chargé par d'autres compilations : aucune mesure de vitesse)
- Scénario rapide jusqu'au combat (reNut, garde PC 64 / 4096) : 583 des 620 copies atteintes, 25 000 appels comparés à
  l'original, **0 différence** ; images correctes. od_xma_cache_verify : 0 écart (~2-3 % des trames reconnues : la
  plupart des sons du jeu sont des flux longs ; gain faible pour nous).
- 1 image/s en combat avec ou sans les copies (od_masse = false) : la charge du PC, pas les copies.
- Reste : .nro (Switch) puis essai sur la console ; mode quatre cœurs seulement avec un forwarder Sphaira (adresse 39
  bits), lancé deux fois (le premier lancement écrit le NPDM).

### Compilations et essais sur ryzen (2026-11-01)
- Synchronisés depuis le PC : l'arbre renut (sauf out/), le SDK (~/workspace/sa2-nx sauf sdk/out), out/renut-tiers
  (notre XenosRecomp corrigé : sans lui, erreurs dans shader_recompiler.cpp), out/deko3d_tools_conteneur et
  _tools/rpmalloc-1.4.5. Scripts Ryujinx : ceux du dépôt principal (tools/distant/).
- .nro ordinaire (sans LTO, sans donnée du jeu) : out/build/switch-renut-masse (12 tâches, quelques minutes). Ryujinx,
  instances ~/ryujinx-instances/masse et masse2 (installation des shaders faite, 22 s dans l'émulateur) :
  - A (garde des copies sur ARM : od_masse_garde_appels = 64, od_masse_garde_periode = 4096, od_xma_cache_verify) :
    combat atteint, 583 copies, 115 000 appels comparés, **0 différence** ; XMA 0 écart.
  - B (od_coeur_systeme_essai : le cœur 2 tient lieu de cœur 3) : « cœur du système 2 ; fils du jeu sur 0x3 », les deux
    fils de mixage et la traduction en jeu déplacés ; combat atteint ; cache XMA 3,5 % des trames.
  - Ryujinx donne 0x7 à un .nro : le vrai mode quatre cœurs ne s'essaie que sur la console (forwarder Sphaira).
- .nro instrumenté PGO (LTO + generate, sans donnée) : out/build/switch-renut-pgo-gen-masse (13 min) ; essai Ryujinx de
  380 s correct, flux pgo/od_pgo_1.stream écrit. ELF : out/switch-elf/opdarkness-20261101-0832-renut-masse-pgo-gen.elf.
  La fusion et la compilation OD_PGO=use se font sur ryzen (mêmes chemins ; out/pgo de ryzen).

## Fil de dessin, lot du 2026-11-01 (après la partie PGO de 8 min)
Profil console de la partie PGO (out/switch-logs-pgo-20261101, .nro instrumenté, quatre cœurs) : en combat le fil de
dessin du jeu est à 92-107 % d'un cœur, GPU 10-17 ms, fil du rendu 6-9 ms par image : le processeur limite. Profil plat
(rien au-dessus de 3,8 %) ; les fonctions chaudes du jeu (parcours de scène sub_82568148 / 8255FE78 / 8255FBF8 /
82551278, nœud sub_82674900 avec ses compteurs et son chronomètre) sont déjà des copies localisées : les réécrire ne
gagnerait que les appels virtuels. Fait à la place (commit 99b4731), tout vérifié sur PC :
- **Verrous** (SDK, xboxkrnl_rtl.cpp) : `__atomic` séquentiels au lieu des `__sync` (un `dmb ish` complet après chaque
  opération sur ARM64, la plus grosse part de RtlTryEnter / RtlLeave, ~3 % du fil). L'ordre libération / dormeurs
  reste garanti (sur ARMv8 une écriture-libération précède toujours une lecture-acquisition suivante).
- **Horloge** (SDK, clock.cpp, `clock_no_scaling`) : multiplication en virgule fixe (fraction arrondie par excès)
  au lieu d'une division 64 bits ; exacte (60 millions de cas comparés).
- **sub_8261CCA8** (test de visibilité, 1,2 %) : tools/pile_locale.py fait une version par mots (pile en mots natifs,
  vecteurs assemblés par voies, sans inversion d'octets ; version par octets si r1 n'est pas aligné sur 16) ;
  8,4 millions d'appels comparés à l'original (od_copies_verif) : 0 écart.
- **reNut, chaîne de deltas** (~7 % du fil avec le dessin) : copie des blocs changés depuis les registres de la
  comparaison (plus de memmove) ; d'après renut-nx (setters' marks) : les fonctions de réglage du Device n'écrivent que
  les valeurs qui changent et marquent leur bloc ; les tags m_Pending 2-4 des écritures du jeu désignent leurs blocs
  (constante de lecture n : ses 24 octets, son mot de texture, les flux ; autre tag : état du rendu) ; 0x3080 et
  au-delà toujours comparés (écrits sans tag ni marque) ; un contrôle (2000 premiers deltas, puis 1 sur
  od_renut_marques_controle = 32) compare tout et apprend un bloc changé sans marque. PC jusqu'au combat : 22,7 blocs
  comparés par dessin au lieu de 39, 0 bloc changé sans marque, 0 différence du device reconstruit
  (sa2_renut_snapshot_deltas_verify, ~4,5 millions de dessins). od_renut_marques = false : comme avant.
- **Boost des chargements** (src/od_boost_chargement.cpp) : les chargements se font derrière un écran animé à
  60 images/s (2D\BG\LOADING_*.ptc) ; détectés au fil de lecture du moteur (sub_82330990) occupé 60 % au moins de
  3 fenêtres de 100 ms ; fin après 5 fenêtres sous 10 % ; boost FastLoad (GPU au plus bas) au plus 60 s ; ligne
  « [chargement] » par chargement (durée, occupation, Mo, images/s), avec ou sans boost (od_boost_chargement).
  À mesurer sur la console (comparer od_boost_chargement = false / true).
Ensuite : .nro instrumenté (nouvelle partie PGO), puis .nro LTO + PGO **compilé depuis zéro** (ninja ne voit pas les
.gcda : un nouveau dossier ou --clean-first, leçon de renut-nx).

## Bâtiments de la mission 2 troués : test alpha « égal à 1 » (2026-11-03, corrigé)
Défaut : sur la console, murs de pierre de la mission 2 en stries et transparents ; pleins sur la vidéo Xbox 360, sur PC
et dans Ryujinx (carte AMD). Les options de profondeur (zcull, depth_handover, depth_words_view) n'y changent rien.
- **Shader** : dessins sautés dans Ryujinx (od_renut_diag_sauter_ps) : PS 4000C0C5A0EA1B39, VS 9C36E53D99BD88A0.
- **Cause, mesurée sur la console** : ces dessins ont le test alpha « égal » à 1 (fonction 2, seuil 1, RB_COLORCONTROL
  8700000A). L'alpha du shader est le produit de l'alpha de la texture et de celui de la couleur du sommet. Une version
  du shader découpée en bandes verticales montre, sur le même mur : alpha à 1 exactement ou un peu en dessous, au hasard
  d'un pixel à l'autre, et le verdict du test qui échoue par endroits ; sans ce discard, ou avec l'alpha arrondi à
  1/255 avant le test, le mur est plein. Pourquoi Ryujinx et le PC y échappent n'a pas été mesuré (GPU différent, sans doute).
- **Correction** : od_renut_test_alpha_tolerant (render/draw.cpp, vrai par défaut) : « égal » et « différent » à 0 ou à 1
  deviennent des comparaisons à un demi-pas de 8 bits (= 1 : >= 1 - 0,5/255). Seuls la fonction et le seuil envoyés au
  dessin changent : pas de nouvelle traduction des shaders. Les autres seuils ne sont pas touchés.
- **Vérifié** : par l'utilisateur sur la console, cinématique et combat de la mission 2 (.nro LTO + PGO du 2026-11-03,
  00 h 38), bâtiments pleins. Pas essayé sur PC ni dans Ryujinx.
- Méthode à réutiliser : retoucher en mémoire le GLSL d'un seul shader au chargement, en bandes verticales qui comparent
  plusieurs variantes dans la même image, sans réinstallation (code retiré ensuite). od_renut_diag_trace_ps donne
  désormais aussi la fonction et le seuil du test alpha.

## Définition en combat et cœur réservé au fil de dessin (2026-11-03)

Même .nro LTO + PGO pour A, B et C. Journaux dans out/ (005 et 006 : .nro précédent, sans ces deux options). Images/s par
tranche de dessins par image, nombre d'intervalles de 10 s entre parenthèses ; deux parties du même .nro diffèrent d'environ
8 % à dessins égaux, et les petits effectifs comptent peu.

| Dessins | 005 (ni l'un ni l'autre) | 006 (idem) | A : régulateur | B : régulateur + cœur 0 | C : cœur 0 seul |
|---|---|---|---|---|---|
| 250-330 | 59,2 (3) | 55,8 (5) | 54,4 (5) | 56,8 (11) | 56,7 (15) |
| 330-390 | 58,7 (5) | 56,0 (12) | 51,9 (8) | 52,7 (11) | 53,2 (7) |
| 390-450 | 55,8 (15) | 51,8 (13) | 48,8 (10) | 47,8 (2) | 52,0 (5) |
| 450-600 | 52,6 (66) | 48,2 (18) | 50,3 (2) | 50,2 (3) | 48,9 (8) |

Journaux : 005 = switch-logs-partie-20261103, 006 = switch-logs-ombres-20261103, A = switch-logs-regulateur-20261103 (6 min),
B = switch-logs-coeur-20261103 (8 min, partie perdue), C = switch-logs-sans-regulateur-20261103 (9,5 min). Aucun FATAL ;
od_renut_marques à 0 écart dans A, B et C.

- **Régulateur, gardé** (od_render_descente_ms = 21, od_render_montee_ms = 18, switch/defauts.toml) : le combat reste en 540p
  (GPU 15-16 ms par image contre 10-14 ms), quelques vues lourdes passant en 480p ; environ 3 à 5 images/s de moins sous
  450 dessins. Choix de l'utilisateur après avoir joué les deux : image moins floue, jeu pas moins fluide. Changements de
  définition : 14 en 6 min (A), 16 en 8 min (B), 24 en 9,5 min (C).
- **Cœur réservé, abandonné** (od_coeur_dessin, -1 par défaut ; SDK, core/threading_switch.cpp, XThread::set_name) : le fil de
  dessin était bien fixé seul sur le cœur 0 (masque 0x1, les autres fils du jeu sur 0x6 ; trois petits fils du SDK restent en
  0x7). C ne fait pas mieux que 005 et 006 : 17,9 ms de fil par image à 476 dessins. Le cœur partagé ne freinait donc pas ce fil.
- Suite possible pour le processeur : le fil de dessin reste limitant au-delà de 400 dessins, son profil est plat.

## Paralléliser le fil de dessin ? Étude du 2026-11-03 (lecture du code et profil, rien d'essayé)

Profil : parties B et C, ~17,9 ms de fil par image à 476 dessins. Répartition des piles en marche, **approximative** (piles
limitées à ~10 cadres, classement par une liste de fonctions repères) :
envoi des dessins 41 % (~7,3 ms : sub_82674900, 8266CB50, 82568148, 82551278, 8255FBF8...), non classé 27 % (~4,9 ms),
parcours de scène 20 % (~3,5 ms : 8264AD80, 8264ACB8, 826559B0, 8264EF80, 8263B0F8, 82637CE8), capture reNut 12 % (~2,2 ms :
QueueDraw, CaptureDelta). Il faudrait gagner au moins 3,5 ms à cette charge pour 60 images/s, davantage à cause de la
variation entre images.

- **Cadence écartée** : PaceSwap ne fait pas perdre de temps. 1-2 ms par image en moyenne, sur les images en avance ; une
  image en retard part aussitôt (journal « game thread », 49-71 % d'images en retard au-delà de 450 dessins).
- **Moteur** : trois fils seulement (sub_82152428 : vmsSystemThread, vmsObjectThread, vmsDrawThread_1), avec des files de tâches
  sous verrou (sub_8232E388). Le dessin est une tâche unique (sub_8226A5E8 -> sub_822DF6F8 -> sub_822EEE10) : le moteur
  n'offre pas de découpage prêt à l'emploi.
- **Parcours / envoi sur deux fils** : le plus gros gain en théorie, écarté pour le risque. Pas de verrou dans le parcours
  lu : listes remplies et drapeaux de nœuds posés sans atomique (sub_82637CE8, sub_826559B0), compteur global non atomique
  (sub_8264ACB8, 829B5718). L'envoi passe par des appels virtuels nombreux (sub_8266CB50 : 235 fonctions et 186 appels
  indirects en statique). Sans le source, on ne peut pas prouver l'absence de partage ; une erreur donnerait des plantages au
  hasard, et le rejeu ne suffit pas pour vérifier.
- **Parcours en plusieurs fils** : mêmes obstacles, en plus de l'ordre des listes à conserver ; gain borné à ~1,7 ms sur 2 fils.
- **Capture reNut vers le fil du rendu** : seule piste sûre, c'est notre code. La copie doit rester synchrone (le jeu réécrit le
  device juste après), mais la comparaison et une partie de la file peuvent partir. Estimation non mesurée : 0,5 à 1 ms.
  Gain sous le bruit de 8 % entre parties : à juger aux ms de fil par image, pas aux images/s.
- Outils de cette étude : scripts jetables de la session, non gardés (arbre d'appels par dominateurs, statique + piles).

**Suite du 2026-11-03 : prototype `od_fil_maj` (src/od_fil_maj.cpp, coupé par défaut).** Les avis ci-dessus sur le découpage et la
capture sont remplacés par ce qui suit (capture : aucun gain facile, comparer coûte autant que copier ; ~1 % sur les verrous).

- **Structure réelle de l'image** (échantillonneur en LD_PRELOAD, piles complètes, PC) : sub_8232E198 enchaîne les tâches ;
  sub_822DF6F8 parcourt ses éléments sous le grand verrou global (sub_8232C5B0, objet 0x8299C70C) et appelle sub_822EEE10
  pour chaque scène (5 en combat). sub_822EEE10 = tête (mise à jour : sub_82234B78, 822BE7A8, 822FF618, 822EFB98...) puis
  queue (sub_822EDB40, 82223080, envoi sub_822EE928, 822EDB40). Parcours et envoi alternent donc dans l'image ; suivent le 2D
  (sub_823286D8, avec clears et résolutions) et Swap. Le fil des objets attend beaucoup ce grand verrou.
- **Relevé du partage** (copie instrumentée hors dépôt, tous les accès mémoire du code généré, natifs coupés ; combat du
  tutoriel de la mission 1, ~285-390 dessins, 3 000 à 5 800 images, PC) : tête d'une scène contre queues précédentes :
  42 paires de fonctions, 9 présentes à chaque image, ~25 mots : accesseurs du contexte d'affichage (sub_825280B0/C0,
  826107B0, sommet de pile de matrices via 82539838 et 82609B48), plus 829E77B0 (fausse alerte : écrit par la tête elle-même).
  Valeurs lues inchangées sauf 4 mots de matrice (sans doute sa diagonale). Le reste apparaît sur une seule image
  (825D42E8, 8258DC48, 8256F928...). La tête n'appelle aucune fonction de reNut ni Direct3D.
- **Têtes toutes avant les queues, même fil** : 0 FATAL, image correcte à l'œil en cinématique, menu et combat. Le rejeu n'est
  pas assez déterministe pour comparer au pixel (l'image 12700 du tutoriel est identique, les autres dérivent). Vitesse
  équivalente (395 contre 410 images pour 10 s ; un essai à 36 contre 50 images/s n'a pas été reproduit).
- **Prototype** : un fil d'aide (XHostThread) exécute les têtes en chaîne ; le fil de dessin garde les queues et n'attend que
  la tête de sa scène. Grand verrou considéré comme acquis pour le fil d'aide (reconnu à r13). Crochets écrits à la main sur
  sub_822EEE10, 8232C5B0 et 8232C720 ; leurs copies restent dans leurs morceaux (tools/copies_masse.py) pour ne pas déplacer le
  profil PGO. Après tools/apres_codegen.py, compilé et essayé sur ryzen (tutoriel) : PC 0 FATAL ; Ryujinx (LTO + PGO) 0 FATAL,
  5 scènes par chaîne. Sur ryzen, le PC rend par llvmpipe : **aucune mesure de performance valable, à faire sur la console**.
  .nro : out/reference/opdarkness-20261103-fil-maj-lto-pgo.nro (ELF out/switch-elf/opdarkness-20261103-1456-renut-fil-maj-lto-pgo.elf).
- **Non levé** : relevé limité au tutoriel de la mission 1 ; aucune trace du fil d'aide en mission 2 jusqu'à 153 dessins par
  image (sans doute la cinématique, à confirmer) ; lectures possibles d'une matrice en cours d'écriture (pas de copie
  pour le fil d'aide) ; données partagées sur de rares images (création de shaders ?) sans protection ; TLS du fil d'aide.
  L'essai d'endurance PC de 15 min a échoué pour une raison sans rapport (perte de l'écran virtuel) : à refaire.

### Clignotement des personnages avec od_fil_maj (2026-11-03, cause localisée, correction à valider sur la console)

- **Essai console (parallèle complet, opdarkness_011.log)** : 0 FATAL en 3,7 min, 56,1 images/s à 250-330 dessins, fil de dessin
  ~11 ms par image + ~2,4 ms d'attente, fil d'aide ~5 ms. **Défaut** vu par l'utilisateur sur la grille en vue plongeante :
  personnages qui disparaissent ou changent d'aspect d'une image à l'autre (pas en caméra rase).
- **Reproduit sur PC** (ryzen, llvmpipe) : `~/ess/grille3.sh`, stick droit vers le haut sur la grille (`stickd_*`, ajouté à
  od_manette), vue plongeante. L'évaluation est faite à l'œil, sur des montages de 12 images, pour un seul écran.
- **Modes de diagnostic (od_fil_maj_mode)** :

| Mode | Déroulement | Images correctes / 12 |
|---|---|---|
| sans fil d'aide | référence | 12 |
| 0 | têtes en parallèle de tout | ~8 (soldat absent) |
| 1 | tête N après la queue N-1 : parallèle aux seuls éléments intercalés | ~5 (soldat et Edward absents) |
| 2 | toutes les têtes avant la première queue | 12, deux passages |
| 3 | tête N quand le fil de dessin arrive à N (ordre d'origine exact, fil d'aide) | 12 |
| 4 | parallèle aux queues, attente de la fin des têtes avant un élément intercalé | 12, un passage (défaut retenu) |

- **Lecture** : ni le fil d'aide ni l'ordre ne sont en cause (modes 2 et 3). Le mode 1, sans chevauchement avec aucune queue, a
  le défaut : c'est le **chevauchement avec les éléments intercalés** de la liste qui le donne. Liste en combat : scène, scène,
  822806B8, 82270418 ×4, 822A70E8, scène, 822A70E8, scène, scène. L'hypothèse précédente (matrice lue pendant les queues, à
  corriger par une copie stable) n'explique pas le mode 1. **Le mot exact en conflit n'est pas identifié** : sans doute un état
  modifié temporairement par 822806B8 (appelle 826094B0, 8260A280, 822EDAD0..., 826107B0), non prouvé.
- **Répartition sur PC** (ms par chaîne, grille, ordres de grandeur à ne pas transposer à la console) : scène 1 : tête ~0,4, queue
  ~0,3 ; scène 2 : tête ~0,2, queue ~0,3 ; scènes 0, 3, 4 : quelques centièmes ; éléments intercalés : moins de 0,05. En pratique,
  le parallélisme utile est la tête de la scène 2 pendant la queue de la scène 1 ; la tête de la scène 1 reste attendue.
- **Non levé** : mode 4 vérifié sur un seul écran ; gain à mesurer sur la console (il peut être inférieur aux ~1,5 ms du mode 0).
  Le mode 4 garde le chevauchement tête 2 / queue 1, avec ses 4 mots de matrice relevés : un défaut plus rare reste possible.
- **Blocage d'essais sur ryzen** : deux essais figés plus d'une heure au démarrage (`No DRI3 support detected`, `Command queue does
  not support present` ; le jeu reste bloqué et ignore SIGTERM). Cause de l'absence de DRI3 inconnue. `grille3.sh` s'arrête seul.
- **Endurance du mode 4 (2026-11-03, ryzen, llvmpipe)** : ~12 min, 42 546 images, 0 FATAL, aucune attente de plus de 3 s, 72 captures
  sans personnage manquant à l'œil (caméra alternant vue plongeante et vue rase, en vignettes pour 60 d'entre elles) : un seul écran,
  grille de la mission 1. Ryujinx (LTO + PGO, 240 s, scénario de base) : 0 FATAL, 5 scènes par chaîne, fil d'aide ~17 %, attente ~9,6 %.
  .nro : out/reference/opdarkness-20261103-fil-maj4-lto-pgo.nro (ELF out/switch-elf/opdarkness-20261103-1709-renut-fil-maj4-lto-pgo.elf),
  od_fil_maj coupé par défaut, mode 4 par défaut. **Reste : essai console** (défaut en vue plongeante, gain, au-delà de 330 dessins).
- **Partie A sur la console, mode 4 (2026-11-03, 17 h 28-17 h 45, out/switch-logs-fil-maj4-20261103/opdarkness_012.log)** : 16,7 min, 0 FATAL,
  aucune attente de plus de 3 s. Par tranche de dessins (250-330 / 330-400 / 400-450 / 450-700) : 57,1 / 55,7 / 53,7 / 50,3 images/s, travail
  13,38 / 14,95 / 16,45 / 18,48 ms. Parties du matin sans fil d'aide, même régulateur : 54,4-56,8 / 52,0-53,5 / 47,9-51,3 / 48,9-50,3.
  **Comparaison faussée** : 42 % du temps en 360p (421 s) contre presque rien dans 008 et 009 ; 010 est à 480p. Une partie B est nécessaire.
  Par chaîne : scène 1 tête ~2,9 ms, attente ~2,55 ms, queue ~3 ms ; scène 2 tête ~1,7 ms, attente ~0, queue ~2,2 ms : le fil de dessin
  économise la tête de la scène 2 et des petites scènes, soit ~2 ms au mieux, et attend presque toute la tête de la scène 1.
- **Retour de l'utilisateur sur la partie A** : plus de clignotement vu, mais un tremblement de caméra et des animations de personnages
  touchés qui lui semblent nouveaux. Non expliqué : effet normal du jeu jamais remarqué, ou défaut du chevauchement tête 2 / queue 1 encore
  présent en mode 4. À trancher par la partie B (option coupée) sur des situations comparables.
- **Partie B, même .nro, second fil coupé (2026-11-03, 18 h 56-19 h 05, out/switch-logs-partie-b-20261103/opdarkness_013.log)** : 8,2 min (partie
  perdue), 0 FATAL. Le joueur attribue le tremblement au placement de la caméra du jeu : il l'a revu sans le second fil. Toutes définitions :
  55,7 / 53,6 / 48,2 / 46,6 images/s, travail 14,53 / 16,30 / 18,91 / 19,94 ms (A : 13,38 / 14,95 / 16,45 / 18,48).
- **À 540p seulement (images/s, nombre d'intervalles de 10 s entre parenthèses)**, tranches 250-330 / 330-400 / 400-450 / 450-700 :
  A 57,0 (18) / 54,3 (7) / 53,4 (4) / 51,0 (2) ; B 55,7 (12) / 54,0 (8) / 47,0 (2) / 46,6 (1) ; 008 54,4 (5) / 52,0 (8) / 48,8 (10) / 50,3 (2) ;
  009 56,8 (11) / 52,7 (11) / 47,8 (2) / 55,0 (1). **Bilan** : +0,2 à +2,6 images/s jusqu'à 400 dessins (dans le bruit) ; +4,6 à +6,4 à 400-450 sur très peu
  d'intervalles. Gain plausible et limité, non démontré. Le fil de dessin étant souvent en avance sous 400 dessins (19-30 % d'images en retard), un
  gain important n'y est pas attendu. A a descendu bien plus souvent en 360p (60 descentes contre 18, pour une partie deux fois plus longue) :
  cause non établie, B est trop courte pour comparer. Reste à gagner : ~2,5 ms d'attente de la tête de la scène 1, à condition de la lancer plus tôt.

## Grandes pages, profil PGO et allocateur (2026-11-05)

Plan suivi : docs/PLAN_GRANDES_PAGES.md du dépôt principal ; découvertes de torchlight (~/workspace/torchlight,
switch/DECOUVERTES_2026-11-04.md et switch/RENDU_NATIF.md).

- **Fenêtre du jeu alignée sur 2 Mo** (SDK, core/guest_memory_switch.cpp, `RexGmInit`) : le second paramètre de
  `virtmemFindAslr` est une garde, pas un alignement ; jamais aligné jusque-là, donc toute la mémoire du jeu en pages de 4 Ko.
  Partie A sur la console (opdarkness_015, out/switch-logs-fenetre-a-20261105) : base alignée, zones du jeu de +60/+76 ns par
  défaut de TLB à ~0 (src/od_tlb.cpp, option `OD_MESURE_TLB`, lignes `[tlb]`). Images/s contre 012 (même mission, autre
  session) : +5 à +13 %, aucun temps en 360p. Gardé sans partie B (décision de l'utilisateur).
- **Code du .nro en blocs de 2 Mo** (src/od_code_blocs.cpp, `OD_CODE_BLOCS`, ON, -fno-lto ; repris de torchlight) : reprojection
  à la même adresse dans un constructeur de priorité 101. Console : `[code] … fait`, code de +57 à -10 ns en lecture et de +114
  à +15 ns en exécution. Sans chargeur (Ryujinx) : rien à faire.
- **Profil PGO : 173 des 619 copies en masse sans profil** depuis le prototype du second fil (tools/switch/audit_profil.py, adapté
  de torchlight : le « 0 coverage mismatch » de la compilation ne prouve rien, avertissements masqués dans les en-têtes
  système). Les copies sont réparties entre 12 morceaux par taille ; des tailles changées en avaient déplacé 173, et le
  profil est rangé par objet. Corrigé par src/masse/morceaux_pgo.txt (répartition de la collecte du 2 novembre), lu par
  tools/copies_masse.py (`--figer` après une nouvelle collecte). Audit ensuite : 147 fichiers, 0 fonction sans profil.
  Piège de l'audit : GCC nomme le .gcda d'après le dossier courant et un `-o` relatif ; un objet dans /dev/null ou un `-o`
  absolu ne trouve aucun profil.
- **Recherche rapide de `BaseHeap::AllocRange`** (SDK, system/xmemory.cpp et .h, reprise de torchlight-nx 1.0.5 ;
  `heap_recherche_rapide`, `heap_recherche_verif`) : groupes de 64 pages pleins sautés d'un coup, même résultat (Ryujinx :
  4 096 recherches comparées, 0 différente). Console (opdarkness_017, out/switch-logs-allocrange-20261106) : 0,344 -> 0,042 ms
  par allocation physique, 5,0 -> 0,5 s sur une partie, pire médian par bilan de 10 s 1,31 -> 0,27 ms.
- **Second fil (od_fil_maj), alternance dans la partie** (`banjo_renut_ab_target = 19`, décidée à l'entrée de chaque chaîne de
  scènes ; out/outils-essais/ab.py) : -0,50 ± 0,17 ms par image (+1,5 image/s), -0,6 à -1,3 ms dans les scènes chargées ;
  0 FATAL, rien vu par l'utilisateur. Gardé (od_fil_maj = true dans le toml du joueur), toujours coupé par défaut dans le code.
- **D'une session à l'autre** (017 contre 015 : mêmes fréquences, GPU et dessins comparables) : pas plus rapide, dans l'écart
  habituel de ~8 % ; le code en blocs et le profil retrouvé (2 à 4 % attendus) ne s'y voient pas.
- **Repris de torchlight-nx 1.0.5, écartés ici** : fil d'écriture du journal (notre SDK l'a déjà), avertissement de Mesa (NVK),
  cache de shaders (rendu propre à torchlight). Pour plus tard : lecture en avance des fichiers (chargements), vue 0xE0000000
  en blocs (ferait perdre ses blocs à la bande 0xB0000000, 74 Mo, pour 52 Mo gagnés), ordre des fonctions à l'édition des liens.
- **Second fil par défaut (2026-11-06)** : `od_fil_maj` vrai par défaut dans le code (PC et Switch) ; modes de diagnostic 0-3
  (`od_fil_maj_mode`), chronos par rang (ligne « ms par chaîne ») et liste imprimée toutes les 500 chaînes retirés ; restent
  le comportement du mode 4, le bilan de 10 s et l'alternance 19. Code généré local remis à jour (apres_codegen.py : seuls les
  crochets du second fil changent, comme sur ryzen) ; essai PC (scénario rapide, reNut) : combat atteint, 0 FATAL, aucune
  attente de plus de 3 s. Fichier sans profil PGO : rien à recollecter.

## Tête de la scène 1 plus tôt ? Étude du 2026-11-06 (point 2 : écarté)

- **Chronométrage (PC, reNut, scénario rapide en combat ; src/od_chrono_taches.cpp, temporaire, non versionné)** : tout sur le
  fil de dessin. Par image : sub_8226A4E0 ~7,4 ms (appel virtuel sub_82680070 0,24 ms, puis sub_822DF630), sub_8226A5E8 ~11 ms
  (presque entièrement sub_822DF6F8 couche 1 : les scènes), puis ~6,2 ms jusqu'à l'image suivante (2D, dont sub_822DF6F8 couche 2,
  0,15 ms ; Swap). Liste de sub_822DF630 en combat : 3 × 822A7008, 106 × 822CB178 (0,13 ms en tout), 1 × 82267578, 5 × 822EECD8
  (mises à jour des scènes, ~0,3 ms). **82267578 coûte presque tout (4,4 à 7,6 ms sur PC), dans sub_82300CF8.**
- **Relevé du partage** (copie hors dépôt ~/workspace/od-partage2 : macros d'accès du code généré instrumentées, copies natives
  coupées, phases posées par les crochets ; analyse.py ; 3 800 images de combat, 0 FATAL) : la tête de la scène 1 lit à chaque
  image, avec une valeur changée dans l'image, ce qu'écrit l'arbre de sub_82300CF8 (sub_82300558 -> sub_82234B78, 48 mots et plus,
  24 millions d'accès ; sub_82301348, 82301518 -> globales 829F1464..., 825A5D58, 8259F920 ; pile de matrices sub_82609B48), et
  écrit des mots qu'il lit ou écrit (4103A7B0... avec sub_82301F98). **Vraie dépendance : la tête de la scène 1 ne peut pas
  commencer avant la fin de 82267578.** Après lui ne restent que les 5 × 822EECD8 (~0,3 ms) : rien à gagner.
- Pistes restantes, non commencées (même risque que le découpage du parcours, à prouver par ce relevé) : découper sub_82300CF8
  lui-même entre deux fils (s'il traite des objets indépendants), ou partager la tête de la scène 1 entre le fil d'aide et le fil
  de dessin (qui l'attend sans rien faire). Mesurer d'abord la part de sub_82300CF8 sur la console (profil).

### sub_82300CF8 : l'essentiel du coût sur PC était un défaut du SDK sous Linux (2026-11-06)

- **Ce que fait la fonction** : mise à jour d'un système d'éléments par image (appels virtuels 124 / 116 de l'objet autour) :
  file d'objets en attente (sub_82235788, 8263B520, 8259F920 : 0,04 ms), sub_823020A8, sub_823011B8 (0,15 ms), puis
  **sub_82301348** : destruction des éléments de l'image d'avant (liste objet+200), double boucle sources (objet+744) × objets
  (objet+8, flottant +52 au-dessus d'un seuil) : sub_82302268, test sub_822E0C80(…, 8), allocation de 252 octets (sub_8232C070,
  réserve 0x8299C6BC), construction sub_82300558 (ce que lit la tête de la scène 1), jusqu'à 128 éléments (~36 en combat) ;
  sub_82301518 ensuite ; enfin un facteur de détail réglé par une moyenne glissante sur 32 images. Probablement les projections
  d'ombres (ou effets semblables) par paire source × objet.
- **Le coût sur PC** (chronométrage temporaire, gardé dans out/outils-essais/od_chrono_taches-20261106.cpp) : 6,4 ms dans
  sub_82301348, dont 5,8 ms dans les 36 destructions, dont 5,7 ms dans la libération sub_8232C3A8, dont 4,6 ms dans
  VirtualProtect (sub_82398370 -> NtProtectVirtualMemory -> BaseHeap::Protect -> rex::memory::Protect) : sous Linux, l'ancienne
  protection était demandée à l'hôte, qui relit /proc/self/maps (~125 µs par appel). **Corrigé dans le SDK** (patches/
  sa2-nx-sdk.patch) : ancienne protection lue dans la table des pages, comme sur la Switch. PC en combat : 82267578 7,2 -> 1,9 ms
  par image, 39 -> 48 images/s, 0 FATAL.
- **Sur la Switch** ce chemin était déjà rapide (RexGmProtect : demande de lecture-écriture sur des pages non protégées = verrou
  + recherche dans une table) : sub_82300CF8 n'y est pas le goulot, ce qui concorde avec le profil console (82267578 ~1 %
  des piles du fil de dessin). Les temps PC des études précédentes du fil de dessin (A4E0 ~7 ms) étaient gonflés par ce défaut.

### Coût de l'échantillonneur de piles sur la console : négligeable (2026-11-06)

- Partie de l'utilisateur de 14 min, .nro f0f6fcc1 (out/reference/opdarkness-20261106-echantillonneur-lto-pgo.nro), toml
  `od_fil_maj = true`, `banjo_renut_ab_target = 20` (échantillonnage suspendu une période sur deux) ; journaux
  out/switch-logs-echantillonneur-20261106 ; 0 FATAL, 6 chargements en boost.
- **Alternance** (out/outils-essais/ab.py, 85 intervalles) : actif - en pause = -0,05 ± 0,11 ms par image ; -0,11 ± 0,15 ms
  sur les 42 intervalles à moins de 12 ms de GPU ; rien dans aucune tranche de dessins. **Coût direct** (ligne « sampler: ») :
  53 ms de pauses par 10 s, tous fils confondus (~9,5 µs par pause, une pause de 4,2 ms une fois), fil du profileur 0,7-1,2 %
  d'un cœur. **Décision : témoin gardé dans toutes les parties.** (Chez torchlight les bilans de 10 s duraient ~15 s ; ici
  l'écriture du bilan prend 0,2-0,5 s sur le fil du profileur, sans effet sur les images.)
- Partie jouée sur une autre carte que les précédentes (tuiles MAP\MAP2_2x_11) : **limitée par le GPU** (occupé à 99 %, 21-22 ms
  par image en 480p et 540p), 38 % du temps en 360p (14 % en 017, 0 % en 015) ; les images/s ne se comparent donc pas aux parties
  d'avant. Le coût GPU de cette carte serait la piste si la descente en 360p gêne.

## GPU en combat et reprises de rr6-nx (2026-11-10)

Partie longue de l'utilisateur (36 min, mission 3 à Rjukan, out/switch-logs-longue-20261110) : combats **limités par le
GPU**, 60 % du temps en 360p (GPU saturé à 21-24 ms par image en 480p / 540p, 10-13 ms en 360p). Profil console par
shader du 2026-11-03 (out/switch-logs-avant-20261106b/opdarkness_007.log, out/outils-essais/profil.py), combat en
720p, 22 ms : scène ~52 % (sept shaders de 1 à 2 ms), flou et post-traitement ~23 %, résolutions + présentation 15 %.

Rejeu de la mission sans console : sauvegarde out/sauvegarde-apres-longue-20261110 (menu de carte de Rjukan),
`~/essais-ryujinx/m3.sh` (Ryujinx) et `m3pc.sh` (reNut PC ; copies dans out/outils-essais) : Load Game, 4 x bas, A,
A, puis A répétés ; menu d'action du combat vers 470 s. Ryujinx ne mesure pas le GPU (horodatages nuls).

Repris de rr6-nx (github.com/ChanseyIsTheBest/rr6-nx, branche ultimate ; clone ~/workspace/rr6-nx), tout « mêmes
pixels » :
- **Résolutions de couleur non dessinées** (banjo_deko3d_color_handover, scanout_handover, resolve_keeps_target) :
  la texture lit l'image de la cible ; à l'effacement entier suivant elle la prend. Ajout propre au jeu
  (od_renut_relais_destruction) : le jeu décrit sa cible de flou 320x180 par une autre vue d'EDRAM à chaque passe,
  la cible est donc détruite plusieurs fois par image ; son image va alors à la texture au lieu d'être recopiée.
  Ryujinx, combat : 7 résolutions de couleur par image, 5 évitées (2 sans l'ajout).
- **Profondeur** (depth_handover_readonly, _sampled, depth_read_layout) : une résolution laissée dans la cible n'est
  plus dessinée pour un dessin qui n'écrit ni profondeur ni stencil. Ici les 2 par image restent dessinées : le jeu
  efface un coin 320x180 de la profondeur de scène entre-temps.
- **Rampe gamma** (banjo_renut_gamma_upload_once) : elle était recopiée à chaque image par le moteur de copie, soit
  une barrière complète par image (96 056 en 94 000 images dans le journal console).
- **Cache par tuiles de Maxwell** (sasrt_gpu_tiled_cache, faux par défaut ; alternance dans la partie
  `banjo_renut_ab_target = 21`) : Ryujinx ne l'émule pas, la console seule peut juger (image et gain).
- **Test alpha** : tout pixel shader traduit par la console finissait par le test alpha lu à l'exécution + discard,
  donc sans rejet précoce en profondeur. Passes « alpha » (constante 903) et « earlyz » (909) de
  tools/renut/deko3d_glsl_opt.py portées dans src/od_chaine_deko3d.cpp (PassesPixel, od_chaine_passes) ;
  banjo_deko3d_alpha_spec = true dans switch/defauts.toml. Le paquet GLSL porte une version (mot 6 de l'en-tête) :
  un paquet d'avant est refait au lancement. Relevé PC du combat (étiquettes « z » et « a<fonction> » du profil par
  shader, ajoutées) : les shaders de scène les plus lourds (2E117723B83B68CF, EC78065CF82CFB45, 4C5CFD54864D90BF,
  8794D18797E2B5B4) dessinent avec le test alpha « > » **sans écrire la profondeur** : le cas de la constante 909.

Vérifié dans Ryujinx (mission 3, 0 FATAL, captures comparées à un témoin options coupées : mêmes images à la
dérive de caméra près). **Gain GPU non mesuré : à mesurer sur la console.** Pas repris : spécialisations de shaders de
sasrt-nx (plain_fetches, one_x, clip_spec... : passes GLSL écrites pour le texte de DXC, à porter en C++).

### Partie console du 2026-11-10 (13 h 59, out/switch-logs-rr6-20261110)
- **Artefacts** aux fondus au noir et aux changements de caméra : bloc de données parasites en bas à droite de la
  scène (captures/ du dossier). Non reproduit dans Ryujinx. Suspect principal : od_renut_relais_destruction (cible
  rebâtie sur une mémoire neuve, non effacée ; rr6-nx : sasrt_renut_deko3d_clear_new, pas porté) ; puis le cache par
  tuiles, puis color_handover (image neuve à l'effacement entier). Essai en cours : toml de la carte avec
  `od_renut_relais_destruction = false`, sans l'alternance 21.
- Mesures (5 intervalles de combat seulement) : 720p 96 % du temps ; combat à 330-400 dessins en 540p : GPU 14,4 ms
  par image sans le cache par tuiles (partie du matin : saturé à 21-24 ms en 480p / 540p). Cache par tuiles : GPU
  17,7 contre 14,4 ms (+1,56 ± 0,52 ms par image) : plus lent, à laisser coupé. Barrières complètes : 1 806 en
  110 660 images (96 056 en 94 000 le matin).
- **Coupable trouvé en deux essais console** (14 h 34 : relais à la destruction seul coupé, artefacts pires : écran
  entier de la scène en motifs colorés, géométrie juste ; 14 h 41 : banjo_deko3d_color_handover coupé, plus rien) :
  le passage de relais des résolutions de couleur. Coupé dans switch/defauts.toml avec od_renut_relais_destruction.
  Hypothèse : données compressées par le matériel lues brutes (cibles de couleur toujours DkImageFlags_HwCompression
  dans notre plume ; Ryujinx n'émule pas la compression). Restent actifs : rampe gamma, passes alpha / earlyz, relais
  de profondeur en lecture seule.
- **Enquête du relais des couleurs (essais console 4 à 6, journaux out/switch-logs-essais-relais-20261110)** : artefacts
  aussi sans le balayage de l'écran (essai 4) et sans compression des cibles de couleur (essai 5,
  banjo_deko3d_color_compression = false) ; **absents sans la prise d'image à l'effacement** (essai 6,
  od_renut_relais_prise = false), mais alors aucune résolution n'est évitée (49 681 laissées, toutes dessinées après
  coup). Le défaut apparaît pendant les fondus enchaînés (la scène précédente gardée et mélangée à la nouvelle). Ryujinx
  ne le montre pas, même avec les images neuves peintes en magenta (od_renut_diag_image_neuve : 4 images neuves,
  aucun magenta à l'écran) : pas une lecture d'image jamais dessinée ; cause non trouvée (ordre GPU ou caches propres
  à la console ?). Gain en jeu : ~1 résolution plein écran par image (~0,3 ms sur 15-22 ms). **Abandonné** : relais
  des couleurs coupé par défaut, options de diagnostic gardées.

### Partie de mesure du soir (2026-11-10, 19 h 19, out/switch-logs-mesure-20261110)
- 13 min, 0 FATAL, relais des couleurs coupé, aucun artefact vu ; image jugée plus nette par l'utilisateur.
- Part du temps : 360p 4 % (59 % le matin), 480p 33 %, 540p 42 %, 720p 21 %. GPU par image à définition et dessins
  égaux (ligne « [renut] 10.0 s ... GPU x ms a frame ») : 540p, 250-330 dessins, 17,1 -> 15,5 ms ; 480p, 330-400,
  16,8 -> 15,4 ; 480p, 250-330, 15,1 = 15,1. Gain modeste (0 à 1,5 ms) mais assez pour rester sous le seuil de
  descente (21 ms) : le matin, le régulateur restait bloqué en 360p (montée estimée au-dessus de 18 ms).

## Passes GLSL de banjo-nx sur la console (2026-11-10 au soir)
- Mesure sur PC (tools/renut/deko3d_shader_stats.py, uam de out/deko3d_tools, variantes de pipeline_list.bin de la
  carte, poids = profil GPU console par shader du 2026-11-03) ; GLSL de la console extrait du paquet de la carte par
  out/outils-essais/extraire_glsl.py. Instructions uam pondérées : console (glslang + alpha + earlyz) PS 187 / VS 381 ;
  paquet PC (DXC + passes) PS 148 / VS 215. Le GLSL de glslang n'a pas la forme de celui de DXC (g_UboShared_1.v[34]
  au lieu de g_UboShared.v[34u]) : les passes qui cherchent des mots du tampon ne s'y appliquent pas ; les passes de
  structure, si.
- Passes seules sur le texte console : pred PS 155 (BCC98654F74995F3, le plus lourd du profil : 504 -> 312), guardsel
  166, sel 179, vsout VS 310, ifmerge VS 368 ; les autres ~0. Retenu « vsout,pred,sel,ifmerge » : PS 147 (-21 %),
  VS 265 (-30 %) ; avec predmul, bool01, dowhile, guardsel : 145 / 263 (pas la peine).
- Portées en C++ (src/od_passes_glsl.cpp, sans std::regex), appliquées par la chaîne après alpha / earlyz, aux deux
  stages (vsout aux vertex shaders) ; version du paquet GLSL 3 (réinstallation au premier lancement). Identiques au
  script octet pour octet, ensemble et une par une, sur les 176 shaders console et les 176 du PC (outil
  src/outils/od_passes_glsl.cpp, compilé à la main). La constante 905 est déjà donnée par plume (banjo_deko3d_vs_outputs,
  d'après le texte) : rien à changer dans le moteur. Nouvelles variantes des vertex shaders (905) : préinstaller par
  Ryujinx avant la console. **Ni essayé dans Ryujinx ni sur la console.**
- **Partie PGO de l'utilisateur (2026-11-10, 20 h 30, 26 min, .nro instrumenté a6ae76a2 ; out/switch-logs-pgo-20261110,
  flux out/pgo-flux-renut-20261110)** : 0 FATAL, 157 bilans od_renut_marques à 0, 540p 76 % du temps (360p 1 %). GPU
  par image en 540p, à dessins égaux, contre la partie de mesure du soir (passes absentes) : 150-250 14,7 -> 14,2 ms,
  250-330 15,5 -> 15,0, 330-400 16,4 -> 15,8, 400-450 16,8 -> 16,4 : ~-0,5 ms (3 %), constant (le temps GPU ne dépend pas
  de l'instrumentation du processeur).
- Profil : out/pgo (ancien dans out/pgo-20261110a, sur ryzen), console seule. .nro final LTO + PGO :
  out/reference/opdarkness-20261110-passes-lto-pgo.nro (8f8c540a ; ELF out/switch-elf/opdarkness-20261110-renut-passes-
  lto-pgo.elf ; ryzen out/build/switch-renut-pgo, refait dans un dossier vidé) ; audit_profil.py : 0 fonction sans
  profil ; Ryujinx mission 3 : 0 FATAL.
- **Partie de mesure du .nro final (2026-11-10, 23 h 19, 19 min, out/switch-logs-final-20261110)** : 0 FATAL, 0 bloc
  changé sans marque, aucun défaut vu par l'utilisateur (« nickel »). 540p 69 % du temps, 720p 29 %, 480p 1 %, 360p 0 %.
  GPU par image en 540p : 330-400 dessins 13,5 ms (16,4 à la mesure de 19 h 19, 15,8 en PGO), 450-700 15,5 ms (19,4 /
  19,7) ; images/s 57 à 330-400, 50,4 au-delà de 450 (22 intervalles) : le GPU passe sous 16,7 ms, le processeur
  redevient limitant en combat chargé. Les scènes diffèrent d'une partie à l'autre (bruit ~8 %).
- **Candidat à la première release** (décision de l'utilisateur, 2026-11-10) : ce .nro (8f8c540a). Prochaine session :
  release et réessai d'une installation de zéro sur la console (traduction des 176 shaders par la console, sans aucun
  fichier préparé sur PC).

## Reprises de renut_nx_ultimate_base (2026-11-13)

Base : github.com/ChanseyIsTheBest/renut_nx_ultimate_base (katamari-nx 1.0.1 + 137 ports ; docs/ultimate-fixes.md,
une ligne par port). Choix d'après le profil console du .nro 8f8c540a (partie de mesure du 2026-11-10, fil
vmsDrawThread_1, 82 intervalles à 1 020 MHz) : CaptureDelta 10,0 % (soi), sections critiques ~5,4 % (RtlLeave 2,8,
RtlTryEnter 2,0, adaptateur HostToGuestFunction 0,6), mutex ~4,5 % (surtout render_thread::Push), code du jeu ~70 %.

- **Comparaison par masques et préchargement de la chaîne de deltas** (bangaio-nx : banjo_renut_delta_masks,
  banjo_renut_delta_prefetch ; backend/queued.cpp, sa2_line_compare.h Lines64DifferMask). Dans CaptureDelta, les
  échantillons étaient sur la boucle par bloc (réduction NEON, branchement) et sur les chargements des lignes du device :
  22,9 blocs comparés par delta en moyenne pour 2,9 changés (les marques désignent trop de blocs). Désormais : les 39 blocs
  lus en 3 séries + le bloc court, une réduction pour quatre blocs, sans branchement, puis seuls les blocs changés sont
  visités ; les marques ne choisissent plus les blocs (comparaison exacte). Au début de QueueDraw, les 78 lignes (device et
  ombre) sont demandées d'avance (prfm). Options `od_renut_delta_masques` et `od_renut_delta_prefetch` (vraies) ;
  alternance dans la partie : `banjo_renut_ab_target = 22` (masques coupés : blocs marqués un par un) et 23 (sans
  préchargement).
- **Sections critiques en accroches brutes, propriétaire lu dans le PCR** (sa2-nx kernel_critsec_pcr_owner + sasrt-nx C1,
  port 93 ; SDK xboxkrnl_rtl.cpp, patches/sa2-nx-sdk.patch) : RtlEnter / RtlTryEnter / RtlLeaveCriticalSection du jeu sans
  adaptateur HostToGuestFunction et sans XThread::GetCurrentThread() (KPCR de r13 -> PRCB -> KTHREAD) ; nos atomiques et
  le réveil ciblé de WaitMissedCriticalSection gardés. Sur PC, chaque appel compare au fil cherché (avertissement si
  écart). `kernel_critsec_pcr_owner` (vrai). od_comptes.cpp passe par RtlTryEnterCriticalSectionFast /
  RtlLeaveCriticalSectionFast (mêmes corps).
- **File du rendu sans verrou** (port 105 : dishonored-nx, d'après sa2-nx du 2026-10-28 ; backend/render_thread.cpp) :
  anneau de Vyukov de 16 384 places, un compare-and-swap par ajout, même ordre et mêmes réveils ; plus de std::call_once
  à chaque Push. `od_renut_file_sans_verrou` (vrai).
- Corrigé au passage : la version PC ne se liait plus depuis le 2026-11-10 (Deko3DSetTiledCache appelé hors de la Switch).
- **Vérifié sur PC (ryzen, mission 3, sauvegarde du 2026-11-10, ~/essais-ryujinx/m3pc.sh, sa2_renut_snapshot_deltas_verify)** :
  masques seuls 5 266 467 dessins comparés au device entier, 0 différence ; lot complet 5 312 379, 0 différence ; aucun
  écart de propriétaire PCR ; 60 images/s ; mêmes deux erreurs qu'avant au démarrage (/shaders\, BaseHeap::Release).
- .nro LTO + PGO (incrémental dans out/build/switch-renut-pgo sur ryzen, profil out/pgo du 2026-11-10 ; audit_profil.py :
  0 fonction du code du jeu sans profil ; queued.cpp, render_thread.cpp et le SDK modifié ont perdu le leur) :
  out/reference/opdarkness-20261113-ultimate1-lto-pgo.nro (md5 32cefed4 ; ELF
  out/switch-elf/opdarkness-20261113-renut-ultimate1-lto-pgo.elf).

Écarté :
- **CG-13** (blur-nx, ports 123 / 133 : un seul mode flottant pour tout le code du jeu, aucune bascule de FPCR) : écrit dans
  le SDK (rex/ppc/context.h, REX_FPCR_ONE_MODE, non défini). Comptage sur PC (REX_OD_DENORMAUX, build
  out/build/renut-denorm de ryzen, ligne « [denormaux] » toutes les 10 s, mission 3) : en combat ~146 000 régions scalaires
  par seconde (notre générateur évite déjà les bascules autour des lfs / stfs : fp_moves_modeless ; blur-nx en avait 1,1
  million), dont 2 à 2,6 % lisent un dénormal ou font un dépassement par le bas (30 000 à 38 000 par 10 s). Le mode unique
  changerait ces résultats, pour un gain estimé ~1 % : pas pris.

**fmadds arrondis une fois** (blur-nx, port 96 ; blur-nx : -3 % de processeur ; appliqué le 2026-11-13 à la demande de
  l'utilisateur) : tools/fma_single.py (adapté : macros à la fin de opdarkness_pch.h), lancé par tools/apres_codegen.py
  après vmx_ecritures et avant les copies (4 623 sites dans 133 fichiers ; 4 160 dans les copies en masse) ; SDK
  rex/ppc/fma_single.h ; od_compresseur.cpp passe par REX_FMADDS (ses deux fmadds), comme le code généré. Code généré
  d'avant (celui de la collecte PGO du 2026-11-10) : out/generated-avant-fma-20261113.tgz sur ryzen. Change des résultats
  (plus proches du Xenon : un seul arrondi), d'où une nouvelle collecte PGO (fonctions changées sans profil sinon).
- **Partie console du 2026-11-13 (8 h 54 - 9 h 19, 25 min, .nro 32cefed4, toml du joueur + banjo_renut_ab_target = 22 ;
  out/switch-logs-ultimate1-20261113)** : 0 FATAL, mêmes deux erreurs qu'avant au démarrage, 148 bilans à 0 bloc changé
  sans marque, file sans verrou active (« lock-free queue »).
  - Alternance 22 (masques coupés la moitié du temps, out/outils-essais/ab.py) : au-delà de 450 dessins (98 intervalles)
    on 47,8 / off 47,0 images/s, **-0,43 ± 0,43 ms par image** avec les masques ; en tout +0,27 images/s. Gain faible, à la
    limite du bruit.
  - Images/s à définition égale (par_definition.py) contre la partie du 10 (8f8c540a) : 540p 450-700 dessins 50,4 (22
    intervalles) -> 53,2 (35) ; 330-400 56,8 -> 58,2. +5 % au plus chargé, dans l'écart habituel entre deux parties (~8 %).
  - Profil du fil de dessin (switch_profile_report.py, 105 intervalles contre 82) : CaptureDelta 10,0 -> 9,1 % (moitié du
    temps avec l'ancienne boucle), sections critiques 5,6 -> 5,0 %, mutex 4,5 -> 2,6 %, Push 1,1 -> 2,0 % (l'anneau :
    stlr puis ldar de g_waiting), **QueueDraw 0,3 -> 2,0 %** : la boucle des 78 prfm de PrefetchDeltaLines (échantillons sur
    les prfm : le tampon de remplissage de l'A57 sature). Le préchargement coûte plus qu'il ne rapporte :
    `od_renut_delta_prefetch = false` à essayer (alternance 23 pour le mesurer), puis par défaut.
- **Partie PGO du 2026-11-13 (10 h 37, 38 min, .nro instrumenté 67323954 avec le port 96, alternance 23 ;
  out/switch-logs-fma-pgo-20261113, flux out/pgo-flux-renut-20261113)** : 0 FATAL, rien de bizarre à l'image (joué sans
  son). Alternance 23 : **le préchargement gagne** -0,75 ± 0,51 ms par image en tout (-1,06 ± 0,75 à 400-450 dessins,
  -0,95 ± 0,74 au-delà de 450) : le profil se trompait (les échantillons tombent sur les prfm, mais le temps est rendu dans
  CaptureDelta). `od_renut_delta_prefetch` reste vrai.
- **.nro final LTO + PGO** (profil fusionné sur ryzen dans out/pgo, l'ancien dans out/pgo-20261110b ; dossier
  out/build/switch-renut-pgo vidé) : out/reference/opdarkness-20261113-fma-lto-pgo.nro (md5 ca63d591 ; ELF
  out/switch-elf/opdarkness-20261113-renut-fma-lto-pgo.elf) ; audit_profil 0 fonction sans profil ; Ryujinx mission 3 :
  0 FATAL, images correctes.
- **Partie de mesure (11 h 44 - 14 h 42 avec pauses, out/switch-logs-mesure-fma-20261113)**, cache des shaders retiré
  avant (copie out/carte-avant-zero-20261113, avec les anciens fichiers du rendu natif, retirés aussi) : 0 FATAL ;
  **installation de zéro** : 166 shaders (recherche 11,3 s, traduction 22,0 s, 0 échec), uam 272 variantes en 343 s,
  ~6 min 20 en tout (9 min le 2026-10-27). 540p 450-700 dessins 50,3 images/s (50,4 le 10, 53,2 le matin), 19,5 ms par
  image à 500 dessins (18,9 / 17,9, régression sur les intervalles de plus de 330 dessins) : dans le bruit, scènes plus
  légères (456 dessins en moyenne contre 775-800) ; GPU 14,5 ms (15,3-15,5). Profil : CaptureDelta 9,1 -> 6,1 %,
  xeLeaveCriticalSection 2,6 -> 3,8 %. Port 96 : ni défaut à l'image ni au son (l'utilisateur), gain non mesurable.
  **ca63d591 devient le candidat à la release.**

## Release 1.0.0 (2026-11-13)

- .nro : out/reference/opdarkness-20261113-release-1.0.0.nro (md5 d89300e4 ; ELF
  out/switch-elf/opdarkness-20261113-renut-release-1.0.0.elf) : même code que ca63d591, version 1.0.0 et
  `-fmacro-prefix-map` (chemins de la machine retirés des __FILE__, repris de torchlight-nx ; restent 10 chemins de
  devkitPro, ceux de son mainteneur). Audit du profil 0, Ryujinx mission 3 : 0 FATAL, images correctes.
- Zip : dist/operation-darkness-nx-1.0.0.zip (switch/operation-darkness-nx/ : .nro, README.md, LICENSE,
  THIRD_PARTY_NOTICES.md, tools/extract_xiso.py) ; texte de la release : dist/release-1.0.0.md.
- Choix de l'utilisateur : nom operation-darkness-nx, outils de développement gardés (coupés sans réglage), Switch
  seulement, icône = jaquette du jeu (dite dans Legal), reNut publié tel quel en créditant son auteur, mode TV non essayé.
- tools/extract_xiso.py (messages en anglais) : extraction de l'ISO de l'utilisateur identique à assets/ (5 302 fichiers).
- Correctif du SDK : appliqué sur un clone propre de sa2-nx à fdd6f14, 32 fichiers identiques au SDK de la compilation.
- Dépôt public : ~/workspace/operation-darkness-nx-public (un seul commit, sans historique ; arbre = celui de la branche
  renut). Push et release GitHub : par l'utilisateur, ou à sa demande.
