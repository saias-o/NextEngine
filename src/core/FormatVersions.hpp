#pragma once

#include <nlohmann/json.hpp>

#include <string>

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

// Validates the schema/version envelope shared by every durable JSON format.
// Returns an empty string when the envelope is acceptable, otherwise an
// actionable diagnostic naming `label` (e.g. "snapshot", "scene", "project").
// Rules, in order:
//   - `schema` and `version`, when present, must be integers;
//   - when both are present they must agree — writeSchema always keeps them
//     equal, so a divergence marks a tampered or non-conforming document;
//   - the resolved version must not exceed `current`.
// Older or schema-less documents are accepted here; each loader migrates them.
inline std::string schemaEnvelopeError(const nlohmann::json& doc, int current,
                                       const char* label) {
    const std::string name = label;
    if (!doc.is_object()) return name + " document must be a JSON object";
    const auto schemaIt = doc.find("schema");
    const auto versionIt = doc.find("version");
    const bool hasSchema = schemaIt != doc.end();
    const bool hasVersion = versionIt != doc.end();
    if (hasSchema && !schemaIt->is_number_integer())
        return name + " schema must be an integer";
    if (hasVersion && !versionIt->is_number_integer())
        return name + " version must be an integer";
    if (hasSchema && hasVersion && schemaIt->get<int>() != versionIt->get<int>())
        return name + " schema/version mismatch (schema " +
               std::to_string(schemaIt->get<int>()) + ", version " +
               std::to_string(versionIt->get<int>()) + ")";
    const int resolved = readSchema(doc, kLegacyVersion);
    if (resolved > current)
        return "unsupported " + name + " schema v" + std::to_string(resolved) +
               " (supported through v" + std::to_string(current) + ")";
    return "";
}

} // namespace saida::format
