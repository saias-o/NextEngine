#include "editor/EditorUI.hpp"

#include "core/Camera.hpp"
#include "core/Paths.hpp"
#include "core/Time.hpp"
#include "editor/Command.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"
#include "project/Project.hpp"
#include "scene/LightNode.hpp"
#include "scene/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "audio/AudioManager.hpp"

#include "editor/panels/MenuBarPanel.hpp"
#include "editor/panels/SceneHierarchyPanel.hpp"
#include "editor/panels/InspectorPanel.hpp"
#include "editor/panels/FileBrowserPanel.hpp"
#include "editor/panels/ViewportPanel.hpp"

#include <memory>

#include "imgui.h"
#include "imgui_internal.h"

#include <glm/gtc/quaternion.hpp>

#include "editor/EditorApp.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

namespace ne {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

namespace {
bool intersectRayPlane(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec3& planeOrigin, const glm::vec3& planeNormal, float& t) {
    float denom = glm::dot(planeNormal, rayDir);
    if (std::abs(denom) > 1e-6f) {
        glm::vec3 p0l0 = planeOrigin - rayOrigin;
        t = glm::dot(p0l0, planeNormal) / denom;
        return (t >= 0.0f);
    }
    return false;
}
} // namespace

EditorUI::EditorUI() {
    applyEditorStyle();
    // Default open path for the "Open Project" dialog.
    std::strncpy(newProjectPath_, NE_PROJECT_ROOT, sizeof(newProjectPath_) - 1);
    newProjectPath_[sizeof(newProjectPath_) - 1] = '\0';
    openBrowsePath_ = std::string(NE_PROJECT_ROOT);
}

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

void EditorUI::draw(EditorApp* app, Scene* scene, Camera* camera, Project* project, ResourceManager* resources, float dt) {
    app_ = app;
    ctxScene_ = scene;
    ctxCamera_ = camera;
    ctxProject_ = project;
    ctxResources_ = resources;

    // Keyboard shortcuts (skip while typing in a text field).
    ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && !io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z) && history_.canUndo()) { history_.undo(); selectedNode_ = nullptr; }
        if (ImGui::IsKeyPressed(ImGuiKey_Y) && history_.canRedo()) { history_.redo(); selectedNode_ = nullptr; }
        if (ImGui::IsKeyPressed(ImGuiKey_C)) copySelected(resources);
        if (ImGui::IsKeyPressed(ImGuiKey_V)) pasteClipboard(scene, resources);
        if (ImGui::IsKeyPressed(ImGuiKey_D)) duplicateSelected(resources);
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            saveScene(scene, resources, resolveScenePath(project));
        }
    }

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

    MenuBarPanel menuBarPanel;
    menuBarPanel.draw(this, project, scene, resources);

    if (showSceneTree_) {
        SceneHierarchyPanel hierarchyPanel;
        hierarchyPanel.draw(this, scene);
    }
    
    if (showInspector_) {
        InspectorPanel inspectorPanel;
        inspectorPanel.draw(this);
    }
    
    if (showFileBrowser_) {
        FileBrowserPanel fileBrowserPanel;
        fileBrowserPanel.draw(this, project, scene, resources);
    }

    ViewportPanel viewportPanel;
    viewportPanel.draw(this, camera, dt);
    
    drawGizmo(camera, scene);

    // Modal dialogs.
    drawAboutWindow();
    drawBuildWindow(project);
    drawSettingsWindow(project);
    drawNewProjectDialog(project);
    drawOpenProjectDialog(project);
    drawSaveSceneAsDialog(project, scene, resources);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene update: serialization, clipboard, undo/redo helpers
// ─────────────────────────────────────────────────────────────────────────────

void EditorUI::saveScene(Scene* scene, ResourceManager* resources, const std::string& path) {
    if (!scene || !resources) return;
    if (SceneSerializer::saveToFile(*scene, *resources, path))
        currentScenePath_ = path;
}

void EditorUI::loadScene(Scene* scene, ResourceManager* resources, const std::string& path) {
    if (!scene || !resources) return;
    if (SceneSerializer::loadIntoScene(*scene, *resources, path)) {
        currentScenePath_ = path;
        selectedNode_ = nullptr;
        history_.clear();
    }
}

void EditorUI::copySelected(ResourceManager* resources) {
    if (selectedNode_ && resources)
        clipboard_ = SceneSerializer::nodeToJson(*selectedNode_, *resources);
}

void EditorUI::pasteClipboard(Scene* scene, ResourceManager* resources) {
    if (clipboard_.empty() || !resources) return;
    Node* parent = selectedNode_ ? selectedNode_ : scene;
    if (!parent) return;
    if (auto node = SceneSerializer::nodeFromJson(clipboard_, *resources)) {
        Node* added = node.get();
        history_.execute(std::make_unique<AddNodeCommand>(parent, std::move(node)));
        selectedNode_ = added;
    }
}

void EditorUI::duplicateSelected(ResourceManager* resources) {
    if (!selectedNode_ || !resources || !selectedNode_->parent()) return;
    std::string json = SceneSerializer::nodeToJson(*selectedNode_, *resources);
    if (auto node = SceneSerializer::nodeFromJson(json, *resources)) {
        Node* added = node.get();
        history_.execute(std::make_unique<AddNodeCommand>(selectedNode_->parent(), std::move(node)));
        selectedNode_ = added;
    }
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

void EditorUI::drawSaveSceneAsDialog(Project* project, Scene* scene, ResourceManager* resources) {
    if (showSaveSceneAsDialog_) {
        ImGui::OpenPopup("Save Scene As");
        showSaveSceneAsDialog_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::Text("Enter scene file name (e.g. level1.scene):");
        ImGui::Spacing();
        
        ImGui::InputText("##SceneName", saveScenePathBuf_, sizeof(saveScenePathBuf_));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(120, 0))) {
            std::string fileName = saveScenePathBuf_;
            if (fileName.find(".scene") == std::string::npos && !fileName.empty()) {
                fileName += ".scene";
            }
            
            std::string savePath = fileName;
            if (project && project->isLoaded()) {
                savePath = project->scenesDir() + "/" + fileName;
            }
            
            if (!fileName.empty()) {
                saveScene(scene, resources, savePath);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
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
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.851f, 0.576f, 0.290f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.951f, 0.676f, 0.390f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.751f, 0.476f, 0.190f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        
        if (ImGui::Button("Sponsor NextEngine (Donate)", ImVec2(ImGui::GetContentRegionAvail().x, 35.0f))) {
#ifdef _WIN32
            std::system("start https://github.com/sponsors/saias-o");
#elif __APPLE__
            std::system("open https://github.com/sponsors/saias-o");
#else
            std::system("xdg-open https://github.com/sponsors/saias-o");
#endif
        }
        ImGui::PopStyleColor(4);

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
                ImGui::PushID(i);
                bool isSelected = (static_cast<int>(selectedBuildPlatform_) == i);
                
                // Platforms customized with badges
                char label[64];
                if (i == 0) std::snprintf(label, sizeof(label), " [Win] %s", platforms[i]);
                else if (i == 1) std::snprintf(label, sizeof(label), " [XR]  %s", platforms[i]);
                else if (i == 2) std::snprintf(label, sizeof(label), " [Tux] %s", platforms[i]);
                else std::snprintf(label, sizeof(label), " [Web] %s", platforms[i]);

                if (ImGui::Selectable(label, isSelected, ImGuiSelectableFlags_None, ImVec2(0, 32))) {
                    selectedBuildPlatform_ = static_cast<BuildPlatform>(i);
                }
                ImGui::PopID();
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
            if (selectedBuildPlatform_ == BuildPlatform::Windows) {
                // Windows Platform Settings
                ImGui::SeparatorText("Windows Build Settings");
                
                ImGui::Text("Target Architecture: x86_64 (Direct3D 12 & Vulkan)");
                ImGui::Spacing();

                ImGui::Text("Build Configuration:");
                if (ImGui::RadioButton("Debug##win", buildConfiguration_ == BuildConfig::Debug)) buildConfiguration_ = BuildConfig::Debug; 
                ImGui::SameLine();
                if (ImGui::RadioButton("Release##win", buildConfiguration_ == BuildConfig::Release)) buildConfiguration_ = BuildConfig::Release; 
                ImGui::SameLine();
                if (ImGui::RadioButton("Profile##win", buildConfiguration_ == BuildConfig::Profile)) buildConfiguration_ = BuildConfig::Profile;
                
                ImGui::Spacing();
                ImGui::Text("Output Binary Directory:");
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##WinOutputPath", buildOutputPath_, sizeof(buildOutputPath_));

                ImGui::Spacing();
                ImGui::Checkbox("Copy assets/ directory to output path", &buildCopyAssets_);
                ImGui::Checkbox("Enable Link-Time Optimization (LTO / -O3)", &buildEnableLto_);
            }
            else if (selectedBuildPlatform_ == BuildPlatform::MetaQuest) {
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
            else if (selectedBuildPlatform_ == BuildPlatform::Linux) {
                // Linux Build Settings
                ImGui::SeparatorText("Linux Build Settings");

                ImGui::Text("Target Architecture: x86_64 & AArch64 (GCC/Clang)");
                ImGui::Spacing();

                ImGui::Text("Build Configuration:");
                if (ImGui::RadioButton("Debug##lin", buildConfiguration_ == BuildConfig::Debug)) buildConfiguration_ = BuildConfig::Debug; 
                ImGui::SameLine();
                if (ImGui::RadioButton("Release##lin", buildConfiguration_ == BuildConfig::Release)) buildConfiguration_ = BuildConfig::Release; 
                ImGui::SameLine();
                if (ImGui::RadioButton("Profile##lin", buildConfiguration_ == BuildConfig::Profile)) buildConfiguration_ = BuildConfig::Profile;

                ImGui::Spacing();
                ImGui::Checkbox("Statically link GCC runtime libraries (libstdc++/libgcc)", &buildCopyAssets_);
                ImGui::Checkbox("Strip local debugging symbols to reduce binary size", &buildEnableLto_);
            }
            else if (selectedBuildPlatform_ == BuildPlatform::WebGL) {
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

namespace GizmoConfig {
    constexpr float RotationSensitivity = 50.0f;
    constexpr float RingThicknessRatio = 0.08f;
    constexpr int RingSegments = 64;
    constexpr float SelectionThresholdTranslate = 10.0f;
    constexpr float SelectionThresholdHead = 18.0f;
    constexpr float GizmoScreenScale = 0.15f;
    constexpr float MinWorldLength = 0.01f;
    
    constexpr ImU32 ColorX = IM_COL32(239, 68, 68, 255);
    constexpr ImU32 ColorY = IM_COL32(16, 185, 129, 255);
    constexpr ImU32 ColorZ = IM_COL32(59, 130, 246, 255);
    
    constexpr ImU32 HoverColorX = IM_COL32(252, 165, 165, 255);
    constexpr ImU32 HoverColorY = IM_COL32(110, 231, 183, 255);
    constexpr ImU32 HoverColorZ = IM_COL32(147, 197, 253, 255);
    
    constexpr float LineThicknessDefault = 2.5f;
    constexpr float LineThicknessHover = 4.0f;
    constexpr float RingThicknessDefault = 1.5f;
    constexpr float RingThicknessHover = 2.5f;
}

void EditorUI::drawGizmo(Camera* camera, Scene* scene) {
    if (!ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_T)) gizmoMode_ = GizmoMode::Translate;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoMode_ = GizmoMode::Rotate;
        if (ImGui::IsKeyPressed(ImGuiKey_S)) gizmoMode_ = GizmoMode::Scale;
    }

    if ((app_ && app_->isPlayMode()) || !camera || !scene || !selectedNode_) {
        grabbedAxis_ = GizmoAxis::None;
        return;
    }

    // Determine input clicks
    ImVec2 imMousePos = ImGui::GetMousePos();
    glm::vec2 mousePos = glm::vec2(imMousePos.x, imMousePos.y);
    bool isMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    bool isMouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left);

    ImGuiViewport* vp = ImGui::GetMainViewport();
    viewportPos_ = glm::vec2(vp->WorkPos.x, vp->WorkPos.y);
    viewportSize_ = glm::vec2(vp->WorkSize.x, vp->WorkSize.y);

    glm::mat4 viewProj = camera->projection() * camera->view();
    glm::mat4 invVP = glm::inverse(viewProj);

    float ndcX = ((mousePos.x - viewportPos_.x) / viewportSize_.x) * 2.0f - 1.0f;
    float ndcY = ((mousePos.y - viewportPos_.y) / viewportSize_.y) * 2.0f - 1.0f;
    glm::vec4 nearW = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    glm::vec4 farW = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    glm::vec3 rayOrigin = glm::vec3(nearW) / nearW.w;
    glm::vec3 rayDir = glm::normalize((glm::vec3(farW) / farW.w) - rayOrigin);

    gizmoNodePos_ = selectedNode_->transform().position;
    glm::quat nodeRot = selectedNode_->transform().rotation;
    
    gizmoLocalAxes_[0] = (gizmoMode_ == GizmoMode::Rotate) ? nodeRot * glm::vec3(1,0,0) : glm::vec3(1,0,0);
    gizmoLocalAxes_[1] = (gizmoMode_ == GizmoMode::Rotate) ? nodeRot * glm::vec3(0,1,0) : glm::vec3(0,1,0);
    gizmoLocalAxes_[2] = (gizmoMode_ == GizmoMode::Rotate) ? nodeRot * glm::vec3(0,0,1) : glm::vec3(0,0,1);

    glm::vec4 clipCenter = viewProj * glm::vec4(gizmoNodePos_, 1.0f);
    if (clipCenter.w <= 0.0f) return;
    
    glm::vec3 ndcCenter = glm::vec3(clipCenter) / clipCenter.w;
    gizmoCenter2D_ = glm::vec2(viewportPos_.x + (ndcCenter.x + 1.0f) * 0.5f * viewportSize_.x, viewportPos_.y + (ndcCenter.y + 1.0f) * 0.5f * viewportSize_.y);

    gizmoWorldLength_ = std::max(GizmoConfig::MinWorldLength, glm::length(camera->position - gizmoNodePos_) * GizmoConfig::GizmoScreenScale);

    for (int i = 0; i < 3; ++i) {
        glm::vec3 axisEnd3D = gizmoNodePos_ + gizmoLocalAxes_[i] * gizmoWorldLength_;
        glm::vec4 clipEnd = viewProj * glm::vec4(axisEnd3D, 1.0f);
        if (clipEnd.w > 0.0f) {
            glm::vec3 ndcEnd = glm::vec3(clipEnd) / clipEnd.w;
            gizmoEnds2D_[i] = glm::vec2(viewportPos_.x + (ndcEnd.x + 1.0f) * 0.5f * viewportSize_.x, viewportPos_.y + (ndcEnd.y + 1.0f) * 0.5f * viewportSize_.y);
            gizmoAxisValid_[i] = true;
        } else {
            gizmoAxisValid_[i] = false;
        }
    }

    int hoveredAxis = -1;
    updateGizmoHover(rayOrigin, rayDir, mousePos, hoveredAxis);

    if (isMouseClicked && hoveredAxis != -1 && grabbedAxis_ == GizmoAxis::None) {
        grabbedAxis_ = static_cast<GizmoAxis>(hoveredAxis);
        dragStartNodePos_ = gizmoNodePos_;
        dragStartNodeRotEuler_ = glm::degrees(glm::eulerAngles(selectedNode_->transform().rotation));
        dragStartNodeRotQuat_ = selectedNode_->transform().rotation;
        dragStartNodeScale_ = selectedNode_->transform().scale;
        dragStartMousePos_ = mousePos;
        
        if (gizmoMode_ == GizmoMode::Rotate) {
            float t;
            if (intersectRayPlane(rayOrigin, rayDir, gizmoNodePos_, gizmoLocalAxes_[hoveredAxis], t)) {
                dragStartHitPos3D_ = rayOrigin + rayDir * t;
            }
        }
    }

    if (grabbedAxis_ != GizmoAxis::None && isMouseDown) {
        handleGizmoDrag(rayOrigin, rayDir, mousePos);
    } else if (!isMouseDown) {
        grabbedAxis_ = GizmoAxis::None;
    }

    if (!ImGui::GetIO().WantCaptureMouse && isMouseClicked && hoveredAxis == -1 && grabbedAxis_ == GizmoAxis::None) {
        performRaycastSelection(scene, rayOrigin, rayDir);
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (gizmoMode_ == GizmoMode::Rotate) {
        renderGizmoRotationRings(drawList, camera, viewProj, hoveredAxis);
    } else {
        renderGizmoTranslateScale(drawList, hoveredAxis);
    }

    drawList->AddCircleFilled(ImVec2(gizmoCenter2D_.x, gizmoCenter2D_.y), 5.0f, ImColor(255, 255, 255, 220));
    drawList->AddCircle(ImVec2(gizmoCenter2D_.x, gizmoCenter2D_.y), 5.0f, ImColor(0, 0, 0, 255), 0, 1.0f);
}

void EditorUI::updateGizmoHover(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec2& mousePos, int& outHoveredAxis) {
    if (ImGui::GetIO().WantCaptureMouse && grabbedAxis_ == GizmoAxis::None) return;
    
    if (gizmoMode_ == GizmoMode::Rotate) {
        float closestDistToRing = 9999.0f;
        for (int i = 0; i < 3; ++i) {
            float t;
            if (intersectRayPlane(rayOrigin, rayDir, gizmoNodePos_, gizmoLocalAxes_[i], t)) {
                glm::vec3 hitPos = rayOrigin + rayDir * t;
                float distFromCenter = glm::length(hitPos - gizmoNodePos_);
                if (std::abs(distFromCenter - gizmoWorldLength_) < gizmoWorldLength_ * GizmoConfig::RingThicknessRatio) {
                    if (t < closestDistToRing) {
                        closestDistToRing = t;
                        outHoveredAxis = i;
                    }
                }
            }
        }
    } else {
        float closestDist = 9999.0f;
        for (int i = 0; i < 3; ++i) {
            if (!gizmoAxisValid_[i]) continue;
            float dSeg = distanceToSegment(mousePos, gizmoCenter2D_, gizmoEnds2D_[i]);
            float dHead = glm::length(mousePos - gizmoEnds2D_[i]);
            if (dSeg < GizmoConfig::SelectionThresholdTranslate || dHead < GizmoConfig::SelectionThresholdHead) {
                if (dSeg < closestDist) {
                    closestDist = dSeg;
                    outHoveredAxis = i;
                }
            }
        }
    }
}

void EditorUI::handleGizmoDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec2& mousePos) {
    int axis = static_cast<int>(grabbedAxis_);
    if (gizmoMode_ == GizmoMode::Rotate) {
        float t;
        if (intersectRayPlane(rayOrigin, rayDir, dragStartNodePos_, gizmoLocalAxes_[axis], t)) {
            glm::vec3 currentHitPos = rayOrigin + rayDir * t;
            glm::vec3 vStart = glm::normalize(dragStartHitPos3D_ - dragStartNodePos_);
            glm::vec3 vCurr = glm::normalize(currentHitPos - dragStartNodePos_);
            
            float cosTheta = glm::clamp(glm::dot(vStart, vCurr), -1.0f, 1.0f);
            float angle = glm::acos(cosTheta);
            
            if (glm::dot(glm::cross(vStart, vCurr), gizmoLocalAxes_[axis]) < 0.0f) {
                angle = -angle;
            }
            selectedNode_->transform().rotation = glm::angleAxis(angle, gizmoLocalAxes_[axis]) * dragStartNodeRotQuat_;
        }
    } else if (gizmoAxisValid_[axis]) {
        glm::vec2 dir2D = gizmoEnds2D_[axis] - gizmoCenter2D_;
        float len2D = glm::length(dir2D);
        if (len2D > 1.0f) {
            glm::vec2 u = dir2D / len2D;
            float screenProj = glm::dot(mousePos - dragStartMousePos_, u);
            float worldDelta = screenProj * (gizmoWorldLength_ / len2D);

            if (gizmoMode_ == GizmoMode::Translate) {
                glm::vec3 newPos = dragStartNodePos_;
                if (axis == 0) newPos.x += worldDelta;
                else if (axis == 1) newPos.y += worldDelta;
                else if (axis == 2) newPos.z += worldDelta;
                selectedNode_->transform().position = newPos;
            } else if (gizmoMode_ == GizmoMode::Scale) {
                glm::vec3 newScale = dragStartNodeScale_;
                float scaleDelta = screenProj * (gizmoWorldLength_ / len2D) * 0.5f;
                if (axis == 0) newScale.x += scaleDelta;
                else if (axis == 1) newScale.y += scaleDelta;
                else if (axis == 2) newScale.z += scaleDelta;
                selectedNode_->transform().scale = newScale;
            }
        }
    }
}

void EditorUI::performRaycastSelection(Scene* scene, const glm::vec3& rayOrigin, const glm::vec3& rayDir) {
    Node* closestNode = nullptr;
    float closestT = 99999.0f;
    scene->traverse([&](Node& n, const glm::mat4& worldMatrix) {
        if (!n.parent()) return;
        glm::vec3 objWorldPos = glm::vec3(worldMatrix[3]);
        float sx = glm::length(glm::vec3(worldMatrix[0]));
        float sy = glm::length(glm::vec3(worldMatrix[1]));
        float sz = glm::length(glm::vec3(worldMatrix[2]));
        float maxScale = glm::max(sx, glm::max(sy, sz));

        float radius = 0.5f * maxScale;
        if (n.mesh()) radius = 1.0f * maxScale;
        else if (n.asLightConst()) radius = 0.6f * maxScale;

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
    selectedNode_ = closestNode;
}

void EditorUI::renderGizmoRotationRings(ImDrawList* drawList, Camera* camera, const glm::mat4& viewProj, int hoveredAxis) {
    ImU32 colors[3] = { GizmoConfig::ColorX, GizmoConfig::ColorY, GizmoConfig::ColorZ };
    ImU32 hoverColors[3] = { GizmoConfig::HoverColorX, GizmoConfig::HoverColorY, GizmoConfig::HoverColorZ };
    
    glm::vec3 renderAxes[3] = { gizmoLocalAxes_[0], gizmoLocalAxes_[1], gizmoLocalAxes_[2] };
    if (grabbedAxis_ != GizmoAxis::None) {
        renderAxes[0] = dragStartNodeRotQuat_ * glm::vec3(1,0,0);
        renderAxes[1] = dragStartNodeRotQuat_ * glm::vec3(0,1,0);
        renderAxes[2] = dragStartNodeRotQuat_ * glm::vec3(0,0,1);
    }
    
    for (int i = 0; i < 3; ++i) {
        bool isHovered = (hoveredAxis == i || static_cast<int>(grabbedAxis_) == i);
        ImU32 col = isHovered ? hoverColors[i] : colors[i];
        ImVec4 colV = ImGui::ColorConvertU32ToFloat4(col);
        
        glm::vec3 normal = renderAxes[i];
        glm::vec3 u = glm::normalize(glm::cross(normal, std::abs(normal.x) > 0.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0)));
        glm::vec3 v = glm::cross(normal, u);
        
        std::vector<ImVec2> pointsFront;
        std::vector<ImVec2> pointsBack;
        
        for (int s = 0; s <= GizmoConfig::RingSegments; ++s) {
            float theta = (static_cast<float>(s) / GizmoConfig::RingSegments) * glm::two_pi<float>();
            glm::vec3 pos3D = gizmoNodePos_ + (u * std::cos(theta) + v * std::sin(theta)) * gizmoWorldLength_;
            
            glm::vec4 clipPos = viewProj * glm::vec4(pos3D, 1.0f);
            if (clipPos.w > 0.0f) {
                glm::vec3 ndcPos = glm::vec3(clipPos) / clipPos.w;
                ImVec2 p2d(viewportPos_.x + (ndcPos.x + 1.0f) * 0.5f * viewportSize_.x, viewportPos_.y + (ndcPos.y + 1.0f) * 0.5f * viewportSize_.y);
                
                if (glm::dot(glm::normalize(pos3D - camera->position), glm::normalize(pos3D - gizmoNodePos_)) > 0.0f) {
                    pointsBack.push_back(p2d);
                } else {
                    pointsFront.push_back(p2d);
                }
            }
        }
        
        float thickness = isHovered ? GizmoConfig::RingThicknessHover : GizmoConfig::RingThicknessDefault;
        if (!pointsBack.empty()) {
            ImU32 backCol = ImGui::ColorConvertFloat4ToU32(ImVec4(colV.x, colV.y, colV.z, colV.w * 0.2f));
            for (size_t p = 1; p < pointsBack.size(); ++p) drawList->AddLine(pointsBack[p-1], pointsBack[p], backCol, thickness);
        }
        if (!pointsFront.empty()) {
            for (size_t p = 1; p < pointsFront.size(); ++p) drawList->AddLine(pointsFront[p-1], pointsFront[p], col, thickness);
        }
    }
}

void EditorUI::renderGizmoTranslateScale(ImDrawList* drawList, int hoveredAxis) {
    ImU32 colors[3] = { GizmoConfig::ColorX, GizmoConfig::ColorY, GizmoConfig::ColorZ };
    ImU32 hoverColors[3] = { GizmoConfig::HoverColorX, GizmoConfig::HoverColorY, GizmoConfig::HoverColorZ };

    for (int i = 0; i < 3; ++i) {
        if (!gizmoAxisValid_[i]) continue;

        bool isHovered = (hoveredAxis == i || static_cast<int>(grabbedAxis_) == i);
        ImU32 col = isHovered ? hoverColors[i] : colors[i];
        float thickness = isHovered ? GizmoConfig::LineThicknessHover : GizmoConfig::LineThicknessDefault;

        drawList->AddLine(ImVec2(gizmoCenter2D_.x, gizmoCenter2D_.y), ImVec2(gizmoEnds2D_[i].x, gizmoEnds2D_[i].y), col, thickness);

        if (gizmoMode_ == GizmoMode::Translate) {
            glm::vec2 dir = gizmoEnds2D_[i] - gizmoCenter2D_;
            float dLen = glm::length(dir);
            if (dLen > 1.0f) {
                dir = dir / dLen;
                glm::vec2 perp = glm::vec2(-dir.y, dir.x);
                float headLen = isHovered ? 14.0f : 11.0f;
                float headWidth = isHovered ? 7.0f : 5.5f;
                glm::vec2 p0 = gizmoEnds2D_[i] + dir * headLen;
                glm::vec2 p1 = gizmoEnds2D_[i] - dir * headLen + perp * headWidth;
                glm::vec2 p2 = gizmoEnds2D_[i] - dir * headLen - perp * headWidth;
                drawList->AddTriangleFilled(ImVec2(p0.x, p0.y), ImVec2(p1.x, p1.y), ImVec2(p2.x, p2.y), col);
            }
        } else if (gizmoMode_ == GizmoMode::Scale) {
            float halfSize = isHovered ? 6.0f : 4.5f;
            drawList->AddRectFilled(
                ImVec2(gizmoEnds2D_[i].x - halfSize, gizmoEnds2D_[i].y - halfSize),
                ImVec2(gizmoEnds2D_[i].x + halfSize, gizmoEnds2D_[i].y + halfSize), col
            );
        }
    }
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
                
                ImGui::SeparatorText("Performance");
                int maxFps = project->maxFps();
                if (ImGui::InputInt("Max FPS (0 = Unlimited)", &maxFps)) {
                    if (maxFps < 0) maxFps = 0;
                    project->setMaxFps(maxFps);
                }
                
                ImGui::SeparatorText("Metadata");
                ImGui::TextDisabled("Version: 1.0.0");
                ImGui::TextDisabled("Company Name: DefaultCompany");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rendering")) {
                ImGui::Spacing();
                ImGui::SeparatorText("Shadows");
                
                int currentRes = project->shadowResolution();
                int comboIndex = 0;
                if (currentRes == 512) comboIndex = 0;
                else if (currentRes == 1024) comboIndex = 1;
                else if (currentRes == 2048) comboIndex = 2;
                else if (currentRes == 4096) comboIndex = 3;
                else if (currentRes == 8192) comboIndex = 4;
                
                const char* shadowResNames[] = { "Very Low (512)", "Low (1024)", "Medium (2048)", "High (4096)", "Ultra (8192)" };
                if (ImGui::Combo("Resolution", &comboIndex, shadowResNames, 5)) {
                    int newRes = 2048;
                    if (comboIndex == 0) newRes = 512;
                    else if (comboIndex == 1) newRes = 1024;
                    else if (comboIndex == 2) newRes = 2048;
                    else if (comboIndex == 3) newRes = 4096;
                    else if (comboIndex == 4) newRes = 8192;
                    project->setShadowResolution(newRes);
                }
                
                float dist = project->shadowDistance();
                if (ImGui::DragFloat("Distance", &dist, 0.5f, 10.0f, 200.0f)) {
                    project->setShadowDistance(dist);
                }
                
                float soft = project->shadowSoftness();
                if (ImGui::DragFloat("Softness (Blur)", &soft, 0.05f, 0.0f, 10.0f)) {
                    project->setShadowSoftness(soft);
                }

                ImGui::SeparatorText("Global Quality");

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

            if (ImGui::BeginTabItem("Audio")) {
                ImGui::Spacing();
                ImGui::SeparatorText("Global");
                float masterVol = project->masterVolume();
                if (ImGui::SliderFloat("Master Volume", &masterVol, 0.0f, 1.0f)) {
                    project->setMasterVolume(masterVol);
                    AudioManager::get().setMasterVolume(masterVol);
                }

                ImGui::Spacing();
                ImGui::SeparatorText("Default Settings");
                AudioSettings defSettings = project->defaultAudioSettings();
                bool changed = false;
                changed |= ImGui::SliderFloat("Default Volume", &defSettings.volume, 0.0f, 1.0f);
                changed |= ImGui::Checkbox("Loop by Default", &defSettings.loop);
                changed |= ImGui::Checkbox("Spatialized by Default", &defSettings.spatialized);
                if (defSettings.spatialized) {
                    changed |= ImGui::DragFloat("Min Distance", &defSettings.minDistance, 0.5f, 0.1f, 1000.0f);
                    changed |= ImGui::DragFloat("Max Distance", &defSettings.maxDistance, 1.0f, 1.0f, 10000.0f);
                }
                
                if (changed) {
                    project->defaultAudioSettings() = defSettings;
                    AudioManager::get().setDefaultSettings(defSettings);
                }
                
                ImGui::Spacing();
                ImGui::SeparatorText("Audio Assets Aliases");
                ImGui::TextDisabled("Map friendly names to audio files.");
                
                // List existing
                std::string toRemove = "";
                if (ImGui::BeginTable("AudioAliasesTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("Alias Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("File Path", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& [name, path] : project->audioAliases()) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", path.c_str());
                        ImGui::TableNextColumn();
                        ImGui::PushID(name.c_str());
                        if (ImGui::Button("X", ImVec2(24, 0))) {
                            toRemove = name;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }

                if (!toRemove.empty()) {
                    project->removeAudioAlias(toRemove);
                }
                
                ImGui::Spacing();
                ImGui::SeparatorText("Register New Audio Alias");
                ImGui::TextDisabled("Drag & Drop .ogg files below, or type manually.");
                
                static char newAliasName[64] = "";
                static char newAliasPath[256] = "";
                
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##AliasName", "Alias Name (e.g. 'Explosion')", newAliasName, sizeof(newAliasName));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##AliasPath", "File Path (e.g. 'assets/audio/exp.ogg')", newAliasPath, sizeof(newAliasPath));
                
                // Drag & Drop target on the inputs
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_AUDIO")) {
                        const char* pathStr = (const char*)payload->Data;
                        std::filesystem::path fp(pathStr);
                        if (fp.extension() == ".ogg") {
                            std::filesystem::path root(project->rootPath());
                            std::string relPath = std::filesystem::relative(fp, root).string();
                            std::replace(relPath.begin(), relPath.end(), '\\', '/');
                            std::strncpy(newAliasPath, relPath.c_str(), sizeof(newAliasPath) - 1);
                            
                            if (strlen(newAliasName) == 0) {
                                std::string stem = fp.stem().string();
                                std::strncpy(newAliasName, stem.c_str(), sizeof(newAliasName) - 1);
                            }
                            project->assetRegistry().registerAsset(relPath, AssetType::Audio);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                
                if (ImGui::Button("Add / Update Alias", ImVec2(-1, 0))) {
                    if (strlen(newAliasName) > 0 && strlen(newAliasPath) > 0) {
                        project->setAudioAlias(newAliasName, newAliasPath);
                        newAliasName[0] = '\0';
                        newAliasPath[0] = '\0';
                    }
                }

                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

std::string EditorUI::resolveScenePath(Project* project) const {
    if (!currentScenePath_.empty()) return currentScenePath_;
    if (project && project->isLoaded()) return project->scenesDir() + "/main.scene";
    return "scene.scene";
}

} // namespace ne
