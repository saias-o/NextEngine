#include "scene/SceneSettings.hpp"

#include <imgui.h>

namespace ne {

void SceneSettingsBehaviour::onDrawInspector() {
    if (ImGui::CollapsingHeader("Scene Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient Light", &ambientLight.x);
        ImGui::ColorEdit3("Clear Color (Fog/Sky)", &clearColor.x);
        ImGui::Checkbox("Enable Post Processing", &enablePostProcessing);
    }
}

} // namespace ne
