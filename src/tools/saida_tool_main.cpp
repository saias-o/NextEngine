// saida_tool — CLI headless d'outillage moteur (Phase B du plan d'integration,
// PLAN_INTEGRATION_SAIDA.md). Pas de fenetre, pas de GPU : lie l'authoring-core
// (EngineManifest / SaidaOp) et sert de socle aux workers Temporal (describe,
// validate, apply-ops). Codes de sortie fiables : 0 = ok, non-zero = erreur.
//
// Sous-commandes :
//   describe-engine [--pretty]   Ecrit l'EngineManifest JSON sur stdout (B4).
//   validate-ops <file>          Valide statiquement des SaidaOps (B2).
//   validate-scene <file>        Valide la structure d'un snapshot de scene (B2).
//   validate-scenario <file>     Valide un asset scenario sans GPU (B2).
//   apply-ops <scene> <ops>      Applique des ops -> snapshot deterministe (B3).
//   help | --help | -h           Affiche l'aide.
//
// Convention : les diagnostics vont sur stderr, la sortie machine sur stdout.

#include "authoring/EngineManifest.hpp"
#include "authoring/SaidaOp.hpp"
#include "authoring/SaidaOpApplier.hpp"
#include "authoring/SceneSnapshot.hpp"
#include "scene/Scene.hpp"
#include "scenario/ScenarioAsset.hpp"

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace {

using json = nlohmann::json;

constexpr int kExitOk = 0;
constexpr int kExitInvalid = 1;  // input traite mais invalide (validation)
constexpr int kExitUsage = 2;    // mauvaise invocation / I/O

int usage(std::ostream& out) {
    out << "saida_tool — SaidaEngine headless tooling\n"
           "\n"
           "Usage:\n"
           "  saida_tool describe-engine [--pretty]\n"
           "  saida_tool validate-ops <ops.json> [--pretty]\n"
           "  saida_tool validate-scene <scene.json> [--pretty]\n"
           "  saida_tool validate-scenario <scenario.json> [--pretty]\n"
           "  saida_tool apply-ops <scene.json> <ops.json> [--out <file>]\n"
           "  saida_tool help\n"
           "\n"
           "Commands:\n"
           "  describe-engine   Print the EngineManifest as JSON (nodes, ops,\n"
           "                    reflected properties, contract versions).\n"
           "                    --pretty indents the output.\n"
           "  validate-ops      Statically validate a JSON array of SaidaOps\n"
           "                    (schema + per-type shape). Prints a JSON report;\n"
           "                    exit 0 if all valid, 1 if any invalid.\n"
           "  validate-scene    Structurally validate a scene snapshot (node types,\n"
           "                    unique ids, transform/children shape). Prints a JSON\n"
           "                    report; exit 0 if valid, 1 if any issue.\n"
           "  validate-scenario Statically validate a Saida scenario asset\n"
           "                    (format + registered actions/conditions). Prints a\n"
           "                    JSON report; exit 0 if valid, 1 if invalid.\n"
           "  apply-ops         Load a resource-free scene snapshot (or .saidaproj),\n"
           "                    apply a JSON array of SaidaOps in order, then emit a\n"
           "                    deterministic snapshot to --out (or stdout). All ops\n"
           "                    must apply cleanly (atomic); exit 1 if any is rejected\n"
           "                    and nothing is written.\n"
           "\n"
           "Files may be '-' for stdin. Exit codes: 0 ok, 1 invalid input, 2 usage/IO.\n";
    return kExitUsage;
}

// Lit un fichier entier ; '-' = stdin. Renvoie false + message sur echec.
bool readInput(const std::string& path, std::string& out, std::string& error) {
    if (path == "-") {
        std::ostringstream ss;
        ss << std::cin.rdbuf();
        out = ss.str();
        return true;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open '" + path + "'";
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

int cmdDescribeEngine(const std::vector<std::string>& args) {
    bool pretty = false;
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else {
            std::cerr << "describe-engine: unknown option '" << a << "'\n";
            return kExitUsage;
        }
    }
    const json manifest = saida::authoring::buildEngineManifest();
    std::cout << (pretty ? manifest.dump(2) : manifest.dump()) << "\n";
    return kExitOk;
}

int cmdValidateOps(const std::vector<std::string>& args) {
    std::string path;
    bool pretty = false;
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "validate-ops: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (path.empty()) {
            path = a;
        } else {
            std::cerr << "validate-ops: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (path.empty()) {
        std::cerr << "validate-ops: missing <ops.json> (use '-' for stdin)\n";
        return kExitUsage;
    }

    std::string text, ioError;
    if (!readInput(path, text, ioError)) {
        std::cerr << "validate-ops: " << ioError << "\n";
        return kExitUsage;
    }

    const json parsed = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        std::cerr << "validate-ops: input is not valid JSON\n";
        return kExitUsage;
    }
    // Accepte un tableau d'ops, ou une op unique (enveloppee en tableau).
    json ops = parsed.is_array() ? parsed : json::array({parsed});

    json errors = json::array();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const auto res = saida::authoring::parseSaidaOp(ops[i]);
        std::string error = res.ok ? saida::authoring::validateOpShape(res.op) : res.error;
        if (!error.empty())
            errors.push_back(json{{"index", i}, {"error", error}});
    }

    const bool allValid = errors.empty();
    json report{{"ok", allValid}, {"count", ops.size()},
                {"invalid", errors.size()}, {"errors", std::move(errors)}};
    std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    return allValid ? kExitOk : kExitInvalid;
}

int cmdValidateScenario(const std::vector<std::string>& args) {
    std::string path;
    bool pretty = false;
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "validate-scenario: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (path.empty()) {
            path = a;
        } else {
            std::cerr << "validate-scenario: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (path.empty()) {
        std::cerr << "validate-scenario: missing <scenario.json> (use '-' for stdin)\n";
        return kExitUsage;
    }

    std::string text, ioError;
    if (!readInput(path, text, ioError)) {
        std::cerr << "validate-scenario: " << ioError << "\n";
        return kExitUsage;
    }

    const json parsed = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        std::cerr << "validate-scenario: input is not valid JSON\n";
        return kExitUsage;
    }

    saida::ScenarioAsset asset;
    std::vector<saida::ScenarioIssue> issues;
    const bool ok = saida::ScenarioAsset::parse(parsed, asset, &issues);

    json issueJson = json::array();
    for (const auto& issue : issues)
        issueJson.push_back(json{{"path", issue.path}, {"message", issue.message}});

    const std::size_t issueCount = issueJson.size();
    json report{{"ok", ok},
                {"issueCount", issueCount},
                {"issues", std::move(issueJson)}};
    if (ok) {
        report["id"] = asset.id;
        report["version"] = asset.version;
        report["steps"] = asset.steps.size();
        report["roles"] = asset.roles.size();
    }

    std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    return ok ? kExitOk : kExitInvalid;
}

// Walk a scene-snapshot node subtree, appending structural issues. Pure JSON
// checks (no GPU) : every node needs a string type/name, ids must be unique
// integers, transforms and children must be well-shaped.
void walkSceneNode(const json& n, const std::string& path, json& issues,
                   std::unordered_set<long long>& ids, std::size_t& nodeCount) {
    auto issue = [&](const std::string& msg) {
        issues.push_back(json{{"path", path}, {"message", msg}});
    };
    if (!n.is_object()) {
        issue("node must be a JSON object");
        return;
    }
    ++nodeCount;
    if (!n.contains("type") || !n["type"].is_string())
        issue("node needs a string 'type'");
    if (!n.contains("name") || !n["name"].is_string())
        issue("node needs a string 'name'");
    if (n.contains("id")) {
        if (!n["id"].is_number_integer()) {
            issue("'id' must be an integer");
        } else if (!ids.insert(n["id"].get<long long>()).second) {
            issue("duplicate node id " + std::to_string(n["id"].get<long long>()));
        }
    }
    if (auto t = n.find("transform"); t != n.end()) {
        if (!t->is_object()) {
            issue("'transform' must be an object");
        } else {
            for (const char* k : {"position", "scale"})
                if (t->contains(k) && (!(*t)[k].is_array() || (*t)[k].size() != 3))
                    issue(std::string("transform '") + k + "' must be a 3-number array");
            if (t->contains("rotation") &&
                (!(*t)["rotation"].is_array() || (*t)["rotation"].size() != 4))
                issue("transform 'rotation' must be a 4-number array");
        }
    }
    if (auto c = n.find("children"); c != n.end()) {
        if (!c->is_array()) {
            issue("'children' must be an array");
        } else {
            std::size_t i = 0;
            for (const json& child : *c)
                walkSceneNode(child, path + "/children[" + std::to_string(i++) + "]", issues,
                              ids, nodeCount);
        }
    }
}

int cmdValidateScene(const std::vector<std::string>& args) {
    std::string path;
    bool pretty = false;
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "validate-scene: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (path.empty()) {
            path = a;
        } else {
            std::cerr << "validate-scene: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (path.empty()) {
        std::cerr << "validate-scene: missing <scene.json> (use '-' for stdin)\n";
        return kExitUsage;
    }

    std::string text, ioError;
    if (!readInput(path, text, ioError)) {
        std::cerr << "validate-scene: " << ioError << "\n";
        return kExitUsage;
    }
    const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (doc.is_discarded()) {
        std::cerr << "validate-scene: input is not valid JSON\n";
        return kExitUsage;
    }

    json issues = json::array();
    std::unordered_set<long long> ids;
    std::size_t nodeCount = 0;
    if (!doc.is_object() || !doc.contains("scene") || !doc["scene"].is_object()) {
        issues.push_back(json{{"path", ""}, {"message", "document needs a 'scene' object"}});
    } else {
        walkSceneNode(doc["scene"], "scene", issues, ids, nodeCount);
    }

    const bool ok = issues.empty();
    json report{{"ok", ok}, {"nodeCount", nodeCount},
                {"issueCount", issues.size()}, {"issues", std::move(issues)}};
    std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    return ok ? kExitOk : kExitInvalid;
}

int cmdApplyOps(const std::vector<std::string>& args) {
    std::string scenePath, opsPath, outPath;
    bool pretty = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--pretty") {
            pretty = true;
        } else if (a == "--out") {
            if (i + 1 >= args.size()) {
                std::cerr << "apply-ops: --out needs a path\n";
                return kExitUsage;
            }
            outPath = args[++i];
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "apply-ops: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (scenePath.empty()) {
            scenePath = a;
        } else if (opsPath.empty()) {
            opsPath = a;
        } else {
            std::cerr << "apply-ops: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (scenePath.empty() || opsPath.empty()) {
        std::cerr << "apply-ops: needs <scene.json> <ops.json> (use '-' for stdin, "
                     "at most one)\n";
        return kExitUsage;
    }
    if (scenePath == "-" && opsPath == "-") {
        std::cerr << "apply-ops: only one input may read from stdin\n";
        return kExitUsage;
    }

    std::string sceneText, opsText, ioError;
    if (!readInput(scenePath, sceneText, ioError)) {
        std::cerr << "apply-ops: " << ioError << "\n";
        return kExitUsage;
    }
    if (!readInput(opsPath, opsText, ioError)) {
        std::cerr << "apply-ops: " << ioError << "\n";
        return kExitUsage;
    }

    const json opsParsed = json::parse(opsText, nullptr, /*allow_exceptions=*/false);
    if (opsParsed.is_discarded()) {
        std::cerr << "apply-ops: ops input is not valid JSON\n";
        return kExitUsage;
    }
    const json ops = opsParsed.is_array() ? opsParsed : json::array({opsParsed});

    saida::Scene scene;
    std::string sceneError;
    if (!saida::authoring::deserializeSceneSnapshot(sceneText, scene, &sceneError)) {
        std::cerr << "apply-ops: cannot load scene: " << sceneError << "\n";
        return kExitUsage;
    }

    // Snapshot the loaded ids up front: create_node mints fresh ids via
    // generateNodeId(), which is seeded per-process — non-reproducible. We keep
    // loaded ids and renumber only the new nodes below.
    std::unordered_set<saida::NodeId> loadedIds;
    saida::NodeId maxLoadedId = 0;
    scene.traverse([&](saida::Node& n, const glm::mat4&) {
        loadedIds.insert(n.id());
        maxLoadedId = std::max(maxLoadedId, n.id());
    });

    // Apply in order; the first rejected op aborts the whole batch (atomic — an
    // op-log must never leave a half-applied snapshot). No GPU resources, so ops
    // needing one (e.g. create_node MeshNode) are reported as failures.
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const json res = json::parse(
            saida::authoring::applyOpJson(scene, nullptr, ops[i].dump()));
        if (!res.value("ok", false)) {
            json report{{"ok", false}, {"failedIndex", i},
                        {"error", res.value("error", std::string("unknown error"))},
                        {"applied", i}};
            std::cerr << report.dump() << "\n";
            return kExitInvalid;
        }
    }

    // Deterministic ids: renumber every node created by the ops (id absent from
    // the loaded set) to a sequential id above the loaded max, in depth-first
    // order. Loaded ids are preserved; the snapshot is byte-reproducible.
    saida::NodeId nextId = maxLoadedId + 1;
    scene.traverse([&](saida::Node& n, const glm::mat4&) {
        if (loadedIds.find(n.id()) == loadedIds.end())
            n.assignSerializedId(nextId++);
    });

    const std::string snapshot =
        saida::authoring::serializeSceneSnapshot(scene, nullptr);

    if (outPath.empty() || outPath == "-") {
        std::cout << snapshot << "\n";
    } else {
        std::ofstream out(outPath, std::ios::binary);
        if (!out) {
            std::cerr << "apply-ops: cannot write '" << outPath << "'\n";
            return kExitUsage;
        }
        out << snapshot << "\n";
        json report{{"ok", true}, {"applied", ops.size()}, {"out", outPath}};
        std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    }
    return kExitOk;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        return usage(std::cerr);
    }
    const std::string command = argv[1];
    const std::vector<std::string> rest(argv + 2, argv + argc);

    if (command == "help" || command == "--help" || command == "-h") {
        usage(std::cout);
        return kExitOk;
    }
    if (command == "describe-engine") {
        return cmdDescribeEngine(rest);
    }
    if (command == "validate-ops") {
        return cmdValidateOps(rest);
    }
    if (command == "validate-scenario") {
        return cmdValidateScenario(rest);
    }
    if (command == "validate-scene") {
        return cmdValidateScene(rest);
    }
    if (command == "apply-ops") {
        return cmdApplyOps(rest);
    }

    std::cerr << "saida_tool: unknown command '" << command << "'\n\n";
    return usage(std::cerr);
}
