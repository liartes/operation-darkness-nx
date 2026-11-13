#!/bin/sh
# Essai de la traduction des shaders pour deko3d par la console (docs/DEKO3D.md, « Traduction des shaders par la
# console ») : compare la sortie de la console (essai_deko3d/sortie de la carte, --od_essai_deko3d) à celle de l'outil PC
# (out/build/renut/od_chaine_deko3d, même code) fichier par fichier, et rappelle les bilans des deux rapports.
#   tools/renut/comparer_essai_deko3d.sh <sortie de la console> <sortie du PC>
set -e
C="$1"; P="$2"
[ -d "$C" ] && [ -d "$P" ] || { echo "usage : $0 <sortie de la console> <sortie du PC>"; exit 1; }
for d in hlsl prep spv vk glsl dksh; do
  n=0; pareils=0; absents=0
  for f in "$P/$d"/*; do
    [ -e "$f" ] || continue
    n=$((n + 1))
    g="$C/$d/$(basename "$f")"
    if [ ! -f "$g" ]; then absents=$((absents + 1)); continue; fi
    if cmp -s "$f" "$g"; then pareils=$((pareils + 1)); else echo "  différent : $d/$(basename "$f")"; fi
  done
  echo "$d : $pareils identiques sur $n (absents de la console : $absents)"
done
echo "--- console"; grep '^#' "$C/rapport.txt" 2>/dev/null || { echo "(pas de rapport.txt : essai interrompu ?) $(wc -l < "$C/lignes.txt") lignes"; }
echo "--- PC"; grep '^#' "$P/rapport.txt"
