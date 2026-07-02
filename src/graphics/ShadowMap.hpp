#pragma once

#include "rhi/Rhi.hpp"

#include <vulkan/vulkan.h>

#include <functional>
#include <memory>

namespace saida {

class VulkanDevice;

// Depth 2D-array shadow maps for directional and spot lights. Fully rhi::
// (encoder recording since 16.3.e.a, RenderTexture creation since 16.3.f).
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

    VkImageView arrayView() const { return texture_->view(); }
    VkSampler sampler() const { return sampler_; }

private:
    void createTexture();
    void createSampler();
    void createPipeline();

    VulkanDevice& device_;
    rhi::Format format_ = rhi::Format::Undefined;
    uint32_t resolution_;

    std::unique_ptr<rhi::RenderTexture> texture_;  // kMaxShadows depth layers
    VkSampler sampler_ = VK_NULL_HANDLE;

    std::unique_ptr<rhi::Pipeline> pipeline_;  // depth-only, vertex-stage push constants
};

} // namespace saida
