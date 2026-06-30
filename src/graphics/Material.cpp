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
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible);
    paramsBuffer_->write(&params, sizeof(params));

    // 1. Classic Path: Allocate local descriptor set and write it
    descriptorSet_ = manager.allocateMaterialSet(manager.materialSetLayout());

    VkDescriptorImageInfo albedoInfo{};
    albedoInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    albedoInfo.imageView = albedo_->imageView();
    albedoInfo.sampler = albedo_->sampler();

    VkDescriptorImageInfo normalInfo{};
    normalInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalInfo.imageView = normalMap_->imageView();
    normalInfo.sampler = normalMap_->sampler();

    VkDescriptorImageInfo mrInfo{};
    mrInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    mrInfo.imageView = metallicRoughnessMap_->imageView();
    mrInfo.sampler = metallicRoughnessMap_->sampler();

    VkDescriptorImageInfo emissiveInfo{};
    emissiveInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    emissiveInfo.imageView = emissiveMap_->imageView();
    emissiveInfo.sampler = emissiveMap_->sampler();

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = paramsBuffer_->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(MaterialParams);

    std::array<VkWriteDescriptorSet, 5> writes{};
    
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].dstArrayElement = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &albedoInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet_;
    writes[1].dstBinding = 1;
    writes[1].dstArrayElement = 0;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &normalInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSet_;
    writes[2].dstBinding = 2;
    writes[2].dstArrayElement = 0;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &mrInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSet_;
    writes[3].dstBinding = 3;
    writes[3].dstArrayElement = 0;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &bufferInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descriptorSet_;
    writes[4].dstBinding = 4;
    writes[4].dstArrayElement = 0;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    writes[4].pImageInfo = &emissiveInfo;

    vkUpdateDescriptorSets(device_.device(), static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

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

Material::~Material() = default;  // paramsBuffer_ RAII; set freed with the pool

} // namespace saida
