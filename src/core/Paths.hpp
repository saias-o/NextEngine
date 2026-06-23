#pragma once

#include <algorithm>
#include <string>

// Centralized asset path resolution.
//
// Editor/dev mode (default): both roots are absolute paths baked at configure
// time by CMake, so the executable can run from any working directory.
//   NE_PROJECT_ROOT : repository root (source assets: models/, assets/).
//   NE_SHADER_DIR   : compiled SPIR-V output directory (build/shaders).
//
// Shipped-game mode: a packaged game has no access to those build paths. The
// runtime sets a "runtime root" (the directory of the game exe) at startup via
// setRuntimeRoot(); when set, assets resolve relative to it (<root>/<rel>) and
// shaders under <root>/shaders/. The public API below is unchanged, so every
// existing call site benefits from the redirection transparently.
namespace ne {

// Set/clear the runtime root (directory of the shipped game exe). Empty string
// (the default) means dev/editor mode → baked absolute paths are used.
void setRuntimeRoot(const std::string& dir);
const std::string& runtimeRoot();  // empty when unset

inline std::string assetPath(const std::string& relative) {
    const std::string& root = runtimeRoot();
    if (!root.empty()) return root + "/" + relative;
    return std::string(NE_PROJECT_ROOT) + "/" + relative;
}

inline std::string shaderPath(const std::string& name) {
    const std::string& root = runtimeRoot();
    if (!root.empty()) return root + "/shaders/" + name;
    return std::string(NE_SHADER_DIR) + "/" + name;
}

inline std::string pathFromFileUrl(const std::string& pathOrUrl) {
    constexpr const char* kFilePrefix = "file://";
    if (pathOrUrl.rfind(kFilePrefix, 0) != 0) return pathOrUrl;

    std::string path = pathOrUrl.substr(std::char_traits<char>::length(kFilePrefix));
    while (!path.empty() && path.front() == '/' && path.size() > 2 && path[2] == ':') {
        path.erase(path.begin());
    }
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

} // namespace ne
