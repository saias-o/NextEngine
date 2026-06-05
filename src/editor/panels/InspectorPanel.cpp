#include "editor/panels/InspectorPanel.hpp"
#include "editor/EditorUI.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "graphics/ResourceManager.hpp"
#include "project/Project.hpp"
#include "project/AssetRegistry.hpp"

#include <imgui.h>

namespace ne {

void InspectorPanel::draw(EditorUI* editor) {
    ImGui::Begin("Inspector", &editor->showInspector_);

    if (!editor->selectedNode_) {
        ImGui::TextDisabled("Select a node in the Scene Tree");
        ImGui::End();
        return;
    }

    Node* node = editor->selectedNode_;

    ImGui::SeparatorText("Node");
    
    bool enabled = node->enabled();
    if (ImGui::Checkbox("##NodeEnabled", &enabled)) {
        node->setEnabled(enabled);
    }
    ImGui::SameLine();
    ImGui::Text("Name: %s", node->name().c_str());
    
    const char* typeLabel = "Node";
    if (node->asLightConst()) typeLabel = "LightNode";
    else if (node->mesh()) typeLabel = "MeshNode";
    
    ImGui::Text("Type: %s", typeLabel);

    if (Scene* sceneNode = dynamic_cast<Scene*>(node)) {
        SceneSettings& s = sceneNode->settings();
        ImGui::SeparatorText("Scene Settings");
        ImGui::ColorEdit3("Ambient Light", &s.ambientLight.x);
        ImGui::ColorEdit3("Clear Color", &s.clearColor.x);
        ImGui::Checkbox("Post Processing", &s.enablePostProcessing);

        ImGui::SeparatorText("Lighting");
        int mode = static_cast<int>(s.lightingMode);
        if (ImGui::RadioButton("Realtime", &mode, static_cast<int>(LightingMode::Realtime)))
            s.lightingMode = LightingMode::Realtime;
        ImGui::SameLine();
        if (ImGui::RadioButton("Baked", &mode, static_cast<int>(LightingMode::Baked)))
            s.lightingMode = LightingMode::Baked;

        if (s.lightingMode == LightingMode::Realtime) {
            ImGui::TextDisabled("Lighting & shadows computed live; baking disabled.");
        } else {
            // The renderer performs the GPU bake next frame (it owns the lights,
            // shadow maps and lightmaps); we just raise the request flag here.
            if (ImGui::Button("Generate Bake"))
                s.bakeRequested = true;
            ImGui::SameLine();
            ImGui::TextDisabled(s.baked ? "Bake: ready" : "Bake: none");
        }
    }

    ImGui::SeparatorText("Transform");
    {
        Transform& t = node->transform();
        ImGui::DragFloat3("Position", &t.position.x, 0.05f);

        glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
        if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
            t.rotation = glm::quat(glm::radians(euler));

        ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);
    }

    if (auto* meshNode = dynamic_cast<MeshNode*>(node)) {
        ImGui::SeparatorText("Mesh");

        ImGui::Text("Mesh: %s", meshNode->mesh() ? "assigned" : "None");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                AssetID id = *(AssetID*)payload->Data;
                // We should ideally check if it's a mesh asset via registry
                Mesh* newMesh = editor->ctxResources_->getMesh(id);
                if (newMesh) meshNode->setMesh(newMesh);
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Text("Material: %s", meshNode->material() ? "assigned" : "None");
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                AssetID id = *(AssetID*)payload->Data;
                // We assume texture dropping creates a default white material for now
                MaterialDesc desc;
                desc.albedoId = id;
                desc.baseColor = glm::vec4(1.0f);
                Material* newMat = editor->ctxResources_->getMaterial(desc);
                if (newMat) meshNode->setMaterial(newMat);
            }
            ImGui::EndDragDropTarget();
        }
            
        ImGui::Spacing();
        ImGui::Checkbox("Cast Shadows", &meshNode->castShadows());
        ImGui::Checkbox("Include to light baking", &meshNode->includeInLightBaking());
    }

    LightNode* light = node->asLight();
    if (light) {
        ImGui::SeparatorText("Light");

        const char* ltype = light->type == LightType::Directional ? "Directional"
                          : light->type == LightType::Point       ? "Point"
                                                                  : "Spot";
        ImGui::Text("Type: %s", ltype);
        ImGui::ColorEdit3("Color", &light->color.x);
        ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 20.0f);

        // Directional & spot point along `direction`; point & spot have a range.
        if (light->type == LightType::Directional || light->type == LightType::Spot)
            ImGui::SliderFloat3("Direction", &light->direction.x, -1.0f, 1.0f);
        if (light->type == LightType::Point || light->type == LightType::Spot)
            ImGui::DragFloat("Range", &light->range, 0.1f, 0.1f, 100.0f);

        if (light->type == LightType::Spot) {
            ImGui::DragFloat("Inner Angle", &light->spotInnerAngle, 0.5f, 1.0f, 89.0f);
            ImGui::DragFloat("Outer Angle", &light->spotOuterAngle, 0.5f, 1.0f, 89.0f);
            if (light->spotInnerAngle > light->spotOuterAngle)
                light->spotInnerAngle = light->spotOuterAngle;
        }

        // Only directional & spot render shadow maps (point would need a cube map).
        if (light->type != LightType::Point)
            ImGui::Checkbox("Cast Shadows", &light->castShadows);
    }

    if (node->hasBehaviours()) {
        ImGui::SeparatorText("Behaviours");
        for (const auto& b : node->behaviours()) {
            ImGui::PushID(b.get());
            b->onDrawInspector();
            ImGui::PopID();
        }
    }

    ImGui::End();
}

} // namespace ne
