#!/bin/sh
# Outils PC de la chaîne des shaders deko3d (équivalent Linux de tools/renut/deko3d_build_tools.sh de banjo-nx, écrit
# pour Windows) : SPIRV-Cross (~/workspace/_tools/SPIRV-Cross, étiquette vulkan-sdk-1.4.304.0) et uam (la copie
# modifiée de banjo-nx, out/renut-tiers/uam : même code que celui lié dans le .nro), dans out/deko3d_tools.
# gpu.cmake (shaders hôtes) et tools/renut/deko3d_*.py les cherchent sous ces noms (spirv-cross.exe, uam.exe).
# Pour la compilation Switch, une copie faite dans le conteneur devkitPro (glibc 2.36 : celle du PC demande 2.38) :
#   docker run ... devkitpro/devkita64 env DEKO3D_OUTILS=out/deko3d_tools_conteneur CC=gcc CXX=g++ tools/deko3d_outils.sh
set -e
cd "$(dirname "$0")/.."
SRC="${SPIRV_CROSS_SRC:-$HOME/workspace/_tools/SPIRV-Cross}"
OUT="$PWD/${DEKO3D_OUTILS:-out/deko3d_tools}"
CC="${CC:-clang-20}"; CXX="${CXX:-clang++-20}"
mkdir -p "$OUT/spirv-cross-build" "$OUT/uam-build"
cmake -S "$SRC" -B "$OUT/spirv-cross-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" -DSPIRV_CROSS_ENABLE_TESTS=OFF -DSPIRV_CROSS_SHARED=OFF -DSPIRV_CROSS_ENABLE_C_API=OFF \
  > "$OUT/spirv-cross-build/configure.log"
ninja -C "$OUT/spirv-cross-build" spirv-cross > "$OUT/spirv-cross-build/build.log"
cmake -S out/renut-tiers/uam -B "$OUT/uam-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" > "$OUT/uam-build/configure.log"
ninja -C "$OUT/uam-build" uam_tool > "$OUT/uam-build/build.log"
# Les noms de Windows que les outils attendent.
ln -sf spirv-cross "$OUT/spirv-cross-build/spirv-cross.exe"
U="$(find "$OUT/uam-build" -maxdepth 2 -type f -name 'uam' -perm -u+x | head -1)"
[ -n "$U" ] && ln -sf "$U" "$OUT/uam-build/uam.exe"
"$OUT/spirv-cross-build/spirv-cross" --help 2>&1 | head -1
"$OUT/uam-build/uam.exe" --version
