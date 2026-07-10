#pragma once

#include "editor/CommandHistory.hpp"
#include "editor/EditorEnums.hpp"
#include "editor/SceneDocument.hpp"
#include "editor/ThumbnailCache.hpp"

#include <any>
#include <cstddef>
#include <future>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace saida {

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
class ProfilerPanel;
class AnimationPanel;
class McpBridge;

class EditorApp;

class EditorUI {
    friend class MenuBarPanel;
    friend class SceneHierarchyPanel;
    friend class InspectorPanel;
    friend class FileBrowserPanel;
    friend class ViewportPanel;
    friend class ModelImporterPanel;
    friend class ProfilerPanel;
    friend class AnimationPanel;
    friend class PropertyEditor;
    friend class McpBridge;
public:
    EditorUI();
    ~EditorUI();

    void draw(EditorApp* app, Scene* scene, Camera* camera, Project* project, ResourceManager* resources, float dt);

    Node* selectedNode() const { return selectedNode_; }
    Project* ctxProject() const { return ctxProject_; }

    void clearSelection() { selectedNode_ = nullptr; document_.clearSelection(); }

    bool quitRequested() const { return quitRequested_; }

    bool isViewportHovered(float mx, float my) const {
        return mx >= viewportPos_.x && mx <= viewportPos_.x + viewportSize_.x &&
               my >= viewportPos_.y && my <= viewportPos_.y + viewportSize_.y;
    }
    glm::vec2 viewportPosition() const { return viewportPos_; }
    glm::vec2 viewportSize() const { return viewportSize_; }

    void openModelImporter(const std::string& path, ResourceManager* resources);
    void closeModelImporter();

    bool isPreviewMode() const { return isPreviewMode_; }
    Scene* previewScene() const { return previewScene_.get(); }

    // Play is inspectable but read-only: no editor mutation may touch the live
    // World sub-scene. Every command and inspector edit is gated on this.
    bool canEdit() const;

    void markDirty();

private:
    // Play mode rejects edits so the live scene cannot diverge from the document.
    void execute(std::unique_ptr<Command> command);

    void saveScene(Scene* scene, ResourceManager* resources, const std::string& path);
    void loadScene(Scene* scene, ResourceManager* resources, const std::string& path);
    void copySelected(ResourceManager* resources);
    void pasteClipboard(Scene* scene, ResourceManager* resources);
    void duplicateSelected(ResourceManager* resources);
    
    void drawGizmo(Camera* camera, Scene* scene);
    void updateGizmoHover(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec2& mousePos, int& outHoveredAxis);
    void handleGizmoDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir, const glm::vec2& mousePos);
    void performRaycastSelection(Scene* scene, const glm::vec3& rayOrigin, const glm::vec3& rayDir);
    void renderGizmoRotationRings(ImDrawList* drawList, Camera* camera, const glm::mat4& viewProj, int hoveredAxis);
    void renderGizmoTranslateScale(ImDrawList* drawList, int hoveredAxis);

    void drawColliderGizmos(Camera* camera, Scene* scene);
    
    void drawAboutWindow();
    void drawBuildWindow(Project* project);
    void drawSettingsWindow(Project* project);
    void drawNewProjectDialog(Project* project);
    void drawOpenProjectDialog(Project* project);
    void drawSaveSceneAsDialog(Project* project, Scene* scene, ResourceManager* resources);
    void applyEditorStyle();

    bool showSceneTree_   = true;
    bool showInspector_   = true;
    bool showFileBrowser_ = true;
    bool showViewportOverlay_ = true;
    bool showModelImporter_ = false;
    bool showProfiler_ = false;
    bool showAnimation_ = false;

    // Animation panel — l'état survit aux frames (les panneaux sont recréés).
    char animViewName_[128] = "NewView";
    int animViewSourceIndex_ = 0;
    float animViewStart_ = 0.0f;
    float animViewEnd_ = 0.0f;
    bool animViewLoop_ = true;
    float animViewSpeed_ = 1.0f;
    std::string animStatus_;
    std::vector<std::string> animAssetFiles_;  // .sclip/.sgraph du projet (relatifs)
    double animAssetScanTime_ = -1.0;
    uint64_t animAppliedGraphId_ = 0;  // AssetID du .sgraph appliqué (0 = aucun)

    Node* selectedNode_ = nullptr;

    bool quitRequested_ = false;
    bool dockLayoutBuilt_ = false;

    std::string currentBrowsePath_;
    float fileBrowserZoom_ = 1.0f;

    // Cache listings to avoid disk access every frame.
    struct FileListing {
        std::string path;
        std::string query;
        double time = -1.0;
        std::vector<std::string> dirs;
        std::vector<std::string> files;
    };
    FileListing fileListing_;

    bool showNewProjectDialog_  = false;
    bool showOpenProjectDialog_ = false;
    bool showSaveSceneAsDialog_ = false;
    bool showAboutWindow_       = false;
    bool showBuildWindow_       = false;
    bool showSettingsWindow_    = false;
    bool useLightTheme_         = false;
    char newProjectName_[128]   = "MyGame";
    char newProjectPath_[512]   = "";
    char saveScenePathBuf_[512] = "main.scene";
    void rebuildSceneHierarchy(Scene* scene);

    std::string resolveScenePath(Project* project) const;

    void loadProjectMainScene(Project* project, Scene* scene, ResourceManager* resources);

    // Scan asynchronously so opening the dialog never blocks on disk I/O.
    void startProjectScan(const std::string& root);

    std::string openBrowsePath_;
    std::vector<std::string> openProjCache_;
    std::future<std::vector<std::string>> openScanFuture_;
    bool openScanDone_ = false;

    BuildPlatform selectedBuildPlatform_ = BuildPlatform::Windows;
    BuildConfig buildConfiguration_ = BuildConfig::Release;
    char buildOutputPath_[512] = "build/export";
    bool buildCopyAssets_ = true;
    bool buildEnableLto_ = false;
    bool buildSceneMainChecked_ = true;
    bool buildSceneDemoChecked_ = false;

    std::vector<std::string> buildScenes_;
    int buildMainSceneIndex_ = 0;
    bool buildHasResult_ = false;
    bool buildLastSuccess_ = false;
    std::string buildLastError_;
    std::string buildLastLog_;
    std::string buildLastOutputDir_;
    std::string buildLastExe_;
    void refreshBuildScenes_(Project* project);

    // Deferral avoids iterator invalidation during UI traversal.
    Node* nodeToDelete_ = nullptr;
    Node* nodeToCreateChildUnder_ = nullptr;
    Node* nodeToCreateParentFor_ = nullptr;
    CreateNodeType createType_ = CreateNodeType::None;
    CreateNodeType createParentType_ = CreateNodeType::None;
    std::string draggedScenePath_;

    Node* nodeToReparent_ = nullptr;
    Node* newParent_ = nullptr;
    size_t newChildIndex_ = static_cast<size_t>(-1);

    Node* nodeToRename_ = nullptr;
    char nodeRenameBuf_[128] = "";

    std::string fileToRename_;
    char fileRenameBuf_[256] = "";

    char sceneTreeSearchBuf_[128] = "";
    char fileBrowserSearchBuf_[128] = "";

    GizmoMode gizmoMode_ = GizmoMode::Translate;
    GizmoAxis grabbedAxis_ = GizmoAxis::None;
    glm::vec3 dragStartNodePos_{0.0f};
    glm::vec3 dragStartNodeRotEuler_{0.0f};
    glm::quat dragStartNodeRotQuat_{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 dragStartNodeScale_{1.0f};
    glm::vec2 dragStartMousePos_{0.0f};
    glm::vec3 dragStartHitPos3D_{0.0f};

    glm::vec3 gizmoNodePos_{0.0f};
    float gizmoWorldLength_{0.0f};
    glm::vec2 gizmoCenter2D_{0.0f};
    glm::vec2 gizmoEnds2D_[3];
    glm::vec3 gizmoLocalAxes_[3];
    bool gizmoAxisValid_[3]{false, false, false};

    glm::vec2 viewportPos_{0.0f, 0.0f};
    glm::vec2 viewportSize_{0.0f, 0.0f};

    SceneDocument document_;
    CommandHistory history_;

    // A single ImGui widget is active, so one transactional edit slot suffices.
    unsigned int propEditId_ = 0;
    std::any propEditOld_;

    // A project change invalidates history and clipboard node references.
    uint64_t lastProjectVersion_ = 0;
    std::string clipboard_;
    std::string currentScenePath_;
    EditorApp* app_ = nullptr;
    Scene* ctxScene_ = nullptr;
    Camera* ctxCamera_ = nullptr;
    ResourceManager* ctxResources_ = nullptr;
    Project* ctxProject_ = nullptr;
    ThumbnailCache thumbnails_;
    ThumbnailCache brandingImages_;

    bool isPreviewMode_ = false;
    std::string previewModelPath_;
    std::unique_ptr<Scene> previewScene_;

#ifdef SAIDA_ENABLE_MCP
    std::unique_ptr<McpBridge> mcp_;
#endif
};

} // namespace saida
