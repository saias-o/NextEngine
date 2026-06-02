#pragma once

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
    const std::string& rootPath() const { return rootPath_; }     // project directory
    const std::string& filePath() const { return filePath_; }     // .neproj file
    const std::string& engineVersion() const { return engineVersion_; }

    // Standard project sub-directories (relative to rootPath).
    std::string assetsDir()  const { return rootPath_ + "/assets"; }
    std::string scenesDir()  const { return rootPath_ + "/scenes"; }
    std::string scriptsDir() const { return rootPath_ + "/scripts"; }
    std::string shadersDir() const { return rootPath_ + "/shaders"; }

private:
    bool loaded_ = false;
    std::string name_;
    std::string rootPath_;
    std::string filePath_;
    std::string engineVersion_ = "0.1.0";
};

} // namespace ne
