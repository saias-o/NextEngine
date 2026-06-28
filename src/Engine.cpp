#include "Engine.hpp"

#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Time.hpp"
#include "core/Window.hpp"
#include "graphics/ImGuiLayer.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Swapchain.hpp"
#include "graphics/VulkanDevice.hpp"
#include "project/Project.hpp"
#include "render/Renderer.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneTree.hpp"
#include "scene/SceneSerializer.hpp"
#include "scene/BehaviourRegistry.hpp"
#include "scene/NodeRegistry.hpp"
#include "scene/MeshNode.hpp"
#include "scene/LightNode.hpp"
#include "scene/CameraNode.hpp"
#include "physics/CollisionShapeNode.hpp"
#include "physics/StaticBodyNode.hpp"
#include "physics/RigidBodyNode.hpp"
#include "physics/AreaNode.hpp"
#include "physics/CharacterBodyNode.hpp"
#include "scene/WebCanvasNode.hpp"
#include "scene/UINode.hpp"
#include "scene/UICanvasNode.hpp"
#include "scene/UIColorNode.hpp"
#include "scene/UIImageNode.hpp"
#include "scene/UITextNode.hpp"
#include "scene/UIInteractableNode.hpp"
#include "scene/UIButtonNode.hpp"
#include "scene/UIToggleNode.hpp"
#include "audio/AudioManager.hpp"
#include "audio/AudioSourceBehaviour.hpp"
#include "scene/CharacterBehaviour.hpp"
#include "scene/CameraFollowBehaviour.hpp"
#include "scene/SpawnerBehaviour.hpp"
#include "scene/RotatorBehaviour.hpp"
#include "scene/ReflectedTypes.hpp"
#include "render/RenderFeatureRegistry.hpp"
#include "scene/LODGroupBehaviour.hpp"
#include "scene/animation/Animator.hpp"
#include "scripting/ScriptBehaviour.hpp"
#include "ui/RmlUiRuntime.hpp"
#ifdef NE_ENABLE_XR
#include "xr/XrInstance.hpp"
#include "xr/XrSession.hpp"
#include "xr/XrVulkanBinding.hpp"
#include "xr/XrRegistration.hpp"
#include "xr/toolkit/XRInput.hpp"
#include "xr/toolkit/XROrigin.hpp"
#endif

#include <exception>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <process.h>
#endif

namespace ne {

namespace {
constexpr uint32_t kWidth = 1600;
constexpr uint32_t kHeight = 900;
}

Engine::Engine(SceneSetup sceneSetup, const std::string& initialProject, bool requireXr) {
    // The XR process currently has no desktop mirror swapchain. Keep its host
    // GLFW window hidden instead of presenting a misleading unrendered surface.
    window_ = std::make_unique<Window>(kWidth, kHeight, "NextEngine", !requireXr);
    Input::bind(window_.get());

    // Process roles are explicit: the editor always owns a desktop Vulkan/ImGui
    // path, while the --xr preview process requires OpenXR from startup. A Vulkan
    // device cannot be converted from one presentation owner to the other later.
#ifdef NE_ENABLE_XR
    if (requireXr) {
        try {
            xrInstance_ = std::make_unique<xr::Instance>();
            xrCreator_ = std::make_unique<xr::XrVulkanDeviceCreator>(*xrInstance_);
            device_ = std::make_unique<VulkanDevice>(*window_, xrCreator_.get());
            xrSession_ = std::make_unique<xr::Session>(*xrInstance_, *device_);
            xrMode_ = true;
            Log::info("XR mode active (OpenXR session)");
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("XR preview startup failed: ") + e.what());
        }
    }
#endif
    if (!device_) device_ = std::make_unique<VulkanDevice>(*window_);
    if (!xrMode_) swapchain_ = std::make_unique<Swapchain>(*device_, *window_);
    resources_ = std::make_unique<ResourceManager>(*device_);

    scene_ = std::make_unique<Scene>();
    project_ = std::make_unique<Project>();
    sceneTree_ = std::make_unique<SceneTree>(*resources_);

    resources_->setRegistry(&project_->assetRegistry());

    if (!initialProject.empty() && !project_->load(initialProject))
        Log::warn("Failed to load initial project: ", initialProject);

    // No project loaded → the game provides its scene.
    if (!project_->isLoaded() && sceneSetup)
        sceneSetup(*scene_, *resources_);

    AudioManager::get().init();
    
    // Reflection-described types (manifest + factories), incl. Rotator, Character,
    // LightNode. Single central list — see scene/ReflectedTypes.cpp.
    registerReflectedTypes();
    registerBuiltinRenderFeatures();  // water/skybox/debug-lines plug into the Renderer

    // Register remaining built-in behaviours (not yet migrated to reflection).
    BehaviourRegistry::instance().registerType<Animator>("Animator");
    BehaviourRegistry::instance().registerType<LODGroupBehaviour>("LOD Group");
    BehaviourRegistry::instance().registerType<ScriptBehaviour>("ScriptBehaviour");
#ifdef NE_ENABLE_XR
    ne::xr::registerTypes();
#endif

    // Register built-in nodes
    NodeRegistry::instance().registerType<Node>("Node");
    NodeRegistry::instance().registerType<Scene>("Scene");
    NodeRegistry::instance().registerType<MeshNode>("MeshNode");
    // LightNode registered via registerReflectedTypes().
    NodeRegistry::instance().registerType<CameraNode>("Camera");
    NodeRegistry::instance().registerType<WebCanvasNode>("WebCanvasNode");
    NodeRegistry::instance().registerType<UINode>("UINode");
    NodeRegistry::instance().registerType<UICanvasNode>("UICanvasNode");
    NodeRegistry::instance().registerType<UIColorNode>("UIColorNode");
    NodeRegistry::instance().registerType<UIImageNode>("UIImageNode");
    NodeRegistry::instance().registerType<UITextNode>("UITextNode");
    NodeRegistry::instance().registerType<UIInteractableNode>("UIInteractableNode");
    NodeRegistry::instance().registerType<UIButtonNode>("UIButtonNode");
    NodeRegistry::instance().registerType<UIToggleNode>("UIToggleNode");
    NodeRegistry::instance().registerType<CollisionShapeNode>("CollisionShape");
    NodeRegistry::instance().registerType<StaticBodyNode>("StaticBody");
    NodeRegistry::instance().registerType<RigidBodyNode>("RigidBody");
    NodeRegistry::instance().registerType<AreaNode>("Area");
    NodeRegistry::instance().registerType<CharacterBodyNode>("CharacterBody");
    // XR Toolkit nodes/behaviours are registered by xr::registerTypes() above.
    if (project_->isLoaded()) {
        AudioManager::get().setProjectRoot(project_->rootPath());
        AudioManager::get().setDefaultSettings(project_->defaultAudioSettings());
        AudioManager::get().setMasterVolume(project_->masterVolume());
    }

    // Desktop present path: ImGui overlay + Renderer driving the window swapchain.
    // In XR mode the OpenXR session owns presentation, and the Renderer is built
    // for stereo multiview (2-layer targets sized to one eye, no ImGui/swapchain).
    if (!xrMode_) {
        imgui_ = std::make_unique<ImGuiLayer>(*device_, *window_, swapchain_->colorFormat(),
            swapchain_->imageCount(), VK_SAMPLE_COUNT_1_BIT);
        renderer_ = std::make_unique<Renderer>(*device_, *swapchain_, *window_,
            *resources_, *imgui_);
    }
#ifdef NE_ENABLE_XR
    else {
        renderer_ = std::make_unique<Renderer>(*device_, *window_, *resources_,
            xrSession_->eyeExtent(), static_cast<VkFormat>(xrSession_->colorFormat()),
            xrSession_->viewCount());
    }
#endif

    // Default editor/viewport camera placement (the app may move it).
    camera_.position = {0.0f, 2.5f, 8.0f};
    camera_.yaw = -90.0f;
    camera_.pitch = -15.0f;
}

#ifdef NE_ENABLE_XR
bool Engine::launchExternalPreviewIfNeeded() {
    bool xrScene = false;
    scene_->traverse([&](Node& node, const glm::mat4&) {
        if (dynamic_cast<XROrigin*>(&node)) xrScene = true;
    });
    if (!xrScene) return false;

    if (!project_->isLoaded()) {
        Log::error("XR Preview requires a loaded .neproj project");
        return true;
    }

    const std::filesystem::path scenePath =
        std::filesystem::path(NE_BINARY_DIR) / "xr_preview.scene";
    if (!SceneSerializer::saveToFile(*scene_, *resources_, scenePath.string())) {
        Log::error("XR Preview could not serialize the current scene");
        return true;
    }

    const std::filesystem::path executable =
        std::filesystem::path(NE_RUNTIME_DIR) / "NextEngine.exe";
    if (!std::filesystem::exists(executable)) {
        Log::error("XR Preview executable not found: ", executable.string());
        return true;
    }

    // MinGW's spawn command-line quoting is not reliable for arguments containing
    // spaces. Keep the child command line path-free and transfer launch data via
    // a fixed manifest next to the executable.
    const std::filesystem::path manifestPath =
        std::filesystem::path(NE_BINARY_DIR) / "xr_preview.launch";
    {
        std::ofstream manifest(manifestPath, std::ios::trunc);
        if (!manifest) {
            Log::error("XR Preview could not create launch manifest: ",
                       manifestPath.string());
            return true;
        }
        manifest << project_->filePath() << '\n' << scenePath.string() << '\n';
    }

#ifdef _WIN32
    std::vector<std::string> arguments = {
        executable.string(), "--xr-preview"};
    std::vector<const char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const std::string& argument : arguments) argv.push_back(argument.c_str());
    argv.push_back(nullptr);

    const intptr_t child = _spawnv(_P_NOWAIT, executable.string().c_str(), argv.data());
    if (child == -1) {
        Log::error("XR Preview process launch failed: ", std::strerror(errno));
        return true;
    }
    std::thread([child] {
        int exitCode = 0;
        _cwait(&exitCode, child, _WAIT_CHILD);
    }).detach();
    Log::info("XR Preview launched (process ", child, ")");
#else
    Log::error("XR Preview process launch is currently implemented for Windows only");
#endif  // _WIN32
    return true;
}
#endif  // NE_ENABLE_XR

Engine::~Engine() {
    unmountWorld();  // tear down the World (and its physics) before subsystems
    scene_.reset();
    sceneOverride_ = nullptr;
    sceneTree_.reset();
    RmlUiRuntime::shutdown();
    AudioManager::get().shutdown();
    vkDeviceWaitIdle(device_->device());
    // Subsystems are torn down by their unique_ptr destructors, in reverse
    // declaration order, while device_ is still alive (renderer_ before imgui_,
    // swapchain_ and device_).
}

void Engine::mountWorld() {
    cameraDirector_.reset();  // first scene camera snaps in (no blend from stale state)
    sceneTree_->setProjectRoot(project_->rootPath());  // resolve relative .scene paths

    // Register the project's data-driven autoloads (idempotent: dedup by name).
    // A value ending in ".scene" is a prefab path; otherwise a behaviour type.
    for (const auto& [name, value] : project_->autoloads()) {
        bool isScene = value.size() > 6 && value.compare(value.size() - 6, 6, ".scene") == 0;
        if (isScene) {
            std::filesystem::path p(value);
            std::string path = p.is_absolute() ? value : (project_->rootPath() + "/" + value);
            sceneTree_->registerAutoloadScene(name, path);
        } else {
            sceneTree_->registerAutoloadType(name, value);
        }
    }

    // Keep a snapshot to restore the edit doc on Stop, then MOVE the live scene
    // into the World as the current sub-scene — moving (not copying) means a
    // single set of live resources (no duplicate WebCanvas views, audio, etc.).
    playSnapshot_ = SceneSerializer::nodeToJson(*scene_, *resources_);
    std::unique_ptr<Scene> live = std::move(scene_);
    scene_ = std::make_unique<Scene>();  // placeholder while the doc is "in play"

    Scene* world = sceneTree_->mountWorld(std::move(live));
    setSceneOverride(world);
}

void Engine::unmountWorld() {
    if (!sceneTree_->mounted()) return;
    setSceneOverride(nullptr);
    sceneTree_->unmountWorld();  // destroys the World (and the moved/loaded sub-scene)

    // Rebuild the edit document from the snapshot taken at play start.
    if (!playSnapshot_.empty()) {
        if (auto restored = SceneSerializer::nodeFromJson(
                playSnapshot_, *resources_, NodeIdPolicy::Preserve))
            scene_.reset(static_cast<Scene*>(restored.release()));
        playSnapshot_.clear();
    }
}

void Engine::run() {
#ifdef NE_ENABLE_XR
    if (xrMode_) { runXr(); return; }
#endif
    runDesktop();
}

void Engine::setRenderViewport(glm::vec2 position, glm::vec2 size) {
    renderViewportOverride_ = size.x > 1.0f && size.y > 1.0f;
    renderViewportPos_ = position;
    renderViewportSize_ = size;
    if (renderer_) renderer_->setViewportRect(position, size);
}

void Engine::clearRenderViewport() {
    renderViewportOverride_ = false;
    renderViewportPos_ = glm::vec2(0.0f);
    renderViewportSize_ = glm::vec2(0.0f);
    if (renderer_) renderer_->clearViewportRect();
}

void Engine::runDesktop() {
    double last = glfwGetTime();
    while (!window_->shouldClose()) {
        window_->pollEvents();

        int maxFps = project_ ? project_->maxFps() : Project::kDefaultMaxFps;
        if (maxFps > 0) {
            double targetTime = 1.0 / maxFps;
            while (true) {
                double elapsed = glfwGetTime() - last;
                if (elapsed >= targetTime) break;
                
                // If we have more than 2ms to wait, sleep to save CPU.
                // Otherwise, yield/spin for precision.
                if (targetTime - elapsed > 0.002) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } else {
                    std::this_thread::yield();
                }
            }
        }
        
        double now = glfwGetTime();
        float realDt = static_cast<float>(now - last);
        last = now;

        Time::update(realDt);  // sets scaled delta + elapsed
        Input::newFrame();     // single per-frame input snapshot

        static bool wasLeftDown = false;
        bool isLeftDown = Input::isMouseButtonDown(MouseButton::Left);
        bool isLeftPressed = Input::isMouseButtonPressed(MouseButton::Left);
        bool isLeftReleased = !isLeftDown && wasLeftDown;
        wasLeftDown = isLeftDown;

        Scene* activeScene = sceneOverride_ ? sceneOverride_ : scene_.get();

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        window_->framebufferSize(framebufferWidth, framebufferHeight);
        glm::vec2 uiMouse = Input::mousePosition();
        glm::vec2 uiViewportSize{static_cast<float>(framebufferWidth), static_cast<float>(framebufferHeight)};
        if (renderViewportOverride_) {
            uiMouse -= renderViewportPos_;
            uiViewportSize = renderViewportSize_;
        }
        if (uiInteraction_.update(*activeScene, camera_, uiMouse, uiViewportSize,
                                  isLeftDown, isLeftPressed, isLeftReleased)) {
            Input::consumeMouse();
        }

        imgui_->beginFrame();
        if (onFrame_)
            onFrame_(realDt);              // application: its input + UI

        // The application callback may switch Play/Preview mode and destroy the
        // previously active scene. Never carry that borrowed pointer across the
        // callback boundary.
        activeScene = sceneOverride_ ? sceneOverride_ : scene_.get();
        activeScene->update(Time::delta());     // behaviours: scaled time (pausable)

        // During Play, scene cameras drive the view (the editor fly cam is frozen).
        // The director picks the highest-priority active CameraNode and blends; with
        // no camera it returns false and the editor camera stays in control.
        if (sceneTree_->mounted())
            cameraDirector_.update(*activeScene, camera_, Time::delta());

        // Apply deferred gameplay ops (queueFree, changeScene) once behaviours are
        // done — never mutate the tree mid-update. The World object identity is
        // stable across scene swaps, so sceneOverride_ stays valid.
        if (sceneTree_->mounted()) {
            sceneTree_->applyDeferred();
            sceneTree_->tickTimers(Time::delta());  // scaled dt → frozen on pause
            if (sceneTree_->quitRequested()) window_->close();
        }

        AudioManager::get().update();      // update audio spatialization
        imgui_->endFrame();  // finalize draw data even if the frame is skipped (resize)

        renderer_->drawFrame(*activeScene, camera_, project_.get());
    }
    vkDeviceWaitIdle(device_->device());
}

#ifdef NE_ENABLE_XR
void Engine::runXr() {
    // XR loop: paced by xrWaitFrame (inside renderFrame), not GLFW. The window
    // still exists (mirror/host) so we keep pumping it to stay responsive.
    double last = glfwGetTime();
    double lastPoseLog = last;
    while (!window_->shouldClose()) {
        window_->pollEvents();
        if (!xrSession_->pollEvents()) break;   // EXITING / loss pending

        double now = glfwGetTime();
        float realDt = static_cast<float>(now - last);
        last = now;
        Time::update(realDt);
        Input::newFrame();
        // Advance the XR input service (edges). The OpenXR action layer (Étape C)
        // will submitHand() here; until then hands read inactive and the toolkit
        // behaviours stay inert.
        XRInput::beginFrame();
        // Poll OpenXR action sets → hand poses + buttons into XRInput (this is what
        // makes grab/touch/teleport live). Before the scene update so edges are fresh.
        xrSession_->syncActions();
        // Feed the head pose (world space, from last frame's locate) so toolkit
        // behaviours — teleport placement, head-facing UI — can read it.
        {
            XRPose head;
            head.tracked = true;
            head.position = xrSession_->headPosition();
            head.orientation = xrSession_->headOrientation();
            XRInput::submitHead(head);
        }

        Scene* activeScene = sceneOverride_ ? sceneOverride_ : scene_.get();
        activeScene->update(Time::delta());

        if (sceneTree_->mounted()) {
            sceneTree_->applyDeferred();
            sceneTree_->tickTimers(Time::delta());
            if (sceneTree_->quitRequested()) break;
        }
        AudioManager::get().update();

        // Push the player rig (if any) to the session: it recentres the reference
        // space so head/hands/eyes come back world-space this frame (locomotion).
        {
            XROrigin* origin = nullptr;
            activeScene->traverse([&](Node& n, const glm::mat4&) {
                if (!origin)
                    if (auto* o = dynamic_cast<XROrigin*>(&n)) origin = o;
            });
            if (origin)
                xrSession_->setReferenceOffset(origin->transform().position,
                                               glm::radians(origin->yawDegrees));
        }

        // Head pose drives the engine camera (audio listener + the future scene
        // render). Log it once a second so head tracking is observable from logs.
        camera_.position = xrSession_->headPosition();
        if (now - lastPoseLog > 1.0) {
            lastPoseLog = now;
            glm::vec3 p = xrSession_->headPosition();
            Log::info("XR head pos: ", p.x, ", ", p.y, ", ", p.z);
        }

        // Render the scene in stereo (one multiview pass for both eyes) into the
        // acquired XR images. The whole engine pipeline (shadows, GI, HDR, tonemap)
        // is reused — only presentation differs from the desktop path.
        xrSession_->renderFrame(
            [&](VkCommandBuffer cmd, const std::vector<xr::EyeView>& eyes) {
                // Convert xr::EyeView → EyeRenderInfo to keep the Renderer decoupled from XR types.
                std::vector<EyeRenderInfo> eyeInfos;
                eyeInfos.reserve(eyes.size());
                for (const auto& e : eyes)
                    eyeInfos.push_back({e.image, e.imageView, e.extent, e.view, e.projection, e.eyePosition});
                renderer_->drawXr(cmd, eyeInfos, *activeScene, project_.get());
            });
    }
    vkDeviceWaitIdle(device_->device());
}
#endif  // NE_ENABLE_XR

} // namespace ne
