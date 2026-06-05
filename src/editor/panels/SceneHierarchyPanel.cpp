#include "editor/panels/SceneHierarchyPanel.hpp"
#include "editor/EditorUI.hpp"
#include "scene/Scene.hpp"
#include "scene/Node.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "scene/SceneSerializer.hpp"
#include "editor/Command.hpp"
#include "project/Project.hpp"
#include "graphics/ResourceManager.hpp"

#include <imgui.h>
#include <filesystem>
#include <cstring>
#include <cctype>
#include <algorithm>

namespace ne {

namespace {

static std::string toLower(const std::string& str) {
    std::string lower = str;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
    return lower;
}

static bool nodeMatchesSearch(Node* node, const std::string& query) {
    if (query.empty()) return true;
    
    std::string nameLower = toLower(node->name());
    
    std::string typeLower = "node";
    if (dynamic_cast<MeshNode*>(node)) typeLower = "meshnode";
    else if (dynamic_cast<LightNode*>(node)) typeLower = "lightnode";
    else if (dynamic_cast<Scene*>(node)) typeLower = "scene";

    if (nameLower.find(query) != std::string::npos || typeLower.find(query) != std::string::npos) {
        return true;
    }
    
    for (auto& child : node->children()) {
        if (nodeMatchesSearch(child.get(), query)) return true;
    }
    return false;
}

const char* nodeTypeIcon(const Node* node) {
    if (node->asLightConst())    return "[L]";
    if (node->mesh())            return "[M]";
    return "[N]";
}

bool isDescendantOf(const Node* potentialDescendant, const Node* potentialAncestor) {
    const Node* curr = potentialDescendant;
    while (curr) {
        if (curr == potentialAncestor) {
            return true;
        }
        curr = curr->parent();
    }
    return false;
}

} // namespace

void SceneHierarchyPanel::draw(EditorUI* editor, Scene* scene) {
    ImGui::Begin("Scene Tree", &editor->showSceneTree_);

    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##SceneTreeSearch", "Search by name or type...", editor->sceneTreeSearchBuf_, sizeof(editor->sceneTreeSearchBuf_));
    ImGui::PopItemWidth();
    ImGui::Separator();

    if (scene) {
        drawSceneTreeNode(editor, scene);
    } else {
        ImGui::TextDisabled("(no scene)");
    }

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsAnyItemHovered()) {
        editor->selectedNode_ = nullptr;
    }

    if (ImGui::BeginPopupContextWindow("SceneTreeContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (scene) {
            if (ImGui::BeginMenu("Create Node")) {
                if (ImGui::MenuItem("Node")) {
                    editor->nodeToCreateChildUnder_ = scene;
                    editor->createType_ = CreateNodeType::Node;
                }
                if (ImGui::MenuItem("MeshNode (Cube)")) {
                    editor->nodeToCreateChildUnder_ = scene;
                    editor->createType_ = CreateNodeType::MeshNode;
                }
                if (ImGui::MenuItem("Directional Light")) {
                    editor->nodeToCreateChildUnder_ = scene;
                    editor->createType_ = CreateNodeType::DirectionalLight;
                }
                if (ImGui::MenuItem("Point Light")) {
                    editor->nodeToCreateChildUnder_ = scene;
                    editor->createType_ = CreateNodeType::PointLight;
                }
                if (ImGui::MenuItem("Spot Light")) {
                    editor->nodeToCreateChildUnder_ = scene;
                    editor->createType_ = CreateNodeType::SpotLight;
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    if (editor->nodeToDelete_) {
        if (editor->nodeToDelete_->parent()) {
            if (editor->selectedNode_ && (editor->selectedNode_ == editor->nodeToDelete_ || isDescendantOf(editor->selectedNode_, editor->nodeToDelete_)))
                editor->selectedNode_ = nullptr;
            editor->history_.execute(std::make_unique<DeleteNodeCommand>(editor->nodeToDelete_));
        }
        editor->nodeToDelete_ = nullptr;
    }

    if (editor->nodeToReparent_ && editor->newParent_) {
        if (editor->nodeToReparent_->parent() && !isDescendantOf(editor->newParent_, editor->nodeToReparent_))
            editor->history_.execute(std::make_unique<ReparentNodeCommand>(editor->nodeToReparent_, editor->newParent_));
        editor->nodeToReparent_ = nullptr;
        editor->newParent_ = nullptr;
    }

    if (editor->nodeToCreateChildUnder_ && editor->createType_ != CreateNodeType::None) {
        Mesh* defaultMesh = nullptr;
        Material* defaultMaterial = nullptr;
        if (scene) {
            scene->traverse([&](Node& n, const glm::mat4&) {
                if (!defaultMesh && n.mesh()) { defaultMesh = n.mesh(); defaultMaterial = n.material(); }
            });
        }

        std::unique_ptr<Node> newNode;
        if (editor->createType_ == CreateNodeType::Node) {
            newNode = std::make_unique<Node>("Node");
        } else if (editor->createType_ == CreateNodeType::MeshNode) {
            newNode = std::make_unique<MeshNode>("MeshNode", defaultMesh, defaultMaterial);
        } else if (editor->createType_ == CreateNodeType::DirectionalLight) {
            newNode = std::make_unique<LightNode>("Directional Light", LightType::Directional);
        } else if (editor->createType_ == CreateNodeType::PointLight) {
            newNode = std::make_unique<LightNode>("Point Light", LightType::Point);
        } else if (editor->createType_ == CreateNodeType::SpotLight) {
            newNode = std::make_unique<LightNode>("Spot Light", LightType::Spot);
        } else if (editor->createType_ == CreateNodeType::SceneInstance && editor->ctxResources_ && editor->ctxProject_) {
            std::filesystem::path p(editor->draggedScenePath_);
            newNode = SceneSerializer::loadNodeFromSceneFile(editor->draggedScenePath_, *editor->ctxResources_);
            if (newNode) {
                newNode->setName(p.stem().string() + " (Scene)");
                if (Scene* s = dynamic_cast<Scene*>(newNode.get())) {
                    std::string relativePath = std::filesystem::path(editor->draggedScenePath_).lexically_relative(editor->ctxProject_->rootPath()).generic_string();
                    AssetID id = editor->ctxResources_->getOrRegister(relativePath, AssetType::Scene);
                    s->setPrefabAssetId(id);
                }
            }
        }

        if (newNode) {
            Node* added = newNode.get();
            editor->history_.execute(std::make_unique<AddNodeCommand>(editor->nodeToCreateChildUnder_, std::move(newNode)));
            editor->selectedNode_ = added;
        }
        editor->nodeToCreateChildUnder_ = nullptr;
        editor->createType_ = CreateNodeType::None;
    }

    if (editor->nodeToCreateParentFor_) {
        if (editor->nodeToCreateParentFor_->parent()) {
            auto newParent = std::make_unique<Node>("Node");
            Node* rawParent = newParent.get();
            editor->history_.execute(std::make_unique<CreateParentCommand>(editor->nodeToCreateParentFor_, std::move(newParent)));
            editor->selectedNode_ = rawParent;
        }
        editor->nodeToCreateParentFor_ = nullptr;
    }

    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_SCENE")) {
            editor->nodeToCreateChildUnder_ = scene;
            editor->createType_ = CreateNodeType::SceneInstance;
            editor->draggedScenePath_ = (const char*)payload->Data;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::End();
}

void SceneHierarchyPanel::drawSceneTreeNode(EditorUI* editor, Node* node) {
    std::string query = toLower(editor->sceneTreeSearchBuf_);
    if (!nodeMatchesSearch(node, query)) {
        return; 
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_OpenOnDoubleClick
                             | ImGuiTreeNodeFlags_SpanAvailWidth;

    bool isNestedScene = (dynamic_cast<Scene*>(node) != nullptr && node != editor->ctxScene_ && static_cast<Scene*>(node)->prefabAssetId() != kAssetInvalid);
    bool isLeaf = node->children().empty() || isNestedScene;
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (node == editor->selectedNode_)
        flags |= ImGuiTreeNodeFlags_Selected;
    
    if (!node->parent() || !query.empty())
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    const char* icon = nodeTypeIcon(node);
    if (isNestedScene) icon = "[P]";

    bool open = false;

    bool active = node->isActiveInHierarchy();
    bool hasCustomColor = false;
    ImVec4 customColor;
    
    if (active) {
        if (isNestedScene) {
            hasCustomColor = true;
            customColor = ImVec4(0.6f, 0.8f, 1.0f, 1.0f); // Slightly blue
        } else if (dynamic_cast<MeshNode*>(node)) {
            hasCustomColor = true;
            customColor = ImVec4(0.6f, 0.9f, 0.6f, 1.0f); // Slightly green
        } else if (dynamic_cast<LightNode*>(node)) {
            hasCustomColor = true;
            customColor = ImVec4(0.9f, 0.9f, 0.5f, 1.0f); // Slightly yellow
        }
    }

    if (!active) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    } else if (hasCustomColor) {
        ImGui::PushStyleColor(ImGuiCol_Text, customColor);
    }

    if (editor->nodeToRename_ == node) {
        open = ImGui::TreeNodeEx(reinterpret_cast<void*>(node), flags, "%s", icon);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename_node", editor->nodeRenameBuf_, sizeof(editor->nodeRenameBuf_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            if (node->name() != editor->nodeRenameBuf_)
                editor->history_.execute(std::make_unique<RenameNodeCommand>(node, editor->nodeRenameBuf_));
            editor->nodeToRename_ = nullptr;
        } else if (ImGui::IsItemDeactivated() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            if (node->name() != editor->nodeRenameBuf_)
                editor->history_.execute(std::make_unique<RenameNodeCommand>(node, editor->nodeRenameBuf_));
            editor->nodeToRename_ = nullptr;
        }
        ImGui::PopStyleVar();
    } else {
        open = ImGui::TreeNodeEx(reinterpret_cast<void*>(node), flags, "%s %s", icon, node->name().c_str());
    }

    if (!active || hasCustomColor) {
        ImGui::PopStyleColor();
    }

    if (ImGui::BeginDragDropSource()) {
        ImGui::SetDragDropPayload("SCENE_NODE", &node, sizeof(Node*));
        ImGui::Text("Move %s", node->name().c_str());
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCENE_NODE")) {
            Node* draggedNode = *(Node**)payload->Data;
            if (draggedNode != node && draggedNode->parent() != node && !isDescendantOf(node, draggedNode)) {
                editor->nodeToReparent_ = draggedNode;
                editor->newParent_ = node;
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_SCENE")) {
            editor->nodeToCreateChildUnder_ = node;
            editor->createType_ = CreateNodeType::SceneInstance;
            editor->draggedScenePath_ = (const char*)payload->Data;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        editor->selectedNode_ = node;

    if (isNestedScene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && editor->ctxResources_ && editor->ctxProject_) {
        std::string absPath = editor->ctxResources_->getRegistry()->getAbsolutePath(static_cast<Scene*>(node)->prefabAssetId());
        if (!absPath.empty()) {
            editor->loadScene(editor->ctxScene_, editor->ctxResources_, absPath);
        }
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Middle))
        node->setEnabled(!node->enabled());

    if (ImGui::BeginPopupContextItem()) {
        editor->selectedNode_ = node; 

        if (ImGui::MenuItem("Rename")) {
            editor->nodeToRename_ = node;
            std::strncpy(editor->nodeRenameBuf_, node->name().c_str(), sizeof(editor->nodeRenameBuf_) - 1);
            editor->nodeRenameBuf_[sizeof(editor->nodeRenameBuf_) - 1] = '\0';
        }
        if (node->enabled()) {
            if (ImGui::MenuItem("Hide")) {
                node->setEnabled(false);
            }
        } else {
            if (ImGui::MenuItem("Show")) {
                node->setEnabled(true);
            }
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("Create Child")) {
            if (ImGui::MenuItem("Node")) {
                editor->nodeToCreateChildUnder_ = node;
                editor->createType_ = CreateNodeType::Node;
            }
            if (ImGui::MenuItem("MeshNode (Cube)")) {
                editor->nodeToCreateChildUnder_ = node;
                editor->createType_ = CreateNodeType::MeshNode;
            }
            if (ImGui::MenuItem("Directional Light")) {
                editor->nodeToCreateChildUnder_ = node;
                editor->createType_ = CreateNodeType::DirectionalLight;
            }
            if (ImGui::MenuItem("Point Light")) {
                editor->nodeToCreateChildUnder_ = node;
                editor->createType_ = CreateNodeType::PointLight;
            }
            if (ImGui::MenuItem("Spot Light")) {
                editor->nodeToCreateChildUnder_ = node;
                editor->createType_ = CreateNodeType::SpotLight;
            }
            ImGui::EndMenu();
        }

        bool canCreateParent = node->parent() != nullptr;
        if (ImGui::MenuItem("Create Parent", nullptr, false, canCreateParent)) {
            editor->nodeToCreateParentFor_ = node;
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
            editor->duplicateSelected(editor->ctxResources_);
        }
        
        bool canDelete = node->parent() != nullptr;
        if (ImGui::MenuItem("Delete", "Del", false, canDelete)) {
            editor->nodeToDelete_ = node;
        }

        ImGui::EndPopup();
    }

    if (open) {
        if (!isLeaf) {
            for (auto& child : node->children()) {
                drawSceneTreeNode(editor, child.get());
            }
            ImGui::TreePop();
        }
    }
}

} // namespace ne
