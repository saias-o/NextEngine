// Corpus de rétrocompatibilité.
//
// Charge à chaque build les fixtures *figés* de tests/fixtures/compat/ avec les
// vrais chargeurs du moteur. Deux garanties : un ancien document se charge
// toujours (ou échoue avec un diagnostic, jamais de corruption silencieuse),
// et un simple chargement ne réécrit jamais le fichier source.

#include "project/AssetRegistry.hpp"
#include "project/Project.hpp"
#include "runtime/BootManifest.hpp"
#include "scenario/ScenarioAsset.hpp"
#include "scene/SceneSerializer.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "compat corpus FAILED: %s\n", what);
        std::abort();
    }
}

fs::path corpusDir() { return fs::path(SAIDA_COMPAT_CORPUS_DIR); }

std::string readAll(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream out;
    out << file.rdbuf();
    return out.str();
}

// Garde anti-réécriture : capture les octets du fixture à la construction,
// vérifie qu'ils sont identiques à la destruction.
class FrozenFile {
public:
    explicit FrozenFile(fs::path path)
        : path_(std::move(path)), before_(readAll(path_)) {
        require(!before_.empty(), "fixture file must exist and be non-empty");
    }
    ~FrozenFile() {
        if (readAll(path_) != before_) {
            std::fprintf(stderr, "compat corpus FAILED: fixture rewritten: %s\n",
                         path_.string().c_str());
            std::abort();
        }
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
    std::string before_;
};

void testProjects() {
    // Project::load initialise un asset_registry dans la racine du projet :
    // on charge une copie temporaire pour ne jamais toucher au corpus.
    const fs::path tmp = fs::temp_directory_path() / "SaidaCompatCorpusTests";
    for (const char* name : {"project_v0.saidaproj", "project_v1.saidaproj"}) {
        FrozenFile frozen(corpusDir() / name);
        const fs::path root = tmp / fs::path(name).stem();
        fs::remove_all(root);
        fs::create_directories(root);
        fs::copy_file(frozen.path(), root / name);

        saida::Project project;
        require(project.load((root / name).string()), name);
        require(!project.name().empty(), "project name must survive migration");
        require(project.mainScene() == "scenes/main.scene",
                "project main scene must survive migration");
        require(project.autoloads().at("GameState") == "scripts/game_state.mjs",
                "project autoloads must survive migration");
    }
    fs::remove_all(tmp);
}

void testAssetRegistries() {
    // AssetRegistry::load attend un fichier nommé asset_registry.json à la
    // racine d'un projet : on copie le fixture dans un dossier temporaire.
    const fs::path tmp = fs::temp_directory_path() / "SaidaCompatCorpusTests";
    for (const char* name : {"asset_registry_v0.json", "asset_registry_v1.json"}) {
        FrozenFile frozen(corpusDir() / name);
        const fs::path root = tmp / fs::path(name).stem();
        fs::remove_all(root);
        fs::create_directories(root);
        fs::copy_file(frozen.path(), root / "asset_registry.json");

        saida::AssetRegistry registry;
        require(registry.load(root.string()), name);
        require(registry.getID("scenes/main.scene") != saida::kAssetInvalid,
                "registry asset ids must survive migration");
        require(registry.getID("assets/textures/checker.png") != saida::kAssetInvalid,
                "registry paths must survive migration");
    }
    fs::remove_all(tmp);
}

void testScenes() {
    for (const char* name : {"scene_v0.scene", "scene_v2.scene"}) {
        FrozenFile frozen(corpusDir() / name);
        require(saida::SceneSerializer::validateSceneDocumentFile(frozen.path().string()),
                name);
    }
}

void testScenarios() {
    for (const char* name : {"scenario_v0.saidascenario", "scenario_v1.saidascenario"}) {
        FrozenFile frozen(corpusDir() / name);
        std::ifstream file(frozen.path());
        nlohmann::json doc = nlohmann::json::parse(file);
        saida::ScenarioAsset asset;
        std::vector<saida::ScenarioIssue> issues;
        require(saida::ScenarioAsset::parse(doc, asset, &issues), name);
        require(!asset.id.empty(), "scenario id must survive migration");
    }
}

void testBootManifests() {
    for (const char* name : {"game_v0.saida", "game_v1.saida"}) {
        FrozenFile frozen(corpusDir() / name);
        const auto result = saida::loadBootManifest(frozen.path().string());
        require(result.ok, name);
        require(result.manifest.project == "CompatCorpus.saidaproj",
                "boot manifest project must survive migration");
        require(result.manifest.mainScene == "scenes/main.scene",
                "boot manifest scene must survive migration");
    }
}

// WitnessGame gelé : les artefacts durables réels du jeu témoin V1. Toute
// dérive de format qui empêcherait de recharger le jeu livré est capturée ici,
// à côté des fixtures synthétiques v0/v1.
void testWitnessGame() {
    const fs::path tmp = fs::temp_directory_path() / "SaidaCompatCorpusTests";

    // Projet : Project::load initialise un asset_registry dans la racine, on
    // charge une copie temporaire pour ne jamais toucher au corpus.
    {
        FrozenFile frozen(corpusDir() / "witness_v1.saidaproj");
        const fs::path root = tmp / "witness_project";
        fs::remove_all(root);
        fs::create_directories(root);
        fs::copy_file(frozen.path(), root / "witness_v1.saidaproj");

        saida::Project project;
        require(project.load((root / "witness_v1.saidaproj").string()),
                "witness_v1.saidaproj");
        require(project.name() == "Witness Game", "witness project name must survive");
        require(project.mainScene() == "scenes/hub.scene",
                "witness main scene must survive");
        require(project.autoloads().at("GameState") == "scripts/game_state.mjs",
                "witness autoloads must survive");
        require(project.audioAliases().at("pickup") == "assets/audio/pickup.ogg",
                "witness audio aliases must survive");
    }

    // Registre d'assets : copié en asset_registry.json dans un dossier temporaire.
    {
        FrozenFile frozen(corpusDir() / "witness_v1_asset_registry.json");
        const fs::path root = tmp / "witness_registry";
        fs::remove_all(root);
        fs::create_directories(root);
        fs::copy_file(frozen.path(), root / "asset_registry.json");

        saida::AssetRegistry registry;
        require(registry.load(root.string()), "witness_v1_asset_registry.json");
        require(registry.getID("scenes/hub.scene") != saida::kAssetInvalid,
                "witness hub scene id must survive");
        require(registry.getID("assets/models/totem.gltf") != saida::kAssetInvalid,
                "witness totem mesh id must survive");
    }
    fs::remove_all(tmp);

    // Scènes : validation headless du HUD UI, de la physique et des types V1.
    for (const char* name : {"witness_v1_hub.scene", "witness_v1_arena.scene"}) {
        FrozenFile frozen(corpusDir() / name);
        require(saida::SceneSerializer::validateSceneDocumentFile(frozen.path().string()),
                name);
    }
}

} // namespace

int main() {
    testProjects();
    testAssetRegistries();
    testScenes();
    testScenarios();
    testBootManifests();
    testWitnessGame();
    return 0;
}
