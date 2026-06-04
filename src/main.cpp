#include "Engine.hpp"
#include "core/Log.hpp"
#include "editor/EditorApp.hpp"
#include "game/DemoScene.hpp"

#include <cstdlib>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    try {
        std::string initialProject;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--project" && i + 1 < argc)
                initialProject = argv[++i];
        }

        // The game provides the scene; the editor app drives the engine.
        ne::Engine engine(ne::buildDemoScene, initialProject);
        ne::EditorApp editor(engine);
        engine.setOnFrame([&editor](float dt) { editor.update(dt); });
        engine.run();
    } catch (const std::exception& e) {
        ne::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
