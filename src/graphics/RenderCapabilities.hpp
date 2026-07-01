#pragma once

// Compatibility shim: RenderCapabilities is now the backend-neutral
// rhi::Capabilities (Étape 16.3). New code should include "rhi/Capabilities.hpp"
// and use saida::rhi::Capabilities directly.
#include "rhi/Capabilities.hpp"

namespace saida {
using RenderCapabilities = rhi::Capabilities;
}
