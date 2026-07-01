#pragma once

#include <cstdint>

// Backend-neutral fixed-function pipeline state enums (Étape 16.3.d). Consumed
// by the backend Pipeline's neutral Desc; backends map them to their own enums.

namespace saida::rhi {

enum class CompareOp : uint32_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always,
};

enum class CullMode : uint32_t {
    None,
    Front,
    Back,
};

enum class Topology : uint32_t {
    TriangleList,
    TriangleStrip,
    LineList,
    PointList,
};

// Moved here from graphics/Pipeline.hpp (aliased back into saida:: there) so the
// WebGPU backend's pipeline desc can share it.
enum class BlendMode : uint32_t {
    None,
    Alpha,
    Additive,
};

} // namespace saida::rhi
