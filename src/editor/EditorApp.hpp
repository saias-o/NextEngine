#pragma once

#include "editor/EditorUI.hpp"

namespace saida {

class Engine;

// The editor application layer on top of an Engine. Owns the editor UI and the
// editor/viewport input (free-fly camera, pan/zoom, play-mode cursor). Driven
// once per frame via Engine::setOnFrame -> EditorApp::update. Lives in the
// `saida_editor` target, so the engine library stays editor-agnostic.
class EditorApp {
public:
    explicit EditorApp(Engine& engine);

    void update(float dt);  // input + UI; call from the engine's onFrame hook

    // Clic Build automatisé (flag --build) : exécute le même code que le bouton
    // du dialogue Build sur le projet chargé, logge [BUILD] PASS/FAIL et
    // retourne le code de sortie du process. Aucune frame UI nécessaire.
    int runAutomatedBuild(bool web, const std::string& outputDir);

    bool isPlayMode() const { return playMode_; }
    // Requests a Play/Stop transition. The actual world mount/unmount is DEFERRED
    // to the end of update(): tearing down the world mid-UI-frame would free the
    // Scene that the panels (hierarchy/inspector/gizmo) are still drawing this
    // frame — a use-after-free. See applyPlayMode().
    void setPlayMode(bool play);

private:
    void processInput(float dt);
    void updateCursorCapture(bool isPlayMode);
    void applyPlayMode(bool play);  // does the real mount/unmount (deferred)

    Engine& engine_;
    EditorUI ui_;
    bool playMode_ = false;
    bool wasPlayMode_ = false;
    int pendingPlayMode_ = -1;  // -1 none, 0 stop, 1 play (applied at end of update)
    float flySpeed_ = 6.0f;     // editor fly speed (units/s); scroll wheel adjusts it
};

} // namespace saida
