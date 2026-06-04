#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ne {

class VulkanDevice;
class Mesh;
class Texture;
class Material;
struct Vertex;

// Loads and caches GPU resources so the same asset is uploaded once and shared.
// Resources are content-addressed (a mesh id, a texture path, a material's
// texture+color) so a serialized scene fully describes its assets and loads
// without the game pre-registering anything. Owns the material descriptor set
// layout (set 1) and the pool its materials allocate from.
class ResourceManager {
public:
    explicit ResourceManager(VulkanDevice& device);
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Mesh by id: a built-in primitive ("builtin:cube") or an .obj file path.
    Mesh* getMesh(const std::string& id);
    // Texture / material, content-addressed (cached, uploaded once).
    Texture* getTexture(const std::string& path);
    Material* getMaterial(const std::string& texturePath, const glm::vec4& baseColor);

    // The id a mesh was loaded with (for serialization).
    std::string meshId(const Mesh* mesh) const;

    // Direct .obj load (e.g. heavy models). getMesh() delegates here for paths.
    Mesh* loadMesh(const std::string& path);

    VkDescriptorSetLayout materialSetLayout() const { return materialSetLayout_; }

private:
    void createMaterialSetLayout();
    void createMaterialPool();
    Mesh* createMesh(const std::string& id, const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices);

    VulkanDevice& device_;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool materialPool_ = VK_NULL_HANDLE;

    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshes_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials_;
    std::unordered_map<const Mesh*, std::string> reverseMeshMap_;  // mesh -> id
};

} // namespace ne
