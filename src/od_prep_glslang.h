// opdarkness - préparation du HLSL de XenosRecomp pour glslang (traduction des shaders sur la Switch ;
// docs/SHADERS_EN_JEU.md). Portage à l'identique de tools/prep_glslang.py (vérifié par tools/glslang_essai.sh).
#pragma once

#include <string>
#include <string_view>

namespace opdarkness::glslang_prep {

std::string Preparer(std::string_view hlsl);

// HLSL de reNut (XenosRecomp RENUT_RECOMP, prélude shader_common.h de reNut) : portage à l'identique de
// tools/renut/prep_glslang_renut.py (traduction par la console pour deko3d, docs/DEKO3D.md).
std::string PreparerRenut(std::string_view hlsl);

}  // namespace opdarkness::glslang_prep
