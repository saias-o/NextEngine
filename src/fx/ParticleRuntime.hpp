#pragma once

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace ne {

class Buffer;
class ComputePipeline;
class VulkanDevice;

// GPU backing store for NEFX. Step 2 owns the SSBOs, descriptor sets and compute
// pipelines here; the v1 renderer can still CPU-pack billboards into the render
// buffer while later steps switch emission/simulation to the compute sets.
class ParticleRuntime {
public:
    struct Desc {
        uint32_t framesInFlight = 2;
        uint32_t maxParticles = 65536;
        uint32_t maxEmitters = 256;
    };

    struct RenderParticle {
        glm::vec4 positionSize; // xyz world position, w billboard size
        glm::vec4 color;        // linear HDR
    };

    struct ComputePush {
        uint32_t particleCount = 0;
        uint32_t emitterCount = 0;
        float dt = 0.0f;
        float time = 0.0f;
    };

    ParticleRuntime(VulkanDevice& device, const Desc& desc);
    ~ParticleRuntime();
    ParticleRuntime(const ParticleRuntime&) = delete;
    ParticleRuntime& operator=(const ParticleRuntime&) = delete;

    uint32_t framesInFlight() const { return desc_.framesInFlight; }
    uint32_t maxParticles() const { return desc_.maxParticles; }

    VkDescriptorSetLayout renderSetLayout() const { return renderSetLayout_; }
    VkDescriptorSet renderSet(uint32_t frame) const;
    RenderParticle* mappedRenderParticles(uint32_t frame) const;
    void flushRenderParticles(uint32_t frame, uint32_t count);

    VkDescriptorSetLayout computeSetLayout() const { return computeSetLayout_; }
    VkDescriptorSet computeSet(uint32_t frame) const;
    ComputePipeline* emitPipeline() const { return emitPipeline_.get(); }
    ComputePipeline* simPipeline() const { return simPipeline_.get(); }

private:
    struct SimParticle {
        glm::vec4 positionAge;
        glm::vec4 velocityLifetime;
        glm::vec4 color;
        glm::vec4 sizeRotation;
    };

    struct GpuEmitter {
        glm::vec4 positionRadius;
        glm::vec4 gravityLifetime;
        glm::vec4 colorA;
        glm::vec4 colorB;
        glm::vec4 params;
    };

    struct GpuCounters {
        uint32_t aliveCount = 0;
        uint32_t deadCount = 0;
        uint32_t emitCount = 0;
        uint32_t pad = 0;
    };

    uint32_t frameIndex(uint32_t frame) const;
    void createRenderResources();
    void createComputeResources();

    VulkanDevice& device_;
    Desc desc_;

    VkDescriptorSetLayout renderSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool renderPool_ = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Buffer>> renderParticleBuffers_;
    std::vector<VkDescriptorSet> renderSets_;

    VkDescriptorSetLayout computeSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool computePool_ = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Buffer>> simParticleBuffers_;
    std::vector<std::unique_ptr<Buffer>> aliveIndexBuffers_;
    std::vector<std::unique_ptr<Buffer>> deadIndexBuffers_;
    std::vector<std::unique_ptr<Buffer>> counterBuffers_;
    std::vector<std::unique_ptr<Buffer>> emitterBuffers_;
    std::vector<VkDescriptorSet> computeSets_;

    std::unique_ptr<ComputePipeline> emitPipeline_;
    std::unique_ptr<ComputePipeline> simPipeline_;
};

} // namespace ne
