#include "render/RenderFeatureRegistry.hpp"

// The ONE place that knows the concrete built-in effects. The Renderer does not.
#include "render/features/WaterFeature.hpp"
#include "render/features/SkyboxFeature.hpp"
#include "render/features/DebugLinesFeature.hpp"
#include "render/features/OutlineFeature.hpp"
#include "render/features/ParticleFeature.hpp"

#include <algorithm>

namespace ne {

RenderFeatureRegistry& RenderFeatureRegistry::instance() {
    static RenderFeatureRegistry registry;
    return registry;
}

void RenderFeatureRegistry::add(int order, Factory factory) {
    entries_.push_back({order, std::move(factory)});
}

std::vector<std::unique_ptr<ScenePassFeature>> RenderFeatureRegistry::build() const {
    std::vector<Entry> sorted = entries_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Entry& a, const Entry& b) { return a.order < b.order; });
    std::vector<std::unique_ptr<ScenePassFeature>> out;
    out.reserve(sorted.size());
    for (const auto& e : sorted) out.push_back(e.factory());
    return out;
}

void registerBuiltinRenderFeatures() {
    static bool done = false;
    if (done) return;  // idempotent (the Engine may be constructed more than once)
    done = true;

    auto& r = RenderFeatureRegistry::instance();
    r.add(100, [] { return std::make_unique<WaterFeature>(); });      // before sky: writes depth
    r.add(150, [] { return std::make_unique<OutlineFeature>(); });    // after opaques/water, before sky
    r.add(200, [] { return std::make_unique<SkyboxFeature>(); });     // fills the far plane
    r.add(250, [] { return std::make_unique<ParticleFeature>(); });   // transparent HDR FX
    r.add(300, [] { return std::make_unique<DebugLinesFeature>(); }); // overlay, no depth
}

} // namespace ne
