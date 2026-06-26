#pragma once

#include "scene/ParticleSystemNode.hpp"

#include <glm/glm.hpp>

namespace ne {

struct ParticlePreset {
    ParticleSystemNode::EffectClass effectClass = ParticleSystemNode::EffectClass::Simple;
    int maxParticles = 256;
    float spawnRate = 48.0f;
    float lifetime = 1.5f;
    float startSpeed = 1.0f;
    float startSize = 0.2f;
    glm::vec4 startColor{1.0f};
    glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
    glm::vec3 gravity{0.0f};
    float radius = 0.25f;
    float emissive = 1.0f;
    ParticleSystemNode::BlendMode blendMode = ParticleSystemNode::BlendMode::Additive;
    bool looping = true;
    bool playing = true;
};

class ParticlePresetLibrary {
public:
    static ParticlePreset presetFor(ParticleSystemNode::EffectClass effectClass);
    static void apply(ParticleSystemNode& node, ParticleSystemNode::EffectClass effectClass);
};

} // namespace ne
