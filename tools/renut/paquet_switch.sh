#!/bin/sh
# Paquet des shaders du jeu pour reNut sur la Switch (out/renut_switch), d'après la build PC (docs/DEKO3D.md, étape 3) :
#   1. build PC (out/build/renut) : cache des shaders, HLSL retouché dans renut_gpu/early_alpha_hlsl (early alpha de
#      banjo-nx, flou de profondeur allégé OD_RENUT_FLOU) ;
#   2. XenosRecomp sur ce HLSL -> SPIR-V (out/deko3d_shaders/spirv) ;
#   3. SPIRV-Cross -> GLSL (deko3d_shaders.py) ; 4. passes de banjo-nx (deko3d_glsl_opt.py, long) ;
#   5. paquet GLSL et cache Switch (deko3d_shader_package.py, renut_switch_shader_cache.py).
# Ensuite : PC=$PWD/out/build/renut OUT=$PWD/out/build/switch-renut CMAKE_OPTS=-DOD_RENUT=ON tools/switch/compiler.sh
set -e
cd "$(dirname "$0")/../.."
ninja -C out/build/renut renut_shader_cache_gen
X=out/build/renut/xenos_recomp/build/XenosRecomp/XenosRecomp
rm -rf out/deko3d_shaders/spirv out/deko3d_shaders/glsl out/deko3d_shaders/glsl_opt
mkdir -p out/deko3d_shaders/spirv
LD_LIBRARY_PATH=out/renut-tiers/reblue-XenosRecomp/thirdparty/dxc-bin/lib/x64 \
  BANJO_DEKO3D_SPIRV_DIR=out/deko3d_shaders/spirv BANJO_HLSL_OVERRIDE=out/build/renut/renut_gpu/early_alpha_hlsl \
  "$X" out/renut_shader_dump out/deko3d_shaders/spirv_pass.cpp out/renut-tiers/reblue-XenosRecomp/XenosRecomp/shader_common.h \
  > out/deko3d_shaders/xenosrecomp.log 2>&1
echo "SPIR-V : $(ls out/deko3d_shaders/spirv | wc -l) fichiers"
python3 -I tools/renut/deko3d_shaders.py --spirv out/deko3d_shaders/spirv --out out/deko3d_shaders/glsl --no-uam \
  > out/deko3d_shaders/glsl.log 2>&1
# opdarkness (2026-10-22) : sans les passes scale1 et fragscale (tout en 720p), pour la définition dynamique de reNut
# (540p, 360p : sa2_render_dynamic) ; PASSES=<liste> pour en choisir d'autres.
PASSES="${PASSES:-$(sed -n "s/^    ('\([a-z0-9]*\)', pass_.*/\1/p" tools/renut/deko3d_glsl_opt.py | grep -vx 'scale1\|fragscale' | paste -sd,)}"
python3 -I tools/renut/deko3d_glsl_opt.py --in out/deko3d_shaders/glsl/glsl --out out/deko3d_shaders/glsl_opt --check \
  --passes "$PASSES" \
  > out/deko3d_shaders/glsl_opt.log 2>&1
python3 -I tools/renut/deko3d_shader_package.py --glsl out/deko3d_shaders/glsl_opt --out out/renut_switch 2>&1 | tail -2
python3 -I tools/renut/renut_switch_shader_cache.py --pc out/build/renut 2>&1 | tail -2
ls -la out/renut_switch
