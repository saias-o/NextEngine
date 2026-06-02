#include "project/Project.hpp"

#include "core/Log.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace ne {

namespace {
namespace fs = std::filesystem;

constexpr const char* kProjectExtension = ".neproj";
constexpr const char* kProjectHeader    = "[NextEngine Project]";

// Trim leading/trailing whitespace.
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

bool Project::create(const std::string& parentDir, const std::string& projectName) {
    fs::path projDir = fs::path(parentDir) / projectName;

    // Create directory structure.
    try {
        fs::create_directories(projDir / "assets" / "textures");
        fs::create_directories(projDir / "assets" / "models");
        fs::create_directories(projDir / "scenes");
        fs::create_directories(projDir / "scripts");
        fs::create_directories(projDir / "shaders");
    } catch (const fs::filesystem_error& e) {
        Log::error("Project::create: failed to create directories: ", e.what());
        return false;
    }

    name_     = projectName;
    rootPath_ = projDir.string();
    filePath_ = (projDir / (projectName + kProjectExtension)).string();
    engineVersion_ = "0.1.0";
    loaded_   = true;

    if (!save()) {
        loaded_ = false;
        return false;
    }

    Log::info("Created project '", name_, "' at ", rootPath_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

bool Project::load(const std::string& neprojPath) {
    fs::path path(neprojPath);

    if (!fs::exists(path)) {
        Log::error("Project::load: file not found: ", neprojPath);
        return false;
    }

    std::ifstream file(neprojPath);
    if (!file.is_open()) {
        Log::error("Project::load: cannot open: ", neprojPath);
        return false;
    }

    std::string line;
    // First line must be the header.
    if (!std::getline(file, line) || trim(line) != kProjectHeader) {
        Log::error("Project::load: invalid project file (missing header): ", neprojPath);
        return false;
    }

    // Read key=value pairs.
    std::string loadedName;
    std::string loadedVersion;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;  // skip blank/comment lines

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "name")                loadedName    = value;
        else if (key == "engine_version") loadedVersion = value;
    }

    if (loadedName.empty()) {
        Log::error("Project::load: missing 'name' in project file: ", neprojPath);
        return false;
    }

    name_          = loadedName;
    engineVersion_ = loadedVersion.empty() ? "0.1.0" : loadedVersion;
    filePath_      = path.string();
    rootPath_      = path.parent_path().string();
    loaded_        = true;

    Log::info("Loaded project '", name_, "' from ", rootPath_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────

bool Project::save() const {
    if (!loaded_) {
        Log::error("Project::save: no project loaded");
        return false;
    }

    std::ofstream file(filePath_);
    if (!file.is_open()) {
        Log::error("Project::save: cannot write to ", filePath_);
        return false;
    }

    file << kProjectHeader << "\n";
    file << "name=" << name_ << "\n";
    file << "engine_version=" << engineVersion_ << "\n";

    Log::info("Saved project '", name_, "' to ", filePath_);
    return true;
}

} // namespace ne
