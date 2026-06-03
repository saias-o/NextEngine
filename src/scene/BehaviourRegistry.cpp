#include "scene/BehaviourRegistry.hpp"

#include "scene/Behaviour.hpp"

namespace ne {

BehaviourRegistry& BehaviourRegistry::instance() {
    static BehaviourRegistry registry;
    return registry;
}

void BehaviourRegistry::registerType(const std::string& typeName, Factory factory) {
    factories_[typeName] = std::move(factory);
}

std::unique_ptr<Behaviour> BehaviourRegistry::create(const std::string& typeName) const {
    auto it = factories_.find(typeName);
    return it != factories_.end() ? it->second() : nullptr;
}

} // namespace ne
