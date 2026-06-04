#pragma once

#include "editor/EditorUI.hpp"

namespace ne {

class Engine;

// The editor application layer on top of an Engine. Owns the editor UI and the
// editor/viewport input (free-fly camera, pan/zoom, play-mode cursor). Driven
// once per frame via Engine::setOnFrame -> EditorApp::update. Lives in the
// `ne_editor` target, so the engine library stays editor-agnostic.
class EditorApp {
public:
    explicit EditorApp(Engine& engine);

    void update(float dt);  // input + UI; call from the engine's onFrame hook

private:
    void processInput(float dt);
    void updateCursorCapture(bool isPlayMode);

    Engine& engine_;
    EditorUI ui_;
    bool wasPlayMode_ = false;
};

} // namespace ne
