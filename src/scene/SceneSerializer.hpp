#pragma once

#include <memory>
#include <string>

namespace ne {

class Node;
class Scene;
class ResourceManager;

// Reads/writes a scene (node hierarchy, transforms, lights, mesh/material
// references by key, and registered behaviours) as JSON. Also converts a single
// node subtree to/from a JSON string, used for the editor's copy/paste/duplicate.
class SceneSerializer {
public:
    static bool saveToFile(Node& sceneRoot, ResourceManager& resources, const std::string& path);
    static bool loadIntoScene(Scene& scene, ResourceManager& resources, const std::string& path);

    static std::string nodeToJson(Node& node, ResourceManager& resources);
    static std::unique_ptr<Node> nodeFromJson(const std::string& json, ResourceManager& resources);

    // Load a .scene file as a single node subtree (for instancing a scene as a
    // child of another). Returns nullptr on failure.
    static std::unique_ptr<Node> loadNodeFromSceneFile(const std::string& path,
                                                       ResourceManager& resources);
};

} // namespace ne
