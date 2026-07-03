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
