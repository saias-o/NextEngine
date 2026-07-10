#include "scene/animation/RetargetProfile.hpp"

#include "scene/animation/AnimationClip.hpp"
#include "scene/animation/Rig.hpp"

#include <fstream>
#include <unordered_set>

namespace saida {

namespace {

using json = nlohmann::json;

AssetDiagnostic error(std::string code, std::string path, std::string message) {
    return {std::move(code), AssetDiagnostic::Severity::Error,
            std::move(path), std::move(message)};
}

AssetDiagnostic warning(std::string code, std::string path, std::string message) {
    return {std::move(code), AssetDiagnostic::Severity::Warning,
            std::move(path), std::move(message)};
}

} // namespace

RetargetProfileParseResult RetargetProfile::parse(const nlohmann::json& j) {
    RetargetProfileParseResult result;
    auto& diags = result.diagnostics;

    if (!j.is_object()) {
        diags.push_back(error("retarget.root.not_object", "", "document must be a JSON object"));
        return result;
    }

    const int schema = j.value("schema", 0);
    if (schema <= 0) {
        diags.push_back(error("retarget.schema.missing", "/schema",
                              "'schema' must be a positive integer"));
        return result;
    }
    if (schema > kRetargetProfileSchema) {
        diags.push_back(error("retarget.schema.newer", "/schema",
                              "schema " + std::to_string(schema) + " is newer than supported " +
                                  std::to_string(kRetargetProfileSchema)));
        return result;
    }

    RetargetProfile& p = result.profile;
    p.name = j.value("name", "");

    if (!j.contains("map") || !j["map"].is_object()) {
        diags.push_back(error("retarget.map.missing", "/map",
                              "'map' must be an object {rigBone: clipTrack}"));
        return result;
    }
    for (auto it = j["map"].begin(); it != j["map"].end(); ++it) {
        if (!it.value().is_string() || it.value().get<std::string>().empty()) {
            diags.push_back(error("retarget.map.malformed", "/map/" + it.key(),
                                  "mapping must be a non-empty source track name"));
            continue;
        }
        p.entries.emplace_back(it.key(), it.value().get<std::string>());
    }

    result.ok = !hasErrors(diags);
    return result;
}

RetargetProfileParseResult RetargetProfile::loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        RetargetProfileParseResult result;
        result.diagnostics.push_back(error("retarget.io.open", "", "cannot open " + path));
        return result;
    }
    json j = json::parse(file, nullptr, /*allow_exceptions=*/false);
    if (j.is_discarded()) {
        RetargetProfileParseResult result;
        result.diagnostics.push_back(error("retarget.io.json", "", path + " is not valid JSON"));
        return result;
    }
    return parse(j);
}

nlohmann::json RetargetProfile::toJson() const {
    json map = json::object();
    for (const auto& [rigBone, clipTrack] : entries) map[rigBone] = clipTrack;
    return {{"schema", kRetargetProfileSchema}, {"name", name}, {"map", map}};
}

bool RetargetProfile::saveFile(const std::string& path) const {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) return false;
    file << toJson().dump(1) << "\n";
    return file.good();
}

std::vector<AssetDiagnostic> RetargetProfile::validate(const Rig* targetRig,
                                                       const AnimationClip* sourceClip) const {
    std::vector<AssetDiagnostic> diags;

    std::unordered_set<std::string> seen;
    for (const auto& [rigBone, clipTrack] : entries) {
        if (!seen.insert(rigBone).second)
            diags.push_back(error("retarget.map.duplicate", "/map/" + rigBone,
                                  "duplicate mapping for rig bone '" + rigBone + "'"));
        if (targetRig && targetRig->findBoneIndex(rigBone) < 0)
            diags.push_back(error("retarget.map.unknown_bone", "/map/" + rigBone,
                                  "rig has no bone named '" + rigBone + "'"));
        if (sourceClip && !sourceClip->getTracks(clipTrack))
            diags.push_back(error("retarget.map.unknown_track", "/map/" + rigBone,
                                  "source clip has no track '" + clipTrack + "'"));
    }

    // Couverture : os du rig sans piste (ni directe, ni mappée) — warning §8.1.
    if (targetRig && sourceClip) {
        const RetargetMap map = toRetargetMap();
        for (const auto& bone : targetRig->bones()) {
            if (!sourceClip->getTracks(map.resolve(bone.name)))
                diags.push_back(warning("retarget.coverage.unmapped_bone", "/map",
                                        "rig bone '" + bone.name +
                                            "' has no source track (will hold rest pose)"));
        }
    }

    return diags;
}

RetargetMap RetargetProfile::toRetargetMap() const {
    RetargetMap map;
    for (const auto& [rigBone, clipTrack] : entries) map.set(rigBone, clipTrack);
    return map;
}

RetargetProfile RetargetProfile::fromAutoMap(const Rig& targetRig,
                                             const AnimationClip& sourceClip) {
    std::vector<std::string> rigBones;
    rigBones.reserve(targetRig.boneCount());
    for (const auto& bone : targetRig.bones()) rigBones.push_back(bone.name);

    const RetargetMap suggested = RetargetMap::autoMap(rigBones, sourceClip.boneNames());

    RetargetProfile profile;
    for (const auto& bone : rigBones) {
        const std::string& track = suggested.resolve(bone);
        if (track != bone) profile.entries.emplace_back(bone, track);
    }
    return profile;
}

} // namespace saida
