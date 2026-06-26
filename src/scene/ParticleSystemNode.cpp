#include "scene/ParticleSystemNode.hpp"

namespace ne {

void ParticleSystemNode::describe(reflect::TypeBuilder<ParticleSystemNode>& t) {
    t.doc("Lightweight billboard particle emitter rendered by NEFX.");
    t.property("effectClass", &ParticleSystemNode::effectClass)
        .enumValues({"Simple", "Fire", "Magic", "Rain", "Snow", "Smoke", "Explosion"})
        .tooltip("high-level preset family");
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

uint32_t ParticleSystemNode::consumeBurstCount() {
    const uint32_t count = pendingBursts_;
    pendingBursts_ = 0;
    return count;
}

} // namespace ne
