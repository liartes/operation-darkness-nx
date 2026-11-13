# Rendu natif : journal et écarts avec sa2-nx

Le rendu (src/me, src/cpu/sa2_enhancements.*) vient de sa2-nx par tools/import_rendu_sa2.sh (commit dans
src/me/SA2_NX_COMMIT). Toute modification de ces fichiers est listée ici, pour la réappliquer après une
resynchronisation (et la proposer à sa2-nx).

## Écarts avec sa2-nx
- `src/me/me_native_draw_extent_estimator.h`, `PhysicalReadable` : la branche Windows (table des pages du tas
  physique) sert aussi sous Linux. Sous Linux, `rex::memory::QueryProtect` relit et analyse tout /proc/self/maps à
  chaque appel : ~90 % du fil de l'anneau en combat (perf, 2026-10-08 : FindEntryForAddress 27,5 % de tout le
  processus). sa2-nx avait corrigé le même problème sous Windows (VirtualQuery, 87 % du fil de l'anneau).

- Traduction des shaders au fil du jeu (2026-10-09, docs/SHADERS_EN_JEU.md) :
  - `src/me/native/sa2_native_shaders.{h,cpp}` : `ShadersNative::Add` ; `Data::inputs` devient une `std::deque`
    (adresses stables), shaders ajoutés dans `Data::added` et `added_per_fingerprint`, verrou `std::shared_mutex`
    (partagé dans Identify, IdentifyReordered, IdentifyContainer, PerNumber, PerFingerprint ; exclusif dans Add,
    qui vide les caches d'identification) ; IdentifyContainer cherche aussi dans les ajouts.
  - `src/me/me_native_system.{h,cpp}` : `me::native::ShaderKnown` et `me::native::AddShader` (fin du fichier),
    `g_library_rw` (bibliothèque modifiable, posée même sans paquet), verrou `g_library_by_code_mutex` autour des
    lectures de `g_library_by_code` (ByCode, NoteShaderObject) et de l'ajout.

  - `src/me/me_native_system.{h,cpp}` (variantes et chargements immédiats, 2026-10-09) : `HandOverVertexVariant`
    (appelée en tête de DumpLoadedVertexVariant) et `me::native::SetVariantHandler` ; dans DumpMissingImmediate,
    `g_immediate_handler` (me::native::SetImmediateHandler) appelé même sans dossier de vidage, et nouvel essai
    d'un programme resté sans modèle quand la bibliothèque a grandi (`g_library_generation`, augmentée par
    AddShader) ; `WrapUnknownVertexShader` (vertex shader inconnu depuis 2 s enveloppé par registres) ;
    `g_library_ready_handler` (me::native::SetLibraryReadyHandler) appelé après le chargement du paquet.

- `src/me/me_native_system.cpp`, `ProveFullOverwrite` (2026-10-14) : en mode profondeur seule (RB_MODECONTROL = 5),
  le test alpha et l'alpha-to-coverage ne rendent plus le dessin « killable » : aucun pixel shader n'y tourne (le
  rendu le dessine sans, sa2_native_draws.cpp). Operation Darkness efface la profondeur de la scène des cinématiques
  par un rectangle dans la vue 4x (640x360, base 2D0) en mode 5, test alpha resté actif : la preuve refusée
  (rejets « depthstencil ») empêchait RedirectClearDepthEDRAM4 et coûtait deux conversions de toute la cible par
  image (« depth-repr-switch », 1280x720 2x <-> 1280x1280 1x, 720 tuiles, stencil en 9 passes).

- Copies identiques (2026-10-14, sa2_native_skip_identity_copy, vrai par défaut) : un dessin plein écran opaque qui
  recopie dans une cible la copie (resolve) qu'on vient d'en faire, sans rien entre les deux, réécrit les mêmes
  pixels : il est sauté. Operation Darkness recopie ainsi le résultat de son flou de profondeur de champ dans la
  cible qui le contient encore (puis l'efface !), à chaque image. Preuve :
  - `src/me/me_native_draw_extent_estimator.h` : l'estimateur lit aussi les attributs 8:8:8:8 (couleur des sommets,
    sa2_native_estimator_byte4) et relève l'interpolant 1 et x/y de l'interpolant 0 (Diagnostics::interpolator1,
    interpolator0_xy_valid) ;
  - `src/me/me_native_system.cpp` ProveIdentityCopy : couleur (interpolant 1) = 1 aux trois sommets, coordonnées de
    texture = centres des texels pour chaque centre de pixel -> SubmissionDraw::identity_copy_size
    (`src/me/native/sa2_native_draws.h`) ;
  - `src/me/native/sa2_native_targets.cpp` NoteIdentityCopy / SkipIdentityCopy / NoteDrawAlphaOne : copie de couleur
    1x 8:8:8:8 sans effacement, puis le dessin suivant (aucun autre dessin ni copie entre) : pixel shader de la liste
    sa2_native_identity_copy_ps (texture x interpolant 1), mélange opaque, masque F, ni profondeur ni test alpha, même
    cible, texture = destination de la copie (même taille, sans signes ni exposant), rectangle prouvé couvrant tout ;
    canaux identiques, ou alpha forcé à 1 si le dernier dessin avant la copie, plein écran opaque sur cette cible,
    venait d'un shader de sa2_native_alpha_one_ps (le flou de profondeur de champ écrit alpha = 1).
  La seconde recopie du jeu (après son flou qui préserve les contours) force l'alpha à 1 sur un alpha calculé : pas
  identique, gardée.

- Définition dynamique (2026-10-14, sa2_render_dynamic, sa2_render_height_dynamic ; activée sur la Switch par
  switch/defauts.toml) : `src/me/sa2_render_mode.{h,cpp}` NoteFrameGpu + WantedScaleQuarters (540p tant que le GPU ne
  tient pas 60 images/s : < 57 images/s et GPU occupé > 90 % sur 30 images ; retour quand le temps GPU x rapport des
  surfaces tiendrait dans 90 % ; 2 s au moins entre deux changements), appelé par `sa2_native_targets.cpp` après chaque
  image présentée avec gpu_ns_ (x1,627 sur la Switch). Sur PC le GPU mesuré reste sous 30 % (limité ailleurs) : la règle
  ne s'y déclenche pas ; changement en direct vérifié par sa2_render_test_toggle_* (5 bascules 720/540 pendant la
  cinématique, images correctes). Paliers (même jour) : hauteur du mode, sa2_render_height_dynamic (540), puis
  sa2_render_height_dynamic_min (360) si 540p décroche encore ; remontée palier par palier ; 360p vérifié de même
  (6 bascules, images correctes).

- Bit « pas de texture signée » (sa2_gpu_no_texture_signs, 2026-10-16) : `src/me/native/sa2_native_draws.cpp` pose
  kSpecNoTextureSigns quand aucune composante n'est biaisée (2) ou en gamma (3) (`(draw_signs & 0xAA) == 0`) au lieu
  de « aucun signe » : le signé (1) est pris par le format de l'hôte et masseffectApplyTextureSigns ne le transforme
  pas. Seul cas du jeu relevé : le flou de profondeur de champ (texture 2, signes 0x55) ; images identiques au pixel
  près (rejeu lockstep-test, flou d'origine) ; PC : 5,3 -> 3,2 ns par pixel pour ce shader.

## Journal
- 2026-10-07 : intégration, premier rendu (titre, introduction, mission 1).
- 2026-10-09 : traduction en jeu (src/od_traducteur.cpp) : 44 shaders traduits sans paquet (~64 ms chacun), SPIR-V
  identique octet pour octet à celui du paquet.
- 2026-10-09 : variantes et chargements immédiats traduits en jeu (src/od_variantes.cpp) : avec un paquet réduit
  aux 17 shaders de Direct3D, 80 shaders traduits, 6 variantes réparées, 2 chargements immédiats ; dessins sautés
  ramenés de 1,7 million à 2 305, tous dans les premières secondes.
- 2026-10-08 : Sleep(0) du SDK (src/od_sommeil.cpp) : titre 27-28 images/s, combat ~20. Puis PhysicalReadable
  (ci-dessus).
- me_rectangle_spirv.h (ExpandRectangleShader), 2026-10-09 : déclarations ajoutées séparées en deux (types de
  pointeur privé avant la première variable, le reste avant la première fonction) pour accepter le SPIR-V de glslang
  (docs/SHADERS_EN_JEU.md, champ de bataille noir).
- ShadersNative::Add (2026-10-09) : l'entrée ajoutée prend sa place dans les listes de candidats dans l'ordre du paquet
  (empreinte du conteneur), et AddShader choisit l'entrée de l'index par code (g_library_by_code) comme
  BuildLibraryCodeIndex (premier dans l'ordre du paquet, sauf mieux relié aux registres : RegisterLinkScore) : le
  choix ne dépend plus de l'ordre des traductions. Sans cela, avec les 109 entrées du paquet passées par le cache,
  Edward n'était pas dessiné.
- 2026-10-14 : effacement de profondeur en mode 5 accepté par la preuve (ci-dessus). Rejeu lockstep-test, cinématique
  du combat (images 11000-12450), PC : conversions « depth-repr-switch » 800 par 10 s (580 000 tuiles) -> 0,
  edram_import 1,0-2,5 ms -> 0 ms par image, 37-42 -> 45-53 images/s. Images : 12600 identique au pixel près, 13000
  seulement la barre de boutons qui clignote ; dans la cinématique, décalage d'animation seulement (le titre de
  mission suit l'horloge réelle : le rejeu plus rapide y arrive plus tôt), meilleure paire 0,45 % de pixels écartés
  de plus de 16, sur les contours des personnages en mouvement (out/captures/edram-avant, edram-apres, edram-apres2).
- 2026-10-14 : copies identiques sautées. Rejeu lockstep-test (images 9000, 12600-15000) : images identiques au pixel
  près avec et sans ; ~1 copie sautée par image ; passes sans profondeur 5,6 -> 4,1 ms par image (PC, tutoriel).
