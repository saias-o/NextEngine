#include "project/Project.hpp"

#include "core/FormatVersions.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace saida {

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr const char* kProjectExtension = ".saidaproj";
constexpr const char* kProjectHeader    = "[SaidaEngine Project]";

// Trim leading/trailing whitespace.
std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

bool parseBool01(const std::string& value, bool fallback) {
    if (value == "1" || value == "true") return true;
    if (value == "0" || value == "false") return false;
    return fallback;
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

    std::string loadedName;
    std::string loadedEngineVersion;
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
    std::unordered_map<std::string, std::string> loadedAudioAliases;
    std::unordered_map<std::string, std::string> loadedAutoloads;

    try {
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();
        const std::string trimmed = trim(text);

        if (trimmed.empty()) {
            Log::error("Project::load: empty project file: ", neprojPath);
            return false;
        }

        if (!trimmed.empty() && trimmed.front() == '{') {
            json doc = json::parse(trimmed);
            if (doc.contains("schema") && !doc["schema"].is_number_integer()) {
                Log::error("Project::load: project schema must be an integer: ", neprojPath);
                return false;
            }
            const int version = format::readSchema(doc, format::kLegacyVersion);
            if (!format::hasIntegerSchema(doc)) {
                Log::warn("Project::load: project has no integer schema, treating as legacy v",
                          version, ": ", neprojPath);
            }
            if (version > format::kProjectVersion) {
                Log::error("Project::load: project schema v", version,
                           " is newer than supported v", format::kProjectVersion,
                           ": ", neprojPath);
                return false;
            }
            if (version < format::kProjectVersion) {
                Log::info("Project::load: migrated project schema v", version,
                          " -> v", format::kProjectVersion, " in memory: ", neprojPath);
            }

            loadedName = doc.value("name", std::string());
            loadedEngineVersion = doc.value("engineVersion", doc.value("engine_version", std::string()));
            loadedMainScene = doc.value("mainScene", doc.value("main_scene", std::string()));

            const json runtime = doc.value("runtime", json::object());
            loadedMaxFps = runtime.value("maxFps", doc.value("max_fps", loadedMaxFps));
            loadedVSync = runtime.value("vsync", doc.value("vsync", loadedVSync));

            const json rendering = doc.value("rendering", json::object());
            loadedShadowRes = rendering.value("shadowResolution", doc.value("shadow_resolution", loadedShadowRes));
            loadedShadowDist = rendering.value("shadowDistance", doc.value("shadow_dist", loadedShadowDist));
            loadedShadowSoft = rendering.value("shadowSoftness", doc.value("shadow_soft", loadedShadowSoft));
            loadedAutoMeshLods = rendering.value("autoMeshLods", doc.value("auto_mesh_lods", loadedAutoMeshLods));
            loadedShowColliders = rendering.value("showColliders", doc.value("show_colliders", loadedShowColliders));

            const json audio = doc.value("audio", json::object());
            loadedMasterVolume = audio.value("masterVolume", doc.value("audio_master_vol", loadedMasterVolume));
            const json audioDefault = audio.value("default", json::object());
            loadedAudioSettings.volume = audioDefault.value("volume", doc.value("audio_default_vol", loadedAudioSettings.volume));
            loadedAudioSettings.loop = audioDefault.value("loop", doc.value("audio_default_loop", loadedAudioSettings.loop));
            loadedAudioSettings.spatialized = audioDefault.value("spatialized", doc.value("audio_default_spatial", loadedAudioSettings.spatialized));
            loadedAudioSettings.minDistance = audioDefault.value("minDistance", doc.value("audio_default_min_dist", loadedAudioSettings.minDistance));
            loadedAudioSettings.maxDistance = audioDefault.value("maxDistance", doc.value("audio_default_max_dist", loadedAudioSettings.maxDistance));

            if (auto aliases = audio.find("aliases"); aliases != audio.end() && aliases->is_object()) {
                for (const auto& [name, value] : aliases->items()) {
                    if (value.is_string()) loadedAudioAliases[name] = value.get<std::string>();
                }
            }

            if (auto autoloads = doc.find("autoloads"); autoloads != doc.end() && autoloads->is_object()) {
                for (const auto& [name, value] : autoloads->items()) {
                    if (value.is_string()) loadedAutoloads[name] = value.get<std::string>();
                }
            }
        } else {
            std::istringstream lines(text);
            std::string line;
            if (!std::getline(lines, line) || trim(line) != kProjectHeader) {
                Log::error("Project::load: invalid project file (expected JSON or legacy header): ", neprojPath);
                return false;
            }

            Log::info("Project::load: migrated legacy project format v0 -> v",
                      format::kProjectVersion, " in memory: ", neprojPath);

            while (std::getline(lines, line)) {
                line = trim(line);
                if (line.empty() || line[0] == '#') continue;

                auto eq = line.find('=');
                if (eq == std::string::npos) continue;

                std::string key   = trim(line.substr(0, eq));
                std::string value = trim(line.substr(eq + 1));

                if (key == "name")                loadedName       = value;
                else if (key == "engine_version") loadedEngineVersion = value;
                else if (key == "main_scene")     loadedMainScene  = value;
                else if (key == "max_fps")        loadedMaxFps     = std::stoi(value);
                else if (key == "vsync")          loadedVSync      = parseBool01(value, loadedVSync);
                else if (key == "shadow_resolution") loadedShadowRes = std::stoi(value);
                else if (key == "shadow_dist")    loadedShadowDist = std::stof(value);
                else if (key == "shadow_soft")    loadedShadowSoft = std::stof(value);
                else if (key == "audio_master_vol") loadedMasterVolume = std::stof(value);
                else if (key == "audio_default_vol") loadedAudioSettings.volume = std::stof(value);
                else if (key == "audio_default_loop") loadedAudioSettings.loop = parseBool01(value, loadedAudioSettings.loop);
                else if (key == "audio_default_spatial") loadedAudioSettings.spatialized = parseBool01(value, loadedAudioSettings.spatialized);
                else if (key == "audio_default_min_dist") loadedAudioSettings.minDistance = std::stof(value);
                else if (key == "audio_default_max_dist") loadedAudioSettings.maxDistance = std::stof(value);
                else if (key == "auto_mesh_lods") loadedAutoMeshLods = parseBool01(value, loadedAutoMeshLods);
                else if (key == "show_colliders") loadedShowColliders = parseBool01(value, loadedShowColliders);
                else if (key.find("audio_alias_") == 0) loadedAudioAliases[key.substr(12)] = value;
                else if (key.find("autoload_") == 0) loadedAutoloads[key.substr(9)] = value;
            }
        }
    } catch (const std::exception& e) {
        Log::error("Project::load: failed to parse ", neprojPath, ": ", e.what());
        return false;
    }

    if (loadedName.empty()) {
        Log::error("Project::load: missing 'name' in project file: ", neprojPath);
        return false;
    }

    name_          = loadedName;
    engineVersion_ = loadedEngineVersion.empty() ? "0.1.0" : loadedEngineVersion;
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
    audioAliases_ = std::move(loadedAudioAliases);
    autoloads_ = std::move(loadedAutoloads);
    filePath_      = path.string();
    rootPath_      = path.parent_path().string();
    loaded_        = true;
    setActiveProjectRoot(rootPath_);  // scripts/ui du projet résolus partout

    for (const auto& [aliasName, aliasPath] : audioAliases_)
        AudioManager::get().setAlias(aliasName, aliasPath);

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

    json doc;
    format::writeSchema(doc, format::kProjectVersion);
    doc["name"] = name_;
    doc["engineVersion"] = engineVersion_;
    if (!mainScene_.empty()) doc["mainScene"] = mainScene_;
    doc["runtime"] = {
        {"maxFps", maxFps_},
        {"vsync", vSync_}
    };
    doc["rendering"] = {
        {"shadowResolution", shadowResolution_},
        {"shadowDistance", shadowDistance_},
        {"shadowSoftness", shadowSoftness_},
        {"autoMeshLods", autoMeshLods_},
        {"showColliders", showColliders_}
    };

    json aliases = json::object();
    for (const auto& [aliasName, aliasPath] : audioAliases_)
        aliases[aliasName] = aliasPath;

    doc["audio"] = {
        {"masterVolume", masterVolume_},
        {"default", {
            {"volume", defaultAudioSettings_.volume},
            {"loop", defaultAudioSettings_.loop},
            {"spatialized", defaultAudioSettings_.spatialized},
            {"minDistance", defaultAudioSettings_.minDistance},
            {"maxDistance", defaultAudioSettings_.maxDistance}
        }},
        {"aliases", std::move(aliases)}
    };

    json autoloads = json::object();
    for (const auto& [autoName, autoVal] : autoloads_)
        autoloads[autoName] = autoVal;
    doc["autoloads"] = std::move(autoloads);

    file << doc.dump(2) << "\n";

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

} // namespace saida
