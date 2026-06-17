# NextEngine

A lightweight 3D game engine written in **C++17 + Vulkan**, built step by step with a focus on clean architecture, clear ownership, and readable code. No over-engineering — no heavy ECS, no AAA render-graph unless justified.

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
- **Scripting-ready** — node + behaviour + signal architecture (Lua scripting planned)

## Build

**Requirements:** MSYS2 ucrt64, GCC, CMake, Ninja, Vulkan SDK, glslc

```sh
cmake -S . -B build -G Ninja
cmake --build build
./build/NextEngine.exe
```

With Vulkan validation layers:
```sh
./run.sh
```

Assets and shader paths are baked as absolute paths by CMake — the exe runs from any directory.

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
third_party/   VMA, xatlas, Jolt, OpenXR SDK, Dear ImGui, stb, tinyobjloader, cgltf
```

The engine compiles as a static library (`ne_engine`); game code links against it — iterating game logic never recompiles the engine.

## Status

Alpha — actively developed. See [CLAUDE.md](CLAUDE.md) for the full roadmap.

## License

Source available. License TBD before stable release.
