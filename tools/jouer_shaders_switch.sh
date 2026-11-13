#!/bin/sh
# Joue sur PC (./jouer.sh : rendu natif, manette enregistrée) avec les shaders traduits par la Switch : sans paquet
# (SA2_SHADER_LIBRARY vers un fichier absent, comme la console), avec le cache de la console (cache_shaders.odsc de la
# carte SD, traduit par glslang sur la Switch) et le flou de profondeur allégé (od_flou_profondeur = 1, comme
# switch/defauts.toml). Les shaders que la console n'a jamais rencontrés sont traduits par DXC (fiables) et ajoutés à
# côté. Sert à savoir si un défaut d'affichage de la Switch vient de ses shaders (il apparaît ici aussi) ou de son
# pilote graphique (il n'apparaît pas). Exemple : l'arme invisible du tireur d'élite (CLAUDE.md, « Défauts
# d'affichage connus »).
# Usage : tools/jouer_shaders_switch.sh [cache .odsc de la Switch] [options du jeu]
#   défaut : out/shaders-switch/cache_shaders.odsc (copié depuis la carte : tools/switch/console.sh, ou
#   cp "<SD>/switch/operation-darkness-nx/cache_shaders.odsc" out/shaders-switch/)
cd "$(dirname "$0")/.."
SOURCE="out/shaders-switch/cache_shaders.odsc"
case "$1" in *.odsc) SOURCE="$1"; shift;; esac
[ -f "$SOURCE" ] || { echo "cache de la Switch absent : $SOURCE"; exit 1; }
# Copie de travail (les traductions DXC des shaders manquants s'ajoutent dans le dossier, pas dans le .odsc).
TRAVAIL="$PWD/out/shaders-switch/travail"
rm -rf "$TRAVAIL" "$TRAVAIL.odsc"
mkdir -p "$TRAVAIL"
cp "$SOURCE" "$TRAVAIL.odsc"
echo "Shaders de la Switch : $SOURCE ($(stat -c %s "$SOURCE") octets) ; sans paquet ; DXC pour les manquants"
SA2_SHADER_LIBRARY=/nonexistent exec ./jouer.sh --od_compilateur=dxc --od_cache_shaders="$TRAVAIL" \
  --od_flou_profondeur=1 "$@"
