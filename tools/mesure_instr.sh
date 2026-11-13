#!/bin/bash
# Travail du fil qui dessine (vmsDrawThread) en combat (scénario rapide, 20 s à partir de 100 s) : instructions
# (espace utilisateur) et temps processeur par seconde (perf stat sur ce fil), avec les images/s du journal (le jeu
# est plafonné à 60 : par seconde = par 60 images). Bien moins bruité que l'occupation en % (tools/mesure_fils.sh) :
# sert à comparer deux versions du code généré.
# Usage : tools/mesure_instr.sh [étiquette] [options du jeu]   (perf : kernel.perf_event_paranoid <= 1)
cd "$(dirname "$0")/.."
tools/nettoyer_shm.sh
ETIQ="${1:-mesure}"; shift
LIBS="${LIBS:-$HOME/workspace/sa2-nx/sdk/out/linux-amd64}"
rm -f opdarkness.log
LD_LIBRARY_PATH="$LIBS" timeout 130 out/build/sa2sdk/opdarkness --game_data_root="$PWD/assets" --gpu_plugin=xenos \
  --license_mask=1 --log_file=opdarkness.log --sa2_renderer_native=true --od_manette="$(tools/scenario_combat.sh)" \
  "$@" > /dev/null 2>&1 &
sleep 100
P=$(pgrep -x opdarkness | head -1)
T=$(for t in /proc/$P/task/*; do [ "$(cat $t/comm)" = "vmsDrawThread_1" ] && basename $t; done | head -1)
r=$(perf stat -x, -e instructions:u,task-clock -t "$T" -- sleep 20 2>&1)
sleep 1
ips=$(grep -E "\[images\]" opdarkness.log | tail -2 | grep -oE "\] [0-9.]+ images" | awk '{s+=$2;n++} END{if(n) printf "%.1f", s/n}')
wait
instr=$(echo "$r" | awk -F, '/instructions/ {print $1}')
ms=$(echo "$r" | awk -F, '/task-clock/ {print $1}')
echo "$ETIQ : $ips images/s ; fil qui dessine : $(awk -v a="$instr" 'BEGIN{printf "%.1f", a/20/1e6}') M instructions/s, $(awk -v a="$ms" 'BEGIN{printf "%.0f", a/20/10}') % d'un cœur"
