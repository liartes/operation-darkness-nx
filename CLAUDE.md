# Projet : recompilation d'Operation Darkness (Xbox 360, 2008) avec ReXGlue

Objectif : version PC (Linux) d'abord, puis éventuellement Switch, sur le modèle de torchlight-nx
(~/workspace/torchlight-nx, public ; dépôt de travail ~/workspace/torchlight, dont le CLAUDE.md contient toutes les
leçons du portage : manifeste, rendu natif, Switch) et du guide de portage de sa2-nx
(~/workspace/sa2-nx/docs/porting-guide.md, outils dans ~/workspace/sa2-nx/tools). Répondre en français.
**Plan d'action : docs/PLAN.md** (étapes, cases à cocher, choix du rendu natif).

Jeu : Operation Darkness (Success / Atlus, USA), titre 415407D2, XEX du 2008-04-26. Moteur Intrinsic Alchemy
(fichiers .igb, IGB/), D3DX9 (effets compilés à l'exécution ?), audio XACT (XACT_US/), vidéos WMV (MOVIE/,
MOVIE_SCN/), interface XUI (XUI/), données de jeu propres (RULEDATA, SCR, STGDATA, MAP...).

## Règles
- Les fichiers du jeu viennent de la copie légitime de l'utilisateur (ISO dans ce dossier). Ne jamais versionner ni
  distribuer de données dérivées du jeu (assets/, code généré, out/analyse/image.bin, captures d'écran, shaders).
- Ne renommer ni supprimer de fichiers dans assets/ sans demander.
- Le manifeste est suivi par git : committer avant toute série de modifications.
- Textes vus par les joueurs (README.md, fenêtres) en anglais ; documentation de développement en français.

## Mise en place (faite le 2026-10-07)
- assets/ : contenu de la partition XDVDFS de l'ISO (XGD2, partition à 0xFD90000), extrait par
  `tools/extract_xiso.py "Operation Darkness (USA).iso" assets` (5302 fichiers, 4,6 Go).
- Projet créé par `rexglue init --project-name opdarkness --xex-path assets/default.xex --game-root assets`.
- SDK : ~/rexglue-sdk-0.10.0-linux-amd64/linux-amd64 (Clang 20, CMake, Ninja), le même que torchlight.

## Version PC
- ./run.sh [options du jeu] : codegen si besoin, configure (cmake --preset, CMAKE_PREFIX_PATH vers le SDK la première
  fois), compile (JOBS=6 par défaut), lance, affiche le FATAL du log (opdarkness.log).
- Lancement : LD_LIBRARY_PATH=<sdk>/lib ./out/build/linux-amd64-release/opdarkness --game_data_root=$PWD/assets
  --gpu_plugin=xenos --license_mask=1 --log_file=opdarkness.log
- Débogage (repris de torchlight) : build debug (PRESET=linux-amd64-debug), sous gdb toujours
  `handle SIGSEGV nostop noprint pass` et `handle SIG35 nostop noprint pass`.
- /dev/shm/xenia_memory_* : jamais libérée en sortie brutale ; /dev/shm plein => « Erreur du bus » au lancement.

## Manifeste (opdarkness_manifest.toml)
- Codegen : `rexglue codegen opdarkness_manifest.toml` (~50 s, 30 040 fonctions), puis reconfigurer (cmake --preset)
  pour que les nouvelles sources soient prises en compte (run.sh le fait).
- Critère : 0 erreur d'analyse, et 0 « ERROR: conditional branch to unknown address » dans generated/default
  (`grep -c ERROR generated/default/opdarkness_recomp.*.cpp | grep -v ':0$'`).
- Analyse : `tools/dump_image.sh` (image invitée décodée -> out/analyse/image.bin, base 0x82000000), puis
  `tools/desas.py <début> <fin|+taille>` (llvm-mc-20).
- tools/candidats.py [--ecrire] : fonctions atteintes seulement par pointeur (mots des données, paires lis/addi du
  code) dont l'instruction précédente termine une fonction ; exclut les cases et tables des switch (bctr après
  lwzx/add, mot qui est lui-même une adresse de code) et les labels internes (b court qui saute par-dessus).
  --ecrire régénère la section de fin du manifeste. À relancer après chaque codegen (stable : 118 entrées).
- Tables de saut : `python3 ~/workspace/sa2-nx/tools/switch_check.py --image out/analyse/image.bin
  --gen generated/default --code 82150000:8293A130` (2026-10-07 : 0 tronquée ; 6 sans borne, 4 formes à revoir).
- Corrections déjà faites : 3 thunks sans prologue (« b » vers eux non résolus) ; thunk 0x82589EC8 suivi d'une
  fonction sans prologue 0x82589ED8 que l'analyse commençait à 0x82589EE8.
- Au lancement, FATAL « invalid or unregistered function at 0x... » : ajouter `0x... = {}` (ou `{ end = ... }`),
  régénérer. Leçons de torchlight : une entrée au milieu d'une fonction la coupe (vérifier prologue mflr/stwu et fin
  de la précédente) ; fonctions trouvées dans les trous entre fonctions (tools/find_gaps.py de torchlight).

## Essais automatiques
- Manette de script : --od_manette="105:start,115:a,150-152:haut" (secondes depuis la création des entrées ; boutons
  a b x y start back haut bas gauche droite lb rb lt rt stick_*), src/opdarkness_manette.cpp (de torchlight).
- tools/captures.sh <durée> <t1> <t2>... : lance la build et capture la fenêtre (xwd -> PNG, tools/xwd2png.py) dans
  out/captures/ ; MANETTE=... et OPTIONS=... en variables d'environnement. xwd échoue parfois (BadColor) : relancer.
- **Scénario rapide** (2026-10-09, à préférer) : MANETTE="$(tools/scenario_combat.sh)" : START toutes les 3 s pour
  passer logos et vidéos (menu principal ~30 s), New Game, puis START/A jusqu'à la bataille (~80 s ; en bataille
  START ouvre le menu système, donc A seulement ensuite) : menu d'action d'Edward ~90 s en rendu natif.
- Anciens repères de temps (PC, GPU émulé, sans passer les vidéos) : logo Atlus ~15 s, Dolby ~40 s, écran titre ~100 s ; START à 105 s puis A
  (115, 125, 135 s : New Game, nom « Edward », Yes) ; introduction ; A/START toutes les 8-24 s -> mission 1 (combat
  en 3D) vers 280 s.

## Rendu natif (voie A : docs/PLAN.md étape 3, docs/D3D.md)
- SDK de sa2-nx (~/workspace/sa2-nx, cloné le 2026-10-07) : générateur ~/workspace/sa2-nx/sdk/out/linux-amd64/rexglue,
  runtime compilé avec le jeu : cmake -S . -B out/build/sa2sdk -G Ninja -DCMAKE_BUILD_TYPE=Release
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DREXSDK_DIR=$HOME/workspace/sa2-nx/sdk
  -DGIT_EXECUTABLE=/bin/false -DCMAKE_C_FLAGS=-march=x86-64-v2 -DCMAKE_CXX_FLAGS=-march=x86-64-v2 -DOD_NATIF=ON.
  Lancement : LD_LIBRARY_PATH=~/workspace/sa2-nx/sdk/out/linux-amd64 (librexruntime.so du SDK de sa2-nx, pas celui
  du SDK d'origine : « undefined symbol » sinon) ; tools/captures.sh avec BIN=out/build/sa2sdk/opdarkness LIBS=...
- Options : --sa2_renderer_native=true (rendu natif), --od_dump_shaders=true (conteneurs -> <build>/shaders),
  --sa2_shader_misses=misses (ce que le paquet ignore -> <build>/misses/{discovery,missing,variants}).
  tools/paquet_shaders.sh refait le paquet (sa2_shaders.mesp à côté de l'exécutable) ; renommer misses en misses-N
  avant de relancer, le script les prend tous.
- Code importé tel quel (tools/import_rendu_sa2.sh, commit dans src/me/SA2_NX_COMMIT) : src/me, src/cpu ; propre au
  jeu : src/od_me_hooks.cpp, src/od_api_stub.cpp, cmake/rendu_natif.cmake.
- Bilans du rendu toutes les 10 s dans le journal ([native] ...), images/s : ligne [images] (src/od_images.cpp).

## Jouer sur PC (./jouer.sh)
- Rendu natif + collecte des shaders inconnus (out/build/sa2sdk/shaders, misses-<date>/) ; ensuite
  tools/paquet_shaders.sh. Manette SDL reconnue ; clavier avec --mnk_mode=true : A = Espace, B = Retour arrière,
  X = L, Y = P, START = Entrée, BACK = Tab, stick gauche = ZQSD en position QWERTY (W A S D), croix = Maj+flèches,
  stick droit = flèches, LB/RB = 1/3, gâchettes = Q/E (codes de touches QWERTY du SDK).

## Version Switch (commencée le 2026-10-09)
- Compilation : `JOBS=6 tools/switch/compiler.sh [--reconfigurer]` -> out/build/switch/opdarkness.nro (conteneur Docker
  devkitpro/devkita64, GCC 15.2 ; toolchain de sa2-nx tools/switch/cmake ; NVK statique de torchlight
  ~/workspace/mesa-switch/nvk-sdk, Mesa 26.2.1 sans le correctif « set 4 » de sa2-nx : optimisation seulement).
  Première compilation complète ~1 h (134 fichiers générés en -O3), sans LTO pour l'instant. Le code généré vient du
  générateur sur PC ; les SPIR-V internes du rendu viennent de la build PC (glslangValidator de l'hôte ne tourne
  pas dans le conteneur : OD_SPIRV_INTERNES).
- Réglages : switch/defauts.toml (compilés dans le .nro, src/opdarkness_app.h BuiltInConfig) ; switch/opdarkness.toml
  (écrit à côté du .nro s'il manque). Données : sdmc:/switch/operation-darkness-nx/game_root/ = copie de assets/ (pas de paquet
  .nxpack : 4,4 Go, au-delà de la limite FAT32).
- Carte SD : tools/switch/carte_sd.sh -> out/switch-sd/switch/operation-darkness-nx/ (operation-darkness-nx.nro ;
  PAQUET=1 ajoute sa2_shaders.mesp, CACHE_PC=1 le cache traduit sur PC : **par défaut aucun des deux**, la console
  traduit tout elle-même, demande de l'utilisateur) et l'ELF non strippé dans out/switch-elf/ ; tools/switch/console.sh
  (MTP par DBI) : etat, envoyer, envoyer-jeu (copie assets/ dans game_root/, une fois), vider-cache, journaux [--vider].
- Essais sur la console sans l'utilisateur à la manette : opdarkness.toml de la carte avec od_manette (scénario
  rapide, tools/scenario_combat.sh) et od_ecrans="20,30,..." (captures PPM dans captures/ à côté du .nro,
  src/od_ecrans.cpp ; l'album de la console ne capture pas le jeu). Modèle : switch/essai-console.toml (od_manette à remplacer par la sortie de tools/scenario_combat.sh).
  Récupérer : console.sh journaux, puis copier "$T"/captures/*.ppm (voir les commandes de la session du 2026-10-09).
  Le scénario finit en boucle dans le tutoriel : il ne va pas au-delà du premier tutoriel d'attaque.
- Shaders : traduits par la console elle-même (glslang, docs/SHADERS_EN_JEU.md « Switch : glslang »), cache
  cache_shaders.odsc sur la carte. Banc d'essai PC : tools/glslang_essai.sh, --od_compilateur=glslang.
- Code propre à Linux gardé (__linux__) : od_plantage.cpp (le SDK Switch a son gestionnaire), prctl d'od_sommeil.
- Modifications du SDK de sa2-nx : patches/sa2-nx-sdk.patch (dont REXGLUE_GLSLANG_HLSL) et patches/glslang-verrou.patch.

## Enregistrement et rejeu des entrées (src/od_enregistreur.cpp)
- ./jouer.sh enregistre la manette dans out/entrees/<date>.txt (lignes E : changement d'état d'un joueur ; K :
  événement de touche, utilisé par les menus ; chacune avec le numéro d'image et le temps en ms).
- tools/rejouer.sh out/entrees/<date>.txt [durée] : rejeu sans joueur (la vraie manette est ignorée), captures toutes
  les PAS=30 s dans out/captures/rejeu-<date>/, collecte des shaders. Repère : images (--od_rejeu_horloge=ms pour le
  temps).
- Interceptions : sub_82399730 (XInputGetState, seul appelant sub_8232EC90) et sub_82399808 (XInputGetKeystroke,
  appelant sub_8217BC78).
- Limite : le jeu n'est pas déterministe (chargements, threads) ; essai du 2026-10-08 : START rejoué, même
  transition titre -> menu, quelques images de décalage. Vérifier sur les captures qu'un long rejeu ne diverge pas.
  Rejeu de la session de l'utilisateur (out/entrees/20261008-204322.txt, 244 s, mission 1) : suit bien la partie
  (cinématique, menus d'action, déplacements, tour de Jude Lancelot).
- **Cadence du jeu (2026-10-09)** : la tâche du moteur qui lit la manette (sub_8232EC90) tourne à 60 lectures/s fixes
  (horloge des vblanks), quel que soit le nombre d'images (30 à 60) ; la logique, elle, avance avec les images
  (avec le GPU émulé à 27 images/s, le jeu est en retard sur le temps). Rejeu par images ou par temps : divergent
  dès que la cadence change (compilation de shaders à la première partie, chargements). D'où le mode **lockstep**
  du SDK (--vblank_per_swap=true : un vblank par image présentée, plafonné à 60/s ; un vblank quand même après
  100 ms sans image, pendant les chargements), activé dans jouer.sh et tools/rejouer.sh, et le repère de rejeu par
  défaut = nombre de lectures de la manette (colonne <lecture> des fichiers, --od_rejeu_horloge=lectures).
  Les enregistrements d'avant (sans lockstep ni colonne <lecture>) se rejouent mal.
- Test du 2026-10-09 (out/captures/det-*) : rejeu natif contre émulé en lockstep, captures aux mêmes images :
  même point à l'image 15000 (tutoriel), images intermédiaires différentes à cause des vidéos (lues en temps réel :
  leur nombre d'images dépend de la vitesse). Le rejeu se recale sur les écrans qui attendent un bouton.
- tools/captures_images.sh <dossier> <images> <durée> [options] : captures à des numéros d'image exacts (le jeu se
  met en pause, --od_pause_images) ; sert à comparer rendu natif et GPU émulé, ou deux rejeux.
- tools/rejouer_gdb.sh <entrées> <durée> : rejeu sous gdb, s'arrête sur un signal fatal (piles dans
  out/profil/plantage.txt) ; ~27 images/s sous gdb.

## Défauts d'affichage connus
- **Arme invisible du tireur d'élite ennemi** (signalé par l'utilisateur, déjà avant le 2026-10-16 ; rendu natif, PC et
  Switch, depuis le début du rendu Vulkan) : la petite vidéo de son arrivée montre ses mains vides, l'arme reste
  invisible pendant tout le reste de la mission. Aucun dessin refusé ni shader manquant dans le journal. Le scénario
  automatique ne va pas jusque-là : il faut un enregistrement (./jouer.sh) qui y arrive, puis comparer rendu natif et
  GPU émulé à la même image (tools/captures_images.sh). Référence trouvée par l'utilisateur : vidéo YouTube « Operation
  Darkness - Intro and First Mission (Xbox 360) », https://youtu.be/MaROx91ZCkk?t=549 (vers 9:09) : l'arrivée du
  tireur, fusil visible, a lieu **pendant la mission 1**.
  2026-10-16 soir : sur PC avec les shaders DXC, fusil visible ; sur PC avec le cache traduit par la Switch
  (tools/jouer_shaders_switch.sh), fusil **invisible** : un shader mal traduit par glslang. 110 shaders dans le cache
  de la console. Le PC en glslang (--od_compilateur=glslang) ne reproduit pas fidèlement la Switch (dessins manquants,
  nuage noir derrière l'affichage des dégâts, que la Switch n'a pas) : comparer avec le cache de la console.

## Plantages
- 2026-10-08, session de l'utilisateur, mission 1, **en validant avec A la cible d'une attaque d'Edward** (image
  9596, 244 s) : fenêtre fermée aussitôt, terminal « Aborted (core dumped) », journal coupé net sans FATAL (écrit en
  différé), pas de trace système. Non reproduit par deux rejeux (dont un sous gdb ; le rejeu passe peut-être à côté
  de l'attaque). Hypothèse principale : FATAL « Call to invalid or unregistered function » (le SDK journalise puis
  abort(), comme aux premiers lancements) dans du code d'attaque jamais exécuté avant ; autres abort possibles :
  src/me/native/sa2_shader_library.cpp (message « [sa2] shader library » sur stderr), exception C++ non rattrapée
  (« terminate called » sur stderr) : l'utilisateur n'a vu que « Aborted ».
- **Résolu le 2026-10-09** : seconde session (out/entrees/20261009-082831.txt), journal vidé chaque seconde :
  `FATAL Call to invalid or unregistered function at 0x8225F300` (`li r3,8 ; blr`, méthode de 2 instructions
  précédée d'un `bl` vers une fonction sans retour : écartée par tools/candidats.py, et laissée dans les trous par
  l'audit qui n'avait gardé que les `blr` isolés). Ajoutée au manifeste avec 2 fonctions d'appel du même genre
  (0x824A9B80, 0x824B9858) ; les autres trous sont des enregistrements SEH. Rejeu de la session : Edward tire, la
  partie continue (tour de Jude Lancelot).
- Leçon : un trou de code qui se termine par blr / bctr / b est une fonction, quelle que soit l'instruction d'avant ;
  revoir la liste de tools/trous.py après chaque correction du manifeste.
- Depuis : ./jouer.sh vide le journal chaque seconde (--log_flush_interval=1), garde le code de sortie
  (out/plantages/sorties.txt ; 128 + signal) et écrit la pile native des signaux fataux autres que SIGSEGV
  (src/od_plantage.cpp, --od_plantage_dossier) dans out/plantages/plantage-<pid>.txt.

## Reprise (état au 2026-10-16 au soir, travail suspendu)

### Où on en est
- PC jouable du démarrage à la mission 1 (au moins jusqu'à l'arrivée du tireur d'élite), rendu natif (./jouer.sh).
- **Switch, 720p**, données dans game_root/, shaders traduits par la console (glslang). **Compilation de référence :
  LTO + PGO** (OUT=out/build/switch-pgo CMAKE_OPTS="-DOD_SWITCH_LTO=ON -DOD_PGO=use", profil out/pgo, jamais
  versionné). Tout à 60 images/s sauf la cinématique du combat (540p dynamique, 51-57 images/s, à-coups ~670 ms au
  total sur 30 s, pire image 150 ms). Charge en combat ~124-131 % / 300 %. Clavier de la Switch pour le nom du joueur
  (validé), sauvegarde OK.
- **Dernier .nro essayé sur la console : 20 h 29** (ELF opdarkness-20261016-2029-lto-pgo2-flip.elf, archive
  1016-flip) : tout le travail du 16 sauf la collecte PGO n°3.
- **Sur la console : le .nro instrumenté de 22 h 15 pour la collecte PGO n°3** (tout le code du 16 au soir ; ELF
  opdarkness-20261016-2215-pgo-gen3.elf ; out/build/switch-pgo-gen), réglages du joueur (switch/opdarkness.toml),
  journaux vidés, dossier pgo/ vide (flux n°1 et n°2 gardés dans out/pgo-flux et out/pgo-flux2 ; profils out/pgo-1 et
  out/pgo). Essai de base dans out/switch-essai/opdarkness.toml.
- **Défaut ouvert : arme invisible du tireur d'élite** (« Défauts d'affichage connus ») : vient d'un shader traduit par
  la Switch (reproduit sur PC avec tools/jouer_shaders_switch.sh, cache de la console out/shaders-switch/
  cache_shaders.odsc ; avec les shaders DXC le fusil est visible).
- Défaut de lecture (tampon nul, fichiers 2D\ au démarrage) : cause inconnue, parade en place (src/od_lecture.cpp),
  revu le 16 sans plantage ; GetFileSize hors de cause.
- Rien n'est poussé (pas de remote).

### Prochaine session (demandes de l'utilisateur)
1. **Collecte PGO n°3** : le .nro instrumenté est sur la carte ; l'utilisateur joue
   (lent, normal ; HOME avant de fermer) ; récupérer pgo/od_pgo_*.stream -> out/pgo-flux3, mettre out/pgo de côté
   (out/pgo-2), tools/switch/pgo_fusion.sh out/pgo-flux3 ; **ne rien régénérer avant l'analyse** ;
   python3 tools/pgo_classement.py (fonctions chaudes, copies localisées, fichiers) ; compilation OD_PGO=use + LTO ;
   essai de base sur la console ; nouvelle passe de code natif selon le classement.
2. **Arme invisible** : raccourci clavier qui bascule en direct une moitié des shaders suspects entre version Switch
   et version DXC (recréer les pipelines), pour trouver le shader fautif en une partie (le fusil reste invisible
   toute la mission) ; caches prêts : out/captures/tireur/cache-switch(.odsc), cache-dxc, cache-glslang ;
   enregistrements out/entrees/20261016-205431.txt (jusqu'au tireur). Les rejeux ne sont pas assez reproductibles
   pour cette scène (dérives de plus de 1 000 images).
3. Rendu (fil « GPU ring native ») : Draw, CopyVertices, XXH3 (appelants à mesurer), RedirectClearDepthEDRAM4.

### Environnement (hors du dépôt, à vérifier en reprenant)
- ~/workspace/sa2-nx : clone de sa2-nx (commit fdd6f14) **avec patches/sa2-nx-sdk.patch appliqué** (modifications
  non commitées dans sdk/ : `git -C ~/workspace/sa2-nx status` doit montrer rtl.h, xboxkrnl_rtl.cpp, config.h,
  config.cpp, builders/context.cpp et thirdparty/CMakeLists.txt) **et patches/glslang-verrou.patch** (dans
  sdk/thirdparty/glslang, non suivi par git). Construits dedans : sdk/out/linux-amd64 (rexglue, librexruntime.so,
  libSPIRV-Tools*.a...), sdk/out/switch-arm64 (bibliothèques Switch), out/glslang, out/glslang-hlsl, out/tools,
  out/shaders.
- Docker : image devkitpro/devkita64 (compilation Switch) ; NVK de torchlight : ~/workspace/mesa-switch/nvk-sdk.
- ~/workspace/_tools/dxc-1.8.2505.1 : DXC Linux (release Microsoft).
- Console : DBI en mode MTP (monté par GVFS) ; lancer le .nro en title override (R) ou par un forwarder.
- perf : kernel.perf_event_paranoid=1 réglé par l'utilisateur, **perdu au redémarrage** (lui demander
  `! sudo sysctl kernel.perf_event_paranoid=1` si besoin de perf ; tools/piles.sh marche sans).
- Identité git du dépôt (locale) : Liartes <13268731+liartes@users.noreply.github.com>, comme torchlight-nx.
- **L'utilisateur compile parfois d'autres projets (torchlight) sur le même PC : ne pas lancer de mesures pendant
  (il le signale) ; vérifier `pgrep -x ninja` et le dossier du processus avant de mesurer.**

### Méthode des essais sur la console (l'utilisateur lance le .nro et rebranche le MTP, rien d'autre)
- Modèle de opdarkness.toml : switch/essai-console.toml (od_manette = sortie de tools/scenario_combat.sh ;
  od_ecrans = captures PPM dans captures/ à côté du .nro). Copier par `gio copy` (après `gio remove`), vider les
  journaux (console.sh journaux --vider : efface aussi le témoin du profileur, à remettre : fichier vide
  logs/rex/stacks_profile.flag).
- Récupérer : tools/switch/console.sh journaux (-> out/switch-logs/), captures : `cp "$T"/captures/*.ppm` avec
  T="$(ls -d /run/user/$(id -u)/gvfs/mtp:host=*/*SD\ Card)/switch/operation-darkness-nx" ; mosaïque avec PIL.
- Images/s : lignes [images] du journal ; profil : out/switch-logs/rex/rex_profile.log (par intervalle de 10 s :
  « game N fps », « fences ... waited N (X ms) » = attente du GPU) ; symboliser :
  `DEVKITPRO=<dossier avec devkitA64/bin/aarch64-none-elf-nm -> llvm-nm-20> python3
  ~/workspace/sa2-nx/tools/switch_profile_report.py out/switch-logs/rex/rex_profile.log --elf out/switch-elf/<ELF du
  .nro testé> --top 40`. Journal du rendu : « GPU time by pass », « EDRAM mode4 transfer causes ».
- Outils de diagnostic de la session (empreintes de microcode des shaders du journal, désassembleur et validation
  SPIR-V, bissections) : tools/dev/ (LISEZMOI.md) ; od_hlsl pour voir le HLSL (out/build/sa2sdk/od_hlsl <sortie>
  <fichier.odsh>).

### Journal détaillé du 2026-10-14 au 2026-10-16 (anciennes priorités et résultats)
1. **Récupérer l'essai de stabilité** du .nro de 20 h 25 (tools/switch/console.sh journaux ; plusieurs
   opdarkness_00N.log = plusieurs lancements ; rex/rex_crash.log s'il a planté ; chercher « [chargeur] »,
   « [res] dynamic », « Unhandled guest access »). Symboliser un plantage : llvm-nm-20 -n sur l'ELF du .nro testé,
   adresse image+0x... (voir la session du 2026-10-14). Si plus de plantage : la stabilité est revenue sans
   l'étape D. Sinon : la cause du tampon nul est ailleurs (fil readDeviceThread, allocateur sub_8232B648 ?).
2. Cinématique : vérifier qu'elle tient 60 en 540p avec la descente plus rapide ; sinon le palier 360p existe.
   Étape 2 éventuelle (demande de l'utilisateur) : détection exacte des cinématiques si le passage en 540p en jeu gêne.
   2026-10-16 : cinématique en 720p = GPU à 96-100 %, ~30 images/s ; en 540p GPU ~45 %, 53-56 images/s (à-coups
   ailleurs : fils du jeu et du rendu à 35-45 % chacun). La remontée en 720p se faisait trop tôt (« room on the GPU »
   à 24,7 images/s, GPU 50 % en 540p, puis 46 images/s) : règle refaite sur le temps GPU par image (< 80 % d'une
   image à 60 Hz après mise à l'échelle). Essai du .nro de 9 h 54 (archive 1016-remontee) : 540p 2 s après le début
   de la cinématique, gardé 22 s, remontée en 720p seulement à 4 ms de GPU par image (cinématique finie), plus
   d'aller-retour. Restent des à-coups côté processeur (images de 65 à 233 ms avec 6 à 22 ms de GPU) au début de la
   cinématique et à la transition suivante : 51-56 images/s. Analyse des à-coups (2026-10-16 après-midi) :
   (a) textures : le contrôle du désentrelacement rapide refaisait chaque niveau par la voie lente (une partie
   n'atteint jamais les 200 niveaux contrôlés) : 9,3 -> 3,2 ms par MB sur PC, coupé sur la Switch
   (sa2_native_tile_fast_checks = 0, 1 sur 64 contrôlé) ; bilan « C3 textures prepared » toutes les 10 s.
   (b) changement de définition (233 ms vers le bas) : nouvelles images créées avant l'attente du GPU, durée des
   étapes dans « [res] render scale phases » ; l'attente des copies mises à l'échelle (20-50 ms sur PC) est
   supprimée : anciennes images détruites quand le GPU a fini l'envoi qui les copie (retired_images_, libérées dans
   CompleteA ; sa2_render_rescale_wait = true pour revenir à l'attente). PC : 30-100 -> 5-9 ms par changement,
   captures correctes avec une bascule toutes les 250 images (sa2_render_height_docked=540
   sa2_render_test_toggle_at=2500 sa2_render_test_toggle_every=250). (c) images lentes du jeu lui-même
   (fil de dessin en attente d'un événement, lectures lentes de la carte SD) : non traitées. Pistes écartées :
   pipelines (tous préparés d'avance, 0,1 ms) ; recherche de place de l'allocateur physique du SDK (17 000 pages
   parcourues par allocation, ~0,05-0,1 ms sur la console ; essai d'indication du haut du tas sans effet, retiré) ;
   relevé « [mémoire] » des allocations du jeu (src/od_memoire.cpp, ~0,3 ms chacune sur PC, appels système).
   Essai console du .nro de 13 h 43 (archive 1016-acoups) : textures 5x plus rapides (62 textures / 6,8 MB en
   19,5 ms contre 49 / 6,1 MB en 98 ms) ; changement de définition 233 -> 183 ms (création anticipée 57 ms, attente
   du GPU surchargé 69 ms, destructions 25 ms) ; allocations physiques ~0,1 ms chacune (hors de cause). L'image de
   233 ms du début de la cinématique reste : GPU 6 ms, fil de dessin du jeu occupé à 91 % dans son propre code
   (parcours récursifs de la scène, sub_8263B0F8 / sub_8264EF80 / sub_826559B0 : mise en place de la cinématique) et
   fil de lecture dans l'ouverture et la taille des fichiers sur la carte SD. Levier restant : vitesse du code du
   jeu (PGO, point 3) ; ouvertures de fichiers (point 5).
   Demande de l'utilisateur (2026-10-16) : 1) rendez-vous audio, 2) FFmpeg, 3) PGO. Rendez-vous audio
   (sub_82890C30, 1 500 par seconde ; profil console : >50 % des échantillons des 2 fils de mixage) : mode bloquant
   od_audio_attente = 2 (src/od_audio.cpp), relevé « [audio] » (od_audio_stats). FFmpeg : déjà -O3 + NEON ; relevé
   « [xma] » sur la console : ~1 500 trames/s à 133 us (20 % d'un cœur) ; FMA autorisées pour FFmpeg sur ARM64
   (patches/sa2-nx-sdk.patch). Essai du .nro de 14 h 08 (archive 1016-audio) : fils de mixage 29,1 -> 16,7 % d'un
   cœur, rien de reporté ailleurs, charge de la cinématique -20 points : mode 2 gardé (switch/defauts.toml). FMA de
   FFmpeg : 138 us par trame contre 137, retirées (correctif du SDK revenu à son état d'avant).
   Défaut de lecture revenu dans cet essai (2D\MENU\m00_tit0000.ptc, fin de lecture sautée, partie normale) **sans
   échec de GetFileSize ni de ReadFile** : hypothèse GetFileSize réfutée ; les ouvertures ratées du bilan [io]
   (4, 2, 2 à chaque lancement) sont des recherches normales du jeu. Requête : taille 0, tampon 0 (objet comme remis
   à zéro : course dans le moteur ?). Cause toujours inconnue ; la parade suffit pour l'instant.
   PGO (outillage prêt, commit « PGO de la version Switch ») : .nro instrumenté OUT=out/build/switch-pgo-gen
   CMAKE_OPTS=-DOD_PGO=generate tools/switch/compiler.sh ; compteurs dans pgo/od_pgo_<N>.stream à côté du .nro
   (toutes les 2 min et à HOME) ; tools/switch/pgo_fusion.sh <dossier> -> out/pgo (jamais versionné) ; puis
   OUT=out/build/switch-pgo CMAKE_OPTS=-DOD_PGO=use. LTO réessayé (demande de l'utilisateur : les plantages du 14
   étaient le défaut de lecture, qui arrive aussi sans LTO) : OUT=out/build/switch-lto CMAKE_OPTS=-DOD_SWITCH_LTO=ON
   (~15 min ; carte : OUT=out/build/switch-lto tools/switch/carte_sd.sh). .nro LTO de 14 h 32 (ELF
   opdarkness-20261016-1432-lto.elf), essai (archive 1016-lto, un lancement) contre 1016-audio (même code sans LTO) :
   mêmes images/s, charge totale ~5 % plus basse, pas de défaut de lecture. PGO lancé avec le LTO (le .nro optimisé
   doit avoir les mêmes options) : OUT=out/build/switch-pgo-gen CMAKE_OPTS="-DOD_SWITCH_LTO=ON -DOD_PGO=generate".
   Démarrages LTO (archive 1016-lto-demarrages) : 3 normaux, aucun défaut de lecture ; un arrêt net 2 s après le
   démarrage (14 h 41, sans rex_crash.log) et un journal vide, au moment où le .nro a disparu de la carte.
   .nro instrumenté de 15 h 08 (ELF opdarkness-20261016-1508-pgo-gen.elf) envoyé avec les réglages du joueur
   (switch/opdarkness.toml ; essai de base gardé dans out/switch-essai/opdarkness.toml) : partie de l'utilisateur
   en cours ; ensuite récupérer pgo/ (od_pgo_<N>.stream), tools/switch/pgo_fusion.sh, compilation OD_PGO=use + LTO.
   Partie de collecte de l'utilisateur (25 min, sauvegarde OK ; demande : clavier virtuel pour le nom du joueur) ->
   out/pgo (175 .gcda dont 135 du code généré). **.nro LTO + PGO de 16 h 30** (ELF opdarkness-20261016-1630-lto-pgo.elf,
   OUT=out/build/switch-pgo CMAKE_OPTS="-DOD_SWITCH_LTO=ON -DOD_PGO=use") : essai (archive 1016-lto-pgo) contre LTO
   seul et sans LTO : mêmes images/s ; fil de dessin 28,6 % (31,9 LTO, 31,2 sans), fil du rendu 28,7 % (32,0 / 33,5),
   charge de la cinématique 170-195 % (181-204 / 182-210) ; à-coups de la cinématique 800 ms au total, pire 167 ms
   (1 018 / 267 et 950 / 200). **Compilation de référence de la Switch désormais : LTO + PGO.** Le profil ne couvre que
   le code de la partie de collecte : à refaire après les changements du code (fonctions modifiées sans profil).
   tools/pgo_classement.py : fonctions du jeu classées par le profil (noms retrouvés sans .gcno) : produit de matrices
   sub_82609660/788 (12,5 %), mixage XACT (~15 %), décodeur vidéo WMV (sub_823ACE18/ACAA8 : >50 % du fil de lecture
   pendant les vidéos MOVIE/ et MOVIE_SCN/), verrou + comptes de références du moteur (sub_8232CCC8, CEC0, CF90).
   Étape 2 (plan accepté) : tools/vmx_ecritures.py (stvlx/stvrx en une copie, 1 043 boucles d'octets, branché dans
   apres_codegen.py) ; src/od_matrices.cpp (produit natif, od_matrices_verif) ; puis clavier (src/od_clavier.cpp +
   point d'accroche à ajouter au SDK) ; puis réveils ciblés de WaitMissedCriticalSection dans le SDK (RtlLeave réveille
   aujourd'hui tous les dormeurs, à chaque libération de n'importe quel verrou).
   Fait (2026-10-16 soir) : vmx_ecritures (1 043 boucles remplacées) ; od_matrices vérifié sur PC (4,2 millions
   d'appels, 0 écart ; captures correctes) ; clavier : SetKeyboardProvider (include/rex/kernel/xam/keyboard.h du SDK,
   XamShowKeyboardUI l'appelle hors du fil du jeu, xeXamDispatchHeadlessEx ; annulation = X_ERROR_CANCELLED, nom
   inchangé) + src/od_clavier.cpp (swkbd de libnx ; PC : fenêtre ImGui du SDK) ; verrous : table de 16 sections
   attendues, RtlLeave ne réveille que pour l'une d'elles (ou si un dormeur n'a pas trouvé de place). Tout dans
   patches/sa2-nx-sdk.patch. .nro LTO + PGO avec tout cela en compilation (out/build/switch-pgo) : à essayer (essai de
   base pour les mesures, puis réglages du joueur pour le clavier au « New Game »).
   Essai du .nro de 16 h 58 (ELF opdarkness-20261016-1658-lto-pgo-etape2.elf, archive 1016-etape2) contre 16 h 30 :
   vmsObjectThread 13,7 -> 10,2 % (matrices), fil de dessin 28,6 -> 25,9 %, fil de lecture 14,6 -> 13,1 %, charge
   en combat 152-158 -> 132-143 %, à-coups de la cinématique 800 -> 750 ms ; mêmes images/s, aucune erreur.
   Clavier : validé par l'utilisateur sur la console (nom du joueur saisi au clavier de la Switch). Réglages du
   joueur sur la console (switch/opdarkness.toml). Suite possible : nouvelle collecte PGO avec le code de l'étape 2
   (fonctions modifiées sans profil) ; décodeur vidéo WMV (sub_823ACE18/ACAA8) ; mixage XACT ; cause du défaut de
   lecture (tampon nul).
   Vidéo et audio (demande de l'utilisateur : 2 et 3, puis nouveau PGO) : log10 du compresseur audio natif (od_log_natif,
   src/od_copies.cpp ; réservé à l'appel de sub_82335928, retour 823359D0 ; log de l'hôte : 1 ulp d'écart sur ~2 % des
   doubles, 0 sur le float utilisé, 33,5 millions d'appels vérifiés) ; vsubsws vectorisé (tools/vmx_ecritures.py, 375,
   formule testée sur 10 millions de cas) ; 8 nouvelles copies localisées (décodeur WMV 823ACAA8, 823ACE18, 823ABB38,
   823AA7F8, 823A7D10 ; mixage XACT 828A5210, 8288F270 ; compresseur 82335928). PC : vidéos, son et combat corrects.
   Ensuite : .nro instrumenté (OUT=out/build/switch-pgo-gen, LTO + OD_PGO=generate) pour une nouvelle partie de collecte.
   .nro instrumenté de 17 h 42 (ELF opdarkness-20261016-1742-pgo-gen.elf) envoyé, partie de l'utilisateur en cours
   (premier flux gardé dans out/pgo-flux/od_pgo_1.stream, retiré de la carte). Pour la fusion : mettre de côté out/pgo
   (premier profil) avant tools/switch/pgo_fusion.sh. tools/pgo_classement.py nomme aussi les copies localisées et
   donne le total des autres fichiers (le rendu natif pèse près de la moitié des arcs : piste suivante) ; il faut le
   code généré EXACT du .nro instrumenté (ne pas relancer le générateur ni apres_codegen.py entre la collecte et
   l'analyse : les numéros de ligne font partie de la somme de contrôle).
   Seconde collecte (35 min, archive 1016-pgo-gen2, flux out/pgo-flux2 ; premier profil gardé dans out/pgo-1) ->
   out/pgo. Classement : matrices 12,5 % -> 0,3 % (od_matrices.cpp), décodeur WMV sorti du haut ; le rendu natif fait
   51 % des arcs (sa2_native_targets, sa2_native_draws, me_native_system). .nro LTO + PGO n°2 de 18 h 34 (ELF
   opdarkness-20261016-1834-lto-pgo2.elf) : essai (archive 1016-lto-pgo2) contre 1016-etape2 : fil de dessin 25,9 ->
   24,6 %, lecture 13,1 -> 12,2 %, Audio Worker 10,8 -> 8,6 %, combat 132-143 -> 127-134 % ; rendu inchangé (28,8 %) ;
   mêmes images/s, aucune erreur. Le fil du rendu natif devient la cible.
   Rendu (profil console du fil « GPU ring native », .nro 16 h 58) : Draw 10,9 %, RexGmQueryProtect 6,1 % (estimateur
   NativeDrawExtentEstimator::PhysicalReadable : sur la Switch encore QueryProtect, PC et Windows passent par la table
   des pages du tas physique), XXH3 6,1 % (empreintes des textures), EDRAM4 (Synchronize, RedirectClearDepth, Publish,
   PlanClear) ~14 %, memmove 6 %.
   Passe sur le rendu (2026-10-16 soir, perf sur PC : kernel.perf_event_paranoid=1 réglé par l'utilisateur) :
   estimateur : table des pages aussi sur la Switch ; indices 16 bits avec redémarrage (0xFFFF, le cas
   d'Operation Darkness) par la voie rapide (od_indices16_restart, vérif od_indices16_restart_verif : 524 288 dessins,
   0 écart ; Draw 17,2 -> 12,0 % du fil sur PC) ; EDRAM : pré-parcours des propriétaires dans SynchronizeEDRAM4
   (od_edram4_prescan, vérif od_edram4_prescan_verif), table de versions cherchée une fois par PublishEDRAM4, plus de
   TileUsedEDRAM4 par tuile hors clear_overwrite (vérifié, 0 écart). Restent sur la console RedirectClearDepthEDRAM4
   (4,8 %) et PlanClearColor0EDRAM4 (1,6 %), faibles sur PC. XXH3 : appelants incertains (dédoublonnage de Draw,
   textures, reconnaissance des shaders ; perf --call-graph dwarf ne déroule qu'une petite part des piles).
   .nro LTO + PGO de 20 h 04 (ELF opdarkness-20261016-2004-lto-pgo2-rendu.elf, archive 1016-rendu) contre 1016-lto-pgo2 :
   échantillons actifs du fil du rendu 810 -> 609 (-25 % de travail réel ; QueryProtect disparu, SynchronizeEDRAM4
   7,2 -> 4,9 %, PublishEDRAM4 sorti du haut ; 47 665 synchronisations sur 53 268 finies au pré-parcours) mais charge
   affichée inchangée (~29 %) : le temps libéré part en attente active, WAIT_REG_MEM revérifié toutes les 50 µs
   (me_native_system.cpp, rex::thread::Sleep(50 µs)). À-coups de la cinématique 783 -> 734 ms, pire 183 -> 150 ms ;
   mêmes images/s, aucune erreur. Suite possible : savoir ce que ces WAIT_REG_MEM attendent (vblank -> dormir jusqu'à
   l'événement ; valeur écrite par le jeu -> autre réveil).
   Fait : toutes les longues attentes sont le drapeau 1FC9B006 (passe de 1 à 0 au retour d'écran : la routine de retour
   d'écran du jeu l'efface), déjà appris par le rendu (flip_poll_, « the flip wait polls »). od_flip_wait_sleep : le fil
   des retours d'écran signale chaque Interrupt(0, 2) fini (NoteVblankDone), l'attente apprise dort dessus (2 ms au plus,
   compteur lu avant le drapeau : pas de réveil perdu). PC sans lockstep : 2 à 6 sommeils par image, mêmes images/s,
   aucune erreur. .nro de 20 h 29 (ELF opdarkness-20261016-2029-lto-pgo2-flip.elf, archive 1016-flip) contre
   1016-rendu : fil du rendu 29,7 -> 27,4 %, combat 127-136 -> 124-131 %, à-coups de la cinématique 734 -> 671 ms ;
   ~5 sommeils par image (attente de ~10 ms, limite de 2 ms) ; mêmes images/s, aucune erreur. Le reste de la charge
   du fil du rendu est du travail (3,5 à 8,5 ms par image selon la scène).
3. PGO (sa2-nx : src/sa2_pgo.cpp, fusion des profils, ~+15 %) ; FFmpeg (décodeur XMA, 25 % de la console).
4. Étape D à réessayer une option à la fois seulement si la cause des plantages est comprise.
5. Données sur la Switch : paquet sans MOVIE/ si les ouvertures de fichiers ralentissent. PC : au-delà de la mission 1.
6. deko3d écarté le 2026-10-16 (docs/OPTIMISATION.md « deko3d ») : NAK et uam font jeu égal. Pistes nées de la
   mesure, traitées : tampons uniformes déjà utilisés sur la console ; bit 64 corrigé (flou d'origine -40 % sur PC) ;
   registres de NAK laissés (modifier Mesa).
   Essais de stabilité du 2026-10-16 (archives 1016-stabilite, 1016-lecture, 1016-lecture2) : le plantage revient
   **sans l'étape D** (elle n'était pas la cause). Cause : une fin de lecture du fil readDeviceThread avec un tampon
   nul (fichiers 2D\ICON\*.cel au démarrage). sub_82330CC8 (boucle du fil) appelle sub_82330990 (ouvrir, taille,
   allouer, lire, décompresser ; jusqu'à 64 essais) puis la fonction de fin de la requête ; sub_82330990 ne rend un
   tampon nul en « succès » que si l'allocation a échoué : hypothèse = GetFileSize (sub_82398810) en échec ->
   allocation de 0xFFFFFFFF -> tampon nul, ReadFile de 0xFFFFFFFF octets pris pour une fin de fichier réussie.
   Parades (src/od_lecture.cpp) : sub_8232C460 ignore un bloc nul (plus de plantage, mais un démarrage est resté sur
   un écran noir), fin de lecture sans tampon sautée, journal des échecs de ReadFile et de GetFileSize, GetFileSize
   rappelé 5 fois s'il échoue. Sur la console : .nro de 9 h 28 (avec l'icône, ELF opdarkness-20261016-0928.elf),
   essai de base. Essai du .nro de 9 h 28 (archive 1016-getfilesize) : 8 démarrages sans aucun défaut (ni tampon
   nul, ni GetFileSize en échec, ni plantage) : hypothèse ni confirmée ni infirmée (défaut ~1 sur 6-7 avant) ;
   continuer à chercher « GetFileSize », « [lecture] fin de lecture » dans les prochains journaux. Bruit : les
   lectures asynchrones audio rendent 0x26 (EOF) sur la console, 35 lignes « ReadFile du jeu en échec » par
   lancement, sans conséquence.

### Pièges rencontrés (ne pas les refaire)
- (2026-10-16) Ne jamais lancer le jeu (captures, rejeux, mesures) pendant que l'utilisateur joue sur le PC
  (./jouer.sh) : deux instances se gênent, et tools/captures*.sh effacent opdarkness.log, que sa partie écrit
  (récupérable tant qu'elle tourne : cp /proc/<pid>/fd/<n> où <n> pointe sur « opdarkness.log (deleted) »).
- Le jeu attend sans rien faire (aucun thread occupé) : Sleep(0) du moteur (threads « vms »), voir
  src/od_sommeil.cpp ; diagnostic --od_sleep0_stats (comptes par adresse de retour), tools/piles.sh +
  tools/piles_resume.py (piles de tous les threads sous gdb), tools/mesure_combat.sh "opts1" "opts2" (images/s et
  cœurs en combat, ~5,5 min par jeu d'options : ne rien compiler pendant).
- Lancer la build sa2sdk avec LD_LIBRARY_PATH=~/workspace/sa2-nx/sdk/out/linux-amd64 (sinon « undefined symbol »
  ou librexruntime.so introuvable). Sous gdb aussi (tools/piles.sh le fait).
- Sous Linux, rex::memory::QueryProtect relit /proc/self/maps : ne jamais l'appeler dans un chemin chaud
  (docs/RENDU_NATIF.md).
- Ne pas lancer codegen pendant une compilation (fichiers réécrits en cours de route).
- find_gaps.py de sa2-nx : faux trous sur les tables de saut placées dans le code ; utiliser tools/trous.py.
- pgrep -f attrape le « timeout » qui enveloppe le jeu : utiliser pgrep -x opdarkness. pkill -f / pgrep -f d'un
  motif présent dans la commande elle-même tue le shell qui la lance (code 144).
- Options du générateur (codegen_flavor.toml) : refaire l'audit read_before_write.py sur le code généré avec
  l'option ; vérifier la vidéo d'introduction (décodeur vidéo qui sort de la convention d'appel) ; bissection
  fonction par fonction (docs/OPTIMISATION.md, 1 ter). Le générateur est celui de sa2-nx **avec
  patches/sa2-nx-sdk.patch** (option non_volatile_vectors_as_local).
- Essais : --fullscreen=false (fenêtre de 1280x720 au lieu du plein écran) ; tools/captures.sh capture la fenêtre
  du jeu et non le cadre du gestionnaire de fenêtres.
- Comparer deux configurations du rendu (avec / sans paquet, DXC / glslang) **à image exacte** : rejeu en lockstep
  (tools/captures_images.sh + out/entrees/lockstep-test.txt, images 12200-12600 = cinématique puis menu d'action
  d'Edward ; le rejeu dérive parfois de quelques centaines d'images, regarder les images). Jamais à des instants du
  scénario rapide. Faire les caches de comparaison depuis zéro (un cache du scénario rapide est incomplet).
- Bissections de shaders : tools/paquet_vers_cache.py (entrées du paquet -> .odsh), caches mélangés, desassembleur
  et validateur SPIR-V compilés avec libSPIRV-Tools.a (voir docs/SHADERS_EN_JEU.md).
- Sur la console, un shader traduit en jeu peut manquer quelques instants (traduction de 0,1 à 0,6 s) : regarder
  une capture plus tard avant de conclure.

- (2026-10-14) Essais console : le scénario de manette suit l'horloge, il passe par les mêmes écrans jusqu'à ~140 s
  puis peut diverger ; comparer seulement les mêmes écrans (captures aux mêmes instants) ou des moyennes par fil sur
  la partie stable (rex_profile.log, intervalles 12-30). Les cinématiques choisissent leurs plans de caméra
  différemment d'un lancement à l'autre : comparer deux rendus dans la même image (od_flou_profondeur = 2 a servi).
- (2026-10-14) Sur PC, le GPU mesuré (gpu_ns_) reste sous 30 % même quand les images/s baissent : le PC n'est pas
  représentatif des limites GPU de la console ; mesurer le GPU sur la console (sa2_gpu_marks_categories = true).
- (2026-10-14) sub_82330828 n'est pas une lecture mais une décompression « si besoin » (rend 0 pour les données non
  compressées) : ne pas l'intercepter pour relancer des lectures.
- (2026-10-14) Outils de sa2-nx écrits pour Windows : find_self_loops.py a un chemin de nm en dur (copie avec
  NM = '/usr/bin/llvm-nm-20') ; switch_profile_report.py : DEVKITPRO=<dossier avec devkitA64/bin/aarch64-none-elf-nm
  -> llvm-nm-20> ; localize_native.py --list écrit dans l'arborescence de sa2-nx (tools/apres_codegen.py l'appelle
  fonction par fonction avec --out).
- (2026-10-14) Toute nouvelle interception d'une fonction du jeu (REX_HOOK_RAW) : relancer tools/apres_codegen.py
  (appels directs refaits) avant de compiler ; la compilation vérifie « interceptées mais appelées directement : 0 ».

### Historique
- 2026-10-07 : projet initialisé, codegen propre, PC (GPU émulé) jusqu'à la mission 1 ; guide de sa2-nx intégré au
  plan ; audits du code (PLAN étape 1) ; voie A choisie par l'utilisateur ; rendu natif de sa2-nx intégré, premier
  rendu natif (titre, intro, mission 1).
- 2026-10-08 : lenteur générale (~8 images/s) = Sleep(0) du SDK ; anneau du rendu 10x plus rapide (QueryProtect) ;
  Sleep(0) par site d'appel : combat à 60 images/s ; ./jouer.sh pour que l'utilisateur joue.
- 2026-10-08 (soir) : enregistrement/rejeu des entrées ; session de l'utilisateur jusqu'à un plantage à l'attaque
  d'Edward ; traces de plantage (journal vidé chaque seconde, src/od_plantage.cpp) ; paquet à 97 shaders.
- 2026-10-09 : plantage de l'attaque = fonction manquante 0x8225F300, corrigé et vérifié par rejeu ; traduction
  des shaders en jeu ; optimisations (options du générateur A/B/C, Sleep(0) de courtoisie) ; début de la Switch :
  .nro, traduction des shaders sur la console par glslang (optimiseur SPIRV-Tools), corrections du rendu sans paquet
  (rectangles, ordre des shaders ajoutés, variantes enveloppées).
- 2026-10-10 : Switch : carte, décor et personnages affichés (7 fonctions du pilote coupées).
- 2026-10-13 : coupables = 2 raccourcis NVK (set4_differences, nvk_emission) ; boutons selon leur lettre ; scénario
  avec B ; améliorations de sa2-nx enfin actives ; profil console : cinématiques limitées par le GPU (conversions
  EDRAM de profondeur) ; choix de l'utilisateur : 720p partout, optimiser pour ça.
- 2026-10-14 : conversions EDRAM des cinématiques supprimées (preuve d'écrasement en mode 5, commit 833e8d0) ; ZCULL
  1x sans gain ; temps GPU par passe ; sa2_gpu_boost (GPU 307 -> 460,8 MHz en portable) ; flou de profondeur de champ
  allégé (od_flou_profondeur, captures côte à côte validées) ; LTO abandonné (2 plantages) ; copies identiques sautées
  (sa2_native_skip_identity_copy) ; audio : attente active sub_82890C30 (version qui cède : plus coûteuse au total,
  éteinte), copies localisées (src/copies_localisees.txt) ; étape D du générateur faite puis retirée (plantages) ;
  définition dynamique 720/540/360 (sa2_render_dynamic) ; parade du chargeur (src/od_lecture.cpp).
- 2026-10-16 : deko3d écarté (uam = NAK) ; défaut de lecture (tampon nul) paré, cause inconnue ; à-coups de la
  cinématique (contrôle du désentrelacement coupé, changement de définition sans attente) ; rendez-vous audio
  bloquant ; LTO + PGO (outillage, deux collectes, classement des fonctions par le profil) ; écritures stvlx/stvrx en
  une copie, vsubsws vectorisé, produit de matrices natif, log10 natif du compresseur, copies localisées du décodeur
  WMV et du mixage XACT ; clavier de la Switch ; rendu : table des pages, indices avec redémarrage, EDRAM, attente du
  retour d'écran endormie ; arme invisible du tireur attribuée à un shader traduit par la console.
