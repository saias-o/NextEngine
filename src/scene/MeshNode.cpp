#include "scene/MeshNode.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Material.hpp"
#include <nlohmann/json.hpp>

namespace ne {

void MeshNode::serialize(nlohmann::json& j, ResourceManager& resources) const {
    Node::serialize(j, resources);
    j["mesh"] = resources.meshId(mesh_);
    if (material_) {
        j["texture"] = material_->desc().albedoId;
        j["baseColor"] = {material_->desc().baseColor.r, material_->desc().baseColor.g,
                          material_->desc().baseColor.b, material_->desc().baseColor.a};
    }
    j["castShadows"] = castShadows_;
    j["includeInLightBaking"] = includeInLightBaking_;
    j["meshEnabled"] = meshEnabled_;
}

void MeshNode::deserialize(const nlohmann::json& j, ResourceManager& resources) {
    Node::deserialize(j, resources);
    if (j.contains("mesh")) {
        AssetID meshId = kAssetInvalid;
        if (j["mesh"].is_number_integer()) {
            meshId = j["mesh"].get<AssetID>();
        } else if (j["mesh"].is_string()) {
            std::string meshStr = j["mesh"].get<std::string>();
            if (meshStr == "cube") {
                meshId = kAssetBuiltinCube;
            } else {
                meshId = resources.getOrRegister(meshStr, AssetType::Mesh);
            }
        }
        if (meshId != kAssetInvalid) {
            mesh_ = resources.getMesh(meshId);
        }
    }
    if (j.contains("texture") || j.contains("baseColor")) {
        MaterialDesc desc{};
        if (j.contains("texture")) {
            if (j["texture"].is_number_integer()) {
                desc.albedoId = j["texture"].get<AssetID>();
            } else if (j["texture"].is_string()) {
                desc.albedoId = resources.getOrRegister(j["texture"].get<std::string>(), AssetType::Texture);
            }
        }
        if (j.contains("baseColor") && j["baseColor"].is_array() && j["baseColor"].size() == 4) {
            desc.baseColor = {j["baseColor"][0].get<float>(), j["baseColor"][1].get<float>(),
                              j["baseColor"][2].get<float>(), j["baseColor"][3].get<float>()};
        }
        material_ = resources.getMaterial(desc);
    }
    if (j.contains("castShadows")) castShadows_ = j["castShadows"].get<bool>();
    if (j.contains("includeInLightBaking")) includeInLightBaking_ = j["includeInLightBaking"].get<bool>();
    if (j.contains("meshEnabled")) meshEnabled_ = j["meshEnabled"].get<bool>();
}

} // namespace ne
