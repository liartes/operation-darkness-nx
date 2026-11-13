#!/bin/bash
# Banc d'essai du prélude glslang (Switch, docs/SHADERS_EN_JEU.md) : HLSL des shaders du cache de traduction avec
# shaders/shader_common_glslang.h (outil od_hlsl de la build PC), préparation (tools/prep_glslang.py), puis
# glslangValidator (HLSL) sur chacun ; erreurs regroupées. Sortie : out/glslang/{hlsl,spv}.
# Usage : tools/glslang_essai.sh [cache .odsh]
cd "$(dirname "$0")/.."
G=~/workspace/sa2-nx/out/glslang-hlsl/StandAlone/glslangValidator
CACHE="${1:-out/build/sa2sdk/cache_shaders}"
rm -rf out/glslang; mkdir -p out/glslang/hlsl out/glslang/spv out/glslang/err
out/build/sa2sdk/od_hlsl --prelude shaders/shader_common_glslang.h out/glslang/brut "$CACHE"/*.odsh >/dev/null
ok=0; ko=0
for f in out/glslang/brut/*.hlsl; do
  n=$(basename "$f" .hlsl)
  python3 -I tools/prep_glslang.py < "$f" > out/glslang/hlsl/$n.hlsl
  case $n in vs_*) st=vert; iy=--invert-y;; *) st=frag; iy=;; esac
  if $G -V -D -S $st -e main $iy --target-env vulkan1.2 -o out/glslang/spv/$n.spv out/glslang/hlsl/$n.hlsl \
       > out/glslang/err/$n.txt 2>&1; then ok=$((ok+1)); else ko=$((ko+1)); fi
done
echo "compilés : $ok ; échecs : $ko"
grep -h "^ERROR" out/glslang/err/*.txt | sed -E 's/^ERROR: [^:]+:[0-9]+: //' | sort | uniq -c | sort -rn | head -${N:-15}
