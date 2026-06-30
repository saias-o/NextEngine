#include "core/FormatVersions.hpp"
#include "project/AssetRegistry.hpp"
#include "project/Project.hpp"
#include "scenario/ScenarioAsset.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using json = nlohmann::json;

namespace {

void require(bool condition) {
    if (!condition) std::abort();
}

std::filesystem::path testRoot() {
    auto root = std::filesystem::temp_directory_path() / "SaidaEngineFormatVersionTests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    return root;
}

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path);
    file << text;
}

json readJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    json doc;
    file >> doc;
    return doc;
}

json baseScenario() {
    return {
        {"version", saida::format::kScenarioVersion},
        {"id", "format.test"},
        {"roles", json::object()},
        {"blackboard", json::object()},
        {"steps", json::array({{{"id", "start"}, {"end", "success"}}})}
    };
}

void testLegacyProjectMigratesToJson() {
    const auto root = testRoot() / "LegacyProject";
    std::filesystem::create_directories(root);
    const auto path = root / "LegacyProject.saidaproj";
    writeText(path,
        "[SaidaEngine Project]\n"
        "name=LegacyProject\n"
        "engine_version=0.1.0\n"
        "main_scene=scenes/main.scene\n"
        "max_fps=144\n"
        "vsync=1\n"
        "shadow_resolution=4096\n"
        "shadow_dist=80\n"
        "shadow_soft=2.5\n"
        "audio_master_vol=0.75\n"
        "audio_default_vol=0.5\n"
        "audio_default_loop=1\n"
        "audio_default_spatial=1\n"
        "audio_default_min_dist=2\n"
        "audio_default_max_dist=50\n"
        "auto_mesh_lods=1\n"
        "show_colliders=0\n"
        "audio_alias_click=assets/audio/click.ogg\n"
        "autoload_GameState=scripts/game_state.mjs\n");

    saida::Project project;
    require(project.load(path.string()));
    require(project.name() == "LegacyProject");
    require(project.mainScene() == "scenes/main.scene");
    require(project.maxFps() == 144);
    require(project.vSync());
    require(project.shadowResolution() == 4096);
    require(project.autoMeshLods());
    require(!project.showColliders());
    require(project.audioAliases().at("click") == "assets/audio/click.ogg");
    require(project.autoloads().at("GameState") == "scripts/game_state.mjs");

    require(project.save());
    json saved = readJson(path);
    require(saved["version"] == saida::format::kProjectVersion);
    require(saved["runtime"]["maxFps"] == 144);
    require(saved["runtime"]["vsync"] == true);
    require(saved["rendering"]["showColliders"] == false);
    require(saved["audio"]["aliases"]["click"] == "assets/audio/click.ogg");
    require(saved["autoloads"]["GameState"] == "scripts/game_state.mjs");
}

void testFutureProjectLoadsBestEffort() {
    const auto root = testRoot() / "FutureProject";
    std::filesystem::create_directories(root);
    const auto path = root / "FutureProject.saidaproj";
    writeText(path, json{
        {"version", 99},
        {"name", "FutureProject"},
        {"engineVersion", "9.9.9"},
        {"runtime", {{"maxFps", 30}, {"vsync", true}}},
        {"unknownFutureBlock", {{"keptByFutureEngine", true}}}
    }.dump(2));

    saida::Project project;
    require(project.load(path.string()));
    require(project.name() == "FutureProject");
    require(project.engineVersion() == "9.9.9");
    require(project.maxFps() == 30);
    require(project.vSync());
}

void testAssetRegistryMigratesToEnvelope() {
    const auto root = testRoot() / "RegistryProject";
    std::filesystem::create_directories(root);
    const auto path = root / "asset_registry.json";
    writeText(path, json{
        {"123", {{"path", "scenes/main.scene"}, {"hash", 42}, {"type", "Scene"}}}
    }.dump(2));

    saida::AssetRegistry registry;
    require(registry.load(root.string()));
    require(registry.getID("scenes/main.scene") == 123);
    require(registry.getPath(123) == "scenes/main.scene");
    require(registry.save(root.string()));

    json saved = readJson(path);
    require(saved["version"] == saida::format::kAssetRegistryVersion);
    require(saved["assets"]["123"]["path"] == "scenes/main.scene");
}

void testFutureAssetRegistryIsRejected() {
    const auto root = testRoot() / "FutureRegistryProject";
    std::filesystem::create_directories(root);
    writeText(root / "asset_registry.json", json{
        {"version", 99},
        {"assets", json::object()}
    }.dump(2));

    saida::AssetRegistry registry;
    require(!registry.load(root.string()));
}

void testScenarioVersioning() {
    saida::ScenarioAsset asset;
    std::vector<saida::ScenarioIssue> issues;

    json missing = baseScenario();
    missing.erase("version");
    require(saida::ScenarioAsset::parse(missing, asset, &issues));
    require(asset.version == saida::format::kScenarioVersion);
    require(asset.toJson()["version"] == saida::format::kScenarioVersion);

    json future = baseScenario();
    future["version"] = 99;
    issues.clear();
    require(!saida::ScenarioAsset::parse(future, asset, &issues));
    require(!issues.empty());
}

} // namespace

int main() {
    testLegacyProjectMigratesToJson();
    testFutureProjectLoadsBestEffort();
    testAssetRegistryMigratesToEnvelope();
    testFutureAssetRegistryIsRejected();
    testScenarioVersioning();
    return 0;
}
