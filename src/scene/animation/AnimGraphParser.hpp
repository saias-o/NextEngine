#pragma once

#include "scene/animation/AnimStateMachine.hpp"
#include <memory>
#include <string>
#include <string_view>

namespace ne {

class ResourceManager;
class Rig;

// Parses a .animgraph file and generates an AnimStateMachine.
class AnimGraphParser {
public:
    static std::unique_ptr<AnimStateMachine> parse(std::string_view source, ResourceManager& resources, const Rig& rig);
};

} // namespace ne
