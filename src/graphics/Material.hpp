#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <memory>

namespace ne {

class VulkanDevice;
class Texture;
class Buffer;

// A surface description: a texture plus shader parameters, exposed as descriptor
// set 1 (binding 0 = sampler, binding 1 = params UBO). Materials are created and
// owned by the ResourceManager; nodes reference them.
class Material {
public:
    Material(VulkanDevice& device, VkDescriptorSetLayout layout, VkDescriptorPool pool,
             Texture* texture, const glm::vec4& baseColor);
    ~Material();
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    VkDescriptorSet descriptorSet() const { return descriptorSet_; }
    Texture* texture() const { return texture_; }

private:
    VulkanDevice& device_;
    Texture* texture_;
    std::unique_ptr<Buffer> paramsBuffer_;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;  // freed with the pool
};

} // namespace ne
