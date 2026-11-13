#!/bin/sh
# Captures à des images précises : le jeu s'arrête à chacune (--od_pause_images, src/od_images.cpp), ce script
# capture la fenêtre (xwd -> PNG) puis le relance. Deux lancements (rendu natif / GPU émulé, ou deux rejeux) donnent
# ainsi des captures comparables image par image, quelle que soit leur vitesse. À utiliser en mode lockstep
# (--vblank_per_swap=true, ajouté ici) avec un rejeu d'entrées (--od_rejouer=...).
# Usage : tools/captures_images.sh <dossier de sortie> <images séparées par des virgules> <durée max s> [options du jeu]
set -e
cd "$(dirname "$0")/.."
tools/nettoyer_shm.sh
OUT="$1"; IMAGES="$2"; DUREE="$3"; shift 3
mkdir -p "$OUT"
TEMOIN="$PWD/out/captures/pause-$$.flag"
rm -f "$TEMOIN" opdarkness.log
LD_LIBRARY_PATH="${LIBS:-$HOME/workspace/sa2-nx/sdk/out/linux-amd64}" timeout "$DUREE" \
  "${BIN:-out/build/sa2sdk/opdarkness}" --game_data_root="$PWD/assets" --gpu_plugin=xenos --license_mask=1 \
  --log_file=opdarkness.log --vblank_per_swap=true --od_pause_images="$IMAGES" --od_pause_fichier="$TEMOIN" "$@" \
  > "$OUT/run.txt" 2>&1 &
JEU=$!
reste=$(echo "$IMAGES" | tr ',' '\n' | grep -c .)
while [ "$reste" -gt 0 ] && kill -0 $JEU 2>/dev/null; do
  if [ -f "$TEMOIN" ]; then
    n=$(cat "$TEMOIN")
    sleep 0.7  # le rendu finit et présente l'image en cours
    id=$(xwininfo -root -tree | awk '/\("opdarkness" "opdarkness"\)/ && /[0-9]+x[0-9]+/ {print $1; exit}')
    [ -n "$id" ] || id=$(xwininfo -root -tree | awk '/opdarkness|ReXGlue|rexglue/ && /[0-9]+x[0-9]+/ {print $1; exit}')
    if [ -n "$id" ] && xwd -silent -id "$id" -out "$OUT/i$n.xwd"; then
      python3 -I tools/xwd2png.py "$OUT/i$n.xwd" "$OUT/i$n.png" && rm -f "$OUT/i$n.xwd" && echo "image $n"
    else
      echo "image $n : capture échouée"
    fi
    rm -f "$TEMOIN"
    reste=$((reste - 1))
  fi
  sleep 0.1
done
kill $JEU 2>/dev/null || true
wait 2>/dev/null || true
grep -E "FATAL|critical" opdarkness.log || true
