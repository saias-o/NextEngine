#pragma once

#include "fx/ParticleRuntime.hpp"
#include "graphics/Pipeline.hpp"
#include "render/RenderFeature.hpp"
#include "scene/ParticleSystemNode.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace ne {

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

    struct Push {
        uint32_t particleOffset = 0;
        uint32_t pad[3]{};
    };

    static constexpr uint32_t kFramesInFlightFallback = 2;
    static constexpr uint32_t kVertsPerParticle = 6;

    VulkanDevice* device_ = nullptr;
    std::unique_ptr<Pipeline> alphaPipeline_;
    std::unique_ptr<Pipeline> additivePipeline_;
    std::unique_ptr<ParticleRuntime> runtime_;
    std::unordered_map<ParticleSystemNode*, EmitterState> states_;

    void spawn(ParticleSystemNode& emitter, EmitterState& state, uint32_t count);
    void simulate(ParticleSystemNode& emitter, EmitterState& state, float dt);
    uint32_t pack(const std::vector<ParticleSystemNode*>& emitters,
                  ParticleSystemNode::BlendMode mode,
                  ParticleRuntime::RenderParticle* out, uint32_t capacity, uint32_t& offset);
};

} // namespace ne
