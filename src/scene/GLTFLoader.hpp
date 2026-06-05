#pragma once

#include <string>

namespace ne {

class Scene;
class ResourceManager;

class GLTFLoader {
public:
    static bool load(const std::string& path, Scene& scene, ResourceManager& resources);
};

} // namespace ne
