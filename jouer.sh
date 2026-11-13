#!/bin/sh
# Lance le jeu sur PC avec le rendu natif (build out/build/sa2sdk, SDK de sa2-nx), en collectant au passage les
# shaders que le paquet ne connaît pas encore (out/build/sa2sdk/shaders et misses-<date>/).
# Après la partie : tools/paquet_shaders.sh refait le paquet avec tout ce qui a été collecté.
# Les entrées de la manette sont enregistrées dans out/entrees/<date>.txt (src/od_enregistreur.cpp), pour rejouer la
# partie sans joueur : tools/rejouer.sh out/entrees/<date>.txt. Le journal de la session est copié dans
# out/entrees/<date>.log (opdarkness.log est réécrit à chaque lancement, rejeux compris).
# Options : ./jouer.sh --sa2_renderer_native=false (GPU émulé), ou toute autre option du jeu.
cd "$(dirname "$0")"
tools/nettoyer_shm.sh
B=out/build/sa2sdk
DATE="$(date +%Y%m%d-%H%M%S)"
MISSES="misses-$DATE"
mkdir -p out/entrees out/plantages
ENTREES="$PWD/out/entrees/$DATE.txt"
LD_LIBRARY_PATH="$HOME/workspace/sa2-nx/sdk/out/linux-amd64" "$B/opdarkness" --game_data_root="$PWD/assets" \
  --gpu_plugin=xenos --license_mask=1 --log_file=opdarkness.log --sa2_renderer_native=true \
  --od_dump_shaders=true --sa2_shader_misses="$MISSES" --od_enregistrer="$ENTREES" \
  --log_flush_interval=1 --od_plantage_dossier="$PWD/out/plantages" --vblank_per_swap=true "$@"
code=$?
cp opdarkness.log "out/entrees/$DATE.log" 2>/dev/null  # journal de la session, à côté de ses entrées
echo "$DATE code de sortie $code" >> out/plantages/sorties.txt
[ $code -gt 128 ] && echo "LE JEU A PLANTÉ : signal $((code - 128)) (code $code)"
n=$(find "$B/$MISSES" -type f 2>/dev/null | wc -l)
echo "Entrées enregistrées : $ENTREES ($(grep -c '^[EK]' "$ENTREES" 2>/dev/null || echo 0) lignes)"
echo "Partie terminée : $(ls "$B/shaders" | wc -l) conteneurs de shaders, $n fichiers nouveaux dans $B/$MISSES"
[ "$n" -gt 0 ] && echo "Refaire le paquet : tools/paquet_shaders.sh"
grep -E "FATAL|critical" opdarkness.log || true
