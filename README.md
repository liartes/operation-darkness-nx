<h1 align=center>operation-darkness-nx</h1>

<p align=center><b>An unofficial, fan-made Nintendo Switch port of the Xbox 360 game Operation Darkness (2008).</b></p>

> [!IMPORTANT]
> operation-darkness-nx is a non-commercial fan project. It is not affiliated with, endorsed by or supported by
> Success, Atlus, Microsoft or Nintendo. It contains no game files: it runs only with your own copy of the game.
> Please don't ask the game's publishers for help with it.

It's not an emulator. The PowerPC code of the game's `default.xex` is statically recompiled to C++ with
[ReXGlue](https://github.com/rexglue/rexglue-sdk) and compiled for the Switch. The game is drawn by
[reNut](https://github.com/masterspike52/reNut)'s Direct3D-level renderer on deko3d, the Switch's native graphics
API, and the console translates the game's shaders itself: nothing has to be prepared on a computer.

> [!NOTE]
> You need your own copy of Operation Darkness for Xbox 360 (USA release): a disc image (ISO) made from your disc.

## What you need

- A Nintendo Switch running custom firmware ([Atmosphère](https://github.com/Atmosphere-NX/Atmosphere)).
- A disc image of Operation Darkness (USA), made from your own disc. Only the USA release has been tested.
- About 5 GB free on the SD card (4.6 GB of game files, plus the translated shaders and your saves). FAT32 and
  exFAT both work: no game file is larger than 4 GB.
- [Python 3](https://www.python.org) on your computer, to extract the game files from the disc image (or any Xbox
  360 ISO extraction tool that keeps the folder layout).
- [Sphaira](https://github.com/ITotalJustice/sphaira), to create a Home menu icon (forwarder) for the game.

## How to install

1. Extract `operation-darkness-nx-<version>.zip` to the root of your SD card. You get
   `sdmc:/switch/operation-darkness-nx/operation-darkness-nx.nro`.
2. Extract the game files from your disc image with [`tools/extract_xiso.py`](tools/extract_xiso.py) (a single
   Python file with no dependencies, also in the zip, in `switch/operation-darkness-nx/tools/`):

   ```sh
   python3 extract_xiso.py "Operation Darkness (USA).iso" game_root
   ```

   Then copy the `game_root` folder into `sdmc:/switch/operation-darkness-nx/`. You should get
   `sdmc:/switch/operation-darkness-nx/game_root/default.xex`, next to the game's other files and folders.
3. In Sphaira, select `operation-darkness-nx.nro` and choose **Install Forwarder**. This creates a Home menu icon.
4. Start the game from that icon, **twice**: on the first launch, the game sets up its forwarder so that it can use
   all four CPU cores, which takes effect from the next launch. Quit with the Home button and start it again.

   You can also start the game by holding **R** while starting any installed game (title override), but it then
   gets only three CPU cores. Launching from the Album (applet mode) is not supported.

On the first launch, an installation screen with a progress bar appears while the console finds the game's shaders
and translates them for its GPU. It takes a few minutes (about six on our console); then the game starts. The next
launches skip this step.

Your folder then looks like this:

```text
/switch/operation-darkness-nx/
  operation-darkness-nx.nro
  opdarkness.toml             (optional settings, written empty on the first launch)
  game_root/                  (the game files, from your disc image)
  opdarkness/                 (your saves)
  deko3d_shaders.bin, ...     (the translated shaders, written by the installation)
  logs/                       (log files)
```

> [!NOTE]
> A few shaders are built by the game itself during play. The console translates them in the background the first
> time they are needed: something can be missing on screen for a fraction of a second, once.

**Updating:** replace `operation-darkness-nx.nro` with the new one. Your game files and saves stay where they are; if
the new version changes the shader translation, the installation runs again on its own.

## Controls

The A, B, X and Y buttons follow their letters, like Switch games, not their positions on the Xbox 360 controller.
The in-game prompts still show the Xbox 360 icons. The player's name is typed with the Switch keyboard.

## Settings

The optional `opdarkness.toml` next to `operation-darkness-nx.nro` overrides the built-in settings. You don't need to
change anything to play.

The resolution follows the load: 720p when the console keeps up, down to 540p (and lower in the heaviest scenes) to
keep the game smooth. In handheld mode the port asks the system for the official 460.8 MHz GPU profile, which is not
an overclock; during loading screens it uses the system's official CPU boost mode, like the console's own games.
Overclocking (for example with sys-clk) is your choice and is not required. If you do overclock, raise the **CPU**:
in battle the game is mostly limited by its drawing thread.

## Known issues

- The button icons are the Xbox 360 ones.
- Docked (TV) mode has not been tested yet: the port was played in handheld mode only.
- A brief stutter, or an object missing for a moment, the first time a shader built by the game is needed (see
  above).

## Reporting a problem

Please give the version of the port and, after a crash, the files of `sdmc:/switch/operation-darkness-nx/logs/`.

## Building from source

The build follows the Switch ports of ChanseyIsTheBest and is not a one-command build. You need:

- **[sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx)** in `~/workspace/sa2-nx` at commit `fdd6f14`, with
  [`patches/sa2-nx-sdk.patch`](patches/sa2-nx-sdk.patch) applied to it and `patches/glslang-verrou.patch` to
  `sdk/thirdparty/glslang` (see [`patches/README.md`](patches/README.md)): its ReXGlue SDK fork, code generator and
  Switch toolchain.
- reNut's renderer as sa2-nx carries it (sa2-nx commit `adf3a4e`): `tools/import_renut_sa2.sh adf3a4e` copies its
  third-party code (plume with its deko3d back end, XenosRecomp, uam, Dear ImGui) into `out/renut-tiers/`; the
  renderer itself, with this port's changes, is in `src/renut_engine/`.
- [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) (tag `vulkan-sdk-1.4.304.0`) and
  [rpmalloc](https://github.com/mjansson/rpmalloc) 1.4.5, in `~/workspace/_tools/`.
- **Docker** with the `devkitpro/devkita64` image, and clang 20, CMake, Ninja and Python 3 on the host.
- **Your own game files** in `assets/` (`tools/extract_xiso.py`): the recompiled game code is generated on your
  machine from your `default.xex` and never distributed.

Then generate the code (`rexglue codegen opdarkness_manifest.toml` with the generator built from sa2-nx, then
`tools/apres_codegen.py`), build the PC version once with `-DOD_RENUT=ON` (it provides the renderer's internal
shaders to the Switch build), and build the `.nro` with `tools/switch/compiler.sh` and
`CMAKE_OPTS="-DOD_RENUT=ON -DOD_RENUT_SANS_DONNEES=ON -DOD_SWITCH_LTO=ON"`. The release build also uses a
profile-guided optimization profile from play sessions (`OD_PGO`, see `CMakeLists.txt`). The development notes
(`CLAUDE.md` and `docs/`, in French) describe each step and tool in detail.

## Acknowledgements

This port would not exist without the work of these projects and their developers. Thank you!

**The foundations of the port**

- [sa2-nx](https://github.com/ChanseyIsTheBest/sa2-nx) by ChanseyIsTheBest: its porting guide, its ReXGlue SDK fork
  with the Switch layer, its code generator improvements and tools, and the reNut renderer as it carries it.
- [reNut](https://github.com/masterspike52/reNut) by masterspike52: the Direct3D-level renderer the game is drawn
  with.
- [renut-nx](https://github.com/ChanseyIsTheBest/renut-nx),
  [renut_nx_ultimate_base](https://github.com/ChanseyIsTheBest/renut_nx_ultimate_base) and
  [rr6-nx](https://github.com/ChanseyIsTheBest/rr6-nx) by ChanseyIsTheBest: the documented optimizations of the
  renderer, of the deko3d shader translation and of the recompiled code that this port took up (localized copies of
  hot functions, four-core mode, snapshot deltas, critical sections, shader passes and more).
- [NFSMW-NX](https://github.com/StevensND/nfsmw-nx) by StevensND: the Switch layer and toolchain the ports above
  build on, and its deko3d shader path through SPIRV-Cross.
- [masseffect-nx](https://github.com/nebadasSwifty/masseffect-nx) by NebadasSwifty: the native renderer and CPU
  enhancement framework sa2-nx started from.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) by Tom Clay and
  [Xenia](https://github.com/xenia-project/xenia) by Ben Vanik and the Xenia contributors: the static recompiler,
  the Xbox 360 runtime and the research behind them.
- [plume](https://github.com/renderbag/plume) by renderbag, with the deko3d back end of the ports above;
  [XenosRecomp](https://github.com/hedge-dev/XenosRecomp) by hedge-dev; [uam](https://github.com/devkitPro/uam) by
  fincs: the graphics layer, the Xenos shader translator and the deko3d shader compiler.
- [torchlight-nx](https://github.com/liartes/torchlight-nx): the method, tools and project layout this port
  started from.

**Switch homebrew**

- [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere), [libnx](https://github.com/switchbrew/libnx),
  [deko3d](https://github.com/devkitPro/deko3d) and [devkitPro](https://github.com/devkitPro): the custom firmware,
  the Switch library, the graphics API and the toolchain. [Sphaira](https://github.com/ITotalJustice/sphaira): the
  forwarders.

**Libraries**

- [glslang](https://github.com/KhronosGroup/glslang), [SPIRV-Tools](https://github.com/KhronosGroup/SPIRV-Tools)
  and [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross): the game's shaders translated on the console.
- [rpmalloc](https://github.com/mjansson/rpmalloc): the memory allocator of the shader translation threads.
- [Dear ImGui](https://github.com/ocornut/imgui): the installation screen.
- [FFmpeg](https://github.com/FFmpeg/FFmpeg) and [libmspack](https://github.com/kyz/libmspack): audio
  decoding and decompression in the runtime.
- [fmt](https://github.com/fmtlib/fmt), [spdlog](https://github.com/gabime/spdlog),
  [toml++](https://github.com/marzer/tomlplusplus), [xxHash](https://github.com/Cyan4973/xxHash),
  [SIMDe](https://github.com/simd-everywhere/simde), [o1heap](https://github.com/pavel-kirienko/o1heap),
  [utfcpp](https://github.com/nemtrif/utfcpp) and [CLI11](https://github.com/CLIUtils/CLI11).

## Legal

operation-darkness-nx is an unofficial, non-commercial fan project, with no affiliation with Success, Atlus,
Microsoft, Nintendo or the current owners of Operation Darkness. Operation Darkness is a trademark of its respective
owner, used here only to say which game this port works with. This repository contains no assets or program code
from the original game. The `.nro` contains the game's program code, recompiled, but no game data: users must provide
their own legally obtained copy, whose files they copy to their own console. The `.nro`'s icon is the game's cover
art, which belongs to its owner. Running homebrew requires custom firmware, which violates Nintendo's terms of
service and can get a console banned: your call.

Source code is provided under the GPL-3.0 License (see [LICENSE](LICENSE)). The changes to other projects in
`patches/` are under the licenses of those projects, and the code taken from them under theirs (see
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).
