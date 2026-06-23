#pragma once

namespace ne {

// Registers every reflection-described behaviour and node into both the global
// TypeRegistry (the manifest the LLM reads) and the Behaviour/Node factory
// registries (used by the scene serializer). Called once at Engine startup.
//
// This is the single place new reflected types are listed — the MCP
// `write_cpp_behaviour` tool appends here so adding a behaviour never requires
// editing Engine.cpp. Keeping one central file also sidesteps the static-library
// dead-stripping of per-TU self-registration on this toolchain.
void registerReflectedTypes();

} // namespace ne
