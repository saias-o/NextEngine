#include "Engine.hpp"
#include "core/Log.hpp"
#include "editor/EditorApp.hpp"
#include "core/Time.hpp"
#include "runtime/TestAutoload.hpp"
#include "scene/SceneSerializer.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    try {
        std::string initialProject;
        std::string runtimeScene;
        std::string buildOutputDir;
        bool xrPreview = false;
        bool readXrPreviewManifest = false;
        bool buildRequested = false;
        bool buildWeb = false;
        bool playRequested = false;
        std::vector<std::string> testAutoloads;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--project" && i + 1 < argc)
                initialProject = argv[++i];
            else if (arg == "--scene" && i + 1 < argc)
                runtimeScene = argv[++i];
            else if (arg == "--build" && i + 1 < argc) {
                buildRequested = true;
                buildOutputDir = argv[++i];
            } else if (arg == "--build-platform" && i + 1 < argc)
                buildWeb = (std::string(argv[++i]) == "web");
            else if (arg == "--play")
                playRequested = true;
            else if (arg == "--test-autoload" && i + 1 < argc)
                testAutoloads.emplace_back(argv[++i]);
            else if (arg == "--xr")
                xrPreview = true;
            else if (arg == "--xr-preview") {
                xrPreview = true;
                readXrPreviewManifest = true;
            }
        }

        if (readXrPreviewManifest) {
            const std::filesystem::path manifestPath =
                std::filesystem::path(SAIDA_BINARY_DIR) / "xr_preview.launch";
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
        saida::Engine engine(nullptr, initialProject, xrPreview);
        for (const std::string& spec : testAutoloads) {
            std::string error;
            if (!saida::runtime::applyTestAutoload(engine.project(), spec, error))
                throw std::runtime_error(error);
        }
        if (xrPreview) {
            if (runtimeScene.empty())
                throw std::runtime_error("--xr requires --scene <path>");
            const std::string scenePath =
                std::filesystem::absolute(runtimeScene).lexically_normal().string();
            if (!saida::SceneSerializer::loadIntoScene(
                    engine.scene(), engine.resources(), scenePath))
                throw std::runtime_error("failed to load XR preview scene: " + scenePath);
            engine.mountWorld();
            saida::Time::setScale(1.0f);
            engine.run();
            return EXIT_SUCCESS;
        }

        // Normal desktop editor.
        saida::EditorApp editor(engine);

        // --build <out> : clic Build automatisé — même code que le bouton du
        // dialogue Build, puis sortie immédiate avec le verdict en code retour.
        if (buildRequested) return editor.runAutomatedBuild(buildWeb, buildOutputDir);

        // --play : ouvre le projet puis déclenche le même passage en Play que le
        // bouton de l'éditeur. Utile aux recettes automatisées avec un pilote de
        // jeu qui termine lui-même le processus via tree.quit().
        if (playRequested) editor.setPlayMode(true);

        engine.setOnFrame([&editor](float dt) { editor.update(dt); });
        engine.run();
    } catch (const std::exception& e) {
        saida::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
