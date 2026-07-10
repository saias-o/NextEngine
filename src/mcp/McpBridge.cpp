#include "mcp/McpBridge.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Reflection.hpp"
#include "editor/Command.hpp"
#include "editor/EditorUI.hpp"
#include "editor/SceneDocument.hpp"
#include "graphics/ResourceManager.hpp"
#include "mcp/McpServer.hpp"
#include "project/Project.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/GLTFLoader.hpp"
#include "scene/Node.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scene/SignalWiring.hpp"
#include "scripting/JsContext.hpp"
#include "scripting/JsRuntime.hpp"
#include "scripting/ScriptBehaviour.hpp"
#include "scenario/ScenarioAsset.hpp"
#include "scenario/ScenarioRegistry.hpp"
#include "scenario/ScenarioRunnerBehaviour.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace saida {

using json = nlohmann::json;

namespace {

// Editor context handed to every tool. `exec` routes through the undoable
// command chokepoint; `canEdit` is false during Play (mutations rejected).
struct ToolCtx {
    Scene* scene = nullptr;
    ResourceManager* resources = nullptr;
    SceneDocument* doc = nullptr;
    Project* project = nullptr;
    bool canEdit = false;
    std::function<void(std::unique_ptr<Command>)> exec;
};

[[noreturn]] void fail(const std::string& msg) { throw std::runtime_error(msg); }

void requireEdit(const ToolCtx& ctx) {
    if (!ctx.canEdit) fail("scene is read-only while in Play mode");
}

NodeId parseNodeId(const json& v) {
    if (v.is_string()) {
        try { return std::stoull(v.get<std::string>()); } catch (...) { return kNodeInvalid; }
    }
    if (v.is_number_unsigned()) return v.get<uint64_t>();
    if (v.is_number_integer()) return static_cast<NodeId>(v.get<int64_t>());
    return kNodeInvalid;
}

// NodeId is 64-bit; emit as a string so JS clients never lose precision.
std::string idStr(NodeId id) { return std::to_string(id); }

Node* requireNode(const ToolCtx& ctx, const json& args, const char* key = "id") {
    if (!args.contains(key)) fail(std::string("missing '") + key + "'");
    NodeId id = parseNodeId(args[key]);
    Node* node = ctx.doc ? ctx.doc->find(id) : nullptr;
    if (!node) fail("no node with id " + idStr(id));
    return node;
}

std::vector<std::string> sortedKeys(const std::unordered_map<std::string, std::function<std::unique_ptr<Node>()>>& m) {
    std::vector<std::string> keys;
    keys.reserve(m.size());
    for (const auto& [k, _] : m) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    return keys;
}
std::vector<std::string> sortedKeys(const std::unordered_map<std::string, std::function<std::unique_ptr<Behaviour>()>>& m) {
    std::vector<std::string> keys;
    keys.reserve(m.size());
    for (const auto& [k, _] : m) keys.push_back(k);
    std::sort(keys.begin(), keys.end());
    return keys;
}

glm::vec3 vec3Of(const json& j, const glm::vec3& fallback) {
    if (j.is_array() && j.size() == 3)
        return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
    return fallback;
}

json compactNode(const Node& node) {
    json j;
    j["id"] = idStr(node.id());
    j["name"] = node.name();
    j["type"] = node.typeName();
    if (!node.enabled()) j["enabled"] = false;
    if (!node.groups().empty()) j["groups"] = node.groups();

    json behs = json::array();
    for (const auto& b : node.behaviours())
        if (b->typeName()) behs.push_back(b->typeName());
    if (!behs.empty()) j["behaviours"] = std::move(behs);

    json children = json::array();
    for (const auto& c : node.children()) children.push_back(compactNode(*c));
    if (!children.empty()) j["children"] = std::move(children);
    return j;
}

const reflect::PropertyDesc* findProp(const std::string& typeName, const std::string& prop) {
    if (const auto* d = reflect::TypeRegistry::instance().find(typeName))
        return d->findProperty(prop);
    return nullptr;
}

// Apply a JSON value to a (node or behaviour) reflected property, re-resolving
// the target each time so the closure stays valid across scene reconstructions.
std::function<void(Node&)> makePropApply(bool onBehaviour, std::string behType,
                                         std::string prop, json value) {
    return [onBehaviour, behType, prop, value](Node& n) {
        if (onBehaviour) {
            for (const auto& b : n.behaviours()) {
                if (b->typeName() && behType == b->typeName()) {
                    if (const auto* p = findProp(behType, prop)) p->set(b.get(), value);
                    return;
                }
            }
        } else if (const auto* p = findProp(n.typeName(), prop)) {
            p->set(&n, value);
        }
    };
}

json readProp(const Node& n, bool onBehaviour, const std::string& behType, const std::string& prop) {
    json out;
    if (onBehaviour) {
        for (const auto& b : n.behaviours())
            if (b->typeName() && behType == b->typeName()) {
                if (const auto* p = findProp(behType, prop)) p->get(b.get(), out);
                return out;
            }
    } else if (const auto* p = findProp(n.typeName(), prop)) {
        p->get(&n, out);
    }
    return out;
}

std::string manifestHash() {
    std::string dump = reflect::TypeRegistry::instance().manifest("").dump();
    return std::to_string(std::hash<std::string>{}(dump));
}

json toolDescribeApi(const ToolCtx&, const json& args) {
    auto& reg = reflect::TypeRegistry::instance();
    if (args.contains("type")) return reg.manifestFor(args["type"].get<std::string>());

    // Summary mode avoids rebuilding an unchanged full manifest.
    if (args.value("summary", false)) {
        json out, behaviours = json::array(), nodes = json::array();
        json all = reg.manifest("");
        for (const auto& t : all.value("behaviours", json::array())) behaviours.push_back(t["name"]);
        for (const auto& t : all.value("nodes", json::array())) nodes.push_back(t["name"]);
        out["behaviours"] = std::move(behaviours);
        out["nodes"] = std::move(nodes);
        out["hash"] = manifestHash();
        return out;
    }

    json full = reg.manifest(args.value("category", std::string()));
    full["hash"] = manifestHash();  // cache key: unchanged hash => reuse last manifest
    return full;
}

json toolListNodeTypes(const ToolCtx&, const json&) {
    return sortedKeys(NodeRegistry::instance().factories());
}
json toolListBehaviourTypes(const ToolCtx&, const json&) {
    return sortedKeys(BehaviourRegistry::instance().factories());
}

json toolGetScene(const ToolCtx& ctx, const json&) {
    if (!ctx.scene) fail("no scene bound");
    json out = compactNode(*ctx.scene);
    if (!ctx.scene->connections().empty()) {
        json conns = json::array();
        for (const auto& c : ctx.scene->connections())
            conns.push_back({{"from", idStr(c.from)}, {"signal", c.signal},
                             {"to", idStr(c.to)}, {"slot", c.slot}});
        out["connections"] = std::move(conns);
    }
    return out;
}

json toolGetNode(const ToolCtx& ctx, const json& args) {
    Node* node = requireNode(ctx, args);
    if (!ctx.resources) fail("no resources bound");
    return json::parse(SceneSerializer::nodeToJson(*node, *ctx.resources));
}

json toolFindNodes(const ToolCtx& ctx, const json& args) {
    if (!ctx.scene) fail("no scene bound");
    std::string group = args.value("group", std::string());
    std::string type = args.value("type", std::string());
    json hits = json::array();
    ctx.scene->traverse([&](Node& n, const glm::mat4&) {
        if (!group.empty() && !n.isInGroup(group)) return;
        if (!type.empty() && type != n.typeName()) return;
        hits.push_back({{"id", idStr(n.id())}, {"name", n.name()}, {"type", n.typeName()}});
    });
    return hits;
}

json toolCreateNode(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    if (!args.contains("type")) fail("missing 'type'");
    std::string type = args["type"].get<std::string>();
    auto node = NodeRegistry::instance().create(type);
    if (!node) fail("unknown node type '" + type + "'");
    if (args.contains("name")) node->setName(args["name"].get<std::string>());
    node->regenerateId();
    NodeId newId = node->id();

    NodeId parent = args.contains("parent") ? parseNodeId(args["parent"]) : ctx.scene->id();
    if (!ctx.doc->find(parent)) fail("no parent node with id " + idStr(parent));

    ctx.exec(std::make_unique<AddNodeCommand>(parent, std::move(node)));
    return {{"id", idStr(newId)}};
}

json toolDeleteNode(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    ctx.exec(std::make_unique<DeleteNodeCommand>(node->id()));
    return {{"ok", true}};
}

json toolRenameNode(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    if (!args.contains("name")) fail("missing 'name'");
    ctx.exec(std::make_unique<RenameNodeCommand>(node->id(), node->name(),
                                                 args["name"].get<std::string>()));
    return {{"ok", true}};
}

json toolReparent(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    if (!args.contains("parent")) fail("missing 'parent'");
    NodeId parent = parseNodeId(args["parent"]);
    if (!ctx.doc->find(parent)) fail("no parent node with id " + idStr(parent));
    ctx.exec(std::make_unique<ReparentNodeCommand>(node->id(), parent));
    return {{"ok", true}};
}

json toolSetTransform(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    Transform oldT = node->transform();
    Transform newT = oldT;
    if (args.contains("position")) newT.position = vec3Of(args["position"], oldT.position);
    if (args.contains("scale")) newT.scale = vec3Of(args["scale"], oldT.scale);
    if (args.contains("rotation") && args["rotation"].is_array() && args["rotation"].size() == 4) {
        const auto& r = args["rotation"];
        newT.rotation = glm::quat(r[3].get<float>(), r[0].get<float>(),
                                  r[1].get<float>(), r[2].get<float>());
    }
    ctx.exec(std::make_unique<TransformCommand>(node->id(), oldT, newT));
    return {{"ok", true}};
}

json toolAddBehaviour(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    if (!args.contains("type")) fail("missing 'type'");
    std::string type = args["type"].get<std::string>();
    if (!BehaviourRegistry::instance().create(type)) fail("unknown behaviour type '" + type + "'");
    ctx.exec(std::make_unique<AddBehaviourCommand>(node->id(), type));
    return {{"ok", true}};
}

json toolSetProperty(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    if (!args.contains("name") || !args.contains("value")) fail("missing 'name'/'value'");
    std::string prop = args["name"].get<std::string>();
    bool onBehaviour = args.contains("behaviour");
    std::string behType = onBehaviour ? args["behaviour"].get<std::string>() : std::string();

    std::string typeKey = onBehaviour ? behType : std::string(node->typeName());
    if (!findProp(typeKey, prop))
        fail("type '" + typeKey + "' has no reflected property '" + prop + "'");

    json oldVal = readProp(*node, onBehaviour, behType, prop);
    json newVal = args["value"];
    std::string label = "Set " + prop;
    ctx.exec(std::make_unique<SetPropertyCommand>(
        node->id(), label,
        makePropApply(onBehaviour, behType, prop, oldVal),
        makePropApply(onBehaviour, behType, prop, newVal)));
    return {{"ok", true}};
}

json toolGroup(const ToolCtx& ctx, const json& args, bool add) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    if (!args.contains("group")) fail("missing 'group'");
    std::string group = args["group"].get<std::string>();
    NodeId id = node->id();
    auto apply = [group, add](Node& n) { if (add) n.addToGroup(group); else n.removeFromGroup(group); };
    auto undo = [group, add](Node& n) { if (add) n.removeFromGroup(group); else n.addToGroup(group); };
    ctx.exec(std::make_unique<SetPropertyCommand>(id, add ? "Add To Group" : "Remove From Group",
                                                  undo, apply));
    return {{"ok", true}};
}

json toolConnectSignal(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    for (const char* k : {"from", "signal", "to", "slot"})
        if (!args.contains(k)) fail(std::string("missing '") + k + "'");
    SignalConnectionDef def;
    def.from = parseNodeId(args["from"]);
    def.signal = args["signal"].get<std::string>();
    def.to = parseNodeId(args["to"]);
    def.slot = args["slot"].get<std::string>();
    if (!ctx.doc->find(def.from)) fail("no 'from' node with id " + idStr(def.from));
    if (!ctx.doc->find(def.to)) fail("no 'to' node with id " + idStr(def.to));
    ctx.exec(std::make_unique<ConnectSignalCommand>(std::move(def)));
    return {{"ok", true}};
}

json toolSetSceneSettings(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    if (!ctx.scene) fail("no scene bound");
    SceneSettings oldS = ctx.scene->settings();
    SceneSettings newS = oldS;
    if (args.contains("ambient")) newS.ambientLight = glm::vec4(vec3Of(args["ambient"], glm::vec3(oldS.ambientLight)), 0.0f);
    if (args.contains("clearColor")) newS.clearColor = glm::vec4(vec3Of(args["clearColor"], glm::vec3(oldS.clearColor)), 1.0f);
    if (args.contains("giEnabled")) newS.giEnabled = args["giEnabled"].get<bool>();
    if (args.contains("giIntensity")) newS.giIntensity = args["giIntensity"].get<float>();
    if (args.contains("fogEnabled")) newS.fogEnabled = args["fogEnabled"].get<bool>();
    if (args.contains("fogColor")) newS.fogColor = glm::vec4(vec3Of(args["fogColor"], glm::vec3(oldS.fogColor)), 1.0f);
    if (args.contains("fogDensity")) newS.fogDensity = args["fogDensity"].get<float>();
    if (args.contains("bloomEnabled")) newS.bloomEnabled = args["bloomEnabled"].get<bool>();
    if (args.contains("bloomIntensity")) newS.bloomIntensity = args["bloomIntensity"].get<float>();
    if (args.contains("aoEnabled")) newS.aoEnabled = args["aoEnabled"].get<bool>();
    if (args.contains("skyboxExposure")) newS.skyboxExposure = args["skyboxExposure"].get<float>();

    NodeId rootId = ctx.scene->id();
    ctx.exec(std::make_unique<SetPropertyCommand>(
        rootId, "Scene Settings",
        [oldS](Node& n) { if (auto* s = dynamic_cast<Scene*>(&n)) s->settings() = oldS; },
        [newS](Node& n) { if (auto* s = dynamic_cast<Scene*>(&n)) s->settings() = newS; }));
    return {{"ok", true}};
}

namespace fs = std::filesystem;

std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void spit(const std::string& path, const std::string& content) {
    fs::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) fail("could not write " + path);
    f << content;
}

std::string sanitizeIdent(const std::string& s) {
    std::string out;
    for (char c : s)
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') out += c;
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0]))) out = "B" + out;
    return out;
}

std::string projectRootOf(const ToolCtx& ctx) {
    if (ctx.project && ctx.project->isLoaded()) return ctx.project->rootPath();
    return std::string(SAIDA_PROJECT_ROOT);
}

SandboxedPathResult resolveToolPath(const ToolCtx& ctx, const std::string& path,
                                    const std::string& defaultDirectory = {}) {
    SandboxedPathResult resolved =
        resolveSandboxedProjectPath(projectRootOf(ctx), path, defaultDirectory);
    if (!resolved) fail(resolved.error + ": " + path);
    return resolved;
}

json toolWriteScript(const ToolCtx& ctx, const json& args) {
    if (!args.contains("path") || !args.contains("code")) fail("missing 'path'/'code'");
    SandboxedPathResult scriptPath =
        resolveToolPath(ctx, args["path"].get<std::string>(), "scripts");
    spit(scriptPath.absolute, args["code"].get<std::string>());

    json out{{"path", scriptPath.absolute}, {"relativePath", scriptPath.relative}};
    if (args.contains("attachTo")) {
        requireEdit(ctx);
        Node* node = ctx.doc->find(parseNodeId(args["attachTo"]));
        if (!node) fail("attachTo node not found");
        ctx.exec(std::make_unique<AttachScriptCommand>(node->id(), scriptPath.relative));
        out["attached"] = true;
    }
    return out;
}

json toolWriteUi(const ToolCtx& ctx, const json& args) {
    if (!args.contains("path") || !args.contains("code")) fail("missing 'path'/'code'");
    SandboxedPathResult uiPath = resolveToolPath(ctx, args["path"].get<std::string>());
    spit(uiPath.absolute, args["code"].get<std::string>());
    return {{"path", uiPath.absolute}, {"relativePath", uiPath.relative}};
}

std::string scenarioAbsPath(const ToolCtx& ctx, const json& args) {
    if (!args.contains("path")) fail("missing 'path'");
    return resolveToolPath(ctx, args["path"].get<std::string>()).absolute;
}

json scenarioIssuesJson(const std::vector<ScenarioIssue>& issues) {
    json out = json::array();
    for (const auto& issue : issues)
        out.push_back({{"path", issue.path}, {"message", issue.message}});
    return out;
}

json toolListScenarioActions(const ToolCtx&, const json&) {
    return ScenarioActionRegistry::names();
}

json toolListScenarioConditions(const ToolCtx&, const json&) {
    return ScenarioConditionRegistry::names();
}

json toolValidateScenario(const ToolCtx& ctx, const json& args) {
    ScenarioAsset asset;
    std::vector<ScenarioIssue> issues;
    std::string abs = scenarioAbsPath(ctx, args);
    bool ok = ScenarioAsset::loadFromFile(abs, asset, &issues);
    return {{"ok", ok}, {"path", abs}, {"issues", scenarioIssuesJson(issues)}};
}

json toolGetScenario(const ToolCtx& ctx, const json& args) {
    std::string abs = scenarioAbsPath(ctx, args);
    std::string text = slurp(abs);
    if (text.empty()) fail("could not read scenario: " + abs);
    return json::parse(text);
}

json toolCreateScenario(const ToolCtx& ctx, const json& args) {
    if (!args.contains("path")) fail("missing 'path'");
    std::string abs = scenarioAbsPath(ctx, args);
    json doc = args.value("scenario", json::object());
    if (doc.empty()) {
        doc = {
            {"version", 1},
            {"id", fs::path(abs).stem().string()},
            {"roles", json::object()},
            {"blackboard", json::object()},
            {"steps", json::array({{{"id", "start"}, {"end", "success"}}})}
        };
    }
    ScenarioAsset asset;
    std::vector<ScenarioIssue> issues;
    ScenarioAsset::parse(doc, asset, &issues);
    if (!issues.empty()) return {{"ok", false}, {"path", abs}, {"issues", scenarioIssuesJson(issues)}};
    spit(abs, asset.toJson().dump(2) + "\n");
    return {{"ok", true}, {"path", abs}};
}

json toolUpdateScenarioStep(const ToolCtx& ctx, const json& args) {
    if (!args.contains("step") || !args["step"].is_object()) fail("missing object 'step'");
    std::string abs = scenarioAbsPath(ctx, args);
    json doc = json::parse(slurp(abs));
    std::string id = args["step"].value("id", std::string());
    if (id.empty()) fail("step.id is required");
    json& steps = doc["steps"];
    if (!steps.is_array()) steps = json::array();
    bool replaced = false;
    for (auto& step : steps) {
        if (step.is_object() && step.value("id", std::string()) == id) {
            step = args["step"];
            replaced = true;
            break;
        }
    }
    if (!replaced) steps.push_back(args["step"]);

    ScenarioAsset asset;
    std::vector<ScenarioIssue> issues;
    ScenarioAsset::parse(doc, asset, &issues);
    if (!issues.empty()) return {{"ok", false}, {"path", abs}, {"issues", scenarioIssuesJson(issues)}};
    spit(abs, asset.toJson().dump(2) + "\n");
    return {{"ok", true}, {"path", abs}};
}

json toolAttachScenario(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    if (!args.contains("path")) fail("missing 'path'");
    SandboxedPathResult scenarioPath = resolveToolPath(ctx, args["path"].get<std::string>());
    auto* runner = node->getBehaviour<ScenarioRunnerBehaviour>();
    if (!runner) {
        ctx.exec(std::make_unique<AddBehaviourCommand>(node->id(), "ScenarioRunner"));
        runner = node->getBehaviour<ScenarioRunnerBehaviour>();
    }
    if (!runner) fail("failed to attach ScenarioRunner");
    runner->scenarioPath = scenarioPath.relative;
    if (args.contains("autoStart")) runner->autoStart = args["autoStart"].get<bool>();
    return {{"ok", true},
            {"id", idStr(node->id())},
            {"behaviour", "ScenarioRunner"},
            {"path", scenarioPath.absolute},
            {"relativePath", scenarioPath.relative}};
}

std::string cppType(const std::string& t) {
    if (t == "int") return "int";
    if (t == "bool") return "bool";
    if (t == "string") return "std::string";
    if (t == "vec3") return "glm::vec3";
    return "float";
}

std::string cppDefault(const std::string& t, const json& d) {
    if (t == "bool") return (d.is_boolean() && d.get<bool>()) ? "true" : "false";
    if (t == "string") return "\"" + (d.is_string() ? d.get<std::string>() : std::string()) + "\"";
    if (t == "int") return d.is_number() ? std::to_string(d.get<int>()) : "0";
    if (t == "vec3") {
        if (d.is_array() && d.size() == 3)
            return "{" + std::to_string(d[0].get<float>()) + "f," + std::to_string(d[1].get<float>()) +
                   "f," + std::to_string(d[2].get<float>()) + "f}";
        return "{0.0f, 0.0f, 0.0f}";
    }
    return (d.is_number() ? std::to_string(d.get<float>()) : "0.0") + "f";
}

void patchReflectedTypes(const std::string& className) {
    std::string path = std::string(SAIDA_PROJECT_ROOT) + "/src/scene/ReflectedTypes.cpp";
    std::string src = slurp(path);
    if (src.empty()) fail("could not read ReflectedTypes.cpp");

    std::string includeLine = "#include \"generated/" + className + ".hpp\"";
    std::string registerLine = "    registerBehaviour<" + className + ">();";
    if (src.find(includeLine) != std::string::npos) return;  // already registered

    const std::string incMarker = "// <<SAIDA_MCP_INCLUDES>>";
    const std::string regMarker = "// <<SAIDA_MCP_REGISTER>>";
    auto incPos = src.find(incMarker);
    auto regPos = src.find(regMarker);
    if (incPos == std::string::npos || regPos == std::string::npos)
        fail("ReflectedTypes.cpp markers missing");
    src.insert(incPos, includeLine + "\n");
    regPos = src.find(regMarker);  // shifted by the insert above
    src.insert(regPos, registerLine + "\n    ");
    spit(path, src);
}

json toolWriteCppBehaviour(const ToolCtx&, const json& args) {
    if (!args.contains("name")) fail("missing 'name'");
    std::string className = sanitizeIdent(args["name"].get<std::string>());
    const json& props = args.contains("properties") ? args["properties"] : json::array();
    std::string doc = args.value("doc", std::string());
    std::string onUpdate = args.value("onUpdate", std::string());
    std::string onReady = args.value("onReady", std::string());

    // Members + describe() property lines.
    std::string members, describeBody;
    for (const auto& p : props) {
        std::string pn = sanitizeIdent(p.at("name").get<std::string>());
        std::string ty = p.value("type", std::string("float"));
        json def = p.contains("default") ? p["default"] : json(nullptr);
        members += "    " + cppType(ty) + " " + pn + " = " + cppDefault(ty, def) + ";\n";
        describeBody += "    t.property(\"" + pn + "\", &" + className + "::" + pn + ")";
        if (p.contains("min") && p.contains("max"))
            describeBody += ".range(" + std::to_string(p["min"].get<double>()) + ", " +
                            std::to_string(p["max"].get<double>()) + ")";
        if (p.contains("tooltip"))
            describeBody += ".tooltip(\"" + p["tooltip"].get<std::string>() + "\")";
        describeBody += ";\n";
    }

    std::string header =
        "#pragma once\n\n"
        "#include \"core/Reflection.hpp\"\n"
        "#include \"scene/Behaviour.hpp\"\n\n"
        "#include <glm/glm.hpp>\n"
        "#include <string>\n\n"
        "namespace saida {\n\n"
        "class " + className + " : public Behaviour {\n"
        "public:\n";
    if (!onReady.empty()) header += "    void onReady() override;\n";
    header +=
        "    void onUpdate(float dt) override;\n\n"
        "    SAIDA_REFLECT_BEHAVIOUR(" + className + ", \"" + className + "\")\n\n" +
        members +
        "};\n\n"
        "} // namespace saida\n";

    std::string body =
        "#include \"generated/" + className + ".hpp\"\n\n"
        "#include \"core/Reflection.hpp\"\n"
        "#include \"scene/Node.hpp\"\n\n"
        "namespace saida {\n\n"
        "void " + className + "::describe(reflect::TypeBuilder<" + className + ">& t) {\n";
    if (!doc.empty()) body += "    t.doc(\"" + doc + "\");\n";
    body += describeBody;
    body += "}\n\n";
    if (!onReady.empty())
        body += "void " + className + "::onReady() {\n" + onReady + "\n}\n\n";
    body +=
        "void " + className + "::onUpdate(float dt) {\n"
        "    (void)dt;\n" +
        onUpdate + "\n"
        "}\n\n"
        "} // namespace saida\n";

    std::string dir = std::string(SAIDA_PROJECT_ROOT) + "/src/generated/";
    spit(dir + className + ".hpp", header);
    spit(dir + className + ".cpp", body);
    patchReflectedTypes(className);

    return {{"type", className},
            {"files", json::array({dir + className + ".hpp", dir + className + ".cpp"})},
            {"note", "Run the 'build' tool to compile; the type then appears in describe_api."}};
}

json toolBuild(const ToolCtx&, const json&) {
    std::string cmd = "cmake --build \"" + std::string(SAIDA_PROJECT_ROOT) + "/build\" -j 2 2>&1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) fail("could not launch build (is cmake on PATH?)");
    std::string out;
    char buf[4096];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
    int rc = pclose(pipe);
    std::string tail = out.size() > 6000 ? out.substr(out.size() - 6000) : out;
    return {{"ok", rc == 0}, {"exitCode", rc}, {"output", tail}};
}

json toolHeadlessCheck(const ToolCtx& ctx, const json&) {
    json errors = json::array();
    int checked = 0;
    if (ctx.scene) {
        JsContext jsctx(JsRuntime::instance());
        std::string root = projectRootOf(ctx);
        ctx.scene->traverse([&](Node& n, const glm::mat4&) {
            for (const auto& b : n.behaviours()) {
                auto* sb = dynamic_cast<ScriptBehaviour*>(b.get());
                if (!sb || sb->scriptPath().empty()) continue;
                fs::path pp(sb->scriptPath());
                std::string abs = pp.is_absolute() ? sb->scriptPath() : root + "/" + sb->scriptPath();
                std::string code = slurp(abs);
                ++checked;
                if (code.empty()) {
                    errors.push_back({{"script", sb->scriptPath()}, {"error", "file not found or empty"}});
                    continue;
                }
                std::string err;
                if (!jsctx.compileCheck(code, abs, pp.extension() == ".mjs", err))
                    errors.push_back({{"script", sb->scriptPath()}, {"error", err}});
            }
        });
    }
    return {{"ok", errors.empty()}, {"scriptsChecked", checked}, {"errors", errors}};
}

json toolReadLogs(const ToolCtx&, const json& args) {
    size_t count = args.value("count", static_cast<size_t>(100));
    return Log::recent(count);
}

// Push an arbitrary serialized config into a behaviour via its load() hook. This
// is how nested, data-driven behaviours (StateMachine, Blackboard,
// ScriptBehaviour properties) are authored in one call. Undoable.
json toolConfigureBehaviour(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    Node* node = requireNode(ctx, args);
    if (!args.contains("behaviour") || !args.contains("data")) fail("missing 'behaviour'/'data'");
    std::string behType = args["behaviour"].get<std::string>();
    json newData = args["data"];

    json oldData;
    bool found = false;
    for (const auto& b : node->behaviours())
        if (b->typeName() && behType == b->typeName()) { b->save(oldData); found = true; break; }
    if (!found) fail("node has no behaviour of type '" + behType + "'");

    auto applyData = [behType](Node& n, const json& d) {
        for (const auto& b : n.behaviours())
            if (b->typeName() && behType == b->typeName()) { b->load(d); return; }
    };
    ctx.exec(std::make_unique<SetPropertyCommand>(
        node->id(), "Configure " + behType,
        [applyData, oldData](Node& n) { applyData(n, oldData); },
        [applyData, newData](Node& n) { applyData(n, newData); }));
    return {{"ok", true}};
}

// Import a generated .glb/.gltf as a node subtree, optionally with auto LODs.
// The external model generator (a separate MCP) produces the file; this brings it
// into the scene. Undoable; re-imported from the path on undo/redo.
json toolImportModel(const ToolCtx& ctx, const json& args) {
    requireEdit(ctx);
    if (!args.contains("path")) fail("missing 'path'");
    if (!ctx.resources) fail("no resources bound");
    SandboxedPathResult modelPath = resolveToolPath(ctx, args["path"].get<std::string>());
    if (!fs::exists(modelPath.absolute)) fail("model not found: " + modelPath.absolute);

    auto node = std::make_unique<Node>(args.value("name", fs::path(modelPath.relative).stem().string()));
    node->setImportedFromPath(modelPath.relative);
    GLTFLoadOptions opts;
    opts.autoMeshLods = args.value("lod", true);  // autoLOD by default
    if (!GLTFLoader::load(modelPath.absolute, *node, *ctx.resources, opts))
        fail("failed to import model: " + modelPath.absolute);
    node->regenerateId();
    NodeId id = node->id();

    NodeId parent = args.contains("parent") ? parseNodeId(args["parent"]) : ctx.scene->id();
    if (!ctx.doc->find(parent)) fail("no parent node with id " + idStr(parent));
    ctx.exec(std::make_unique<AddNodeCommand>(parent, std::move(node)));
    return {{"id", idStr(id)}};
}

json toolAuthoringGuide(const ToolCtx&, const json&) {
    static const char* guide =
        "SaidaEngine authoring. One paradigm: nodes + behaviours + signals.\n"
        "RULES: 1) all logic is a Behaviour (no manager classes). 2) compose small\n"
        "focused behaviours, don't grow god-classes. 3) call down, signal up. 4) no\n"
        "globals except services/autoloads (Blackboard). 5) find nodes by group or\n"
        "scoped query, never by global name.\n\n"
        "WORKFLOW:\n"
        "- describe_api {summary:true} to list types; then {type:'X'} for one type's\n"
        "  properties/signals/slots. Cache by 'hash'.\n"
        "- Build scenes with create_node / set_property / set_transform / add_to_group.\n"
        "- Behaviour: add_behaviour, then set_property (scalars) or configure_behaviour\n"
        "  (full JSON: StateMachine/Blackboard/script props).\n"
        "- Wire events: connect_signal {from,signal,to,slot} (reflected names).\n"
        "- Scenario flows: use create_scenario / update_scenario_step /\n"
        "  validate_scenario / attach_scenario. Do not write ScriptBehaviour code\n"
        "  to orchestrate tutorials, quests, puzzles, waves, cinematics, boss phases\n"
        "  or missions; scripts are only for reusable local capabilities.\n"
        "- Code: write_script (JS, fast iteration) or write_cpp_behaviour + build (perf).\n"
        "  UI: write_ui (HTML/RML/CSS).\n"
        "- Validate: run_headless_check (scripts), read_logs. Maps: import_model.\n"
        "- Complex behaviour: see list_recipes (NPC, trigger light, scripted sequence).\n"
        "All edits are undoable and rejected during Play.";
    return {{"guide", guide}};
}

json toolListRecipes(const ToolCtx&, const json&) {
    return json::array({
        {{"name", "npc_patrol_chase"},
         {"summary", "An NPC that patrols, sees the player via a trigger volume, and chases."},
         {"steps", json::array({
             "create_node CharacterBody 'NPC'",
             "add_behaviour Character on the NPC (movement)",
             "create_node Area 'Vision' as a child (perception volume) + a CollisionShape child",
             "add_behaviour StateMachine on the NPC",
             "configure_behaviour StateMachine {initialState:'patrol', states:['patrol','chase'], transitions:[{from:'patrol',to:'chase',when:{key:'sawPlayer',op:'==',value:1}},{from:'chase',to:'patrol',after:3}]}",
             "create a Blackboard node, add it to group 'blackboard'",
             "connect_signal Vision.bodyEntered -> (a script slot or set blackboard sawPlayer=1)"})}},
        {{"name", "trigger_light"},
         {"summary", "A light that turns on when the player enters an area."},
         {"steps", json::array({
             "create_node LightNode 'Lamp' (set intensity 0 via set_property)",
             "create_node Area 'Switch' + CollisionShape child",
             "write_script a small JS that listens and sets the light intensity, attachTo the Area",
             "or connect_signal Switch.bodyEntered -> a behaviour slot that enables the light"})}},
        {{"name", "scripted_sequence"},
         {"summary", "A generic declarative scenario sequence; prefer this over custom script orchestration."},
         {"steps", json::array({
             "create_scenario {path:'scenarios/intro.saidascenario', scenario:{version:1,id:'intro',roles:{player:{group:'player',required:true}},blackboard:{},steps:[{id:'start',enter:[{'objective.show':{text:'Reach the marker'}}],wait:{'area.entered':{area:'marker',by:'player'}},next:'done'},{id:'done',enter:[{'objective.complete':{}}],end:'success'}]}}",
             "attach_scenario the created .saidascenario to a node with ScenarioRunner",
             "validate_scenario before saving or continuing"})}},
        {{"name", "enemy_wave"},
         {"summary", "Spawn a wave and complete when the enemy group is empty."},
         {"steps", json::array({
             "place a ScenarioAnchor with key 'wave_spawn'",
             "create_scenario with scene.instantiate actions for enemy prefabs at 'wave_spawn'",
             "wait on {'group.count':{group:'enemy',op:'==',value:0}}",
             "end success"})}},
        {{"name", "boss_phases"},
         {"summary", "Drive boss phases with blackboard and transitions."},
         {"steps", json::array({
             "boss local behaviours expose health/combat capabilities",
             "scenario transitions watch blackboard.equals or signal.received",
             "enter actions call slots or emit signals to switch phase behaviours",
             "never put the phase graph inside the boss onUpdate"})}}
    });
}

json tool(const char* name, const char* desc, json schema) {
    return {{"name", name}, {"description", desc}, {"inputSchema", std::move(schema)}};
}
json obj(json props, std::vector<std::string> required = {}) {
    json s{{"type", "object"}, {"properties", std::move(props)}};
    if (!required.empty()) s["required"] = required;
    return s;
}
json str() { return {{"type", "string"}}; }

json toolList() {
    json tools = json::array();
    tools.push_back(tool("authoring_guide",
        "Authoring contract and tool workflow.", obj({})));
    tools.push_back(tool("describe_api",
        "Reflected type manifest. {summary:true} lists names only (cheap); {type:'X'} one type; {category:'behaviour'|'node'} filters. Cache by returned 'hash'.",
        obj({{"summary", json{{"type", "boolean"}}}, {"category", str()}, {"type", str()}})));
    tools.push_back(tool("list_node_types", "Names of all instantiable node types.", obj({})));
    tools.push_back(tool("list_behaviour_types", "Names of all attachable behaviour types.", obj({})));
    tools.push_back(tool("get_scene", "Compact tree of the current scene (ids, names, types, behaviours, connections).", obj({})));
    tools.push_back(tool("get_node", "Full JSON of one node (all serialized fields).", obj({{"id", str()}}, {"id"})));
    tools.push_back(tool("find_nodes", "Find nodes by {group} and/or {type}; returns id/name/type.", obj({{"group", str()}, {"type", str()}})));
    tools.push_back(tool("create_node", "Create a node under {parent} (default: scene root). Returns the new id.",
        obj({{"type", str()}, {"name", str()}, {"parent", str()}}, {"type"})));
    tools.push_back(tool("delete_node", "Delete a node (and its subtree).", obj({{"id", str()}}, {"id"})));
    tools.push_back(tool("rename_node", "Rename a node.", obj({{"id", str()}, {"name", str()}}, {"id", "name"})));
    tools.push_back(tool("reparent_node", "Move a node under a new parent.", obj({{"id", str()}, {"parent", str()}}, {"id", "parent"})));
    tools.push_back(tool("set_transform", "Set position/rotation(quat xyzw)/scale on a node.",
        obj({{"id", str()}, {"position", json{{"type", "array"}}}, {"rotation", json{{"type", "array"}}}, {"scale", json{{"type", "array"}}}}, {"id"})));
    tools.push_back(tool("add_behaviour", "Attach a behaviour type to a node.", obj({{"id", str()}, {"type", str()}}, {"id", "type"})));
    tools.push_back(tool("set_property",
        "Set a reflected property. Omit 'behaviour' for a node property, or pass the behaviour type name to target a behaviour.",
        obj({{"id", str()}, {"behaviour", str()}, {"name", str()}, {"value", json{{"type", {"number", "string", "boolean", "array"}}}}}, {"id", "name", "value"})));
    tools.push_back(tool("add_to_group", "Add a node to a group tag.", obj({{"id", str()}, {"group", str()}}, {"id", "group"})));
    tools.push_back(tool("remove_from_group", "Remove a node from a group tag.", obj({{"id", str()}, {"group", str()}}, {"id", "group"})));
    tools.push_back(tool("connect_signal", "Wire a reflected signal on {from} to a reflected slot on {to}.",
        obj({{"from", str()}, {"signal", str()}, {"to", str()}, {"slot", str()}}, {"from", "signal", "to", "slot"})));
    tools.push_back(tool("set_scene_settings",
        "Set scene-wide rendering (ambient, clearColor, gi*, fog*, bloom*, aoEnabled, skyboxExposure).", obj({})));
    tools.push_back(tool("write_script",
        "Write a JS/.mjs gameplay script (hot-reloaded). Path is project-relative (defaults under scripts/). Optionally {attachTo:<nodeId>} to add a ScriptBehaviour.",
        obj({{"path", str()}, {"code", str()}, {"attachTo", str()}}, {"path", "code"})));
    tools.push_back(tool("write_ui",
        "Write an HTML/RML/CSS file for a WebCanvasNode (hot-reloaded). Path is project-relative.",
        obj({{"path", str()}, {"code", str()}}, {"path", "code"})));
    tools.push_back(tool("list_scenario_actions",
        "List the only valid declarative scenario action keys. LLMs must not invent actions.", obj({})));
    tools.push_back(tool("list_scenario_conditions",
        "List the only valid declarative scenario condition keys. LLMs must not invent conditions.", obj({})));
    tools.push_back(tool("create_scenario",
        "Create a .saidascenario JSON asset. {path, scenario?}. Validates before writing.",
        obj({{"path", str()}, {"scenario", json{{"type", "object"}}}}, {"path"})));
    tools.push_back(tool("validate_scenario",
        "Validate a .saidascenario asset with strict action/condition/schema checks.",
        obj({{"path", str()}}, {"path"})));
    tools.push_back(tool("get_scenario",
        "Read a .saidascenario asset as JSON.", obj({{"path", str()}}, {"path"})));
    tools.push_back(tool("update_scenario_step",
        "Replace or append one scenario step in a .saidascenario, validating before writing.",
        obj({{"path", str()}, {"step", json{{"type", "object"}}}}, {"path", "step"})));
    tools.push_back(tool("attach_scenario",
        "Attach a ScenarioRunner to a node and point it at a .saidascenario.",
        obj({{"id", str()}, {"path", str()}, {"autoStart", json{{"type", "boolean"}}}}, {"id", "path"})));
    tools.push_back(tool("write_cpp_behaviour",
        "Scaffold a reflected C++ behaviour (perf path). {name, doc?, properties:[{name,type,default,min,max,tooltip}], onReady?, onUpdate?}. Then call 'build'.",
        obj({{"name", str()}, {"doc", str()}, {"properties", json{{"type", "array"}}}, {"onReady", str()}, {"onUpdate", str()}}, {"name"})));
    tools.push_back(tool("build",
        "Compile the engine (picks up new C++ behaviours). Returns ok + compiler output.", obj({})));
    tools.push_back(tool("run_headless_check",
        "Validate the current scene: compile every attached script (no GPU). Returns ok + per-script errors.", obj({})));
    tools.push_back(tool("read_logs",
        "Recent engine log lines (newest last).", obj({{"count", json{{"type", "number"}}}})));
    tools.push_back(tool("configure_behaviour",
        "Push a full serialized config into a behaviour (StateMachine/Blackboard/ScriptBehaviour). {id, behaviour:'<TypeName>', data:{...}}.",
        obj({{"id", str()}, {"behaviour", str()}, {"data", json{{"type", "object"}}}}, {"id", "behaviour", "data"})));
    tools.push_back(tool("list_recipes",
        "Curated composition recipes for complex things (NPC, trigger light, scripted sequence).", obj({})));
    tools.push_back(tool("import_model",
        "Import a .glb/.gltf as a node subtree (autoLOD on by default). {path, parent?, name?, lod?}.",
        obj({{"path", str()}, {"parent", str()}, {"name", str()}, {"lod", json{{"type", "boolean"}}}}, {"path"})));
    return tools;
}

} // namespace

McpBridge::McpBridge() : server_(std::make_unique<McpServer>()) {}
McpBridge::~McpBridge() = default;

bool McpBridge::start(uint16_t port) { return server_->start(port); }
bool McpBridge::running() const { return server_ && server_->running(); }

void McpBridge::poll(EditorUI& ui) {
    if (!server_) return;
    server_->poll([this, &ui](const std::string& method, const json& params) {
        return dispatch(ui, method, params);
    });
}

json McpBridge::dispatch(EditorUI& ui, const std::string& method, const json& params) {
    if (method == "initialize") {
        return {{"protocolVersion", "2024-11-05"},
                {"capabilities", {{"tools", json::object()}}},
                {"serverInfo", {{"name", "SaidaEngine"}, {"version", "0.1"}}}};
    }
    if (method == "notifications/initialized" || method == "ping") return json::object();
    if (method == "tools/list") return {{"tools", toolList()}};
    if (method == "tools/call") {
        std::string name = params.value("name", std::string());
        json args = params.contains("arguments") ? params["arguments"] : json::object();
        json result = callTool(ui, name, args);
        // MCP wraps tool output as content items; we return the JSON as text.
        return {{"content", json::array({{{"type", "text"}, {"text", result.dump(2)}}})}};
    }
    throw std::runtime_error("unknown method '" + method + "'");
}

json McpBridge::callTool(EditorUI& ui, const std::string& name, const json& args) {
    ToolCtx ctx;
    ctx.doc = &ui.document_;
    ctx.scene = ui.document_.scene();
    ctx.resources = ui.ctxResources_;
    ctx.project = ui.ctxProject_;
    ctx.canEdit = ui.canEdit();
    ctx.exec = [&ui](std::unique_ptr<Command> c) { ui.execute(std::move(c)); };

    if (name == "describe_api") return toolDescribeApi(ctx, args);
    if (name == "list_node_types") return toolListNodeTypes(ctx, args);
    if (name == "list_behaviour_types") return toolListBehaviourTypes(ctx, args);
    if (name == "get_scene") return toolGetScene(ctx, args);
    if (name == "get_node") return toolGetNode(ctx, args);
    if (name == "find_nodes") return toolFindNodes(ctx, args);
    if (name == "create_node") return toolCreateNode(ctx, args);
    if (name == "delete_node") return toolDeleteNode(ctx, args);
    if (name == "rename_node") return toolRenameNode(ctx, args);
    if (name == "reparent_node") return toolReparent(ctx, args);
    if (name == "set_transform") return toolSetTransform(ctx, args);
    if (name == "add_behaviour") return toolAddBehaviour(ctx, args);
    if (name == "set_property") return toolSetProperty(ctx, args);
    if (name == "add_to_group") return toolGroup(ctx, args, true);
    if (name == "remove_from_group") return toolGroup(ctx, args, false);
    if (name == "connect_signal") return toolConnectSignal(ctx, args);
    if (name == "set_scene_settings") return toolSetSceneSettings(ctx, args);
    if (name == "write_script") return toolWriteScript(ctx, args);
    if (name == "write_ui") return toolWriteUi(ctx, args);
    if (name == "list_scenario_actions") return toolListScenarioActions(ctx, args);
    if (name == "list_scenario_conditions") return toolListScenarioConditions(ctx, args);
    if (name == "create_scenario") return toolCreateScenario(ctx, args);
    if (name == "validate_scenario") return toolValidateScenario(ctx, args);
    if (name == "get_scenario") return toolGetScenario(ctx, args);
    if (name == "update_scenario_step") return toolUpdateScenarioStep(ctx, args);
    if (name == "attach_scenario") return toolAttachScenario(ctx, args);
    if (name == "write_cpp_behaviour") return toolWriteCppBehaviour(ctx, args);
    if (name == "build") return toolBuild(ctx, args);
    if (name == "run_headless_check") return toolHeadlessCheck(ctx, args);
    if (name == "read_logs") return toolReadLogs(ctx, args);
    if (name == "configure_behaviour") return toolConfigureBehaviour(ctx, args);
    if (name == "list_recipes") return toolListRecipes(ctx, args);
    if (name == "import_model") return toolImportModel(ctx, args);
    if (name == "authoring_guide") return toolAuthoringGuide(ctx, args);
    throw std::runtime_error("unknown tool '" + name + "'");
}

} // namespace saida
