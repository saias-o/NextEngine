#include "Hub.hpp"
#include "core/Log.hpp"

#include <cstdlib>
#include <exception>

int main() {
    try {
        ne::Hub hub;
        hub.run();
    } catch (const std::exception& e) {
        ne::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
