#pragma once

#include "editor/EditorUI.hpp"
#include <string>

namespace saida {

class Scene;

class ModelImporterPanel {
public:
    void draw(EditorUI* editor, Scene* previewScene, const std::string& modelPath);

private:
    std::string exportStatus_;
};

} // namespace saida
