#include "editor/panels/InspectorPanel.hpp"
#include "editor/EditorUI.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "physics/CollisionObjectNode.hpp"
#include "physics/CollisionShapeNode.hpp"
#include "physics/RigidBodyNode.hpp"
#include "physics/AreaNode.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "graphics/ResourceManager.hpp"
#include "project/Project.hpp"
#include "project/AssetRegistry.hpp"

#include "scene/BehaviourRegistry.hpp"
#include "scene/BVHLoader.hpp"
#include "scene/animation/Animator.hpp"
#include "scene/animation/AnimationClip.hpp"

#include "scene/UICanvasNode.hpp"
#include "scene/UIColorNode.hpp"
#include "scene/UIImageNode.hpp"
#include "scene/UITextNode.hpp"
#include "scene/UIButtonNode.hpp"
#include "scene/UIToggleNode.hpp"
#include "scene/WebCanvasNode.hpp"

#include <imgui.h>
#include <cstring>
#include <algorithm>

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

    drawNodeHeader(node, editor);

    if (auto* uiNode = dynamic_cast<UINode*>(node)) {
        drawUINode(uiNode);
        if (auto* colorNode = dynamic_cast<UIColorNode*>(uiNode)) {
            ImGui::SeparatorText("UIColorNode");
            glm::vec4 color = colorNode->color();
            if (ImGui::ColorEdit4("Color##UIColor", &color.x)) colorNode->setColor(color);
        }
        else if (auto* imageNode = dynamic_cast<UIImageNode*>(uiNode)) {
            ImGui::SeparatorText("UIImageNode");
            std::string texName = "Image Texture [" + getAssetName(imageNode->texture(), editor) + "]";
            if (imageNode->texture() == kAssetInvalid) texName = "Drop Texture Here";
            ImGui::Button(texName.c_str(), ImVec2(-FLT_MIN, 30));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                    AssetID id = *(AssetID*)payload->Data;
                    imageNode->setTexture(id);
                }
                ImGui::EndDragDropTarget();
            }
        }
        else if (auto* textNode = dynamic_cast<UITextNode*>(uiNode)) {
            ImGui::SeparatorText("UITextNode");
            char textBuf[256];
            std::strncpy(textBuf, textNode->text().c_str(), sizeof(textBuf));
            textBuf[sizeof(textBuf) - 1] = '\0';
            if (ImGui::InputText("Text", textBuf, sizeof(textBuf))) {
                textNode->setText(textBuf);
            }
            
            float fontSize = textNode->fontSize();
            if (ImGui::DragFloat("Font Size", &fontSize, 0.5f, 1.0f, 128.0f)) textNode->setFontSize(fontSize);
            
            glm::vec4 color = textNode->color();
            if (ImGui::ColorEdit4("Color##UIText", &color.x)) textNode->setColor(color);
        }
        
        if (auto* interactable = dynamic_cast<UIInteractableNode*>(uiNode)) {
            ImGui::SeparatorText("UI Interactable");
            bool isInteractable = interactable->interactable();
            if (ImGui::Checkbox("Interactable", &isInteractable)) interactable->setInteractable(isInteractable);
            
            if (auto* btn = dynamic_cast<UIButtonNode*>(interactable)) {
                glm::vec4 normal = btn->normalColor();
                glm::vec4 hover = btn->hoverColor();
                glm::vec4 press = btn->pressedColor();
                bool changed = false;
                changed |= ImGui::ColorEdit4("Normal Color", &normal.x);
                changed |= ImGui::ColorEdit4("Hover Color", &hover.x);
                changed |= ImGui::ColorEdit4("Pressed Color", &press.x);
                if (changed) btn->setColors(normal, hover, press);
            }
            else if (auto* tgl = dynamic_cast<UIToggleNode*>(interactable)) {
                bool isOn = tgl->isOn();
                if (ImGui::Checkbox("Is On", &isOn)) tgl->setIsOn(isOn);
                
                glm::vec4 onCol = tgl->onColor();
                glm::vec4 offCol = tgl->offColor();
                bool changed = false;
                changed |= ImGui::ColorEdit4("On Color", &onCol.x);
                changed |= ImGui::ColorEdit4("Off Color", &offCol.x);
                if (changed) tgl->setColors(onCol, offCol);
            }
        }
    } else if (auto* webNode = dynamic_cast<WebCanvasNode*>(node)) {
        ImGui::SeparatorText("Web Canvas");
        
        int w = webNode->width();
        int h = webNode->height();
        if (ImGui::DragInt("Width", &w, 1, 1, 8192) || ImGui::DragInt("Height", &h, 1, 1, 8192)) {
            // Re-init is required, but we can't easily re-init dynamically without VulkanDevice here
            // We just let the user set the values. It will take effect on next scene reload.
            // A potential improvement is to expose a resize method to WebCanvasNode.
        }

        int mode = static_cast<int>(webNode->mode());
        if (ImGui::Combo("Mode", &mode, "ScreenSpace\0WorldSpace\0")) {
            webNode->setMode(static_cast<WebCanvasNode::Mode>(mode));
        }

        char urlBuf[512];
        std::strncpy(urlBuf, webNode->url().c_str(), sizeof(urlBuf));
        urlBuf[sizeof(urlBuf) - 1] = '\0';
        ImGui::InputText("URL", urlBuf, sizeof(urlBuf));
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            webNode->setUrl(urlBuf);
        }
        
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_HTML")) {
                const char* path = (const char*)payload->Data;
                std::string url = "file:///" + std::string(path);
                std::replace(url.begin(), url.end(), '\\', '/');
                webNode->setUrl(url);
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::Button("Reload Page", ImVec2(-FLT_MIN, 30))) {
            webNode->reload();
        }
        ImGui::Separator();
        ImGui::Text("Startup Scripts");
        
        auto& scripts = const_cast<std::vector<std::string>&>(webNode->startupScripts());
        
        for (size_t i = 0; i < scripts.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            
            char buffer[1024];
            strncpy(buffer, scripts[i].c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            
            if (ImGui::InputText("##script", buffer, sizeof(buffer))) {
                scripts[i] = buffer;
            }
            
            ImGui::SameLine();
            if (ImGui::Button("X")) {
                scripts.erase(scripts.begin() + i);
                --i;
            }
            ImGui::PopID();
        }
        
        if (ImGui::Button("Add Script")) {
            webNode->addStartupScript("");
        }
        
        ImGui::Separator();
        ImGui::SeparatorText("Execute JS");
        static char jsBuf[1024] = "";
        ImGui::InputTextMultiline("##JS", jsBuf, sizeof(jsBuf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4));
        if (ImGui::Button("Run JS", ImVec2(-FLT_MIN, 0))) {
            webNode->executeJS(jsBuf);
        }
    } else {
        // Not a UI node, draw standard transform
        drawTransform(node);
    }

    if (Scene* sceneNode = dynamic_cast<Scene*>(node)) {
        SceneSettings& s = sceneNode->settings();
        ImGui::SeparatorText("Scene Settings");
        ImGui::ColorEdit3("Ambient Light", &s.ambientLight.x);
        ImGui::ColorEdit3("Clear Color", &s.clearColor.x);
        ImGui::Checkbox("Post Processing", &s.enablePostProcessing);
        ImGui::Checkbox("Change Rendering At Load", &s.changeRenderingAtLoad);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When loaded as a sub-scene at play time, override the\n"
                              "World's rendering settings (skybox/GI/ambient) with this scene's.");

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

        ImGui::SeparatorText("Global Illumination");
        ImGui::Checkbox("Enable GI (indirect diffuse)", &s.giEnabled);
        ImGui::SliderFloat("GI Intensity", &s.giIntensity, 0.0f, 8.0f);
        ImGui::Checkbox("Debug: show voxel grid", &s.giDebugVoxels);

        ImGui::SeparatorText("Debug");
        ImGui::Checkbox("Show skeletons (bone lines)", &s.showSkeletons);

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

    if (auto* body = node->asCollisionObject())
        drawPhysicsBody(body);
    if (auto* shape = dynamic_cast<CollisionShapeNode*>(node))
        drawCollisionShape(shape);

    drawBehaviours(node, editor);

    ImGui::End();
}

// Walk up to the body that owns this shape and force a rebuild so edits apply.
static void markOwningBodyDirty(Node* node) {
    for (Node* p = node->parent(); p; p = p->parent()) {
        if (auto* body = p->asCollisionObject()) { body->markDirty(); return; }
    }
}

void InspectorPanel::drawPhysicsBody(CollisionObjectNode* body) {
    ImGui::SeparatorText("Physics Body");
    bool dirty = false;
    dirty |= ImGui::DragFloat("Friction", &body->friction, 0.01f, 0.0f, 2.0f);
    dirty |= ImGui::DragFloat("Restitution", &body->restitution, 0.01f, 0.0f, 1.0f);

    if (auto* rb = dynamic_cast<RigidBodyNode*>(body)) {
        dirty |= ImGui::Checkbox("Kinematic", &rb->kinematic);
        dirty |= ImGui::DragFloat("Mass", &rb->mass, 0.05f, 0.0f, 1000.0f);
        dirty |= ImGui::DragFloat("Gravity Factor", &rb->gravityFactor, 0.05f, 0.0f, 4.0f);
        dirty |= ImGui::DragFloat("Linear Damping", &rb->linearDamping, 0.005f, 0.0f, 1.0f);
        dirty |= ImGui::DragFloat("Angular Damping", &rb->angularDamping, 0.005f, 0.0f, 1.0f);
    } else if (auto* area = dynamic_cast<AreaNode*>(body)) {
        dirty |= ImGui::Checkbox("Moving (kinematic trigger)", &area->moving);
    } else if (auto* ch = dynamic_cast<CharacterBodyNode*>(body)) {
        dirty |= ImGui::DragFloat("Mass", &ch->mass, 0.5f, 1.0f, 500.0f);
        dirty |= ImGui::DragFloat("Max Slope Angle", &ch->maxSlopeAngle, 0.5f, 0.0f, 89.0f);
        ImGui::TextDisabled("On floor: %s", ch->isOnFloor() ? "yes" : "no");
    }

    if (dirty) body->markDirty();  // rebuild so creation-time params take effect
}

void InspectorPanel::drawCollisionShape(CollisionShapeNode* shape) {
    ImGui::SeparatorText("Collision Shape");

    const char* kinds[] = {"Auto", "Box", "Sphere", "Capsule", "ConvexHull", "Mesh"};
    int current = static_cast<int>(shape->shapeType);
    if (ImGui::Combo("Shape", &current, kinds, IM_ARRAYSIZE(kinds))) {
        shape->shapeType = static_cast<CollisionShapeType>(current);
        if (shape->shapeType == CollisionShapeType::Auto)
            shape->resetAuto();  // detect once, now, then freeze
        markOwningBodyDirty(shape);
    }

    bool dirty = false;
    if (shape->shapeType == CollisionShapeType::Auto) {
        ImGui::TextDisabled("Detected: %s (frozen)", toString(shape->resolvedType()));
        if (ImGui::Button("Recompute from mesh")) {
            shape->resetAuto();
            markOwningBodyDirty(shape);
        }
    } else if (shape->shapeType == CollisionShapeType::Box) {
        dirty |= ImGui::DragFloat3("Half Extents", &shape->halfExtents.x, 0.02f, 0.02f, 100.0f);
    } else if (shape->shapeType == CollisionShapeType::Sphere) {
        dirty |= ImGui::DragFloat("Radius", &shape->radius, 0.02f, 0.02f, 100.0f);
    } else if (shape->shapeType == CollisionShapeType::Capsule) {
        dirty |= ImGui::DragFloat("Radius", &shape->radius, 0.02f, 0.02f, 100.0f);
        dirty |= ImGui::DragFloat("Height", &shape->height, 0.02f, 0.04f, 100.0f);
        const char* axes[] = {"X", "Y", "Z"};
        dirty |= ImGui::Combo("Axis", &shape->axis, axes, IM_ARRAYSIZE(axes));
    } else if (shape->shapeType == CollisionShapeType::ConvexHull) {
        ImGui::TextDisabled("Convex hull of the body's mesh (dynamic-capable).");
    } else if (shape->shapeType == CollisionShapeType::Mesh) {
        ImGui::TextDisabled("Exact triangle mesh — static bodies only.");
    }
    if (shape->shapeType != CollisionShapeType::ConvexHull &&
        shape->shapeType != CollisionShapeType::Mesh)
        dirty |= ImGui::DragFloat3("Offset", &shape->offset.x, 0.02f);

    if (dirty) markOwningBodyDirty(shape);
}

void InspectorPanel::drawNodeHeader(Node* node, EditorUI* editor) {
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

    if (!node->importedFromPath().empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Source: %s", node->importedFromPath().c_str());
        if (ImGui::Button("Open in 3D Importer", ImVec2(-FLT_MIN, 30))) {
            if (editor->ctxResources_) {
                editor->openModelImporter(node->importedFromPath(), editor->ctxResources_);
            }
        }
    }
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

void InspectorPanel::drawUINode(UINode* ui) {
    ImGui::SeparatorText("UI Transform");
    
    float x = ui->x();
    float y = ui->y();
    float w = ui->width();
    float h = ui->height();
    float px = ui->pivotX();
    float py = ui->pivotY();
    float ax = ui->anchorX();
    float ay = ui->anchorY();

    bool changed = false;
    changed |= ImGui::DragFloat("X", &x, 1.0f);
    changed |= ImGui::DragFloat("Y", &y, 1.0f);
    changed |= ImGui::DragFloat("Width", &w, 1.0f, 0.0f);
    changed |= ImGui::DragFloat("Height", &h, 1.0f, 0.0f);
    changed |= ImGui::DragFloat("Pivot X", &px, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Pivot Y", &py, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Anchor X", &ax, 0.01f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Anchor Y", &ay, 0.01f, 0.0f, 1.0f);

    if (changed) {
        ui->setPosition(x, y);
        ui->setSize(w, h);
        ui->setPivot(px, py);
        ui->setAnchor(ax, ay);
    }
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

    if (meshNode->hasLods()) {
        ImGui::SeparatorText("LOD Group");
        ImGui::Text("Levels: %zu", meshNode->lods().size());
        ImGui::Text("Active LOD: %d", meshNode->activeLodIndex());
        if (ImGui::BeginTable("MeshLods", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("LOD");
            ImGui::TableSetupColumn("Triangles");
            ImGui::TableSetupColumn("Min Coverage");
            ImGui::TableSetupColumn("Status");
            ImGui::TableHeadersRow();
            for (int i = 0; i < static_cast<int>(meshNode->lods().size()); ++i) {
                const MeshLodLevel& lvl = meshNode->lods()[static_cast<size_t>(i)];
                Mesh* lodMesh = meshNode->meshForLod(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("LOD %d", i);
                ImGui::TableSetColumnIndex(1);
                if (lodMesh)
                    ImGui::Text("%u", lodMesh->allocation().indexCount / 3);
                else
                    ImGui::TextDisabled("-");
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%.2f", lvl.minScreenCoverage);
                ImGui::TableSetColumnIndex(3);
                if (i == meshNode->activeLodIndex())
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "active");
                else
                    ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }
    }
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

void InspectorPanel::drawBehaviours(Node* node, EditorUI* editor) {
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

            // Animator: drop a .bvh from the file browser to add it as a clip.
            if (auto* anim = dynamic_cast<Animator*>(b.get())) {
                ImGui::Button("Drop .bvh to add clip", ImVec2(-FLT_MIN, 24));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_BVH")) {
                        if (ResourceManager* res = editor->ctxResources_) {
                            AssetID id = BVHLoader::load((const char*)payload->Data, *res);
                            if (id != kAssetInvalid)
                                if (AnimationClip* clip = res->getAnimation(id))
                                    anim->addClip(clip->name(), clip);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
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
