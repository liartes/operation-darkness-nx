#!/bin/sh
# Échantillonnage des piles de tous les threads (profil des attentes : un thread qui dort apparaît dans sa fonction
# d'attente, ce qu'un profil perf ne montre pas). Lance le jeu sous gdb, l'interrompt N fois à partir de T secondes
# (toutes les 3 s), écrit les piles dans out/profil/gdb.txt ; tools/piles_resume.py les regroupe.
# Usage : tools/piles.sh <T> <N> [options du jeu]   (build out/build/sa2sdk, SDK de sa2-nx)
set -e
cd "$(dirname "$0")/.."
tools/nettoyer_shm.sh
T="$1"; N="$2"; shift 2
mkdir -p out/profil
LIBS="${LIBS:-$HOME/workspace/sa2-nx/sdk/out/linux-amd64}"
BIN="${BIN:-out/build/sa2sdk/opdarkness}"
cmds=""
i=0; while [ $i -lt "$N" ]; do cmds="$cmds -ex 'thread apply all bt 16' -ex continue"; i=$((i+1)); done
rm -f opdarkness.log
eval LD_LIBRARY_PATH="$LIBS" gdb -q -batch -ex "'set debuginfod enabled off'" \
  -ex "'handle SIGSEGV nostop noprint pass'" -ex "'handle SIG35 nostop noprint pass'" \
  -ex "'handle SIGPIPE nostop noprint pass'" -ex run $cmds --args "$BIN" --game_data_root="$PWD/assets" \
  --gpu_plugin=xenos --license_mask=1 --log_file=opdarkness.log "$@" > out/profil/gdb.txt 2>&1 &
sleep "$T"
P=$(pgrep -x opdarkness | head -1)
i=0; while [ $i -lt "$N" ]; do kill -INT "$P"; sleep 3; i=$((i+1)); done
sleep 2; kill "$P" 2>/dev/null || true; wait 2>/dev/null || true
echo "$(grep -c '^Thread' out/profil/gdb.txt) piles de threads dans out/profil/gdb.txt"
