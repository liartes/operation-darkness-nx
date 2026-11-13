#!/bin/bash
# PGO, étape 2 sur 3 (repris de sa2-nx, tools/win/pgo_merge.ps1). Étape 1 : le .nro instrumenté
# (OUT=out/build/switch-pgo-gen CMAKE_OPTS=-DOD_PGO=generate tools/switch/compiler.sh), joué sur la console : il écrit
# pgo/od_pgo_<N>.stream à côté du .nro (une par session, src/od_pgo.cpp). Copier ce dossier sur le PC, puis :
#   tools/switch/pgo_fusion.sh <dossier des .stream>
# Les compteurs de chaque session sont ajoutés aux .gcda du profil (out/pgo, gcov-tool merge-stream dans le conteneur
# devkitPro), puis leur résumé est rempli (tools/pgo_summary.py : l'écriture par flux le laisse à zéro).
# Étape 3 : OUT=out/build/switch-pgo CMAKE_OPTS=-DOD_PGO=use tools/switch/compiler.sh
# merge-stream AJOUTE aux .gcda déjà présents : refuse de tourner si out/pgo en contient (les mettre de côté avant).
set -e
cd "$(dirname "$0")/../.."
RACINE="$PWD"
FLUX="$(cd "$1" && pwd)"
PROFIL="$RACINE/out/pgo"
ls "$FLUX"/od_pgo_*.stream >/dev/null 2>&1 || { echo "aucun od_pgo_*.stream dans $FLUX"; exit 1; }
mkdir -p "$PROFIL"
if ls "$PROFIL"/*.gcda >/dev/null 2>&1; then echo "$PROFIL contient déjà des .gcda : les mettre de côté"; exit 1; fi
N=$(ls "$FLUX"/od_pgo_*.stream | wc -l)
docker run --rm -u "$(id -u):$(id -g)" -v "$HOME/workspace:$HOME/workspace" -w "$RACINE" devkitpro/devkita64 \
  bash -c "for f in '$FLUX'/od_pgo_*.stream; do echo \"fusion de \$(basename \$f) (\$(du -h \$f | cut -f1))\"; /opt/devkitpro/devkitA64/bin/aarch64-none-elf-gcov-tool merge-stream \"\$f\"; done"
COMPTE=$(ls "$PROFIL"/*.gcda 2>/dev/null | wc -l)
[ "$COMPTE" -gt 0 ] || { echo "aucun .gcda dans $PROFIL après la fusion : les flux nomment un autre dossier"; exit 1; }
python3 -I "$RACINE/tools/pgo_summary.py" "$PROFIL" "$N"
echo "$N session(s) fusionnée(s) dans $COMPTE fichier(s) de $PROFIL"
