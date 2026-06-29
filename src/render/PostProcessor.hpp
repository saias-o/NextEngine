#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace ne {

class GpuProfiler;
class Pipeline;
class SceneSettings;
class VulkanDevice;

// Internal HDR post-processing block. It owns reusable intermediate targets and
// only rebuilds descriptors when the render targets are recreated.
class PostProcessor {
public:
    PostProcessor(VulkanDevice& device, VkExtent2D extent, VkFormat hdrFormat,
                  VkImageView hdrInputView);
    ~PostProcessor();
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void setHdrInput(VkImageView hdrInputView);
    void recordBloom(VkCommandBuffer cmd, const SceneSettings& settings,
                     const glm::vec4& sourceRect, GpuProfiler* profiler);

    VkImageView bloomView() const;
    VkSampler bloomSampler() const { return linearSampler_; }

private:
    struct Target {
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent2D extent{};
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    };

    struct BloomPush {
        glm::vec4 sourceRect{0.0f, 0.0f, 1.0f, 1.0f};
        glm::vec4 params{0.0f}; // x threshold, y use bright-pass, z radius, w unused
    };

    void createTargets();
    void destroyTargets();
    void createSampler();
    void createDescriptorResources();
    void updateDescriptorSets();
    void createPipelines();
    void transitionTarget(VkCommandBuffer cmd, Target& target, VkImageLayout newLayout,
                          VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                          VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);
    void beginFullscreenPass(VkCommandBuffer cmd, Target& target, bool clear);

    VulkanDevice& device_;
    VkExtent2D extent_{};
    VkFormat hdrFormat_ = VK_FORMAT_UNDEFINED;
    VkImageView hdrInputView_ = VK_NULL_HANDLE;

    std::vector<Target> bloom_;
    VkSampler linearSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout inputSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> downsampleSets_;
    std::vector<VkDescriptorSet> upsampleSets_;
    std::unique_ptr<Pipeline> bloomDownsamplePipeline_;
    std::unique_ptr<Pipeline> bloomUpsamplePipeline_;
};

} // namespace ne
