#include "Engine.hpp"
#include "core/Log.hpp"
#include "game/DemoScene.hpp"

#include <cstdlib>
#include <exception>

int main() {
    try {
        // The game provides the scene; the engine library has no built-in content.
        ne::Engine engine(ne::buildDemoScene);
        engine.run();
    } catch (const std::exception& e) {
        ne::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
