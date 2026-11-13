#!/bin/sh
# Construit le paquet de shaders du rendu natif (sa2_shaders.mesp + .idx) à partir des vidages d'une build
# (guide de sa2-nx §6) et le copie à côté de l'exécutable. Données tirées du jeu : jamais versionnées.
#   1. conteneurs créés par le jeu : <build>/shaders (--od_dump_shaders=true) ;
#   2. ce que le paquet ignorait, vidé par le rendu natif (--sa2_renderer_native=true --sa2_shader_misses=misses) :
#      missing/ (conteneurs reconstruits), discovery/ (microcode brut des shaders propres à Direct3D, enveloppé par
#      wrap_discovered.py), variants/ (vertex shaders réécrits par Direct3D, réparés par tools/repare_variantes.py).
# Les dossiers misses* de la build sont tous pris (relancer le jeu après chaque paquet, jusqu'à ne plus rien vider).
# Usage : tools/paquet_shaders.sh [build]   (défaut out/build/sa2sdk)
# Outils : ~/workspace/sa2-nx (build_shader_library.sh, XenosRecomp), DXC 1.8.2505.1 dans ~/workspace/_tools.
set -e
cd "$(dirname "$0")/.."
B="$(cd "${1:-out/build/sa2sdk}" && pwd)"
SA2="$HOME/workspace/sa2-nx"
DXC="${DXC:-$HOME/workspace/_tools/dxc-1.8.2505.1/bin/dxc}"
rm -rf "$B/wrapped" "$B/repaired"
manquants=$(ls -d "$B"/misses*/missing 2>/dev/null || true)
decouvertes=$(ls -d "$B"/misses*/discovery 2>/dev/null || true)
variantes=$(ls -d "$B"/misses*/variants 2>/dev/null || true)
if [ -n "$decouvertes" ]; then
  python3 "$SA2/tools/wrap_discovered.py" --raw $decouvertes --containers "$B/shaders" $manquants --out "$B/wrapped"
fi
for v in $variantes; do
  python3 -I tools/repare_variantes.py "$v" "$B/repaired" "$B/shaders" $manquants
done
(cd "$SA2" && CXX="${CXX:-clang++-20}" DXC="$DXC" JOBS="${JOBS:-6}" bash tools/build_shader_library.sh \
  "$B/shaders" $manquants $(ls -d "$B/wrapped" "$B/repaired" 2>/dev/null))
cp "$SA2/out/shaders/sa2_shaders.mesp" "$SA2/out/shaders/sa2_shaders.mesp.idx" "$B/"
echo "paquet copié dans $B"
