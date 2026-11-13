#!/bin/sh
# Rejoue un enregistrement d'entrées sous gdb pour attraper un plantage : gdb laisse passer les SIGSEGV que le SDK
# rattrape (pages surveillées) et SIG35, et s'arrête sur tout autre signal fatal (SIGABRT, SIGILL, SIGBUS, SIGFPE,
# ou un SIGSEGV re-levé par le gestionnaire du SDK) : pile du thread fautif, puis piles de tous les threads, dans
# out/profil/plantage.txt. Journal du jeu vidé chaque seconde (--log_flush_interval=1).
# Usage : tools/rejouer_gdb.sh <entrées> <durée max en s> [options du jeu]
set -e
cd "$(dirname "$0")/.."
tools/nettoyer_shm.sh
ENTREES="$(realpath "$1")"; DUREE="$2"; shift 2
mkdir -p out/profil
rm -f opdarkness.log
LD_LIBRARY_PATH="$HOME/workspace/sa2-nx/sdk/out/linux-amd64" timeout "$DUREE" gdb -q -batch \
  -ex 'set debuginfod enabled off' -ex 'set pagination off' \
  -ex 'handle SIGSEGV nostop noprint pass' -ex 'handle SIG35 nostop noprint pass' \
  -ex 'handle SIGPIPE nostop noprint pass' -ex 'handle SIGINT stop print nopass' \
  -ex run -ex 'echo \n=== THREAD FAUTIF ===\n' -ex 'bt 40' -ex 'info registers rip' \
  -ex 'echo \n=== TOUS LES THREADS ===\n' -ex 'thread apply all bt 25' \
  --args out/build/sa2sdk/opdarkness --game_data_root="$PWD/assets" --gpu_plugin=xenos --license_mask=1 \
  --log_file=opdarkness.log --log_flush_interval=1 --sa2_renderer_native=true --od_rejouer="$ENTREES" "$@" \
  > out/profil/plantage.txt 2>&1 || true
grep -nE "received signal|exited|THREAD FAUTIF" out/profil/plantage.txt | head -5
