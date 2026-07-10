#pragma once

#ifndef SAIDA_RHI_WEBGPU
#include <vulkan/vulkan.h>
#endif

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include "project/AssetRegistry.hpp"
#include "graphics/Material.hpp"
#include "graphics/GeometryRegistry.hpp"
#include "rhi/Rhi.hpp"

#ifdef SAIDA_RHI_WEBGPU
#include "graphics/Buffer.hpp"
#include "graphics/Texture.hpp"
#endif

namespace saida {

class VulkanDevice;
class Mesh;
#ifndef SAIDA_RHI_WEBGPU
class Texture;
#endif
struct Vertex;
class Rig;
class AnimationClip;
class ClipView;
class AnimGraphAsset;

// Loads and caches GPU resources. Owns material set 1 layout/pool.
class ResourceManager {
public:
    static constexpr uint32_t kMaxBindlessTextures = 8192;
    static constexpr uint32_t kMaxBindlessMaterials = 4096;
    ResourceManager(rhi::Device& device, AssetRegistry* registry = nullptr);
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    rhi::Device& device() { return device_; }

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
    AssetID registerGeneratedTexture(const uint8_t* pixels, uint32_t width, uint32_t height,
                                     rhi::Format format = rhi::Format::RGBA8Srgb,
                                     bool generateMipmaps = true);

    // Register a dynamically generated mesh (e.g. from gltf primitive)
    AssetID registerMemoryMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

    // Register memory structures directly from loaders
    AssetID registerMemoryRig(const std::string& path, std::unique_ptr<Rig> rig);
    AssetID registerMemoryAnimation(const std::string& subPath, std::unique_ptr<AnimationClip> clip);

    // Assets d'authoring animation (.sclip / .sgraph), chargés et cachés par
    // AssetID. Un fichier invalide loggue ses diagnostics et retourne
    // kAssetInvalid ; recharger un chemin déjà chargé rend le même id.
    AssetID loadClipView(const std::string& path);
    const ClipView* getClipView(AssetID id) const;
    AssetID loadAnimGraph(const std::string& path);
    const AnimGraphAsset* getAnimGraph(AssetID id) const;

    Texture* defaultWhiteTexture();
    Texture* defaultNormalTexture();

    rhi::BindGroupLayout& materialSetLayout() const { return *materialSetLayout_; }

    // Global bindless texture/material table. Pipelines choose the set index.
#ifndef SAIDA_RHI_WEBGPU
    VkDescriptorSetLayout globalMaterialSetLayout() const { return globalMaterialSetLayout_; }
    VkDescriptorSet globalMaterialSet() const { return globalMaterialSet_; }
#endif
    Buffer* globalMaterialBuffer() const { return globalMaterialBuffer_.get(); }
    
    GeometryRegistry& geometry() { return *geometryRegistry_; }

    void setRegistry(AssetRegistry* registry) { registry_ = registry; }
    AssetRegistry* getRegistry() const { return registry_; }

    // Register a texture in the bindless array if needed, returns its index.
    uint32_t ensureBindlessTextureIndex(Texture* texture);
    
    // Register material data in the global SSBO, returns its index.
    uint32_t registerMaterialData(const glm::vec4& baseColor, const glm::vec4& emissive,
                                  float metallic, float roughness, float ao,
                                  uint32_t albedoIdx, uint32_t normalIdx, uint32_t mrIdx, uint32_t emissiveIdx,
                                  MaterialType type);

private:
    Mesh* createMesh(AssetID id, const std::vector<Vertex>& vertices,
                     const std::vector<uint32_t>& indices);
    void ensureDefaultTextures();
    uint32_t getBindlessTextureIndex(Texture* texture);

    rhi::Device& device_;
    AssetRegistry* registry_;
    std::unique_ptr<rhi::BindGroupLayout> materialSetLayout_;

#ifndef SAIDA_RHI_WEBGPU
    // Global Bindless resources
    VkDescriptorSetLayout globalMaterialSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool globalMaterialPool_ = VK_NULL_HANDLE;
    VkDescriptorSet globalMaterialSet_ = VK_NULL_HANDLE;
#endif
    std::unique_ptr<Buffer> globalMaterialBuffer_;
    uint32_t nextBindlessTextureIndex_ = 0;
    uint32_t nextMaterialIndex_ = 0;

    std::unique_ptr<GeometryRegistry> geometryRegistry_;

    void createGlobalBindlessResources();

    std::unordered_map<AssetID, std::unique_ptr<Mesh>> meshes_;
    std::unordered_map<AssetID, std::unique_ptr<Texture>> textures_;
    std::unordered_map<MaterialDesc, std::unique_ptr<Material>> materials_;
    std::unordered_map<AssetID, std::unique_ptr<Rig>> rigs_;
    std::unordered_map<AssetID, std::unique_ptr<AnimationClip>> animations_;
    std::unordered_map<AssetID, std::unique_ptr<ClipView>> clipViews_;
    std::unordered_map<AssetID, std::unique_ptr<AnimGraphAsset>> animGraphs_;
    std::unordered_map<const Mesh*, AssetID> reverseMeshMap_;  // mesh -> id
    
    std::unique_ptr<Texture> defaultWhiteTexture_;
    std::unique_ptr<Texture> defaultNormalTexture_;
};

} // namespace saida
