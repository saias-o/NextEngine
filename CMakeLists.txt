cmake_minimum_required(VERSION 3.20)
project(NextEngine LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Vulkan REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_program(GLSLC glslc REQUIRED)

# Compile shaders
set(SHADER_DIR ${CMAKE_SOURCE_DIR}/shaders)
set(SHADER_OUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

set(SHADERS
    ${SHADER_DIR}/shader.vert
    ${SHADER_DIR}/shader.frag
    ${SHADER_DIR}/shadow.vert
    ${SHADER_DIR}/bake.vert
    ${SHADER_DIR}/bake.frag
    ${SHADER_DIR}/culling.comp
    ${SHADER_DIR}/tonemap.vert
    ${SHADER_DIR}/tonemap.frag
    ${SHADER_DIR}/skybox.vert
    ${SHADER_DIR}/skybox.frag
    ${SHADER_DIR}/water.vert
    ${SHADER_DIR}/water.frag
    ${SHADER_DIR}/ui.vert
    ${SHADER_DIR}/ui.frag
    ${SHADER_DIR}/voxelize.vert
    ${SHADER_DIR}/voxelize.frag
    ${SHADER_DIR}/ddgi_trace.comp
    ${SHADER_DIR}/ddgi_blend.comp
    ${SHADER_DIR}/ddgi_borders.comp
    ${SHADER_DIR}/debug_line.vert
    ${SHADER_DIR}/debug_line.frag
    ${SHADER_DIR}/lightmap_dilate.frag
)

# GLSL headers #included by the shaders above. Listed as a dependency so editing
# them re-triggers compilation of every shader that includes them.
file(GLOB SHADER_INCLUDES ${SHADER_DIR}/*.glsl)

set(SHADER_SPIRV "")
foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC} -I ${SHADER_DIR} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER} ${SHADER_INCLUDES}
        COMMENT "Compiling ${SHADER_NAME} -> ${SHADER_NAME}.spv"
    )
    list(APPEND SHADER_SPIRV ${SPIRV_OUTPUT})
    
    if(SHADER_NAME STREQUAL "shader.vert" OR SHADER_NAME STREQUAL "shader.frag")
        set(BINDLESS_OUTPUT ${SHADER_OUT_DIR}/bindless.${SHADER_NAME}.spv)
        add_custom_command(
            OUTPUT ${BINDLESS_OUTPUT}
            COMMAND ${GLSLC} -I ${SHADER_DIR} -DBINDLESS ${SHADER} -o ${BINDLESS_OUTPUT}
            DEPENDS ${SHADER} ${SHADER_INCLUDES}
            COMMENT "Compiling bindless.${SHADER_NAME} -> bindless.${SHADER_NAME}.spv"
        )
        list(APPEND SHADER_SPIRV ${BINDLESS_OUTPUT})
    endif()

    # Multiview (XR stereo, 1 pass / 2 layers): shaders that read per-eye data via
    # gl_ViewIndex get a -DMULTIVIEW variant. The scene vertex picks per-eye
    # camera matrices; the skybox fragment picks per-eye inverse view-proj.
    if(SHADER_NAME STREQUAL "shader.vert" OR SHADER_NAME STREQUAL "skybox.frag"
       OR SHADER_NAME STREQUAL "water.vert")
        set(MULTIVIEW_OUTPUT ${SHADER_OUT_DIR}/multiview.${SHADER_NAME}.spv)
        add_custom_command(
            OUTPUT ${MULTIVIEW_OUTPUT}
            COMMAND ${GLSLC} -I ${SHADER_DIR} -DMULTIVIEW ${SHADER} -o ${MULTIVIEW_OUTPUT}
            DEPENDS ${SHADER} ${SHADER_INCLUDES}
            COMMENT "Compiling multiview.${SHADER_NAME} -> multiview.${SHADER_NAME}.spv"
        )
        list(APPEND SHADER_SPIRV ${MULTIVIEW_OUTPUT})
    endif()
endforeach()

add_custom_target(Shaders DEPENDS ${SHADER_SPIRV})

# ── Jolt Physics (vendored) ─────────────────────────────────────────────────
# Only the static `Jolt` lib is built — the samples/tests/unit-test targets are
# auto-skipped because Jolt's Build/CMakeLists gates them on being the top-level
# project. The SIMD/define flags Jolt picks are PUBLIC on the target, so anything
# linking it gets a matching ABI automatically.
set(INTERPROCEDURAL_OPTIMIZATION OFF)          # consumer isn't LTO; mixing breaks the MinGW link
set(ENABLE_ALL_WARNINGS OFF)                    # don't -Werror Jolt on this toolchain's warning set
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF)      # MSVC-only feature; off avoids surprises elsewhere
set(ENABLE_OBJECT_STREAM OFF)                   # we don't use Jolt's RTTI ObjectStream serialization
set(ENABLE_INSTALL OFF)                         # no install/export targets needed
set(CROSS_PLATFORM_DETERMINISTIC OFF)
set(DOUBLE_PRECISION OFF)                        # 32-bit positions: smaller/faster, fine for our worlds
add_subdirectory(third_party/jolt/Build ${CMAKE_BINARY_DIR}/jolt)

# ── LLM-native MCP server (Étape 13) ────────────────────────────────────────
# In-process MCP server for LLM-driven editing. Compiled into ne_editor only,
# so the shipped runtime never links it. Set OFF for a pure no-MCP editor.
option(NE_ENABLE_MCP "Enable the in-process MCP server (Étape 13)" ON)

# ── OpenXR (vendored, loader only) ──────────────────────────────────────────
# Set NE_ENABLE_XR=OFF to produce a pure-desktop build with no OpenXR dependency.
option(NE_ENABLE_XR "Enable OpenXR support (Étape 14)" ON)

if(NE_ENABLE_XR)
    # Build just the static openxr_loader from the pre-generated SDK repo (no Python
    # generation, no API layers/tests). The loader finds the active runtime (Quest
    # Link / Oculus) via the registry at runtime. Headers are propagated by the target.
    set(DYNAMIC_LOADER OFF)        # static loader, consistent with the project's -static link
    set(BUILD_API_LAYERS OFF)
    set(BUILD_TESTS OFF)
    set(BUILD_CONFORMANCE_TESTS OFF)
    set(BUILD_SDK_TESTS OFF)
    set(BUILD_ALL_EXTENSIONS OFF)
    add_subdirectory(third_party/openxr ${CMAKE_BINARY_DIR}/openxr)

    # MinGW's winapifamily.h does not define the UWP system partition constant
    # used by OpenXR 1.1.60.  This is a desktop Win32 build, so that partition is
    # absent; defining it to zero also keeps the loader's -Werror=undef happy.
    if (MINGW)
        target_compile_definitions(openxr_loader PRIVATE WINAPI_PARTITION_SYSTEM=0)
    endif()
endif()

# ── QuickJS + RmlUi (vendored, static) ───────────────────────────────────────
# QuickJS is the single JavaScript runtime for gameplay scripts, editor tooling
# and web UI logic. We build only the library, not qjs/qjsc executables.
add_library(quickjs STATIC
    third_party/quickjs/dtoa.c
    third_party/quickjs/libregexp.c
    third_party/quickjs/libunicode.c
    third_party/quickjs/quickjs.c
)
target_include_directories(quickjs PUBLIC third_party/quickjs)
target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
if (WIN32)
    target_compile_definitions(quickjs PRIVATE WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()
if (MINGW)
    target_link_libraries(quickjs PUBLIC m)
endif()

# Freetype is used only by RmlUi's font engine. Disable optional compression /
# shaping dependencies so the engine remains easy to build from source.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HVF ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/freetype ${CMAKE_BINARY_DIR}/freetype)
if (NOT TARGET Freetype::Freetype)
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
add_subdirectory(third_party/rmlui ${CMAKE_BINARY_DIR}/rmlui)

# AutoLOD is built separately: `cmake -S autolod -B build/autolod -G Ninja && cmake --build build/autolod`
# AutoLODBridge resolves build/autolod/autolod.exe at runtime.

# ── Engine library ──────────────────────────────────────────────────────────
# Everything reusable lives here and compiles once. Game and editor targets link
# it, so changing application code does not recompile the engine.
add_library(ne_engine STATIC
    src/Engine.cpp
    src/core/FileWatcher.cpp
    src/core/Window.cpp
    src/core/Camera.cpp
    src/core/Input.cpp
    src/core/Paths.cpp
    src/core/Reflection.cpp
    src/scene/NodeId.cpp
    src/scene/Node.cpp
    src/scene/ReflectedTypes.cpp
    src/scripting/JsContext.cpp
    src/scripting/JsEngineBindings.cpp
    src/scripting/JsRuntime.cpp
    src/scripting/ScriptBehaviour.cpp
    src/ui/RmlUiRenderInterface.cpp
    src/ui/RmlUiRuntime.cpp
    src/scene/Behaviour.cpp
    src/scene/MeshNode.cpp
    src/scene/MeshLod.cpp
    src/scene/LightNode.cpp
    src/scene/CameraNode.cpp
    src/render/CameraDirector.cpp
    src/scene/CameraFollowBehaviour.cpp
    src/scene/Scene.cpp
    src/scene/SceneTree.cpp
    src/scene/SceneSerializer.cpp
    src/scene/SignalWiring.cpp
    src/scene/BVHLoader.cpp
    src/scene/BehaviourRegistry.cpp
    src/scene/NodeRegistry.cpp
    src/scene/WebCanvasNode.cpp
    src/scene/UINode.cpp
    src/scene/UICanvasNode.cpp
    src/scene/UIColorNode.cpp
    src/scene/UIImageNode.cpp
    src/scene/UITextNode.cpp
    src/scene/UIInteractableNode.cpp
    src/scene/UIButtonNode.cpp
    src/scene/UIToggleNode.cpp
    src/scene/animation/Pose.cpp
    src/scene/animation/AnimationClip.cpp
    src/scene/animation/ClipNode.cpp
    src/scene/animation/BlendNode.cpp
    src/scene/animation/AnimStateMachine.cpp
    src/scene/animation/Animator.cpp
    src/scene/animation/AnimBlackboard.cpp
    src/scene/animation/AnimGraphParser.cpp
    src/scene/animation/PlayableDirector.cpp
    src/scene/animation/Retarget.cpp
    src/ui/UIInteractionSystem.cpp
    src/audio/AudioManager.cpp
    src/graphics/VulkanDevice.cpp
    src/graphics/Swapchain.cpp
    src/graphics/Buffer.cpp
    src/graphics/Material.cpp
    src/graphics/ImGuiLayer.cpp
    src/graphics/GeometryRegistry.cpp
    src/graphics/Mesh.cpp
    src/graphics/UIRenderer.cpp
    src/graphics/StbImageUsage.cpp
    src/graphics/Primitives.cpp
    src/graphics/Pipeline.cpp
    src/graphics/ComputePipeline.cpp
    src/graphics/StorageImage.cpp
    src/graphics/ShadowMap.cpp
    src/graphics/Texture.cpp
    src/graphics/ResourceManager.cpp
    src/render/Renderer.cpp
    src/render/RenderFeatureRegistry.cpp
    src/render/features/WaterFeature.cpp
    src/render/features/SkyboxFeature.cpp
    src/render/features/DebugLinesFeature.cpp
    src/render/LightBaker.cpp
    src/render/GIVolume.cpp
    # XR sources are added conditionally below via target_sources (NE_ENABLE_XR)
    src/project/Project.cpp
    src/project/AssetRegistry.cpp
    src/graphics/VmaUsage.cpp
    src/graphics/TinyObjUsage.cpp
    src/graphics/cgltf_impl.cpp
    src/scene/GLTFLoader.cpp
    src/tools/AutoLODBridge.cpp
    src/audio/AudioUsage.cpp
    src/audio/AudioSourceBehaviour.cpp
    src/scene/CharacterBehaviour.cpp
    src/scene/HealthBehaviour.cpp
    src/scene/WaterNode.cpp
    src/scene/SpawnerBehaviour.cpp
    src/scene/RotatorBehaviour.cpp
    src/scene/Blackboard.cpp
    src/scene/StateMachineBehaviour.cpp
    src/scene/ScenarioBehaviour.cpp
    src/physics/PhysicsWorld.cpp
    src/physics/CollisionShapeNode.cpp
    src/physics/CollisionObjectNode.cpp
    src/physics/CharacterBodyNode.cpp
    src/physics/PhysicsNodes.cpp
    # Dear ImGui (vendored) + GLFW/Vulkan backends
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
    # xatlas (vendored, MIT): automatic lightmap UV unwrapping
    third_party/xatlas/xatlas.cpp
)

# LLM-generated behaviours (written by the MCP write_cpp_behaviour tool). Globbed
# with CONFIGURE_DEPENDS so Ninja re-globs on build when a new file appears — no
# manual CMake edit needed. Registered centrally in scene/ReflectedTypes.cpp.
file(GLOB NE_GENERATED_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/generated/*.cpp)
if(NE_GENERATED_SOURCES)
    target_sources(ne_engine PRIVATE ${NE_GENERATED_SOURCES})
endif()

# PUBLIC: propagated to anything linking the engine (the game/editor exe).
target_include_directories(ne_engine PUBLIC
    src
    third_party/vma
    third_party/tinyobjloader
    third_party/stb
    third_party/cgltf
    third_party/imgui
    third_party/json
    third_party/miniaudio
    third_party/xatlas
)
target_link_libraries(ne_engine PUBLIC Vulkan::Vulkan glfw glm::glm Jolt quickjs RmlUi::RmlUi)
if(NE_ENABLE_XR)
    target_sources(ne_engine PRIVATE
        src/xr/XrInstance.cpp
        src/xr/XrVulkanBinding.cpp
        src/xr/XrRegistration.cpp
        src/xr/XrSwapchain.cpp
        src/xr/XrSession.cpp
        src/xr/XrActions.cpp
        src/xr/XrHandTracking.cpp
        src/xr/toolkit/XRInput.cpp
        src/xr/toolkit/XRController.cpp
        src/xr/toolkit/XRHand.cpp
        src/xr/toolkit/XRGrabbable.cpp
        src/xr/toolkit/XRTouchable.cpp
        src/xr/toolkit/XRDirectInteractor.cpp
        src/xr/toolkit/XROrigin.cpp
        src/xr/toolkit/TeleportArea.cpp
        src/xr/toolkit/XRRayInteractor.cpp
        src/xr/toolkit/XRPassthrough.cpp
        src/xr/toolkit/XRAnchors.cpp
        src/xr/toolkit/XRAnchor.cpp
    )
    target_link_libraries(ne_engine PUBLIC openxr_loader)
endif()
add_dependencies(ne_engine Shaders)

# Absolute paths baked at configure time so the exe runs from any cwd; and GLM
# conventions (radians, Vulkan 0..1 depth) — PUBLIC so game code sees them too.
target_compile_definitions(ne_engine PUBLIC
    NE_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    NE_BINARY_DIR="${CMAKE_BINARY_DIR}"
    NE_SHADER_DIR="${SHADER_OUT_DIR}"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)
if(NE_ENABLE_XR)
    target_compile_definitions(ne_engine PUBLIC NE_ENABLE_XR)
endif()

# Warnings for the engine's own sources (third-party TUs override this with -w).
target_compile_options(ne_engine PRIVATE -Wall -Wextra)
set_source_files_properties(
    src/graphics/VmaUsage.cpp
    src/graphics/TinyObjUsage.cpp
    src/graphics/StbImageUsage.cpp
    src/graphics/cgltf_impl.cpp
    src/audio/AudioUsage.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

# Statically link the GCC/libstdc++ runtimes. Required here because this MSYS2
# toolchain's dynamic libstdc++ import library crashes the linker (ld exit 116)
# on iostream/exception symbols; bonus: self-contained exe. PUBLIC so the link
# options propagate to executables linking the engine.
if (MINGW)
    target_link_options(ne_engine PUBLIC -static -static-libgcc -static-libstdc++)
endif()

# ── Editor library ──────────────────────────────────────────────────────────
# The editor (UI panels, commands, input/camera control) lives on top of the
# engine, not inside it — so a shipped game can link ne_engine without it.
add_library(ne_editor STATIC
    src/editor/EditorApp.cpp
    src/editor/EditorUI.cpp
    src/editor/BuildExporter.cpp
    src/editor/SceneDocument.cpp
    src/editor/Command.cpp
    src/editor/CommandHistory.cpp
    src/editor/ThumbnailCache.cpp
    src/editor/InspectorRegistry.cpp
    src/editor/panels/MenuBarPanel.cpp
    src/editor/panels/SceneHierarchyPanel.cpp
    src/editor/panels/InspectorPanel.cpp
    src/editor/panels/FileBrowserPanel.cpp
    src/editor/panels/ViewportPanel.cpp
    src/editor/panels/ModelImporterPanel.cpp
)
target_link_libraries(ne_editor PUBLIC ne_engine)
target_compile_options(ne_editor PRIVATE -Wall -Wextra)

if(NE_ENABLE_MCP)
    target_sources(ne_editor PRIVATE
        src/mcp/McpServer.cpp
        src/mcp/McpBridge.cpp
    )
    target_compile_definitions(ne_editor PUBLIC NE_ENABLE_MCP)
    if(WIN32)
        target_link_libraries(ne_editor PUBLIC ws2_32)
    endif()
endif()

if (BUILD_TESTING)
    add_executable(ne_core_tests tests/CoreTests.cpp)
    target_include_directories(ne_core_tests PRIVATE src)
    target_compile_options(ne_core_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_core_tests COMMAND ne_core_tests)

    add_executable(ne_scene_lifecycle_tests tests/SceneLifecycleTests.cpp)
    target_link_libraries(ne_scene_lifecycle_tests PRIVATE ne_engine)
    target_compile_options(ne_scene_lifecycle_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_scene_lifecycle_tests COMMAND ne_scene_lifecycle_tests)

    add_executable(ne_reflection_tests tests/ReflectionTests.cpp)
    target_link_libraries(ne_reflection_tests PRIVATE ne_engine)
    target_compile_options(ne_reflection_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_reflection_tests COMMAND ne_reflection_tests)

    add_executable(ne_signal_wiring_tests tests/SignalWiringTests.cpp)
    target_link_libraries(ne_signal_wiring_tests PRIVATE ne_engine)
    target_compile_options(ne_signal_wiring_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_signal_wiring_tests COMMAND ne_signal_wiring_tests)

    add_executable(ne_js_signal_tests tests/JsSignalBindingTests.cpp)
    target_link_libraries(ne_js_signal_tests PRIVATE ne_engine)
    target_compile_options(ne_js_signal_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_js_signal_tests COMMAND ne_js_signal_tests)

    add_executable(ne_gameplay_primitive_tests tests/GameplayPrimitiveTests.cpp)
    target_link_libraries(ne_gameplay_primitive_tests PRIVATE ne_engine)
    target_compile_options(ne_gameplay_primitive_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_gameplay_primitive_tests COMMAND ne_gameplay_primitive_tests)

    add_executable(ne_physics_character_tests tests/PhysicsCharacterTest.cpp)
    target_link_libraries(ne_physics_character_tests PRIVATE ne_engine)
    add_test(NAME ne_physics_character_tests COMMAND ne_physics_character_tests)

    add_executable(ne_collision_auto_tests tests/CollisionAutoShapeTest.cpp)
    target_link_libraries(ne_collision_auto_tests PRIVATE ne_engine)
    add_test(NAME ne_collision_auto_tests COMMAND ne_collision_auto_tests)

    add_executable(ne_editor_command_tests tests/EditorCommandTests.cpp)
    target_link_libraries(ne_editor_command_tests PRIVATE ne_editor)
    target_compile_options(ne_editor_command_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_editor_command_tests COMMAND ne_editor_command_tests)

    add_executable(ne_editor_document_tests tests/EditorDocumentTests.cpp)
    target_link_libraries(ne_editor_document_tests PRIVATE ne_editor)
    target_compile_options(ne_editor_document_tests PRIVATE -Wall -Wextra)
    add_test(NAME ne_editor_document_tests COMMAND ne_editor_document_tests)

    if(NE_ENABLE_MCP)
        add_executable(ne_mcp_server_tests tests/McpServerTests.cpp)
        target_link_libraries(ne_mcp_server_tests PRIVATE ne_editor)
        if(WIN32)
            target_link_libraries(ne_mcp_server_tests PRIVATE ws2_32)
        endif()
        target_compile_options(ne_mcp_server_tests PRIVATE -Wall -Wextra)
        add_test(NAME ne_mcp_server_tests COMMAND ne_mcp_server_tests)
    endif()
endif()

# ── Editor executable (the app the Hub launches) ────────────────────────────
# Thin target: just the entry point, linking the editor. Launched directly it
# opens an empty scene; the Hub passes a project via --project to load content.
add_executable(NextEngine
    src/main.cpp
)
target_link_libraries(NextEngine PRIVATE ne_editor)
target_compile_options(NextEngine PRIVATE -Wall -Wextra)
add_dependencies(NextEngine Shaders)

if (WIN32)
    # Copy glfw
    add_custom_command(TARGET NextEngine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:glfw>"
        "$<TARGET_FILE_DIR:NextEngine>"
    )
endif()

# ── Standalone game runtime (export template) ───────────────────────────────
# Editor-less player: links ne_engine only (no ImGui/editor). The editor's Build
# step copies this exe next to the packaged game data; it reads game.ne at
# startup and runs the project's main scene. Built once with the engine.
add_executable(NextEngineRuntime
    src/runtime/main.cpp
)
target_link_libraries(NextEngineRuntime PRIVATE ne_engine)
target_compile_options(NextEngineRuntime PRIVATE -Wall -Wextra)
add_dependencies(NextEngineRuntime Shaders)

if (WIN32)
    add_custom_command(TARGET NextEngineRuntime POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:glfw>"
        "$<TARGET_FILE_DIR:NextEngineRuntime>"
    )
endif()

# Building the editor also builds the runtime template, so the Build button
# always has a fresh NextEngineRuntime.exe to package.
add_dependencies(NextEngine NextEngineRuntime)

# ── Hub executable ──────────────────────────────────────────────────────────
add_executable(NextEngineHub
    src/hub/main.cpp
    src/hub/Hub.cpp
)
target_link_libraries(NextEngineHub PRIVATE ne_engine)
target_compile_options(NextEngineHub PRIVATE -Wall -Wextra)

if (WIN32)
    add_custom_command(TARGET NextEngineHub POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:glfw>"
        "$<TARGET_FILE_DIR:NextEngineHub>"
    )
endif()
