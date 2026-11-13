# Optimisation : remplacer le code hérité de l'émulation par du code natif (étude du 2026-10-09)

Guide de sa2-nx §9-10. Sur PC le jeu tient déjà ses 60 images/s en combat ; le but est la Switch (3 cœurs A57 à
1 GHz, bien plus lents qu'un cœur d'i5) : ce qui compte est le temps processeur de chaque fil, surtout celui qui
dessine (vmsDrawThread), pas le total (les fils qui cèdent en boucle prennent tout le processeur libre).

Outils : tools/mesure_fils.sh "opts1" "opts2" (occupation par fil en combat, scénario rapide) ; perf
(kernel.perf_event_paranoid=1) sur le combat : `perf record -F 1999 -g -p <pid>` vers 100 s du scénario rapide.

## Mesures (i5-8250U, rendu natif, combat, 60 images/s)
| Fil | Avant | Après l'attente du GPU qui dort |
|---|---|---|
| vmsDrawThread (dessin, logique d'image) | 101 % | 37 % |
| vmsObjectThread | 18 % | 28 % (cède en boucle : prend le processeur libéré) |
| autres fils du moteur (XThread…, readDevice) | 8-12 % chacun | 10-14 % |
| anneau du rendu natif | ~13 % des échantillons | ~18 % |

## 1. Fait : l'attente du GPU de Direct3D (src/od_attente_gpu.cpp)
sub_82349A30 (« le GPU a-t-il avancé ? ») et ses boucles (sub_82345720, attente de place dans le tampon de commandes ;
Swap) prenaient ~18 % du processus : le fil qui dessine tournait en attendant l'anneau. Il dort maintenant jusqu'au
progrès de l'anneau (me::native::WaitRingProgress) : 101 % -> 37 % d'un cœur.

## 1 bis. Fait : memcpy et memset natifs (src/od_crt.cpp, 2026-10-09)
sub_826A4FC0 (memcpy, copie vers l'avant, r3 rendu tel quel) et sub_826A54F0 (memset) par std::memmove / std::memset
de l'hôte ; une copie dont la destination chevauche la source plus loin (seul cas où « vers l'avant » diffère de
memmove) est laissée à l'original. Profil en combat (perf, 20 s) : memset + memcpy du jeu 2,0 % du processus avant,
~0,4 % après (5 fois moins). Aucun chevauchement vu en combat. --od_crt_natif=false rend les originaux.
Mesure par fil trop bruitée pour ce gain (±10 % d'un lancement à l'autre selon le moment du combat) : comparer les
parts du profil perf.
Piège rencontré : « Erreur du bus » au lancement = /dev/shm plein de fichiers xenia_memory_* laissés par les arrêts
brutaux ; tools/nettoyer_shm.sh (appelé par tous les scripts de lancement) supprime ceux qu'aucun processus n'ouvre.

## 1 ter. Fait : options du générateur (codegen_flavor.toml, 2026-10-09)
Mesure : tools/mesure_instr.sh (instructions par seconde du fil qui dessine, combat du scénario rapide, 60 images/s ;
écart entre deux lancements < 0,5 %).
| Code généré | Instructions/s du fil qui dessine |
|---|---|
| départ | 75 M |
| étape A : cr, ctr, xer, réservation en variables locales ; skip_msr, dform_disp_split, fctiwz_inline, fp_moves_modeless | 67,1 M (−10,5 %) |
| étape B : + registres non volatils entiers et flottants en variables locales (non_volatile_as_local), vecteurs laissés dans le contexte | 58,5 M (−22 % au total) |

Étape B, ce qui a cassé et pourquoi :
- La vidéo d'introduction sortait brouillée (vert et magenta). Cause : le décodeur vidéo du jeu (bibliothèque de
  Microsoft, 0x8277-0x8291) charge des constantes dans les registres vectoriels v64-v72 (sub_8291E7B8,
  sub_8291EB00) et ses fonctions de calcul vectoriel écrites à la main les lisent ensuite (sub_82902A40 : v64, v65 ;
  sub_8247B6A8, sub_8247BCE0, sub_82901090, sub_82901438, sub_829016F8, sub_82901960 : v69, v72), hors convention
  d'appel. En variables locales, ces registres valent 0 à l'entrée de chaque fonction. Correction : option
  `non_volatile_vectors_as_local = false` ajoutée au générateur de sa2-nx (patches/sa2-nx-sdk.patch) : vecteurs dans
  le contexte avec leurs sauvegardes __savevmx / __restvmx ; le gain vient surtout des registres entiers.
- Trouvé par bissection **fonction par fonction** : code des deux formes généré dans out/genA et out/genB (même
  découpage en fichiers), puis chaque fonction prise dans l'une ou l'autre selon son adresse ; les fonctions d'aide
  __save* / __rest* toujours en forme A. Les deux formes s'appellent sans problème (chacune garde ses registres).
  Mesure du brouillage sur une capture à 50 s : part des pixels où le vert domine (g > r + 25 et g > b + 25) ; ne
  pas se fier à un seuil de luminosité, les fondus donnent des images brouillées sombres.
- L'audit tools/read_before_write.py de sa2-nx l'avait signalé, mais il doit tourner sur le code généré **avec**
  l'option : sur le code de l'étape A, il ne voit pas ces registres (écrits ctx.v64).
- Erreur à ne pas refaire : share_registers sur des fonctions ordinaires (essayé sur toute la bibliothèque) ; ses
  appels __savegprlr sont supprimés, elle écrase les registres de ses appelants (écran noir). Réservé aux fragments
  d'exception (4 dans la bibliothèque C : 0x826AA3A8, 0x826B4178, 0x826B70B8, 0x826B7A80).
- setjmp / longjmp (setjmp_address / longjmp_address) : non déclarés, leur déclaration brouillait aussi la vidéo,
  sans explication (jamais appelés pendant le scénario, src/od_setjmp.cpp) ; opdarkness_overrides.toml.

Étape C (fait, 2026-10-09) : tools/apres_codegen.py, lancé par la compilation après chaque génération (CMakeLists.txt,
add_custom_command APPEND) : accès mémoire sans volatile + barrières de boucles (pch_no_volatile.py et
loop_barriers.py de sa2-nx, ensemble), appels directs __imp__sub_X pour les fonctions sans interception
(direct_calls.py ; interceptions = adresses 82xxxxxx du code de src/, liste out/crochets.txt). Vérification après
chaque édition des liens : aucune fonction interceptée (symbole sub_X fort) appelée directement.
Mesure : 58,4 M instructions/s (58,5 avant) : rien sur PC avec clang ; gardée pour la Switch (GCC : +15-20 %
d'images/s chez masseffect-nx). Vidéo et combat vérifiés.
Pas elide_lr_stores (src/od_sommeil.cpp lit ctx.lr) ; non_argument_as_local à voir (audit en mode --volatile).

**Où va le fil qui dessine (perf, instructions, combat, après l'étape C)** : 53 % dans le code du jeu, 25 % dans
librexruntime, 11 % libc, 5 % __tls_get_addr. En tête : sub_8239AF20 (Sleep, notre interception) 7 %, atomic_dec
5 %, pthread_cond_broadcast 4,5 %, WaitMissedCriticalSection 3,4 %, atomic_cas 3,4 %, RtlTryEnterCriticalSection
~5 %, RtlLeaveCriticalSection ~3 %, sched_yield, et des fonctions du moteur 0x8232C5B0-0x8232E018 (2-3,5 % chacune).
Le fil passe donc l'essentiel de ses instructions à **attendre des verrous en tournant** : le nombre d'instructions
par seconde mesure en partie de l'attente, et les options du générateur n'y peuvent plus grand-chose. Suite : point 3.

## 1 quater. Fait : Sleep(0) de courtoisie (src/od_sommeil.cpp, --od_sleep0_courtoisie, 2026-10-09)
~120 000 Sleep(0) par seconde en combat, dont ~75 % juste après un RtlLeaveCriticalSection (comptes de références
sub_8232CEC0 / sub_8232CF90, files sub_8232C5B0 / sub_8232C720...), presque tous sur le fil qui dessine : un
sched_yield chacun, plus WaitMissedCriticalSection. Reconnus au code (« bl RtlLeaveCriticalSection ; li r3,0 ;
bl Sleep » avant l'adresse de retour), ils rendent la main tout de suite.
| | images/s | processus | fil qui dessine |
|---|---|---|---|
| avant | 59,0 | 3,2 cœurs | 63 % d'un cœur, 58,6 M instructions/s |
| après | 59,7-60,0 | 2,2 cœurs | 14 % d'un cœur, 49,7 M instructions/s |
Répartition après (tools/mesure_fils.sh) : XThreadA9FFD6C0 27 %, XThreadAAFFE6C0 23 % (son ?), vmsObjectThread
20 %, opdarkness (fil principal : anneau du rendu natif) 16 %, readDeviceThread 15 %, vmsDrawThread 14 %.

Ce qui reste, par fil (perf, cycles, tout le processus, combat, 2026-10-09 après ce changement) :
- « GPU ring native » 22 % des échantillons : le rendu natif de sa2-nx (TileUsedEDRAM4, SynchronizeEDRAM4,
  PublishEDRAM4, Draw...) : travail réel, côté sa2-nx.
- readDeviceThread 12 % : groupe sub_823ACE18, sub_823ACAA8, sub_823ABB38, sub_823A7D10, sub_823AA7F8,
  sub_823A7468 : transformée en ondelettes entière en VMX (vaddsws, vsubsws, vsraw, vavgsw, vperm) : décodage
  d'images (HD Photo / JPEG XR ?) ; pas zlib (zlib 1.1.3 est ailleurs : sub_82377320, sub_82381190, sub_8268A1E0...).
  Travail réel ; le remplacer demanderait une réécriture exacte au bit près.
- deux XThread 11 % + 10 % (+ « Audio Worker » 9 %, « XMA Decoder » 3 %) : son. Code du jeu (sub_82890C30,
  sub_8289FF20, sub_8288F270, sub_828A5210 : XAudio/XACT) et décodage WMA de FFmpeg dans librexruntime
  (decode_frame, ff_imdct_half_c, vector_fmul_window_c : FFmpeg compilé sans assembleur x86 sur PC ; avec
  l'assembleur ARMv8 pour la Switch, thirdparty/CMakeLists.txt du SDK).
- vmsObjectThread 8 % (logique des objets : memset, fma, sub_82609788...), opdarkness 8 %, vmsDrawThread 7 %.

## 2. Profil restant (perf, 2026-10-09, tous fils)
Plat : aucune fonction du jeu au-dessus de 2,4 %.
- sub_826A6900 (2,4 %), sub_826A6A80 : dans le fil audio (pile sub_8289F308 > sub_82335928 > sub_826A6A80 >
  sub_826A6900) : calcul audio (mixage XACT ?) ; à identifier.
- sub_82890C30 (2,0 %), sub_8289FF20, sub_8288F270, sub_828A5210 : fils XThread du son (XACT / XMA) ?
- sub_826A54F0 = memset du jeu (2,0 % ; 1 040 appels dans 642 fonctions) ; sub_826A4FC0 = memcpy (1 645 appels).
- groupe sub_823ACAA8, sub_823ABB38, sub_823ACE18, sub_823A7D10, sub_823AA7F8, sub_823A7468 (~6 % à eux tous) : à
  identifier (zone 0x823A…, juste après Direct3D : D3DX ? XUI ?).
- __savegprlr_N / __restgprlr_N : ~7 % du fil qui dessine avant le point 1 : sauvegarde des registres à l'entrée et
  à la sortie de chaque fonction traduite.
- Rendu natif : TargetsVulkan::TileUsedEDRAM4, PublishEDRAM4, SynchronizeEDRAM4, XXH3 (empreintes) : < 1 % chacun.

## 3. Pistes, dans l'ordre du rendement attendu
1. **Options du générateur** (guide §9.1 ; étapes A et B faites, 1 ter), le plus gros levier pour la Switch, sur tout le code à la fois :
   registres non volatils en variables locales (non_volatile_as_local, non_argument_as_local : supprime les
   __savegprlr/__restgprlr et les accès au contexte autour de chaque instruction), suppression des écritures de lr,
   appels directs entre fonctions (tools/direct_calls.py de sa2-nx), en-tête précompilé sans volatile. Ces options
   existent dans le générateur de sa2-nx (app/codegen_flavor.toml chez eux). Préalable : l'audit
   tools/read_before_write.py (fonctions qui lisent un registre laissé par l'appelant : à marquer share_registers),
   puis vérification par captures à image exacte et par sessions rejouées.
2. **Fonctions de la bibliothèque C en natif** : fait (1 bis) pour memcpy, memset et memmove (sub_826A49E0). Voisines
   examinées et laissées : floor (sub_826A42E8) et ceil (sub_826A7448), sans branchement, rien à gagner ; atoi
   (sub_826A42D8, strtol base 10), hors du profil ; fonctions mathématiques sub_826A4900 et sub_826A6900 (2,4 %, fil
   audio) : la bibliothèque de l'hôte n'est pas exacte au bit près contre celle de la Xbox ; seule une réécriture
   de leur code PowerPC (guide §9.3, natives vérifiées) le serait.
3. **Fils du moteur qui cèdent en boucle** (vmsObjectThread, XThread…) : sur la Switch ils volent du temps aux fils
   utiles. Comprendre ce qu'ils attendent (src/od_sommeil.cpp a les sites ; --od_sleep0_stats) et les faire dormir
   sur un événement plutôt que céder.
4. **Fonctions chaudes en natif** (guide §9.3), une fois 1 fait (le profil change) : identifier sub_826A6900 et le
   groupe 0x823A…, puis copies localisées (tools/localize_native.py de sa2-nx) ou réécritures vérifiées au bit près.
5. **Direct3D remplacé** (guide §10) : seulement si l'anneau ou le fil qui dessine limitent encore sur la Switch.

## Améliorations du rendu de sa2-nx enfin actives (2026-10-13)
sa2::enh::Resolve() n'était jamais appelé (l'application de sa2-nx le fait dans OnPostInitLogging) : la quarantaine
d'améliorations du rendu importé (sa2_cpu_ring_*, sa2_cpu_direct_*, sa2_gpu_blend_skip, sa2_gpu_render_scale,
sa2_gpu_structured_cf...) et 14 réglages « externes » restaient éteints, sur PC comme sur Switch. Appel ajouté
(src/opdarkness_app.h) ; interrupteurs généraux sa2_cpu_enhancements / sa2_noncpu_enhancements. Vérifié sur PC à image
exacte (sans paquet, glslang) : images identiques hors animations et décalage du rejeu ; aucune différence signalée
par les contrôles internes. Découvert parce que sa2_render_height_handheld = 540 restait sans effet sur la console.


## Flou de profondeur de champ allégé (Switch, 2026-10-14)
Temps GPU par passe sur la console (720p, portable, 307 MHz, sa2_gpu_marks_categories) : dans les tutoriels,
19,8 ms par image dont 11 ms de passes sans profondeur ; la plus chère, ~4,7 ms, est le flou de profondeur de champ du
jeu (pixel shader 08fb9abd43fe7c0c, empreinte de microcode F424494A3251D489, dessiné en rectangle plein écran) :
5 points (le pixel et 4 voisins écartés selon la profondeur), à chacun profondeur (s3), image nette (s1) et image
floue (s2), mélange net -> flou selon |2 z - 1|, puis moyenne pondérée : 15 lectures de texture par pixel.
- od_flou_profondeur = 1 (src/od_traducteur.cpp) : point central seul, la formule exacte du shader pour ce point
  (3 lectures). Le HLSL traduit est réécrit à la traduction ; la version allégée n'est jamais écrite dans le cache
  (le shader du cache est ignoré au chargement et retraduit, ~0,1-0,6 s sur la console à la première apparition).
- od_flou_profondeur = 2 : comparaison, moitié gauche du jeu, moitié droite allégée, trait jaune (les plans des
  cinématiques changent d'un lancement à l'autre : deux lancements ne se comparent pas).
- Captures (rejeu lockstep-test, out/captures/flou-comparaison) : aucune différence visible sur les personnages ;
  bords des objets lointains (palmiers, horizon) un peu moins adoucis. Validé à l'œil nu par l'utilisateur :
  activé sur la Switch (switch/defauts.toml), pas sur PC.

## Processeur de la console : l'audio (2026-10-14)
Profil de la console (720p, portable, 127 % de charge sur 300 %, archive out/switch-logs-archive/1430-flou ;
symbolisation : tools/switch_profile_report.py de sa2-nx avec l'ELF du .nro testé) : environ la moitié de la charge
est l'audio. Fils de mixage XAudio (2 XThread, 12 % + 9 %), « Audio Worker » (11 %), décodeur XMA (26 %, FFmpeg).
- **sub_82890C30 = attente active** (~47 % des deux fils de mixage, ~10 % de la console) : rendez-vous des fils de
  mixage sur un mot de 64 bits (un octet par fil), boucle avec db16cyc sur la Xbox, à plein ici. Réécrite en natif
  (src/od_audio.cpp, od_audio_attente = 1) : mêmes calculs, mêmes accès mémoire, mêmes registres en sortie ; quelques
  tours avec l'instruction de pause, puis céder le processeur. PC : 1,74 % -> 0,13 % des échantillons. **Console :
  plus coûteux au total** (essais du 2026-10-14, tutoriel) : mixage 25,6 -> 18,3 %, mais fil qui dessine 7,8 -> 16,1 %
  et vmsObjectThread 10,0 -> 15,0 % (total 147 -> 158 %) : éteint par défaut (od_audio_attente = 0).
- **Copies localisées** (src/copies_localisees.txt, src/od_copies.cpp, od_copies_localisees) : log (sub_826A6900) et
  log10 (sub_826A6A80), ~58 % de « Audio Worker » (appelés pour chaque échantillon : enveloppe en décibels d'un
  compresseur ou limiteur) ; le rééchantillonneur VMX sub_8289FF20 (~17-22 % des fils de mixage) ; sub_82335ED0 et
  sub_82336B78 (Audio Worker). Produites par tools/apres_codegen.py dans generated/hot/ (localize_native.py de sa2-nx,
  --allow-double sûr avec -ffp-contract=off, --sync full : l'outil ne connaît pas nos interceptions). Vérification
  croisée des fonctions pures (od_copies_verif : copie puis original sur le même contexte, contexte entier comparé) :
  67 millions d'appels de log/log10, 0 écart. Sur PC, pas de gain mesurable (comme sa2-nx).
- **Console, tout le travail du jour sans l'attente qui cède** (archive out/switch-logs-archive/1830-audio-off) : 147 %
  au lieu de 152 % le matin (mixage 25,6 au lieu de 27,8 ; Audio Worker 10,6 au lieu de 11,4 ; anneau du rendu 24,7
  au lieu de 25,5) ; cinématique 39-41 images/s, reste à 60.

## Étape D : non_argument_as_local et elide_lr_stores (2026-10-14)
Les deux dernières options du générateur de sa2-nx (codegen_flavor.toml, explications dans le fichier).
- non_argument_as_local : r0, r2, r11, r12, f0, v32-v63 en variables locales. Audit tools/non_argument_audit.py de
  sa2-nx sans l'option (out/profil/audit_nonarg.txt) : 307 ENTRY (218 fonctions d'aide __save*/__rest*, gardées par
  nom ; fragments d'exception de la bibliothèque C, r12 ; quelques r11 ; le décodeur vidéo, v32-v63 entre ses
  fonctions), 54 RET (sub_82782EF8 rend r11 à 34 appelants...). Les 109 fonctions concernées sont dans
  non_argument_as_local_keep : elles gardent ces registres dans le contexte et le générateur les transmet autour de
  chaque appel. Avec l'option : HANDOVER 0, aucune fonction signalée hors de la liste ;
  read_before_write.py --volatile : 0.
- elide_lr_stores avec lr_keep_returns = les 759 adresses de retour des bl vers Sleep / SleepEx (od_sommeil.cpp lit
  ctx.lr) ; ctx.lr = ... : 13 503 écritures restantes dans le code généré.
- Vérifié sur PC : rejeu lockstep-test, images du jeu identiques au pixel près (9000, 12600-15000), vidéos propres
  (intro, film d'attente) ; copies localisées régénérées : 67 M de vérifications, 0 écart ; sites des Sleep(0)
  reconnus ; même charge des fils. Gain attendu sur la console (GCC) : sa2-nx mesurait -5,6 % d'instructions et
  -8,8 % d'accès mémoire dans les fonctions du jeu avec non_argument_as_local.

## deko3d : comparaison des compilateurs de shaders (2026-10-16)
Question de l'utilisateur : passer le rendu de Vulkan (NVK) à deko3d (point d'entrée : ProjectCane, portage de Sly
Cooper avec un sous-ensemble d'OpenGL réimplémenté sur deko3d, 8 shaders GLSL compilés par uam à la construction).
Ce que deko3d sait piloter, notre NVK le pilote déjà (compression des images de rendu : has_compression = true dans
nvkmd_switch_pdev.c ; ZCULL) ; le seul inconnu était la qualité du code des shaders : uam (ancien compilateur
nv50_ir de nouveau) contre NAK.
- uam (image devkitpro/devkita64, /opt/devkitpro/tools/bin/uam, -r = code Maxwell brut) sur 7 shaders chers :
  SPIR-V du cache -> SPIRV-Cross --vulkan-semantics -> adaptation (spécialisation de la console + bit 256 des
  tampons uniformes, lectures par adresse retirées, tableaux de textures -> poignées ARB_bindless_texture, que uam
  accepte) ; outils de la mesure non versionnés (dossier de travail de la session).
- NAK : statistiques de NVK lues sur la console (od_stats_nak, lignes « [nak] », archive
  out/switch-logs-archive/1016-nak ; vk_common_GetPipelineExecutableStatisticsKHR du NVK statique).
- Résultat (instructions / octets de code, NAK puis uam) : flou de profondeur de champ 1840/22784 - 2193/23424 ;
  flou qui préserve les contours 128/1376 - 118/1280 ; brume 39/416 - 46/512 ; matériaux de scène 257/2912 -
  246/2624, 220/2560 - 220/2368, 364/4000 - 353/3776 ; texture x couleur 38/416 - 44/512. Écarts de -19 % à +10 %
  dans les deux sens : **pas d'avantage pour uam**, alors que NAK compile en plus le chemin des lectures par adresse
  (moins favorable). Seule différence notable : registres du matériau le plus cher par pixel (0B8F92074BE4720D) :
  NAK 48 (40 warps/SM) contre uam 29 (64 warps/SM).
- Conclusion : pas de passage à deko3d (réécriture de ~36 000 lignes et de la chaîne des shaders sans gain GPU
  démontré). Pistes nées de la mesure, dans NVK : chemin des tampons uniformes pour les constantes (bit 256 de la
  spécialisation, absent sur la console) ; le flou de profondeur de champ d'origine est spécialisé sans le bit 64
  (« pas de texture signée ») sur la console, d'où ~2000 instructions de gestion des signes ; registres de NAK sur
  le matériau 0B8F92074BE4720D.
- Suite des 3 pistes (2026-10-16) : (1) constantes en tampons uniformes : déjà le cas sur la console (le prélude
  glslang fixe MASSEFFECT_UBO = 1 ; le SPIR-V du PC, compilé par DXC, gardait les deux chemins : fausse piste) ;
  (2) bit 64 : le flou n'avait que des composantes « signées » (0x55), que le shader ne transforme pas : règle
  corrigée (docs/RENDU_NATIF.md), flou d'origine 5,3 -> 3,2 ns par pixel sur PC, images identiques ; (3) registres de
  NAK : son ordonnanceur choisit déjà la cible de registres selon les paliers d'occupation (opt_instr_sched_prepass.rs) ;
  changer ce choix demanderait de modifier Mesa et de recompiler NVK pour quelques dixièmes de ms : non fait.
- Effet du bit 64 sur la console (essais du 2026-10-16 contre celui du 2026-10-14 à 19 h) : aucun écart mesurable sur
  la cinématique (720p : GPU 96-100 % des deux côtés ; 540p : passe principale 3900-4500 ms par 10 s des deux
  côtés). La console utilise le flou allégé (od_flou_profondeur = 1, 3 lectures au lieu de 15) : le code des signes
  retiré y pèse peu ; le gain ne vaut que pour le flou d'origine.
