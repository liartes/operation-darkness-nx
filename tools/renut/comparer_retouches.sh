#!/bin/sh
# Retouches du paquet du PC refaites en C++ (src/od_retouches.cpp) contre les scripts (od_flou_profondeur.py mode 1,
# od_ombres.py) : l'outil od_chaine_deko3d traduit les conteneurs sans puis avec --retouches ; les scripts sont lancés
# sur le HLSL de la première sortie, qui doit alors être identique à celui de la seconde, fichier pour fichier.
#   tools/renut/comparer_retouches.sh [dossier de conteneurs] [dossier de travail]
set -e
cd "$(dirname "$0")/../.."
ENTREE="${1:-out/renut_shader_dump}"
TRAVAIL="${2:-out/comparer-retouches}"
OUTIL="${OUTIL:-out/build/renut/od_chaine_deko3d}"
rm -rf "$TRAVAIL"
mkdir -p "$TRAVAIL"
for m in sans avec; do
  [ $m = avec ] && X=--retouches || X=
  "$OUTIL" "$ENTREE" "$TRAVAIL/$m" --sans-uam --fils 6 $X > "$TRAVAIL/$m.log" 2>&1
done
cp -r "$TRAVAIL/sans/hlsl" "$TRAVAIL/scripts"
python3 -I tools/renut/od_flou_profondeur.py "$TRAVAIL/scripts" 1
python3 -I tools/renut/od_ombres.py "$TRAVAIL/scripts"
diff -r -q "$TRAVAIL/scripts" "$TRAVAIL/avec/hlsl" && echo "HLSL retouché identique aux scripts ($(ls "$TRAVAIL/scripts" | wc -l) fichiers)"
echo "retouchés : $(diff -r -q "$TRAVAIL/sans/hlsl" "$TRAVAIL/avec/hlsl" | wc -l)"
grep "^#" "$TRAVAIL/avec/rapport.txt" | head -2
