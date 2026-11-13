#!/bin/sh
# Rejoue une partie enregistrée par ./jouer.sh (out/entrees/<date>.txt) sans joueur, avec le rendu natif, et capture
# la fenêtre toutes les PAS secondes (out/captures/rejeu-<nom>/t<s>.png) pour vérifier que le rejeu suit le même
# chemin. Collecte aussi les shaders inconnus, comme jouer.sh.
# Usage : tools/rejouer.sh <entrées> [durée en s, défaut : celle de l'enregistrement + 30] [options du jeu]
# Variables : PAS (défaut 30).
set -e
cd "$(dirname "$0")/.."
ENTREES="$(realpath "$1")"; shift
DUREE="${1:-}"; [ -n "$DUREE" ] && shift
[ -z "$DUREE" ] && DUREE=$(( $(grep '^[EK]' "$ENTREES" | tail -1 | awk '{print int($3/1000)}') + 30 ))
PAS="${PAS:-30}"
NOM="$(basename "$ENTREES" .txt)"
OUT="out/captures/rejeu-$NOM"
rm -rf "$OUT"; mkdir -p "$OUT"
instants=""; t=$PAS; while [ $t -lt "$DUREE" ]; do instants="$instants $t"; t=$((t + PAS)); done
LIBS="$HOME/workspace/sa2-nx/sdk/out/linux-amd64" BIN=out/build/sa2sdk/opdarkness \
  OPTIONS="--vblank_per_swap=true --sa2_renderer_native=true --od_dump_shaders=true --sa2_shader_misses=misses-rejeu-$NOM --od_rejouer=$ENTREES $*" \
  tools/captures.sh "$DUREE" $instants
mv out/captures/t*.png "$OUT/" 2>/dev/null || true
echo "Rejeu de $DUREE s : captures dans $OUT"
grep -E "FATAL|critical" opdarkness.log || true
