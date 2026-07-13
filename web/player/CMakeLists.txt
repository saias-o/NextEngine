# SaidaEngine web PLAYER (PLAN_SAIDA_ENGINE_UPDATE §3 — Phase A.1).
#
# Distinct du runtime d'authoring (web/runtime) : ce target compile le vrai
# cycle de jeu — SceneTree, SceneSerializer complet, behaviours gameplay,
# GLTFLoader — et boote un package game.saida comme l'exe desktop.
#
# Build (emsdk activé, depuis la racine) :
#   emcmake cmake -S web/player -B build-web-player -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-web-player
# Servir : python web/serve.py build-web-player

cmake_minimum_required(VERSION 3.20)
project(SaidaWebPlayer LANGUAGES C CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "This target is Emscripten-only; configure with emcmake.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SAIDA_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(GLM_INCLUDE_DIR ${SAIDA_ROOT}/third_party/glm CACHE PATH "Directory containing glm/")

# Décodeurs meshopt (EXT_meshopt_compression au chargement des GLB).
file(GLOB MESHOPT_SOURCES ${SAIDA_ROOT}/third_party/meshoptimizer/src/*.cpp)

# QuickJS reste le runtime JavaScript unique du moteur. Le player le compile
# depuis les mêmes sources vendues que le target desktop.
add_library(quickjs STATIC
    ${SAIDA_ROOT}/third_party/quickjs/dtoa.c
    ${SAIDA_ROOT}/third_party/quickjs/libregexp.c
    ${SAIDA_ROOT}/third_party/quickjs/libunicode.c
    ${SAIDA_ROOT}/third_party/quickjs/quickjs.c
)
target_include_directories(quickjs PUBLIC ${SAIDA_ROOT}/third_party/quickjs)
target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)

add_executable(saida_player
    main.cpp
    # --- game core (le même moteur de jeu que le desktop, §2.1) ---
    ${SAIDA_ROOT}/src/core/FileWatcher.cpp
    ${SAIDA_ROOT}/src/core/Input.cpp
    ${SAIDA_ROOT}/src/core/PlatformCaps.cpp
    ${SAIDA_ROOT}/src/core/Reflection.cpp
    ${SAIDA_ROOT}/src/core/Profiler.cpp
    ${SAIDA_ROOT}/src/core/Camera.cpp
    ${SAIDA_ROOT}/src/runtime/BootManifest.cpp
    ${SAIDA_ROOT}/src/project/AssetRegistry.cpp
    ${SAIDA_ROOT}/src/project/AssetLoader.cpp
    ${SAIDA_ROOT}/src/project/Project.cpp
    ${SAIDA_ROOT}/src/scene/Behaviour.cpp
    ${SAIDA_ROOT}/src/scene/BehaviourRegistry.cpp
    ${SAIDA_ROOT}/src/scene/Blackboard.cpp
    ${SAIDA_ROOT}/src/scene/CameraNode.cpp
    ${SAIDA_ROOT}/src/scene/GLTFLoader.cpp
    ${SAIDA_ROOT}/src/scene/GltfMeshopt.cpp
    ${SAIDA_ROOT}/src/scene/LightNode.cpp
    ${SAIDA_ROOT}/src/scene/MeshLod.cpp
    ${SAIDA_ROOT}/src/scene/MeshNode.cpp
    ${SAIDA_ROOT}/src/scene/Node.cpp
    ${SAIDA_ROOT}/src/scene/NodeId.cpp
    ${SAIDA_ROOT}/src/scene/NodeRegistry.cpp
    ${SAIDA_ROOT}/src/scene/ParticleSystemNode.cpp
    ${SAIDA_ROOT}/src/scene/ReflectedTypesPlayer.cpp
    ${SAIDA_ROOT}/src/scene/RotatorBehaviour.cpp
    ${SAIDA_ROOT}/src/scene/Scene.cpp
    ${SAIDA_ROOT}/src/scene/SceneSerializer.cpp
    ${SAIDA_ROOT}/src/scene/SceneTree.cpp
    ${SAIDA_ROOT}/src/scene/SceneTimerQueue.cpp
    ${SAIDA_ROOT}/src/scene/SignalWiring.cpp
    ${SAIDA_ROOT}/src/scene/SpawnerBehaviour.cpp
    ${SAIDA_ROOT}/src/scene/StateMachineBehaviour.cpp
    ${SAIDA_ROOT}/src/scene/Transform.cpp
    ${SAIDA_ROOT}/src/scene/WaterNode.cpp
    ${SAIDA_ROOT}/src/scripting/JsContext.cpp
    ${SAIDA_ROOT}/src/scripting/JsEngineBindings.cpp
    ${SAIDA_ROOT}/src/scripting/JsRuntime.cpp
    ${SAIDA_ROOT}/src/scripting/JsTimerBindings.cpp
    ${SAIDA_ROOT}/src/scripting/ScriptBehaviour.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimBlackboard.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimGraphAsset.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimGraphParser.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimStateMachine.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimationClip.cpp
    ${SAIDA_ROOT}/src/scene/animation/Animator.cpp
    ${SAIDA_ROOT}/src/scene/animation/BlendNode.cpp
    ${SAIDA_ROOT}/src/scene/animation/ClipNode.cpp
    ${SAIDA_ROOT}/src/scene/animation/ClipView.cpp
    ${SAIDA_ROOT}/src/scene/animation/Pose.cpp
    ${SAIDA_ROOT}/src/scene/animation/Retarget.cpp
    ${SAIDA_ROOT}/src/scene/animation/RetargetProfile.cpp
    ${SAIDA_ROOT}/src/scene/animation/Rig.cpp
    ${SAIDA_ROOT}/src/tools/AutoLODBridge.cpp
    # --- rendu (mêmes sources que le viewer web) ---
    ${SAIDA_ROOT}/src/graphics/GeometryRegistry.cpp
    ${SAIDA_ROOT}/src/graphics/Material.cpp
    ${SAIDA_ROOT}/src/graphics/Mesh.cpp
    ${SAIDA_ROOT}/src/graphics/Primitives.cpp
    ${SAIDA_ROOT}/src/graphics/ResourceManager.cpp
    ${SAIDA_ROOT}/src/graphics/ShadowMap.cpp
    ${SAIDA_ROOT}/src/graphics/StbImageUsage.cpp
    ${SAIDA_ROOT}/src/graphics/TinyObjUsage.cpp
    ${SAIDA_ROOT}/src/graphics/cgltf_impl.cpp
    ${SAIDA_ROOT}/src/fx/ParticleEffect.cpp
    ${SAIDA_ROOT}/src/fx/ParticlePresetLibrary.cpp
    ${SAIDA_ROOT}/src/fx/ParticleQuality.cpp
    ${SAIDA_ROOT}/src/fx/ParticleRuntime.cpp
    ${SAIDA_ROOT}/src/render/CameraDirector.cpp
    ${SAIDA_ROOT}/src/render/GIVolume.cpp
    ${SAIDA_ROOT}/src/render/PostProcessor.cpp
    ${SAIDA_ROOT}/src/render/RenderFeatureRegistry.cpp
    ${SAIDA_ROOT}/src/render/Renderer.cpp
    ${SAIDA_ROOT}/src/render/features/ParticleFeature.cpp
    ${SAIDA_ROOT}/src/render/features/SkyboxFeature.cpp
    ${SAIDA_ROOT}/src/render/features/WaterFeature.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/BindGroup.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Buffer.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/CommandEncoder.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Device.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Pipeline.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/RenderTexture.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Sampler.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Surface.cpp
    ${SAIDA_ROOT}/src/rhi/webgpu/Texture.cpp
    ${MESHOPT_SOURCES}
)

target_include_directories(saida_player PRIVATE
    ${SAIDA_ROOT}/src
    ${GLM_INCLUDE_DIR}
    ${SAIDA_ROOT}/third_party/json
    ${SAIDA_ROOT}/third_party/stb
    ${SAIDA_ROOT}/third_party/tinyobjloader
    ${SAIDA_ROOT}/third_party/cgltf
    ${SAIDA_ROOT}/third_party/meshoptimizer/src
    ${SAIDA_ROOT}/third_party/quickjs
)
target_compile_definitions(saida_player PRIVATE
    SAIDA_RHI_WEBGPU SAIDA_SCENE_TREE_TIMERS
    GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_compile_options(saida_player PRIVATE $<$<CONFIG:Release>:-Oz> --use-port=emdawnwebgpu)
target_link_libraries(saida_player PRIVATE quickjs)

target_link_options(saida_player PRIVATE
    --use-port=emdawnwebgpu
    -sUSE_GLFW=3
    -sINITIAL_MEMORY=512MB
    -sEXIT_RUNTIME=0
    $<$<CONFIG:Release>:-Oz>
    "SHELL:--preload-file \"${SAIDA_ROOT}/build/shaders/wgsl@/shaders\""
    # Le package du jeu de référence, monté tel quel sous /project.
    -sFORCE_FILESYSTEM=1
    "SHELL:--shell-file \"${CMAKE_CURRENT_SOURCE_DIR}/shell.html\""
)

file(GLOB WEB_WGSL_SHADERS ${SAIDA_ROOT}/build/shaders/wgsl/*.wgsl)
set_property(TARGET saida_player APPEND PROPERTY LINK_DEPENDS
    ${WEB_WGSL_SHADERS} ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)

set_target_properties(saida_player PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
