#include "editor/EditorUI.hpp"

#include "core/Camera.hpp"
#include "core/Paths.hpp"
#include "core/Time.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"
#include "project/Project.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"

#include "imgui.h"
#include "imgui_internal.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace ne {

namespace {

// ── Icon helpers (plain-text fallbacks — no font atlas changes needed) ───────
const char* nodeTypeIcon(const Node* node) {
    if (node->asLightConst())    return "[L]";   // LightNode
    if (node->mesh())            return "[M]";   // MeshNode
    return "[N]";                                 // plain Node
}

const char* nodeTypeLabel(const Node* node) {
    if (node->asLightConst())    return "LightNode";
    if (node->mesh())            return "MeshNode";
    return "Node";
}

const char* lightTypeLabel(LightType t) {
    switch (t) {
        case LightType::Directional: return "Directional";
        case LightType::Point:       return "Point";
    }
    return "Unknown";
}

// Euler degrees ↔ quaternion helpers (for the inspector Transform widget).
glm::vec3 quatToEulerDeg(const glm::quat& q) {
    return glm::degrees(glm::eulerAngles(q));   // pitch, yaw, roll
}

glm::quat eulerDegToQuat(const glm::vec3& deg) {
    return glm::quat(glm::radians(deg));
}

// File-type icon for the file browser.
const char* fileIcon(const std::filesystem::directory_entry& entry) {
    if (entry.is_directory()) return "[D]";
    auto ext = entry.path().extension().string();
    if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") return "[3D]";
    if (ext == ".png" || ext == ".jpg" || ext == ".bmp")  return "[Img]";
    if (ext == ".vert" || ext == ".frag" || ext == ".glsl" || ext == ".spv") return "[Sh]";
    if (ext == ".lua")   return "[Lua]";
    if (ext == ".neproj") return "[Proj]";
    if (ext == ".scene") return "[Sc]";
    return "[F]";
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

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

EditorUI::EditorUI() {
    applyEditorStyle();
    // Default open path for the "Open Project" dialog.
    std::strncpy(newProjectPath_, NE_PROJECT_ROOT, sizeof(newProjectPath_) - 1);
    newProjectPath_[sizeof(newProjectPath_) - 1] = '\0';
    openBrowsePath_ = std::string(NE_PROJECT_ROOT);
}

void EditorUI::setPlayMode(bool play) {
    playMode_ = play;
    Time::setScale(play ? 1.0f : 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Style — dark professional look inspired by Godot / modern editors
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::applyEditorStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // Rounding
    style.WindowRounding    = 6.0f;
    style.ChildRounding     = 5.0f;
    style.FrameRounding     = 4.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding      = 4.0f;
    style.TabRounding       = 5.0f;

    // Spacing
    style.WindowPadding    = ImVec2(10, 10);
    style.FramePadding     = ImVec2(8, 5);
    style.ItemSpacing      = ImVec2(10, 6);
    style.ItemInnerSpacing = ImVec2(8, 5);
    style.IndentSpacing    = 20.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize  = 1.0f; // Crisp outlines for premium look
    style.TabBorderSize    = 0.0f;

    // Colors — Deep charcoal/obsidian modern theme
    ImVec4* c = style.Colors;
    ImVec4 bg       = ImVec4(0.078f, 0.078f, 0.086f, 1.0f);   // Deep obsidian dark (#141416)
    ImVec4 bgChild  = ImVec4(0.106f, 0.106f, 0.118f, 1.0f);   // Very dark charcoal (#1B1B1E)
    ImVec4 bgPopup  = ImVec4(0.118f, 0.118f, 0.133f, 1.0f);   // Slate black (#1E1E22)
    ImVec4 accent   = ImVec4(0.231f, 0.510f, 0.965f, 1.0f);   // Electric Blue accent (#3B82F6)
    ImVec4 accentH  = ImVec4(0.329f, 0.608f, 0.980f, 1.0f);   // Hover accent blue (#549BFE)
    ImVec4 accentA  = ImVec4(0.149f, 0.396f, 0.816f, 1.0f);   // Active accent blue (#2665D0)
    ImVec4 text     = ImVec4(0.890f, 0.890f, 0.902f, 1.0f);   // Crisp off-white (#E3E3E6)
    ImVec4 textDim  = ImVec4(0.470f, 0.470f, 0.500f, 1.0f);   // Muted gray-blue (#787880)
    ImVec4 border   = ImVec4(0.137f, 0.137f, 0.153f, 1.0f);   // Subtle dark border (#232327)
    ImVec4 header   = ImVec4(0.141f, 0.141f, 0.157f, 1.0f);   // Clean obsidian panels (#242428)
    ImVec4 headerH  = ImVec4(0.176f, 0.176f, 0.196f, 1.0f);   // Hovered panels (#2D2D32)

    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = textDim;
    c[ImGuiCol_WindowBg]              = bg;
    c[ImGuiCol_ChildBg]               = bgChild;
    c[ImGuiCol_PopupBg]               = bgPopup;
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = ImVec4(0.051f, 0.051f, 0.059f, 1.0f);   // Ultra dark input fields (#0D0D0F)
    c[ImGuiCol_FrameBgHovered]        = ImVec4(0.086f, 0.086f, 0.098f, 1.0f);   // Slightly lighter on hover (#161619)
    c[ImGuiCol_FrameBgActive]         = ImVec4(0.118f, 0.118f, 0.133f, 1.0f);
    c[ImGuiCol_TitleBg]               = ImVec4(0.140f, 0.140f, 0.150f, 1.0f);
    c[ImGuiCol_TitleBgActive]         = ImVec4(0.170f, 0.170f, 0.180f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.120f, 0.120f, 0.130f, 1.0f);
    c[ImGuiCol_MenuBarBg]             = ImVec4(0.160f, 0.160f, 0.170f, 1.0f);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0.130f, 0.130f, 0.140f, 1.0f);
    c[ImGuiCol_ScrollbarGrab]         = ImVec4(0.300f, 0.300f, 0.320f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.400f, 0.400f, 0.420f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]   = accent;
    c[ImGuiCol_CheckMark]             = accent;
    c[ImGuiCol_SliderGrab]            = accent;
    c[ImGuiCol_SliderGrabActive]      = accentH;
    c[ImGuiCol_Button]                = header;
    c[ImGuiCol_ButtonHovered]         = headerH;
    c[ImGuiCol_ButtonActive]          = accentA;
    c[ImGuiCol_Header]                = header;
    c[ImGuiCol_HeaderHovered]         = headerH;
    c[ImGuiCol_HeaderActive]          = accentA;
    c[ImGuiCol_Separator]             = border;
    c[ImGuiCol_SeparatorHovered]      = accent;
    c[ImGuiCol_SeparatorActive]       = accentH;
    c[ImGuiCol_ResizeGrip]            = ImVec4(0.26f, 0.26f, 0.28f, 1.0f);
    c[ImGuiCol_ResizeGripHovered]     = accent;
    c[ImGuiCol_ResizeGripActive]      = accentH;
    c[ImGuiCol_Tab]                   = header;
    c[ImGuiCol_TabHovered]            = accentH;
    c[ImGuiCol_TabSelected]           = accent;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = ImVec4(0.180f, 0.180f, 0.190f, 1.0f);
    c[ImGuiCol_TabDimmedSelected]     = ImVec4(0.220f, 0.220f, 0.235f, 1.0f);
    c[ImGuiCol_DockingPreview]        = ImVec4(accent.x, accent.y, accent.z, 0.7f);
    c[ImGuiCol_DockingEmptyBg]        = ImVec4(0.12f, 0.12f, 0.13f, 1.0f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_NavHighlight]          = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.50f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main draw entry point
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::draw(Scene* scene, Camera* camera, Project* project, ResourceManager* resources, float dt) {
    // Full-viewport dockspace with passthrough so the 3D render shows behind.
    // We use a fixed ID so the DockBuilder setup below targets the right node.
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoTitleBar
                               | ImGuiWindowFlags_NoCollapse
                               | ImGuiWindowFlags_NoResize
                               | ImGuiWindowFlags_NoMove
                               | ImGuiWindowFlags_NoBringToFrontOnFocus
                               | ImGuiWindowFlags_NoNavFocus
                               | ImGuiWindowFlags_NoDocking
                               | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

    // Build the default dock layout once.
    if (!dockLayoutBuilt_) {
        dockLayoutBuilt_ = true;

        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        // Split: left (18%) | rest
        ImGuiID dockLeft, dockRemaining;
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.18f, &dockLeft, &dockRemaining);

        // Split remaining: rest | right (22%)
        ImGuiID dockRight, dockCenter;
        ImGui::DockBuilderSplitNode(dockRemaining, ImGuiDir_Right, 0.22f, &dockRight, &dockCenter);

        // Split center: viewport | bottom (25% of center height)
        ImGuiID dockBottom, dockViewport;
        ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.25f, &dockBottom, &dockViewport);

        ImGui::DockBuilderDockWindow("Scene Tree",   dockLeft);
        ImGui::DockBuilderDockWindow("Inspector",    dockRight);
        ImGui::DockBuilderDockWindow("File Browser", dockBottom);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    if (ImGuiDockNode* centralNode = ImGui::DockBuilderGetCentralNode(dockspaceId)) {
        viewportPos_ = glm::vec2(centralNode->Pos.x, centralNode->Pos.y);
        viewportSize_ = glm::vec2(centralNode->Size.x, centralNode->Size.y);
    }

    ImGui::End();  // DockSpaceHost

    drawMenuBar(project, scene);

    if (showSceneTree_)   drawSceneTree(scene);
    if (showInspector_)   drawInspector();
    if (showFileBrowser_) drawFileBrowser(project, resources);

    drawViewportOverlay(camera, dt);
    drawGizmo(camera, scene);

    // Modal dialogs.
    drawAboutWindow();
    drawBuildWindow(project);
    drawSettingsWindow(project);
    drawNewProjectDialog(project);
    drawOpenProjectDialog(project);
}

// ─────────────────────────────────────────────────────────────────────────────
// Menu bar
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawMenuBar(Project* project, Scene* scene) {
    if (ImGui::BeginMainMenuBar()) {
        // ── 1. File Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) {
                showNewProjectDialog_ = true;
            }
            if (ImGui::MenuItem("Open Project...")) {
                showOpenProjectDialog_ = true;
                openBrowsePath_ = std::string(NE_PROJECT_ROOT);
            }
            bool hasProject = project && project->isLoaded();
            if (ImGui::MenuItem("Save Project", nullptr, false, hasProject)) {
                if (project) project->save();
            }
            ImGui::Separator();
            if (hasProject) {
                ImGui::TextDisabled("  Project: %s", project->name().c_str());
            } else {
                ImGui::TextDisabled("  (no project)");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("New Scene"))        { /* TODO */ }
            if (ImGui::MenuItem("Open Scene..."))    { /* TODO */ }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene"))       { /* TODO */ }
            if (ImGui::MenuItem("Save Scene As...")) { /* TODO */ }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Esc"))      { quitRequested_ = true; }
            ImGui::EndMenu();
        }

        // ── 2. Edit Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z"))   { /* TODO */ }
            if (ImGui::MenuItem("Redo", "Ctrl+Y"))   { /* TODO */ }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C"))   { /* TODO */ }
            if (ImGui::MenuItem("Paste", "Ctrl+V"))  { /* TODO */ }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) { /* TODO */ }
            ImGui::EndMenu();
        }

        // ── 3. View Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Tree",       nullptr, &showSceneTree_);
            ImGui::MenuItem("Inspector",        nullptr, &showInspector_);
            ImGui::MenuItem("File Browser",     nullptr, &showFileBrowser_);
            ImGui::Separator();
            ImGui::MenuItem("Viewport Overlay", nullptr, &showViewportOverlay_);
            ImGui::EndMenu();
        }

        // ── 4. Scene Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("Scene")) {
            Node* parentNode = selectedNode_ ? selectedNode_ : scene;
            bool hasParent = parentNode != nullptr;
            if (ImGui::MenuItem("Add Node", nullptr, false, hasParent)) {
                nodeToCreateChildUnder_ = parentNode;
                createType_ = 1;
            }
            if (ImGui::BeginMenu("Create 3D Object", hasParent)) {
                if (ImGui::MenuItem("Cube")) {
                    nodeToCreateChildUnder_ = parentNode;
                    createType_ = 2;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Create Light", hasParent)) {
                if (ImGui::MenuItem("Directional Light")) {
                    nodeToCreateChildUnder_ = parentNode;
                    createType_ = 3;
                }
                if (ImGui::MenuItem("Point Light")) {
                    nodeToCreateChildUnder_ = parentNode;
                    createType_ = 4;
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            bool canDelete = selectedNode_ != nullptr && selectedNode_->parent() != nullptr;
            if (ImGui::MenuItem("Delete Selected", "Del", false, canDelete)) {
                nodeToDelete_ = selectedNode_;
            }
            ImGui::EndMenu();
        }

        // ── 5. Settings Menu ───────────────────────────────────────────
        if (ImGui::BeginMenu("Settings")) {
            bool hasProject = project && project->isLoaded();
            if (ImGui::MenuItem("Settings", nullptr, false, hasProject)) {
                showSettingsWindow_ = true;
            }
            ImGui::EndMenu();
        }

        // ── 6. Build Menu ─────────────────────────────────────────────
        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Build")) {
                // Same action as the build button
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B")) {
                showBuildWindow_ = true;
            }
            ImGui::EndMenu();
        }

        // ── 7. Help Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About NextEngine")) {
                showAboutWindow_ = true;
            }
            ImGui::EndMenu();
        }

        // ── Right-aligned Scene / Play mode toggle ──────────────────────
        {
            float contentWidth = ImGui::GetContentRegionAvail().x;
            float buttonWidth  = 160.0f;
            ImGui::SameLine(ImGui::GetCursorPosX() + contentWidth - buttonWidth);

            bool sceneActive = !playMode_;
            if (sceneActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.62f, 0.90f, 1.0f));
            }
            if (ImGui::SmallButton("Scene##mode")) { setPlayMode(false); }
            if (sceneActive) ImGui::PopStyleColor(2);

            ImGui::SameLine();

            bool playActive = playMode_;
            if (playActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.70f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.78f, 0.40f, 1.0f));
            }
            if (ImGui::SmallButton("Play##mode")) { setPlayMode(true); }
            if (playActive) ImGui::PopStyleColor(2);
        }

        ImGui::EndMainMenuBar();
    }
}

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

// ─────────────────────────────────────────────────────────────────────────────
// Scene Tree
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawSceneTree(Scene* scene) {
    ImGui::Begin("Scene Tree", &showSceneTree_);

    ImGui::PushItemWidth(-1);
    ImGui::InputTextWithHint("##SceneTreeSearch", "Search by name or type...", sceneTreeSearchBuf_, sizeof(sceneTreeSearchBuf_));
    ImGui::PopItemWidth();
    ImGui::Separator();

    if (scene) {
        drawSceneTreeNode(scene);
    } else {
        ImGui::TextDisabled("(no scene)");
    }

    // Deselect when clicking empty space.
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)
        && !ImGui::IsAnyItemHovered()) {
        selectedNode_ = nullptr;
    }

    // Right-click context menu in empty window space
    if (ImGui::BeginPopupContextWindow("SceneTreeContext", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
        if (scene) {
            if (ImGui::BeginMenu("Create Node")) {
                if (ImGui::MenuItem("Node")) {
                    nodeToCreateChildUnder_ = scene;
                    createType_ = 1;
                }
                if (ImGui::MenuItem("MeshNode (Cube)")) {
                    nodeToCreateChildUnder_ = scene;
                    createType_ = 2;
                }
                if (ImGui::MenuItem("Directional Light")) {
                    nodeToCreateChildUnder_ = scene;
                    createType_ = 3;
                }
                if (ImGui::MenuItem("Point Light")) {
                    nodeToCreateChildUnder_ = scene;
                    createType_ = 4;
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }

    // Handle deferred creations/deletions after the recursion finishes to avoid iterator invalidation
    if (nodeToDelete_) {
        Node* p = nodeToDelete_->parent();
        if (p) {
            if (selectedNode_ && (selectedNode_ == nodeToDelete_ || isDescendantOf(selectedNode_, nodeToDelete_))) {
                selectedNode_ = nullptr;
            }
            p->removeChild(nodeToDelete_);
        }
        nodeToDelete_ = nullptr;
    }

    if (nodeToReparent_ && newParent_) {
        if (Node* oldParent = nodeToReparent_->parent()) {
            std::unique_ptr<Node> detached = oldParent->detachChild(nodeToReparent_);
            if (detached) {
                newParent_->addChild(std::move(detached));
            }
        }
        nodeToReparent_ = nullptr;
        newParent_ = nullptr;
    }

    if (nodeToCreateChildUnder_ && createType_ != 0) {
        // Find default mesh/material in the scene
        Mesh* defaultMesh = nullptr;
        Material* defaultMaterial = nullptr;
        if (scene) {
            scene->traverse([&](Node& n, const glm::mat4&) {
                if (auto* mn = dynamic_cast<MeshNode*>(&n)) {
                    if (!defaultMesh) {
                        defaultMesh = mn->mesh();
                        defaultMaterial = mn->material();
                    }
                }
            });
        }

        if (createType_ == 1) {
            nodeToCreateChildUnder_->createChild<Node>("Node");
        } else if (createType_ == 2) {
            nodeToCreateChildUnder_->createChild<MeshNode>("MeshNode", defaultMesh, defaultMaterial);
        } else if (createType_ == 3) {
            nodeToCreateChildUnder_->createChild<LightNode>("Directional Light", LightType::Directional);
        } else if (createType_ == 4) {
            nodeToCreateChildUnder_->createChild<LightNode>("Point Light", LightType::Point);
        } else if (createType_ == 5) {
            std::filesystem::path p(draggedScenePath_);
            // Crée un nœud conteneur avec le nom de la scène.
            // À terme, le sérialiseur (Claude) chargera l'arbre depuis p.string() et l'attachera ici.
            Node* instance = nodeToCreateChildUnder_->createChild<Node>(p.stem().string() + " (Scene)");
            (void)instance; // évite l'avertissement unused variable
        }

        nodeToCreateChildUnder_ = nullptr;
        createType_ = 0;
    }

    ImGui::End();
}

void EditorUI::drawSceneTreeNode(Node* node) {
    std::string query = toLower(sceneTreeSearchBuf_);
    if (!nodeMatchesSearch(node, query)) {
        return; // Filter out this node and its children
    }

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                             | ImGuiTreeNodeFlags_OpenOnDoubleClick
                             | ImGuiTreeNodeFlags_SpanAvailWidth;

    bool isLeaf = node->children().empty();
    if (isLeaf)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (node == selectedNode_)
        flags |= ImGuiTreeNodeFlags_Selected;
    
    // Auto-expand if searching or if it's the root node
    if (!node->parent() || !query.empty())
        flags |= ImGuiTreeNodeFlags_DefaultOpen;

    const char* icon = nodeTypeIcon(node);
    bool open = false;

    if (nodeToRename_ == node) {
        // Draw the node hierarchy properly but with an input text over it
        open = ImGui::TreeNodeEx(reinterpret_cast<void*>(node), flags, "%s", icon);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##rename_node", nodeRenameBuf_, sizeof(nodeRenameBuf_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            node->setName(nodeRenameBuf_);
            nodeToRename_ = nullptr;
        } else if (ImGui::IsItemDeactivated() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
            node->setName(nodeRenameBuf_);
            nodeToRename_ = nullptr;
        }
        ImGui::PopStyleVar();
    } else {
        open = ImGui::TreeNodeEx(reinterpret_cast<void*>(node), flags, "%s %s", icon, node->name().c_str());
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
                nodeToReparent_ = draggedNode;
                newParent_ = node;
            }
        }
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_SCENE")) {
            nodeToCreateChildUnder_ = node;
            createType_ = 5;
            draggedScenePath_ = (const char*)payload->Data;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        selectedNode_ = node;

    // Right-click context menu on individual node
    if (ImGui::BeginPopupContextItem()) {
        selectedNode_ = node; // Select on right-click

        if (ImGui::MenuItem("Rename")) {
            nodeToRename_ = node;
            std::strncpy(nodeRenameBuf_, node->name().c_str(), sizeof(nodeRenameBuf_) - 1);
            nodeRenameBuf_[sizeof(nodeRenameBuf_) - 1] = '\0';
        }
        ImGui::Separator();

        if (ImGui::BeginMenu("Create Child")) {
            if (ImGui::MenuItem("Node")) {
                nodeToCreateChildUnder_ = node;
                createType_ = 1;
            }
            if (ImGui::MenuItem("MeshNode (Cube)")) {
                nodeToCreateChildUnder_ = node;
                createType_ = 2;
            }
            if (ImGui::MenuItem("Directional Light")) {
                nodeToCreateChildUnder_ = node;
                createType_ = 3;
            }
            if (ImGui::MenuItem("Point Light")) {
                nodeToCreateChildUnder_ = node;
                createType_ = 4;
            }
            ImGui::EndMenu();
        }

        // Deleting node (cannot delete root Scene node!)
        if (node->parent()) {
            ImGui::Separator();
            if (ImGui::MenuItem("Delete Node")) {
                nodeToDelete_ = node;
            }
        }

        ImGui::EndPopup();
    }

    if (open && !isLeaf) {
        for (auto& child : node->children())
            drawSceneTreeNode(child.get());
        ImGui::TreePop();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Inspector
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawInspector() {
    ImGui::Begin("Inspector", &showInspector_);

    if (!selectedNode_) {
        ImGui::TextDisabled("Select a node in the Scene Tree");
        ImGui::End();
        return;
    }

    Node* node = selectedNode_;

    // ── Node info ────────────────────────────────────────────────────────
    ImGui::SeparatorText("Node");
    ImGui::Text("Name: %s", node->name().c_str());
    ImGui::Text("Type: %s", nodeTypeLabel(node));

    // ── Transform ────────────────────────────────────────────────────────
    ImGui::SeparatorText("Transform");
    {
        Transform& t = node->transform();
        ImGui::DragFloat3("Position", &t.position.x, 0.05f);

        glm::vec3 euler = quatToEulerDeg(t.rotation);
        if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f))
            t.rotation = eulerDegToQuat(euler);

        ImGui::DragFloat3("Scale", &t.scale.x, 0.01f, 0.001f, 100.0f);
    }

    // ── MeshNode ─────────────────────────────────────────────────────────
    if (auto* meshNode = dynamic_cast<MeshNode*>(node)) {
        ImGui::SeparatorText("Mesh");
        ImGui::Text("Mesh: (assigned)");
        if (meshNode->material())
            ImGui::Text("Material: (assigned)");
            
        ImGui::Spacing();
        ImGui::Checkbox("Cast Shadows", &meshNode->castShadows());
        ImGui::Checkbox("Include to light baking", &meshNode->includeInLightBaking());
    }

    // ── LightNode ────────────────────────────────────────────────────────
    LightNode* light = node->asLight();
    if (light) {
        ImGui::SeparatorText("Light");
        ImGui::Text("Type: %s", lightTypeLabel(light->type));
        ImGui::ColorEdit3("Color", &light->color.x);
        ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 20.0f);

        if (light->type == LightType::Directional)
            ImGui::SliderFloat3("Direction", &light->direction.x, -1.0f, 1.0f);
        if (light->type == LightType::Point)
            ImGui::DragFloat("Range", &light->range, 0.1f, 0.1f, 100.0f);
    }

    // ── Behaviours ───────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// File Browser — shows the contents of the loaded project
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawFileBrowser(Project* project, ResourceManager* resources) {
    ImGui::Begin("File Browser", &showFileBrowser_);

    if (!project || !project->isLoaded()) {
        ImGui::TextDisabled("No project loaded.");
        ImGui::TextDisabled("Use Project > New / Open to get started.");
        ImGui::End();
        return;
    }

    namespace fs = std::filesystem;
    fs::path root(project->rootPath());

    // If the current browse path is empty or outside the project, reset it.
    if (currentBrowsePath_.empty() ||
        currentBrowsePath_.find(project->rootPath()) == std::string::npos) {
        currentBrowsePath_ = project->rootPath();
    }

    fs::path browsePath(currentBrowsePath_);

    // ── Header: Breadcrumb Navigation ─────────────────────────────
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    
    // Project root button
    if (ImGui::Button(project->name().c_str())) {
        currentBrowsePath_ = root.string();
    }

    if (browsePath != root) {
        std::vector<fs::path> components;
        fs::path p = browsePath;
        // Collect path components up to root
        while (p != root && !p.empty() && p.string().find(root.string()) != std::string::npos) {
            components.push_back(p);
            p = p.parent_path();
        }
        std::reverse(components.begin(), components.end());

        for (const auto& comp : components) {
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.0f, 4.0f);
            if (ImGui::Button(comp.filename().string().c_str())) {
                currentBrowsePath_ = comp.string();
            }
        }
    }
    ImGui::PopStyleColor();

    ImGui::SameLine(ImGui::GetWindowWidth() - 360.0f);
    ImGui::SetNextItemWidth(200.0f);
    ImGui::InputTextWithHint("##FileSearch", "Search files...", fileBrowserSearchBuf_, sizeof(fileBrowserSearchBuf_));

    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::SliderFloat("##Zoom", &fileBrowserZoom_, 0.5f, 3.0f, "Zoom %.1f");

    if (ImGui::IsWindowHovered()) {
        float scroll = ImGui::GetIO().MouseWheel;
        if (scroll != 0.0f && ImGui::GetIO().KeyCtrl) {
            fileBrowserZoom_ += scroll * 0.15f; // Faster zoom with scroll
            fileBrowserZoom_ = std::clamp(fileBrowserZoom_, 0.5f, 3.0f);
        }
    }

    ImGui::Separator();

    // ── List entries ─────────────────────────────────────────────────────
    try {
        std::vector<fs::directory_entry> dirs;
        std::vector<fs::directory_entry> files;
        std::string query = toLower(fileBrowserSearchBuf_);

        if (query.empty()) {
            for (auto& entry : fs::directory_iterator(browsePath)) {
                auto name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue;
                if (name == "build") continue;

                if (entry.is_directory()) dirs.push_back(entry);
                else files.push_back(entry);
            }
        } else {
            // Recursive search when a query is entered
            for (auto& entry : fs::recursive_directory_iterator(project->rootPath())) {
                auto name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue;
                if (name == "build") continue;

                std::string nameLower = toLower(name);
                std::string extLower = toLower(entry.path().extension().string());
                
                if (nameLower.find(query) != std::string::npos || extLower.find(query) != std::string::npos) {
                    if (entry.is_directory()) dirs.push_back(entry);
                    else files.push_back(entry);
                }
            }
        }

        auto cmp = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename() < b.path().filename();
        };
        std::sort(dirs.begin(), dirs.end(), cmp);
        std::sort(files.begin(), files.end(), cmp);

        bool useGrid = fileBrowserZoom_ > 1.0f;
        float iconSize = 64.0f * fileBrowserZoom_;
        float cellSize = iconSize + 20.0f;

        if (useGrid) {
            float panelWidth = ImGui::GetContentRegionAvail().x;
            int columnCount = std::max(1, static_cast<int>(panelWidth / cellSize));
            ImGui::Columns(columnCount, nullptr, false);
        }

        std::string pathToDelete_;

        auto drawItemContextMenu = [&](const std::string& pathStr, const std::string& filename) {
            if (ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename")) {
                    fileToRename_ = pathStr;
                    std::strncpy(fileRenameBuf_, filename.c_str(), sizeof(fileRenameBuf_) - 1);
                    fileRenameBuf_[sizeof(fileRenameBuf_) - 1] = '\0';
                }
                if (ImGui::MenuItem("Delete")) {
                    pathToDelete_ = pathStr;
                }
                ImGui::EndPopup();
            }
        };

        auto handleInlineRename = [&](const std::string& pathStr, float width) {
            if (fileToRename_ == pathStr) {
                ImGui::SetNextItemWidth(width);
                ImGui::SetKeyboardFocusHere();
                if (ImGui::InputText(("##rename_" + pathStr).c_str(), fileRenameBuf_, sizeof(fileRenameBuf_), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
                    try { fs::rename(fileToRename_, fs::path(fileToRename_).parent_path() / fileRenameBuf_); } catch(...) {}
                    fileToRename_.clear();
                } else if (ImGui::IsItemDeactivated() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                    try { fs::rename(fileToRename_, fs::path(fileToRename_).parent_path() / fileRenameBuf_); } catch(...) {}
                    fileToRename_.clear();
                }
                return true;
            }
            return false;
        };

        char buffer[256];
        for (auto& d : dirs) {
            std::string pathStr = d.path().string();
            std::string filename = d.path().filename().string();
            if (useGrid) {
                ImGui::BeginGroup();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
                ImGui::Button(("[DIR]\n" + filename).c_str(), ImVec2(iconSize, iconSize));
                ImGui::PopStyleColor();
                drawItemContextMenu(pathStr, filename);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    currentBrowsePath_ = pathStr;
                
                if (!handleInlineRename(pathStr, iconSize)) {
                    ImGui::TextWrapped("%s", filename.c_str());
                }
                ImGui::EndGroup();
                ImGui::NextColumn();
            } else {
                if (!handleInlineRename(pathStr, -1.0f)) {
                    std::snprintf(buffer, sizeof(buffer), "[D] %s", filename.c_str());
                    if (ImGui::Selectable(buffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            currentBrowsePath_ = pathStr;
                    }
                    drawItemContextMenu(pathStr, filename);
                }
            }
        }
        for (auto& f : files) {
            std::string pathStr = f.path().string();
            std::string filename = f.path().filename().string();
            auto ext = f.path().extension().string();
            
            if (useGrid) {
                ImGui::BeginGroup();
                
                if (ext == ".png" || ext == ".jpg" || ext == ".bmp") {
                    Texture* tex = resources->loadTexture(f.path().string());
                    if (tex && tex->getImGuiTextureID()) {
                        ImGui::Image(tex->getImGuiTextureID(), ImVec2(iconSize, iconSize));
                    } else {
                        ImGui::Button("[IMG]", ImVec2(iconSize, iconSize));
                    }
                } else if (ext == ".scene") {
                    ImGui::Button("[SCENE]", ImVec2(iconSize, iconSize));
                } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
                    ImGui::Button("[AUDIO]", ImVec2(iconSize, iconSize));
                } else if (ext == ".obj" || ext == ".fbx" || ext == ".gltf") {
                    ImGui::Button("[3D]", ImVec2(iconSize, iconSize));
                } else {
                    ImGui::Button("[FILE]", ImVec2(iconSize, iconSize));
                }

                if (ext == ".scene") {
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("FILE_SCENE", pathStr.c_str(), pathStr.size() + 1);
                        ImGui::Text("Instantiate %s", filename.c_str());
                        ImGui::EndDragDropSource();
                    }
                }

                drawItemContextMenu(pathStr, filename);

                if (!handleInlineRename(pathStr, iconSize)) {
                    ImGui::TextWrapped("%s", filename.c_str());
                }
                ImGui::EndGroup();
                ImGui::NextColumn();
            } else {
                if (!handleInlineRename(pathStr, -1.0f)) {
                    std::snprintf(buffer, sizeof(buffer), "%s %s", fileIcon(f), filename.c_str());
                    ImGui::Selectable(buffer);
                    drawItemContextMenu(pathStr, filename);

                }
                
                // Drag source for .scene files in list mode
                if (ext == ".scene") {
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("FILE_SCENE", pathStr.c_str(), pathStr.size() + 1);
                        ImGui::Text("Instantiate %s", filename.c_str());
                        ImGui::EndDragDropSource();
                    }
                }
            }
        }
        
        if (!pathToDelete_.empty()) {
            try { fs::remove_all(pathToDelete_); } catch (...) {}
        }

        if (useGrid) {
            ImGui::Columns(1);
        }
    } catch (const fs::filesystem_error&) {
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error reading directory");
    }

    ImGui::End();
}

// ─────────────────────────────────────────────────────────────────────────────
// Viewport overlay (FPS, camera, mode indicator)
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawViewportOverlay(Camera* camera, float dt) {
    if (!showViewportOverlay_) return;
    (void)dt;

    ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration
                                  | ImGuiWindowFlags_NoDocking
                                  | ImGuiWindowFlags_AlwaysAutoResize
                                  | ImGuiWindowFlags_NoSavedSettings
                                  | ImGuiWindowFlags_NoFocusOnAppearing
                                  | ImGuiWindowFlags_NoNav
                                  | ImGuiWindowFlags_NoMove;

    // Place overlay on the top left of the central viewport area
    ImVec2 pos(viewportPos_.x + 16.0f, viewportPos_.y + 16.0f);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.60f);

    if (ImGui::Begin("##ViewportOverlay", nullptr, overlayFlags)) {
        if (playMode_)
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.4f, 1.0f), "PLAY MODE");
        else
            ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "SCENE MODE");

        ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);

        if (camera) {
            ImGui::Text("Cam: %.1f %.1f %.1f",
                camera->position.x, camera->position.y, camera->position.z);
        }
        ImGui::TextDisabled("TAB: toggle cursor");
    }
    ImGui::End();

    // Toolbar on the left, centered vertically in the viewport
    ImVec2 toolbarPos(viewportPos_.x + 16.0f, viewportPos_.y + viewportSize_.y * 0.5f - 60.0f);
    ImGui::SetNextWindowPos(toolbarPos, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.60f);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    if (ImGui::Begin("##Toolbar", nullptr, overlayFlags)) {
        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.290f, 0.565f, 0.851f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.350f, 0.620f, 0.900f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.250f, 0.500f, 0.800f, 1.0f));
        
        if (ImGui::Selectable(" T ", gizmoMode_ == 0, 0, ImVec2(24, 24))) gizmoMode_ = 0;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate (T)");
        if (ImGui::Selectable(" R ", gizmoMode_ == 1, 0, ImVec2(24, 24))) gizmoMode_ = 1;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate (R)");
        if (ImGui::Selectable(" S ", gizmoMode_ == 2, 0, ImVec2(24, 24))) gizmoMode_ = 2;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale (S)");
        
        ImGui::PopStyleColor(3);
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

// ─────────────────────────────────────────────────────────────────────────────
// New Project dialog (modal popup)
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawNewProjectDialog(Project* project) {
    if (showNewProjectDialog_) {
        ImGui::OpenPopup("New Project");
        showNewProjectDialog_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Create a new NextEngine project.");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Project Name:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##projname", newProjectName_, sizeof(newProjectName_));

        ImGui::Spacing();
        ImGui::Text("Parent Directory:");
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##projpath", newProjectPath_, sizeof(newProjectPath_));

        ImGui::Spacing();

        // Preview the full path.
        namespace fs = std::filesystem;
        fs::path fullPath = fs::path(newProjectPath_) / newProjectName_;
        ImGui::TextDisabled("Will create: %s", fullPath.string().c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool nameValid = std::strlen(newProjectName_) > 0;
        bool pathValid = std::strlen(newProjectPath_) > 0 && fs::is_directory(newProjectPath_);

        if (!pathValid && std::strlen(newProjectPath_) > 0)
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Parent directory does not exist.");

        if (ImGui::Button("Create", ImVec2(120, 0)) && nameValid && pathValid) {
            if (project) {
                project->create(newProjectPath_, newProjectName_);
                currentBrowsePath_ = project->rootPath();
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Open Project dialog (modal popup with integrated file browser)
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawOpenProjectDialog(Project* project) {
    if (showOpenProjectDialog_) {
        ImGui::OpenPopup("Open Project");
        showOpenProjectDialog_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(600, 420), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_None)) {
        ImGui::Text("Select a .neproj file:");
        ImGui::Separator();

        namespace fs = std::filesystem;
        fs::path browsePath(openBrowsePath_);

        // Current path.
        ImGui::TextDisabled("Path: %s", browsePath.string().c_str());
        ImGui::Separator();

        // ".." to go up.
        if (browsePath.has_parent_path() && browsePath.parent_path() != browsePath) {
            if (ImGui::Selectable("[D] .."))
                openBrowsePath_ = browsePath.parent_path().string();
        }

        // List entries in a scrollable region.
        ImGui::BeginChild("##OpenBrowse", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), ImGuiChildFlags_Borders);
        try {
            std::vector<fs::directory_entry> dirs;
            std::vector<fs::directory_entry> projFiles;

            for (auto& entry : fs::directory_iterator(browsePath)) {
                auto name = entry.path().filename().string();
                if (name.empty() || name[0] == '.') continue;

                if (entry.is_directory())
                    dirs.push_back(entry);
                else if (entry.path().extension() == ".neproj")
                    projFiles.push_back(entry);
            }

            auto cmp = [](const fs::directory_entry& a, const fs::directory_entry& b) {
                return a.path().filename() < b.path().filename();
            };
            std::sort(dirs.begin(), dirs.end(), cmp);
            std::sort(projFiles.begin(), projFiles.end(), cmp);

            char buffer[256];
            for (auto& d : dirs) {
                std::snprintf(buffer, sizeof(buffer), "[D] %s", d.path().filename().string().c_str());
                if (ImGui::Selectable(buffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        openBrowsePath_ = d.path().string();
                }
            }
            for (auto& f : projFiles) {
                std::snprintf(buffer, sizeof(buffer), "[Proj] %s", f.path().filename().string().c_str());
                if (ImGui::Selectable(buffer, false, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        if (project) {
                            project->load(f.path().string());
                            currentBrowsePath_ = project->rootPath();
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
        } catch (const fs::filesystem_error&) {
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "Error reading directory");
        }
        ImGui::EndChild();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// About NextEngine dialog (modal popup)
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawAboutWindow() {
    if (showAboutWindow_) {
        ImGui::OpenPopup("About NextEngine");
        showAboutWindow_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("About NextEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Spacing();
        
        // Brand logo/accent banner using C++11 raw string literal
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.290f, 0.565f, 0.851f, 1.0f)); // Brand Accent Blue
        ImGui::Text(R"(  _  _           _   ___            _            )");
        ImGui::Text(R"( | \| |___ __ _| |_| __|_ _  __ _ (_)_ _  ___    )");
        ImGui::Text(R"( | .` / -_) \ /|  _| _|| ' \/ _` || | ' \/ -_)   )");
        ImGui::Text(R"( |_|\_\___/_\_\ \__|___|_||_\__, | |_|_|_\___|   )");
        ImGui::Text(R"(                            |___/                )");
        ImGui::PopStyleColor();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(" NextEngine C++ Game Engine");
        ImGui::Text(" Version: "); ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.30f, 0.70f, 0.35f, 1.0f), "Alpha 0.0.1"); // Sleek Green

        ImGui::Spacing();
        ImGui::Text(" A lightweight, Vulkan-powered 3D editor and runtime.");
        ImGui::Text(" Designed for modularity, speed, and simplicity.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled(" Powered by Vulkan, GLFW, Dear ImGui, and GLM.");
        ImGui::TextDisabled(" Copyright (c) 2026 NextEngine Contributors.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Close button aligned nicely
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 116.0f);
        if (ImGui::Button("Close", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Build Settings dialog (modal popup)
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawBuildWindow(Project* project) {
    if (showBuildWindow_) {
        ImGui::OpenPopup("Build Settings");
        showBuildWindow_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680, 500), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Build Settings", nullptr, ImGuiWindowFlags_None)) {
        if (project && project->isLoaded()) {
            ImGui::Text("Configure build settings for project: %s", project->name().c_str());
        } else {
            ImGui::Text("Configure build targets, platforms, and optimization parameters.");
        }
        ImGui::Separator();
        ImGui::Spacing();

        // Left Column: Platform List Sidebar
        ImGui::BeginChild("##PlatformList", ImVec2(200, -ImGui::GetFrameHeightWithSpacing() - 10), ImGuiChildFlags_Borders);
        {
            const char* platforms[] = { "Windows (Direct3D/Vulkan)", "Meta Quest (XR SDK)", "Linux (Vulkan)", "WebGL (WebAssembly)" };
            for (int i = 0; i < 4; ++i) {
                bool isSelected = (selectedBuildPlatform_ == i);
                
                // Platforms customized with badges
                char label[64];
                if (i == 0) std::snprintf(label, sizeof(label), " [Win] %s", platforms[i]);
                else if (i == 1) std::snprintf(label, sizeof(label), " [XR]  %s", platforms[i]);
                else if (i == 2) std::snprintf(label, sizeof(label), " [Tux] %s", platforms[i]);
                else std::snprintf(label, sizeof(label), " [Web] %s", platforms[i]);

                if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_None, ImVec2(0, 32))) {
                    selectedBuildPlatform_ = i;
                }
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        // Right Column: Dynamic Settings Pane
        ImGui::BeginChild("##PlatformSettings", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 10), ImGuiChildFlags_Borders);
        {
            // ── Scenes in Build ──────────────────────────────────────────────
            ImGui::SeparatorText("Scenes in Build");
            ImGui::Checkbox("scenes/main.scene (Default Startup)", &buildSceneMainChecked_);
            ImGui::Checkbox("scenes/demo_physics.scene", &buildSceneDemoChecked_);
            ImGui::Spacing();
            ImGui::Separator();

            // ── Platform-Specific UI Panel ───────────────────────────────────
            if (selectedBuildPlatform_ == 0) {
                // Windows Platform Settings
                ImGui::SeparatorText("Windows Build Settings");
                
                ImGui::Text("Target Architecture: x86_64 (Direct3D 12 & Vulkan)");
                ImGui::Spacing();

                ImGui::Text("Build Configuration:");
                ImGui::RadioButton("Debug##win", &buildConfiguration_, 0); ImGui::SameLine();
                ImGui::RadioButton("Release##win", &buildConfiguration_, 1); ImGui::SameLine();
                ImGui::RadioButton("Profile##win", &buildConfiguration_, 2);
                
                ImGui::Spacing();
                ImGui::Text("Output Binary Directory:");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##WinOutputPath", buildOutputPath_, sizeof(buildOutputPath_));

                ImGui::Spacing();
                ImGui::Checkbox("Copy assets/ directory to output path", &buildCopyAssets_);
                ImGui::Checkbox("Enable Link-Time Optimization (LTO / -O3)", &buildEnableLto_);
            }
            else if (selectedBuildPlatform_ == 1) {
                // Meta Quest (XR) Settings
                ImGui::SeparatorText("Meta Quest (XR) Settings");
                
                ImGui::TextColored(ImVec4(0.290f, 0.565f, 0.851f, 1.0f), "[Meta OpenXR Core SDK v62.0]");
                ImGui::Spacing();

                ImGui::Text("Graphics API: Vulkan 1.3 (Stereo Foveated Rendering)");
                ImGui::Spacing();

                static int questTarget = 2; // Quest 3
                ImGui::Text("Target Hardware Device:");
                ImGui::RadioButton("Quest 2", &questTarget, 0); ImGui::SameLine();
                ImGui::RadioButton("Quest Pro", &questTarget, 1); ImGui::SameLine();
                ImGui::RadioButton("Quest 3", &questTarget, 2);

                ImGui::Spacing();
                static bool questHandTracking = true;
                ImGui::Checkbox("High-Frequency Hand Tracking (60Hz tracking)", &questHandTracking);
                
                static bool questFoveated = true;
                ImGui::Checkbox("Enable Foveation (Fixed/Dynamic Level High)", &questFoveated);

                static bool questMultiview = true;
                ImGui::Checkbox("Optimize through Mobile Multi-view (OVR_multiview2)", &questMultiview);

                ImGui::Spacing();
                ImGui::TextDisabled("Note: Meta Quest target builds require Android NDK (Clang compiler) and Vulkan mobile SPIR-V shaders compilation pipeline.");
            }
            else if (selectedBuildPlatform_ == 2) {
                // Linux Build Settings
                ImGui::SeparatorText("Linux Build Settings");

                ImGui::Text("Target Architecture: x86_64 & AArch64 (GCC/Clang)");
                ImGui::Spacing();

                ImGui::Text("Build Configuration:");
                ImGui::RadioButton("Debug##lin", &buildConfiguration_, 0); ImGui::SameLine();
                ImGui::RadioButton("Release##lin", &buildConfiguration_, 1); ImGui::SameLine();
                ImGui::RadioButton("Profile##lin", &buildConfiguration_, 2);

                ImGui::Spacing();
                ImGui::Checkbox("Statically link GCC runtime libraries (libstdc++/libgcc)", &buildCopyAssets_);
                ImGui::Checkbox("Strip local debugging symbols to reduce binary size", &buildEnableLto_);
            }
            else if (selectedBuildPlatform_ == 3) {
                // WebGL Settings
                ImGui::SeparatorText("WebGL (WebAssembly) Settings");

                ImGui::TextColored(ImVec4(0.85f, 0.65f, 0.20f, 1.0f), "[WebGL 2.0 & WebGPU Target Engines]");
                ImGui::Spacing();

                ImGui::Text("Compiler backend: Emscripten (wasm32-unknown-emscripten)");
                ImGui::Spacing();

                static bool webgpuSupport = true;
                ImGui::Checkbox("Inject WebGPU fallback shading modules", &webgpuSupport);

                static bool threadedWasm = false;
                ImGui::Checkbox("Enable Emscripten threads (SharedArrayBuffer)", &threadedWasm);

                ImGui::Spacing();
                ImGui::TextDisabled("Note: WebGL platform support requires setting up the Emscripten SDK environment in your global CMake configurations.");
            }
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::Spacing();

        // Footer buttons: Build, Build & Run, Cancel
        if (ImGui::Button("Build", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Build & Run", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 116.0f);
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

// Helper function to calculate distance from point to segment in 2D
float distanceToSegment(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    glm::vec2 ab = b - a;
    float l2 = ab.x * ab.x + ab.y * ab.y;
    if (l2 == 0.0f) return glm::length(p - a);
    float t = ((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / l2;
    t = glm::clamp(t, 0.0f, 1.0f);
    glm::vec2 projection = a + t * ab;
    return glm::length(p - projection);
}

void EditorUI::drawGizmo(Camera* camera, Scene* scene) {
    // Handle hotkeys for gizmo mode
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_T)) gizmoMode_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoMode_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_S)) gizmoMode_ = 2;
    }

    // 1. Gizmo is only active in SCENE mode and when both camera and scene are valid
    if (playMode_ || !camera || !scene) {
        grabbedAxis_ = -1;
        return;
    }

    // Determine input clicks
    ImVec2 imMousePos = ImGui::GetMousePos();
    glm::vec2 mousePos = glm::vec2(imMousePos.x, imMousePos.y);
    bool isMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool isMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    // Setup viewport details
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 vpPos = vp->WorkPos;
    ImVec2 vpSize = vp->WorkSize;

    // ViewProjection matrix
    glm::mat4 viewProj = camera->projection() * camera->view();
    glm::mat4 invVP = glm::inverse(viewProj);

    // Track if we clicked on the gizmo
    int hoveredAxis = -1;
    bool hasGizmo = (selectedNode_ != nullptr);
    glm::vec2 center2D(0.0f);
    glm::vec2 ends2D[3];
    bool axisValid[3] = { false, false, false };
    float worldLength = 0.0f;
    glm::vec3 nodePos(0.0f);

    if (hasGizmo) {
        nodePos = selectedNode_->transform().position;
        glm::vec4 clipCenter = viewProj * glm::vec4(nodePos, 1.0f);
        if (clipCenter.w > 0.0f) {
            glm::vec3 ndcCenter = glm::vec3(clipCenter) / clipCenter.w;
            center2D = glm::vec2(
                vpPos.x + (ndcCenter.x + 1.0f) * 0.5f * vpSize.x,
                vpPos.y + (ndcCenter.y + 1.0f) * 0.5f * vpSize.y
            );

            float dist = glm::length(camera->position - nodePos);
            worldLength = dist * 0.15f;
            if (worldLength < 0.01f) worldLength = 0.01f;

            glm::vec3 axes3D[3] = {
                nodePos + glm::vec3(worldLength, 0.0f, 0.0f),
                nodePos + glm::vec3(0.0f, worldLength, 0.0f),
                nodePos + glm::vec3(0.0f, 0.0f, worldLength)
            };

            for (int i = 0; i < 3; ++i) {
                glm::vec4 clipEnd = viewProj * glm::vec4(axes3D[i], 1.0f);
                if (clipEnd.w > 0.0f) {
                    glm::vec3 ndcEnd = glm::vec3(clipEnd) / clipEnd.w;
                    ends2D[i] = glm::vec2(
                        vpPos.x + (ndcEnd.x + 1.0f) * 0.5f * vpSize.x,
                        vpPos.y + (ndcEnd.y + 1.0f) * 0.5f * vpSize.y
                    );
                    axisValid[i] = true;
                }
            }

            // Check hovered axis
            if (!ImGui::GetIO().WantCaptureMouse || grabbedAxis_ != -1) {
                float closestDist = 999.0f;
                for (int i = 0; i < 3; ++i) {
                    if (!axisValid[i]) continue;
                    float dSeg = distanceToSegment(mousePos, center2D, ends2D[i]);
                    float dHead = glm::length(mousePos - ends2D[i]);
                    if (dSeg < 10.0f || dHead < 18.0f) {
                        if (dSeg < closestDist) {
                            closestDist = dSeg;
                            hoveredAxis = i;
                        }
                    }
                }
            }
        }
    }

    // Detect click to grab the gizmo
    if (isMouseClicked && hoveredAxis != -1 && grabbedAxis_ == -1 && hasGizmo) {
        grabbedAxis_ = hoveredAxis;
        dragStartNodePos_ = nodePos;
        dragStartNodeRotEuler_ = quatToEulerDeg(selectedNode_->transform().rotation);
        dragStartNodeScale_ = selectedNode_->transform().scale;
        dragStartMousePos_ = mousePos;
    }

    // Handle gizmo dragging movement
    if (grabbedAxis_ != -1 && isMouseDown && hasGizmo) {
        int axis = grabbedAxis_;
        if (axisValid[axis]) {
            glm::vec2 dir2D = ends2D[axis] - center2D;
            float len2D = glm::length(dir2D);
            if (len2D > 1.0f) {
                glm::vec2 u = dir2D / len2D;
                glm::vec2 mouseDelta = mousePos - dragStartMousePos_;
                float screenProj = glm::dot(mouseDelta, u);
                float worldDelta = screenProj * (worldLength / len2D);

                if (gizmoMode_ == 0) { // Translate
                    glm::vec3 newPos = dragStartNodePos_;
                    if (axis == 0) newPos.x += worldDelta;
                    else if (axis == 1) newPos.y += worldDelta;
                    else if (axis == 2) newPos.z += worldDelta;
                    selectedNode_->transform().position = newPos;
                } else if (gizmoMode_ == 1) { // Rotate
                    glm::vec3 newRot = dragStartNodeRotEuler_;
                    float rotDelta = screenProj * 50.0f; // Scale mouse delta to angle
                    if (axis == 0) newRot.x -= rotDelta; // Usually right moves negative on X? Adjust by feel
                    else if (axis == 1) newRot.y -= rotDelta;
                    else if (axis == 2) newRot.z -= rotDelta;
                    selectedNode_->transform().rotation = eulerDegToQuat(newRot);
                } else if (gizmoMode_ == 2) { // Scale
                    glm::vec3 newScale = dragStartNodeScale_;
                    float scaleDelta = screenProj * (worldLength / len2D) * 0.5f;
                    if (axis == 0) newScale.x += scaleDelta;
                    else if (axis == 1) newScale.y += scaleDelta;
                    else if (axis == 2) newScale.z += scaleDelta;
                    selectedNode_->transform().scale = newScale;
                }
            }
        }
    } else {
        grabbedAxis_ = -1;
    }

    // ── CLICK RAYCAST SELECTION & DESELECTION ──
    // Triggered when clicking the viewport without grabbing any gizmo handles
    if (!ImGui::GetIO().WantCaptureMouse && isMouseClicked && hoveredAxis == -1 && grabbedAxis_ == -1) {
        // 1. Generate 3D ray from 2D mouse position
        float ndcX = ((mousePos.x - vpPos.x) / vpSize.x) * 2.0f - 1.0f;
        float ndcY = ((mousePos.y - vpPos.y) / vpSize.y) * 2.0f - 1.0f;

        // In Vulkan coordinate projection mapping, near is 0.0f, far is 1.0f
        glm::vec4 nearNDC(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farNDC(ndcX, ndcY, 1.0f, 1.0f);

        glm::vec4 nearWorld4 = invVP * nearNDC;
        glm::vec3 rayOrigin = glm::vec3(nearWorld4) / nearWorld4.w;

        glm::vec4 farWorld4 = invVP * farNDC;
        glm::vec3 rayDir = glm::normalize((glm::vec3(farWorld4) / farWorld4.w) - rayOrigin);

        // 2. Perform Ray-Sphere intersection traversal
        Node* closestNode = nullptr;
        float closestT = 99999.0f;

        scene->traverse([&](Node& n, const glm::mat4& worldMatrix) {
            // Do not allow selecting the root scene node via raycasting!
            if (!n.parent()) return;

            glm::vec3 objWorldPos = glm::vec3(worldMatrix[3]);

            // Calculate world scale of object
            float sx = glm::length(glm::vec3(worldMatrix[0]));
            float sy = glm::length(glm::vec3(worldMatrix[1]));
            float sz = glm::length(glm::vec3(worldMatrix[2]));
            float maxScale = glm::max(sx, glm::max(sy, sz));

            // Bounding radius
            float radius = 0.5f * maxScale;
            if (n.mesh()) {
                radius = 1.0f * maxScale; // MeshNodes (Cubes)
            } else if (n.asLightConst()) {
                radius = 0.6f * maxScale; // LightNodes
            }

            glm::vec3 oc = rayOrigin - objWorldPos;
            float b = glm::dot(rayDir, oc);
            float c = glm::dot(oc, oc) - radius * radius;
            float discriminant = b * b - c;

            if (discriminant >= 0.0f) {
                float t = -b - glm::sqrt(discriminant);
                if (t > 0.0f && t < closestT) {
                    closestT = t;
                    closestNode = &n;
                }
            }
        });

        // 3. Apply selection or deselect
        if (closestNode) {
            selectedNode_ = closestNode;
        } else {
            selectedNode_ = nullptr; // Clicked on empty space: deselect!
        }
        return;
    }

    // ── DRAWING THE GIZMO ──
    if (!hasGizmo) return;

    // Draw the Gizmo on the Background Draw List
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    ImU32 colors[3] = {
        ImColor(239, 68, 68, 255),  // X: Bright Red (#EF4444)
        ImColor(16, 185, 129, 255), // Y: Emerald Green (#10B981)
        ImColor(59, 130, 246, 255)  // Z: Royal Blue (#3B82F6)
    };

    ImU32 hoverColors[3] = {
        ImColor(252, 165, 165, 255), // X Hover: Pinkish Red
        ImColor(110, 231, 183, 255), // Y Hover: Minty Green
        ImColor(147, 197, 253, 255)  // Z Hover: Light Blue
    };

    // Draw axes lines & arrow heads
    for (int i = 0; i < 3; ++i) {
        if (!axisValid[i]) continue;

        bool isHovered = (hoveredAxis == i || grabbedAxis_ == i);
        ImU32 col = isHovered ? hoverColors[i] : colors[i];
        float thickness = isHovered ? 4.0f : 2.5f;

        // Draw main axis segment
        drawList->AddLine(
            ImVec2(center2D.x, center2D.y),
            ImVec2(ends2D[i].x, ends2D[i].y),
            col,
            thickness
        );

        // Draw shape at the end based on gizmo mode
        if (gizmoMode_ == 0) {
            // Translate: Triangle arrow head
            glm::vec2 dir = ends2D[i] - center2D;
            float dLen = glm::length(dir);
            if (dLen > 1.0f) {
                dir = dir / dLen;
                glm::vec2 perp = glm::vec2(-dir.y, dir.x);

                float headLen = isHovered ? 14.0f : 11.0f;
                float headWidth = isHovered ? 7.0f : 5.5f;

                glm::vec2 p0 = ends2D[i] + dir * headLen;
                glm::vec2 p1 = ends2D[i] - dir * headLen + perp * headWidth;
                glm::vec2 p2 = ends2D[i] - dir * headLen - perp * headWidth;

                drawList->AddTriangleFilled(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col);
            }
        } else if (gizmoMode_ == 1) {
            // Rotate: Circle
            float radius = isHovered ? 8.0f : 6.0f;
            drawList->AddCircleFilled(ImVec2(ends2D[i].x, ends2D[i].y), radius, col);
        } else if (gizmoMode_ == 2) {
            // Scale: Box
            float halfSize = isHovered ? 6.0f : 4.5f;
            drawList->AddRectFilled(
                ImVec2(ends2D[i].x - halfSize, ends2D[i].y - halfSize),
                ImVec2(ends2D[i].x + halfSize, ends2D[i].y + halfSize),
                col
            );
        }
    }

    // Draw small center sphere (anchor point)
    drawList->AddCircleFilled(
        ImVec2(center2D.x, center2D.y),
        5.0f,
        ImColor(255, 255, 255, 220)
    );
    drawList->AddCircle(
        ImVec2(center2D.x, center2D.y),
        5.0f,
        ImColor(0, 0, 0, 255),
        0,
        1.0f
    );
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::drawSettingsWindow(Project* project) {
    if (!showSettingsWindow_) return;

    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &showSettingsWindow_)) {
        if (!project || !project->isLoaded()) {
            ImGui::TextDisabled("No project loaded.");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("SettingsTabs")) {
            
            if (ImGui::BeginTabItem("General")) {
                ImGui::Spacing();
                char nameBuf[128];
                strncpy(nameBuf, project->name().c_str(), sizeof(nameBuf));
                if (ImGui::InputText("Project Name", nameBuf, sizeof(nameBuf))) {
                    project->setName(nameBuf);
                }
                
                ImGui::SeparatorText("Metadata");
                ImGui::TextDisabled("Version: 1.0.0");
                ImGui::TextDisabled("Company Name: DefaultCompany");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rendering")) {
                ImGui::Spacing();
                ImGui::SeparatorText("Global Quality");
                
                static int shadowResolution = 1;
                const char* shadowResNames[] = { "Low (512)", "Medium (1024)", "High (2048)", "Ultra (4096)" };
                ImGui::Combo("Shadow Resolution", &shadowResolution, shadowResNames, 4);

                static int msaa = 0;
                const char* msaaNames[] = { "Off", "2x MSAA", "4x MSAA", "8x MSAA" };
                ImGui::Combo("Anti-aliasing", &msaa, msaaNames, 4);

                static bool vsync = true;
                ImGui::Checkbox("V-Sync", &vsync);
                
                ImGui::Spacing();
                ImGui::TextDisabled("(Note: These settings will be wired to the Vulkan backend later)");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Editor")) {
                ImGui::Spacing();
                ImGui::SeparatorText("Preferences");
                
                static bool autoSave = true;
                ImGui::Checkbox("Auto-save Project", &autoSave);

                static float gizmoSize = 1.0f;
                ImGui::SliderFloat("Gizmo Size", &gizmoSize, 0.1f, 3.0f);

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

} // namespace ne
