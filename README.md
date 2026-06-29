# NextEngine

A lightweight 3D game engine written in **C++17 + Vulkan** and LLM-native.

## Features

- **Vulkan 1.3** with Dynamic Rendering, MSAA, VMA memory management
- **Scene graph** — nodes, hierarchy, world-transform propagation (Godot-style)
- **PBR shading** — metallic-roughness, normal maps, IBL, ACES tonemapping
- **Real-time lighting** — Directional / Point / Spot + shadow mapping (PCF)
- **Lightmap baking** — GPU bake with xatlas auto-unwrap and seam dilation
- **Post-processing** — SSAO, bloom, distance fog, HDR
- **DDGI** — dynamic diffuse global illumination (irradiance volume)
- **Skeletal animation** — glTF/BVH, cubic-spline interpolation, retargeting, GPU skinning
- **Physics** — Jolt Physics integration (rigid bodies, characters, areas, triggers)
- **OpenXR / VR** — stereo multiview rendering, action sets, NEXRTK toolkit (grab, teleport, anchors, passthrough)
- **Editor** — scene tree, inspector, file browser, undo/redo, ImGui
- **Scripting-ready** — node + behaviour + signal architecture (in Javascript)

## Build on Windows

The supported development toolchain is **MSYS2 UCRT64 + GCC + CMake +
Ninja**. Keep all C/C++ dependencies in the same UCRT64 environment; mixing
MSVC, MINGW64 and UCRT64 libraries is a common source of obscure linker errors.

### 1. Install the toolchain

Open the **MSYS2 UCRT64** terminal, update MSYS2, then install the required
packages:

```sh
pacman -Syu
pacman -S --needed \
  git git-lfs \
  mingw-w64-ucrt-x86_64-gcc \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-pkgconf \
  mingw-w64-ucrt-x86_64-glfw \
  mingw-w64-ucrt-x86_64-glm \
  mingw-w64-ucrt-x86_64-shaderc \
  mingw-w64-ucrt-x86_64-vulkan-headers \
  mingw-w64-ucrt-x86_64-vulkan-loader \
  mingw-w64-ucrt-x86_64-vulkan-validation-layers
git lfs install
```

`shaderc` provides `glslc`. A recent Vulkan-capable GPU driver is also
required. A separate LunarG Vulkan SDK is optional when using the MSYS2
packages above.

### 2. Clone all vendored dependencies

FreeType, QuickJS-NG and RmlUi are pinned Git submodules. Clone them at the
same time as the engine:

```sh
git clone --recurse-submodules https://github.com/saias-o/NextEngine.git
cd NextEngine
git lfs pull
```

For an existing checkout, or when `third_party/freetype`,
`third_party/quickjs` or `third_party/rmlui` is empty:

```sh
git submodule sync --recursive
git submodule update --init --recursive
git lfs pull
```

Do not replace these dependencies with arbitrary newer revisions: their
gitlinks pin versions known to compile together.

### 3. Configure and compile

Run these commands from the repository root in the UCRT64 terminal:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

For an optimized build, use `-DCMAKE_BUILD_TYPE=Release` when configuring.
Re-running the two commands is safe after source or CMake changes.

### Consignes build Codex

Codex tourne souvent depuis PowerShell, mais le build doit quand meme passer
par l'environnement **MSYS2 UCRT64**. Ne pas appeler `c++.exe` directement
depuis PowerShell : cela peut echouer sans diagnostic utile. Utiliser
`bash.exe` MSYS2, avec `/ucrt64/bin` en tete du `PATH`.

Dans le sandbox Codex, `C:\msys64\tmp` peut etre interdit en ecriture. Dans ce
cas, creer des temporaires dans le workspace et forcer MSYS2/GCC a les utiliser
avant de lancer Ninja :

```powershell
New-Item -ItemType Directory -Force -Path build\tmp, build\msys_home | Out-Null
C:\msys64\usr\bin\bash.exe -lc 'cd /c/Users/evand/Documents/NextEngine && export HOME=/c/Users/evand/Documents/NextEngine/build/msys_home && export TMPDIR=/c/Users/evand/Documents/NextEngine/build/tmp && export TMP=/c/Users/evand/Documents/NextEngine/build/tmp && export TEMP=/c/Users/evand/Documents/NextEngine/build/tmp && export PATH=/ucrt64/bin:/usr/bin:$PATH && cmake --build build --parallel'
```

Symptomes typiques quand cette redirection manque :

- `Cannot create temporary file in C:\msys64\tmp\: Permission denied`;
- `c++.exe` ou Ninja sort avec un code d'erreur sans diagnostic C++ clair;
- MSYS2 tente de creer `/home/CodexSandboxOffline` puis retombe sur `/tmp`.

Successful builds produce:

- `build/bin/NextEngine.exe` — engine and demo game;
- `build/bin/NextEngineHub.exe` — project hub;
- `build/bin/NextEngineRuntime.exe` — standalone runtime template;
- `build/tests/*.exe` — test binaries;
- `build/libne_engine.a` — static engine library.

Shaders are compiled to `build/shaders/` by `glslc`. Asset and shader paths
are baked as absolute paths by CMake, so the executable can run from any
working directory.

### 4. Run manually

`NextEngine.exe` is a GUI application with an infinite render loop. A human can
launch it normally:

```sh
./build/bin/NextEngine.exe
```

For a Debug build with Vulkan validation layers, launch it through:

```sh
./run.sh
```

Automated agents should normally stop after a successful build and verify that
the expected artifacts exist. They must not launch the application in a
headless job without an explicit timeout and an available desktop/GPU session.

### Optional AutoLOD tool

AutoLOD is configured and built separately:

```sh
cmake -S autolod -B build/autolod -G Ninja
cmake --build build/autolod --parallel
```

### Build troubleshooting

- `third_party/freetype ... does not contain a CMakeLists.txt`, the equivalent
  RmlUi error, or a missing `third_party/quickjs/dtoa.c` means the submodules
  were not initialized. Run the existing-checkout commands from step 2.
- Confirm that `cmake`, `ninja`, `g++` and `glslc` resolve from
  `/ucrt64/bin` (`which cmake ninja g++ glslc`). If they do not, reopen the
  MSYS2 UCRT64 terminal.
- CMake may first print `Could NOT find Freetype`, followed by
  `Found Freetype::Freetype - Freetype font engine enabled`. This is expected:
  the project uses its vendored FreeType target.
- OpenXR 1.1.60 references `WINAPI_PARTITION_SYSTEM`, which is absent from the
  MinGW Windows headers. The project supplies the correct desktop-only value
  privately to `openxr_loader`; do not patch the vendored OpenXR sources.
- This machine/toolchain has previously made dynamic `libstdc++` linking fail
  with `ld` exit code 116. The MinGW build intentionally links the GCC runtime
  statically in `CMakeLists.txt`.

For Quest testing, compiling is not enough: Meta Quest Link must be running and
the Meta/Oculus runtime must be selected as the active OpenXR runtime. Headset
rendering and controller input require an interactive test with the headset.

The ready-to-test scene is `MyGame/scenes/XRSetup.scene`. Open the `MyGame`
project, open that scene, enable hand tracking in the Quest settings, connect
Quest Link, put the Touch controllers down so the runtime switches to bare
hands, then press Play. XR scenes launch as a separate `--xr` preview process,
because OpenXR must create the Vulkan device before rendering starts; the
desktop editor remains open. Startup logs report `XR hand tracking supported` or
`unavailable`, followed by per-hand `tracking active/lost` transitions. The
procedural blue/orange hands need no external model.

## Architecture

```
src/
  core/        Camera, Input, Time, Log, Signal
  graphics/    VulkanDevice, Swapchain, Pipeline, Buffer, Texture, Mesh, Material
  render/      Renderer, ShadowMap, LightBaker, GIVolume
  scene/        Node, Scene, Behaviour, SceneTree, GLTFLoader, animation/
  physics/     Jolt wrapper (PhysicsWorld, RigidBodyNode, CharacterBodyNode…)
  xr/          OpenXR session, actions, NEXRTK toolkit
  editor/      Scene editor (ImGui-based)
  game/        Demo scene content (lives in the exe, not the engine lib)
shaders/       GLSL → SPIR-V (compiled by glslc at build time)
third_party/   VMA, xatlas, Jolt, OpenXR, QuickJS, RmlUi, FreeType, ImGui, stb…
```

The engine compiles as a static library (`ne_engine`); game code links against it — iterating game logic never recompiles the engine.

## Status

Alpha — actively developed. Read [AGENTS.md](AGENTS.md) first for the complete
architecture, development rules, roadmap and known environment pitfalls.

## License

Source available. License TBD before stable release.
