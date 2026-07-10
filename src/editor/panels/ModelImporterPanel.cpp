#include "editor/panels/ModelImporterPanel.hpp"
#include "editor/EditorUI.hpp"
#include "scene/Scene.hpp"
#include "scene/animation/Animator.hpp"
#include "scene/animation/AnimStateMachine.hpp"
#include "scene/animation/ClipNode.hpp"
#include "scene/animation/AnimationClip.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

namespace saida {

void ModelImporterPanel::draw(EditorUI* editor, Scene* previewScene, const std::string& modelPath) {
    if (!editor->showModelImporter_) return;

    bool open = editor->showModelImporter_;
    bool visible = ImGui::Begin("3D Importer", &open);
    bool closeRequested = !open;
    if (!visible) {
        ImGui::End();
        if (closeRequested) editor->closeModelImporter();
        return;
    }

    ImGui::Text("Previewing Model:");
    ImGui::TextDisabled("%s", modelPath.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    Animator* foundAnim = nullptr;
    if (previewScene) {
        previewScene->traverse([&](Node& node, const glm::mat4&) {
            if (!foundAnim && node.getBehaviour<Animator>()) {
                foundAnim = node.getBehaviour<Animator>();
            }
        });
    }

    if (foundAnim && !foundAnim->clips().empty()) {
        ImGui::Text("Animation Controls");
        ImGui::Spacing();

        static float speed = 1.0f;

        // Clip selector — stable order for a deterministic combo.
        std::vector<std::string> clipNames;
        clipNames.reserve(foundAnim->clips().size());
        for (const auto& [name, clip] : foundAnim->clips()) clipNames.push_back(name);
        std::sort(clipNames.begin(), clipNames.end());

        const std::string& current = foundAnim->currentClip();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Clip", current.empty() ? "(select a clip)" : current.c_str())) {
            for (const auto& name : clipNames) {
                if (ImGui::Selectable(name.c_str(), name == current)) {
                    foundAnim->play(name);
                    if (ClipNode* c = foundAnim->activeClipNode()) c->setPlaybackSpeed(speed);
                }
            }
            ImGui::EndCombo();
        }

        if (ClipNode* clipNode = foundAnim->activeClipNode()) {
            bool isPlaying = (speed > 0.0f);

            if (isPlaying) {
                if (ImGui::Button("Pause", ImVec2(80, 0))) {
                    speed = 0.0f;
                    clipNode->setPlaybackSpeed(speed);
                }
            } else {
                if (ImGui::Button("Play", ImVec2(80, 0))) {
                    speed = 1.0f;
                    clipNode->setPlaybackSpeed(speed);
                }
            }

            ImGui::SameLine();
            float currentTime = clipNode->time();
            float dur = clipNode->duration();
            if (dur == 0.0f) dur = 1.0f; // prevent division by zero in slider

            ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderFloat("##Time", &currentTime, 0.0f, dur, "Time: %.2fs")) {
                clipNode->setTime(currentTime);
            }

            ImGui::Spacing();
        } else if (foundAnim->rootNode()) {
            ImGui::TextDisabled("Animation uses a complex graph (not a simple clip).");
        }
    } else if (foundAnim && foundAnim->rootNode()) {
        ImGui::TextDisabled("Animation uses a complex graph (not a simple clip).");
    } else {
        ImGui::TextDisabled("No animation detected in this model.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Close Preview", ImVec2(150, 30))) {
        closeRequested = true;
    }

    ImGui::End();
    if (closeRequested) editor->closeModelImporter();
}

} // namespace saida
