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

# Main executable
add_executable(NextEngine
    src/main.cpp
    src/Engine.cpp
    src/core/Window.cpp
    src/graphics/VulkanDevice.cpp
    src/graphics/Swapchain.cpp
    src/graphics/Buffer.cpp
    src/graphics/Mesh.cpp
    src/graphics/Pipeline.cpp
    src/graphics/VmaUsage.cpp
    src/graphics/TinyObjUsage.cpp
)
target_include_directories(NextEngine PRIVATE
    src
    third_party/vma
    third_party/tinyobjloader
)
target_link_libraries(NextEngine PRIVATE Vulkan::Vulkan glfw glm::glm)

# Absolute path to the project root, used at runtime to locate assets (models).
target_compile_definitions(NextEngine PRIVATE NE_PROJECT_ROOT="${CMAKE_SOURCE_DIR}")

# Third-party implementation units; don't police their warnings.
set_source_files_properties(
    src/graphics/VmaUsage.cpp
    src/graphics/TinyObjUsage.cpp
    PROPERTIES COMPILE_OPTIONS "-w")

# Statically link the GCC/libstdc++ runtimes. Required here because this
# MSYS2 toolchain's dynamic libstdc++ import library crashes the linker
# (ld exit 116) on iostream/exception symbols; as a bonus it produces a
# self-contained executable with no runtime DLL dependencies.
if (MINGW)
    target_link_options(NextEngine PRIVATE -static -static-libgcc -static-libstdc++)
endif()
add_dependencies(NextEngine Shaders)
