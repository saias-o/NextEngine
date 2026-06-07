#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "project/AssetRegistry.hpp"
#include "graphics/Material.hpp"
#include "graphics/GeometryRegistry.hpp"

namespace ne {

class VulkanDevice;
class Mesh;
class Texture;
struct Vertex;
class Rig;
class AnimationClip;

// Loads and caches GPU resources so the same asset is uploaded once and shared.
// Texture / material, content-addressed (cached, uploaded once).
// Resources are content-addressed (a mesh id, a texture id, a material's
// texture+color) so a serialized scene fully describes its assets and loads
// without the game pre-registering anything. Owns the material descriptor set
// layout (set 1) and the pool its materials allocate from.
class ResourceManager {
public:
    static constexpr uint32_t kMaxMaterials = 64; // Max CPU-path fallback materials
    static constexpr uint32_t kMaxBindlessTextures = 8192;
    static constexpr uint32_t kMaxBindlessMaterials = 4096;
    ResourceManager(VulkanDevice& device, AssetRegistry* registry = nullptr);
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    VulkanDevice& device() { return device_; }

    // Mesh by id: a built-in primitive or an .obj AssetID.
    Mesh* getMesh(AssetID id);
    // Texture / material, content-addressed (cached, uploaded once).
    Texture* getTexture(AssetID id, bool srgb = true);
    Material* getMaterial(const MaterialDesc& desc);
    Rig* getRig(AssetID id);
    AnimationClip* getAnimation(AssetID id);

    // The id a mesh was loaded with (for serialization).
    AssetID meshId(const Mesh* mesh) const;

    // Direct .obj load (e.g. heavy models). getMesh() delegates here for paths.
    Mesh* loadMesh(AssetID id);

    // Register a path dynamically (e.g. for hardcoded demo scenes without pre-sync)
    AssetID getOrRegister(const std::string& path, AssetType type = AssetType::Unknown, bool srgb = true);

    AssetID registerMemoryTexture(const uint8_t* data, size_t size, bool srgb = true);

    // Register a dynamically generated mesh (e.g. from gltf primitive)
    AssetID registerMemoryMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Register memory structures directly from loaders
    AssetID registerMemoryRig(const std::string& path, std::unique_ptr<Rig> rig);
    AssetID registerMemoryAnimation(const std::string& subPath, std::unique_ptr<AnimationClip> clip);

    Texture* defaultWhiteTexture();
    Texture* defaultNormalTexture();

    VkDescriptorSetLayout materialSetLayout() const { return materialSetLayout_; }
    
    // Global bindless Set Layout (Set 1 in GPU-driven mode)
    VkDescriptorSetLayout globalMaterialSetLayout() const { return globalMaterialSetLayout_; }
    VkDescriptorSet globalMaterialSet() const { return globalMaterialSet_; }
    Buffer* globalMaterialBuffer() const { return globalMaterialBuffer_.get(); }
    
    GeometryRegistry& geometry() { return *geometryRegistry_; }

    void setRegistry(AssetRegistry* registry) { registry_ = registry; }
    AssetRegistry* getRegistry() const { return registry_; }

    // Register a texture in the bindless array, returns its index.
    uint32_t getBindlessTextureIndex(Texture* texture);
    
    // Register material data in the global SSBO, returns its index.
    uint32_t registerMaterialData(const glm::vec4& baseColor, const glm::vec4& emissive,
                                  float metallic, float roughness, float ao,
                                  uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx, uint32_t emissiveIdx);

private:
    void createMaterialSetLayout();
    void createMaterialPool();
    Mesh* createMesh(AssetID id, const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices);
    void ensureDefaultTextures();

    VulkanDevice& device_;
    AssetRegistry* registry_;
    VkDescriptorSetLayout materialSetLayout_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> materialPools_;
    
    // Global Bindless resources
    VkDescriptorSetLayout globalMaterialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool globalMaterialPool_ = VK_NULL_HANDLE;
    VkDescriptorSet globalMaterialSet_ = VK_NULL_HANDLE;
    std::unique_ptr<Buffer> globalMaterialBuffer_;
    uint32_t nextBindlessTextureIndex_ = 0;
    uint32_t nextMaterialIndex_ = 0;

    std::unique_ptr<GeometryRegistry> geometryRegistry_;

    void createGlobalBindlessResources();
    VkDescriptorPool createNewPool();

public:
    VkDescriptorSet allocateMaterialSet(VkDescriptorSetLayout layout);
private:

    std::unordered_map<AssetID, std::unique_ptr<Mesh>> meshes_;
    std::unordered_map<AssetID, std::unique_ptr<Texture>> textures_;
    std::unordered_map<MaterialDesc, std::unique_ptr<Material>> materials_;
    std::unordered_map<AssetID, std::unique_ptr<Rig>> rigs_;
    std::unordered_map<AssetID, std::unique_ptr<AnimationClip>> animations_;
    std::unordered_map<const Mesh*, AssetID> reverseMeshMap_;  // mesh -> id
    
    std::unique_ptr<Texture> defaultWhiteTexture_;
    std::unique_ptr<Texture> defaultNormalTexture_;
};

} // namespace ne
