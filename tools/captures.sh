#!/bin/sh
# Lance le jeu (build déjà faite) et capture sa fenêtre X11 (xwd) aux instants donnés, en PNG dans out/captures/.
# Usage : [MANETTE="110:start,..."] tools/captures.sh <durée totale> <t1> <t2> ... (secondes depuis le lancement ;
# MANETTE : script de --od_manette ; OPTIONS : options du jeu en plus ; BIN : exécutable, par défaut la build du SDK
# d'origine, out/build/sa2sdk/opdarkness pour celle du SDK de sa2-nx, avec LIBS=~/workspace/sa2-nx/sdk/out/linux-amd64). Captures du jeu : à ne pas versionner (out/ est ignoré).
set -e
cd "$(dirname "$0")/.."
tools/nettoyer_shm.sh
SDK="${REXSDK:-$HOME/rexglue-sdk-0.10.0-linux-amd64/linux-amd64}"
DUREE="$1"; shift
mkdir -p out/captures
rm -f opdarkness.log
LD_LIBRARY_PATH="${LIBS:-$SDK/lib}" timeout "$DUREE" "${BIN:-./out/build/linux-amd64-release/opdarkness}" --game_data_root="$PWD/assets" \
  --gpu_plugin=xenos --license_mask=1 --log_file=opdarkness.log ${MANETTE:+--od_manette=$MANETTE} $OPTIONS > out/captures/run.txt 2>&1 &
debut=$(date +%s)
for t in "$@"; do
  while [ $(( $(date +%s) - debut )) -lt "$t" ]; do sleep 1; done
  # Fenêtre du jeu elle-même (classe opdarkness), pas le cadre ajouté par le gestionnaire de fenêtres (24 bits).
  id=$(xwininfo -root -tree | awk '/\("opdarkness" "opdarkness"\)/ && /[0-9]+x[0-9]+/ {print $1; exit}')
  [ -n "$id" ] || id=$(xwininfo -root -tree | awk '/opdarkness|ReXGlue|rexglue/ && /[0-9]+x[0-9]+/ {print $1; exit}')
  [ -n "$id" ] && xwd -silent -id "$id" -out "out/captures/t$t.xwd" && \
    python3 -I tools/xwd2png.py \
      "out/captures/t$t.xwd" "out/captures/t$t.png" && rm -f "out/captures/t$t.xwd" && echo "capture t=$t" \
    || echo "t=$t : fenêtre introuvable"
done
wait || true
