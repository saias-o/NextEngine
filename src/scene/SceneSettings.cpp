#include "scene/SceneSettings.hpp"

#include "nlohmann/json.hpp"

#include <imgui.h>

namespace ne {

void SceneSettingsBehaviour::onDrawInspector() {
    if (ImGui::CollapsingHeader("Scene Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient Light", &ambientLight.x);
        ImGui::ColorEdit3("Clear Color (Fog/Sky)", &clearColor.x);
        ImGui::Checkbox("Enable Post Processing", &enablePostProcessing);
    }
}

void SceneSettingsBehaviour::save(nlohmann::json& j) const {
    j["ambient"] = {ambientLight.x, ambientLight.y, ambientLight.z};
    j["clearColor"] = {clearColor.x, clearColor.y, clearColor.z};
    j["postProcessing"] = enablePostProcessing;
}

void SceneSettingsBehaviour::load(const nlohmann::json& j) {
    if (auto it = j.find("ambient"); it != j.end() && it->is_array() && it->size() == 3)
        ambientLight = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), 0.0f};
    if (auto it = j.find("clearColor"); it != j.end() && it->is_array() && it->size() == 3)
        clearColor = {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>(), 1.0f};
    enablePostProcessing = j.value("postProcessing", true);
}

} // namespace ne
