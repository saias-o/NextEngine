#include "authoring/EngineManifest.hpp"

#include "authoring/SaidaOp.hpp"
#include "core/Reflection.hpp"
#include "scene/LightNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/WaterNode.hpp"

namespace saida::authoring {
namespace {

template <typename T>
nlohmann::json reflectedNodeManifest() {
    nlohmann::json out = reflect::localDesc<T>().manifest();
    out["name"] = T::reflectName();
    out["category"] = "node";
    return out;
}

} // namespace

nlohmann::json buildEngineManifest() {
    nlohmann::json m;
    m["engineVersion"] = kEngineVersion;
    m["opVersion"] = kOpVersion;

    // Types de nodes que l'applier sait instancier/editer au stade du spike.
    m["nodes"] = nlohmann::json::array({
        nlohmann::json{{"name", "Node"}, {"category", "node"}},
        nlohmann::json{{"name", "MeshNode"}, {"category", "node"}},
        reflectedNodeManifest<LightNode>(),
        reflectedNodeManifest<WaterNode>(),
        reflectedNodeManifest<ParticleSystemNode>(),
    });

    // Ops du contrat SaidaOp — source unique de verite : le registre (Phase A1).
    m["ops"] = knownOpTypes();

    // set_property "allege" : proprietes cablees a la main (pas encore la vraie
    // reflexion — c'est la question ouverte du palier S2).
    m["properties"] = {
        {"*", nlohmann::json::array({"name", "enabled"})},
        {"LightNode", reflect::localDesc<LightNode>().manifest().value("properties", nlohmann::json::array())},
        {"Water", reflect::localDesc<WaterNode>().manifest().value("properties", nlohmann::json::array())},
        {"ParticleSystem", reflect::localDesc<ParticleSystemNode>().manifest().value("properties", nlohmann::json::array())},
    };

    return m;
}

} // namespace saida::authoring
