#include "scene/ParticleSystemNode.hpp"

#include "core/Log.hpp"
#include "fx/ParticleEffect.hpp"
#include "fx/ParticlePresetLibrary.hpp"

namespace ne {

void ParticleSystemNode::describe(reflect::TypeBuilder<ParticleSystemNode>& t) {
    t.doc("Lightweight billboard particle emitter rendered by NEFX.");
    t.property("effectClass", &ParticleSystemNode::effectClass)
        .enumValues({"Simple", "Fire", "Magic", "Rain", "Snow", "Smoke", "Explosion"})
        .tooltip("high-level preset family");
    t.property("effectPath", &ParticleSystemNode::effectPath)
        .tooltip("absolute or project-resolved .nefx path to load through loadEffect");
    t.property("maxParticles", &ParticleSystemNode::maxParticles).range(1.0, 20000.0)
        .tooltip("maximum live particles for this emitter");
    t.property("spawnRate", &ParticleSystemNode::spawnRate).range(0.0, 5000.0)
        .tooltip("particles spawned per second while playing");
    t.property("lifetime", &ParticleSystemNode::lifetime).range(0.02, 60.0)
        .tooltip("particle lifetime in seconds");
    t.property("startSpeed", &ParticleSystemNode::startSpeed).range(0.0, 100.0)
        .tooltip("initial radial speed");
    t.property("startSize", &ParticleSystemNode::startSize).range(0.001, 100.0)
        .tooltip("initial billboard size in metres");
    t.property("startColor", &ParticleSystemNode::startColor).tooltip("linear HDR start color");
    t.property("endColor", &ParticleSystemNode::endColor).tooltip("linear HDR end color");
    t.property("gravity", &ParticleSystemNode::gravity).tooltip("world-space acceleration");
    t.property("radius", &ParticleSystemNode::radius).range(0.0, 1000.0)
        .tooltip("spawn sphere radius");
    t.property("emissive", &ParticleSystemNode::emissive).range(0.0, 100.0)
        .tooltip("HDR intensity multiplier");
    t.property("blendMode", &ParticleSystemNode::blendMode)
        .enumValues({"Alpha", "Additive"})
        .tooltip("particle blending mode");
    t.property("looping", &ParticleSystemNode::looping);
    t.property("playing", &ParticleSystemNode::playing);
    t.signal("finished", &ParticleSystemNode::finished);
    t.slot("play", &ParticleSystemNode::play);
    t.slot("stop", &ParticleSystemNode::stop);
    t.slot("burst", &ParticleSystemNode::burst);
    t.slot("applyEffectPreset", &ParticleSystemNode::applyEffectPreset);
    t.slot("loadEffect", &ParticleSystemNode::loadEffect);
}

void ParticleSystemNode::play() {
    playing = true;
}

void ParticleSystemNode::stop() {
    playing = false;
}

void ParticleSystemNode::burst() {
    ++pendingBursts_;
    playing = true;
}

void ParticleSystemNode::applyEffectPreset() {
    ParticlePresetLibrary::apply(*this, effectClass);
}

void ParticleSystemNode::loadEffect() {
    if (effectPath.empty()) return;
    try {
        ParticleEffect effect = ParticleEffect::loadFromFile(effectPath);
        if (!effect.applyTo(*this)) {
            Log::warn("ParticleSystemNode: effect has no emitter: ", effectPath);
        }
    } catch (const std::exception& e) {
        Log::warn("ParticleSystemNode: failed to load effect '", effectPath, "': ", e.what());
    }
}

uint32_t ParticleSystemNode::consumeBurstCount() {
    const uint32_t count = pendingBursts_;
    pendingBursts_ = 0;
    return count;
}

} // namespace ne
