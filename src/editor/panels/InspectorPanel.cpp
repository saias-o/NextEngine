#include "editor/panels/InspectorPanel.hpp"
#include "editor/EditorUI.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "graphics/ResourceManager.hpp"
#include "project/Project.hpp"
#include "project/AssetRegistry.hpp"

#include "scene/BehaviourRegistry.hpp"

#include <imgui.h>

namespace ne {

namespace {
    std::string getAssetName(AssetID id, EditorUI* editor) {
        if (id == kAssetInvalid) return "None";
        if (editor->ctxProject()) {
            std::string path = editor->ctxProject()->assetRegistry().getPath(id);
            if (!path.empty()) {
                size_t slash = path.find_last_of("/\\");
                if (slash != std::string::npos) return path.substr(slash + 1);
                return path;
            }
        }
        return "Assigned";
    }
}

void InspectorPanel::draw(EditorUI* editor) {
    ImGui::Begin("Inspector", &editor->showInspector_);

    if (!editor->selectedNode_) {
        ImGui::TextDisabled("No node selected");
        ImGui::End();
        return;
    }

    Node* node = editor->selectedNode_;

    drawNodeHeader(node);

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
            if (ImGui::Button("Generate Bake"))
                s.bakeRequested = true;
            ImGui::SameLine();
            ImGui::TextDisabled(s.baked ? "Bake: ready" : "Bake: none");
        }

        ImGui::SeparatorText("Skybox");
        std::string skyboxName = "Skybox Texture [" + getAssetName(s.skyboxTexture, editor) + "]";
        if (s.skyboxTexture == kAssetInvalid) skyboxName = "Drop Skybox Texture Here";
        ImGui::Button(skyboxName.c_str(), ImVec2(-FLT_MIN, 30));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                AssetID id = *(AssetID*)payload->Data;
                s.skyboxTexture = id;
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::DragFloat("Exposure", &s.skyboxExposure, 0.05f, 0.0f, 20.0f);
        ImGui::SliderAngle("Rotation", &s.skyboxRotation);
    }

    drawTransform(node);

    if (auto* meshNode = dynamic_cast<MeshNode*>(node)) {
        drawMeshRenderer(meshNode, editor);
        drawMaterial(meshNode->material(), meshNode, editor);
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

        if (light->type != LightType::Point)
            ImGui::Checkbox("Cast Shadows", &light->castShadows);
    }

    drawBehaviours(node);

    ImGui::End();
}

void InspectorPanel::drawNodeHeader(Node* node) {
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
}

void InspectorPanel::drawTransform(Node* node) {
    ImGui::SeparatorText("Transform");
    Transform& t = node->transform();
    ImGui::DragFloat3("Position", &t.position.x, 0.05f);

    glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
    if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
        t.rotation = glm::quat(glm::radians(euler));

    ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);
}

void InspectorPanel::drawMeshRenderer(MeshNode* meshNode, EditorUI* editor) {
    ImGui::SeparatorText("Mesh Renderer");
    
    bool enabled = meshNode->meshEnabled();
    if (ImGui::Checkbox("Enabled##MeshEnabled", &enabled)) {
        meshNode->setMeshEnabled(enabled);
    }
    
    std::string meshName = "Mesh [" + getAssetName(meshNode->mesh() ? editor->ctxResources_->meshId(meshNode->mesh()) : kAssetInvalid, editor) + "]";
    if (!meshNode->mesh()) meshName = "Drop Mesh Here";
    ImGui::Button(meshName.c_str(), ImVec2(-FLT_MIN, 30));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
            AssetID id = *(AssetID*)payload->Data;
            Mesh* newMesh = editor->ctxResources_->getMesh(id);
            if (newMesh) meshNode->setMesh(newMesh);
        }
        ImGui::EndDragDropTarget();
    }
    
    ImGui::Spacing();
    ImGui::Checkbox("Cast Shadows", &meshNode->castShadows());
    ImGui::Checkbox("Include to light baking", &meshNode->includeInLightBaking());
}

void InspectorPanel::drawMaterial(Material* material, MeshNode* meshNode, EditorUI* editor) {
    ImGui::SeparatorText("Material");
    std::string texName = "Base Texture [" + getAssetName(material ? material->desc().albedoId : kAssetInvalid, editor) + "]";
    if (!material || material->desc().albedoId == kAssetInvalid) texName = "Drop Base Texture Here";
    ImGui::Button(texName.c_str(), ImVec2(-FLT_MIN, 30));
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
            AssetID id = *(AssetID*)payload->Data;
            MaterialDesc desc = material ? material->desc() : MaterialDesc{};
            desc.albedoId = id;
            Material* newMat = editor->ctxResources_->getMaterial(desc);
            if (newMat) meshNode->setMaterial(newMat);
        }
        ImGui::EndDragDropTarget();
    }
    
    if (material) {
        MaterialDesc desc = material->desc();
        bool changed = false;
        
        if (ImGui::ColorEdit4("Base Color", &desc.baseColor.x)) changed = true;
        if (ImGui::SliderFloat("Metallic", &desc.metallic, 0.0f, 1.0f)) changed = true;
        if (ImGui::SliderFloat("Roughness", &desc.roughness, 0.0f, 1.0f)) changed = true;
        if (ImGui::ColorEdit4("Emissive", &desc.emissiveColor.x)) changed = true;
        if (ImGui::SliderFloat("Ambient Occlusion", &desc.ao, 0.0f, 1.0f)) changed = true;
        
        if (changed) {
            Material* newMat = editor->ctxResources_->getMaterial(desc);
            if (newMat) meshNode->setMaterial(newMat);
        }
    }
}

void InspectorPanel::drawBehaviours(Node* node) {
    ImGui::SeparatorText("Behaviours");
    
    Behaviour* toRemove = nullptr;
    
    for (const auto& b : node->behaviours()) {
        ImGui::PushID(b.get());
        
        bool enabled = b->enabled();
        if (ImGui::Checkbox("##enabled", &enabled)) {
            b->setEnabled(enabled);
        }
        ImGui::SameLine();
        
        const char* typeName = b->typeName() ? b->typeName() : "Unknown Behaviour";
        bool expanded = ImGui::CollapsingHeader(typeName, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
        if (ImGui::Button("X")) {
            toRemove = b.get();
        }
        
        if (expanded) {
            b->onDrawInspector();
        }
        ImGui::PopID();
    }
    
    if (toRemove) {
        node->removeBehaviour(toRemove);
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    
    if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f))) {
        ImGui::OpenPopup("AddComponentPopup");
    }
    
    if (ImGui::BeginPopup("AddComponentPopup")) {
        auto& factories = BehaviourRegistry::instance().factories();
        for (const auto& pair : factories) {
            if (ImGui::Selectable(pair.first.c_str())) {
                node->addBehaviour(pair.second());
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace ne
