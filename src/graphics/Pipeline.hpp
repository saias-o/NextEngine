#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace ne {

class VulkanDevice;

// A graphics pipeline configured for the Vertex layout, built against an
// existing render pass and descriptor set layout.
class Pipeline {
public:
    Pipeline(VulkanDevice& device, const std::string& vertPath, const std::string& fragPath,
             VkRenderPass renderPass, VkDescriptorSetLayout setLayout,
             VkSampleCountFlagBits samples);
    ~Pipeline();
    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    void bind(VkCommandBuffer cmd) const;
    VkPipelineLayout layout() const { return layout_; }

private:
    VkShaderModule createShaderModule(const std::vector<char>& code) const;

    VulkanDevice& device_;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace ne
