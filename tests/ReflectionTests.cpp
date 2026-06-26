// Locks in the M1 reflection foundation: manifest generation, auto save/load
// round-trip, and the named signal/slot descriptors used by the M2 wiring layer.

#include "core/Reflection.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/ReflectedTypes.hpp"
#include "scene/RotatorBehaviour.hpp"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

using nlohmann::json;

int main() {
    ne::registerReflectedTypes();

    auto& reg = ne::reflect::TypeRegistry::instance();

    // ── manifest: Rotator is present with the expected shape ──
    const ne::reflect::TypeDesc* rotor = reg.find("Rotator");
    assert(rotor && "Rotator must be registered");
    assert(rotor->category == "behaviour");
    assert(rotor->findProperty("axis") && rotor->findProperty("axis")->kind == "vec3");
    assert(rotor->findProperty("speed") && rotor->findProperty("speed")->kind == "float");
    assert(rotor->findProperty("speed")->hasRange);
    assert(rotor->findSignal("fullRotation") && rotor->findSignal("fullRotation")->arity == 0);
    assert(rotor->findSlot("reset"));

    // Enum property surfaces labels (LightNode.bakeMode / lightType).
    const ne::reflect::TypeDesc* light = reg.find("LightNode");
    assert(light && light->category == "node");
    const auto* lt = light->findProperty("lightType");
    assert(lt && lt->kind == "enum" && lt->enumLabels.size() == 3);

    const ne::reflect::TypeDesc* particles = reg.find("ParticleSystem");
    assert(particles && particles->category == "node");
    assert(particles->findProperty("effectClass") &&
           particles->findProperty("effectClass")->enumLabels.size() == 7);
    assert(particles->findProperty("blendMode") &&
           particles->findProperty("blendMode")->enumLabels.size() == 2);
    assert(particles->findProperty("maxParticles") &&
           particles->findProperty("maxParticles")->kind == "int");
    assert(particles->findSignal("finished") && particles->findSignal("finished")->arity == 0);
    assert(particles->findSlot("play"));
    assert(particles->findSlot("stop"));
    assert(particles->findSlot("burst"));
    assert(ne::NodeRegistry::instance().create("ParticleSystem") != nullptr);

    // Manifest JSON is well-formed and category-filterable.
    json full = reg.manifest();
    assert(full.contains("behaviours") && full.contains("nodes"));
    json onlyNodes = reg.manifest("node");
    assert(onlyNodes.contains("nodes") && !onlyNodes.contains("behaviours"));

    // ── auto save/load round-trip via reflection ──
    ne::RotatorBehaviour src;
    src.axis = {0.0f, 0.0f, 1.0f};
    src.speed = 123.5f;
    json saved;
    src.save(saved);
    assert(saved["speed"].get<float>() == 123.5f);
    assert(saved["axis"][2].get<float>() == 1.0f);

    ne::RotatorBehaviour dst;
    dst.load(saved);
    assert(dst.speed == 123.5f);
    assert(dst.axis.z == 1.0f);

    // Missing keys keep defaults (defensive load).
    ne::RotatorBehaviour partial;
    partial.load(json{{"speed", 42.0f}});  // no "axis"
    assert(partial.speed == 42.0f);
    assert(partial.axis.y == 1.0f);  // default preserved

    // ── named signal descriptor wires through to the live Signal<> ──
    int pulses = 0;
    {
        auto conn = rotor->findSignal("fullRotation")->connect(
            &src, [&](const json&) { ++pulses; });
        src.fullRotation.emit();
        src.fullRotation.emit();
    }
    assert(pulses == 2);
    src.fullRotation.emit();  // connection dropped → no more pulses
    assert(pulses == 2);

    // ── slot descriptor invokes the method (no node attached: must not crash) ──
    rotor->findSlot("reset")->invoke(&src, json::array());

    ne::ParticleSystemNode ps;
    ps.maxParticles = 777;
    ps.effectClass = ne::ParticleSystemNode::EffectClass::Magic;
    json particleSaved;
    particles->saveTo(&ps, particleSaved);
    assert(particleSaved["maxParticles"].get<int>() == 777);
    assert(particleSaved["effectClass"].get<int>() ==
           static_cast<int>(ne::ParticleSystemNode::EffectClass::Magic));

    ne::ParticleSystemNode loadedPs;
    particles->loadFrom(&loadedPs, particleSaved);
    assert(loadedPs.maxParticles == 777);
    assert(loadedPs.effectClass == ne::ParticleSystemNode::EffectClass::Magic);

    bool particleFinished = false;
    {
        auto conn = particles->findSignal("finished")->connect(
            &ps, [&](const json&) { particleFinished = true; });
        ps.finished.emit();
    }
    assert(particleFinished);
    particles->findSlot("stop")->invoke(&ps, json::array());
    assert(!ps.playing);
    particles->findSlot("play")->invoke(&ps, json::array());
    assert(ps.playing);

    return 0;
}
