#pragma once

#include "project/AssetRegistry.hpp"
#include "audio/AudioManager.hpp"

#include <string>

namespace ne {

// Represents a NextEngine project on disk. A project is a directory containing
// a `.neproj` file (simple key=value text) plus standard sub-folders (assets/,
// scenes/, scripts/, shaders/). The Project class manages creation, loading,
// and saving of this structure.
//
// When no project is loaded, `isLoaded()` returns false and the editor should
// show a welcome/project-selection screen.
class Project {
public:
    Project() = default;

    // Create a new project at `parentDir/projectName/` (creates the directory
    // structure and the .neproj file). Returns true on success.
    bool create(const std::string& parentDir, const std::string& projectName);

    // Load an existing project from its `.neproj` file path.
    // Returns true on success.
    bool load(const std::string& neprojPath);

    // Save the current project file (overwrite the .neproj).
    // Returns true on success.
    bool save() const;

    bool isLoaded() const { return loaded_; }

    const std::string& name() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    const std::string& rootPath() const { return rootPath_; }     // project directory
    const std::string& filePath() const { return filePath_; }     // .neproj file
    const std::string& engineVersion() const { return engineVersion_; }

    AssetRegistry& assetRegistry() { return assetRegistry_; }
    const AssetRegistry& assetRegistry() const { return assetRegistry_; }

    // Standard project sub-directories (relative to rootPath).
    std::string assetsDir()  const { return rootPath_ + "/assets"; }
    std::string scenesDir()  const { return rootPath_ + "/scenes"; }
    std::string scriptsDir() const { return rootPath_ + "/scripts"; }
    std::string shadersDir() const { return rootPath_ + "/shaders"; }

    static constexpr int kDefaultMaxFps = 240;
    static constexpr int kDefaultShadowResolution = 2048;
    static constexpr float kDefaultShadowDistance = 25.0f;
    static constexpr float kDefaultShadowSoftness = 1.0f;

    int maxFps() const { return maxFps_; }
    void setMaxFps(int fps) { maxFps_ = fps; }

    int shadowResolution() const { return shadowResolution_; }
    void setShadowResolution(int res) { shadowResolution_ = res; }
    
    float shadowDistance() const { return shadowDistance_; }
    void setShadowDistance(float dist) { shadowDistance_ = dist; }
    
    float shadowSoftness() const { return shadowSoftness_; }
    void setShadowSoftness(float soft) { shadowSoftness_ = soft; }

    float masterVolume() const { return masterVolume_; }
    void setMasterVolume(float vol) { masterVolume_ = vol; }

    const AudioSettings& defaultAudioSettings() const { return defaultAudioSettings_; }
    AudioSettings& defaultAudioSettings() { return defaultAudioSettings_; }

    const std::unordered_map<std::string, std::string>& audioAliases() const { return audioAliases_; }
    void setAudioAlias(const std::string& name, const std::string& path);
    void removeAudioAlias(const std::string& name);

private:
    bool loaded_ = false;
    std::string name_;
    std::string rootPath_;
    std::string filePath_;
    std::string engineVersion_ = "0.1.0";
    int maxFps_ = kDefaultMaxFps; // 0 means unlimited
    int shadowResolution_ = kDefaultShadowResolution;
    float shadowDistance_ = kDefaultShadowDistance;
    float shadowSoftness_ = kDefaultShadowSoftness;
    
    float masterVolume_ = 1.0f;
    AudioSettings defaultAudioSettings_;
    std::unordered_map<std::string, std::string> audioAliases_;
    
    AssetRegistry assetRegistry_;
};

} // namespace ne
