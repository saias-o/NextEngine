#pragma once

#include "rhi/Rhi.hpp"

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace saida {

class Buffer;
class ComputePipeline;
class VulkanDevice;

// GPU backing store for SaidaFX. The CPU uploads compact emitter records only; the
// GPU keeps particle state alive across frames, compacts survivors, recycles dead
// slots, and writes the indirect draw command consumed by the renderer.
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
        glm::vec4 rotationStretch; // x radians, y vertical stretch, z align-to-world-down, w reserved
    };

    struct GpuEmitter {
        glm::vec4 positionRadius;
        glm::vec4 gravityLifetime;
        glm::vec4 colorA;
        glm::vec4 colorB;
        glm::vec4 params;      // x speed, y size, z spawn count, w spawn offset
        glm::vec4 shape;       // x type, y/z/w box extents
        glm::vec4 detail;      // x cone angle rad, y ring thickness, z end size, w stretch
        glm::vec4 forces;      // x drag, y noise strength, z noise frequency, w attractor strength
        glm::vec4 attractor;   // xyz attractor position, w effect class
    };

    struct GpuCounters {
        uint32_t readAliveCount = 0;
        uint32_t aliveCount = 0;
        uint32_t deadCount = 0;
        uint32_t emitCount = 0;
    };

    struct ComputePush {
        uint32_t particleCount = 0;
        uint32_t emitterCount = 0;
        uint32_t emitCount = 0;
        float dt = 0.0f;
        float time = 0.0f;
    };

    ParticleRuntime(VulkanDevice& device, const Desc& desc);
    ~ParticleRuntime();
    ParticleRuntime(const ParticleRuntime&) = delete;
    ParticleRuntime& operator=(const ParticleRuntime&) = delete;

    uint32_t framesInFlight() const { return desc_.framesInFlight; }
    uint32_t maxParticles() const { return desc_.maxParticles; }

    rhi::BindGroupLayout& renderSetLayout() const { return *renderSetLayout_; }
    VkDescriptorSet renderSet(uint32_t parity) const;
    VkBuffer indirectBuffer() const;

    GpuEmitter* mappedEmitters(uint32_t frame) const;
    void flushEmitters(uint32_t frame, uint32_t count);
    void reset();

    // Returns the alive-list parity that renderSet() should use for the draw.
    uint32_t recordCompute(VkCommandBuffer cmd, uint32_t frame,
                           uint32_t emitterCount, uint32_t emitCount,
                           float dt, float time);

private:
    struct SimParticle {
        glm::vec4 positionAge;
        glm::vec4 velocityLifetime;
        glm::vec4 colorA;
        glm::vec4 colorB;
        glm::vec4 sizeRotation;  // x start size, y rotation, z angular velocity, w end size scale
        glm::vec4 renderParams;  // x stretch, y align-down, z/w reserved
    };

    uint32_t frameIndex(uint32_t frame) const;
    void createRenderResources();
    void createComputeResources();
    void recordInit(VkCommandBuffer cmd) const;
    void recordComputeBarrier(VkCommandBuffer cmd,
                              VkPipelineStageFlags2 dstStage,
                              VkAccessFlags2 dstAccess) const;
    VkDescriptorSet computeSet(uint32_t frame, uint32_t readParity) const;

    VulkanDevice& device_;
    Desc desc_;
    bool initialized_ = false;
    uint32_t aliveReadParity_ = 0;

    std::unique_ptr<rhi::BindGroupLayout> renderSetLayout_;
    std::unique_ptr<Buffer> simParticleBuffer_;
    std::array<std::unique_ptr<Buffer>, 2> aliveIndexBuffers_;
    std::unique_ptr<Buffer> deadIndexBuffer_;
    std::unique_ptr<Buffer> counterBuffer_;
    std::unique_ptr<Buffer> indirectBuffer_;
    std::vector<std::unique_ptr<rhi::BindGroup>> renderSets_;

    std::unique_ptr<rhi::BindGroupLayout> computeSetLayout_;
    std::vector<std::unique_ptr<Buffer>> emitterBuffers_;
    std::vector<std::unique_ptr<rhi::BindGroup>> computeSets_;

    std::unique_ptr<ComputePipeline> initPipeline_;
    std::unique_ptr<ComputePipeline> preparePipeline_;
    std::unique_ptr<ComputePipeline> emitPipeline_;
    std::unique_ptr<ComputePipeline> simPipeline_;
    std::unique_ptr<ComputePipeline> finalizePipeline_;
};

} // namespace saida
