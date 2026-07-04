#include "authoring/SaidaOp.hpp"

#include "authoring/EngineManifest.hpp"

#include <algorithm>

namespace saida::authoring {

using json = nlohmann::json;

const std::vector<std::string>& knownOpTypes() {
    static const std::vector<std::string> types = {
        "set_transform",
        "create_node",
        "delete_node",
        "rename_node",
        "reparent_node",
        "set_property",
        "set_scene_setting",
        "add_behaviour",
        "remove_behaviour",
        "set_behaviour_property",
    };
    return types;
}

bool isKnownOpType(const std::string& type) {
    const auto& types = knownOpTypes();
    return std::find(types.begin(), types.end(), type) != types.end();
}

json SaidaOp::toJson() const {
    json out;
    out["opVersion"] = opVersion;
    out["type"] = type;
    if (!sceneId.empty()) out["sceneId"] = sceneId;
    out["payload"] = payload.is_object() ? payload : json::object();
    return out;
}

SaidaOpParseResult parseSaidaOp(const json& j) {
    SaidaOpParseResult r;
    if (!j.is_object()) {
        r.error = "op must be a JSON object";
        return r;
    }

    if (j.contains("opVersion")) {
        if (!j["opVersion"].is_number_integer()) {
            r.error = "opVersion must be an integer";
            return r;
        }
        const int v = j["opVersion"].get<int>();
        if (v != kOpVersion) {
            r.error = "unsupported opVersion " + std::to_string(v) +
                      " (engine speaks " + std::to_string(kOpVersion) + ")";
            return r;
        }
        r.op.opVersion = v;
    } else {
        r.op.opVersion = kOpVersion;
    }

    if (!j.contains("type") || !j["type"].is_string()) {
        r.error = "op needs a string 'type'";
        return r;
    }
    r.op.type = j["type"].get<std::string>();
    if (!isKnownOpType(r.op.type)) {
        r.error = "unknown op type '" + r.op.type + "'";
        return r;
    }

    if (j.contains("sceneId")) {
        if (!j["sceneId"].is_string()) {
            r.error = "sceneId must be a string";
            return r;
        }
        r.op.sceneId = j["sceneId"].get<std::string>();
    }

    if (j.contains("payload")) {
        if (!j["payload"].is_object()) {
            r.error = "payload must be a JSON object";
            return r;
        }
        r.op.payload = j["payload"];
    } else {
        r.op.payload = json::object();
    }

    r.ok = true;
    return r;
}

namespace {

// Helpers de forme : presence + type JSON du champ dans le payload.
bool hasString(const json& p, const char* key) {
    return p.contains(key) && p[key].is_string();
}
bool hasNonEmptyString(const json& p, const char* key) {
    return hasString(p, key) && !p[key].get<std::string>().empty();
}
// Un vecteur de N nombres (position/scale = 3, rotation = 4).
bool isNumberArray(const json& v, std::size_t n) {
    if (!v.is_array() || v.size() != n) return false;
    for (const auto& e : v)
        if (!e.is_number()) return false;
    return true;
}

} // namespace

std::string validateOpShape(const SaidaOp& op) {
    if (!isKnownOpType(op.type)) return "unknown op type '" + op.type + "'";
    const json& p = op.payload;
    if (!p.is_object()) return "payload must be a JSON object";

    if (op.type == "set_transform") {
        if (!hasNonEmptyString(p, "nodeId")) return "set_transform needs 'nodeId'";
        if (!p.contains("position") && !p.contains("rotation") && !p.contains("scale"))
            return "set_transform needs at least one of position/rotation/scale";
        if (p.contains("position") && !isNumberArray(p["position"], 3))
            return "set_transform 'position' must be a 3-number array";
        if (p.contains("rotation") && !isNumberArray(p["rotation"], 4))
            return "set_transform 'rotation' must be a 4-number array";
        if (p.contains("scale") && !isNumberArray(p["scale"], 3))
            return "set_transform 'scale' must be a 3-number array";
        return "";
    }
    if (op.type == "create_node") {
        if (!hasNonEmptyString(p, "nodeType")) return "create_node needs 'nodeType'";
        if (p.contains("name") && !p["name"].is_string())
            return "create_node 'name' must be a string";
        if (p.contains("parent") && !p["parent"].is_string())
            return "create_node 'parent' must be a string";
        return "";
    }
    if (op.type == "delete_node") {
        if (!hasNonEmptyString(p, "nodeId")) return "delete_node needs 'nodeId'";
        return "";
    }
    if (op.type == "rename_node") {
        if (!hasNonEmptyString(p, "nodeId")) return "rename_node needs 'nodeId'";
        if (!hasNonEmptyString(p, "name")) return "rename_node needs a non-empty 'name'";
        return "";
    }
    if (op.type == "reparent_node") {
        if (!hasNonEmptyString(p, "nodeId")) return "reparent_node needs 'nodeId'";
        // newParent optionnel (vide = racine), mais s'il est present il est string.
        if (p.contains("newParent") && !p["newParent"].is_string())
            return "reparent_node 'newParent' must be a string";
        return "";
    }
    if (op.type == "set_property") {
        if (!hasNonEmptyString(p, "nodeId")) return "set_property needs 'nodeId'";
        if (!hasNonEmptyString(p, "property")) return "set_property needs 'property'";
        if (!p.contains("value")) return "set_property needs a 'value'";
        return "";
    }
    if (op.type == "set_scene_setting") {
        if (!hasNonEmptyString(p, "setting")) return "set_scene_setting needs 'setting'";
        if (!p.contains("value")) return "set_scene_setting needs a 'value'";
        return "";
    }
    if (op.type == "add_behaviour" || op.type == "remove_behaviour") {
        if (!hasNonEmptyString(p, "nodeId")) return op.type + " needs 'nodeId'";
        if (!hasNonEmptyString(p, "behaviourType")) return op.type + " needs 'behaviourType'";
        return "";
    }
    if (op.type == "set_behaviour_property") {
        if (!hasNonEmptyString(p, "nodeId")) return "set_behaviour_property needs 'nodeId'";
        if (!hasNonEmptyString(p, "behaviourType"))
            return "set_behaviour_property needs 'behaviourType'";
        if (!hasNonEmptyString(p, "property")) return "set_behaviour_property needs 'property'";
        if (!p.contains("value")) return "set_behaviour_property needs a 'value'";
        return "";
    }
    return "op type '" + op.type + "' has no shape validator";
}

SaidaOpParseResult parseSaidaOp(const std::string& text) {
    json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        SaidaOpParseResult r;
        r.error = "invalid JSON";
        return r;
    }
    return parseSaidaOp(j);
}

} // namespace saida::authoring
