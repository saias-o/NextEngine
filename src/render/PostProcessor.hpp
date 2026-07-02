#pragma once

#include "rhi/Rhi.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

namespace saida {

class GpuProfiler;
class Pipeline;
class SceneSettings;
class VulkanDevice;

// Internal HDR post-processing block. It owns reusable intermediate targets and
// only rebuilds descriptors when the render targets are recreated.
class PostProcessor {
public:
    PostProcessor(rhi::Device& device, rhi::Extent2D extent, rhi::Format hdrFormat,
                  rhi::TextureView hdrInputView);
    ~PostProcessor();
    PostProcessor(const PostProcessor&) = delete;
    PostProcessor& operator=(const PostProcessor&) = delete;

    void setHdrInput(rhi::TextureView hdrInputView);
    void recordBloom(rhi::CommandEncoder& encoder, const SceneSettings& settings,
                     const glm::vec4& sourceRect, GpuProfiler* profiler);

    rhi::TextureView bloomView() const;
    rhi::SamplerHandle bloomSampler() const { return linearSampler_->handle(); }

private:
    struct Target {
        std::unique_ptr<rhi::RenderTexture> texture;
        rhi::Extent2D extent{};
        // Explicitly tracked by the owner (PLAN_RHI 7.3: no automatic tracking).
        rhi::ResourceState state = rhi::ResourceState::Undefined;
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
    void transitionTarget(rhi::CommandEncoder& encoder, Target& target, rhi::ResourceState to);
    rhi::RenderPassEncoder beginFullscreenPass(rhi::CommandEncoder& encoder, Target& target,
                                               bool clear);

    rhi::Device& device_;
    rhi::Extent2D extent_{};
    rhi::Format hdrFormat_ = rhi::Format::Undefined;
    rhi::TextureView hdrInputView_ = {};

    std::vector<Target> bloom_;
    std::unique_ptr<rhi::Sampler> linearSampler_;
    // Pilot conversion of 16.3.d: descriptor layout/pool/sets behind rhi::.
    // Groups are immutable — re-pointing an input (setHdrInput, target resize)
    // recreates them. The layout must outlive the groups (it owns their pool).
    std::unique_ptr<rhi::BindGroupLayout> inputLayout_;
    std::vector<std::unique_ptr<rhi::BindGroup>> downsampleGroups_;
    std::vector<std::unique_ptr<rhi::BindGroup>> upsampleGroups_;
    std::unique_ptr<Pipeline> bloomDownsamplePipeline_;
    std::unique_ptr<Pipeline> bloomUpsamplePipeline_;
};

} // namespace saida
