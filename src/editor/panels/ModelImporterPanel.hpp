#pragma once

#include "editor/EditorUI.hpp"
#include <string>

namespace ne {

class Scene;
class ResourceManager;
class Project;

class ModelImporterPanel {
public:
    void draw(EditorUI* editor, Scene* previewScene, const std::string& modelPath, ResourceManager* resources);
};

} // namespace ne
