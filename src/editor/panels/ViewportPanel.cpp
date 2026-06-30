#include "editor/panels/ViewportPanel.hpp"
#include "editor/EditorUI.hpp"
#include "editor/EditorApp.hpp"
#include "core/Camera.hpp"

#include <imgui.h>

namespace saida {

void ViewportPanel::draw(EditorUI* editor, Camera* camera, float dt) {
    if (!editor->showViewportOverlay_) return;

    ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
                                  | ImGuiWindowFlags_NoDocking
                                  | ImGuiWindowFlags_AlwaysAutoResize
                                  | ImGuiWindowFlags_NoSavedSettings
                                  | ImGuiWindowFlags_NoFocusOnAppearing
                                  | ImGuiWindowFlags_NoNav
                                  | ImGuiWindowFlags_NoMove;

    ImVec2 pos(editor->viewportPos_.x + 16.0f, editor->viewportPos_.y + 16.0f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.60f);

    if (ImGui::Begin("##ViewportOverlay", nullptr, overlayFlags)) {
        if (editor->app_ && editor->app_->isPlayMode())
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "PLAY MODE");
        else
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "SCENE MODE");

        const float fps = dt > 0.0f ? 1.0f / dt : 0.0f;
        ImGui::Text("%.1f FPS", fps);

        if (camera) {
            ImGui::Text("Cam: %.1f %.1f %.1f",
                camera->position.x, camera->position.y, camera->position.z);
        }
        ImGui::TextDisabled("TAB: toggle cursor");
    }
    ImGui::End();

    ImVec2 toolbarPos(editor->viewportPos_.x + 16.0f, editor->viewportPos_.y + editor->viewportSize_.y * 0.5f - 60.0f);
    ImGui::SetNextWindowPos(toolbarPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.60f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    if (ImGui::Begin("##Toolbar", nullptr, overlayFlags)) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.290f, 0.565f, 0.851f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.350f, 0.620f, 0.900f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.250f, 0.500f, 0.800f, 1.0f));
        
        if (ImGui::Selectable(" T ", editor->gizmoMode_ == GizmoMode::Translate, 0, ImVec2(24, 24))) editor->gizmoMode_ = GizmoMode::Translate;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate (T)");
        if (ImGui::Selectable(" R ", editor->gizmoMode_ == GizmoMode::Rotate, 0, ImVec2(24, 24))) editor->gizmoMode_ = GizmoMode::Rotate;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate (R)");
        if (ImGui::Selectable(" S ", editor->gizmoMode_ == GizmoMode::Scale, 0, ImVec2(24, 24))) editor->gizmoMode_ = GizmoMode::Scale;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale (S)");
        
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace saida
