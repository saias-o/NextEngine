#pragma once

#include "scene/animation/AnimStateMachine.hpp"
#include <memory>
#include <string_view>
#include <unordered_map>

namespace saida {

class Rig;
class AnimationClip;

// Parses a .animgraph source into an AnimStateMachine. Clips are resolved by name
// from `clips` (e.g. an Animator's loaded clip library) — the parser never owns or
// fabricates clip data.
class AnimGraphParser {
public:
    static std::unique_ptr<AnimStateMachine> parse(
        std::string_view source,
        const std::unordered_map<std::string, const AnimationClip*>& clips,
        const Rig& rig);
};

} // namespace saida
