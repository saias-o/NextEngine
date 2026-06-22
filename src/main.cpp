#include "Engine.hpp"
#include "core/Log.hpp"
#include "editor/EditorApp.hpp"
#include "core/Time.hpp"
#include "scene/SceneSerializer.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
    try {
        std::string initialProject;
        std::string runtimeScene;
        bool xrPreview = false;
        bool readXrPreviewManifest = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--project" && i + 1 < argc)
                initialProject = argv[++i];
            else if (arg == "--scene" && i + 1 < argc)
                runtimeScene = argv[++i];
            else if (arg == "--xr")
                xrPreview = true;
            else if (arg == "--xr-preview") {
                xrPreview = true;
                readXrPreviewManifest = true;
            }
        }

        if (readXrPreviewManifest) {
            const std::filesystem::path manifestPath =
                std::filesystem::path(NE_BINARY_DIR) / "xr_preview.launch";
            std::ifstream manifest(manifestPath);
            if (!manifest || !std::getline(manifest, initialProject) ||
                !std::getline(manifest, runtimeScene) || initialProject.empty() ||
                runtimeScene.empty())
                throw std::runtime_error(
                    "invalid XR preview launch manifest: " + manifestPath.string());
        }

        // XR Preview is a standalone child process launched by the desktop editor.
        // It must create its Vulkan device through OpenXR from process startup;
        // switching the editor's existing desktop device in place is not valid.
        //
        // No SceneSetup: launched directly (no --project), the editor opens on an
        // empty scene. The Hub passes a project via --project to load real content.
        ne::Engine engine(nullptr, initialProject, xrPreview);
        if (xrPreview) {
            if (runtimeScene.empty())
                throw std::runtime_error("--xr requires --scene <path>");
            const std::string scenePath =
                std::filesystem::absolute(runtimeScene).lexically_normal().string();
            if (!ne::SceneSerializer::loadIntoScene(
                    engine.scene(), engine.resources(), scenePath))
                throw std::runtime_error("failed to load XR preview scene: " + scenePath);
            engine.mountWorld();
            ne::Time::setScale(1.0f);
            engine.run();
            return EXIT_SUCCESS;
        }

        // Normal desktop editor.
        ne::EditorApp editor(engine);
        engine.setOnFrame([&editor](float dt) { editor.update(dt); });
        engine.run();
    } catch (const std::exception& e) {
        ne::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
