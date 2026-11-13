#!/bin/bash
# Occupation processeur par fil (en % d'un cœur) en combat, scénario rapide, de 100 à 130 s, pour chaque jeu
# d'options : les fils qui cèdent en boucle (Sleep(0)) prennent tout le processeur libre, donc le total trompe ;
# c'est le fil qui dessine (vmsDrawThread) et celui de l'anneau qui comptent.
# Usage : tools/mesure_fils.sh "options 1" "options 2" ...
cd "$(dirname "$0")/.."
tools/nettoyer_shm.sh
LIBS="${LIBS:-$HOME/workspace/sa2-nx/sdk/out/linux-amd64}"
for opts in "$@"; do
  rm -f opdarkness.log
  LD_LIBRARY_PATH="$LIBS" timeout 135 out/build/sa2sdk/opdarkness --game_data_root="$PWD/assets" --gpu_plugin=xenos \
    --license_mask=1 --log_file=opdarkness.log --sa2_renderer_native=true --od_manette="$(tools/scenario_combat.sh)" \
    $opts > /dev/null 2>&1 &
  sleep 100
  P=$(pgrep -x opdarkness | head -1)
  avant=$(for t in /proc/$P/task/*; do echo "$(basename $t) $(tr ' ' _ < $t/comm) $(awk '{print $14+$15}' $t/stat)"; done | sort)
  sleep 30
  apres=$(for t in /proc/$P/task/*; do echo "$(basename $t) $(tr ' ' _ < $t/comm) $(awk '{print $14+$15}' $t/stat)"; done | sort)
  wait
  echo "[$opts] images/s $(grep -E "\[images\]" opdarkness.log | tail -3 | grep -oE "\] [0-9.]+ images" | awk '{s+=$2;n++} END{if(n) printf "%.1f", s/n}')"
  join <(echo "$avant") <(echo "$apres") | awk '{d=($5-$3)/30; if(d>3) printf "   %-22s %5.0f %%\n", $2, d}' | sort -k2 -nr | head -12
done
