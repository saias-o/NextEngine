#pragma once

#include <nlohmann/json.hpp>

namespace saida::format {

constexpr int kLegacyVersion = 0;
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

} // namespace saida::format
