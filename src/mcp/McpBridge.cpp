#include "mcp/McpBridge.hpp"

#include "core/Log.hpp"
#include "core/Reflection.hpp"
#include "editor/Command.hpp"
#include "editor/EditorUI.hpp"
#include "editor/SceneDocument.hpp"
#include "graphics/ResourceManager.hpp"
#include "mcp/McpServer.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/Node.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "scene/SignalWiring.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <vector>

namespace ne {

using json = nlohmann::json;

namespace {

// Editor context handed to every tool. `exec` routes through the undoable
// command chokepoint; `canEdit` is false during Play (mutations rejected).
struct ToolCtx {
    Scene* scene = nullptr;
    ResourceManager* resources = nullptr;
    SceneDocument* doc = nullptr;
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

// ── compact scene view (token-efficient) ─────────────────────────────────────
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

// ── reflection helpers for set_property ──────────────────────────────────────
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

// ── tools ────────────────────────────────────────────────────────────────────
json toolDescribeApi(const ToolCtx&, const json& args) {
    if (args.contains("type"))
        return reflect::TypeRegistry::instance().manifestFor(args["type"].get<std::string>());
    return reflect::TypeRegistry::instance().manifest(args.value("category", std::string()));
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

// ── tool schema list (kept concise; descriptions guide the LLM) ──────────────
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
    tools.push_back(tool("describe_api",
        "Reflected type manifest (properties, signals, slots). Filter with {category:'behaviour'|'node'} or {type:'<TypeName>'}.",
        obj({{"category", str()}, {"type", str()}})));
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
                {"serverInfo", {{"name", "NextEngine"}, {"version", "0.1"}}}};
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
    throw std::runtime_error("unknown tool '" + name + "'");
}

} // namespace ne
