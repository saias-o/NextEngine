#pragma once

#include "graphics/VmaFwd.hpp"
#include "rhi/Rhi.hpp"

#include <vulkan/vulkan.h>

#include <array>
#include <functional>
#include <memory>

namespace saida {

class VulkanDevice;

// Depth 2D-array shadow maps for directional and spot lights.
// Pilot subsystem of the RHI CommandEncoder extraction (16.3.e.a): its command
// recording is fully rhi::, only resource creation (image/views) is still raw
// Vulkan (render-target creation is neutralised with 16.3.f).
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

    // Emits scene geometry for one shadow layer. The pass encoder already has the
    // depth-only pipeline bound; the callback pushes per-draw constants and draws.
    using DrawGeometryFn = std::function<void(rhi::RenderPassEncoder&, int layer)>;

    // Records `count` depth-only passes (clamped to kMaxShadows) into the
    // encoder, before the main render pass. No-op when count == 0.
    void record(rhi::CommandEncoder& encoder, int count, const DrawGeometryFn& drawGeometry);

    VkImageView arrayView() const { return arrayView_; }
    VkSampler sampler() const { return sampler_; }

private:
    void createImage();
    void createViews();
    void createSampler();
    void createPipeline();

    VulkanDevice& device_;
    rhi::Format format_ = rhi::Format::Undefined;
    uint32_t resolution_;

    VkImage image_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VkImageView arrayView_ = VK_NULL_HANDLE;                 // all layers, sampled
    std::array<VkImageView, kMaxShadows> layerViews_{};      // one per layer (attachment)
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::unique_ptr<rhi::Pipeline> pipeline_;  // depth-only, vertex-stage push constants
};

} // namespace saida
