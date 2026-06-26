#pragma once

#include "core/Reflection.hpp"
#include "core/Signal.hpp"
#include "scene/Node.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace ne {

class ParticleSystemNode : public Node {
public:
    ParticleSystemNode() : Node("ParticleSystem") {}

    NE_REFLECT_NODE(ParticleSystemNode, "ParticleSystem")

    enum class EffectClass {
        Simple = 0,
        Fire,
        Magic,
        Rain,
        Snow,
        Smoke,
        Explosion,
    };

    enum class BlendMode {
        Alpha = 0,
        Additive,
    };

    EffectClass effectClass = EffectClass::Simple;
    int maxParticles = 256;
    float spawnRate = 48.0f;
    float lifetime = 1.5f;
    float startSpeed = 1.0f;
    float startSize = 0.2f;
    glm::vec4 startColor{1.0f, 0.85f, 0.35f, 1.0f};
    glm::vec4 endColor{1.0f, 0.15f, 0.02f, 0.0f};
    glm::vec3 gravity{0.0f, -0.3f, 0.0f};
    float radius = 0.25f;
    float emissive = 2.0f;
    BlendMode blendMode = BlendMode::Additive;
    bool looping = true;
    bool playing = true;

    Signal<> finished;

    void play();
    void stop();
    void burst();

    uint32_t consumeBurstCount();

private:
    uint32_t pendingBursts_ = 0;
};

} // namespace ne
