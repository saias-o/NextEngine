#include "core/Paths.hpp"

#include <cctype>
#include <filesystem>
#include <utility>

namespace saida {

namespace {
namespace fs = std::filesystem;

std::string normalizeSeparators(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

std::string lowerForCompare(std::string value) {
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return value;
}

bool isRootedOrDriveQualified(const fs::path& path, const std::string& raw) {
    if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) return true;
    return raw.find(':') != std::string::npos;
}

bool containsParentTraversal(const fs::path& path) {
    for (const auto& part : path) {
        if (part == "..") return true;
    }
    return false;
}

std::string stripTrailingSlash(std::string path) {
    while (path.size() > 1 && path.back() == '/') path.pop_back();
    return path;
}

bool isInsideRoot(const fs::path& root, const fs::path& child) {
    const std::string rootText = lowerForCompare(stripTrailingSlash(root.generic_string()));
    const std::string childText = lowerForCompare(stripTrailingSlash(child.generic_string()));
    return childText == rootText ||
           (childText.size() > rootText.size() &&
            childText.compare(0, rootText.size(), rootText) == 0 &&
            childText[rootText.size()] == '/');
}

SandboxedPathResult reject(std::string error) {
    SandboxedPathResult result;
    result.error = std::move(error);
    return result;
}
// Directory of the shipped game executable, set once at runtime startup.
// Empty in the editor/dev process → baked absolute paths are used instead.
std::string g_runtimeRoot;
} // namespace

void setRuntimeRoot(const std::string& dir) { g_runtimeRoot = dir; }

const std::string& runtimeRoot() { return g_runtimeRoot; }

namespace {
// Racine du projet .saidaproj actuellement chargé (éditeur ou runtime).
// Résout les fichiers de contenu projet (scripts, ui) hors du registre
// d'assets. Vide quand aucun projet n'est chargé.
std::string g_activeProjectRoot;
} // namespace

void setActiveProjectRoot(const std::string& dir) { g_activeProjectRoot = dir; }

const std::string& activeProjectRoot() { return g_activeProjectRoot; }

SandboxedPathResult resolveSandboxedProjectPath(const std::string& projectRoot,
                                                const std::string& userPath,
                                                const std::string& defaultDirectory) {
    if (projectRoot.empty()) return reject("project root is required");

    std::string raw = normalizeSeparators(pathFromFileUrl(userPath));
    if (raw.empty()) return reject("path is required");

    fs::path relative(raw);
    if (isRootedOrDriveQualified(relative, raw))
        return reject("path must be project-relative");

    if (!defaultDirectory.empty() && !relative.has_parent_path()) {
        std::string defaultRaw = normalizeSeparators(defaultDirectory);
        fs::path defaultPath(defaultRaw);
        if (isRootedOrDriveQualified(defaultPath, defaultRaw))
            return reject("default directory must be project-relative");
        relative = defaultPath / relative;
    }

    relative = relative.lexically_normal();
    if (relative.empty() || relative == ".")
        return reject("path must name a project file");
    if (containsParentTraversal(relative))
        return reject("path must not contain parent traversal");

    const fs::path root = fs::absolute(fs::path(projectRoot)).lexically_normal();
    const fs::path absolute = (root / relative).lexically_normal();
    if (!isInsideRoot(root, absolute))
        return reject("path escapes project root");

    SandboxedPathResult result;
    result.ok = true;
    result.absolute = absolute.generic_string();
    result.relative = relative.generic_string();
    return result;
}

} // namespace saida
