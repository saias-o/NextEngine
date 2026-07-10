// Headless tool entry point. Keep machine output on stdout and diagnostics on stderr.

#include "authoring/EngineManifest.hpp"
#include "authoring/SaidaOp.hpp"
#include "authoring/SaidaOpApplier.hpp"
#include "authoring/SceneSnapshot.hpp"
#include "scene/BVHLoader.hpp"
#include "scene/GLTFLoader.hpp"
#include "scene/Scene.hpp"
#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/ClipView.hpp"
#include "scene/animation/Rig.hpp"
#include "scenario/ScenarioAsset.hpp"
#include "scripting/JsContext.hpp"
#include "scripting/JsRuntime.hpp"

#include <filesystem>

#include <nlohmann/json.hpp>
#include <glm/glm.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
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
           "  saida_tool validate-script <script.js> [--module|--script] [--pretty]\n"
           "  saida_tool validate-scenario <scenario.json> [--pretty]\n"
           "  saida_tool apply-ops <scene.json> <ops.json> [--out <file>] [--skip-invalid]\n"
           "  saida_tool inspect-anim <file.gltf|.glb|.bvh> [--pretty]\n"
           "  saida_tool validate-clipview <view.sclip> [--root <dir>] [--pretty]\n"
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
           "  validate-script   Compile-check a JS/MJS script with QuickJS (no exec,\n"
           "                    no engine bindings). Module mode follows the .mjs\n"
           "                    extension unless --module/--script overrides it.\n"
           "                    exit 0 if it compiles, 1 on a syntax error.\n"
           "  validate-scenario Statically validate a Saida scenario asset\n"
           "                    (format + registered actions/conditions). Prints a\n"
           "                    JSON report; exit 0 if valid, 1 if invalid.\n"
           "  apply-ops         Load a resource-free scene snapshot (or .saidaproj),\n"
           "                    apply a JSON array of SaidaOps in order, then emit a\n"
           "                    deterministic snapshot to --out (or stdout). All ops\n"
           "                    must apply cleanly (atomic); exit 1 if any is rejected\n"
           "                    and nothing is written. --skip-invalid instead drops\n"
           "                    ops that fail against the current state (reported on\n"
           "                    stderr) — used to fold a collaboration op-log where a\n"
           "                    later op targets a node an earlier op deleted.\n"
           "  inspect-anim      Load a glTF/GLB/BVH animation file headless and print\n"
           "                    its rigs (bone names) and clips (name, sub-asset key,\n"
           "                    duration, animated bone count) as JSON.\n"
           "  validate-clipview Parse a .sclip non-destructive clip view, resolve its\n"
           "                    'source' sub-asset key against --root (default: the\n"
           "                    .sclip's directory) and validate ranges/events against\n"
           "                    the source clip. Prints {ok, diagnostics}; exit 0/1.\n"
           "\n"
           "Files may be '-' for stdin. Exit codes: 0 ok, 1 invalid input, 2 usage/IO.\n";
    return kExitUsage;
}

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

int cmdValidateScript(const std::vector<std::string>& args) {
    std::string path;
    bool pretty = false;
    int moduleOverride = -1;  // -1 = auto, 0 = global, 1 = module
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else if (a == "--module") {
            moduleOverride = 1;
        } else if (a == "--script") {
            moduleOverride = 0;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "validate-script: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (path.empty()) {
            path = a;
        } else {
            std::cerr << "validate-script: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (path.empty()) {
        std::cerr << "validate-script: missing <script.js|.mjs> (use '-' for stdin)\n";
        return kExitUsage;
    }

    std::string source, ioError;
    if (!readInput(path, source, ioError)) {
        std::cerr << "validate-script: " << ioError << "\n";
        return kExitUsage;
    }

    bool asModule = false;
    if (moduleOverride >= 0) {
        asModule = moduleOverride == 1;
    } else {
        const std::size_t dot = path.rfind('.');
        asModule = dot != std::string::npos && path.substr(dot) == ".mjs";
    }
    const std::string filename = path == "-" ? (asModule ? "<stdin>.mjs" : "<stdin>.js") : path;

    // QuickJS logs to stdout; redirect it so the report remains machine-readable.
    std::string error;
    bool ok;
    std::streambuf* prevCout = std::cout.rdbuf(std::cerr.rdbuf());
    {
        saida::JsRuntime runtime;
        std::unique_ptr<saida::JsContext> ctx = runtime.createContext();
        ok = ctx->compileCheck(source, filename, asModule, error);
    }  // runtime/context destroyed here — their logs still routed to stderr
    std::cout.rdbuf(prevCout);

    json report{{"ok", ok}, {"module", asModule}};
    if (!ok) report["error"] = error;
    std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    return ok ? kExitOk : kExitInvalid;
}

int cmdApplyOps(const std::vector<std::string>& args) {
    std::string scenePath, opsPath, outPath;
    bool pretty = false;
    bool skipInvalid = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--pretty") {
            pretty = true;
        } else if (a == "--skip-invalid") {
            skipInvalid = true;
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

    // New node IDs are process-seeded, so preserve loaded IDs and renumber new ones.
    std::unordered_set<saida::NodeId> loadedIds;
    saida::NodeId maxLoadedId = 0;
    scene.traverse([&](saida::Node& n, const glm::mat4&) {
        loadedIds.insert(n.id());
        maxLoadedId = std::max(maxLoadedId, n.id());
    });

    // Default mode is atomic; --skip-invalid supports conflict-tolerant op logs.
    json skipped = json::array();
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const json res = json::parse(
            saida::authoring::applyOpJson(scene, nullptr, ops[i].dump()));
        if (!res.value("ok", false)) {
            const std::string error = res.value("error", std::string("unknown error"));
            if (skipInvalid) {
                skipped.push_back(json{{"index", i}, {"error", error}});
                continue;
            }
            json report{{"ok", false}, {"failedIndex", i}, {"error", error},
                        {"applied", i}};
            std::cerr << report.dump() << "\n";
            return kExitInvalid;
        }
    }
    if (skipInvalid && !skipped.empty()) {
        std::cerr << json{{"skipped", skipped}}.dump() << "\n";
    }

    // Renumber only created nodes so identical input yields identical snapshots.
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
        json report{{"ok", true}, {"applied", ops.size() - skipped.size()},
                    {"skipped", skipped.size()}, {"out", outPath}};
        std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    }
    return kExitOk;
}

} // namespace

// --- Animation authoring (PLAN_ANIMATION.md Étape 1) ------------------------

bool hasExtension(const std::string& path, const char* ext) {
    std::string e = std::filesystem::path(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e == ext;
}

// Charge rigs + clips d'un fichier d'animation sans GPU. Retourne false avec
// un message si le format n'est pas supporté ou si l'import échoue.
bool loadAnimationFile(const std::string& path, saida::GltfAnimationData& out,
                       std::string& error) {
    if (hasExtension(path, ".bvh")) {
        auto clip = saida::BVHLoader::parse(path);
        if (!clip) {
            error = "failed to parse BVH '" + path + "'";
            return false;
        }
        out.clipNames.push_back(clip->name());
        out.clips.push_back(std::move(clip));
        return true;
    }
    if (hasExtension(path, ".gltf") || hasExtension(path, ".glb")) {
        return saida::GLTFLoader::loadAnimationData(path, out, &error);
    }
    error = "unsupported animation file '" + path + "' (expected .gltf, .glb or .bvh)";
    return false;
}

int cmdInspectAnim(const std::vector<std::string>& args) {
    std::string path;
    bool pretty = false;
    for (const std::string& a : args) {
        if (a == "--pretty") {
            pretty = true;
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "inspect-anim: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (path.empty()) {
            path = a;
        } else {
            std::cerr << "inspect-anim: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (path.empty()) {
        std::cerr << "inspect-anim: missing <file.gltf|.glb|.bvh>\n";
        return kExitUsage;
    }

    saida::GltfAnimationData data;
    std::string error;
    if (!loadAnimationFile(path, data, error)) {
        std::cerr << "inspect-anim: " << error << "\n";
        return kExitInvalid;
    }

    json rigs = json::array();
    for (size_t i = 0; i < data.rigs.size(); ++i) {
        const saida::Rig& rig = *data.rigs[i];
        json bones = json::array();
        for (const auto& bone : rig.bones()) bones.push_back(bone.name);
        rigs.push_back({{"index", i}, {"boneCount", rig.boneCount()}, {"bones", bones}});
    }

    json clips = json::array();
    for (size_t i = 0; i < data.clips.size(); ++i) {
        const saida::AnimationClip& clip = *data.clips[i];
        const std::string name = data.clipNames[i];
        clips.push_back({{"name", name},
                         {"key", path + "#" + name},
                         {"duration", clip.duration()},
                         {"animatedBones", clip.boneNames().size()}});
    }

    const json report = {{"file", path}, {"rigs", rigs}, {"clips", clips}};
    std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    return kExitOk;
}

int cmdValidateClipView(const std::vector<std::string>& args) {
    std::string path;
    std::string root;
    bool pretty = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--pretty") {
            pretty = true;
        } else if (a == "--root") {
            if (i + 1 >= args.size()) {
                std::cerr << "validate-clipview: --root needs a directory\n";
                return kExitUsage;
            }
            root = args[++i];
        } else if (!a.empty() && a[0] == '-' && a != "-") {
            std::cerr << "validate-clipview: unknown option '" << a << "'\n";
            return kExitUsage;
        } else if (path.empty()) {
            path = a;
        } else {
            std::cerr << "validate-clipview: unexpected extra argument '" << a << "'\n";
            return kExitUsage;
        }
    }
    if (path.empty()) {
        std::cerr << "validate-clipview: missing <view.sclip>\n";
        return kExitUsage;
    }
    if (root.empty()) root = std::filesystem::path(path).parent_path().string();

    auto parsed = saida::ClipView::loadFile(path);
    std::vector<saida::AssetDiagnostic> diags = std::move(parsed.diagnostics);

    if (parsed.ok) {
        // Résout la clé de sous-asset "fichier#clip" relativement à --root
        // (défaut : le dossier du .sclip) et valide contre la source.
        const std::string& key = parsed.view.source;
        const size_t hash = key.rfind('#');
        const std::string relFile = hash == std::string::npos ? key : key.substr(0, hash);
        const std::string clipName = hash == std::string::npos ? "" : key.substr(hash + 1);
        const std::string sourcePath = (std::filesystem::path(root) / relFile).string();

        saida::GltfAnimationData data;
        std::string error;
        if (!loadAnimationFile(sourcePath, data, error)) {
            diags.push_back({"clipview.source.unresolved", saida::AssetDiagnostic::Severity::Error,
                             "/source", error});
        } else {
            const saida::AnimationClip* sourceClip = nullptr;
            for (size_t i = 0; i < data.clips.size(); ++i) {
                if (clipName.empty() || data.clipNames[i] == clipName) {
                    sourceClip = data.clips[i].get();
                    break;
                }
            }
            if (!sourceClip) {
                diags.push_back({"clipview.source.clip_missing",
                                 saida::AssetDiagnostic::Severity::Error, "/source",
                                 "'" + relFile + "' has no clip named '" + clipName + "'"});
            } else {
                auto more = parsed.view.validate(sourceClip);
                diags.insert(diags.end(), more.begin(), more.end());
            }
        }
    }

    json report = {{"ok", !saida::hasErrors(diags)}, {"diagnostics", json::array()}};
    for (const auto& d : diags) report["diagnostics"].push_back(d.toJson());
    std::cout << (pretty ? report.dump(2) : report.dump()) << "\n";
    return saida::hasErrors(diags) ? kExitInvalid : kExitOk;
}

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
    if (command == "validate-script") {
        return cmdValidateScript(rest);
    }
    if (command == "apply-ops") {
        return cmdApplyOps(rest);
    }
    if (command == "inspect-anim") {
        return cmdInspectAnim(rest);
    }
    if (command == "validate-clipview") {
        return cmdValidateClipView(rest);
    }

    std::cerr << "saida_tool: unknown command '" << command << "'\n\n";
    return usage(std::cerr);
}
