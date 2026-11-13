#!/bin/bash
# Prépare le dossier de la carte SD : out/switch-sd/switch/operation-darkness-nx/ (operation-darkness-nx.nro ; avec PAQUET=1 sa2_shaders.mesp de la build PC ;
# avec CACHE_PC=1 cache_shaders.odsc = shaders traduits en jeu sur PC (sinon la console traduit elle-même ce que le
# paquet ne connaît pas, et remplit son propre cache_shaders.odsc), données du jeu : dossier game_root/ copié depuis assets/
# par console.sh envoyer-jeu). Garde l'ELF non strippé dans out/switch-elf/
# (symboliser les plantages). Données dérivées du jeu : jamais versionnées ni distribuées.
set -e
cd "$(dirname "$0")/../.."
RACINE="$PWD"
OUT="${OUT:-$RACINE/out/build/switch}"
PC="$RACINE/out/build/sa2sdk"
SD="$RACINE/out/switch-sd/switch/operation-darkness-nx"
mkdir -p "$SD" out/switch-elf
cp "$OUT/opdarkness.nro" "$SD/operation-darkness-nx.nro"
cp "$OUT/opdarkness" "out/switch-elf/opdarkness-$(date -r "$OUT/opdarkness" +%Y%m%d-%H%M).elf"
# Pas de paquet de shaders : la console traduit tout elle-même (PAQUET=1 copie celui de la build PC).
rm -f "$SD/sa2_shaders.mesp"
[ "${PAQUET:-0}" = 1 ] && cp "$PC/sa2_shaders.mesp" "$SD/"
rm -f "$SD/cache_shaders.odsc"
[ "${CACHE_PC:-0}" = 1 ] && python3 -I tools/switch/regrouper_cache.py "$PC/cache_shaders" "$SD/cache_shaders.odsc"
# Données du jeu : le dossier game_root/ de la carte = copie de assets/ (pas de paquet : plus de 4 Go, limite de FAT32 ;
# switch/defauts.toml). tools/switch/console.sh envoyer-jeu le copie depuis assets/ directement.
ls -la "$SD"
