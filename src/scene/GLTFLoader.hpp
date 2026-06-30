#pragma once

#include <string>

namespace saida {

class Node;
class ResourceManager;

struct GLTFLoadOptions {
    bool autoMeshLods = false;
};

class GLTFLoader {
public:
    static bool load(const std::string& path, Node& rootNode, ResourceManager& resources,
                     const GLTFLoadOptions& options = {});
};

} // namespace saida
