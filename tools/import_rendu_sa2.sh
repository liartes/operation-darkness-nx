#!/bin/sh
# Copie le rendu natif de sa2-nx (PM4 -> Vulkan, venu de masseffect-nx par banjo-nx ; GPL-3.0) dans src/me et
# src/cpu, sans les parties propres à SA2 (couche Direct3D native sa2_api_*, recensement sa2_d3d_api, interceptions
# sa2_me_hooks : remplacés par src/od_me_hooks.cpp et src/od_api_stub.cpp). Les fichiers gardent leur nom et leur
# contenu, pour pouvoir les resynchroniser : ne pas les modifier ici sans le noter dans docs/RENDU_NATIF.md.
# Usage : tools/import_rendu_sa2.sh [dépôt sa2-nx]   (défaut ~/workspace/sa2-nx)
set -e
cd "$(dirname "$0")/.."
SA2="${1:-$HOME/workspace/sa2-nx}"
SRC="$SA2/app/src"
mkdir -p src/me/native src/cpu
for f in "$SRC"/me/*; do
  case "$(basename "$f")" in
    sa2_api_*.cpp|sa2_api_internal.h|sa2_d3d_api.*|sa2_me_hooks.cpp|native) ;;
    *) cp "$f" src/me/ ;;
  esac
done
cp -r "$SRC"/me/native/. src/me/native/
cp "$SRC"/cpu/sa2_enhancements.h "$SRC"/cpu/sa2_enhancements.cpp "$SRC"/cpu/sa2_gpu_boost.cpp src/cpu/
echo "$(git -C "$SA2" rev-parse HEAD) $(git -C "$SA2" log -1 --format=%cs)" > src/me/SA2_NX_COMMIT
echo "rendu de sa2-nx importé : $(cat src/me/SA2_NX_COMMIT)"
