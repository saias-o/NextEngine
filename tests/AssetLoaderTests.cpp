#include "project/AssetLoader.hpp"
#include "project/AssetRegistry.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace {

bool waitForTerminal(saida::AssetHandle& handle) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (handle.state() == saida::AssetLoadState::Queued ||
           handle.state() == saida::AssetLoadState::Loading) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::yield();
    }
    return true;
}

// La comptabilité mémoire est libérée par le destructeur d'Entry ; le worker
// peut garder sa référence un court instant après l'état terminal, donc on
// polle au lieu d'asserter instantanément.
bool waitForResidentZero(saida::AssetLoader& loader) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        loader.collectGarbage();
        if (loader.stats().residentBytes == 0) return true;
        std::this_thread::yield();
    }
    return false;
}

void writeBytes(const std::filesystem::path& path, size_t count, uint8_t value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    std::string bytes(count, static_cast<char>(value));
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "SaidaAssetLoaderTests";
    fs::remove_all(root);
    writeBytes(root / "assets/small.bin", 4096, 0x2a);
    writeBytes(root / "assets/too-large.bin", 32768, 0x7f);

    saida::AssetRegistry registry;
    assert(registry.load(root.string()));
    const saida::AssetID small = registry.registerAsset("assets/small.bin", saida::AssetType::Unknown);
    const saida::AssetID large = registry.registerAsset("assets/too-large.bin", saida::AssetType::Unknown);
    const saida::AssetID missing = registry.registerAsset("assets/missing.bin", saida::AssetType::Unknown);

    saida::AssetLoader loader(&registry, 8192);
    const auto requestStart = std::chrono::steady_clock::now();
    auto handle = loader.request(small, saida::AssetLoadPriority::Critical);
    const auto requestTime = std::chrono::steady_clock::now() - requestStart;
    assert(requestTime < std::chrono::milliseconds(50));
    assert(waitForTerminal(handle));
    assert(handle.ready());
    assert(handle.bytes().size() == 4096);
    assert(handle.bytes().front() == 0x2a);

    auto shared = loader.request(small);
    assert(shared.ready());
    assert(shared.id() == handle.id());
    assert(handle.referenceCount() >= 2);

    auto overBudget = loader.request(large);
    assert(waitForTerminal(overBudget));
    assert(overBudget.failed());
    assert(overBudget.error().find("budget") != std::string::npos);

    auto absent = loader.request(missing);
    assert(waitForTerminal(absent));
    assert(absent.failed());
    assert(absent.error().find("not found") != std::string::npos);

    handle.reset();
    shared.reset();
    overBudget.reset();
    absent.reset();
    assert(waitForResidentZero(loader));

    for (int i = 0; i < 32; ++i) {
        auto cycle = loader.request(small);
        assert(waitForTerminal(cycle));
        assert(cycle.ready());
        cycle.reset();
        assert(waitForResidentZero(loader));
    }

    fs::remove_all(root);
    return 0;
}
