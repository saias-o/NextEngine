cmake_minimum_required(VERSION 3.20)
project(NextEngine LANGUAGES CXX)

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
)

set(SHADER_SPIRV "")
foreach(SHADER ${SHADERS})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPIRV_OUTPUT ${SHADER_OUT_DIR}/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SPIRV_OUTPUT}
        COMMAND ${GLSLC} ${SHADER} -o ${SPIRV_OUTPUT}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER_NAME} -> ${SHADER_NAME}.spv"
    )
    list(APPEND SHADER_SPIRV ${SPIRV_OUTPUT})
endforeach()

add_custom_target(Shaders DEPENDS ${SHADER_SPIRV})

# ── Engine library ──────────────────────────────────────────────────────────
# Everything reusable lives here and compiles once. Game/editor executables
# link it, so changing game code doesn't recompile the engine. The editor still
# lives inside the lib for now (to be moved to an editor target later).
add_library(ne_engine STATIC
    src/Engine.cpp
    src/core/Window.cpp
    src/core/Camera.cpp
    src/core/Input.cpp
    src/scene/Node.cpp
    src/scene/Scene.cpp
    src/scene/SceneSerializer.cpp
    src/scene/BehaviourRegistry.cpp
    src/graphics/VulkanDevice.cpp
    src/graphics/Swapchain.cpp
    src/graphics/Buffer.cpp
    src/graphics/Mesh.cpp
    src/graphics/Primitives.cpp
    src/graphics/Pipeline.cpp
    src/graphics/Texture.cpp
    src/graphics/Material.cpp
    src/graphics/ResourceManager.cpp
    src/graphics/ImGuiLayer.cpp
    src/render/Renderer.cpp
    src/project/Project.cpp
    src/graphics/VmaUsage.cpp
    src/graphics/TinyObjUsage.cpp
    src/graphics/StbImageUsage.cpp
    # Dear ImGui (vendored) + GLFW/Vulkan backends
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_vulkan.cpp
)

# PUBLIC: propagated to anything linking the engine (the game/editor exe).
target_include_directories(ne_engine PUBLIC
    src
    third_party/vma
    third_party/tinyobjloader
    third_party/stb
    third_party/imgui
    third_party/json
)
target_link_libraries(ne_engine PUBLIC Vulkan::Vulkan glfw glm::glm)

# Absolute paths baked at configure time so the exe runs from any cwd; and GLM
# conventions (radians, Vulkan 0..1 depth) — PUBLIC so game code sees them too.
target_compile_definitions(ne_engine PUBLIC
    NE_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
    NE_SHADER_DIR="${SHADER_OUT_DIR}"
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE)

# Warnings for the engine's own sources (third-party TUs override this with -w).
target_compile_options(ne_engine PRIVATE -Wall -Wextra)
set_source_files_properties(
    src/graphics/VmaUsage.cpp
    src/graphics/TinyObjUsage.cpp
    src/graphics/StbImageUsage.cpp
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
    src/editor/CommandHistory.cpp
)
target_link_libraries(ne_editor PUBLIC ne_engine)
target_compile_options(ne_editor PRIVATE -Wall -Wextra)

# ── Editor executable (the app the Hub launches) ────────────────────────────
# Thin target: entry point + the game's scene/behaviours, linking the editor.
add_executable(NextEngine
    src/main.cpp
    src/game/DemoScene.cpp
)
target_link_libraries(NextEngine PRIVATE ne_editor)
target_compile_options(NextEngine PRIVATE -Wall -Wextra)
add_dependencies(NextEngine Shaders)

# ── Hub executable ──────────────────────────────────────────────────────────
add_executable(NextEngineHub
    src/hub/main.cpp
    src/hub/Hub.cpp
)
target_link_libraries(NextEngineHub PRIVATE ne_engine)
target_compile_options(NextEngineHub PRIVATE -Wall -Wextra)
