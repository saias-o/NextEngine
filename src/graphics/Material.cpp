#include "graphics/Material.hpp"

#include "graphics/Buffer.hpp"
#include "graphics/Texture.hpp"
#include "graphics/VulkanDevice.hpp"

#include <array>
#include <stdexcept>

namespace ne {

namespace {
struct MaterialParams {
    glm::vec4 baseColor;  // matches MaterialUBO in shader.frag (std140)
};
}

Material::Material(VulkanDevice& device, VkDescriptorSetLayout layout, VkDescriptorPool pool,
                   Texture* texture, const glm::vec4& baseColor)
    : device_(device), texture_(texture) {
    MaterialParams params{baseColor};
    paramsBuffer_ = std::make_unique<Buffer>(device_, sizeof(MaterialParams),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, MemoryUsage::HostVisible);
    paramsBuffer_->write(&params, sizeof(params));

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = pool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &layout;
    if (vkAllocateDescriptorSets(device_.device(), &allocInfo, &descriptorSet_) != VK_SUCCESS)
        throw std::runtime_error("failed to allocate material descriptor set");

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = texture_->imageView();
    imageInfo.sampler = texture_->sampler();

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = paramsBuffer_->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(MaterialParams);

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &imageInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSet_;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &bufferInfo;

    vkUpdateDescriptorSets(device_.device(),
        static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

Material::~Material() = default;  // paramsBuffer_ RAII; set freed with the pool

} // namespace ne
