#pragma once

#include <string>

namespace ne {

class Node;
class ResourceManager;

class GLTFLoader {
public:
    static bool load(const std::string& path, Node& rootNode, ResourceManager& resources);
};

} // namespace ne
