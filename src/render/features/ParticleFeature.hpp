#pragma once

#include "graphics/Pipeline.hpp"
#include "render/RenderFeature.hpp"
#include "scene/ParticleSystemNode.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace ne {

class Buffer;
class VulkanDevice;

class ParticleFeature : public ScenePassFeature {
public:
    ~ParticleFeature() override;
    void createPipelines(const RenderContext& ctx) override;
    void record(const FrameContext& fc) override;

private:
    struct CpuParticle {
        glm::vec3 position{0.0f};
        glm::vec3 velocity{0.0f};
        glm::vec4 startColor{1.0f};
        glm::vec4 endColor{1.0f};
        float age = 0.0f;
        float lifetime = 1.0f;
        float startSize = 0.1f;
        float rotation = 0.0f;
        float angularVelocity = 0.0f;
    };

    struct EmitterState {
        std::vector<CpuParticle> particles;
        float spawnAccumulator = 0.0f;
        float lastTime = -1.0f;
        uint32_t seed = 1;
        bool emittedFinished = false;
    };

    struct GpuParticle {
        glm::vec4 positionSize; // xyz world position, w size
        glm::vec4 color;        // linear HDR premultiplier handled by blend state
    };

    struct Push {
        uint32_t particleOffset = 0;
        uint32_t pad[3]{};
    };

    static constexpr uint32_t kFramesInFlightFallback = 2;
    static constexpr uint32_t kMaxGpuParticles = 65536;
    static constexpr uint32_t kVertsPerParticle = 6;

    VulkanDevice* device_ = nullptr;
    std::unique_ptr<Pipeline> alphaPipeline_;
    std::unique_ptr<Pipeline> additivePipeline_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<std::unique_ptr<Buffer>> particleBuffers_;
    std::vector<VkDescriptorSet> sets_;
    std::unordered_map<ParticleSystemNode*, EmitterState> states_;

    void spawn(ParticleSystemNode& emitter, EmitterState& state, uint32_t count);
    void simulate(ParticleSystemNode& emitter, EmitterState& state, float dt);
    uint32_t pack(const std::vector<ParticleSystemNode*>& emitters,
                  ParticleSystemNode::BlendMode mode,
                  GpuParticle* out, uint32_t capacity, uint32_t& offset);
};

} // namespace ne
