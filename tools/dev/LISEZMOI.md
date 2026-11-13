# Outils de diagnostic (session du 2026-10-09 au 2026-10-13)

Petits programmes écrits pendant les recherches, à compiler à la demande (clang++-20 ; la commande est en tête de
chaque fichier).

- empreintes.cpp : XXH3 du microcode de chaque .odsh (mots dans l'ordre de l'hôte) = « fingerprint » des lignes
  « VS nN fingerprint X » du journal du rendu (ex. « GPU time by pass », « C6: pipeline ... »).
  `clang++-20 -O1 -std=c++17 empreintes.cpp -I ~/workspace/sa2-nx/sdk/thirdparty/xxHash -o empreintes`
  puis `./empreintes <cache>/*.odsh`.
- dis.cpp : désassembleur SPIR-V d'un .odsh ou d'un .spv.
- rect.cpp : applique la conversion des listes de rectangles du rendu (src/me/native/me_rectangle_spirv.h) au SPIR-V
  d'un vertex shader et le fait valider par SPIRV-Tools.
- HLSL d'un shader : `out/build/sa2sdk/od_hlsl <sortie> <fichier.odsh>` (cible od_hlsl de la build PC ; `--glslang`
  et `--prelude shaders/shader_common_glslang.h` pour la version préparée pour glslang).
- Profil de la console : tools/switch_profile_report.py de sa2-nx cherche aarch64-none-elf-nm sous
  $DEVKITPRO/devkitA64/bin : créer un dossier <d>/devkitA64/bin avec un lien aarch64-none-elf-nm -> /usr/bin/llvm-nm-20
  et lancer avec DEVKITPRO=<d>.
- Perdus avec le dossier temporaire (méthode décrite dans docs/OPTIMISATION.md 1 ter et docs/SHADERS_EN_JEU.md, à
  réécrire au besoin) : le mélange fonction par fonction du code généré de deux variantes du générateur, et les
  bissections de shaders par rejeu à image exacte (cache mélangé, tools/paquet_vers_cache.py,
  tools/captures_images.sh + out/entrees/lockstep-test.txt).
