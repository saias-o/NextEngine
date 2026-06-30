#pragma once

#include "graphics/VmaFwd.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <functional>

namespace saida {

class VulkanDevice;

// Depth 2D-array shadow maps for directional and spot lights.
class ShadowMap {
public:
    static constexpr uint32_t kMaxShadows = 4;
    
    explicit ShadowMap(VulkanDevice& device, uint32_t initialResolution = 2048);
    ~ShadowMap();
    ShadowMap(const ShadowMap&) = delete;
    ShadowMap& operator=(const ShadowMap&) = delete;

    // Resizes the shadow map array. Returns true if resized, false if unchanged.
    // If resized, the arrayView() changes and MUST be updated in descriptors.
    bool resize(uint32_t newResolution);

    // Emits scene geometry for one shadow layer.
    using DrawGeometryFn = std::function<void(VkCommandBuffer, VkPipelineLayout, int layer)>;

    // Records `count` depth-only passes (clamped to kMaxShadows) into `cmd`,
    // before the main render pass. No-op when count == 0.
    void record(VkCommandBuffer cmd, int count, const DrawGeometryFn& drawGeometry);

    VkImageView arrayView() const { return arrayView_; }
    VkSampler sampler() const { return sampler_; }

private:
    void createImage();
    void createViews();
    void createSampler();
    void createPipeline();

    VulkanDevice& device_;
    VkFormat format_{};
    uint32_t resolution_;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView arrayView_ = VK_NULL_HANDLE;                 // all layers, sampled
    std::array<VkImageView, kMaxShadows> layerViews_{};      // one per layer (attachment)
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace saida
