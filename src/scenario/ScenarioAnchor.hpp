#pragma once

#include "core/Reflection.hpp"
#include "scene/Behaviour.hpp"

#include <string>

namespace ne {

class ScenarioAnchor : public Behaviour {
public:
    std::string key;

    NE_REFLECT_BEHAVIOUR(ScenarioAnchor, "ScenarioAnchor")
};

} // namespace ne
