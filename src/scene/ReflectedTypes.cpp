#include "scene/ReflectedTypes.hpp"

#include "core/Reflection.hpp"
#include "audio/AudioSourceBehaviour.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/NodeRegistry.hpp"

// Reflected types (each declares a static describe() and the SAIDA_REFLECT_* macro).
#include "scene/Blackboard.hpp"
#include "scene/CameraFollowBehaviour.hpp"
#include "scene/CharacterBehaviour.hpp"
#include "scene/HealthBehaviour.hpp"
#include "scene/LightNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/WaterNode.hpp"
#include "scene/RotatorBehaviour.hpp"
#include "scene/SpawnerBehaviour.hpp"
#include "scene/StateMachineBehaviour.hpp"
#include "physics/AreaNode.hpp"
#include "scenario/ScenarioAnchor.hpp"
#include "scenario/ScenarioDirector.hpp"
#include "scenario/ScenarioRunnerBehaviour.hpp"
#include "generated/VehicleBehaviour.hpp"
#include "generated/NpcWanderBehaviour.hpp"
#include "generated/GunBehaviour.hpp"
// <<SAIDA_MCP_INCLUDES>>  (write_cpp_behaviour inserts generated #includes above this line)

namespace saida {
namespace {

// Copy the reflected descriptor into the global registry under its public name,
// tag the category, and wire the matching factory.
template <typename T>
void registerBehaviour() {
    reflect::TypeDesc& d = reflect::TypeRegistry::instance().add(T::reflectName());
    d = reflect::localDesc<T>();
    d.name = T::reflectName();
    d.category = "behaviour";
    BehaviourRegistry::instance().registerType<T>(T::reflectName());
}

template <typename T>
void registerNode() {
    reflect::TypeDesc& d = reflect::TypeRegistry::instance().add(T::reflectName());
    d = reflect::localDesc<T>();
    d.name = T::reflectName();
    d.category = "node";
    NodeRegistry::instance().registerType<T>(T::reflectName());
}

} // namespace

void registerReflectedTypes() {
    static bool registered = false;
    if (registered) return;
    registered = true;

    registerBehaviour<RotatorBehaviour>();
    registerBehaviour<AudioSourceBehaviour>();
    registerBehaviour<CameraFollowBehaviour>();
    registerBehaviour<CharacterBehaviour>();
    registerBehaviour<HealthBehaviour>();
    registerBehaviour<SpawnerBehaviour>();
    registerBehaviour<Blackboard>();
    registerBehaviour<StateMachineBehaviour>();
    registerBehaviour<ScenarioAnchor>();
    registerBehaviour<ScenarioDirector>();
    registerBehaviour<ScenarioRunnerBehaviour>();
    // GTA-clone gameplay (drivable cars + wandering NPCs).
    registerBehaviour<VehicleBehaviour>();
    registerBehaviour<NpcWanderBehaviour>();
    registerBehaviour<GunBehaviour>();

    registerNode<LightNode>();
    registerNode<WaterNode>();
    registerNode<ParticleSystemNode>();
    registerNode<AreaNode>();
    // <<SAIDA_MCP_REGISTER>>  (write_cpp_behaviour inserts registerBehaviour<T>() calls above this line)
}

} // namespace saida
