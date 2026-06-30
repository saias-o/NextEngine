// SaidaEngine standalone game runtime.
//
// This is the "export template": a pre-built, editor-less player executable that
// the editor's Build step copies next to the packaged game data. It links only
// saida_engine (no saida_editor / ImGui), resolves all assets relative to its own
// directory (Paths runtime root), reads a tiny boot manifest (game.saida) telling
// it which project + scene to launch, then runs the same engine loop a desktop
// game uses — mirroring the standalone path already used for XR preview in
// src/main.cpp.

#include "Engine.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Time.hpp"
#include "scene/SceneSerializer.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

namespace fs = std::filesystem;

// Directory containing this executable (where the packaged game data lives).
fs::path executableDir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH];
    DWORD n = ::GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n != 0 && n < MAX_PATH)
        return fs::path(std::wstring(buf, buf + n)).parent_path();
#endif
    return fs::current_path();
}

std::string trim(const std::string& s) {
    auto a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    auto b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    try {
        const fs::path root = executableDir();

        // Every asset/shader lookup now resolves under the exe directory.
        saida::setRuntimeRoot(root.string());

        // Boot manifest: key=value text written by the editor's Build step.
        //   project=MyGame.saidaproj
        //   main_scene=scenes/main.scene
        const fs::path manifestPath = root / "game.saida";
        std::ifstream manifest(manifestPath);
        if (!manifest)
            throw std::runtime_error("missing boot manifest: " + manifestPath.string());

        std::string projectFile;
        std::string mainScene;
        std::string line;
        while (std::getline(manifest, line)) {
            line = trim(line);
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(line.substr(0, eq));
            const std::string val = trim(line.substr(eq + 1));
            if (key == "project") projectFile = val;
            else if (key == "main_scene") mainScene = val;
        }
        if (projectFile.empty() || mainScene.empty())
            throw std::runtime_error(
                "invalid boot manifest (need project= and main_scene=): " +
                manifestPath.string());

        const std::string projectAbs = (root / projectFile).lexically_normal().string();
        const std::string sceneAbs   = (root / mainScene).lexically_normal().string();

        // Same standalone path as the XR preview: load project + scene, mount the
        // persistent World (autoloads), run unscaled (Play).
        saida::Engine engine(nullptr, projectAbs, false);
        if (!saida::SceneSerializer::loadIntoScene(
                engine.scene(), engine.resources(), sceneAbs))
            throw std::runtime_error("failed to load main scene: " + sceneAbs);
        engine.mountWorld();
        saida::Time::setScale(1.0f);
        engine.run();
    } catch (const std::exception& e) {
        saida::Log::error(e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
