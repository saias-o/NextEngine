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

    assetRegistry_.sync(rootPath_);
    assetRegistry_.save(rootPath_);

    AudioManager::get().setProjectRoot(rootPath_);

    ++version_;
    Log::info("Created project '", name_, "' at ", rootPath_);
    return true;
}


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
    std::string loadedMainScene;
    int loadedMaxFps = kDefaultMaxFps;
    bool loadedVSync = kDefaultVSync;
    int loadedShadowRes = kDefaultShadowResolution;
    float loadedShadowDist = kDefaultShadowDistance;
    float loadedShadowSoft = kDefaultShadowSoftness;
    float loadedMasterVolume = 1.0f;
    AudioSettings loadedAudioSettings;
    bool loadedAutoMeshLods = false;
    bool loadedShowColliders = true;

    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;  // skip blank/comment lines

        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key   = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        if (key == "name")                loadedName       = value;
        else if (key == "engine_version") loadedVersion    = value;
        else if (key == "main_scene")     loadedMainScene  = value;
        else if (key == "max_fps")        loadedMaxFps     = std::stoi(value);
        else if (key == "vsync")          loadedVSync      = (value == "1");
        else if (key == "shadow_resolution") loadedShadowRes = std::stoi(value);
        else if (key == "shadow_dist")    loadedShadowDist = std::stof(value);
        else if (key == "shadow_soft")    loadedShadowSoft = std::stof(value);
        else if (key == "audio_master_vol") loadedMasterVolume = std::stof(value);
        else if (key == "audio_default_vol") loadedAudioSettings.volume = std::stof(value);
        else if (key == "audio_default_loop") loadedAudioSettings.loop = (value == "1");
        else if (key == "audio_default_spatial") loadedAudioSettings.spatialized = (value == "1");
        else if (key == "audio_default_min_dist") loadedAudioSettings.minDistance = std::stof(value);
        else if (key == "audio_default_max_dist") loadedAudioSettings.maxDistance = std::stof(value);
        else if (key == "auto_mesh_lods") loadedAutoMeshLods = (value == "1");
        else if (key == "show_colliders") loadedShowColliders = (value == "1");
        else if (key.find("audio_alias_") == 0) {
            std::string aliasName = key.substr(12);
            audioAliases_[aliasName] = value;
            AudioManager::get().setAlias(aliasName, value);
        }
        else if (key.find("autoload_") == 0) {
            autoloads_[key.substr(9)] = value;
        }
    }

    if (loadedName.empty()) {
        Log::error("Project::load: missing 'name' in project file: ", neprojPath);
        return false;
    }

    name_          = loadedName;
    engineVersion_ = loadedVersion.empty() ? "0.1.0" : loadedVersion;
    mainScene_     = loadedMainScene;
    maxFps_        = loadedMaxFps;
    vSync_         = loadedVSync;
    shadowResolution_ = loadedShadowRes;
    shadowDistance_ = loadedShadowDist;
    shadowSoftness_ = loadedShadowSoft;
    masterVolume_ = loadedMasterVolume;
    defaultAudioSettings_ = loadedAudioSettings;
    autoMeshLods_ = loadedAutoMeshLods;
    showColliders_ = loadedShowColliders;
    filePath_      = path.string();
    rootPath_      = path.parent_path().string();
    loaded_        = true;

    assetRegistry_.load(rootPath_);
    assetRegistry_.sync(rootPath_);
    assetRegistry_.save(rootPath_);

    AudioManager::get().setProjectRoot(rootPath_);

    ++version_;
    Log::info("Loaded project '", name_, "' from ", rootPath_);
    return true;
}


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
    if (!mainScene_.empty())
        file << "main_scene=" << mainScene_ << "\n";
    file << "max_fps=" << maxFps_ << "\n";
    file << "vsync=" << (vSync_ ? "1" : "0") << "\n";
    file << "shadow_resolution=" << shadowResolution_ << "\n";
    file << "shadow_dist=" << shadowDistance_ << "\n";
    file << "shadow_soft=" << shadowSoftness_ << "\n";
    file << "audio_master_vol=" << masterVolume_ << "\n";
    file << "audio_default_vol=" << defaultAudioSettings_.volume << "\n";
    file << "audio_default_loop=" << (defaultAudioSettings_.loop ? "1" : "0") << "\n";
    file << "audio_default_spatial=" << (defaultAudioSettings_.spatialized ? "1" : "0") << "\n";
    file << "audio_default_min_dist=" << defaultAudioSettings_.minDistance << "\n";
    file << "audio_default_max_dist=" << defaultAudioSettings_.maxDistance << "\n";
    file << "auto_mesh_lods=" << (autoMeshLods_ ? "1" : "0") << "\n";
    file << "show_colliders=" << (showColliders_ ? "1" : "0") << "\n";
    
    for (const auto& [aliasName, aliasPath] : audioAliases_) {
        file << "audio_alias_" << aliasName << "=" << aliasPath << "\n";
    }

    for (const auto& [autoName, autoVal] : autoloads_) {
        file << "autoload_" << autoName << "=" << autoVal << "\n";
    }

    Log::info("Saved project '", name_, "' to ", filePath_);
    return true;
}

void Project::setAudioAlias(const std::string& name, const std::string& path) {
    audioAliases_[name] = path;
    AudioManager::get().setAlias(name, path);
}

void Project::removeAudioAlias(const std::string& name) {
    audioAliases_.erase(name);
    AudioManager::get().removeAlias(name);
}

void Project::setAutoload(const std::string& name, const std::string& pathOrType) {
    autoloads_[name] = pathOrType;
}

void Project::removeAutoload(const std::string& name) {
    autoloads_.erase(name);
}

} // namespace ne
