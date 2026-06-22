#include "editor/panels/InspectorPanel.hpp"
#include "editor/EditorUI.hpp"
#include "editor/InspectorRegistry.hpp"
#include "editor/PropertyEditor.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "scene/CameraNode.hpp"
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
#include "scene/LODGroupBehaviour.hpp"
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
#include <functional>
#include <utility>

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

    // Play is inspectable but read-only: show the live values, disable edits.
    const bool readOnly = !editor->canEdit();
    if (readOnly)
        ImGui::TextDisabled("Play mode — inspecting (read-only)");
    ImGui::BeginDisabled(readOnly);

    drawNodeHeader(node, editor);

    if (auto* uiNode = dynamic_cast<UINode*>(node)) {
        drawUINode(uiNode, editor);
        PropertyEditor pe(*editor, node);
        if (auto* colorNode = dynamic_cast<UIColorNode*>(uiNode)) {
            (void)colorNode;
            ImGui::SeparatorText("UIColorNode");
            pe.colorEdit4("Color##UIColor",
                          [](Node& n) { return static_cast<UIColorNode&>(n).color(); },
                          [](Node& n, glm::vec4 v) { static_cast<UIColorNode&>(n).setColor(v); });
        }
        else if (auto* imageNode = dynamic_cast<UIImageNode*>(uiNode)) {
            ImGui::SeparatorText("UIImageNode");
            std::string texName = "Image Texture [" + getAssetName(imageNode->texture(), editor) + "]";
            if (imageNode->texture() == kAssetInvalid) texName = "Drop Texture Here";
            ImGui::Button(texName.c_str(), ImVec2(-FLT_MIN, 30));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                    AssetID id = *(AssetID*)payload->Data;
                    pe.push<AssetID>("Image Texture",
                                     [](Node& n) { return static_cast<UIImageNode&>(n).texture(); },
                                     [](Node& n, AssetID a) { static_cast<UIImageNode&>(n).setTexture(a); },
                                     id);
                }
                ImGui::EndDragDropTarget();
            }
        }
        else if (auto* textNode = dynamic_cast<UITextNode*>(uiNode)) {
            (void)textNode;
            ImGui::SeparatorText("UITextNode");
            pe.inputText("Text",
                         [](Node& n) { return static_cast<UITextNode&>(n).text(); },
                         [](Node& n, std::string v) { static_cast<UITextNode&>(n).setText(v); });
            pe.dragFloat("Font Size",
                         [](Node& n) { return static_cast<UITextNode&>(n).fontSize(); },
                         [](Node& n, float v) { static_cast<UITextNode&>(n).setFontSize(v); }, 0.5f, 1.0f, 128.0f);
            pe.colorEdit4("Color##UIText",
                          [](Node& n) { return static_cast<UITextNode&>(n).color(); },
                          [](Node& n, glm::vec4 v) { static_cast<UITextNode&>(n).setColor(v); });
        }

        if (auto* interactable = dynamic_cast<UIInteractableNode*>(uiNode)) {
            (void)interactable;
            ImGui::SeparatorText("UI Interactable");
            pe.checkbox("Interactable",
                        [](Node& n) { return static_cast<UIInteractableNode&>(n).interactable(); },
                        [](Node& n, bool v) { static_cast<UIInteractableNode&>(n).setInteractable(v); });

            if (auto* btn = dynamic_cast<UIButtonNode*>(interactable)) {
                (void)btn;
                // setColors is a bundle; each edit re-reads the siblings so the
                // command captures only the changed channel.
                pe.colorEdit4("Normal Color",
                              [](Node& n) { return static_cast<UIButtonNode&>(n).normalColor(); },
                              [](Node& n, glm::vec4 v) { auto& b = static_cast<UIButtonNode&>(n); b.setColors(v, b.hoverColor(), b.pressedColor()); });
                pe.colorEdit4("Hover Color",
                              [](Node& n) { return static_cast<UIButtonNode&>(n).hoverColor(); },
                              [](Node& n, glm::vec4 v) { auto& b = static_cast<UIButtonNode&>(n); b.setColors(b.normalColor(), v, b.pressedColor()); });
                pe.colorEdit4("Pressed Color",
                              [](Node& n) { return static_cast<UIButtonNode&>(n).pressedColor(); },
                              [](Node& n, glm::vec4 v) { auto& b = static_cast<UIButtonNode&>(n); b.setColors(b.normalColor(), b.hoverColor(), v); });
            }
            else if (auto* tgl = dynamic_cast<UIToggleNode*>(interactable)) {
                (void)tgl;
                pe.checkbox("Is On",
                            [](Node& n) { return static_cast<UIToggleNode&>(n).isOn(); },
                            [](Node& n, bool v) { static_cast<UIToggleNode&>(n).setIsOn(v); });
                pe.colorEdit4("On Color",
                              [](Node& n) { return static_cast<UIToggleNode&>(n).onColor(); },
                              [](Node& n, glm::vec4 v) { auto& t = static_cast<UIToggleNode&>(n); t.setColors(v, t.offColor()); });
                pe.colorEdit4("Off Color",
                              [](Node& n) { return static_cast<UIToggleNode&>(n).offColor(); },
                              [](Node& n, glm::vec4 v) { auto& t = static_cast<UIToggleNode&>(n); t.setColors(t.onColor(), v); });
            }
        }
    } else if (auto* webNode = dynamic_cast<WebCanvasNode*>(node)) {
        ImGui::SeparatorText("Web Canvas");
        PropertyEditor pe(*editor, node);

        int w = webNode->width();
        int h = webNode->height();
        if (ImGui::DragInt("Width", &w, 1, 1, 8192) || ImGui::DragInt("Height", &h, 1, 1, 8192)) {
            // Re-init is required, but we can't easily re-init dynamically without VulkanDevice here
            // We just let the user set the values. It will take effect on next scene reload.
            // A potential improvement is to expose a resize method to WebCanvasNode.
        }

        pe.combo("Mode",
                 [](Node& n) { return static_cast<int>(static_cast<WebCanvasNode&>(n).mode()); },
                 [](Node& n, int v) { static_cast<WebCanvasNode&>(n).setMode(static_cast<WebCanvasNode::Mode>(v)); },
                 "ScreenSpace\0WorldSpace\0");

        pe.inputText("URL",
                     [](Node& n) { return static_cast<WebCanvasNode&>(n).url(); },
                     [](Node& n, std::string v) { static_cast<WebCanvasNode&>(n).setUrl(v); });

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_HTML")) {
                const char* path = (const char*)payload->Data;
                std::string url = "file:///" + std::string(path);
                std::replace(url.begin(), url.end(), '\\', '/');
                webNode->setUrl(url); editor->markDirty();
            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::Button("Reload Page", ImVec2(-FLT_MIN, 30))) {
            webNode->reload();
        }
        pe.checkbox("Hot Reload",
                    [](Node& n) { return static_cast<WebCanvasNode&>(n).hotReloadEnabled(); },
                    [](Node& n, bool v) { static_cast<WebCanvasNode&>(n).setHotReloadEnabled(v); });
        ImGui::Separator();
        ImGui::Text("Startup Scripts");
        
        auto& scripts = const_cast<std::vector<std::string>&>(webNode->startupScripts());
        
        for (size_t i = 0; i < scripts.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            
            char buffer[1024];
            strncpy(buffer, scripts[i].c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';
            
            // Startup-script list edits are collection mutations, not yet
            // undoable; they mark the document dirty (full treatment: later lot).
            if (ImGui::InputText("##script", buffer, sizeof(buffer))) {
                scripts[i] = buffer; editor->markDirty();
            }

            ImGui::SameLine();
            if (ImGui::Button("X")) {
                scripts.erase(scripts.begin() + i);
                --i;
                editor->markDirty();
            }
            ImGui::PopID();
        }

        if (ImGui::Button("Add Script")) {
            webNode->addStartupScript(""); editor->markDirty();
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
        drawTransform(node, editor);
    }

    if (Scene* sceneNode = dynamic_cast<Scene*>(node)) {
        SceneSettings& s = sceneNode->settings();
        PropertyEditor pe(*editor, node);
        // Every SceneSettings field is reached the same way; these generic
        // factories turn a member pointer into get/set callables so each field
        // becomes a one-line undoable command.
        auto G = [](auto m) { return [m](Node& n) { return static_cast<Scene&>(n).settings().*m; }; };
        auto S = [](auto m) { return [m](Node& n, auto v) { static_cast<Scene&>(n).settings().*m = v; }; };

        // RGB color members are stored as vec4; edit rgb, preserve alpha.
        auto colorRGB = [](glm::vec4 SceneSettings::* m) {
            return std::pair{
                std::function<glm::vec3(Node&)>([m](Node& n) { return glm::vec3(static_cast<Scene&>(n).settings().*m); }),
                std::function<void(Node&, glm::vec3)>([m](Node& n, glm::vec3 v) {
                    glm::vec4& c = static_cast<Scene&>(n).settings().*m; c = glm::vec4(v, c.w); })};
        };

        ImGui::SeparatorText("Scene Settings");
        { auto c = colorRGB(&SceneSettings::ambientLight); pe.colorEdit3("Ambient Light", c.first, c.second); }
        { auto c = colorRGB(&SceneSettings::clearColor); pe.colorEdit3("Clear Color", c.first, c.second); }
        pe.checkbox("Post Processing", G(&SceneSettings::enablePostProcessing), S(&SceneSettings::enablePostProcessing));
        pe.checkbox("Change Rendering At Load", G(&SceneSettings::changeRenderingAtLoad), S(&SceneSettings::changeRenderingAtLoad));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("When loaded as a sub-scene at play time, override the\n"
                              "World's rendering settings (skybox/GI/ambient) with this scene's.");

        ImGui::SeparatorText("Lighting");
        // Radio buttons are instantaneous: record a one-shot command on click.
        int mode = static_cast<int>(s.lightingMode);
        auto modeGet = [](Node& n) { return static_cast<int>(static_cast<Scene&>(n).settings().lightingMode); };
        auto modeSet = [](Node& n, int v) { static_cast<Scene&>(n).settings().lightingMode = static_cast<LightingMode>(v); };
        if (ImGui::RadioButton("Realtime", &mode, static_cast<int>(LightingMode::Realtime)))
            pe.push<int>("Lighting Mode", modeGet, modeSet, static_cast<int>(LightingMode::Realtime));
        ImGui::SameLine();
        if (ImGui::RadioButton("Baked", &mode, static_cast<int>(LightingMode::Baked)))
            pe.push<int>("Lighting Mode", modeGet, modeSet, static_cast<int>(LightingMode::Baked));

        if (s.lightingMode == LightingMode::Realtime) {
            ImGui::TextDisabled("Lighting & shadows computed live; baking disabled.");
        } else {
            if (ImGui::Button("Generate Bake"))
                s.bakeRequested = true;  // transient action request, not a document edit
            ImGui::SameLine();
            ImGui::TextDisabled(s.baked ? "Bake: ready" : "Bake: none");
        }

        ImGui::SeparatorText("Global Illumination");
        pe.checkbox("Enable GI (indirect diffuse)", G(&SceneSettings::giEnabled), S(&SceneSettings::giEnabled));
        pe.sliderFloat("GI Intensity", G(&SceneSettings::giIntensity), S(&SceneSettings::giIntensity), 0.0f, 8.0f);
        pe.checkbox("Debug: show voxel grid", G(&SceneSettings::giDebugVoxels), S(&SceneSettings::giDebugVoxels));

        ImGui::SeparatorText("Debug");
        pe.checkbox("Show skeletons (bone lines)", G(&SceneSettings::showSkeletons), S(&SceneSettings::showSkeletons));

        ImGui::SeparatorText("Skybox");
        std::string skyboxName = "Skybox Texture [" + getAssetName(s.skyboxTexture, editor) + "]";
        if (s.skyboxTexture == kAssetInvalid) skyboxName = "Drop Skybox Texture Here";
        ImGui::Button(skyboxName.c_str(), ImVec2(-FLT_MIN, 30));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                AssetID id = *(AssetID*)payload->Data;
                pe.push<AssetID>("Skybox Texture", G(&SceneSettings::skyboxTexture), S(&SceneSettings::skyboxTexture), id);
            }
            ImGui::EndDragDropTarget();
        }
        pe.dragFloat("Exposure", G(&SceneSettings::skyboxExposure), S(&SceneSettings::skyboxExposure), 0.05f, 0.0f, 20.0f);
        pe.sliderAngle("Rotation", G(&SceneSettings::skyboxRotation), S(&SceneSettings::skyboxRotation));

        ImGui::SeparatorText("Image-Based Lighting");
        pe.checkbox("Enable IBL", G(&SceneSettings::iblEnabled), S(&SceneSettings::iblEnabled));
        pe.sliderFloat("IBL Diffuse", G(&SceneSettings::iblDiffuseIntensity), S(&SceneSettings::iblDiffuseIntensity), 0.0f, 4.0f);
        pe.sliderFloat("IBL Specular", G(&SceneSettings::iblSpecularIntensity), S(&SceneSettings::iblSpecularIntensity), 0.0f, 4.0f);

        ImGui::SeparatorText("Ambient Occlusion");
        pe.checkbox("Enable AO", G(&SceneSettings::aoEnabled), S(&SceneSettings::aoEnabled));
        pe.sliderFloat("AO Radius", G(&SceneSettings::aoRadius), S(&SceneSettings::aoRadius), 0.05f, 3.0f);
        pe.sliderFloat("AO Intensity", G(&SceneSettings::aoIntensity), S(&SceneSettings::aoIntensity), 0.0f, 3.0f);
        pe.sliderFloat("AO Power", G(&SceneSettings::aoPower), S(&SceneSettings::aoPower), 0.25f, 4.0f);

        ImGui::SeparatorText("Fog");
        pe.checkbox("Enable Fog", G(&SceneSettings::fogEnabled), S(&SceneSettings::fogEnabled));
        { auto c = colorRGB(&SceneSettings::fogColor); pe.colorEdit3("Fog Color", c.first, c.second); }
        pe.sliderFloat("Fog Start", G(&SceneSettings::fogStart), S(&SceneSettings::fogStart), 0.0f, 100.0f);
        pe.sliderFloat("Fog Density", G(&SceneSettings::fogDensity), S(&SceneSettings::fogDensity), 0.0f, 0.25f);

        ImGui::SeparatorText("Bloom");
        pe.checkbox("Enable Bloom", G(&SceneSettings::bloomEnabled), S(&SceneSettings::bloomEnabled));
        pe.sliderFloat("Bloom Threshold", G(&SceneSettings::bloomThreshold), S(&SceneSettings::bloomThreshold), 0.0f, 10.0f);
        pe.sliderFloat("Bloom Intensity", G(&SceneSettings::bloomIntensity), S(&SceneSettings::bloomIntensity), 0.0f, 3.0f);
        pe.sliderFloat("Bloom Radius", G(&SceneSettings::bloomRadius), S(&SceneSettings::bloomRadius), 0.5f, 8.0f);
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

        PropertyEditor pe(*editor, node);
        pe.colorEdit3("Color",
                      [](Node& n) { return n.asLight()->color; },
                      [](Node& n, glm::vec3 v) { n.asLight()->color = v; });
        pe.dragFloat("Intensity",
                     [](Node& n) { return n.asLight()->intensity; },
                     [](Node& n, float v) { n.asLight()->intensity = v; }, 0.05f, 0.0f, 20.0f);

        if (light->type == LightType::Directional || light->type == LightType::Spot)
            pe.sliderFloat3("Direction",
                            [](Node& n) { return n.asLight()->direction; },
                            [](Node& n, glm::vec3 v) { n.asLight()->direction = v; }, -1.0f, 1.0f);
        if (light->type == LightType::Point || light->type == LightType::Spot)
            pe.dragFloat("Range",
                         [](Node& n) { return n.asLight()->range; },
                         [](Node& n, float v) { n.asLight()->range = v; }, 0.1f, 0.1f, 100.0f);

        if (light->type == LightType::Spot) {
            pe.dragFloat("Inner Angle",
                         [](Node& n) { return n.asLight()->spotInnerAngle; },
                         [](Node& n, float v) {
                             LightNode* l = n.asLight();
                             l->spotInnerAngle = std::min(v, l->spotOuterAngle);
                         }, 0.5f, 1.0f, 89.0f);
            pe.dragFloat("Outer Angle",
                         [](Node& n) { return n.asLight()->spotOuterAngle; },
                         [](Node& n, float v) {
                             LightNode* l = n.asLight();
                             l->spotOuterAngle = std::max(v, l->spotInnerAngle);
                         }, 0.5f, 1.0f, 89.0f);
        }

        if (light->type != LightType::Point)
            pe.checkbox("Cast Shadows",
                        [](Node& n) { return n.asLight()->castShadows; },
                        [](Node& n, bool v) { n.asLight()->castShadows = v; });
    }

    if (dynamic_cast<CameraNode*>(node)) {
        ImGui::SeparatorText("Camera");
        PropertyEditor pe(*editor, node);
        pe.dragInt("Priority",
                   [](Node& n) { return static_cast<CameraNode&>(n).priority; },
                   [](Node& n, int v) { static_cast<CameraNode&>(n).priority = v; }, 1.0f, 0, 1000);
        pe.checkbox("Active",
                    [](Node& n) { return static_cast<CameraNode&>(n).active; },
                    [](Node& n, bool v) { static_cast<CameraNode&>(n).active = v; });
        pe.sliderFloat("Field of View",
                       [](Node& n) { return static_cast<CameraNode&>(n).fovDegrees; },
                       [](Node& n, float v) { static_cast<CameraNode&>(n).fovDegrees = v; }, 20.0f, 120.0f);
        pe.dragFloat("Near",
                     [](Node& n) { return static_cast<CameraNode&>(n).nearZ; },
                     [](Node& n, float v) { static_cast<CameraNode&>(n).nearZ = v; }, 0.01f, 0.01f, 10.0f);
        pe.dragFloat("Far",
                     [](Node& n) { return static_cast<CameraNode&>(n).farZ; },
                     [](Node& n, float v) { static_cast<CameraNode&>(n).farZ = v; }, 1.0f, 1.0f, 10000.0f);
        ImGui::TextDisabled("Highest priority active camera is live (Play).");
    }

    if (auto* body = node->asCollisionObject())
        drawPhysicsBody(body, editor);
    if (auto* shape = dynamic_cast<CollisionShapeNode*>(node))
        drawCollisionShape(shape, editor);

    drawBehaviours(node, editor);

    ImGui::EndDisabled();

    ImGui::End();
}

// Walk up to the body that owns this shape and force a rebuild so edits apply.
static void markOwningBodyDirty(Node* node) {
    for (Node* p = node->parent(); p; p = p->parent()) {
        if (auto* body = p->asCollisionObject()) { body->markDirty(); return; }
    }
}

void InspectorPanel::drawPhysicsBody(CollisionObjectNode* body, EditorUI* editor) {
    ImGui::SeparatorText("Physics Body");

    // Every setter rebuilds the body so creation-time params take effect — on
    // undo too, since the command replays the same setter with the old value.
    PropertyEditor pe(*editor, body);
    auto rebuild = [](Node& n) { if (auto* b = n.asCollisionObject()) b->markDirty(); };

    pe.dragFloat("Friction",
                 [](Node& n) { return n.asCollisionObject()->friction; },
                 [rebuild](Node& n, float v) { n.asCollisionObject()->friction = v; rebuild(n); }, 0.01f, 0.0f, 2.0f);
    pe.dragFloat("Restitution",
                 [](Node& n) { return n.asCollisionObject()->restitution; },
                 [rebuild](Node& n, float v) { n.asCollisionObject()->restitution = v; rebuild(n); }, 0.01f, 0.0f, 1.0f);

    if (auto* rb = dynamic_cast<RigidBodyNode*>(body)) {
        (void)rb;
        pe.checkbox("Kinematic",
                    [](Node& n) { return static_cast<RigidBodyNode&>(n).kinematic; },
                    [rebuild](Node& n, bool v) { static_cast<RigidBodyNode&>(n).kinematic = v; rebuild(n); });
        pe.dragFloat("Mass",
                     [](Node& n) { return static_cast<RigidBodyNode&>(n).mass; },
                     [rebuild](Node& n, float v) { static_cast<RigidBodyNode&>(n).mass = v; rebuild(n); }, 0.05f, 0.0f, 1000.0f);
        pe.dragFloat("Gravity Factor",
                     [](Node& n) { return static_cast<RigidBodyNode&>(n).gravityFactor; },
                     [rebuild](Node& n, float v) { static_cast<RigidBodyNode&>(n).gravityFactor = v; rebuild(n); }, 0.05f, 0.0f, 4.0f);
        pe.dragFloat("Linear Damping",
                     [](Node& n) { return static_cast<RigidBodyNode&>(n).linearDamping; },
                     [rebuild](Node& n, float v) { static_cast<RigidBodyNode&>(n).linearDamping = v; rebuild(n); }, 0.005f, 0.0f, 1.0f);
        pe.dragFloat("Angular Damping",
                     [](Node& n) { return static_cast<RigidBodyNode&>(n).angularDamping; },
                     [rebuild](Node& n, float v) { static_cast<RigidBodyNode&>(n).angularDamping = v; rebuild(n); }, 0.005f, 0.0f, 1.0f);
    } else if (auto* area = dynamic_cast<AreaNode*>(body)) {
        (void)area;
        pe.checkbox("Moving (kinematic trigger)",
                    [](Node& n) { return static_cast<AreaNode&>(n).moving; },
                    [rebuild](Node& n, bool v) { static_cast<AreaNode&>(n).moving = v; rebuild(n); });
    } else if (auto* ch = dynamic_cast<CharacterBodyNode*>(body)) {
        pe.dragFloat("Mass",
                     [](Node& n) { return static_cast<CharacterBodyNode&>(n).mass; },
                     [rebuild](Node& n, float v) { static_cast<CharacterBodyNode&>(n).mass = v; rebuild(n); }, 0.5f, 1.0f, 500.0f);
        pe.dragFloat("Max Slope Angle",
                     [](Node& n) { return static_cast<CharacterBodyNode&>(n).maxSlopeAngle; },
                     [rebuild](Node& n, float v) { static_cast<CharacterBodyNode&>(n).maxSlopeAngle = v; rebuild(n); }, 0.5f, 0.0f, 89.0f);
        ImGui::TextDisabled("On floor: %s", ch->isOnFloor() ? "yes" : "no");
    }
}

void InspectorPanel::drawCollisionShape(CollisionShapeNode* shape, EditorUI* editor) {
    ImGui::SeparatorText("Collision Shape");

    PropertyEditor pe(*editor, shape);
    // Editing a shape field rebuilds the owning body (on undo too).
    auto rebuild = [](Node& n) { markOwningBodyDirty(&n); };

    // Switching the shape type and the Auto detection have one-shot side effects
    // (resetAuto), so they stay outside the value-command path; they still mark
    // the document dirty. (Full command treatment: Lot 4 alongside resources.)
    const char* kinds[] = {"Auto", "Box", "Sphere", "Capsule", "ConvexHull", "Mesh"};
    int current = static_cast<int>(shape->shapeType);
    if (ImGui::Combo("Shape", &current, kinds, IM_ARRAYSIZE(kinds))) {
        shape->shapeType = static_cast<CollisionShapeType>(current);
        if (shape->shapeType == CollisionShapeType::Auto)
            shape->resetAuto();  // detect once, now, then freeze
        markOwningBodyDirty(shape);
        editor->markDirty();
    }

    if (shape->shapeType == CollisionShapeType::Auto) {
        ImGui::TextDisabled("Detected: %s (frozen)", toString(shape->resolvedType()));
        if (ImGui::Button("Recompute from mesh")) {
            shape->resetAuto();
            markOwningBodyDirty(shape);
            editor->markDirty();
        }
    } else if (shape->shapeType == CollisionShapeType::Box) {
        pe.dragFloat3("Half Extents",
                      [](Node& n) { return static_cast<CollisionShapeNode&>(n).halfExtents; },
                      [rebuild](Node& n, glm::vec3 v) { static_cast<CollisionShapeNode&>(n).halfExtents = v; rebuild(n); }, 0.02f, 0.02f, 100.0f);
    } else if (shape->shapeType == CollisionShapeType::Sphere) {
        pe.dragFloat("Radius",
                     [](Node& n) { return static_cast<CollisionShapeNode&>(n).radius; },
                     [rebuild](Node& n, float v) { static_cast<CollisionShapeNode&>(n).radius = v; rebuild(n); }, 0.02f, 0.02f, 100.0f);
    } else if (shape->shapeType == CollisionShapeType::Capsule) {
        pe.dragFloat("Radius",
                     [](Node& n) { return static_cast<CollisionShapeNode&>(n).radius; },
                     [rebuild](Node& n, float v) { static_cast<CollisionShapeNode&>(n).radius = v; rebuild(n); }, 0.02f, 0.02f, 100.0f);
        pe.dragFloat("Height",
                     [](Node& n) { return static_cast<CollisionShapeNode&>(n).height; },
                     [rebuild](Node& n, float v) { static_cast<CollisionShapeNode&>(n).height = v; rebuild(n); }, 0.02f, 0.04f, 100.0f);
        const char* axes[] = {"X", "Y", "Z"};
        pe.combo("Axis",
                 [](Node& n) { return static_cast<CollisionShapeNode&>(n).axis; },
                 [rebuild](Node& n, int v) { static_cast<CollisionShapeNode&>(n).axis = v; rebuild(n); }, axes, IM_ARRAYSIZE(axes));
    } else if (shape->shapeType == CollisionShapeType::ConvexHull) {
        ImGui::TextDisabled("Convex hull of the body's mesh (dynamic-capable).");
    } else if (shape->shapeType == CollisionShapeType::Mesh) {
        ImGui::TextDisabled("Exact triangle mesh — static bodies only.");
    }
    if (shape->shapeType != CollisionShapeType::ConvexHull &&
        shape->shapeType != CollisionShapeType::Mesh)
        pe.dragFloat3("Offset",
                      [](Node& n) { return static_cast<CollisionShapeNode&>(n).offset; },
                      [rebuild](Node& n, glm::vec3 v) { static_cast<CollisionShapeNode&>(n).offset = v; rebuild(n); }, 0.02f);
}

void InspectorPanel::drawNodeHeader(Node* node, EditorUI* editor) {
    ImGui::SeparatorText("Node");

    PropertyEditor pe(*editor, node);
    pe.checkbox("##NodeEnabled",
                [](Node& n) { return n.enabled(); },
                [](Node& n, bool v) { n.setEnabled(v); });
    ImGui::SameLine();
    ImGui::Text("Name: %s", node->name().c_str());
    
    const char* typeLabel = "Node";
    if (node->asLightConst()) typeLabel = "LightNode";
    else if (node->mesh()) typeLabel = "MeshNode";
    
    ImGui::Text("Type: %s", typeLabel);

    // Groups (tags) — the sanctioned way to locate a node (e.g. a follow camera's
    // "player" target). Editable here; serialized with the node.
    ImGui::Spacing();
    ImGui::SeparatorText("Groups");
    std::string groupToRemove;
    for (const std::string& g : node->groups()) {
        ImGui::PushID(g.c_str());
        if (ImGui::SmallButton("x")) groupToRemove = g;
        ImGui::SameLine();
        ImGui::TextUnformatted(g.c_str());
        ImGui::PopID();
    }
    if (!groupToRemove.empty()) { node->removeFromGroup(groupToRemove); editor->markDirty(); }

    static char groupBuf[64] = "";
    ImGui::SetNextItemWidth(-50.0f);
    bool submit = ImGui::InputTextWithHint("##newgroup", "group name", groupBuf,
                                           sizeof(groupBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    submit |= ImGui::Button("Add");
    if (submit && groupBuf[0] != '\0') {
        node->addToGroup(groupBuf);
        editor->markDirty();
        groupBuf[0] = '\0';
    }

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

void InspectorPanel::drawTransform(Node* node, EditorUI* editor) {
    ImGui::SeparatorText("Transform");
    PropertyEditor pe(*editor, node);

    pe.dragFloat3("Position",
                  [](Node& n) { return n.transform().position; },
                  [](Node& n, glm::vec3 v) { n.transform().position = v; }, 0.05f);

    // Rotation is edited as Euler degrees; the command stores the Euler triple
    // and reconstructs the quaternion on apply/undo.
    pe.dragFloat3("Rotation",
                  [](Node& n) { return glm::degrees(glm::eulerAngles(n.transform().rotation)); },
                  [](Node& n, glm::vec3 e) { n.transform().rotation = glm::quat(glm::radians(e)); }, 0.5f);

    pe.dragFloat3("Scale",
                  [](Node& n) { return n.transform().scale; },
                  [](Node& n, glm::vec3 v) { n.transform().scale = v; }, 0.01f, 0.001f, 100.0f);
}

void InspectorPanel::drawUINode(UINode* ui, EditorUI* editor) {
    ImGui::SeparatorText("UI Transform");

    PropertyEditor pe(*editor, ui);
    pe.dragFloat("X",
                 [](Node& n) { return static_cast<UINode&>(n).x(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setPosition(v, u.y()); }, 1.0f);
    pe.dragFloat("Y",
                 [](Node& n) { return static_cast<UINode&>(n).y(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setPosition(u.x(), v); }, 1.0f);
    pe.dragFloat("Width",
                 [](Node& n) { return static_cast<UINode&>(n).width(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setSize(v, u.height()); }, 1.0f, 0.0f);
    pe.dragFloat("Height",
                 [](Node& n) { return static_cast<UINode&>(n).height(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setSize(u.width(), v); }, 1.0f, 0.0f);
    pe.dragFloat("Pivot X",
                 [](Node& n) { return static_cast<UINode&>(n).pivotX(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setPivot(v, u.pivotY()); }, 0.01f, 0.0f, 1.0f);
    pe.dragFloat("Pivot Y",
                 [](Node& n) { return static_cast<UINode&>(n).pivotY(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setPivot(u.pivotX(), v); }, 0.01f, 0.0f, 1.0f);
    pe.dragFloat("Anchor X",
                 [](Node& n) { return static_cast<UINode&>(n).anchorX(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setAnchor(v, u.anchorY()); }, 0.01f, 0.0f, 1.0f);
    pe.dragFloat("Anchor Y",
                 [](Node& n) { return static_cast<UINode&>(n).anchorY(); },
                 [](Node& n, float v) { auto& u = static_cast<UINode&>(n); u.setAnchor(u.anchorX(), v); }, 0.01f, 0.0f, 1.0f);
}

void InspectorPanel::drawMeshRenderer(MeshNode* meshNode, EditorUI* editor) {
    ImGui::SeparatorText("Mesh Renderer");

    PropertyEditor pe(*editor, meshNode);
    pe.checkbox("Enabled##MeshEnabled",
                [](Node& n) { return static_cast<MeshNode&>(n).meshEnabled(); },
                [](Node& n, bool v) { static_cast<MeshNode&>(n).setMeshEnabled(v); });

    ResourceManager* res = editor->ctxResources_;
    std::string meshName = "Mesh [" + getAssetName(meshNode->mesh() ? res->meshId(meshNode->mesh()) : kAssetInvalid, editor) + "]";
    if (!meshNode->mesh()) meshName = "Drop Mesh Here";
    ImGui::Button(meshName.c_str(), ImVec2(-FLT_MIN, 30));
    if (res && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
            AssetID id = *(AssetID*)payload->Data;
            pe.push<AssetID>("Assign Mesh",
                             [res](Node& n) { auto* m = static_cast<MeshNode&>(n).mesh(); return m ? res->meshId(m) : kAssetInvalid; },
                             [res](Node& n, AssetID a) { if (Mesh* m = res->getMesh(a)) static_cast<MeshNode&>(n).setMesh(m); },
                             id);
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Spacing();
    pe.checkbox("Cast Shadows",
                [](Node& n) { return static_cast<MeshNode&>(n).castShadows(); },
                [](Node& n, bool v) { static_cast<MeshNode&>(n).castShadows() = v; });
    pe.checkbox("Include to light baking",
                [](Node& n) { return static_cast<MeshNode&>(n).includeInLightBaking(); },
                [](Node& n, bool v) { static_cast<MeshNode&>(n).includeInLightBaking() = v; });

    if (!meshNode->getBehaviour<LODGroupBehaviour>()) {
        if (ImGui::Button("Add LOD Group", ImVec2(-FLT_MIN, 0.0f))) {
            meshNode->addBehaviour<LODGroupBehaviour>();
            if (meshNode->lods().empty()) {
                MeshLodLevel base;
                base.mesh = meshNode->mesh();
                base.material = meshNode->material();
                base.minScreenCoverage = 0.0f;
                meshNode->setLods({base});
            }
            editor->markDirty();
        }
    }
}

void InspectorPanel::drawLodGroup(MeshNode* meshNode, EditorUI* editor) {
    if (!meshNode || !editor || !editor->ctxResources_) return;

    if (meshNode->lods().empty()) {
        MeshLodLevel base;
        base.mesh = meshNode->mesh();
        base.material = meshNode->material();
        base.minScreenCoverage = 0.0f;
        meshNode->setLods({base});
    }

    auto& lods = meshNode->lods();
    ImGui::Text("Active LOD: %d", meshNode->activeLodIndex());
    ImGui::Text("Levels: %zu", lods.size());

    // The LOD list is a per-element collection editor; its edits mark the
    // document dirty but are not individually undoable (a dedicated LOD-list
    // command is out of scope here — tracked as a follow-up).
    if (ImGui::Button("Add LOD", ImVec2(120, 0))) {
        MeshLodLevel lvl;
        lvl.mesh = meshNode->mesh();
        lvl.material = meshNode->material();
        lvl.minScreenCoverage = lods.empty() ? 0.0f : lods.back().minScreenCoverage * 0.5f;
        lods.push_back(lvl);
        editor->markDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Normalize Thresholds", ImVec2(170, 0))) {
        const size_t count = lods.size();
        const std::vector<float> thresholds = coverageThresholdsFromMsft({}, count);
        for (size_t i = 0; i < count; ++i)
            lods[i].minScreenCoverage = thresholds[i];
        editor->markDirty();
    }

    if (ImGui::BeginTable("LodGroupTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn("Mesh");
        ImGui::TableSetupColumn("Material");
        ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Min Coverage", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 34.0f);
        ImGui::TableHeadersRow();

        int removeIndex = -1;
        for (int i = 0; i < static_cast<int>(lods.size()); ++i) {
            ImGui::PushID(i);
            MeshLodLevel& lvl = lods[static_cast<size_t>(i)];
            Mesh* lodMesh = meshNode->meshForLod(i);
            Material* lodMaterial = meshNode->materialForLod(i);

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (i == meshNode->activeLodIndex())
                ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1.0f), "LOD %d", i);
            else
                ImGui::Text("LOD %d", i);

            ImGui::TableSetColumnIndex(1);
            AssetID meshId = lodMesh ? editor->ctxResources_->meshId(lodMesh) : kAssetInvalid;
            std::string meshLabel = getAssetName(meshId, editor);
            if (meshLabel == "None") meshLabel = "Drop Mesh";
            ImGui::Button(meshLabel.c_str(), ImVec2(-FLT_MIN, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                    AssetID id = *(AssetID*)payload->Data;
                    if (Mesh* newMesh = editor->ctxResources_->getMesh(id)) {
                        if (i == 0) meshNode->setMesh(newMesh);
                        lvl.mesh = newMesh;
                        editor->markDirty();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TableSetColumnIndex(2);
            AssetID materialAlbedo = lodMaterial ? lodMaterial->desc().albedoId : kAssetInvalid;
            std::string matLabel = getAssetName(materialAlbedo, editor);
            if (matLabel == "None") matLabel = "Drop Texture";
            ImGui::Button(matLabel.c_str(), ImVec2(-FLT_MIN, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
                    AssetID id = *(AssetID*)payload->Data;
                    MaterialDesc desc = lodMaterial ? lodMaterial->desc() : MaterialDesc{};
                    desc.albedoId = id;
                    if (Material* mat = editor->ctxResources_->getMaterial(desc)) {
                        if (i == 0) meshNode->setMaterial(mat);
                        lvl.material = mat;
                        editor->markDirty();
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::TableSetColumnIndex(3);
            if (lodMesh)
                ImGui::Text("%u", lodMesh->allocation().indexCount / 3);
            else
                ImGui::TextDisabled("-");

            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (ImGui::DragFloat("##coverage", &lvl.minScreenCoverage, 0.01f, 0.0f, 1.0f, "%.3f"))
                editor->markDirty();

            ImGui::TableSetColumnIndex(5);
            if (i > 0 && ImGui::Button("X"))
                removeIndex = i;

            ImGui::PopID();
        }

        if (removeIndex > 0) {
            lods.erase(lods.begin() + removeIndex);
            editor->markDirty();
        }

        ImGui::EndTable();
    }
}

void InspectorPanel::drawMaterial(Material* material, MeshNode* meshNode, EditorUI* editor) {
    ImGui::SeparatorText("Material");

    ResourceManager* res = editor->ctxResources_;
    PropertyEditor pe(*editor, meshNode);

    // A material edit is, from the node's view, switching which (interned)
    // Material it points to: the setter rebuilds the desc and re-interns it, so
    // it round-trips for undo. The getter reads one field off the current desc;
    // the setter overrides that one field on the *current* desc, so edits to
    // different fields compose cleanly.
    auto MG = [](auto m) {
        return [m](Node& n) {
            Material* mat = static_cast<MeshNode&>(n).material();
            return (mat ? mat->desc() : MaterialDesc{}).*m;
        };
    };
    auto MS = [res](auto m) {
        return [res, m](Node& n, auto v) {
            auto& mn = static_cast<MeshNode&>(n);
            MaterialDesc d = mn.material() ? mn.material()->desc() : MaterialDesc{};
            d.*m = v;
            if (Material* nm = res->getMaterial(d)) mn.setMaterial(nm);
        };
    };

    std::string texName = "Base Texture [" + getAssetName(material ? material->desc().albedoId : kAssetInvalid, editor) + "]";
    if (!material || material->desc().albedoId == kAssetInvalid) texName = "Drop Base Texture Here";
    ImGui::Button(texName.c_str(), ImVec2(-FLT_MIN, 30));
    if (res && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_ID")) {
            AssetID id = *(AssetID*)payload->Data;
            pe.push<AssetID>("Base Texture", MG(&MaterialDesc::albedoId), MS(&MaterialDesc::albedoId), id);
        }
        ImGui::EndDragDropTarget();
    }

    if (material && res) {
        pe.colorEdit4("Base Color", MG(&MaterialDesc::baseColor), MS(&MaterialDesc::baseColor));
        pe.sliderFloat("Metallic", MG(&MaterialDesc::metallic), MS(&MaterialDesc::metallic), 0.0f, 1.0f);
        pe.sliderFloat("Roughness", MG(&MaterialDesc::roughness), MS(&MaterialDesc::roughness), 0.0f, 1.0f);
        pe.colorEdit4("Emissive", MG(&MaterialDesc::emissiveColor), MS(&MaterialDesc::emissiveColor));
        pe.sliderFloat("Ambient Occlusion", MG(&MaterialDesc::ao), MS(&MaterialDesc::ao), 0.0f, 1.0f);
    }
}

void InspectorPanel::drawBehaviours(Node* node, EditorUI* editor) {
    ImGui::SeparatorText("Behaviours");

    Behaviour* toRemove = nullptr;

    for (const auto& b : node->behaviours()) {
        if (!b->visibleInEditor()) continue;
        ImGui::PushID(b.get());

        // Behaviour edits (enable, add/remove, internal fields) are not yet
        // routed through commands — that arrives with the InspectorRegistry in
        // Lot 6. Until then they mark the document dirty.
        bool enabled = b->enabled();
        if (ImGui::Checkbox("##enabled", &enabled)) {
            b->setEnabled(enabled); editor->markDirty();
        }
        ImGui::SameLine();

        const char* typeName = b->typeName() ? b->typeName() : "Unknown Behaviour";
        bool expanded = ImGui::CollapsingHeader(typeName, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);

        ImGui::SameLine(ImGui::GetWindowWidth() - 30.0f);
        if (ImGui::Button("X")) {
            toRemove = b.get();
        }

        if (expanded) {
            if (dynamic_cast<LODGroupBehaviour*>(b.get())) {
                if (auto* meshNode = dynamic_cast<MeshNode*>(node))
                    drawLodGroup(meshNode, editor);
                else
                    ImGui::TextDisabled("LOD Group requires a MeshNode.");
            } else {
                InspectorRegistry::instance().draw(*b, *editor);
            }

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
        node->removeBehaviour(toRemove); editor->markDirty();
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
                node->addBehaviour(pair.second()); editor->markDirty();
            }
        }
        ImGui::EndPopup();
    }
}

} // namespace ne
