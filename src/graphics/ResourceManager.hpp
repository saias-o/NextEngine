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

// Loads and caches GPU resources (meshes, textures, materials) by key, so the
// same asset is uploaded once and shared. Owns the material descriptor set
// layout (set 1) and the pool its materials allocate from.
class ResourceManager {
public:
    explicit ResourceManager(VulkanDevice& device);
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    // Meshes: from an .obj file (keyed by path) or from in-memory geometry.
    Mesh* loadMesh(const std::string& path);
    Mesh* createMesh(const std::string& key, const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices);

    Texture* loadTexture(const std::string& path);  // keyed by path

    Material* createMaterial(const std::string& key, Texture* texture,
                             const glm::vec4& baseColor);

    // Lookups by key, and reverse lookups (pointer -> key) for serialization.
    Mesh* mesh(const std::string& key) const;
    Material* material(const std::string& key) const;
    std::string meshKey(const Mesh* mesh) const;
    std::string materialKey(const Material* material) const;

    VkDescriptorSetLayout materialSetLayout() const { return materialSetLayout_; }

private:
    void createMaterialSetLayout();
    void createMaterialPool();

    VulkanDevice& device_;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool materialPool_ = VK_NULL_HANDLE;

    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshes_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> textures_;
    std::unordered_map<std::string, std::unique_ptr<Material>> materials_;
};

} // namespace ne
