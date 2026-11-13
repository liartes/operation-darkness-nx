# opdarkness - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(opdarkness LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# glslang du SDK avec l'entrée HLSL (traduction des shaders en jeu, src/od_traducteur.cpp ; option ajoutée au SDK de
# sa2-nx par patches/sa2-nx-sdk.patch).
set(REXGLUE_GLSLANG_HLSL ON CACHE BOOL "glslang avec l'entrée HLSL" FORCE)

# Chemins de la machine de compilation retirés des __FILE__ (journaux et assertions du SDK et du jeu : ~200 chaînes dans
# le .nro publié) : relatifs au dossier qui contient les dépôts (~/workspace). -fmacro-prefix-map seulement : -ffile-prefix-map
# changerait aussi les chemins des profils PGO. Avant le SDK (include ci-dessous). Repris de torchlight-nx.
get_filename_component(OD_ESPACE "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
add_compile_options("-fmacro-prefix-map=${OD_ESPACE}/=")

include(generated/rexglue.cmake)

# Étape C de l'optimisation (docs/OPTIMISATION.md) : après chaque génération du code, accès mémoire sans volatile,
# barrières de boucles et appels directs (tools/apres_codegen.py, outils de sa2-nx). La compilation relance seule le
# générateur quand une entrée change : l'étape est donc ajoutée à sa commande.
if(TARGET opdarkness_codegen)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    add_custom_command(
        OUTPUT "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/codegen.build.stamp"
        APPEND
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/apres_codegen.py
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tools/apres_codegen.py ${CMAKE_CURRENT_SOURCE_DIR}/tools/vmx_ecritures.py
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/copies_masse.py ${CMAKE_CURRENT_SOURCE_DIR}/src/copies_masse.txt
                ${CMAKE_CURRENT_SOURCE_DIR}/tools/pile_locale.py)
endif()

# Sources
set(OPDARKNESS_SOURCES
    src/main.cpp
    src/opdarkness_manette.cpp
    src/od_images.cpp
    src/od_sommeil.cpp
    src/od_audio.cpp
    src/od_copies.cpp
    # Copies localisées en masse (src/copies_masse.txt, tools/copies_masse.py ; repris de renut-nx) : la garde et
    # 12 morceaux compilés en parallèle.
    src/masse/od_hot_guest.cpp
    src/masse/od_hot_shard_0.cpp src/masse/od_hot_shard_1.cpp src/masse/od_hot_shard_2.cpp src/masse/od_hot_shard_3.cpp
    src/masse/od_hot_shard_4.cpp src/masse/od_hot_shard_5.cpp src/masse/od_hot_shard_6.cpp src/masse/od_hot_shard_7.cpp
    src/masse/od_hot_shard_8.cpp src/masse/od_hot_shard_9.cpp src/masse/od_hot_shard_10.cpp src/masse/od_hot_shard_11.cpp
    src/od_lecture.cpp
    src/od_boost_chargement.cpp
    src/od_memoire.cpp
    src/od_matrices.cpp
    src/od_chrono.cpp
    src/od_fil_maj.cpp
    src/od_comptes.cpp
    src/od_compresseur.cpp
    src/od_clavier.cpp
    src/od_enregistreur.cpp
    src/od_crt.cpp
    src/od_setjmp.cpp
    src/od_plantage.cpp
    src/od_ecrans.cpp
    src/od_pgo.cpp
)

if(WIN32)
    add_executable(opdarkness WIN32 ${OPDARKNESS_SOURCES})
else()
    add_executable(opdarkness ${OPDARKNESS_SOURCES})
endif()

rexglue_setup_target(opdarkness GPU_PLUGINS xenos)

# Rendu natif (Vulkan, lecture de l'anneau PM4) : demande le SDK de sa2-nx (REXSDK_DIR=~/workspace/sa2-nx/sdk).
option(OD_NATIF "Rendu natif de sa2-nx (src/me)" OFF)
if(OD_NATIF)
    include(cmake/rendu_natif.cmake)
endif()

# Rendu reNut (au niveau de Direct3D, plume : Vulkan sur PC, deko3d sur la Switch ; docs/DEKO3D.md), choisi au
# démarrage par od_renderer = "renut". Demande le rendu natif (même SDK) et tools/import_renut_sa2.sh.
option(OD_RENUT "Rendu reNut (src/renut_engine)" OFF)
if(OD_RENUT)
    if(NOT OD_NATIF)
        message(FATAL_ERROR "OD_RENUT demande OD_NATIF=ON")
    endif()
    include(cmake/rendu_renut.cmake)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Comme sa2-nx : pas de FMA que le PowerPC n'avait pas, et le code traduit lit la même mémoire sous plusieurs types.
    foreach(target opdarkness opdarkness_recomp)
        if(TARGET ${target})
            target_compile_options(${target} PRIVATE -ffp-contract=off -fno-strict-aliasing)
        endif()
    endforeach()
endif()

# Une fonction interceptée (REX_HOOK_RAW) ne doit jamais être appelée directement (__imp__sub_X) : vérifié après
# chaque édition des liens. Si ça échoue (nouvelle interception) : python3 tools/apres_codegen.py, puis recompiler.
if(TARGET opdarkness_codegen AND NOT REXGLUE_PLATFORM_SWITCH)
    add_custom_command(TARGET opdarkness POST_BUILD
        COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/apres_codegen.py --verifier $<TARGET_FILE:opdarkness>
        VERBATIM)
endif()

# --- Version Switch (tools/switch/compiler.sh : conteneur devkitA64, NVK statique) ---------------------------------
if(REXGLUE_PLATFORM_SWITCH)
    # opdarkness_register.cpp : une seule fonction de ~30 000 SetFunction exécutée une fois au démarrage ; GCC -O3 y
    # passe un quart d'heure (sa2-nx) : -O0 ne change rien à l'exécution.
    set_property(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/generated/default/opdarkness_register.cpp"
        APPEND PROPERTY COMPILE_OPTIONS -O0)
    # Réglages fixes compilés dans le .nro, et opdarkness.toml du joueur (src/opdarkness_app.h).
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/switch/defauts.toml" OD_BUILTIN_CONFIG)
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/switch/opdarkness.toml" OD_PLAYER_CONFIG)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/switch/defauts.toml" "${CMAKE_CURRENT_SOURCE_DIR}/switch/opdarkness.toml")
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/od_builtin_config.h.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/builtin_config/od_builtin_config.h" @ONLY)
    target_include_directories(opdarkness PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/builtin_config")
    target_compile_definitions(opdarkness PRIVATE OD_BUILTIN_CONFIG=1)
    # LTO (repris de sa2-nx, app/CMakeLists.txt) : code du jeu (opdarkness_recomp) et application (rendu, interceptions)
    # compilés en GIMPLE et optimisés ensemble à l'édition des liens ; les bibliothèques du SDK restent à part.
    # -ffp-contract=off et -fno-strict-aliasing répétés au lien (l'étape LTRANS ne doit pas fusionner ce que la
    # compilation a laissé séparé) ; xxHash sans accès mémoire « rapide » (violation d'aliasing chez nfsmw-nx une fois
    # inliné). OD_SWITCH_LTO_JOBS bas : mémoire de la machine.
    option(OD_SWITCH_LTO "Optimisation à l'édition des liens (Switch)" OFF)
    set(OD_SWITCH_LTO_JOBS "2" CACHE STRING "Tâches LTRANS parallèles du lien LTO")
    target_compile_definitions(opdarkness PRIVATE XXH_FORCE_MEMORY_ACCESS=0)
    if(OD_SWITCH_LTO)
        foreach(target opdarkness_recomp opdarkness)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE -flto -fno-fat-lto-objects -fipa-pta)
            endif()
        endforeach()
        target_link_options(opdarkness PRIVATE -flto=${OD_SWITCH_LTO_JOBS} -flto-partition=balanced -fipa-pta
                            -ffp-contract=off -fno-strict-aliasing)
        message(STATUS "opdarkness : LTO Switch (${OD_SWITCH_LTO_JOBS} tâches LTRANS)")
    endif()
    # Mesure du coût des défauts de TLB sur la console (src/od_tlb.cpp, lignes [tlb] du journal) : essai, jamais dans
    # une compilation pour le joueur.
    option(OD_MESURE_TLB "Mesure du coût des défauts de TLB (essai)" OFF)
    if(OD_MESURE_TLB)
        target_sources(opdarkness PRIVATE src/od_tlb.cpp)
    endif()
    # Code du .nro reprojeté en blocs de 2 Mo au démarrage (src/od_code_blocs.cpp, repris de torchlight). Sans LTO :
    # assembleur de premier niveau.
    option(OD_CODE_BLOCS "Code du .nro reprojeté en blocs de 2 Mo au démarrage" ON)
    if(OD_CODE_BLOCS)
        target_sources(opdarkness PRIVATE src/od_code_blocs.cpp)
        set_source_files_properties(src/od_code_blocs.cpp PROPERTIES COMPILE_OPTIONS "-fno-lto")
    endif()
    # PGO (repris de sa2-nx, app/CMakeLists.txt), code du jeu et application :
    #   OD_PGO=generate  .nro instrumenté qui écrit ses compteurs de branches dans pgo/ sur la carte (src/od_pgo.cpp ;
    #                    enregistrés dans une section, pas par des constructeurs : l'écriture de libgcov à la sortie ne
    #                    convient pas à la Switch). Profilage des valeurs coupé (listes partagées non sûres sans mises à
    #                    jour atomiques).
    #   OD_PGO=use       les compteurs fusionnés sur le PC (tools/switch/pgo_fusion.sh). Le code que les parties n'ont
    #                    pas exécuté est compilé comme sans profil (-fprofile-partial-training) ; les fonctions changées
    #                    depuis le .nro instrumenté n'ont plus de profil (-Wno-coverage-mismatch).
    # Profil : OD_PGO_DIR (out/pgo, jamais versionné : il décrit le code généré du jeu), un .gcda par objet, nommé par
    # son chemin dans le dossier de compilation (-fprofile-prefix-path) : les deux compilations peuvent être dans des
    # dossiers différents.
    set(OD_PGO "" CACHE STRING "PGO de la version Switch : vide, generate ou use")
    set(OD_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/out/pgo" CACHE PATH "Dossier du profil PGO (.gcda)")
    if(OD_PGO STREQUAL "generate")
        set(_od_pgo "-fprofile-generate=${OD_PGO_DIR}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                    -fno-profile-values -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(opdarkness PRIVATE OD_PGO_GENERATE=1)
    elseif(OD_PGO STREQUAL "use")
        if(NOT EXISTS "${OD_PGO_DIR}")
            message(FATAL_ERROR "OD_PGO=use : pas de profil dans ${OD_PGO_DIR}")
        endif()
        # -fprofile-correction : compteurs pris sans mises à jour atomiques par plusieurs fils du jeu.
        set(_od_pgo "-fprofile-use=${OD_PGO_DIR}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}" -fprofile-correction
                    -fno-profile-values -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch)
        # Ninja ne sait pas que les objets dépendent des .gcda : sans ceci, un nouveau profil ne recompilait que les
        # fichiers modifiés (2026-10-17). Les .gcda relancent la configuration, et leur empreinte, définie pour toute
        # la compilation, change toutes les lignes de commande : tout est recompilé avec le nouveau profil.
        file(GLOB _od_gcda "${OD_PGO_DIR}/*.gcda")
        list(SORT _od_gcda)
        set(_od_empreintes "")
        foreach(_f ${_od_gcda})
            file(SHA1 "${_f}" _h)
            string(APPEND _od_empreintes "${_h}")
        endforeach()
        string(SHA1 _od_profil "${_od_empreintes}")
        string(SUBSTRING "${_od_profil}" 0 12 _od_profil)
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_od_gcda})
        list(APPEND _od_pgo "-DOD_PGO_PROFIL=0x${_od_profil}")
        list(LENGTH _od_gcda _od_gcda_n)
        message(STATUS "opdarkness : profil PGO ${_od_profil} (${OD_PGO_DIR}, ${_od_gcda_n} fichiers .gcda)")
    elseif(OD_PGO)
        message(FATAL_ERROR "OD_PGO doit être vide, generate ou use (et non ${OD_PGO})")
    endif()
    if(OD_PGO)
        foreach(target opdarkness_recomp opdarkness)
            if(TARGET ${target})
                target_compile_options(${target} PRIVATE ${_od_pgo})
            endif()
        endforeach()
        target_link_options(opdarkness PRIVATE ${_od_pgo})
        message(STATUS "opdarkness : PGO ${OD_PGO}")
    endif()
    # Icône : out/switch-icone.jpg, faite par tools/switch/compiler.sh à partir de nro-icon.png (jaquette fournie par
    # l'utilisateur, non versionnée) ; sinon l'icône par défaut de libnx.
    set(OD_ICONE "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/out/switch-icone.jpg")
        set(OD_ICONE ICON "${CMAKE_CURRENT_SOURCE_DIR}/out/switch-icone.jpg")
    endif()
    rexglue_switch_add_nro(opdarkness
        NAME "Operation Darkness"
        AUTHOR "Liartes"
        VERSION "1.0.0"
        ${OD_ICONE})
endif()
