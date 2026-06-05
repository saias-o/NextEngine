#include "editor/panels/MenuBarPanel.hpp"
#include "editor/EditorUI.hpp"
#include "editor/EditorApp.hpp"
#include "scene/Scene.hpp"
#include "project/Project.hpp"
#include "graphics/ResourceManager.hpp"
#include "scene/SceneSerializer.hpp"

#include <imgui.h>
#include <filesystem>
#include <cstring>

namespace ne {

void MenuBarPanel::draw(EditorUI* editor, Project* project, Scene* scene, ResourceManager* resources) {
    if (ImGui::BeginMainMenuBar()) {
        // ── 1. File Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...")) {
                editor->showNewProjectDialog_ = true;
            }
            if (ImGui::MenuItem("Open Project...")) {
                editor->showOpenProjectDialog_ = true;
                editor->openBrowsePath_ = std::string(NE_PROJECT_ROOT);
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
            if (ImGui::MenuItem("New Scene")) {
                if (scene) {
                    scene->clearChildren();
                    editor->selectedNode_ = nullptr;
                    editor->currentScenePath_.clear();
                    editor->history_.clear();
                }
            }
            if (ImGui::MenuItem("Reload Scene", "F5")) {
                editor->loadScene(scene, resources, editor->resolveScenePath(project));
            }
            if (ImGui::MenuItem("Open Scene...")) {
                editor->loadScene(scene, resources, editor->resolveScenePath(project));
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                editor->saveScene(scene, resources, editor->resolveScenePath(project));
            }
            if (ImGui::MenuItem("Save Scene As...")) {
                editor->showSaveSceneAsDialog_ = true;
                std::string defaultName = "main.scene";
                if (!editor->currentScenePath_.empty()) {
                    std::filesystem::path p(editor->currentScenePath_);
                    defaultName = p.filename().string();
                }
                strncpy(editor->saveScenePathBuf_, defaultName.c_str(), sizeof(editor->saveScenePathBuf_));
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Esc"))      { editor->quitRequested_ = true; }
            ImGui::EndMenu();
        }

        // ── 2. Edit Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, editor->history_.canUndo())) {
                editor->history_.undo(); editor->selectedNode_ = nullptr;
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, editor->history_.canRedo())) {
                editor->history_.redo(); editor->selectedNode_ = nullptr;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C", false, editor->selectedNode_ != nullptr)) {
                editor->copySelected(resources);
            }
            if (ImGui::MenuItem("Paste", "Ctrl+V", false, !editor->clipboard_.empty())) {
                editor->pasteClipboard(scene, resources);
            }
            if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, editor->selectedNode_ != nullptr)) {
                editor->duplicateSelected(resources);
            }
            ImGui::EndMenu();
        }

        // ── 3. View Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Scene Tree",       nullptr, &editor->showSceneTree_);
            ImGui::MenuItem("Inspector",        nullptr, &editor->showInspector_);
            ImGui::MenuItem("File Browser",     nullptr, &editor->showFileBrowser_);
            ImGui::Separator();
            ImGui::MenuItem("Viewport Overlay", nullptr, &editor->showViewportOverlay_);
            ImGui::EndMenu();
        }

        // ── 4. Scene Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("Scene")) {
            Node* parentNode = editor->selectedNode_ ? editor->selectedNode_ : scene;
            bool hasParent = parentNode != nullptr;
            if (ImGui::MenuItem("Add Node", nullptr, false, hasParent)) {
                editor->nodeToCreateChildUnder_ = parentNode;
                editor->createType_ = CreateNodeType::Node;
            }
            if (ImGui::BeginMenu("Create 3D Object", hasParent)) {
                if (ImGui::MenuItem("Cube")) {
                    editor->nodeToCreateChildUnder_ = parentNode;
                    editor->createType_ = CreateNodeType::MeshNode;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Create Light", hasParent)) {
                if (ImGui::MenuItem("Directional Light")) {
                    editor->nodeToCreateChildUnder_ = parentNode;
                    editor->createType_ = CreateNodeType::DirectionalLight;
                }
                if (ImGui::MenuItem("Point Light")) {
                    editor->nodeToCreateChildUnder_ = parentNode;
                    editor->createType_ = CreateNodeType::PointLight;
                }
                if (ImGui::MenuItem("Spot Light")) {
                    editor->nodeToCreateChildUnder_ = parentNode;
                    editor->createType_ = CreateNodeType::SpotLight;
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            bool canDelete = editor->selectedNode_ != nullptr && editor->selectedNode_->parent() != nullptr;
            if (ImGui::MenuItem("Delete Selected", "Del", false, canDelete)) {
                editor->nodeToDelete_ = editor->selectedNode_;
            }
            ImGui::EndMenu();
        }

        // ── 5. Settings Menu ───────────────────────────────────────────
        if (ImGui::BeginMenu("Settings")) {
            bool hasProject = project && project->isLoaded();
            if (ImGui::MenuItem("Settings", nullptr, false, hasProject)) {
                editor->showSettingsWindow_ = true;
            }
            ImGui::EndMenu();
        }

        // ── 6. Build Menu ─────────────────────────────────────────────
        if (ImGui::BeginMenu("Build")) {
            if (ImGui::MenuItem("Build Project...")) {
                if (project && project->isLoaded()) {
                    editor->saveScene(scene, resources, editor->resolveScenePath(project));
                    editor->showBuildWindow_ = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build Settings...", "Ctrl+Shift+B")) {
                editor->showBuildWindow_ = true;
            }
            ImGui::EndMenu();
        }

        // ── 7. Help Menu ──────────────────────────────────────────────
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About NextEngine")) {
                editor->showAboutWindow_ = true;
            }
            ImGui::EndMenu();
        }

        // ── Right-aligned Scene / Play mode toggle ──────────────────────
        {
            float contentWidth = ImGui::GetContentRegionAvail().x;
            float buttonWidth  = 160.0f;
            ImGui::SameLine(ImGui::GetCursorPosX() + contentWidth - buttonWidth);

            bool sceneActive = !editor->app_->isPlayMode();
            if (sceneActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.29f, 0.56f, 0.85f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.62f, 0.90f, 1.0f));
            }
            if (ImGui::SmallButton("Scene##mode")) { 
                editor->app_->setPlayMode(false);
            }
            if (sceneActive) ImGui::PopStyleColor(2);

            ImGui::SameLine();

            bool playActive = editor->app_->isPlayMode();
            if (playActive) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.30f, 0.70f, 0.35f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.78f, 0.40f, 1.0f));
            }
            if (ImGui::SmallButton("Play##mode")) { 
                editor->app_->setPlayMode(true);
            }
            if (playActive) ImGui::PopStyleColor(2);
        }

        ImGui::EndMainMenuBar();
    }
}

} // namespace ne
