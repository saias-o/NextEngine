#pragma once

#include <nlohmann/json.hpp>

namespace saida::format {

constexpr int kLegacyVersion = 0;
constexpr int kBootManifestVersion = 1;
constexpr int kProjectVersion = 1;
constexpr int kAssetRegistryVersion = 1;
constexpr int kScenarioVersion = 1;
constexpr int kSceneVersion = 2;

inline int readVersion(const nlohmann::json& doc, int fallback = kLegacyVersion) {
    if (!doc.is_object()) return fallback;
    auto it = doc.find("version");
    if (it == doc.end() || !it->is_number_integer()) return fallback;
    return it->get<int>();
}

inline bool hasIntegerVersion(const nlohmann::json& doc) {
    return doc.is_object() && doc.contains("version") && doc["version"].is_number_integer();
}

inline int readSchema(const nlohmann::json& doc, int fallback = kLegacyVersion) {
    if (!doc.is_object()) return fallback;
    auto schema = doc.find("schema");
    if (schema != doc.end() && schema->is_number_integer())
        return schema->get<int>();
    return readVersion(doc, fallback);
}

inline bool hasIntegerSchema(const nlohmann::json& doc) {
    return doc.is_object() && doc.contains("schema") && doc["schema"].is_number_integer();
}

inline void writeSchema(nlohmann::json& doc, int schema) {
    doc["schema"] = schema;
    doc["version"] = schema;
}

} // namespace saida::format
