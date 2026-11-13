# Third-party notices

operation-darkness-nx (an unofficial Switch port of Operation Darkness) is built on the work of other projects. Their
licenses apply to their code and to the parts of this repository derived from it. The rest of this repository is
under the GPL-3.0 (`LICENSE`).

## Projects this port is derived from

| Project | Used for | License |
|---|---|---|
| [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx) by ChanseyIsTheBest (commits `fdd6f14` and `adf3a4e`), itself derived from [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx) by NebadasSwifty, [NFSMW-NX](https://github.com/StevensND/nfsmw-nx) by StevensND and [NFSMW Recompiled](https://github.com/madelrandel-blip/NFSMW-Recompiled) by madelrandel-blip | The SDK fork with its Horizon (Switch) layer, the code generator and its build-time rewrites, the Switch toolchain; the native renderer (`src/me`, `src/cpu`, imported unchanged by `tools/import_rendu_sa2.sh`); the reNut renderer as it carries it (`src/renut_engine`, `tools/renut`, imported by `tools/import_renut_sa2.sh`, with this port's changes marked `opdarkness`) | GPL-3.0; BSD-3-Clause for the SDK |
| [reNut](https://github.com/masterspike52/reNut) by masterspike52, as banjo-nx and sa2-nx carry it | `src/renut_engine`: the Direct3D-level renderer the game is drawn with | reNut has no license file (see below); the parts written by banjo-nx, sa2-nx and this port: GPL-3.0 |
| [renut-nx](https://github.com/ChanseyIsTheBest/renut-nx) (formerly banjo-nx), [renut_nx_ultimate_base](https://github.com/ChanseyIsTheBest/renut_nx_ultimate_base) and [rr6-nx](https://github.com/ChanseyIsTheBest/rr6-nx) by ChanseyIsTheBest | The methods and code behind the localized copies of hot functions (`src/masse`, `tools/copies_masse.py`), the four-core mode and its forwarder (`tools/forwarder`, SDK patch), the XMA frame cache, the snapshot delta marks and masks, the critical sections read through the processor control region, the lock-free render queue, `tools/fma_single.py`, the GLSL passes of `src/od_passes_glsl.cpp` and the deko3d renderer tuning (`banjo_renut_*`, `banjo_deko3d_*`) | GPL-3.0 |
| [NFSMW-NX](https://github.com/StevensND/nfsmw-nx) by StevensND (branch `deko3d-implementation`) | The deko3d shader path through SPIRV-Cross behind `src/od_chaine_deko3d.cpp` and `src/od_deko3d_glsl.cpp` | GPL-3.0 |
| [torchlight-nx](https://github.com/liartes/torchlight-nx) | The scripted controller, the PGO and installation methods, the 2 MB guest window and the faster `BaseHeap::AllocRange` (SDK patch), `patches/glslang-verrou.patch` | GPL-3.0 |
| [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay, with portions from [Xenia](https://xenia.jp) (Ben Vanik and the Xenia contributors) | Code generator, Xbox 360 kernel and runtime (linked into the NRO) | BSD-3-Clause |
| [plume](https://github.com/renderbag/plume) by renderbag and contributors, with banjo-nx's deko3d back end | The graphics layer the renderer draws through (deko3d on the Switch) | MIT |
| [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by hedge-dev and contributors (reblue fork, with reNut's and banjo-nx's changes) | The Xenos shader translator, built into the NRO for the console's own shader translation | MIT |
| [uam](https://github.com/devkitPro/uam) by fincs, with the patches of SwitchWakerHD and banjo-nx | The deko3d shader compiler (Mesa's nouveau code generator), linked into the NRO | zlib (uam), MIT (Mesa's files), MPL-2.0 (SwitchWakerHD's patches) |

**reNut.** The reNut repository has no license file. sa2-nx states that its author permits its reuse in these ports;
this port uses it as sa2-nx carries it. If you are reNut's author and want something changed, please open an issue.

### Changes to these projects

The files in `patches/` are changes to the projects above, to apply on top of the listed commits
(`patches/README.md`). Each patch is under the license of the project it changes, so it can be reused there:

| Patch | Applies to | License |
|---|---|---|
| `sa2-nx-sdk.patch` | `sdk/` of sa2-nx at `fdd6f14` (the ReXGlue SDK with its Horizon layer): critical sections, threading and four-core mode, guest memory, clocks, input, XMA cache, code generator options, glslang with HLSL | BSD-3-Clause |
| `glslang-verrou.patch` | glslang (`sdk/thirdparty/glslang`) | BSD-3-Clause |
| `plume-diagnostics.patch` | plume as sa2-nx carries it (applied by `tools/import_renut_sa2.sh`) | MIT |
| `xenosrecomp-opdarkness.patch` | reblue-XenosRecomp as sa2-nx carries it (applied by `tools/import_renut_sa2.sh`) | MIT |

## Libraries

Libraries linked into the NRO. Except for SPIRV-Cross and rpmalloc, they come from the SDK tree of sa2-nx (ReXGlue SDK
v0.10.0 and its submodules) or from devkitPro.

| Library | License |
|---|---|
| [FFmpeg](https://ffmpeg.org) (libavcodec, libavutil: the XMA audio decoder), as configured by sa2-nx | LGPL-2.1-or-later (configured without GPL parts) |
| [libmspack](https://www.cabextract.org.uk/libmspack/) by Stuart Caie (LZX decompression) | LGPL-2.1 |
| [glslang](https://github.com/KhronosGroup/glslang) | BSD-3-Clause and others (see its LICENSE.txt) |
| [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools) | Apache-2.0 |
| [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) (tag `vulkan-sdk-1.4.304.0`) | Apache-2.0 |
| [SPIRV-Headers](https://github.com/KhronosGroup/SPIRV-Headers), [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) | MIT, Apache-2.0 |
| [rpmalloc](https://github.com/mjansson/rpmalloc) 1.4.5 by Mattias Jansson | Public domain, or MIT |
| [deko3d](https://github.com/devkitPro/deko3d) by fincs, from devkitPro | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | MIT |
| [fmt](https://github.com/fmtlib/fmt), [spdlog](https://github.com/gabime/spdlog) | MIT |
| [xxHash](https://github.com/Cyan4973/xxHash) | BSD-2-Clause |
| [SIMDe](https://github.com/simd-everywhere/simde) | MIT |
| [toml++](https://github.com/marzer/tomlplusplus) | MIT |
| [o1heap](https://github.com/pavel-kirienko/o1heap) | MIT |
| [utfcpp](https://github.com/nemtrif/utfcpp) | BSL-1.0 |
| [CLI11](https://github.com/CLIUtils/CLI11) | BSD-3-Clause |
| [libnx](https://github.com/switchbrew/libnx) and the devkitA64 runtime (newlib, libstdc++) | ISC; newlib and GCC runtime licenses |

### FFmpeg and libmspack (LGPL)

The NRO links FFmpeg and libmspack statically, as they are in the SDK tree of sa2-nx (its own changes to FFmpeg are
listed in its THIRD_PARTY_NOTICES.md). Everything needed to rebuild and relink the NRO with a modified version of
either library is described in `README.md` (Building from source) and is available from the projects above.

## Game data

This repository contains no assets or program code from Operation Darkness. The NRO contains the game's program code,
recompiled from the player's own copy at build time, and no game data. The game's shaders are found in the player's
own game files and translated on the player's console. Some settings descriptions name shaders of the game by the
hash the renderer gives them; these hashes contain no game data. The NRO's icon is the game's cover art, which belongs
to its owner.

## Trademarks

Operation Darkness is a trademark of its respective owner (the game was developed by Success and published in North
America by Atlus). Nintendo Switch is a trademark of Nintendo. Xbox 360 is a trademark of Microsoft. This project is
not affiliated with or endorsed by any of them.
