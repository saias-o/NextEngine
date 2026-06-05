#pragma once

#include "editor/CommandHistory.hpp"
#include "editor/EditorEnums.hpp"
#include "editor/ImGuiTextureCache.hpp"

#include <string>
#include <glm/glm.hpp>

namespace ne {

class Scene;
class Camera;
class Node;
class Project;
class ResourceManager;

class MenuBarPanel;
class SceneHierarchyPanel;
class InspectorPanel;
class FileBrowserPanel;
class ViewportPanel;

// The full editor interface (Godot/Unity-style). Draws a dockable layout:
// main menu bar, scene tree (left), inspector (right), file browser (bottom),
// and a viewport area with Scene / Play Mode tabs.
//
// The viewport itself is the Vulkan render, drawn as the window background
// behind ImGui (passthrough central dock node). This class only draws the UI
// overlays — it does NOT own or manage the render pipeline.
class EditorUI {
    friend class MenuBarPanel;
    friend class SceneHierarchyPanel;
    friend class InspectorPanel;
    friend class FileBrowserPanel;
    friend class ViewportPanel;
public:
    EditorUI();

    // Draw the full editor UI. Call between ImGui::NewFrame() and
    // ImGui::Render(), before endFrame().
    void draw(Scene* scene, Camera* camera, Project* project, ResourceManager* resources, float dt);

    // The currently selected node (nullptr = none).
    Node* selectedNode() const { return selectedNode_; }

    // Is the editor in "play" mode? (vs "scene" editing mode)
    bool isPlayMode() const { return playMode_; }

    // Set the play mode
    void setPlayMode(bool play);

    // Request to quit the application
    bool quitRequested() const { return quitRequested_; }

    bool isViewportHovered(float mx, float my) const {
        return mx >= viewportPos_.x && mx <= viewportPos_.x + viewportSize_.x &&
               my >= viewportPos_.y && my <= viewportPos_.y + viewportSize_.y;
    }

private:
    // Scene update: serialization, clipboard and undo/redo.
    void saveScene(Scene* scene, ResourceManager* resources, const std::string& path);
    void loadScene(Scene* scene, ResourceManager* resources, const std::string& path);
    void copySelected(ResourceManager* resources);
    void pasteClipboard(Scene* scene, ResourceManager* resources);
    void duplicateSelected(ResourceManager* resources);
    void drawGizmo(Camera* camera, Scene* scene);
    void drawAboutWindow();
    void drawBuildWindow(Project* project);
    void drawSettingsWindow(Project* project);
    void drawNewProjectDialog(Project* project);
    void drawOpenProjectDialog(Project* project);
    void drawSaveSceneAsDialog(Project* project, Scene* scene, ResourceManager* resources);
    void applyEditorStyle();

    // Panel visibility toggles (View menu)
    bool showSceneTree_   = true;
    bool showInspector_   = true;
    bool showFileBrowser_ = true;
    bool showViewportOverlay_ = true;

    // Selection
    Node* selectedNode_ = nullptr;

    // Scene / Play mode
    bool playMode_ = false;

    // Request quit
    bool quitRequested_ = false;

    // File browser state
    std::string currentBrowsePath_;
    float fileBrowserZoom_ = 1.0f;

    // Dock layout
    bool dockLayoutBuilt_ = false;

    // Project dialogs
    bool showNewProjectDialog_  = false;
    bool showOpenProjectDialog_ = false;
    bool showSaveSceneAsDialog_ = false;
    bool showAboutWindow_       = false;
    bool showBuildWindow_       = false;
    bool showSettingsWindow_    = false;
    char newProjectName_[128]   = "MyGame";
    char newProjectPath_[512]   = "";
    char saveScenePathBuf_[512] = "main.scene";
    void rebuildSceneHierarchy(Scene* scene);

    std::string resolveScenePath(Project* project) const;

    std::string openBrowsePath_;  // for the open-project file browser

    // Build settings state
    BuildPlatform selectedBuildPlatform_ = BuildPlatform::Windows;
    BuildConfig buildConfiguration_ = BuildConfig::Release;
    char buildOutputPath_[512] = "build/bin";
    bool buildCopyAssets_ = true;
    bool buildEnableLto_ = false;
    bool buildSceneMainChecked_ = true;
    bool buildSceneDemoChecked_ = false;

    // Deferred operations for C++ memory safety & avoiding iterator invalidation
    Node* nodeToDelete_ = nullptr;
    Node* nodeToCreateChildUnder_ = nullptr;
    CreateNodeType createType_ = CreateNodeType::None;
    std::string draggedScenePath_;

    Node* nodeToReparent_ = nullptr;
    Node* newParent_ = nullptr;

    // Renaming state
    Node* nodeToRename_ = nullptr;
    char nodeRenameBuf_[128] = "";

    std::string fileToRename_;
    char fileRenameBuf_[256] = "";

    // Search state
    char sceneTreeSearchBuf_[128] = "";
    char fileBrowserSearchBuf_[128] = "";

    // Gizmo state for dragging & viewport deselect
    GizmoMode gizmoMode_ = GizmoMode::Translate;
    GizmoAxis grabbedAxis_ = GizmoAxis::None;
    glm::vec3 dragStartNodePos_{0.0f};
    glm::vec3 dragStartNodeRotEuler_{0.0f};
    glm::vec3 dragStartNodeScale_{1.0f};
    glm::vec2 dragStartMousePos_{0.0f};

    glm::vec2 viewportPos_{0.0f, 0.0f};
    glm::vec2 viewportSize_{0.0f, 0.0f};

    // Scene update state.
    CommandHistory history_;
    std::string clipboard_;          // JSON of a copied node subtree
    std::string currentScenePath_;   // last saved/opened .scene path
    ResourceManager* ctxResources_ = nullptr;  // set each frame in draw()
    ImGuiTextureCache texCache_;     // cache for ImGui texture IDs
};

} // namespace ne
