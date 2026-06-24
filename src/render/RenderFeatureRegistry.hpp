#pragma once

#include "render/RenderFeature.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace ne {

// Decouples the Renderer from concrete effects — the same idea as Godot's
// CompositorEffect list or Unity's ScriptableRendererFeature list. Effects register
// a factory + a draw order here; the Renderer only ever sees the ScenePassFeature
// interface and NEVER includes or names a concrete effect. Adding an effect is a new
// file + one registration line; the Renderer never changes.
class RenderFeatureRegistry {
public:
    using Factory = std::function<std::unique_ptr<ScenePassFeature>()>;

    static RenderFeatureRegistry& instance();

    // `order` is the draw order inside the HDR scene pass (lower draws first).
    void add(int order, Factory factory);

    // Fresh instances of every registered feature, sorted by order. Called once per
    // Renderer (the desktop and XR renderers each get their own independent set).
    std::vector<std::unique_ptr<ScenePassFeature>> build() const;

private:
    struct Entry { int order; Factory factory; };
    std::vector<Entry> entries_;
};

// Registers the engine's built-in effects (water, skybox, debug lines). Called once
// at startup, before any Renderer is built. Central registration (rather than static
// self-registration) avoids the static-library dead-strip problem — the same reason
// scene/ReflectedTypes.cpp registers node/behaviour types centrally.
void registerBuiltinRenderFeatures();

} // namespace ne
