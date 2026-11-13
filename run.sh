#!/bin/sh
# Génère le code (si le manifeste a changé), configure, compile et lance la version PC, puis affiche le FATAL du log.
# Options supplémentaires du jeu : passées telles quelles (ex. ./run.sh --log_level=debug).
# DUREE=90 ./run.sh : arrête le jeu après 90 s (essais automatiques).
set -e
cd "$(dirname "$0")"
tools/nettoyer_shm.sh
SDK="${REXSDK:-$HOME/rexglue-sdk-0.10.0-linux-amd64/linux-amd64}"
PRESET="${PRESET:-linux-amd64-release}"
[ -f "out/build/$PRESET/CMakeCache.txt" ] || cmake --preset "$PRESET" -DCMAKE_PREFIX_PATH="$SDK" > /dev/null
cmake --build --preset "$PRESET" --target opdarkness_codegen
cmake --preset "$PRESET" > /dev/null
cmake --build --preset "$PRESET" -j "${JOBS:-6}"
rm -f opdarkness.log
LD_LIBRARY_PATH="$SDK/lib" ${DUREE:+timeout $DUREE} \
  "./out/build/$PRESET/opdarkness" --game_data_root="$PWD/assets" --gpu_plugin=xenos --license_mask=1 \
  --log_file=opdarkness.log "$@" || true
grep -E "FATAL|critical" opdarkness.log || echo "Pas de FATAL dans le log"
