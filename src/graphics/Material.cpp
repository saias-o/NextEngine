#include "graphics/Material.hpp"

#include "graphics/Buffer.hpp"
#include "graphics/Texture.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/ResourceManager.hpp"

#include <array>
#include <stdexcept>

namespace saida {

namespace {
struct MaterialParams {
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    float ao;
    float _pad;
    glm::vec4 emissive;
};
}

Material::Material(VulkanDevice& device, ResourceManager& manager, const MaterialDesc& desc)
    : device_(device), desc_(desc) {
    
    albedo_ = manager.getTexture(desc.albedoId);
    if (!albedo_) albedo_ = manager.defaultWhiteTexture();
    
    normalMap_ = manager.getTexture(desc.normalId, false);
    if (!normalMap_) normalMap_ = manager.defaultNormalTexture();
    
    metallicRoughnessMap_ = manager.getTexture(desc.metallicRoughnessId, false);
    if (!metallicRoughnessMap_) metallicRoughnessMap_ = manager.defaultWhiteTexture();
    
    emissiveMap_ = manager.getTexture(desc.emissiveId);
    if (!emissiveMap_) emissiveMap_ = manager.defaultWhiteTexture();

    MaterialParams params{desc.baseColor, desc.metallic, desc.roughness, desc.ao, 0.0f, desc.emissiveColor};
    paramsBuffer_ = std::make_unique<Buffer>(device_, sizeof(MaterialParams),
        rhi::BufferUsage::Uniform, MemoryUsage::HostVisible);
    paramsBuffer_->write(&params, sizeof(params));

    // 1. Classic Path: set 1 (albedo/normal/metallic-roughness/params/emissive).
    rhi::BindGroupEntry albedoEntry;
    albedoEntry.binding = 0;
    albedoEntry.view = albedo_->imageView();
    albedoEntry.sampler = albedo_->sampler();

    rhi::BindGroupEntry normalEntry;
    normalEntry.binding = 1;
    normalEntry.view = normalMap_->imageView();
    normalEntry.sampler = normalMap_->sampler();

    rhi::BindGroupEntry mrEntry;
    mrEntry.binding = 2;
    mrEntry.view = metallicRoughnessMap_->imageView();
    mrEntry.sampler = metallicRoughnessMap_->sampler();

    rhi::BindGroupEntry paramsEntry;
    paramsEntry.binding = 3;
    paramsEntry.buffer = paramsBuffer_.get();
    paramsEntry.range = sizeof(MaterialParams);

    rhi::BindGroupEntry emissiveEntry;
    emissiveEntry.binding = 4;
    emissiveEntry.view = emissiveMap_->imageView();
    emissiveEntry.sampler = emissiveMap_->sampler();

    descriptorSet_ = std::make_unique<rhi::BindGroup>(manager.materialSetLayout(),
        std::vector<rhi::BindGroupEntry>{albedoEntry, normalEntry, mrEntry, paramsEntry, emissiveEntry});

    // 2. GPU-Driven Path: Register into global MaterialData SSBO
    if (device_.capabilities().descriptorIndexing) {
        bindlessIndex_ = manager.registerMaterialData(
            desc.baseColor, desc.emissiveColor, desc.metallic, desc.roughness, desc.ao,
            manager.ensureBindlessTextureIndex(albedo_),
            manager.ensureBindlessTextureIndex(normalMap_),
            manager.ensureBindlessTextureIndex(metallicRoughnessMap_),
            manager.ensureBindlessTextureIndex(emissiveMap_)
        );
    }
}

Material::~Material() = default;  // paramsBuffer_ / descriptorSet_ RAII

} // namespace saida
