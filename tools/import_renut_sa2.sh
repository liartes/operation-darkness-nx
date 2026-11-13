#!/bin/sh
# Copie le rendu reNut de sa2-nx (rendu au niveau de Direct3D de reNut, github.com/masterspike52/reNut, tel que banjo-nx
# et sa2-nx le portent ; docs/DEKO3D.md) depuis une révision de leur dépôt, sans toucher à l'arbre de travail patché
# de ~/workspace/sa2-nx (le SDK y est modifié par patches/sa2-nx-sdk.patch) : lecture par git archive.
#   src/renut_engine     le moteur (versionné ici, adapté à Operation Darkness : chaque écart noté « opdarkness »)
#   tools/renut/         les outils des shaders de banjo-nx et sa2-nx (deko3d_*.py, renut_*.py, early_alpha.py)
#   out/renut-tiers/     le code tiers, non versionné ici (plume avec la dorsale deko3d, reblue-XenosRecomp, uam,
#                        imgui, collect_captured_shaders.cmake) : ~90 Mo
# Usage : tools/import_renut_sa2.sh [révision]   (défaut origin/main ; git -C ~/workspace/sa2-nx fetch avant)
# Le moteur n'est copié que s'il n'existe pas encore (src/renut_engine est modifié ici) ; FORCER=1 l'écrase.
set -e
cd "$(dirname "$0")/.."
SA2="$HOME/workspace/sa2-nx"
REV="${1:-origin/main}"
COMMIT="$(git -C "$SA2" rev-parse "$REV")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
git -C "$SA2" archive "$COMMIT" app/src/renut_engine app/thirdparty/plume app/thirdparty/reblue-XenosRecomp \
  app/thirdparty/uam app/thirdparty/imgui app/cmake/collect_captured_shaders.cmake tools | tar -x -C "$TMP"

if [ ! -d src/renut_engine ] || [ "${FORCER:-0}" = 1 ]; then
  rm -rf src/renut_engine
  cp -r "$TMP/app/src/renut_engine" src/renut_engine
  echo "$COMMIT $(git -C "$SA2" log -1 --format=%cs "$COMMIT")" > src/renut_engine/SA2_NX_COMMIT
  echo "moteur reNut copié dans src/renut_engine"
fi

mkdir -p tools/renut
for f in "$TMP"/tools/deko3d_*.py "$TMP"/tools/deko3d_build_tools.sh "$TMP"/tools/renut_*.py \
         "$TMP"/tools/renut_hlsl_frontend.cpp "$TMP"/tools/renut_hlsl_pch.h "$TMP"/tools/early_alpha.py; do
  [ -e "$f" ] && cp "$f" tools/renut/
done

DEST="out/renut-tiers"
rm -rf "$DEST"
mkdir -p "$DEST/cmake"
cp -r "$TMP"/app/thirdparty/. "$DEST/"
cp "$TMP/app/cmake/collect_captured_shaders.cmake" "$DEST/cmake/"
echo "$COMMIT" > "$DEST/SA2_NX_COMMIT"
# Nos modifications de plume (diagnostics deko3d : barrière avant un dessin, dessins ignorés comptés ; 2026-10-21).
patch -d "$DEST" -p1 < patches/plume-diagnostics.patch
# XenosRecomp : échelles 3/4 et 1/2 (définition dynamique) ; vertex shaders vidés avec OD_HLSL_DUMP_VS (od_ombres.py) ;
# unions locales nommées pour GCC (traduction des shaders par la console, src/od_xenosrecomp_renut.cpp).
patch -d "$DEST" -p1 < patches/xenosrecomp-opdarkness.patch

# DXC : le dépôt de sa2-nx n'en garde pas les binaires (.gitignore) ; la release Linux de Microsoft aux places que
# lisent XenosRecomp (dxc-bin/CMakeLists.txt) et gpu.cmake.
DXC="${DXC_DIR:-$HOME/workspace/_tools/dxc-1.8.2505.1}"
BIN="$DEST/reblue-XenosRecomp/thirdparty/dxc-bin"
mkdir -p "$BIN/bin/x64" "$BIN/lib/x64"
ln -sf "$DXC/bin/dxc" "$BIN/bin/x64/dxc-linux"
ln -sf "$DXC/lib/libdxcompiler.so" "$BIN/lib/x64/libdxcompiler.so"
ln -sf "$DXC/lib/libdxil.so" "$BIN/lib/x64/libdxil.so"
echo "code tiers de reNut dans $DEST ($COMMIT)"
