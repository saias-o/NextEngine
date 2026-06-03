#pragma once

namespace ne {

class Scene;
class ResourceManager;

// Populates a scene with the demo content (orbiting textured cubes + lights).
// This is *game* code: it lives in the executable, not the engine library, so
// changing it doesn't recompile the engine. The Engine calls it once at startup.
void buildDemoScene(Scene& scene, ResourceManager& resources);

} // namespace ne
