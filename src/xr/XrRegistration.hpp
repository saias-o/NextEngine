#pragma once

namespace ne::xr {

// Registers all XR Toolkit node types and behaviours with the engine registries.
// This decouples the core ne::Engine from the specific VR/AR toolkit types.
void registerTypes();

} // namespace ne::xr
