#include "scene/ReflectedTypes.hpp"

#include "core/Reflection.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/NodeRegistry.hpp"

// Reflected types (each declares a static describe() and the NE_REFLECT_* macro).
#include "scene/CharacterBehaviour.hpp"
#include "scene/LightNode.hpp"
#include "scene/RotatorBehaviour.hpp"
// <<NE_MCP_INCLUDES>>  (write_cpp_behaviour inserts generated #includes above this line)

namespace ne {
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
    // ── behaviours ──
    registerBehaviour<RotatorBehaviour>();
    registerBehaviour<CharacterBehaviour>();

    // ── nodes ──
    registerNode<LightNode>();
    // <<NE_MCP_REGISTER>>  (write_cpp_behaviour inserts registerBehaviour<T>() calls above this line)
}

} // namespace ne
