#pragma once

#include <vulkan/vulkan.h>

#include <string>
#include <vector>

namespace saida {

class VulkanDevice;

enum class BlendMode {
    None,
    Alpha,
    Additive,
};

// A graphics pipeline configured for the Vertex layout, built against dynamic
// rendering attachment formats (no VkRenderPass) and descriptor set layouts.
class Pipeline {
public:
    Pipeline(VulkanDevice& device, const std::string& vertPath, const std::string& fragPath,
             const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
             const std::vector<VkDescriptorSetLayout>& setLayouts,
             VkSampleCountFlagBits samples, bool useVertexInput = true, bool useDepth = true, uint32_t pushConstantSize = 0,
             bool depthWrite = true, VkCompareOp depthCompare = VK_COMPARE_OP_LESS, VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT,
             bool useBlending = false, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
             uint32_t viewMask = 0);
    Pipeline(VulkanDevice& device, const std::string& vertPath, const std::string& fragPath,
             const std::vector<VkFormat>& colorFormats, VkFormat depthFormat,
             const std::vector<VkDescriptorSetLayout>& setLayouts,
             VkSampleCountFlagBits samples, bool useVertexInput, bool useDepth, uint32_t pushConstantSize,
             bool depthWrite, VkCompareOp depthCompare, VkCullModeFlags cullMode,
             BlendMode blendMode, VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
             uint32_t viewMask = 0);
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

} // namespace saida
