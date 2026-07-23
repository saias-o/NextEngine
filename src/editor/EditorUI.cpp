#include "editor/EditorUI.hpp"

#include "core/Camera.hpp"
#include "core/Paths.hpp"
#include "core/Time.hpp"
#include "editor/Command.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Texture.hpp"
#include "project/Project.hpp"
#include "nodes/MeshNode.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneSerializer.hpp"
#include "audio/AudioManager.hpp"

#include "editor/panels/MenuBarPanel.hpp"
#include "editor/panels/SceneHierarchyPanel.hpp"
#include "editor/panels/InspectorPanel.hpp"
#include "editor/panels/FileBrowserPanel.hpp"
#include "editor/panels/ViewportPanel.hpp"
#include "editor/panels/ProfilerPanel.hpp"
#include "editor/panels/AnimationPanel.hpp"
#ifdef SAIDA_ENABLE_MCP
#include "mcp/McpBridge.hpp"
#endif

#include <memory>

#include "imgui.h"
#include "imgui_internal.h"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "editor/EditorApp.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace saida {

// Construction

namespace {
std::filesystem::path editorThemePreferencePath() {
    if (const char* appData = std::getenv("APPDATA")) {
        if (*appData != '\0')
            return std::filesystem::path(appData) / "SaidaEngine" / "editor_theme.txt";
    }
    return std::filesystem::path(SAIDA_PROJECT_ROOT) / "build" / "editor_theme.txt";
}

bool loadLightThemePreference() {
    std::ifstream input(editorThemePreferencePath());
    std::string value;
    return input && std::getline(input, value) && value == "light";
}

void saveLightThemePreference(bool useLightTheme) {
    const std::filesystem::path path = editorThemePreferencePath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path);
    if (output) output << (useLightTheme ? "light" : "dark");
}
} // namespace

EditorUI::EditorUI() : history_(document_) {
    useLightTheme_ = loadLightThemePreference();
    applyEditorStyle();
#ifdef SAIDA_ENABLE_MCP
    // SAIDA_MCP_PORT; default 8765. Disabled entirely by SAIDA_MCP=0.
    const char* disabled = std::getenv("SAIDA_MCP");
    if (!disabled || std::string(disabled) != "0") {
        uint16_t port = 8765;
        if (const char* p = std::getenv("SAIDA_MCP_PORT")) {
            try { port = static_cast<uint16_t>(std::stoi(p)); } catch (...) {}
        }
        mcp_ = std::make_unique<McpBridge>();
        mcp_->start(port);
    }
#endif
}

EditorUI::~EditorUI() = default;  // Scene/McpBridge complete here (unique_ptr members)

void EditorUI::applyEditorStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    // The web platform uses soft 8 px cards and a deliberately relaxed rhythm.
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 8.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;

    // This spacing also makes context menus and the top menu less cramped.
    style.WindowPadding    = ImVec2(13, 13);
    style.FramePadding     = ImVec2(10, 7);
    style.CellPadding      = ImVec2(10, 7);
    style.ItemSpacing      = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.IndentSpacing    = 22.0f;
    style.ScrollbarSize    = 13.0f;
    style.GrabMinSize      = 11.0f;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize  = 1.0f;
    style.PopupBorderSize  = 1.0f;
    style.FrameBorderSize  = 1.0f;
    style.TabBorderSize    = 0.0f;

    // Two Saida themes: ink-dark by default, paper-light as an option.
    ImVec4* c = style.Colors;
    const bool light = useLightTheme_;

    // Shared Saida brand colors from the web platform.
    const ImVec4 brand     = ImVec4(0.725f, 0.922f, 0.063f, 1.0f); // #b9eb10
    const ImVec4 brandDeep = ImVec4(0.412f, 0.714f, 0.184f, 1.0f); // #69b62f
    const ImVec4 paper     = ImVec4(0.984f, 0.973f, 0.929f, 1.0f); // #fbf8ed
    const ImVec4 darkGreen = ImVec4(0.106f, 0.310f, 0.125f, 1.0f);

    // Dark remains the default: neutral charcoal with a very subtle green
    // cast, so the Saida lime is the only strong hue in the editor chrome.
    const ImVec4 bg       = light ? ImVec4(0.886f, 0.871f, 0.827f, 1.0f) : ImVec4(0.063f, 0.075f, 0.067f, 1.0f);
    const ImVec4 bgChild  = light ? ImVec4(0.839f, 0.816f, 0.761f, 1.0f) : ImVec4(0.090f, 0.110f, 0.094f, 1.0f);
    const ImVec4 bgPopup  = light ? ImVec4(0.933f, 0.918f, 0.882f, 1.0f) : ImVec4(0.106f, 0.129f, 0.110f, 1.0f);
    const ImVec4 accent   = light ? darkGreen : brand;
    const ImVec4 accentH  = light ? ImVec4(0.153f, 0.396f, 0.173f, 1.0f) : ImVec4(0.816f, 0.965f, 0.357f, 1.0f);
    const ImVec4 accentA  = light ? ImVec4(0.710f, 0.690f, 0.643f, 1.0f) : brandDeep;
    const ImVec4 text     = light ? darkGreen : paper;
    const ImVec4 textDim  = light ? ImVec4(0.251f, 0.373f, 0.263f, 1.0f) : ImVec4(0.635f, 0.702f, 0.651f, 1.0f);
    const ImVec4 border   = light ? ImVec4(0.224f, 0.243f, 0.216f, 0.78f) : ImVec4(0.208f, 0.255f, 0.224f, 0.88f);
    const ImVec4 header   = light ? ImVec4(0.855f, 0.831f, 0.776f, 1.0f) : ImVec4(0.133f, 0.165f, 0.141f, 1.0f);
    const ImVec4 headerH  = light ? ImVec4(0.784f, 0.765f, 0.718f, 1.0f) : ImVec4(0.173f, 0.220f, 0.180f, 1.0f);

    c[ImGuiCol_Text]                  = text;
    c[ImGuiCol_TextDisabled]          = textDim;
    c[ImGuiCol_WindowBg]              = bg;
    c[ImGuiCol_ChildBg]               = bgChild;
    c[ImGuiCol_PopupBg]               = bgPopup;
    c[ImGuiCol_Border]                = border;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = light ? ImVec4(0.941f, 0.925f, 0.890f, 1.0f) : ImVec4(0.047f, 0.063f, 0.051f, 1.0f);
    c[ImGuiCol_FrameBgHovered]        = light ? headerH : ImVec4(0.122f, 0.165f, 0.133f, 1.0f);
    c[ImGuiCol_FrameBgActive]         = light ? bg : ImVec4(0.153f, 0.200f, 0.161f, 1.0f);
    c[ImGuiCol_TitleBg]               = bgChild;
    c[ImGuiCol_TitleBgActive]         = light ? headerH : ImVec4(0.110f, 0.149f, 0.118f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]      = bgChild;
    c[ImGuiCol_MenuBarBg]             = light ? bg : ImVec4(0.082f, 0.102f, 0.086f, 1.0f);
    c[ImGuiCol_ScrollbarBg]           = bgChild;
    c[ImGuiCol_ScrollbarGrab]         = light ? ImVec4(0.596f, 0.576f, 0.529f, 1.0f) : ImVec4(0.208f, 0.255f, 0.224f, 1.0f);
    c[ImGuiCol_ScrollbarGrabHovered]  = light ? ImVec4(0.431f, 0.424f, 0.392f, 1.0f) : ImVec4(0.306f, 0.373f, 0.322f, 1.0f);
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
    c[ImGuiCol_ResizeGrip]            = light ? ImVec4(0.596f, 0.576f, 0.529f, 1.0f) : border;
    c[ImGuiCol_ResizeGripHovered]     = accent;
    c[ImGuiCol_ResizeGripActive]      = accentH;
    c[ImGuiCol_Tab]                   = header;
    c[ImGuiCol_TabHovered]            = headerH;
    c[ImGuiCol_TabSelected]           = light ? accentA : header;
    c[ImGuiCol_TabSelectedOverline]   = accent;
    c[ImGuiCol_TabDimmed]             = bgChild;
    c[ImGuiCol_TabDimmedSelected]     = header;
    c[ImGuiCol_DockingPreview]        = ImVec4(accent.x, accent.y, accent.z, 0.7f);
    c[ImGuiCol_DockingEmptyBg]        = bgChild;
    c[ImGuiCol_PlotLines]             = textDim;
    c[ImGuiCol_PlotLinesHovered]      = accentH;
    c[ImGuiCol_PlotHistogram]         = accent;
    c[ImGuiCol_PlotHistogramHovered]  = accentH;
    c[ImGuiCol_TableHeaderBg]         = header;
    c[ImGuiCol_TableBorderStrong]     = border;
    c[ImGuiCol_TableBorderLight]      = ImVec4(border.x, border.y, border.z, 0.55f);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = light ? ImVec4(0.710f, 0.690f, 0.643f, 0.28f) : ImVec4(0.412f, 0.714f, 0.184f, 0.07f);
    c[ImGuiCol_TextSelectedBg]        = ImVec4(accent.x, accent.y, accent.z, 0.35f);
    c[ImGuiCol_DragDropTarget]        = accent;
    c[ImGuiCol_NavHighlight]          = accent;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = light ? ImVec4(0, 0, 0, 0.18f) : ImVec4(0, 0, 0, 0.48f);
    c[ImGuiCol_ModalWindowDimBg]      = light ? ImVec4(0, 0, 0, 0.24f) : ImVec4(0, 0, 0, 0.58f);
}

// Main draw entry point

void EditorUI::draw(EditorApp* app, Scene* scene, Camera* camera, Project* project, ResourceManager* resources, float dt) {
    document_.bind(scene, resources);

    // A project create/load invalidates all per-project editor state: the undo
    // history references nodes from the previous scene, the clipboard holds a
    // subtree serialized against the old resources, etc. Drop them on change.
    if (project && project->version() != lastProjectVersion_) {
        lastProjectVersion_ = project->version();
        history_.clear();
        document_.clearSessionReferences();
        propEditId_ = 0;
        propEditOld_.reset();

        // Auto-load the project's entry-point scene (sets the document path).
        const std::string mainScene =
            projectDialogs_.findMainScene(project);
        if (!mainScene.empty()) loadScene(mainScene);
    }

    selectedNode_ = document_.selectedNode();
    app_ = app;
    ctxScene_ = scene;
    ctxCamera_ = camera;
    ctxProject_ = project;
    ctxResources_ = resources;

#ifdef SAIDA_ENABLE_MCP
    // Bind the document to the live scene, then service any queued MCP requests
    // on this (main) thread while all ctx pointers are valid.
    if (mcp_) {
        document_.bind(scene, resources);
        mcp_->poll(*this);
    }
#endif

    // Keyboard shortcuts (skip while typing in a text field).
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F3)) {
        showProfiler_ = !showProfiler_;
    }
    if (io.KeyCtrl && !io.WantTextInput) {
        // Copy and Save are read-only and stay available in Play; the mutating
        // shortcuts (undo/redo/paste/duplicate) are gated on canEdit().
        if (ImGui::IsKeyPressed(ImGuiKey_C)) copySelected();
        if (ImGui::IsKeyPressed(ImGuiKey_S)) {
            saveScene(resolveScenePath(project));
        }
        if (canEdit()) {
            if (ImGui::IsKeyPressed(ImGuiKey_Z) && history_.canUndo()) { history_.undo(); selectedNode_ = nullptr; }
            if (ImGui::IsKeyPressed(ImGuiKey_Y) && history_.canRedo()) { history_.redo(); selectedNode_ = nullptr; }
            if (ImGui::IsKeyPressed(ImGuiKey_V)) pasteClipboard();
            if (ImGui::IsKeyPressed(ImGuiKey_D)) duplicateSelected();
        }
    }

    // Delete / Backspace removes the selected node (mirrors the context Delete).
    if (canEdit() && !io.WantTextInput && selectedNode_ && selectedNode_->parent()
        && (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))) {
        nodeToDelete_ = selectedNode_;
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
    menuBarPanel.draw(this, project, scene);

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
        fileBrowserPanel.draw(this, project, resources);
    }

    modelImporter_.draw();

    if (showProfiler_) {
        ProfilerPanel profilerPanel;
        profilerPanel.draw(this);
    }

    if (showAnimation_) {
        AnimationPanel animationPanel;
        animationPanel.draw(this, scene, resources, project);
    }

    ViewportPanel viewportPanel;
    viewportPanel.draw(this, camera, dt);
    
    gizmo_.draw(*this, camera, scene);
    gizmo_.drawColliders(*this, camera, scene);

    // Modal dialogs.
    drawAboutWindow();
    buildController_.draw(project);
    drawSettingsWindow(project);
    const ProjectDialogs::Actions dialogActions =
        projectDialogs_.draw(project);
    if (!dialogActions.browseRoot.empty())
        currentBrowsePath_ = dialogActions.browseRoot;
    if (!dialogActions.sceneToSave.empty())
        saveScene(dialogActions.sceneToSave);
    document_.select(selectedNode_);
}

// Scene update: serialization, clipboard, undo/redo helpers

bool EditorUI::canEdit() const {
    return !(app_ && app_->isPlayMode());
}

bool EditorUI::runAutomatedBuild(Project* project, bool web,
                                 const std::string& outputDir,
                                 std::string* message) {
    return buildController_.runAutomatedBuild(
        project, web, outputDir, message);
}

void EditorUI::markDirty() {
    if (canEdit()) document_.markDirty();
}

void EditorUI::execute(std::unique_ptr<Command> command) {
    if (!canEdit()) return;  // Play mode is read-only.
    history_.execute(std::move(command));
}

void EditorUI::saveScene(const std::string& path) {
    document_.save(path);
}

void EditorUI::loadScene(const std::string& path) {
    if (document_.load(path)) {
        selectedNode_ = nullptr;
        history_.clear();
    }
}

void EditorUI::copySelected() {
    document_.copy(selectedNode_);
}

void EditorUI::pasteClipboard() {
    if (!canEdit()) return;
    Node* parent = selectedNode_ ? selectedNode_ : document_.scene();
    if (Node* pasted = document_.paste(parent, history_))
        selectedNode_ = pasted;
}

void EditorUI::duplicateSelected() {
    if (!canEdit()) return;
    if (Node* duplicate = document_.duplicate(selectedNode_, history_))
        selectedNode_ = duplicate;
}

void EditorUI::openModelImporter(const std::string& path, ResourceManager* resources) {
    modelImporter_.open(path, ctxProject_, resources);
}

// About SaidaEngine dialog (modal popup)

void EditorUI::drawAboutWindow() {
    if (showAboutWindow_) {
        ImGui::OpenPopup("About SaidaEngine");
        showAboutWindow_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("About SaidaEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Spacing();

        brandingImages_.beginFrame();
        ImTextureID logo = 0;
        if (ctxResources_)
            logo = brandingImages_.get(ctxResources_->device(), assetPath("assets/editor/saida_logo.png"));

        if (logo) {
            constexpr float logoWidth = 96.0f;
            constexpr float logoHeight = logoWidth * 306.0f / 256.0f;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - logoWidth) * 0.5f);
            ImGui::Image(logo, ImVec2(logoWidth, logoHeight));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text(" SaidaEngine C++ Game Engine");
        ImGui::Text(" Version: "); ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.30f, 0.70f, 0.35f, 1.0f), "Alpha 0.0.1"); // Sleek Green

        ImGui::Spacing();
        ImGui::Text(" A lightweight, Vulkan-powered 3D editor and runtime.");
        ImGui::Text(" Designed for modularity, speed, and simplicity.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextDisabled(" Powered by Vulkan, GLFW, Dear ImGui, and GLM.");
        ImGui::TextDisabled(" Copyright (c) 2026 SaidaEngine Contributors.");

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.851f, 0.576f, 0.290f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.951f, 0.676f, 0.390f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.751f, 0.476f, 0.190f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        
        if (ImGui::Button("Sponsor SaidaEngine (Donate)", ImVec2(ImGui::GetContentRegionAvail().x, 35.0f))) {
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

// Settings

void EditorUI::drawSettingsWindow(Project* project) {
    if (!showSettingsWindow_) return;

    ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Settings", &showSettingsWindow_)) {
        ImGui::SeparatorText("Appearance");
        int themeIndex = useLightTheme_ ? 1 : 0;
        const char* themeNames[] = { "Saida Dark", "Saida Light" };
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::Combo("Editor Theme", &themeIndex, themeNames, 2)) {
            useLightTheme_ = themeIndex == 1;
            applyEditorStyle();
            saveLightThemePreference(useLightTheme_);
        }
        ImGui::TextDisabled("Saida Dark is the default. The 3D viewport stays dark in both themes.");
        ImGui::Spacing();

        if (!project || !project->isLoaded()) {
            ImGui::TextDisabled("No project loaded.");
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("SettingsTabs")) {
            
            if (ImGui::BeginTabItem("General")) {
                ImGui::Spacing();
                // Le renommage change le dossier, le .saidaproj et hub.json
                // ensemble (renameProjectDirectory); il n'est sûr que projet
                // fermé, donc il appartient au Hub, pas à un projet ouvert.
                char nameBuf[128];
                std::snprintf(nameBuf, sizeof(nameBuf), "%s", project->name().c_str());
                ImGui::BeginDisabled();
                ImGui::InputText("Project Name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_ReadOnly);
                ImGui::EndDisabled();
                ImGui::TextDisabled("Rename from the Hub (project must be closed).");
                
                ImGui::SeparatorText("Performance");
                int maxFps = project->maxFps();
                if (ImGui::InputInt("Max FPS (0 = Unlimited)", &maxFps)) {
                    if (maxFps < 0) maxFps = 0;
                    project->setMaxFps(maxFps);
                    project->save();
                }
                
                ImGui::SeparatorText("Metadata");
                ImGui::TextDisabled("Version: 1.0.0");
                ImGui::TextDisabled("Company Name: DefaultCompany");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Rendering")) {
                ImGui::Spacing();
                ImGui::SeparatorText("Mesh LODs");
                bool autoLods = project->autoMeshLods();
                if (ImGui::Checkbox("Auto Mesh LODs##Rendering", &autoLods)) {
                    project->setAutoMeshLods(autoLods);
                    project->save();
                }
                ImGui::TextDisabled("Imports .glb through AutoLOD and adds editable LOD Group components.");

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

                bool vsync = project->vSync();
                if (ImGui::Checkbox("V-Sync", &vsync)) {
                    project->setVSync(vsync);
                    project->save();
                }
                
                ImGui::Spacing();
                ImGui::TextDisabled("(Note: Anti-aliasing will be wired to the Vulkan backend later)");

                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Editor")) {
                ImGui::Spacing();
                ImGui::SeparatorText("Preferences");

                bool showColliders = project->showColliders();
                if (ImGui::Checkbox("Show Colliders", &showColliders)) {
                    project->setShowColliders(showColliders);
                    project->save();
                }
                
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

            if (ImGui::BeginTabItem("Autoloads")) {
                ImGui::TextDisabled("Persistent singletons spawned into the World at play time.");
                ImGui::TextDisabled("They survive scene changes (game state, save, persistent UI...).");
                ImGui::TextDisabled("Value: a 'scenes/X.scene' prefab path, or a behaviour type name.");
                ImGui::Spacing();

                std::string toRemove;
                if (ImGui::BeginTable("AutoloadsTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Scene / Type", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##Action", ImGuiTableColumnFlags_WidthFixed, 30.0f);
                    ImGui::TableHeadersRow();

                    for (const auto& [name, value] : project->autoloads()) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::Text("%s", name.c_str());
                        ImGui::TableNextColumn();
                        ImGui::TextDisabled("%s", value.c_str());
                        ImGui::TableNextColumn();
                        ImGui::PushID(name.c_str());
                        if (ImGui::Button("X", ImVec2(24, 0))) toRemove = name;
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (!toRemove.empty()) project->removeAutoload(toRemove);

                ImGui::Spacing();
                ImGui::SeparatorText("Register New Autoload");
                static char newAutoName[64] = "";
                static char newAutoVal[256] = "";
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##AutoName", "Name (e.g. 'GameState')", newAutoName, sizeof(newAutoName));
                ImGui::SetNextItemWidth(-1);
                ImGui::InputTextWithHint("##AutoVal", "scenes/X.scene  or  BehaviourType", newAutoVal, sizeof(newAutoVal));

                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("FILE_SCENE")) {
                        std::filesystem::path fp((const char*)payload->Data);
                        std::filesystem::path root(project->rootPath());
                        std::string relPath = std::filesystem::relative(fp, root).string();
                        std::replace(relPath.begin(), relPath.end(), '\\', '/');
                        std::strncpy(newAutoVal, relPath.c_str(), sizeof(newAutoVal) - 1);
                        if (strlen(newAutoName) == 0)
                            std::strncpy(newAutoName, fp.stem().string().c_str(), sizeof(newAutoName) - 1);
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::Button("Add / Update Autoload", ImVec2(-1, 0))) {
                    if (strlen(newAutoName) > 0 && strlen(newAutoVal) > 0) {
                        project->setAutoload(newAutoName, newAutoVal);
                        newAutoName[0] = '\0';
                        newAutoVal[0] = '\0';
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
    return projectDialogs_.resolveScenePath(
        document_.currentPath(), project);
}

} // namespace saida
