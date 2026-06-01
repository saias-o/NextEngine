#include "Engine.hpp"
#include "core/Log.hpp"

#include <cstdlib>
#include <exception>

int main() {
    try {
        ne::Engine engine;
        engine.run();
    } catch (const std::exception& e) {
        ne::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
