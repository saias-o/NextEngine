#pragma once

namespace saida {

class EditorUI;
class Project;
class Scene;
class ResourceManager;

class FileBrowserPanel {
public:
    void draw(EditorUI* editor, Project* project, Scene* scene, ResourceManager* resources);
};

} // namespace saida
