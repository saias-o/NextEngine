#include "fx/ParticleEffect.hpp"

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

    ne::ParticleEffect explosion = ne::ParticleEffect::fromPreset(ne::ParticleSystemNode::EffectClass::Explosion);
    if (!require(!explosion.emitters[0].looping)) return 1;
    bool hasBurst = false;
    for (const ne::ParticleModule& m : explosion.emitters[0].modules) {
        hasBurst = hasBurst || m.type == ne::ParticleModuleType::Burst;
    }
    if (!require(hasBurst)) return 1;

    return 0;
}
