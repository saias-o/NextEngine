#pragma once

#include "rhi/Rhi.hpp"

#include <functional>
#include <memory>

namespace saida {

// Depth 2D-array shadow maps for directional and spot lights. Fully rhi::
// (encoder recording since 16.3.e.a, RenderTexture creation since 16.3.f).
class ShadowMap {
public:
    static constexpr uint32_t kMaxShadows = 4;

    explicit ShadowMap(rhi::Device& device, uint32_t initialResolution = 2048);
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

    rhi::TextureView arrayView() const { return texture_->view(); }
    rhi::SamplerHandle sampler() const { return sampler_->handle(); }

private:
    void createTexture();
    void createSampler();
    void createPipeline();

    rhi::Device& device_;
    rhi::Format format_ = rhi::Format::Undefined;
    uint32_t resolution_;

    std::unique_ptr<rhi::RenderTexture> texture_;  // kMaxShadows depth layers
    std::unique_ptr<rhi::Sampler> sampler_;

    std::unique_ptr<rhi::Pipeline> pipeline_;  // depth-only, vertex-stage push constants
};

} // namespace saida
