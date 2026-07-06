# SaidaEngine web runtime target (Étape 16.4/16.6).
#
# Build (emsdk activated, from the repo root):
#   ./web/build_web.sh                                   # wraps the two lines below
#   emcmake cmake -S web/runtime -B build-web -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-web
# Serve with COOP/COEP headers:  python web/serve.py build-web
#
# Requires the WGSL shaders: cmake --build build --target WebShaders (desktop tree).

cmake_minimum_required(VERSION 3.20)
project(SaidaWebRuntime CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This target is Emscripten-only; configure with emcmake.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SAIDA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# glm is vendored (third_party/glm) so the web build never leaks the desktop
# toolchain's system include directory into the Emscripten sysroot.
set(GLM_INCLUDE_DIR ${SAIDA_ROOT}/third_party/glm CACHE PATH "Directory containing glm/")

add_executable(saida_web
    main.cpp
    # Spike S0 (PLAN_LIVE_EDIT_WEB.md): authoring-core linke dans le runtime
    # d'edition web (invariant 0.2). Reference par le binding saida_apply_op.
    ${SAIDA_ROOT}/src/authoring/EngineManifest.cpp
    ${SAIDA_ROOT}/src/authoring/SceneSnapshot.cpp
    ${SAIDA_ROOT}/src/authoring/SaidaOp.cpp
    ${SAIDA_ROOT}/src/authoring/SaidaOpApplier.cpp
    ${SAIDA_ROOT}/src/core/Reflection.cpp
    ${SAIDA_ROOT}/src/core/Profiler.cpp
    ${SAIDA_ROOT}/src/core/Camera.cpp
    ${SAIDA_ROOT}/src/graphics/GeometryRegistry.cpp
    ${SAIDA_ROOT}/src/graphics/Material.cpp
    ${SAIDA_ROOT}/src/graphics/Mesh.cpp
    ${SAIDA_ROOT}/src/graphics/Primitives.cpp
    ${SAIDA_ROOT}/src/graphics/ResourceManager.cpp
    ${SAIDA_ROOT}/src/graphics/ShadowMap.cpp
    ${SAIDA_ROOT}/src/graphics/StbImageUsage.cpp
    ${SAIDA_ROOT}/src/graphics/TinyObjUsage.cpp
    ${SAIDA_ROOT}/src/fx/ParticleEffect.cpp
    ${SAIDA_ROOT}/src/fx/ParticlePresetLibrary.cpp
    ${SAIDA_ROOT}/src/fx/ParticleQuality.cpp
    ${SAIDA_ROOT}/src/fx/ParticleRuntime.cpp
    ${SAIDA_ROOT}/src/project/AssetRegistry.cpp
    ${SAIDA_ROOT}/src/render/GIVolume.cpp
    ${SAIDA_ROOT}/src/render/RenderFeatureRegistry.cpp
    ${SAIDA_ROOT}/src/render/features/ParticleFeature.cpp
    ${SAIDA_ROOT}/src/render/features/SkyboxFeature.cpp
    ${SAIDA_ROOT}/src/render/features/WaterFeature.cpp
    ${SAIDA_ROOT}/src/render/PostProcessor.cpp
    ${SAIDA_ROOT}/src/render/Renderer.cpp
    ${SAIDA_ROOT}/src/scenario/ScenarioRegistry.cpp
    ${SAIDA_ROOT}/src/scene/Behaviour.cpp
    ${SAIDA_ROOT}/src/scene/BehaviourRegistry.cpp
    ${SAIDA_ROOT}/src/scene/CameraNode.cpp
    ${SAIDA_ROOT}/src/scene/LightNode.cpp
    ${SAIDA_ROOT}/src/scene/MeshLod.cpp
    ${SAIDA_ROOT}/src/scene/MeshNode.cpp
    ${SAIDA_ROOT}/src/scene/Node.cpp
    ${SAIDA_ROOT}/src/scene/NodeId.cpp
    # create_node op (SaidaOpApplier) builds reflected nodes by name via the
    # NodeRegistry; ReflectedTypesWeb.cpp registers the web-renderable subset.
    ${SAIDA_ROOT}/src/scene/NodeRegistry.cpp
    ${SAIDA_ROOT}/src/scene/ReflectedTypesWeb.cpp
    ${SAIDA_ROOT}/src/scene/ParticleSystemNode.cpp
    ${SAIDA_ROOT}/src/scene/Scene.cpp
    ${SAIDA_ROOT}/src/scene/WaterNode.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/BindGroup.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Buffer.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/CommandEncoder.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Device.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Pipeline.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/RenderTexture.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Sampler.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Surface.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Texture.cpp
)

target_include_directories(saida_web PRIVATE
    ${SAIDA_ROOT}/src
    ${GLM_INCLUDE_DIR}
    ${SAIDA_ROOT}/third_party/json
    ${SAIDA_ROOT}/third_party/stb
    ${SAIDA_ROOT}/third_party/tinyobjloader
)
target_compile_definitions(saida_web PRIVATE SAIDA_RHI_WEBGPU GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)

# Size-conscious flags (16.6): -Oz in release, plus link-time stripping.
target_compile_options(saida_web PRIVATE $<$<CONFIG:Release>:-Oz>)

target_link_options(saida_web PRIVATE
    --use-port=emdawnwebgpu
    -sUSE_GLFW=3
    # Mémoire FIXE (pas d'ALLOW_MEMORY_GROWTH) : les Chrome récents refusent les
    # vues sur un ArrayBuffer resizable dans writeTexture/writeBuffer ("must not
    # be resizable") et le port emdawnwebgpu bundlé ne fait pas encore la copie.
    # 512 Mo couvrent largement les scènes d'édition web actuelles.
    -sINITIAL_MEMORY=512MB
    -sEXIT_RUNTIME=0
    # Spike S0: exposer ccall/cwrap pour piloter saida_apply_op depuis le JS.
    # FS (Track 1-F): la plateforme écrit les fichiers projet (obj/textures/
    # asset_registry.json) dans MEMFS sous /project avant saida_load_snapshot.
    -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,FS
    -sEXPORTED_FUNCTIONS=_main,_malloc,_free,_saida_apply_op,_saida_engine_manifest,_saida_scene_snapshot,_saida_scene_snapshot_compare,_saida_load_snapshot,_saida_pick
    $<$<CONFIG:Release>:-Oz>
    # WGSL shaders packaged into MEMFS at /shaders (16.1 web FS: preload).
    # Paths are quoted so a checkout under a directory with spaces still links.
    "SHELL:--preload-file \"${SAIDA_ROOT}/build/shaders/wgsl@/shaders\""
    # BeachDemo scene + sky texture, mapped to /project (parity-test scene).
    "SHELL:--preload-file \"${SAIDA_ROOT}/BeachDemo/scenes/beach.scene@/project/beach.scene\""
    "SHELL:--preload-file \"${SAIDA_ROOT}/BeachDemo/assets/skies/sunset.png@/project/sunset.png\""
    "SHELL:--shell-file \"${CMAKE_CURRENT_SOURCE_DIR}/shell.html\""
)
target_compile_options(saida_web PRIVATE --use-port=emdawnwebgpu)

# The link step embeds the WGSL shaders + BeachDemo assets (index.data) and the
# shell page: relink when any of them change, not just when a .cpp does.
file(GLOB WEB_WGSL_SHADERS ${SAIDA_ROOT}/build/shaders/wgsl/*.wgsl)
set_property(TARGET saida_web APPEND PROPERTY LINK_DEPENDS
    ${WEB_WGSL_SHADERS} ${CMAKE_CURRENT_SOURCE_DIR}/shell.html
    ${SAIDA_ROOT}/BeachDemo/scenes/beach.scene
    ${SAIDA_ROOT}/BeachDemo/assets/skies/sunset.png)

set_target_properties(saida_web PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
