#!/bin/sh
# Mesure les images/s en combat (scénario rapide tools/scenario_combat.sh, mesure de 100 à 130 s : tour d'Edward et
# tutoriel) et l'occupation processeur du jeu (en cœurs) sur la même période, pour chaque jeu d'options donné.
# Usage : tools/mesure_combat.sh "options 1" "options 2" ...   (build out/build/sa2sdk, rendu natif)
cd "$(dirname "$0")/.."
tools/nettoyer_shm.sh
LIBS="${LIBS:-$HOME/workspace/sa2-nx/sdk/out/linux-amd64}"
S="$(tools/scenario_combat.sh)"
for opts in "$@"; do
  rm -f opdarkness.log
  LD_LIBRARY_PATH="$LIBS" timeout 135 out/build/sa2sdk/opdarkness --game_data_root="$PWD/assets" --gpu_plugin=xenos \
    --license_mask=1 --log_file=opdarkness.log --sa2_renderer_native=true --od_manette="$S" $opts > /dev/null 2>&1 &
  sleep 100
  P=$(pgrep -x opdarkness | head -1)
  c1=$(awk '{print $14+$15}' /proc/$P/stat 2>/dev/null)
  sleep 30
  c2=$(awk '{print $14+$15}' /proc/$P/stat 2>/dev/null)
  wait
  ips=$(grep -E "\[images\]" opdarkness.log | tail -3 | grep -oE "\] [0-9.]+ images" | awk '{s+=$2;n++} END{if(n) printf "%.1f", s/n}')
  coeurs=$(awk -v a="$c1" -v b="$c2" 'BEGIN{printf "%.1f", (b-a)/100/30}')
  echo "[$opts] : $ips images/s, $coeurs cœurs"
done
