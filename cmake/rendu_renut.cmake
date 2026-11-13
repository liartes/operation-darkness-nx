# opdarkness - rendu reNut (rendu au niveau de Direct3D de reNut, github.com/masterspike52/reNut, tel que banjo-nx et
# sa2-nx le portent ; docs/DEKO3D.md), inclus par CMakeLists.txt avec OD_RENUT=ON (après le rendu natif : même SDK de
# sa2-nx). Choisi au démarrage par od_renderer = "renut" ; sinon les interceptions rendent la main au jeu.
# Repris de sa2-nx app/cmake/sa2_renut_gpu.cmake (adf3a4e). Code tiers : out/renut-tiers (tools/import_renut_sa2.sh).
#
# Une bibliothèque OBJECT et non statique : ses interceptions remplacent les définitions faibles sub_X du code généré,
# et une archive ne fournit ses membres que pour des symboles indéfinis (les interceptions ne seraient pas liées).

set(RENUT_TIERS_DIR "${CMAKE_SOURCE_DIR}/out/renut-tiers" CACHE PATH "Code tiers de reNut (tools/import_renut_sa2.sh)")
set(RENUT_OUTILS_DIR "${CMAKE_SOURCE_DIR}/tools/renut")
if(NOT EXISTS "${RENUT_TIERS_DIR}/plume/CMakeLists.txt")
    message(FATAL_ERROR "Pas de ${RENUT_TIERS_DIR}/plume : lancer tools/import_renut_sa2.sh")
endif()

# Conteneurs de shaders traduits dans le cache (XenosRecomp de reNut) : ceux du rendu natif (--od_dump_shaders) et
# ceux que reNut capture à l'exécution (renut_shader_capture_dir). Données du jeu : out/ seulement.
set(RENUT_SHADER_DUMP_DIR "${CMAKE_SOURCE_DIR}/out/renut_shader_dump")
if(NOT EXISTS "${RENUT_SHADER_DUMP_DIR}")
    message(FATAL_ERROR "Pas de ${RENUT_SHADER_DUMP_DIR} : y copier les conteneurs (*.bin) de --od_dump_shaders")
endif()
set(RENUT_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets" CACHE PATH "Données du jeu")

set(BANJO_RENUT_XENOS_RECOMP_ARGS "")
foreach(_od_var CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET CMAKE_LINKER_TYPE CMAKE_C_FLAGS CMAKE_CXX_FLAGS
                CMAKE_AR)
    if(DEFINED ${_od_var} AND NOT "${${_od_var}}" STREQUAL "")
        list(APPEND BANJO_RENUT_XENOS_RECOMP_ARGS "-D${_od_var}=${${_od_var}}")
    endif()
endforeach()

if(TARGET o1heap AND NOT TARGET rex::o1heap)
    add_library(rex::o1heap ALIAS o1heap)
endif()

# Switch (compilation croisée dans le conteneur devkitPro, tools/switch/compiler.sh) : XenosRecomp ne peut pas y
# tourner ; le cache de shaders et le paquet GLSL viennent de la build PC (docs/DEKO3D.md, « Étape 3 » :
# tools/renut/renut_switch_shader_cache.py et deko3d_shader_package.py -> out/renut_switch). Dorsale deko3d de plume
# (renut_gpu_backend = "deko3d") avec uam lié dans le .nro ; les shaders internes de reNut passent par DXC, SPIRV-Cross
# et uam de l'hôte : la copie faite dans le conteneur (tools/deko3d_outils.sh, out/deko3d_tools_conteneur). NVK reste
# lié (rendu natif et reNut sur Vulkan au choix ; le retrait viendra après les mesures).
if(REXGLUE_PLATFORM_SWITCH)
    set(PLUME_PLATFORM_SWITCH ON CACHE BOOL "" FORCE)
    set(PLUME_SWITCH_DEVKITPRO "${DEVKITPRO}" CACHE PATH "" FORCE)
    set(PLUME_SWITCH_NVK_ROOT "${REXGLUE_SWITCH_NVK_SDK}" CACHE PATH "" FORCE)
    set(PLUME_SWITCH_NVK_LIBRARY "${REXGLUE_SWITCH_NVK_SDK}/lib/libvulkan.a" CACHE FILEPATH "" FORCE)
    set(PLUME_SWITCH_UAM_DIR "${RENUT_TIERS_DIR}/uam" CACHE PATH "" FORCE)
    set(PLUME_DEKO3D ON CACHE BOOL "deko3d back end (Switch)" FORCE)
    set(BANJO_DEKO3D_HOST_SHADERS ON)
    set(OD_DEKO3D_OUTILS "${CMAKE_SOURCE_DIR}/out/deko3d_tools_conteneur" CACHE PATH
        "SPIRV-Cross et uam compilés dans le conteneur (tools/deko3d_outils.sh)")
    set(BANJO_DEKO3D_SPIRV_CROSS "${OD_DEKO3D_OUTILS}/spirv-cross-build/spirv-cross.exe" CACHE FILEPATH "" FORCE)
    set(BANJO_DEKO3D_UAM "${OD_DEKO3D_OUTILS}/uam-build/uam.exe" CACHE FILEPATH "" FORCE)
    foreach(_od_outil BANJO_DEKO3D_SPIRV_CROSS BANJO_DEKO3D_UAM)
        if(NOT EXISTS "${${_od_outil}}")
            message(FATAL_ERROR "Pas de ${${_od_outil}} : tools/deko3d_outils.sh dans le conteneur (voir son en-tête)")
        endif()
    endforeach()
    # .nro sans donnée du jeu (pour la distribution) : table vide, pas de paquet GLSL ; la console fait les siens à
    # l'installation (src/od_installation.cpp, od_installer = « si_absent » par défaut).
    option(OD_RENUT_SANS_DONNEES "Switch : aucun shader du jeu dans le .nro (installation par la console)" OFF)
    if(OD_RENUT_SANS_DONNEES)
        set(BANJO_DEKO3D_GLSL_CPP "${CMAKE_SOURCE_DIR}/out/renut_switch_aucun/deko3d_glsl_switch.cpp" CACHE FILEPATH "" FORCE)
        set(BANJO_RENUT_SHADER_CACHE_CPP "${CMAKE_SOURCE_DIR}/src/od_shader_cache_vide.cpp" CACHE FILEPATH "" FORCE)
        target_compile_definitions(opdarkness PRIVATE OD_RENUT_SANS_DONNEES=1)
    else()
        set(_od_renut_shaders "${CMAKE_SOURCE_DIR}/out/renut_switch")
        set(BANJO_DEKO3D_GLSL_CPP "${_od_renut_shaders}/deko3d_glsl_switch.cpp" CACHE FILEPATH "" FORCE)
        set(BANJO_RENUT_SHADER_CACHE_CPP "${_od_renut_shaders}/shader_cache_switch.cpp" CACHE FILEPATH "" FORCE)
    endif()
    function(banjo_renut_shader_cache_override TARGET)
        if(NOT EXISTS "${BANJO_RENUT_SHADER_CACHE_CPP}")
            message(FATAL_ERROR "Pas de ${BANJO_RENUT_SHADER_CACHE_CPP} : build PC, puis "
                                "tools/renut/renut_switch_shader_cache.py --pc out/build/renut")
        endif()
        target_sources(${TARGET} PRIVATE "${BANJO_RENUT_SHADER_CACHE_CPP}")
        set_source_files_properties("${BANJO_RENUT_SHADER_CACHE_CPP}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    endfunction()
endif()

if(NOT Python3_EXECUTABLE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()
if(NOT PYTHON_EXECUTABLE)
    set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()

include("${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/gpu.cmake")

set(_od_renut "${CMAKE_SOURCE_DIR}/src/renut_engine")
add_library(od_renut_gpu OBJECT
    "${_od_renut}/gpu/banjo_renut_select.cpp"
    "${_od_renut}/gpu/banjo_renut_tuning.cpp"
    "${_od_renut}/gpu/banjo_renut_checks.cpp"
    "${_od_renut}/gpu/banjo_renut_record.cpp"
    "${_od_renut}/display_sync.cpp"
    "${_od_renut}/perf/frame_stats.cpp"
    "${_od_renut}/sa2_compat/frame_latency.cpp"
    "${_od_renut}/sa2_compat/sa2_compat.cpp"
    "${_od_renut}/od_compat/od_compat.cpp"
    "${_od_renut}/od_compat/od_diag_barrieres.cpp"
    "${_od_renut}/gpu/sa2_renut_pacing.cpp")
target_include_directories(od_renut_gpu PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    "${_od_renut}/sa2_compat"
    "${REXGLUE_ENTRYPOINT_INCLUDE_DIR}"
    "${REXSDK_DIR}/src/core")
target_link_libraries(od_renut_gpu PRIVATE rex::runtime)
if(REXGLUE_PLATFORM_SWITCH)
    # La colle de la dorsale deko3d et le paquet GLSL des shaders du jeu (données du jeu : out/ seulement).
    target_sources(od_renut_gpu PRIVATE "${_od_renut}/gpu/banjo_renut_deko3d.cpp")
    if(EXISTS "${BANJO_DEKO3D_GLSL_CPP}")
        target_sources(od_renut_gpu PRIVATE "${BANJO_DEKO3D_GLSL_CPP}")
        set_source_files_properties("${BANJO_DEKO3D_GLSL_CPP}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    else()
        message(WARNING "Pas de ${BANJO_DEKO3D_GLSL_CPP} : deko3d sans les shaders du jeu "
                        "(tools/renut/deko3d_shader_package.py)")
    endif()
    target_compile_options(od_renut_gpu PRIVATE -g1)
    # Émulateur (Ryujinx) : le .nro démarre sans chargeur, donc sans argv[0] ; sa place habituelle sur la carte
    # (dossier du toml, des journaux et de game_root). Sur la console, hbloader donne toujours argv[0].
    if(TARGET rexcore)
        target_compile_definitions(rexcore PRIVATE
            REX_SWITCH_FALLBACK_EXE="/switch/operation-darkness-nx/operation-darkness-nx-renut.nro")
    endif()
endif()
if(COMMAND rexglue_apply_target_settings)
    rexglue_apply_target_settings(od_renut_gpu)
endif()
renut_gpu_setup(od_renut_gpu)
target_compile_definitions(od_renut_gpu PRIVATE SA2_RENUT_RENDERER=1 OD_RENUT=1)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(od_renut_gpu PRIVATE -ffp-contract=off -fno-strict-aliasing)
endif()

target_link_libraries(opdarkness PRIVATE od_renut_gpu)
target_sources(opdarkness PRIVATE src/od_renut_vblank.cpp)
target_compile_definitions(opdarkness PRIVATE SA2_RENUT_RENDERER=1 OD_RENUT=1)

# --- Traduction des shaders du jeu pour deko3d sans DXC (src/od_chaine_deko3d.cpp, docs/DEKO3D.md « Traduction des
# shaders par la console ») : XenosRecomp de reNut dans le jeu (enfermé dans un espace de noms : le rendu natif lie
# celui de sa2-nx), glslang et SPIRV-Tools du SDK, SPIRV-Cross (sources, étiquette vulkan-sdk-1.4.304.0), uam (celui de
# plume sur la Switch). Essai au démarrage : --od_essai_deko3d ; outil PC : od_chaine_deko3d.
set(OD_SPIRV_CROSS_DIR "$ENV{HOME}/workspace/_tools/SPIRV-Cross" CACHE PATH "Sources de SPIRV-Cross")
add_library(od_spirv_cross STATIC
    "${OD_SPIRV_CROSS_DIR}/spirv_cross.cpp"
    "${OD_SPIRV_CROSS_DIR}/spirv_parser.cpp"
    "${OD_SPIRV_CROSS_DIR}/spirv_cross_parsed_ir.cpp"
    "${OD_SPIRV_CROSS_DIR}/spirv_cfg.cpp"
    "${OD_SPIRV_CROSS_DIR}/spirv_glsl.cpp")
target_include_directories(od_spirv_cross PUBLIC "${OD_SPIRV_CROSS_DIR}")
target_compile_options(od_spirv_cross PRIVATE -w)

set(_od_xr_renut "${RENUT_TIERS_DIR}/reblue-XenosRecomp/XenosRecomp")
file(READ "${_od_xr_renut}/shader_common.h" _od_texte)
file(WRITE "${OD_PRELUDES_DIR}/od_renut_shader_common.inc" "R\"OD_PRELUDE(${_od_texte})OD_PRELUDE\"\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_od_xr_renut}/shader_common.h")
add_library(od_xenosrecomp_renut STATIC src/od_xenosrecomp_renut.cpp)
target_include_directories(od_xenosrecomp_renut PRIVATE "${_od_xr_renut}" "${OD_PRELUDES_DIR}" src
    "${REXSDK_DIR}/thirdparty/xxHash")
target_compile_definitions(od_xenosrecomp_renut PRIVATE RENUT_RECOMP=1)
target_compile_options(od_xenosrecomp_renut PRIVATE -fms-extensions -w)
target_link_libraries(od_xenosrecomp_renut PRIVATE fmt)

set(_od_chaine_sources src/od_chaine_deko3d.cpp src/od_passes_glsl.cpp src/od_deko3d_glsl.cpp src/od_switch_cpu.cpp src/od_alloc_fils.cpp
    src/od_conteneurs.cpp src/od_retouches.cpp)
target_sources(opdarkness PRIVATE ${_od_chaine_sources} src/od_installation.cpp)
# L'installation compresse le paquet GLSL : le compresseur de zstd en plus des décodeurs de reNut (gpu.cmake).
file(GLOB _od_zstd_compress "${RENUT_ZSTD_LIB_DIR}/compress/*.c")
target_sources(renut_zstd PRIVATE ${_od_zstd_compress})
# L'installation lit les en-têtes de reNut (table des shaders) et zstd : les dossiers d'en-têtes de od_renut_gpu.
set_property(SOURCE src/od_installation.cpp APPEND PROPERTY INCLUDE_DIRECTORIES
    "$<TARGET_PROPERTY:od_renut_gpu,INCLUDE_DIRECTORIES>")
target_link_libraries(opdarkness PRIVATE od_xenosrecomp_renut od_spirv_cross SPIRV-Tools-opt)
if(REXGLUE_PLATFORM_SWITCH)
    set_source_files_properties(src/od_chaine_deko3d.cpp PROPERTIES COMPILE_DEFINITIONS OD_CHAINE_UAM=1
        INCLUDE_DIRECTORIES "${RENUT_TIERS_DIR}/uam")
    # Allocateur à cache par fil des fils de traduction (src/od_alloc_fils.cpp) : rpmalloc 1.4.5 (domaine public),
    # ses fonctions de mise en correspondance fournies par nous (newlib n'a pas mmap : src/compat_switch/sys/mman.h).
    set(OD_RPMALLOC_DIR "$ENV{HOME}/workspace/_tools/rpmalloc-1.4.5/rpmalloc" CACHE PATH "Sources de rpmalloc")
    add_library(od_rpmalloc STATIC "${OD_RPMALLOC_DIR}/rpmalloc.c")
    target_include_directories(od_rpmalloc PRIVATE "${CMAKE_SOURCE_DIR}/src/compat_switch")
    target_include_directories(od_rpmalloc PUBLIC "${OD_RPMALLOC_DIR}")
    # sysconf manque aussi : rpmalloc ne lui demande que la taille de page (od_rpmalloc_sysconf, od_alloc_fils.cpp).
    target_compile_definitions(od_rpmalloc PRIVATE RPMALLOC_CONFIGURABLE=1 ENABLE_OVERRIDE=0 ENABLE_PRELOAD=0
        sysconf=od_rpmalloc_sysconf)
    target_compile_options(od_rpmalloc PRIVATE -w)
    target_link_libraries(opdarkness PRIVATE od_rpmalloc)
else()
    if(NOT TARGET uamlib)
        add_subdirectory("${RENUT_TIERS_DIR}/uam" "${CMAKE_BINARY_DIR}/uam-pc" EXCLUDE_FROM_ALL)
    endif()
    add_executable(od_chaine_deko3d EXCLUDE_FROM_ALL src/outils/od_chaine_deko3d.cpp ${_od_chaine_sources}
        src/od_prep_glslang.cpp "${REXSDK_DIR}/thirdparty/glslang/StandAlone/ResourceLimits.cpp")
    target_include_directories(od_chaine_deko3d PRIVATE src "${REXSDK_DIR}/thirdparty/glslang"
        "${REXSDK_DIR}/thirdparty/glslang/StandAlone" "${REXSDK_DIR}/thirdparty/xxHash")
    target_compile_definitions(od_chaine_deko3d PRIVATE OD_CHAINE_OUTIL=1 OD_CHAINE_UAM=1)
    # L'allocateur par fil de la console, essayé sur PC (src/od_alloc_fils.cpp, OD_ALLOC_FILS_FORCE).
    set(OD_RPMALLOC_DIR "$ENV{HOME}/workspace/_tools/rpmalloc-1.4.5/rpmalloc" CACHE PATH "Sources de rpmalloc")
    add_library(od_rpmalloc_pc STATIC EXCLUDE_FROM_ALL "${OD_RPMALLOC_DIR}/rpmalloc.c")
    target_include_directories(od_rpmalloc_pc PUBLIC "${OD_RPMALLOC_DIR}")
    target_compile_definitions(od_rpmalloc_pc PRIVATE RPMALLOC_CONFIGURABLE=1 ENABLE_OVERRIDE=0 ENABLE_PRELOAD=0)
    target_compile_options(od_rpmalloc_pc PRIVATE -w)
    target_compile_definitions(od_chaine_deko3d PRIVATE OD_ALLOC_FILS_FORCE=1)
    target_link_libraries(od_chaine_deko3d PRIVATE od_xenosrecomp_renut od_spirv_cross glslang SPIRV SPIRV-Tools-opt
        fmt uamlib od_rpmalloc_pc pthread rexruntime)
endif()
