#pragma once

#include "editor/CommandHistory.hpp"
#include "editor/EditorEnums.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/ThumbnailCache.hpp"

#include <any>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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
class ModelImporterPanel;

// The full editor interface (Godot/Unity-style). Draws a dockable layout:
// main menu bar, scene tree (left), inspector (right), file browser (bottom),
// and a viewport area with Scene / Play Mode tabs.
//
class EditorApp;

// The main user interface of the editor, composed of several ImGui panels.
class EditorUI {
    friend class MenuBarPanel;
    friend class SceneHierarchyPanel;
    friend class InspectorPanel;
    friend class FileBrowserPanel;
    friend class ViewportPanel;
    friend class ModelImporterPanel;
    friend class PropertyEditor;
public:
    EditorUI();
    ~EditorUI();  // defined in .cpp where Scene is complete (previewScene_ unique_ptr)

    // Draw the full editor UI. Call between ImGui::NewFrame() and
    // ImGui::Render(), before endFrame().
    void draw(EditorApp* app, Scene* scene, Camera* camera, Project* project, ResourceManager* resources, float dt);

    // The currently selected node (nullptr = none).
    Node* selectedNode() const { return selectedNode_; }
    Project* ctxProject() const { return ctxProject_; }

    void clearSelection() { selectedNode_ = nullptr; document_.clearSelection(); }

    // Request to quit the application
    bool quitRequested() const { return quitRequested_; }

    bool isViewportHovered(float mx, float my) const {
        return mx >= viewportPos_.x && mx <= viewportPos_.x + viewportSize_.x &&
               my >= viewportPos_.y && my <= viewportPos_.y + viewportSize_.y;
    }

    void openModelImporter(const std::string& path, ResourceManager* resources);
    void closeModelImporter();

    bool isPreviewMode() const { return isPreviewMode_; }
    Scene* previewScene() const { return previewScene_.get(); }

    // Play is inspectable but read-only: no editor mutation may touch the live
    // World sub-scene. Every command and inspector edit is gated on this.
    bool canEdit() const;

    // Mark the document dirty for mutations that do not yet go through a command
    // (resource/asset edits handled by later lots). No-op in Play.
    void markDirty();

private:
    // Single chokepoint for document mutations. Drops the command (no-op) when
    // editing is disabled (Play mode), otherwise records it on the history so
    // the change is undoable and marks the document dirty.
    void execute(std::unique_ptr<Command> command);

    // Scene update: serialization, clipboard and undo/redo.
    void saveScene(Scene* scene, ResourceManager* resources, const std::string& path);
    void loadScene(Scene* scene, ResourceManager* resources, const std::string& path);
    void copySelected(ResourceManager* resources);
    void pasteClipboard(Scene* scene, ResourceManager* resources);
    void duplicateSelected(ResourceManager* resources);
    
    // Gizmo internal methods
    void drawGizmo(Camera* camera, Scene* scene);
    void updateGizmoHover(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec2& mousePos, int& outHoveredAxis);
    void handleGizmoDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec2& mousePos);
    void performRaycastSelection(Scene* scene, const glm::vec3& rayOrigin, const glm::vec3& rayDir);
    void renderGizmoRotationRings(ImDrawList* drawList, Camera* camera, const glm::mat4& viewProj, int hoveredAxis);
    void renderGizmoTranslateScale(ImDrawList* drawList, int hoveredAxis);

    // Draws collision-shape wireframes (box/sphere/capsule) over the viewport.
    void drawColliderGizmos(Camera* camera, Scene* scene);
    bool showColliders_ = true;
    
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
    bool showModelImporter_ = false;

    // Selection
    Node* selectedNode_ = nullptr;

    // Editor layout and state
    bool quitRequested_ = false;
    bool dockLayoutBuilt_ = false;

    // File browser state
    std::string currentBrowsePath_;
    float fileBrowserZoom_ = 1.0f;

    // Cached directory listing so the browser does not hit the disk every frame.
    // Rescanned when the path/query changes, after a local file mutation, or
    // every kRefreshSeconds to pick up external changes. Paths are absolute;
    // directories and files are pre-sorted by name.
    struct FileListing {
        std::string path;
        std::string query;
        double time = -1.0;        // GetTime() of last scan; < 0 forces a rescan
        std::vector<std::string> dirs;
        std::vector<std::string> files;
    };
    FileListing fileListing_;

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
    char buildOutputPath_[512] = "build/export";
    bool buildCopyAssets_ = true;
    bool buildEnableLto_ = false;
    bool buildSceneMainChecked_ = true;
    bool buildSceneDemoChecked_ = false;

    // Main scene selection (populated from the project's scenes/ when the dialog
    // opens) and the result of the last export, shown in the dialog.
    std::vector<std::string> buildScenes_;       // project-relative, e.g. "scenes/main.scene"
    int buildMainSceneIndex_ = 0;
    bool buildHasResult_ = false;
    bool buildLastSuccess_ = false;
    std::string buildLastError_;
    std::string buildLastLog_;
    std::string buildLastOutputDir_;
    std::string buildLastExe_;
    void refreshBuildScenes_(Project* project);

    // Deferred operations for C++ memory safety & avoiding iterator invalidation
    Node* nodeToDelete_ = nullptr;
    Node* nodeToCreateChildUnder_ = nullptr;
    Node* nodeToCreateParentFor_ = nullptr;
    CreateNodeType createType_ = CreateNodeType::None;
    CreateNodeType createParentType_ = CreateNodeType::None;
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
    glm::quat dragStartNodeRotQuat_{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 dragStartNodeScale_{1.0f};
    glm::vec2 dragStartMousePos_{0.0f};
    glm::vec3 dragStartHitPos3D_{0.0f};

    // Transient Gizmo drawing state (populated each frame)
    glm::vec3 gizmoNodePos_{0.0f};
    float gizmoWorldLength_{0.0f};
    glm::vec2 gizmoCenter2D_{0.0f};
    glm::vec2 gizmoEnds2D_[3];
    glm::vec3 gizmoLocalAxes_[3];
    bool gizmoAxisValid_[3]{false, false, false};

    glm::vec2 viewportPos_{0.0f, 0.0f};
    glm::vec2 viewportSize_{0.0f, 0.0f};

    // Scene update state.
    SceneDocument document_;
    CommandHistory history_;

    // Transient state for transactional inspector edits (see PropertyEditor):
    // the ImGui id and the pre-edit value of the property currently being
    // dragged. A single widget is active at a time, so one slot suffices.
    unsigned int propEditId_ = 0;       // ImGuiID of the active property widget
    std::any propEditOld_;              // value captured when the edit began

    // Last seen Project::version(); a change means the project was created or
    // (re)loaded, so the per-project editor state (history, clipboard, …) must be
    // dropped — its commands reference nodes from the previous scene.
    uint64_t lastProjectVersion_ = 0;
    std::string clipboard_;          // JSON of a copied node subtree
    std::string currentScenePath_;   // last saved/opened .scene path
    EditorApp* app_ = nullptr;
    Scene* ctxScene_ = nullptr;
    Camera* ctxCamera_ = nullptr;
    ResourceManager* ctxResources_ = nullptr;  // set each frame in draw()
    Project* ctxProject_ = nullptr;            // set each frame in draw()
    ThumbnailCache thumbnails_;      // bounded, downscaled asset-browser thumbnails

    // Importer State
    bool isPreviewMode_ = false;
    std::string previewModelPath_;
    std::unique_ptr<Scene> previewScene_;
};

} // namespace ne
