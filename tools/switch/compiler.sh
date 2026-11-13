#!/bin/bash
# Compile la version Switch : out/build/switch/opdarkness.nro (GCC devkitA64 + libnx dans le conteneur Docker
# devkitpro/devkita64, NVK statique de ~/workspace/mesa-switch/nvk-sdk, SDK de sa2-nx avec patches/sa2-nx-sdk.patch).
# Le code généré doit exister (générateur sur le PC) : une compilation croisée ne peut pas lancer rexglue.
# Usage : JOBS=6 tools/switch/compiler.sh [--reconfigurer]
# Autre dossier et options CMake : OUT=out/build/switch-lto CMAKE_OPTS=-DOD_SWITCH_LTO=ON tools/switch/compiler.sh
set -e
cd "$(dirname "$0")/../.."
RACINE="$PWD"
OUT="${OUT:-$RACINE/out/build/switch}"
JOBS="${JOBS:-6}"
SA2="$HOME/workspace/sa2-nx"
NVK="${NVK_SDK:-$HOME/workspace/mesa-switch/nvk-sdk/opt/devkitpro/portlibs/switch}"
PC="${PC:-$RACINE/out/build/sa2sdk}"

# SPIR-V internes du rendu (glslangValidator de l'hôte ne tourne pas dans le conteneur) : ceux de la build PC.
ninja -C "$PC" sa2_edram_shaders >/dev/null
# Icône du .nro : jaquette fournie par l'utilisateur (nro-icon.png, non versionnée) -> out/switch-icone.jpg.
if [ -f "$RACINE/nro-icon.png" ]; then
  mkdir -p "$RACINE/out"
  python3 -I "$RACINE/tools/switch/icone.py" "$RACINE/nro-icon.png" "$RACINE/out/switch-icone.jpg"
fi
[ -f "$RACINE/generated/default/opdarkness_register.cpp" ] || { echo "code généré absent : lancer le générateur"; exit 1; }

CONF=""
if [ "$1" = "--reconfigurer" ] || [ ! -f "$OUT/CMakeCache.txt" ]; then
  CONF="cmake -S '$RACINE' -B '$OUT' -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE='$SA2/tools/switch/cmake/switch-devkitA64.cmake' \
    -DREXGLUE_SWITCH_NVK_SDK='$NVK' -DDEVKITPRO=/opt/devkitpro \
    -DREXSDK_DIR='$SA2/sdk' -DGIT_EXECUTABLE=/bin/false -DOD_NATIF=ON \
    -DOD_SPIRV_INTERNES='$PC/generated-shaders' ${CMAKE_OPTS:-} &&"
fi
mkdir -p "$OUT"
docker run --rm -u "$(id -u):$(id -g)" -e HOME="$HOME" -e DEVKITPRO=/opt/devkitpro \
  -v "$HOME/workspace:$HOME/workspace" -w "$RACINE" devkitpro/devkita64 \
  bash -c "$CONF ninja -C '$OUT' -j $JOBS"
ls -la "$OUT/opdarkness.nro"
