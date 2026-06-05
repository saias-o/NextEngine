#pragma once

#include "graphics/VmaFwd.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <functional>

namespace ne {

class VulkanDevice;

// Real-time shadow maps for directional and spot lights.
//
// Owns a single depth 2D-array image (one layer per shadow-casting light), a
// depth-only render pass / pipeline, a per-layer framebuffer and a comparison
// sampler for hardware PCF. Each frame the Renderer records `count` depth-only
// passes (one layer each) from every light's point of view, then samples the
// array view in the main fragment shader. Point lights are not handled here
// (they would need a cube map — out of scope).
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

    // Emits the scene geometry for one shadow layer. Gets the command buffer,
    // the depth pipeline layout (to push its `mat4 mvp = lightMatrix * world`)
    // and the layer index (which light/matrix to render).
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

} // namespace ne
