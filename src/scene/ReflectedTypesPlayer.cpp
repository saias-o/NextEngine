// Player-web implementation of registerReflectedTypes().
//
// Le player web (web/player) exécute le vrai cycle de jeu — SceneTree,
// behaviours, signaux, scripts QuickJS, physique Jolt (job system mono-thread)
// et audio miniaudio (backend Web Audio). Seule l'UI reste absente (PlatformCaps
// la déclare absente ; les scènes qui l'exigent sont refusées au chargement).
//
// Emscripten-only : ne collisionne jamais avec ReflectedTypes.cpp (desktop) ni
// ReflectedTypesWeb.cpp (viewer d'authoring, autre exécutable).
#ifdef __EMSCRIPTEN__

#include "scene/ReflectedTypes.hpp"

#include "core/Reflection.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/NodeRegistry.hpp"

#include "audio/AudioSourceBehaviour.hpp"
#include "physics/AreaNode.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "physics/CollisionShapeNode.hpp"
#include "physics/RigidBodyNode.hpp"
#include "physics/StaticBodyNode.hpp"
#include "scene/Blackboard.hpp"
#include "scene/CameraFollowBehaviour.hpp"
#include "scene/CharacterBehaviour.hpp"
#include "scene/LightNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/RotatorBehaviour.hpp"
#include "scene/SpawnerBehaviour.hpp"
#include "scene/StateMachineBehaviour.hpp"
#include "scene/WaterNode.hpp"
#include "scene/animation/SequenceDirectorBehaviour.hpp"
#include "scripting/ScriptBehaviour.hpp"

namespace saida {
namespace {

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
    registerBehaviour<SpawnerBehaviour>();
    registerBehaviour<Blackboard>();
    registerBehaviour<StateMachineBehaviour>();
    registerBehaviour<SequenceDirectorBehaviour>();
    registerBehaviour<CharacterBehaviour>();
    registerBehaviour<CameraFollowBehaviour>();
    registerBehaviour<AudioSourceBehaviour>();
    BehaviourRegistry::instance().registerType<ScriptBehaviour>("ScriptBehaviour");

    registerNode<LightNode>();
    registerNode<WaterNode>();
    registerNode<ParticleSystemNode>();
    registerNode<AreaNode>();
    NodeRegistry::instance().registerType<CollisionShapeNode>("CollisionShape");
    NodeRegistry::instance().registerType<StaticBodyNode>("StaticBody");
    NodeRegistry::instance().registerType<RigidBodyNode>("RigidBody");
    NodeRegistry::instance().registerType<CharacterBodyNode>("CharacterBody");
}

} // namespace saida

#endif // __EMSCRIPTEN__
