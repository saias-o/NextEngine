#include "render/features/ParticleFeature.hpp"

#include "core/Camera.hpp"
#include "core/Paths.hpp"
#include "graphics/VulkanDevice.hpp"
#include "scene/Scene.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace ne {

namespace {

uint32_t hashPtr(const void* p) {
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    v ^= v >> 33;
    v *= 0xff51afd7ed558ccdULL;
    v ^= v >> 33;
    return static_cast<uint32_t>(v) | 1u;
}

float next01(uint32_t& seed) {
    seed = seed * 1664525u + 1013904223u;
    return static_cast<float>((seed >> 8) & 0x00ffffffu) / 16777215.0f;
}

float nextSigned(uint32_t& seed) {
    return next01(seed) * 2.0f - 1.0f;
}

glm::vec3 randomUnit(uint32_t& seed) {
    glm::vec3 v;
    do {
        v = glm::vec3(nextSigned(seed), nextSigned(seed), nextSigned(seed));
    } while (glm::dot(v, v) < 0.0001f || glm::dot(v, v) > 1.0f);
    return glm::normalize(v);
}

glm::vec3 directionFor(ParticleSystemNode::EffectClass effect, uint32_t& seed) {
    glm::vec3 dir = randomUnit(seed);
    switch (effect) {
        case ParticleSystemNode::EffectClass::Fire:
        case ParticleSystemNode::EffectClass::Smoke:
            dir = glm::normalize(glm::vec3(dir.x * 0.35f, std::abs(dir.y) + 1.25f, dir.z * 0.35f));
            break;
        case ParticleSystemNode::EffectClass::Rain:
            dir = glm::normalize(glm::vec3(dir.x * 0.12f, -1.0f, dir.z * 0.12f));
            break;
        case ParticleSystemNode::EffectClass::Snow:
            dir = glm::normalize(glm::vec3(dir.x * 0.35f, -0.45f - next01(seed) * 0.4f, dir.z * 0.35f));
            break;
        case ParticleSystemNode::EffectClass::Magic:
            dir = glm::normalize(glm::vec3(dir.x, std::abs(dir.y) * 0.25f, dir.z));
            break;
        case ParticleSystemNode::EffectClass::Explosion:
        case ParticleSystemNode::EffectClass::Simple:
        default:
            break;
    }
    return dir;
}

glm::vec3 spawnOffset(ParticleSystemNode& emitter, uint32_t& seed) {
    if (emitter.radius <= 0.0f) return glm::vec3(0.0f);
    glm::vec3 dir = randomUnit(seed);
    float r = std::cbrt(next01(seed)) * emitter.radius;
    if (emitter.effectClass == ParticleSystemNode::EffectClass::Rain ||
        emitter.effectClass == ParticleSystemNode::EffectClass::Snow) {
        return glm::vec3(dir.x * emitter.radius, 0.0f, dir.z * emitter.radius);
    }
    return dir * r;
}

float classSizeMultiplier(ParticleSystemNode::EffectClass effect, uint32_t& seed) {
    float jitter = 0.75f + next01(seed) * 0.5f;
    if (effect == ParticleSystemNode::EffectClass::Smoke) return jitter * 1.8f;
    if (effect == ParticleSystemNode::EffectClass::Explosion) return jitter * 1.5f;
    if (effect == ParticleSystemNode::EffectClass::Rain) return jitter * 0.35f;
    if (effect == ParticleSystemNode::EffectClass::Snow) return jitter * 0.65f;
    return jitter;
}

glm::vec3 cameraPositionFor(const FrameContext& fc) {
    if (!fc.stereo && fc.camera) return fc.camera->position;
    if (fc.eyes && !fc.eyes->empty()) return (*fc.eyes)[0].eyePosition;
    return glm::vec3(0.0f);
}

bool sphereInFrustum(const Frustum& frustum, const glm::vec3& center, float radius) {
    for (const glm::vec4& plane : frustum.planes) {
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) return false;
    }
    return true;
}

float emitterCullRadius(const ParticleSystemNode& emitter) {
    const float travel = std::max(0.0f, emitter.startSpeed) * std::max(0.0f, emitter.lifetime);
    const float gravityTravel = 0.5f * glm::length(emitter.gravity) * emitter.lifetime * emitter.lifetime;
    return std::max(0.1f, emitter.radius + travel + gravityTravel + emitter.startSize * 2.0f);
}

} // namespace

ParticleFeature::~ParticleFeature() {
    runtime_.reset();
}

void ParticleFeature::createPipelines(const RenderContext& ctx) {
    device_ = &ctx.device;
    budget_ = particleQualityBudget(ctx.device.capabilities().tier);
    const uint32_t frames = std::max(ctx.framesInFlight, kFramesInFlightFallback);
    ParticleRuntime::Desc runtimeDesc{};
    runtimeDesc.framesInFlight = frames;
    runtimeDesc.maxParticles = budget_.maxGpuParticles;
    runtimeDesc.maxEmitters = budget_.maxEmitters;
    runtime_ = std::make_unique<ParticleRuntime>(ctx.device, runtimeDesc);

    std::vector<VkDescriptorSetLayout> setLayouts = {ctx.globalSetLayout, runtime_->renderSetLayout()};
    std::vector<VkFormat> colorFormats = {ctx.colorFormat};
    const char* vert = ctx.stereo() ? "multiview.particle_render.vert.spv" : "particle_render.vert.spv";

    alphaPipeline_ = std::make_unique<Pipeline>(ctx.device,
        shaderPath(vert), shaderPath("particle_render.frag.spv"),
        colorFormats, ctx.depthFormat, setLayouts, ctx.samples,
        false, true, sizeof(Push), false, VK_COMPARE_OP_LESS, VK_CULL_MODE_NONE,
        BlendMode::Alpha, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, ctx.viewMask);

    additivePipeline_ = std::make_unique<Pipeline>(ctx.device,
        shaderPath(vert), shaderPath("particle_render.frag.spv"),
        colorFormats, ctx.depthFormat, setLayouts, ctx.samples,
        false, true, sizeof(Push), false, VK_COMPARE_OP_LESS, VK_CULL_MODE_NONE,
        BlendMode::Additive, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, ctx.viewMask);
}

void ParticleFeature::spawn(ParticleSystemNode& emitter, EmitterState& state, uint32_t count, uint32_t capacity) {
    if (capacity == 0 || emitter.lifetime <= 0.0f) return;

    if (state.seed == 1) state.seed = hashPtr(&emitter);
    if (state.particles.capacity() < capacity) {
        state.particles.reserve(capacity);
    }
    const glm::vec3 center = glm::vec3(emitter.worldTransform()[3]);
    for (uint32_t i = 0; i < count && state.particles.size() < capacity; ++i) {
        CpuParticle p;
        p.position = center + spawnOffset(emitter, state.seed);
        p.velocity = directionFor(emitter.effectClass, state.seed) *
            (emitter.startSpeed * (0.75f + next01(state.seed) * 0.5f));
        p.startColor = emitter.startColor;
        p.endColor = emitter.endColor;
        p.lifetime = std::max(0.02f, emitter.lifetime * (0.75f + next01(state.seed) * 0.5f));
        p.startSize = std::max(0.001f, emitter.startSize * classSizeMultiplier(emitter.effectClass, state.seed));
        p.rotation = next01(state.seed) * glm::two_pi<float>();
        p.angularVelocity = nextSigned(state.seed) * 1.5f;
        state.particles.push_back(p);
    }
}

void ParticleFeature::simulate(ParticleSystemNode& emitter, EmitterState& state, float dt) {
    if (dt <= 0.0f) return;

    const glm::vec3 acceleration = emitter.gravity;
    size_t write = 0;
    for (size_t read = 0; read < state.particles.size(); ++read) {
        CpuParticle p = state.particles[read];
        p.age += dt;
        if (p.age >= p.lifetime) continue;
        p.velocity += acceleration * dt;
        p.position += p.velocity * dt;
        p.rotation += p.angularVelocity * dt;
        state.particles[write++] = p;
    }
    state.particles.resize(write);
}

uint32_t ParticleFeature::pack(const std::vector<ParticleSystemNode*>& emitters,
                               ParticleSystemNode::BlendMode mode,
                               ParticleRuntime::RenderParticle* out, uint32_t capacity, uint32_t& offset) {
    const uint32_t start = offset;
    for (ParticleSystemNode* emitter : emitters) {
        if (!emitter || emitter->blendMode != mode) continue;
        auto it = states_.find(emitter);
        if (it == states_.end()) continue;
        if (!it->second.visibleThisFrame) continue;

        for (const CpuParticle& p : it->second.particles) {
            if (offset >= capacity) return offset - start;
            float t = glm::clamp(p.age / std::max(p.lifetime, 0.001f), 0.0f, 1.0f);
            glm::vec4 color = glm::mix(p.startColor, p.endColor, t);
            color.r *= emitter->emissive;
            color.g *= emitter->emissive;
            color.b *= emitter->emissive;
            float size = p.startSize * (1.0f - 0.65f * t);
            out[offset++] = ParticleRuntime::RenderParticle{glm::vec4(p.position, std::max(size, 0.001f)), color};
        }
    }
    return offset - start;
}

void ParticleFeature::record(const FrameContext& fc) {
    const auto& emitters = fc.scene.particleSystems();
    if (emitters.empty() || !runtime_ || !alphaPipeline_ || !additivePipeline_) return;

    ++recordSerial_;
    const glm::vec3 cameraPosition = cameraPositionFor(fc);
    const bool canFrustumCull = !fc.stereo && fc.camera;
    const Frustum frustum = canFrustumCull ? fc.camera->getFrustum() : Frustum{};

    for (ParticleSystemNode* emitter : emitters) {
        if (!emitter || !emitter->isActiveInHierarchy()) continue;

        EmitterState& state = states_[emitter];
        state.lastSeenSerial = recordSerial_;
        if (state.seed == 1) state.seed = hashPtr(emitter);
        state.visibleThisFrame = true;

        float dt = 0.0f;
        if (state.lastTime >= 0.0f) {
            dt = glm::clamp(fc.time - state.lastTime, 0.0f, 0.1f);
        }
        state.lastTime = fc.time;
        if (emitter->playing) {
            state.emittedFinished = false;
        }

        const uint32_t capacity = particleEmitterCapacity(*emitter, budget_);
        if (state.particles.size() > static_cast<size_t>(capacity)) {
            state.particles.resize(static_cast<size_t>(capacity));
        }

        const glm::vec3 emitterPosition = glm::vec3(emitter->worldTransform()[3]);
        if (canFrustumCull && !sphereInFrustum(frustum, emitterPosition, emitterCullRadius(*emitter))) {
            state.visibleThisFrame = false;
            continue;
        }

        float simDt = dt;
        const glm::vec3 delta = emitterPosition - cameraPosition;
        const float farDistance2 = budget_.farUpdateDistance * budget_.farUpdateDistance;
        const bool farEmitter = glm::dot(delta, delta) > farDistance2;
        if (farEmitter && budget_.farUpdateInterval > 0.0f && dt > 0.0f) {
            state.updateAccumulator += dt;
            if (state.updateAccumulator < budget_.farUpdateInterval) {
                simDt = 0.0f;
            } else {
                simDt = glm::clamp(state.updateAccumulator, 0.0f, 0.25f);
                state.updateAccumulator = 0.0f;
            }
        } else {
            state.updateAccumulator = 0.0f;
        }

        simulate(*emitter, state, simDt);

        const uint32_t bursts = emitter->consumeBurstCount();
        if (bursts > 0) {
            const uint32_t burstSize = std::max(1u, capacity / 8u);
            spawn(*emitter, state, bursts * burstSize, capacity);
            state.emittedFinished = false;
        }

        if (emitter->playing && emitter->looping && emitter->spawnRate > 0.0f && simDt > 0.0f) {
            state.spawnAccumulator += emitter->spawnRate * simDt;
            uint32_t toSpawn = static_cast<uint32_t>(std::floor(state.spawnAccumulator));
            state.spawnAccumulator -= static_cast<float>(toSpawn);
            spawn(*emitter, state, toSpawn, capacity);
            if (toSpawn > 0) state.emittedFinished = false;
        }

        if (!emitter->playing && state.particles.empty() && !state.emittedFinished) {
            emitter->finished.emit();
            state.emittedFinished = true;
        }
    }

    for (auto it = states_.begin(); it != states_.end();) {
        if (it->second.lastSeenSerial != recordSerial_) it = states_.erase(it);
        else ++it;
    }

    const uint32_t frame = std::min<uint32_t>(fc.frameIndex, runtime_->framesInFlight() - 1);
    ParticleRuntime::RenderParticle* gpu = runtime_->mappedRenderParticles(frame);
    if (!gpu) return;

    uint32_t offset = 0;
    const uint32_t alphaOffset = offset;
    const uint32_t alphaCount = pack(emitters, ParticleSystemNode::BlendMode::Alpha,
                                     gpu, runtime_->maxParticles(), offset);
    const uint32_t additiveOffset = offset;
    const uint32_t additiveCount = pack(emitters, ParticleSystemNode::BlendMode::Additive,
                                        gpu, runtime_->maxParticles(), offset);
    if (offset == 0) return;
    runtime_->flushRenderParticles(frame, offset);

    VkDescriptorSet particleSet = runtime_->renderSet(frame);
    VkDescriptorSet bound[2] = {fc.globalSet, particleSet};
    auto drawBatch = [&](Pipeline& pipeline, uint32_t batchOffset, uint32_t batchCount) {
        if (batchCount == 0) return;
        pipeline.bind(fc.cmd);
        vkCmdBindDescriptorSets(fc.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline.layout(), 0, 2, bound, 0, nullptr);
        Push push{};
        push.particleOffset = batchOffset;
        vkCmdPushConstants(fc.cmd, pipeline.layout(),
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0, sizeof(Push), &push);
        vkCmdDraw(fc.cmd, batchCount * kVertsPerParticle, 1, 0, 0);
    };

    drawBatch(*alphaPipeline_, alphaOffset, alphaCount);
    drawBatch(*additivePipeline_, additiveOffset, additiveCount);
}

} // namespace ne
