#include "Hub.hpp"
#include "core/Log.hpp"

#include <cstdlib>
#include <exception>

int main() {
    try {
        saida::Hub hub;
        hub.run();
    } catch (const std::exception& e) {
        saida::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
