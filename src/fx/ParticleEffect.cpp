#include "fx/ParticleEffect.hpp"

#include "fx/ParticlePresetLibrary.hpp"

#include <fstream>
#include <stdexcept>

namespace ne {
namespace {

using json = nlohmann::json;

json vec3Json(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

json vec4Json(const glm::vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}

ParticleModule module(ParticleModuleType type, json params) {
    ParticleModule m;
    m.type = type;
    m.params = std::move(params);
    return m;
}

std::string presetName(ParticleSystemNode::EffectClass effectClass) {
    return std::string(toString(effectClass));
}

ParticleEmitterDesc emitterFromPreset(const ParticlePreset& preset) {
    ParticleEmitterDesc e;
    e.name = presetName(preset.effectClass);
    e.effectClass = preset.effectClass;
    e.maxParticles = preset.maxParticles;
    e.blendMode = preset.blendMode;
    e.looping = preset.looping;

    if (preset.spawnRate > 0.0f) {
        e.modules.push_back(module(ParticleModuleType::SpawnRate, {
            {"rate", preset.spawnRate}
        }));
    }

    if (preset.effectClass == ParticleSystemNode::EffectClass::Explosion) {
        e.modules.push_back(module(ParticleModuleType::Burst, {
            {"count", std::max(1, preset.maxParticles / 3)}
        }));
    }

    const char* shape = "Sphere";
    if (preset.effectClass == ParticleSystemNode::EffectClass::Rain ||
        preset.effectClass == ParticleSystemNode::EffectClass::Snow) {
        shape = "Disc";
    }
    e.modules.push_back(module(ParticleModuleType::Shape, {
        {"type", shape},
        {"radius", preset.radius}
    }));
    e.modules.push_back(module(ParticleModuleType::InitialVelocity, {
        {"speed", preset.startSpeed}
    }));
    e.modules.push_back(module(ParticleModuleType::Gravity, {
        {"acceleration", vec3Json(preset.gravity)}
    }));
    e.modules.push_back(module(ParticleModuleType::ColorOverLife, {
        {"start", vec4Json(preset.startColor)},
        {"end", vec4Json(preset.endColor)},
        {"emissive", preset.emissive}
    }));
    e.modules.push_back(module(ParticleModuleType::SizeOverLife, {
        {"start", preset.startSize},
        {"endScale", 0.35f}
    }));

    if (preset.effectClass == ParticleSystemNode::EffectClass::Smoke) {
        e.modules.push_back(module(ParticleModuleType::Drag, {
            {"linear", 0.35f}
        }));
        e.modules.push_back(module(ParticleModuleType::Noise, {
            {"strength", 0.22f},
            {"frequency", 1.4f}
        }));
    } else if (preset.effectClass == ParticleSystemNode::EffectClass::Magic) {
        e.modules.push_back(module(ParticleModuleType::Noise, {
            {"strength", 0.55f},
            {"frequency", 2.6f}
        }));
    } else if (preset.effectClass == ParticleSystemNode::EffectClass::Snow) {
        e.modules.push_back(module(ParticleModuleType::Noise, {
            {"strength", 0.18f},
            {"frequency", 0.7f}
        }));
    }

    return e;
}

json moduleToJson(const ParticleModule& m) {
    return {
        {"type", toString(m.type)},
        {"enabled", m.enabled},
        {"params", m.params.is_null() ? json::object() : m.params}
    };
}

ParticleModule moduleFromJson(const json& j) {
    ParticleModule m;
    m.type = particleModuleTypeFromString(j.value("type", "SpawnRate"));
    m.enabled = j.value("enabled", true);
    m.params = j.value("params", json::object());
    return m;
}

json emitterToJson(const ParticleEmitterDesc& e) {
    json modules = json::array();
    for (const ParticleModule& m : e.modules) modules.push_back(moduleToJson(m));
    return {
        {"name", e.name},
        {"effectClass", toString(e.effectClass)},
        {"maxParticles", e.maxParticles},
        {"blendMode", toString(e.blendMode)},
        {"looping", e.looping},
        {"modules", std::move(modules)}
    };
}

ParticleEmitterDesc emitterFromJson(const json& j) {
    ParticleEmitterDesc e;
    e.name = j.value("name", "Emitter");
    e.effectClass = particleEffectClassFromString(j.value("effectClass", "Simple"));
    e.maxParticles = j.value("maxParticles", 256);
    e.blendMode = particleBlendModeFromString(j.value("blendMode", "Additive"));
    e.looping = j.value("looping", true);
    if (auto it = j.find("modules"); it != j.end() && it->is_array()) {
        for (const json& m : *it) e.modules.push_back(moduleFromJson(m));
    }
    return e;
}

} // namespace

const char* toString(ParticleModuleType type) {
    switch (type) {
        case ParticleModuleType::SpawnRate: return "SpawnRate";
        case ParticleModuleType::Burst: return "Burst";
        case ParticleModuleType::Shape: return "Shape";
        case ParticleModuleType::InitialVelocity: return "InitialVelocity";
        case ParticleModuleType::Gravity: return "Gravity";
        case ParticleModuleType::Drag: return "Drag";
        case ParticleModuleType::Noise: return "Noise";
        case ParticleModuleType::ColorOverLife: return "ColorOverLife";
        case ParticleModuleType::SizeOverLife: return "SizeOverLife";
        case ParticleModuleType::Attractor: return "Attractor";
        case ParticleModuleType::SubEmitter: return "SubEmitter";
        default: return "SpawnRate";
    }
}

ParticleModuleType particleModuleTypeFromString(const std::string& text) {
    if (text == "Burst") return ParticleModuleType::Burst;
    if (text == "Shape") return ParticleModuleType::Shape;
    if (text == "InitialVelocity") return ParticleModuleType::InitialVelocity;
    if (text == "Gravity") return ParticleModuleType::Gravity;
    if (text == "Drag") return ParticleModuleType::Drag;
    if (text == "Noise") return ParticleModuleType::Noise;
    if (text == "ColorOverLife") return ParticleModuleType::ColorOverLife;
    if (text == "SizeOverLife") return ParticleModuleType::SizeOverLife;
    if (text == "Attractor") return ParticleModuleType::Attractor;
    if (text == "SubEmitter") return ParticleModuleType::SubEmitter;
    return ParticleModuleType::SpawnRate;
}

const char* toString(ParticleSystemNode::EffectClass effectClass) {
    using Effect = ParticleSystemNode::EffectClass;
    switch (effectClass) {
        case Effect::Fire: return "Fire";
        case Effect::Magic: return "Magic";
        case Effect::Rain: return "Rain";
        case Effect::Snow: return "Snow";
        case Effect::Smoke: return "Smoke";
        case Effect::Explosion: return "Explosion";
        case Effect::Simple:
        default: return "Simple";
    }
}

ParticleSystemNode::EffectClass particleEffectClassFromString(const std::string& text) {
    using Effect = ParticleSystemNode::EffectClass;
    if (text == "Fire") return Effect::Fire;
    if (text == "Magic") return Effect::Magic;
    if (text == "Rain") return Effect::Rain;
    if (text == "Snow") return Effect::Snow;
    if (text == "Smoke") return Effect::Smoke;
    if (text == "Explosion") return Effect::Explosion;
    return Effect::Simple;
}

const char* toString(ParticleSystemNode::BlendMode blendMode) {
    return blendMode == ParticleSystemNode::BlendMode::Alpha ? "Alpha" : "Additive";
}

ParticleSystemNode::BlendMode particleBlendModeFromString(const std::string& text) {
    if (text == "Alpha") return ParticleSystemNode::BlendMode::Alpha;
    return ParticleSystemNode::BlendMode::Additive;
}

ParticleEffect ParticleEffect::fromPreset(ParticleSystemNode::EffectClass effectClass) {
    ParticleEffect effect;
    effect.name = presetName(effectClass);
    effect.emitters.push_back(emitterFromPreset(ParticlePresetLibrary::presetFor(effectClass)));
    return effect;
}

ParticleEffect ParticleEffect::fromJson(const json& j) {
    ParticleEffect effect;
    effect.name = j.value("name", "Effect");
    effect.version = j.value("version", kCurrentVersion);
    if (auto it = j.find("emitters"); it != j.end() && it->is_array()) {
        for (const json& e : *it) effect.emitters.push_back(emitterFromJson(e));
    }
    return effect;
}

ParticleEffect ParticleEffect::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("failed to open particle effect: " + path);
    json j;
    file >> j;
    return fromJson(j);
}

json ParticleEffect::toJson() const {
    json emitterArray = json::array();
    for (const ParticleEmitterDesc& e : emitters) emitterArray.push_back(emitterToJson(e));
    return {
        {"format", "NEFX"},
        {"version", version},
        {"name", name},
        {"emitters", std::move(emitterArray)}
    };
}

bool ParticleEffect::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) return false;
    file << toJson().dump(4);
    return true;
}

} // namespace ne
