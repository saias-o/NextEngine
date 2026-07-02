#pragma once

#include <string>

namespace saida {

class Project;

// Packages a loaded project into a standalone Windows game folder, Godot/Unity
// "export template" style: it copies the pre-built editor-less runtime
// (SaidaEngineRuntime.exe) plus the compiled shaders and the project data next to
// it, and writes a boot manifest (game.saida) the runtime reads at startup. No
// recompilation — the runtime template is built once with the engine.
class BuildExporter {
public:
    struct Options {
        std::string outputDir;            // absolute, or relative to project root
        std::string mainScene = "scenes/main.scene";  // project-relative
        bool launchAfterBuild = false;    // "Build & Run"
    };

    struct Result {
        bool success = false;
        std::string error;       // populated when success == false
        std::string log;         // human-readable step log (always populated)
        std::string outputDir;   // absolute output directory
        std::string gameExe;     // absolute path of the produced <Game>.exe
    };

    // Performs the export. Pure filesystem work; safe to call from the UI thread.
    static Result exportWindowsBuild(const Project& project, const Options& options);

    // Packages the web runtime (Étape 16.6): copies the emcmake output
    // (build-web/index.html/.js/.wasm/.data — produced by web/build_web.sh)
    // into <outputDir>/web next to the COOP/COEP dev server (web/serve.py).
    // Fails with a pointer at build_web.sh when the web build is missing.
    static Result exportWebBuild(const Project& project, const Options& options);

    // Launches an already-built game exe (used by "Build & Run" / "Open").
    static bool launch(const std::string& exePath);

    // Opens a folder in the OS file explorer (used by the "Open Folder" button).
    static void openInExplorer(const std::string& dir);
};

} // namespace saida
