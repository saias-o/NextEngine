#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "project/AssetRegistry.hpp"

#include <memory>
#include <string>
#include <functional>

namespace ne {

class VulkanDevice;
class Texture;
class Buffer;

// A surface description: a texture plus shader parameters, exposed as descriptor
// set 1 (binding 0 = sampler, binding 1 = params UBO). Materials are created and
// owned by the ResourceManager; nodes reference them.
class ResourceManager;

// Which shading model a material uses. Selects the scene pipeline at draw time
// (see Renderer::scenePipelineFor) — Unlit and Lit share the exact same vertex
// shader, descriptor-set layout and push constants, only the fragment differs.
//   Lit   = full PBR + lighting + shadows + IBL + GI (shader.frag).
//   Unlit = albedo * baseColor + emissive, no lighting (unlit.frag). Cheaper —
//           ideal for UI, holograms, stylized art, particles, skybox-like quads.
// To add a shading model: add a value here, a fragment shader, and one pipeline
// in the Renderer (createPipeline / createXrPipelines). Nothing else changes.
enum class MaterialType : uint32_t {
    Lit = 0,
    Unlit = 1,
    Count,
};

struct MaterialDesc {
    AssetID albedoId = kAssetInvalid;
    AssetID normalId = kAssetInvalid;
    AssetID metallicRoughnessId = kAssetInvalid;
    AssetID emissiveId = kAssetInvalid;
    glm::vec4 baseColor{1.0f};
    glm::vec4 emissiveColor{0.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    bool doubleSided = false;
    MaterialType type = MaterialType::Lit;

    bool operator==(const MaterialDesc& o) const {
        return albedoId == o.albedoId && normalId == o.normalId &&
               metallicRoughnessId == o.metallicRoughnessId && emissiveId == o.emissiveId &&
               baseColor == o.baseColor && emissiveColor == o.emissiveColor &&
               metallic == o.metallic && roughness == o.roughness && ao == o.ao &&
               doubleSided == o.doubleSided && type == o.type;
    }
};

} // namespace ne

namespace std {
template <>
struct hash<ne::MaterialDesc> {
    size_t operator()(const ne::MaterialDesc& d) const {
        size_t h = 0;
        auto combine = [&h](size_t v) { h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2); };
        combine(d.albedoId); combine(d.normalId); combine(d.metallicRoughnessId); combine(d.emissiveId);
        combine(std::hash<float>()(d.baseColor.r)); combine(std::hash<float>()(d.baseColor.g));
        combine(std::hash<float>()(d.baseColor.b)); combine(std::hash<float>()(d.baseColor.a));
        combine(std::hash<float>()(d.metallic)); combine(std::hash<float>()(d.roughness));
        combine(std::hash<bool>()(d.doubleSided));
        combine(std::hash<uint32_t>()(static_cast<uint32_t>(d.type)));
        return h;
    }
};
} // namespace std

namespace ne {

class Material {
public:
    Material(VulkanDevice& device, ResourceManager& manager, const MaterialDesc& desc);
    ~Material();
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    VkDescriptorSet descriptorSet() const { return descriptorSet_; }
    uint32_t bindlessIndex() const { return bindlessIndex_; }
    const MaterialDesc& desc() const { return desc_; }
    Texture* texture() const { return albedo_; }

private:
    VulkanDevice& device_;
    MaterialDesc desc_;
    Texture* albedo_;
    Texture* normalMap_;
    Texture* metallicRoughnessMap_;
    Texture* emissiveMap_;
    std::unique_ptr<Buffer> paramsBuffer_;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;  // freed with the pool
    uint32_t bindlessIndex_ = 0;
};

} // namespace ne
