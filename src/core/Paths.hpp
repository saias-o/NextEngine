#pragma once

#include <string>

// Centralized asset path resolution. Both roots are absolute paths baked at
// configure time by CMake, so the executable can run from any working directory.
//   NE_PROJECT_ROOT : repository root (source assets: models/, assets/).
//   NE_SHADER_DIR   : compiled SPIR-V output directory (build/shaders).
namespace ne {

inline std::string assetPath(const std::string& relative) {
    return std::string(NE_PROJECT_ROOT) + "/" + relative;
}

inline std::string shaderPath(const std::string& name) {
    return std::string(NE_SHADER_DIR) + "/" + name;
}

} // namespace ne
