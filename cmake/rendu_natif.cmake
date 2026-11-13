# opdarkness - rendu natif (src/me, importé de sa2-nx par tools/import_rendu_sa2.sh ; GPL-3.0). Inclus par
# CMakeLists.txt avec OD_NATIF=ON, qui demande le SDK de sa2-nx (REXSDK_DIR=~/workspace/sa2-nx/sdk).
# Le bloc des shaders internes (SPIR-V) est celui de sa2-nx app/CMakeLists.txt, à resynchroniser avec lui.

target_sources(opdarkness PRIVATE
    src/me/me_native_system.cpp
    src/me/me_ring_wait.cpp
    src/me/me_renderdoc.cpp
    src/me/me_pc_stubs.cpp
    src/me/native/sa2_native_draws.cpp
    src/me/native/sa2_deferred_recording.cpp
    src/me/native/sa2_native_targets.cpp
    src/me/native/sa2_native_shaders.cpp
    src/me/native/sa2_native_textures_pool.cpp
    src/me/native/sa2_shader_library.cpp
    src/me/native/sa2_native_glue.cpp
    src/me/sa2_render_mode.cpp
    src/cpu/sa2_enhancements.cpp
    src/cpu/sa2_gpu_boost.cpp
    src/od_me_hooks.cpp
    src/od_api_stub.cpp)
target_compile_definitions(opdarkness PRIVATE OD_NATIF=1)

# SPIR-V initializers of the native renderer's EDRAM transfer / utility shaders (src/me/native/shaders), compiled
# on the host with glslangValidator (tools/win/build_glslang.ps1 builds it from the SDK's glslang). The Switch build
# uses the same host tool: SPIR-V is platform independent.
set(SA2_GLSLANG_VALIDATOR "$ENV{HOME}/workspace/sa2-nx/out/glslang/StandAlone/glslangValidator${CMAKE_EXECUTABLE_SUFFIX}"
    CACHE FILEPATH "Host glslangValidator")
# Switch (conteneur devkitA64) : le glslangValidator de l'hôte n'y tourne pas (glibc) ; OD_SPIRV_INTERNES = le dossier
# generated-shaders d'une build PC (SPIR-V portable), utilisé tel quel (tools/switch/compiler.sh).
set(OD_SPIRV_INTERNES "" CACHE PATH "Dossier de SPIR-V internes déjà compilés (.inc), au lieu de glslangValidator")
if(OD_SPIRV_INTERNES)
    set(SA2_GENERATED_SHADER_DIR "${OD_SPIRV_INTERNES}")
else()
set(SA2_GENERATED_SHADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated-shaders")
set(SA2_EDRAM_SHADER_INCLUDES)
foreach(_shader sa2_edram_7e3_to_rgba8 sa2_edram_rgba8_to_7e3 sa2_edram_16f_to_16f me_resolve_exp_bias me_edram_depth_to_rgba8 me_edram_depth_to_16f me_edram_import me_edram_color_to_depth me_edram_depth_to_depth me_edram_depth_to_stencil me_edram_color_to_stencil me_edram_depth_to_depth_msaa2 me_edram_depth_msaa2_to_depth_1x me_edram_depth_to_stencil_msaa2 me_edram_depth_msaa2_to_stencil_1x me_edram_raw64_to_rgba8 me_edram_rgba8_to_raw64 me_edram_raw64_to_16f me_edram_16f_to_raw64 me_edram_raw64_to_raw64 me_edram_depth_to_raw64 me_edram_raw64_to_depth me_depth_resolve_guestspace me_depth_resolve_guestspace_msaa2 me_edram_stencil_to_buffer me_edram_stencil_msaa2_to_buffer me_edram_color_stencil_to_buffer me_edram_color_to_color me_edram_r64_to_r64 sa2_resolve_copy sa2_depth_resolve_ms)
    set(_extension comp)
    if(_shader STREQUAL "me_edram_import")
        set(_extension vert)
    elseif(_shader MATCHES "^me_edram_(color_to_depth|depth_to_depth|depth_to_stencil|color_to_stencil|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x|raw64_to_depth|color_to_color|r64_to_r64)$" OR _shader STREQUAL "sa2_resolve_copy" OR
           _shader STREQUAL "sa2_depth_resolve_ms")
        set(_extension frag)
    endif()
    set(_source "${CMAKE_CURRENT_SOURCE_DIR}/src/me/native/shaders/${_shader}.${_extension}")
    set(_output "${SA2_GENERATED_SHADER_DIR}/${_shader}.inc")
    set(_target_flags)
    if(_shader MATCHES "^me_edram_(stencil_to_buffer|color_stencil_to_buffer|stencil_msaa2_to_buffer|depth_to_depth_msaa2|depth_msaa2_to_depth_1x|depth_to_stencil_msaa2|depth_msaa2_to_stencil_1x)$" OR _shader STREQUAL "me_depth_resolve_guestspace_msaa2")
        set(_target_flags --target-env vulkan1.2)
        set(_output "${SA2_GENERATED_SHADER_DIR}/${_shader}_generated.inc")
    endif()
    add_custom_command(OUTPUT "${_output}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${SA2_GENERATED_SHADER_DIR}"
        COMMAND "${SA2_GLSLANG_VALIDATOR}" -V ${_target_flags} -x -o "${_output}" "${_source}"
        DEPENDS "${_source}" VERBATIM)
    list(APPEND SA2_EDRAM_SHADER_INCLUDES "${_output}")
endforeach()
add_custom_target(sa2_edram_shaders DEPENDS ${SA2_EDRAM_SHADER_INCLUDES})
add_dependencies(opdarkness sa2_edram_shaders)
endif()
target_include_directories(opdarkness PRIVATE
    "${SA2_GENERATED_SHADER_DIR}"
    src
    src/me
    src/me/native
    src/me/native/shaders
    "${REXSDK_DIR}/src/core"
    # SPIR-V précompilés du présentateur du SDK (vulkan_spirv/guest_output_*) : le rendu dessine dans sa sortie.
    "${REXSDK_DIR}/src/ui/shaders")

# Sources du SDK, côté CPU, que le rendu réutilise (étendue des dessins, interprétation du microcode).
target_sources(opdarkness PRIVATE
    "${REXSDK_DIR}/src/graphics/register_file.cpp"
    "${REXSDK_DIR}/src/graphics/format/ucode.cpp"
    "${REXSDK_DIR}/src/graphics/pipeline/shader/interpreter.cpp")

foreach(_dep xxhash renderdoc)
    if(TARGET ${_dep})
        target_include_directories(opdarkness PRIVATE $<TARGET_PROPERTY:${_dep},INTERFACE_INCLUDE_DIRECTORIES>)
    endif()
endforeach()

target_sources(opdarkness PRIVATE src/od_attente_gpu.cpp)

# --- Traduction des shaders au fil du jeu (src/od_traducteur.cpp, docs/SHADERS_EN_JEU.md) ---------------------------
# XenosRecomp de sa2-nx (conteneur -> HLSL) compilé dans le jeu ; HLSL -> SPIR-V par glslang (PC et Switch, entrée
# HLSL activée dans le SDK : REXGLUE_GLSLANG_HLSL, CMakeLists.txt) ou, sur PC, par DXC chargé à l'exécution (dlopen ;
# seuls ses en-têtes servent ici : release Linux de Microsoft, OD_DXC_DIR).
get_filename_component(OD_SA2NX_DIR "${REXSDK_DIR}/.." ABSOLUTE)
set(OD_XENOSRECOMP_DIR "${OD_SA2NX_DIR}/shaders/XenosRecomp")

add_library(od_xenosrecomp STATIC "${OD_XENOSRECOMP_DIR}/shader_recompiler.cpp")
target_include_directories(od_xenosrecomp PUBLIC "${OD_SA2NX_DIR}/shaders" "${OD_XENOSRECOMP_DIR}"
    "${REXSDK_DIR}/thirdparty/xxHash")
target_compile_definitions(od_xenosrecomp PUBLIC MASSEFFECT_RECOMP)
target_compile_options(od_xenosrecomp PUBLIC -fms-extensions -include "${OD_XENOSRECOMP_DIR}/pch_min.h")
target_compile_options(od_xenosrecomp PRIVATE -w)
target_link_libraries(od_xenosrecomp PUBLIC fmt)

# Préludes HLSL copiés dans des littéraux : la traduction insère l'un ou l'autre en tête de chaque shader
# (shader_common.h de sa2-nx pour DXC ; shaders/shader_common_glslang.h pour glslang).
set(OD_PRELUDES_DIR "${CMAKE_CURRENT_BINARY_DIR}/od-preludes")
foreach(_prelude "od_shader_common|${OD_XENOSRECOMP_DIR}/shader_common.h"
                 "od_shader_common_glslang|${CMAKE_CURRENT_SOURCE_DIR}/shaders/shader_common_glslang.h")
    string(REPLACE "|" ";" _prelude "${_prelude}")
    list(GET _prelude 0 _nom)
    list(GET _prelude 1 _source)
    file(READ "${_source}" _texte)
    file(WRITE "${OD_PRELUDES_DIR}/${_nom}.inc" "R\"OD_PRELUDE(${_texte})OD_PRELUDE\"\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source}")
endforeach()

target_sources(opdarkness PRIVATE src/od_traducteur.cpp src/od_prep_glslang.cpp src/od_variantes.cpp
    src/od_reparation_variantes.cpp "${REXSDK_DIR}/thirdparty/glslang/StandAlone/ResourceLimits.cpp")
target_include_directories(opdarkness PRIVATE "${OD_PRELUDES_DIR}" "${REXSDK_DIR}/thirdparty/glslang"
    "${REXSDK_DIR}/thirdparty/glslang/StandAlone")
target_link_libraries(opdarkness PRIVATE od_xenosrecomp glslang SPIRV)

if(REXGLUE_PLATFORM_SWITCH)
    target_compile_definitions(opdarkness PRIVATE OD_TRADUCTEUR_DXC=0)
else()
    set(OD_DXC_DIR "$ENV{HOME}/workspace/_tools/dxc-1.8.2505.1" CACHE PATH "Release Linux de DXC (en-têtes include/dxc)")
    set_source_files_properties(src/od_traducteur.cpp PROPERTIES INCLUDE_DIRECTORIES "${OD_DXC_DIR}/include")
    target_link_libraries(opdarkness PRIVATE ${CMAKE_DL_LIBS})

    # Outil PC : HLSL de XenosRecomp depuis des conteneurs (mise au point du prélude glslang, docs/SHADERS_EN_JEU.md).
    add_executable(od_hlsl EXCLUDE_FROM_ALL src/outils/od_hlsl.cpp src/od_prep_glslang.cpp)
    target_include_directories(od_hlsl PRIVATE "${OD_PRELUDES_DIR}" src)
    target_link_libraries(od_hlsl PRIVATE od_xenosrecomp)
endif()
