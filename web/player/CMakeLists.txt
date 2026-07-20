# SaidaEngine web player.
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

# Jolt en wasm (mêmes options que le build desktop ; job system mono-thread
# choisi à la compilation dans PhysicsWorld).
set(INTERPROCEDURAL_OPTIMIZATION OFF)
set(ENABLE_ALL_WARNINGS OFF)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)
set(ENABLE_OBJECT_STREAM OFF)
set(ENABLE_INSTALL OFF)
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(DOUBLE_PRECISION OFF)
add_subdirectory(${SAIDA_ROOT}/third_party/jolt/Build ${CMAKE_BINARY_DIR}/jolt)

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

# RmlUi reste l'unique backend UI sur toutes les plateformes. Le player Web
# utilise le même rasterizer CPU déterministe que WebCanvas, puis compose la
# texture produite dans le pass WebGPU.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF ON CACHE BOOL "" FORCE)
add_subdirectory(${SAIDA_ROOT}/third_party/freetype ${CMAKE_BINARY_DIR}/freetype)
if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
endif()

set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_BACKEND "auto" CACHE STRING "" FORCE)
set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
set(RMLUI_HARFBUZZ_SAMPLE OFF CACHE BOOL "" FORCE)
set(RMLUI_PRECOMPILED_HEADERS OFF CACHE BOOL "" FORCE)
set(RMLUI_COMPILER_OPTIONS OFF CACHE BOOL "" FORCE)
add_subdirectory(${SAIDA_ROOT}/third_party/rmlui ${CMAKE_BINARY_DIR}/rmlui)

add_executable(saida_player
    main.cpp
    # --- game core (le même moteur de jeu que le desktop, §2.1) ---
    ${SAIDA_ROOT}/src/core/AtomicFile.cpp
    ${SAIDA_ROOT}/src/core/FileWatcher.cpp
    ${SAIDA_ROOT}/src/core/Input.cpp
    ${SAIDA_ROOT}/src/core/InputProfile.cpp
    ${SAIDA_ROOT}/src/core/Paths.cpp
    ${SAIDA_ROOT}/src/core/PlatformCaps.cpp
    ${SAIDA_ROOT}/src/core/Reflection.cpp
    ${SAIDA_ROOT}/src/core/Profiler.cpp
    ${SAIDA_ROOT}/src/core/Camera.cpp
    ${SAIDA_ROOT}/src/runtime/BootManifest.cpp
    ${SAIDA_ROOT}/src/runtime/RuntimeRoundTripContract.cpp
    ${SAIDA_ROOT}/src/project/AssetRegistry.cpp
    ${SAIDA_ROOT}/src/project/AssetLoader.cpp
    ${SAIDA_ROOT}/src/project/Project.cpp
    ${SAIDA_ROOT}/src/project/PlayerStorage.cpp
    ${SAIDA_ROOT}/src/scene/Behaviour.cpp
    ${SAIDA_ROOT}/src/scene/BehaviourRegistry.cpp
    ${SAIDA_ROOT}/src/scene/Blackboard.cpp
    ${SAIDA_ROOT}/src/scene/CameraFollowBehaviour.cpp
    ${SAIDA_ROOT}/src/scene/CameraNode.cpp
    ${SAIDA_ROOT}/src/scene/CharacterBehaviour.cpp
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
    ${SAIDA_ROOT}/src/scene/UINode.cpp
    ${SAIDA_ROOT}/src/scene/UICanvasNode.cpp
    ${SAIDA_ROOT}/src/scene/UITextNode.cpp
    ${SAIDA_ROOT}/src/scene/WaterNode.cpp
    ${SAIDA_ROOT}/src/physics/CollisionObjectNode.cpp
    ${SAIDA_ROOT}/src/physics/CollisionShapeNode.cpp
    ${SAIDA_ROOT}/src/physics/CharacterBodyNode.cpp
    ${SAIDA_ROOT}/src/physics/PhysicsNodes.cpp
    ${SAIDA_ROOT}/src/physics/JointNodes.cpp
    ${SAIDA_ROOT}/src/physics/PhysicsWorld.cpp
    ${SAIDA_ROOT}/src/audio/AudioManager.cpp
    ${SAIDA_ROOT}/src/audio/AudioSourceBehaviour.cpp
    ${SAIDA_ROOT}/src/audio/AudioUsage.cpp
    ${SAIDA_ROOT}/src/scripting/JsContext.cpp
    ${SAIDA_ROOT}/src/scripting/JsEngineBindings.cpp
    ${SAIDA_ROOT}/src/scripting/JsRuntime.cpp
    ${SAIDA_ROOT}/src/scripting/JsTimerBindings.cpp
    ${SAIDA_ROOT}/src/scripting/ScriptBehaviour.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimBlackboard.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimGraphAsset.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimGraphParser.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimationAssetDecoders.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimStateMachine.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimationClip.cpp
    ${SAIDA_ROOT}/src/scene/animation/AnimationSequence.cpp
    ${SAIDA_ROOT}/src/scene/animation/Animator.cpp
    ${SAIDA_ROOT}/src/scene/animation/BlendNode.cpp
    ${SAIDA_ROOT}/src/scene/animation/ClipNode.cpp
    ${SAIDA_ROOT}/src/scene/animation/ClipView.cpp
    ${SAIDA_ROOT}/src/scene/animation/Pose.cpp
    ${SAIDA_ROOT}/src/scene/animation/Retarget.cpp
    ${SAIDA_ROOT}/src/scene/animation/RetargetProfile.cpp
    ${SAIDA_ROOT}/src/scene/animation/Rig.cpp
    ${SAIDA_ROOT}/src/scene/animation/RigAsset.cpp
    ${SAIDA_ROOT}/src/scene/animation/SequenceDirectorBehaviour.cpp
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
    ${SAIDA_ROOT}/src/graphics/UIRenderer.cpp
    ${SAIDA_ROOT}/src/graphics/cgltf_impl.cpp
    ${SAIDA_ROOT}/src/ui/RmlUiRenderInterface.cpp
    ${SAIDA_ROOT}/src/ui/RmlUiRuntime.cpp
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
    ${SAIDA_ROOT}/third_party/jolt/Build/..
    ${SAIDA_ROOT}/third_party/miniaudio
)
target_compile_definitions(saida_player PRIVATE
    SAIDA_RHI_WEBGPU SAIDA_SCENE_TREE_TIMERS
    GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_compile_options(saida_player PRIVATE -fexceptions $<$<CONFIG:Release>:-Oz> --use-port=emdawnwebgpu)
target_link_libraries(saida_player PRIVATE quickjs Jolt RmlUi::RmlUi)

target_link_options(saida_player PRIVATE
    --use-port=emdawnwebgpu
    -sUSE_GLFW=3
    -sINITIAL_MEMORY=512MB
    -sSTACK_SIZE=4MB
    -lidbfs.js
    -sEXIT_RUNTIME=0
    -sDISABLE_EXCEPTION_CATCHING=0
    -sEXPORTED_RUNTIME_METHODS=ccall
    -sEXPORTED_FUNCTIONS=_main,_saida_player_status
    $<$<CONFIG:Release>:-Oz>
    "SHELL:--preload-file \"${SAIDA_ROOT}/build/shaders/wgsl@/shaders\""
    # Fonts par défaut du moteur, au chemin canonique assets/fonts/ (même liste
    # que RmlUiRuntime::kEngineFonts; NotoEmoji volontairement hors bundle web).
    "SHELL:--preload-file \"${SAIDA_ROOT}/third_party/rmlui/Samples/assets/LatoLatin-Regular.ttf@/assets/assets/fonts/LatoLatin-Regular.ttf\""
    "SHELL:--preload-file \"${SAIDA_ROOT}/third_party/rmlui/Samples/assets/LatoLatin-Bold.ttf@/assets/assets/fonts/LatoLatin-Bold.ttf\""
    "SHELL:--preload-file \"${SAIDA_ROOT}/third_party/rmlui/Samples/assets/RobotoMono-Regular.ttf@/assets/assets/fonts/RobotoMono-Regular.ttf\""
    # Le package du jeu de référence, monté tel quel sous /project.
    -sFORCE_FILESYSTEM=1
    "SHELL:--shell-file \"${CMAKE_CURRENT_SOURCE_DIR}/shell.html\""
)

file(GLOB WEB_WGSL_SHADERS ${SAIDA_ROOT}/build/shaders/wgsl/*.wgsl)
set_property(TARGET saida_player APPEND PROPERTY LINK_DEPENDS
    ${WEB_WGSL_SHADERS} ${CMAKE_CURRENT_SOURCE_DIR}/shell.html)

set_target_properties(saida_player PROPERTIES SUFFIX ".html" OUTPUT_NAME "index")
