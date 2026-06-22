#pragma once

#include <cstdint>

namespace ne {

using NodeId = uint64_t;
constexpr NodeId kNodeInvalid = 0;

NodeId generateNodeId();

} // namespace ne
