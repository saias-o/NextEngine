// Player-web implementation of registerReflectedTypes().
//
// Le player web (web/player) exécute le vrai cycle de jeu — SceneTree,
// behaviours, signaux — mais son build v1 exclut volontairement physique/Jolt,
// audio, QuickJS et RmlUi (PlatformCaps les déclare absents et les scènes qui
// les référencent obtiennent un diagnostic explicite au chargement, jamais un
// échec silencieux). On enregistre donc les nœuds rendables et les behaviours
// gameplay portables uniquement.
//
// Emscripten-only : ne collisionne jamais avec ReflectedTypes.cpp (desktop) ni
// ReflectedTypesWeb.cpp (viewer d'authoring, autre exécutable).
#ifdef __EMSCRIPTEN__

#include "scene/ReflectedTypes.hpp"

#include "core/Reflection.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/NodeRegistry.hpp"

#include "scene/Blackboard.hpp"
#include "scene/LightNode.hpp"
#include "scene/ParticleSystemNode.hpp"
#include "scene/RotatorBehaviour.hpp"
#include "scene/SpawnerBehaviour.hpp"
#include "scene/StateMachineBehaviour.hpp"
#include "scene/WaterNode.hpp"

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

    registerNode<LightNode>();
    registerNode<WaterNode>();
    registerNode<ParticleSystemNode>();
}

} // namespace saida

#endif // __EMSCRIPTEN__
