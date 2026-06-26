#include "fx/ParticleEffect.hpp"
#include "fx/ParticleQuality.hpp"
#include "scene/ParticleSystemNode.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace {

bool require(bool ok) {
    return ok;
}

} // namespace

int main() {
    ne::ParticleEffect fire = ne::ParticleEffect::fromPreset(ne::ParticleSystemNode::EffectClass::Fire);
    if (!require(fire.name == "Fire")) return 1;
    if (!require(fire.emitters.size() == 1)) return 1;
    if (!require(fire.emitters[0].effectClass == ne::ParticleSystemNode::EffectClass::Fire)) return 1;
    if (!require(!fire.emitters[0].modules.empty())) return 1;

    nlohmann::json saved = fire.toJson();
    if (!require(saved["format"].get<std::string>() == "NEFX")) return 1;
    if (!require(saved["emitters"][0]["modules"].is_array())) return 1;

    ne::ParticleEffect loaded = ne::ParticleEffect::fromJson(saved);
    if (!require(loaded.name == fire.name)) return 1;
    if (!require(loaded.emitters.size() == fire.emitters.size())) return 1;
    if (!require(loaded.emitters[0].modules.size() == fire.emitters[0].modules.size())) return 1;
    if (!require(loaded.emitters[0].blendMode == ne::ParticleSystemNode::BlendMode::Additive)) return 1;
    ne::ParticleSystemNode compiledFire;
    if (!require(loaded.applyTo(compiledFire))) return 1;
    if (!require(compiledFire.effectClass == ne::ParticleSystemNode::EffectClass::Fire)) return 1;
    if (!require(compiledFire.spawnRate == 120.0f)) return 1;
    if (!require(compiledFire.radius == 0.18f)) return 1;
    if (!require(compiledFire.emissive == 4.5f)) return 1;

    ne::ParticleEffect explosion = ne::ParticleEffect::fromPreset(ne::ParticleSystemNode::EffectClass::Explosion);
    if (!require(!explosion.emitters[0].looping)) return 1;
    bool hasBurst = false;
    for (const ne::ParticleModule& m : explosion.emitters[0].modules) {
        hasBurst = hasBurst || m.type == ne::ParticleModuleType::Burst;
    }
    if (!require(hasBurst)) return 1;
    ne::ParticleSystemNode compiledExplosion;
    if (!require(explosion.applyTo(compiledExplosion))) return 1;
    if (!require(compiledExplosion.effectClass == ne::ParticleSystemNode::EffectClass::Explosion)) return 1;
    if (!require(!compiledExplosion.looping)) return 1;
    if (!require(compiledExplosion.spawnRate == 0.0f)) return 1;

    ne::ParticleQualityBudget low = ne::particleQualityBudget(ne::QualityTier::Low);
    ne::ParticleQualityBudget ultra = ne::particleQualityBudget(ne::QualityTier::Ultra);
    if (!require(low.maxGpuParticles < ultra.maxGpuParticles)) return 1;
    if (!require(low.farUpdateInterval > ultra.farUpdateInterval)) return 1;

    ne::ParticleSystemNode node;
    node.maxParticles = 999999;
    if (!require(ne::particleEmitterCapacity(node, low) == low.maxParticlesPerEmitter)) return 1;

    return 0;
}
