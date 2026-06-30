# Plan : RmlUi + QuickJS — UI HTML/CSS/JS + Scripting Global

> **Contexte** : Le moteur SaidaEngine utilise actuellement **Ultralight** (propriétaire, closed-source, ~18 Mo)
> pour rendre du HTML/CSS/JS dans des textures Vulkan. Cela contredit la philosophie du moteur
> (libre, open-source, léger, LLM-native). Par ailleurs, l'Étape 8b prévoyait Lua pour le scripting
> de gameplay — on le remplace par **JavaScript via QuickJS**, langage bien plus LLM-friendly,
> et dont le runtime est déjà nécessaire pour l'UI.
>
> **Objectif double** :
> 1. **UI** : Remplacer Ultralight par **RmlUi** (MIT) + **QuickJS** (MIT) pour rendre du HTML/CSS/JS
>    natif en texture Vulkan (screen-space et world-space VR/XR).
> 2. **Scripting global** : Utiliser QuickJS comme moteur de scripting universel du moteur —
>    `ScriptBehaviour` (équivalent JS des `Behaviour` C++), hot-reload, bindings moteur complets.
>    Un seul runtime, un seul langage, pour tout : UI + gameplay + autoloads + outils.
>
> **Contraintes** :
> - Build avec **MSYS2/MinGW GCC** + **CMake/Ninja**. Linkage statique (`-static`).
> - Tout doit être vendable dans `third_party/` et compiler sans dépendance externe.
> - Respecter le contrat d'architecture existant : nœuds + behaviours + signaux, pas de singletons de gameplay.
> - Les 5 règles du CLAUDE.md s'appliquent aux scripts JS exactement comme au C++.

---

## Table des matières

1. [Phase 0 — Vendoring des dépendances](#phase-0--vendoring-des-dépendances)
2. [Phase 1 — JSEngine : le runtime QuickJS central](#phase-1--jsengine--le-runtime-quickjs-central)
3. [Phase 2 — Bindings moteur C++ ↔ JS](#phase-2--bindings-moteur-c--js)
4. [Phase 3 — ScriptBehaviour : behaviours en JS](#phase-3--scriptbehaviour--behaviours-en-js)
5. [Phase 4 — Backend de rendu RmlUi → Vulkan](#phase-4--backend-de-rendu-rmlui--vulkan)
6. [Phase 5 — Nouveau WebCanvasNode (RmlUi + JS)](#phase-5--nouveau-webcanvasnode-rmlui--js)
7. [Phase 6 — Hot-reload des scripts](#phase-6--hot-reload-des-scripts)
8. [Phase 7 — Nettoyage : Supprimer Ultralight](#phase-7--nettoyage--supprimer-ultralight)
9. [Phase 8 — Mises à jour documentation](#phase-8--mises-à-jour-documentation)
10. [Ordre d'exécution et estimation](#ordre-dexécution-et-estimation)
11. [Risques et points d'attention](#risques-et-points-dattention)

---

## Phase 0 — Vendoring des dépendances

### 0.1 — Vendre QuickJS

- **Repo** : https://github.com/nicbarker/quickjs-ng (fork actif, meilleur support Windows/MinGW)
  ou https://bellard.org/quickjs/ (original Bellard).
- **Recommandation** : **quickjs-ng** (plus maintenu, meilleur MinGW).
- **Destination** : `third_party/quickjs/`
- **Quoi copier** : Les fichiers source C principaux :
  - `quickjs.c`, `quickjs.h`, `quickjs-libc.c`, `quickjs-libc.h`
  - `cutils.c`, `cutils.h`, `libbf.c`, `libbf.h`
  - `libregexp.c`, `libregexp.h`, `libunicode.c`, `libunicode.h`
  - `list.h`, `quickjs-atom.h`, `quickjs-opcode.h`
- **CMake** :
  ```cmake
  file(GLOB QUICKJS_SOURCES third_party/quickjs/*.c)
  add_library(quickjs STATIC ${QUICKJS_SOURCES})
  target_include_directories(quickjs PUBLIC third_party/quickjs)
  # QuickJS a besoin de math
  target_link_libraries(quickjs PRIVATE m)
  # Sous MinGW, peut nécessiter :
  target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)
  ```
- **Validation** : compiler le projet, vérifier que `quickjs` linke sans erreur.
- **Taille** : ~300 Ko de source C. ES2023 complet (async/await, modules, Promises, etc.).

### 0.2 — Vendre RmlUi

- **Repo** : https://github.com/mikke89/RmlUi (branche `master`, dernière release stable)
- **Destination** : `third_party/rmlui/`
- **Quoi copier** : Le repo entier ou au minimum `Source/` et `Include/`.
- **CMake** :
  ```cmake
  set(RMLUI_BACKEND "" CACHE STRING "" FORCE)           # Pas de backend (on fournit le nôtre)
  set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
  set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
  set(RMLUI_THIRDPARTY_CONTAINERS ON CACHE BOOL "" FORCE)
  set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)       # On utilise QuickJS, pas Lua
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)         # Lib statique
  add_subdirectory(third_party/rmlui)
  target_link_libraries(saida_engine PUBLIC rmlui)
  ```
- **Dépendance freetype** : RmlUi a besoin de **freetype** pour le rendu texte.
  - Vendre freetype dans `third_party/freetype/`
  - Source : https://github.com/freetype/freetype (licence FTL, BSD-like)
  - CMake : `add_subdirectory(third_party/freetype)` — RmlUi le détecte automatiquement.

### 0.3 — Vendre une police par défaut

- Copier une police TTF open-source dans `assets/fonts/` (ex: **Inter**, **Roboto**, ou **Noto Sans**).
- RmlUi + freetype en ont besoin pour rasteriser le texte.
- Licence : toutes sont SIL Open Font License (libre, redistribuable).

### 0.4 — Validation Phase 0

```bash
cmake -S . -B build -G Ninja
cmake --build build
```
Le projet doit compiler avec les 3 nouvelles libs (quickjs, rmlui, freetype) sans erreur.
Pas besoin qu'elles fassent quoi que ce soit encore — juste qu'elles linkent.

---

## Phase 1 — JSEngine : le runtime QuickJS central

**Fichiers à créer** : `src/scripting/JSEngine.hpp` / `src/scripting/JSEngine.cpp`

Le JSEngine est le **singleton central** qui gère le runtime QuickJS. Tout le moteur passe par lui
pour exécuter du JS — que ce soit les `ScriptBehaviour`, les scripts UI, ou les outils.

### 1.1 — Interface

```cpp
// src/scripting/JSEngine.hpp
#pragma once

#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

// Forward-declare QuickJS types (pas d'include du gros header dans le .hpp)
struct JSRuntime;
struct JSContext;
typedef struct JSValue JSValue; // ou uint64_t selon la version

namespace saida {

class Node;
class Behaviour;

class JSEngine {
public:
    static JSEngine& get();

    // Créer/détruire un contexte JS isolé (un par ScriptBehaviour ou par WebCanvasNode)
    JSContext* createContext();
    void destroyContext(JSContext* ctx);

    // Exécuter du JS dans un contexte donné
    // Retourne true si pas d'exception. Log l'erreur sinon.
    bool eval(JSContext* ctx, const std::string& script, const std::string& filename = "<eval>");

    // Charger et exécuter un fichier .js
    bool evalFile(JSContext* ctx, const std::string& filepath);

    // Installer les bindings moteur standards dans un contexte
    // (appelé automatiquement à la création du contexte)
    void installBindings(JSContext* ctx);

    // Associer un Node* à un contexte (pour que les bindings sachent sur quel node ils agissent)
    void setContextNode(JSContext* ctx, Node* node);
    Node* getContextNode(JSContext* ctx);

    // Tick : exécuter les jobs pending (Promises, async/await)
    void executePendingJobs();

private:
    JSEngine();
    ~JSEngine();

    JSRuntime* runtime_ = nullptr;
    std::unordered_map<JSContext*, Node*> contextNodes_;  // ctx → node associé
};

} // namespace saida
```

### 1.2 — Implémentation

```cpp
// src/scripting/JSEngine.cpp
#include "scripting/JSEngine.hpp"
#include "core/Log.hpp"
#include <quickjs.h>
#include <quickjs-libc.h>
#include <fstream>
#include <sstream>

namespace saida {

JSEngine& JSEngine::get() {
    static JSEngine instance;
    return instance;
}

JSEngine::JSEngine() {
    runtime_ = JS_NewRuntime();
    // Limite mémoire raisonnable (64 Mo pour le scripting)
    JS_SetMemoryLimit(runtime_, 64 * 1024 * 1024);
    // Limite de stack
    JS_SetMaxStackSize(runtime_, 1024 * 1024);
    Log::info("JSEngine (QuickJS) initialisé.");
}

JSEngine::~JSEngine() {
    JS_FreeRuntime(runtime_);
    Log::info("JSEngine (QuickJS) détruit.");
}

JSContext* JSEngine::createContext() {
    JSContext* ctx = JS_NewContext(runtime_);
    // Installer les modules standard (console, etc.)
    js_std_add_helpers(ctx, 0, nullptr);
    // Installer les bindings moteur
    installBindings(ctx);
    return ctx;
}

void JSEngine::destroyContext(JSContext* ctx) {
    contextNodes_.erase(ctx);
    JS_FreeContext(ctx);
}

bool JSEngine::eval(JSContext* ctx, const std::string& script, const std::string& filename) {
    JSValue val = JS_Eval(ctx, script.c_str(), script.size(), filename.c_str(), 0);
    if (JS_IsException(val)) {
        JSValue exc = JS_GetException(ctx);
        const char* str = JS_ToCString(ctx, exc);
        Log::error("[JS] {}: {}", filename, str ? str : "unknown error");
        JS_FreeCString(ctx, str);
        // Afficher la stack trace si disponible
        if (JS_IsObject(exc)) {
            JSValue stack = JS_GetPropertyStr(ctx, exc, "stack");
            if (!JS_IsUndefined(stack)) {
                const char* stackStr = JS_ToCString(ctx, stack);
                if (stackStr) { Log::error("[JS] Stack:\n{}", stackStr); JS_FreeCString(ctx, stackStr); }
            }
            JS_FreeValue(ctx, stack);
        }
        JS_FreeValue(ctx, exc);
        JS_FreeValue(ctx, val);
        return false;
    }
    JS_FreeValue(ctx, val);
    return true;
}

bool JSEngine::evalFile(JSContext* ctx, const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        Log::error("[JS] Cannot open file: {}", filepath);
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return eval(ctx, ss.str(), filepath);
}

void JSEngine::executePendingJobs() {
    JSContext* pendingCtx;
    while (JS_ExecutePendingJob(runtime_, &pendingCtx) > 0) {
        // Continue processing
    }
}

void JSEngine::setContextNode(JSContext* ctx, Node* node) {
    contextNodes_[ctx] = node;
}

Node* JSEngine::getContextNode(JSContext* ctx) {
    auto it = contextNodes_.find(ctx);
    return it != contextNodes_.end() ? it->second : nullptr;
}

} // namespace saida
```

### 1.3 — Intégration dans Engine::run

Dans `Engine.cpp`, ajouter dans la boucle principale (après l'update de la scène) :

```cpp
// Exécuter les Promises/async en attente
JSEngine::get().executePendingJobs();
```

Ceci remplace le `WebEngine::get().update()` d'Ultralight.

---

## Phase 2 — Bindings moteur C++ ↔ JS

**Fichiers à créer** : `src/scripting/JSBindings.hpp` / `src/scripting/JSBindings.cpp`

C'est ici qu'on expose l'API moteur au JavaScript. C'est le fichier le plus important pour les LLMs
car c'est ce qui définit ce qu'un script JS peut faire dans le moteur.

### 2.1 — Architecture des bindings

Les bindings sont installés par `JSEngine::installBindings(ctx)` dans chaque contexte.
Ils créent un objet global `ne` avec des sous-objets :

```javascript
// Ce que voit un script JS :
ne.log("hello");                              // → saida::Log::info()
ne.warn("oops");                              // → saida::Log::warn()
ne.error("bad");                              // → saida::Log::error()

// --- Time ---
ne.time.delta                                 // → saida::Time::delta()
ne.time.elapsed                               // → saida::Time::elapsed()
ne.time.scale                                 // → saida::Time::scale()  (getter)
ne.time.scale = 0.5                           // → saida::Time::setScale() (setter)

// --- Input ---
ne.input.isActionHeld("jump")                 // → saida::Input::isActionHeld()
ne.input.isActionJustPressed("fire")          // → saida::Input::isActionJustPressed()
ne.input.getAxis("move_left", "move_right")   // → saida::Input::getAxis()
ne.input.getVector("left","right","down","up") // → saida::Input::getVector()  [retourne {x,y}]
ne.input.mouseDelta                           // → saida::Input::mouseDelta() [retourne {x,y}]
ne.input.mousePosition                        // → saida::Input::mousePosition()
ne.input.isKeyDown("Space")                   // → saida::Input::isKeyDown(KeyCode::Space)
ne.input.bindKey("jump", "Space")             // → saida::Input::bindKey()

// --- Node (self) — disponible dans un ScriptBehaviour ---
self.name                                     // → node()->name()
self.position                                 // → node()->transform().position  {x,y,z}
self.position = {x: 1, y: 2, z: 3}           // → setter
self.rotation                                 // → node()->transform().rotation  {x,y,z,w}
self.scale                                    // → node()->transform().scale     {x,y,z}
self.worldTransform                           // → node()->worldTransform()      (mat4 en array 16)
self.enabled                                  // → node()->enabled()
self.enabled = false                          // → node()->setEnabled()
self.parent                                   // → handle vers le parent
self.children                                 // → array de handles enfants

self.getChild("name")                         // → cherche un enfant par nom
self.addToGroup("enemies")                    // → node()->addToGroup()
self.removeFromGroup("enemies")               // → node()->removeFromGroup()
self.queueFree()                              // → node()->queueFree()

// --- SceneTree ---
ne.tree.changeScene("scenes/level2.scene")    // → tree()->changeScene()
ne.tree.instantiate("scenes/bullet.scene", self) // → tree()->instantiate()
ne.tree.firstInGroup("player")                // → tree()->firstInGroup() → handle
ne.tree.autoload("GameState")                 // → tree()->autoloadNode() → handle
ne.tree.paused                                // → tree()->paused()
ne.tree.quit()                                // → tree()->quit()

// --- Timers/Tweens (scoped au node, annulés à sa mort) ---
self.wait(2.0, () => { ... })                 // → behaviour->wait()
self.every(0.5, () => { ... })                // → behaviour->every()
self.tween(1.0, "easeInOut", (t) => { ... })  // → behaviour->tween()

// --- Audio ---
ne.audio.play("sounds/click.wav")             // → AudioManager::play()
ne.audio.setVolume(0.8)                       // → AudioManager::setMasterVolume()

// --- Signals (le JS peut écouter et émettre) ---
self.listen("collision_entered", (other) => { ... })  // → écoute un signal du node
self.emit("custom_signal", data)                      // → émet un signal custom
```

### 2.2 — Implémentation des bindings

```cpp
// src/scripting/JSBindings.cpp

#include "scripting/JSBindings.hpp"
#include "scripting/JSEngine.hpp"
#include "core/Log.hpp"
#include "core/Time.hpp"
#include "core/Input.hpp"
#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"
#include "audio/AudioManager.hpp"
#include <quickjs.h>

namespace saida {

// ========================================================================
// Helpers pour convertir glm <-> JS
// ========================================================================

// vec3 → JS object {x, y, z}
static JSValue vec3ToJS(JSContext* ctx, const glm::vec3& v) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, v.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, v.y));
    JS_SetPropertyStr(ctx, obj, "z", JS_NewFloat64(ctx, v.z));
    return obj;
}

// JS object {x, y, z} → vec3
static glm::vec3 jsToVec3(JSContext* ctx, JSValue val) {
    glm::vec3 v;
    JSValue x = JS_GetPropertyStr(ctx, val, "x");
    JSValue y = JS_GetPropertyStr(ctx, val, "y");
    JSValue z = JS_GetPropertyStr(ctx, val, "z");
    double dx, dy, dz;
    JS_ToFloat64(ctx, &dx, x); v.x = (float)dx;
    JS_ToFloat64(ctx, &dy, y); v.y = (float)dy;
    JS_ToFloat64(ctx, &dz, z); v.z = (float)dz;
    JS_FreeValue(ctx, x); JS_FreeValue(ctx, y); JS_FreeValue(ctx, z);
    return v;
}

// ========================================================================
// ne.log / ne.warn / ne.error
// ========================================================================

static JSValue js_ne_log(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    const char* str = JS_ToCString(ctx, argv[0]);
    Log::info("[JS] {}", str ? str : "");
    JS_FreeCString(ctx, str);
    return JS_UNDEFINED;
}

// ... (même pattern pour warn, error)

// ========================================================================
// ne.time
// ========================================================================

static JSValue js_time_get_delta(JSContext* ctx, JSValue this_val) {
    return JS_NewFloat64(ctx, Time::delta());
}

static JSValue js_time_get_elapsed(JSContext* ctx, JSValue this_val) {
    return JS_NewFloat64(ctx, Time::elapsed());
}

static JSValue js_time_get_scale(JSContext* ctx, JSValue this_val) {
    return JS_NewFloat64(ctx, Time::scale());
}

static JSValue js_time_set_scale(JSContext* ctx, JSValue this_val, JSValue val) {
    double s; JS_ToFloat64(ctx, &s, val);
    Time::setScale((float)s);
    return JS_UNDEFINED;
}

// ========================================================================
// ne.input
// ========================================================================

static JSValue js_input_isActionHeld(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    const char* action = JS_ToCString(ctx, argv[0]);
    bool result = Input::isActionHeld(action);
    JS_FreeCString(ctx, action);
    return JS_NewBool(ctx, result);
}

static JSValue js_input_isActionJustPressed(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    const char* action = JS_ToCString(ctx, argv[0]);
    bool result = Input::isActionJustPressed(action);
    JS_FreeCString(ctx, action);
    return JS_NewBool(ctx, result);
}

static JSValue js_input_getAxis(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    const char* neg = JS_ToCString(ctx, argv[0]);
    const char* pos = JS_ToCString(ctx, argv[1]);
    float result = Input::getAxis(neg, pos);
    JS_FreeCString(ctx, neg); JS_FreeCString(ctx, pos);
    return JS_NewFloat64(ctx, result);
}

// ... (même pattern pour les autres fonctions Input)

// ========================================================================
// self.* (node context — utilise JSEngine::getContextNode)
// ========================================================================

static Node* getSelfNode(JSContext* ctx) {
    return JSEngine::get().getContextNode(ctx);
}

static JSValue js_self_get_name(JSContext* ctx, JSValue this_val) {
    Node* n = getSelfNode(ctx);
    return n ? JS_NewString(ctx, n->name().c_str()) : JS_UNDEFINED;
}

static JSValue js_self_get_position(JSContext* ctx, JSValue this_val) {
    Node* n = getSelfNode(ctx);
    return n ? vec3ToJS(ctx, n->transform().position) : JS_UNDEFINED;
}

static JSValue js_self_set_position(JSContext* ctx, JSValue this_val, JSValue val) {
    Node* n = getSelfNode(ctx);
    if (n) n->transform().position = jsToVec3(ctx, val);
    return JS_UNDEFINED;
}

static JSValue js_self_queueFree(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    Node* n = getSelfNode(ctx);
    if (n) n->queueFree();
    return JS_UNDEFINED;
}

// ========================================================================
// ne.tree.*
// ========================================================================

static JSValue js_tree_changeScene(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    Node* n = getSelfNode(ctx);
    if (!n || !n->tree()) return JS_UNDEFINED;
    const char* path = JS_ToCString(ctx, argv[0]);
    n->tree()->changeScene(path);
    JS_FreeCString(ctx, path);
    return JS_UNDEFINED;
}

static JSValue js_tree_quit(JSContext* ctx, JSValue this_val, int argc, JSValue* argv) {
    Node* n = getSelfNode(ctx);
    if (n && n->tree()) n->tree()->quit();
    return JS_UNDEFINED;
}

// ========================================================================
// Installation globale
// ========================================================================

void installJSBindings(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);

    // --- ne (namespace principal) ---
    JSValue ne = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, ne, "log", JS_NewCFunction(ctx, js_ne_log, "log", 1));
    // ... warn, error

    // --- ne.time ---
    JSValue time = JS_NewObject(ctx);
    // Utiliser JS_DefinePropertyGetSet pour les getters/setters
    JSAtom deltaAtom = JS_NewAtom(ctx, "delta");
    JS_DefinePropertyGetSet(ctx, time, deltaAtom,
        JS_NewCFunction(ctx, (JSCFunction*)js_time_get_delta, "get delta", 0),
        JS_UNDEFINED, 0);
    JS_FreeAtom(ctx, deltaAtom);
    // ... elapsed, scale (getter+setter)
    JS_SetPropertyStr(ctx, ne, "time", time);

    // --- ne.input ---
    JSValue input = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, input, "isActionHeld", JS_NewCFunction(ctx, js_input_isActionHeld, "isActionHeld", 1));
    JS_SetPropertyStr(ctx, input, "isActionJustPressed", JS_NewCFunction(ctx, js_input_isActionJustPressed, "isActionJustPressed", 1));
    JS_SetPropertyStr(ctx, input, "getAxis", JS_NewCFunction(ctx, js_input_getAxis, "getAxis", 2));
    // ... autres fonctions Input
    JS_SetPropertyStr(ctx, ne, "input", input);

    // --- ne.tree ---
    JSValue tree = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, tree, "changeScene", JS_NewCFunction(ctx, js_tree_changeScene, "changeScene", 1));
    JS_SetPropertyStr(ctx, tree, "quit", JS_NewCFunction(ctx, js_tree_quit, "quit", 0));
    // ... instantiate, firstInGroup, autoload, paused
    JS_SetPropertyStr(ctx, ne, "tree", tree);

    JS_SetPropertyStr(ctx, global, "ne", ne);

    // --- self (node context) ---
    JSValue self = JS_NewObject(ctx);
    // Position getter/setter
    JSAtom posAtom = JS_NewAtom(ctx, "position");
    JS_DefinePropertyGetSet(ctx, self, posAtom,
        JS_NewCFunction(ctx, (JSCFunction*)js_self_get_position, "get position", 0),
        JS_NewCFunction(ctx, (JSCFunction*)js_self_set_position, "set position", 1),
        0);
    JS_FreeAtom(ctx, posAtom);
    // ... name, rotation, scale, enabled, parent, children
    JS_SetPropertyStr(ctx, self, "queueFree", JS_NewCFunction(ctx, js_self_queueFree, "queueFree", 0));
    // ... wait, every, tween, addToGroup, removeFromGroup, listen, emit
    JS_SetPropertyStr(ctx, global, "self", self);

    JS_FreeValue(ctx, global);
}

} // namespace saida
```

### 2.3 — Bindings à implémenter (liste complète)

| Namespace JS | Fonctions / Props | Mappe vers C++ |
|---|---|---|
| `ne.log(msg)` | fonction | `Log::info()` |
| `ne.warn(msg)` | fonction | `Log::warn()` |
| `ne.error(msg)` | fonction | `Log::error()` |
| `ne.time.delta` | getter | `Time::delta()` |
| `ne.time.elapsed` | getter | `Time::elapsed()` |
| `ne.time.scale` | getter/setter | `Time::scale()` / `Time::setScale()` |
| `ne.input.isActionHeld(action)` | fonction | `Input::isActionHeld()` |
| `ne.input.isActionJustPressed(action)` | fonction | `Input::isActionJustPressed()` |
| `ne.input.isActionJustReleased(action)` | fonction | `Input::isActionJustReleased()` |
| `ne.input.getActionStrength(action)` | fonction | `Input::getActionStrength()` |
| `ne.input.getAxis(neg, pos)` | fonction | `Input::getAxis()` |
| `ne.input.getVector(l,r,d,u)` | fonction → `{x,y}` | `Input::getVector()` |
| `ne.input.mouseDelta` | getter → `{x,y}` | `Input::mouseDelta()` |
| `ne.input.mousePosition` | getter → `{x,y}` | `Input::mousePosition()` |
| `ne.input.isKeyDown(key)` | fonction | `Input::isKeyDown()` |
| `ne.input.bindKey(action, key)` | fonction | `Input::bindKey()` |
| `ne.tree.changeScene(path)` | fonction | `SceneTree::changeScene()` |
| `ne.tree.instantiate(path, parent?)` | fonction → handle | `SceneTree::instantiate()` |
| `ne.tree.firstInGroup(name)` | fonction → handle | `SceneTree::firstInGroup()` |
| `ne.tree.autoload(name)` | fonction → handle | `SceneTree::autoloadNode()` |
| `ne.tree.paused` | getter/setter | `SceneTree::paused()` / `setPaused()` |
| `ne.tree.quit()` | fonction | `SceneTree::quit()` |
| `ne.audio.play(path)` | fonction | `AudioManager::play()` |
| `ne.audio.setVolume(v)` | fonction | `AudioManager::setMasterVolume()` |
| `self.name` | getter | `node()->name()` |
| `self.position` | getter/setter → `{x,y,z}` | `Transform::position` |
| `self.rotation` | getter/setter → `{x,y,z,w}` | `Transform::rotation` |
| `self.scale` | getter/setter → `{x,y,z}` | `Transform::scale` |
| `self.enabled` | getter/setter | `node()->enabled()` / `setEnabled()` |
| `self.parent` | getter → handle | `node()->parent()` |
| `self.children` | getter → array handles | `node()->children()` |
| `self.getChild(name)` | fonction → handle | cherche par nom dans children |
| `self.addToGroup(name)` | fonction | `node()->addToGroup()` |
| `self.removeFromGroup(name)` | fonction | `node()->removeFromGroup()` |
| `self.queueFree()` | fonction | `node()->queueFree()` |
| `self.wait(sec, fn)` | fonction | `Behaviour::wait()` |
| `self.every(sec, fn)` | fonction | `Behaviour::every()` |
| `self.tween(dur, easing, fn)` | fonction | `Behaviour::tween()` |

### 2.4 — Handles de nœuds

Quand le JS reçoit un nœud (ex: `ne.tree.firstInGroup("player")`), on ne peut pas lui passer
un `Node*` brut. Il faut un **handle opaque** qui reste sûr même si le nœud est détruit.

**Solution** : Utiliser un `JSValue` opaque wrappant un `Node*`, avec des getters/setters
similaires à `self` mais sur l'objet handle. Le handle vérifie la validité du pointeur
avant chaque accès (via un registre de nœuds vivants, ou un weak ref).

**Implémentation minimale v1** : Pour la v1, on peut simplement passer le `Node*` en `JS_NewInt64`
(opaque pointer cast), et vérifier dans chaque binding que le nœud est toujours vivant.
C'est pragmatique — on ajoutera un registre de handles plus tard si nécessaire.

---

## Phase 3 — ScriptBehaviour : behaviours en JS

**Fichiers à créer** : `src/scripting/ScriptBehaviour.hpp` / `src/scripting/ScriptBehaviour.cpp`

C'est la pièce qui remplace l'Étape 8b (Lua). Un `ScriptBehaviour` est un `Behaviour` C++ qui
délègue `onReady`, `onUpdate`, `onDestroy` etc. à des fonctions JS.

### 3.1 — Interface

```cpp
// src/scripting/ScriptBehaviour.hpp
#pragma once

#include "scene/Behaviour.hpp"
#include <string>

struct JSContext;
typedef uint64_t JSValue;  // ou la bonne typedef selon quickjs-ng

namespace saida {

// A Behaviour whose logic lives in a .js file. Delegates onReady/onUpdate/etc.
// to JS functions defined in the script. Hot-reloadable: when the file changes,
// the context is torn down and rebuilt.
//
// Usage in editor: attach to any node, set scriptPath to "scripts/my_logic.js".
// The script must export functions: onReady(), onUpdate(dt), onDestroy() (all optional).
//
// Example script:
//   function onReady() {
//       ne.log("Hello from " + self.name);
//       ne.input.bindKey("jump", "Space");
//   }
//   function onUpdate(dt) {
//       if (ne.input.isActionJustPressed("jump")) {
//           self.position = {
//               x: self.position.x,
//               y: self.position.y + 5 * dt,
//               z: self.position.z
//           };
//       }
//   }
class ScriptBehaviour : public Behaviour {
public:
    ScriptBehaviour() = default;
    ~ScriptBehaviour() override;

    void onReady() override;
    void onUpdate(float dt) override;
    void onDestroy() override;
    void onEnable() override;
    void onDisable() override;
    void onDrawInspector() override;  // Affiche le champ scriptPath dans l'éditeur

    const char* typeName() const override { return "ScriptBehaviour"; }
    void save(nlohmann::json& j) const override;
    void load(const nlohmann::json& j) override;

    const std::string& scriptPath() const { return scriptPath_; }
    void setScriptPath(const std::string& path);

    // Recharge le script (appelé par le hot-reload ou manuellement)
    void reload();

    // Obtenir le contexte JS (pour les bindings avancés ou le bridge UI)
    JSContext* jsContext() const { return ctx_; }

private:
    void initContext();
    void teardownContext();
    bool callJSFunction(const char* name);               // appel sans arg
    bool callJSFunction(const char* name, float arg);    // appel avec 1 float (dt)

    std::string scriptPath_;       // ex: "scripts/player_movement.js" (relatif au projet)
    JSContext* ctx_ = nullptr;
    bool contextReady_ = false;
};

} // namespace saida
```

### 3.2 — Implémentation

```cpp
// src/scripting/ScriptBehaviour.cpp

#include "scripting/ScriptBehaviour.hpp"
#include "scripting/JSEngine.hpp"
#include "scene/Node.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"
#include <quickjs.h>
#include <nlohmann/json.hpp>

namespace saida {

ScriptBehaviour::~ScriptBehaviour() {
    teardownContext();
}

void ScriptBehaviour::initContext() {
    if (ctx_) return;
    ctx_ = JSEngine::get().createContext();
    JSEngine::get().setContextNode(ctx_, node());
    contextReady_ = false;
}

void ScriptBehaviour::teardownContext() {
    if (ctx_) {
        JSEngine::get().destroyContext(ctx_);
        ctx_ = nullptr;
        contextReady_ = false;
    }
}

void ScriptBehaviour::setScriptPath(const std::string& path) {
    scriptPath_ = path;
}

void ScriptBehaviour::reload() {
    teardownContext();
    if (!scriptPath_.empty()) {
        initContext();
        // Résoudre le chemin relatif au projet
        std::string fullPath = Paths::assetPath(scriptPath_);
        if (JSEngine::get().evalFile(ctx_, fullPath)) {
            contextReady_ = true;
            Log::info("[ScriptBehaviour] Loaded: {}", scriptPath_);
        } else {
            Log::error("[ScriptBehaviour] Failed to load: {}", scriptPath_);
        }
    }
}

void ScriptBehaviour::onReady() {
    if (scriptPath_.empty()) return;
    reload();
    if (contextReady_) callJSFunction("onReady");
}

void ScriptBehaviour::onUpdate(float dt) {
    if (!contextReady_) return;
    callJSFunction("onUpdate", dt);
    // Exécuter les jobs async en attente dans ce contexte
    JSEngine::get().executePendingJobs();
}

void ScriptBehaviour::onDestroy() {
    if (contextReady_) callJSFunction("onDestroy");
    teardownContext();
}

void ScriptBehaviour::onEnable() {
    if (contextReady_) callJSFunction("onEnable");
}

void ScriptBehaviour::onDisable() {
    if (contextReady_) callJSFunction("onDisable");
}

bool ScriptBehaviour::callJSFunction(const char* name) {
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, name);
    if (JS_IsFunction(ctx_, fn)) {
        JSValue ret = JS_Call(ctx_, fn, JS_UNDEFINED, 0, nullptr);
        if (JS_IsException(ret)) {
            // Log l'erreur (réutiliser la logique de JSEngine::eval)
            JSValue exc = JS_GetException(ctx_);
            const char* str = JS_ToCString(ctx_, exc);
            Log::error("[JS] {}() in {}: {}", name, scriptPath_, str ? str : "?");
            JS_FreeCString(ctx_, str);
            JS_FreeValue(ctx_, exc);
        }
        JS_FreeValue(ctx_, ret);
    }
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
    return true;
}

bool ScriptBehaviour::callJSFunction(const char* name, float arg) {
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, name);
    if (JS_IsFunction(ctx_, fn)) {
        JSValue jsArg = JS_NewFloat64(ctx_, arg);
        JSValue ret = JS_Call(ctx_, fn, JS_UNDEFINED, 1, &jsArg);
        if (JS_IsException(ret)) {
            JSValue exc = JS_GetException(ctx_);
            const char* str = JS_ToCString(ctx_, exc);
            Log::error("[JS] {}() in {}: {}", name, scriptPath_, str ? str : "?");
            JS_FreeCString(ctx_, str);
            JS_FreeValue(ctx_, exc);
        }
        JS_FreeValue(ctx_, ret);
        JS_FreeValue(ctx_, jsArg);
    }
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
    return true;
}

void ScriptBehaviour::save(nlohmann::json& j) const {
    j["scriptPath"] = scriptPath_;
}

void ScriptBehaviour::load(const nlohmann::json& j) {
    scriptPath_ = j.value("scriptPath", "");
}

void ScriptBehaviour::onDrawInspector() {
    // ImGui : champ texte pour scriptPath_ + bouton Reload
    // (l'éditeur appelle ça pour l'inspecteur)
}

} // namespace saida
```

### 3.3 — Enregistrement dans le BehaviourRegistry

Dans `Engine.cpp` (ou équivalent, là où les autres behaviours sont enregistrés) :

```cpp
#include "scripting/ScriptBehaviour.hpp"

// Au démarrage :
BehaviourRegistry::instance().registerType<ScriptBehaviour>("ScriptBehaviour");
```

Cela permet à la sérialisation de scène de sauver/charger les `ScriptBehaviour` avec leur `scriptPath`.

### 3.4 — Intégration éditeur

Dans l'inspecteur (`src/editor/panels/InspectorPanel.cpp`) :
- Quand un node a un `ScriptBehaviour`, afficher le champ `scriptPath` (éditable, file picker).
- Bouton "Reload" qui appelle `scriptBehaviour->reload()`.
- Menu "Add Behaviour > Script" qui crée un `ScriptBehaviour` vide sur le node sélectionné.

### 3.5 — Exemple de script complet

Fichier : `MyGame/scripts/player_movement.js`

```javascript
// player_movement.js — Behaviour de mouvement joueur

const SPEED = 5.0;
const JUMP_FORCE = 8.0;

function onReady() {
    ne.log("Player ready: " + self.name);

    // Bindings d'input
    ne.input.bindKey("move_left", "A");
    ne.input.bindKey("move_right", "D");
    ne.input.bindKey("move_forward", "W");
    ne.input.bindKey("move_back", "S");
    ne.input.bindKey("jump", "Space");

    // Timer de démonstration
    self.every(5.0, () => {
        ne.log("Still alive! Position: " + JSON.stringify(self.position));
    });
}

function onUpdate(dt) {
    let moveX = ne.input.getAxis("move_left", "move_right");
    let moveZ = ne.input.getAxis("move_back", "move_forward");

    let pos = self.position;
    pos.x += moveX * SPEED * dt;
    pos.z += moveZ * SPEED * dt;
    self.position = pos;

    if (ne.input.isActionJustPressed("jump")) {
        ne.log("Jump!");
        // Animer le saut avec un tween
        let startY = pos.y;
        self.tween(0.5, "easeOutQuad", (t) => {
            let p = self.position;
            p.y = startY + JUMP_FORCE * t * (1 - t) * 4;
            self.position = p;
        });
    }
}

function onDestroy() {
    ne.log("Player destroyed");
}
```

---

## Phase 4 — Backend de rendu RmlUi → Vulkan

RmlUi ne rend rien lui-même. Il appelle des méthodes sur des interfaces que **tu dois implémenter** pour
brancher ton rendu Vulkan.

### 4.1 — Implémenter `Rml::RenderInterface`

**Fichier à créer** : `src/ui/RmlRenderInterface.hpp` / `.cpp`

C'est la pièce centrale pour l'UI. RmlUi appelle cette interface avec des vertices + indices + textures,
et tu les rends via Vulkan.

```cpp
// src/ui/RmlRenderInterface.hpp
#pragma once

#include <RmlUi/Core/RenderInterface.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <memory>
#include <vector>

namespace saida {

class VulkanDevice;
class Buffer;
class Texture;
class Pipeline;

class RmlRenderInterface : public Rml::RenderInterface {
public:
    explicit RmlRenderInterface(VulkanDevice& device);
    ~RmlRenderInterface() override;

    // --- Géométrie (RmlUi envoie pos2D + color + texcoord + indices) ---
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                 Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    // --- Textures ---
    Rml::TextureHandle LoadTexture(Rml::Span<const Rml::byte>& source,
                                    Rml::Vector2i& source_dimensions,
                                    const Rml::String& source_path) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                        Rml::Vector2i source_dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;

    // --- Clipping ---
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    // --- Transform ---
    void SetTransform(const Rml::Matrix4f* transform) override;

    // === Méthodes custom pour le moteur ===

    // Appelé par le Renderer avant/après le rendu UI dans le command buffer
    void beginFrame(VkCommandBuffer cmd, uint32_t viewportWidth, uint32_t viewportHeight);
    void endFrame();

private:
    struct CompiledGeometry {
        std::unique_ptr<Buffer> vbo;
        std::unique_ptr<Buffer> ibo;
        int indexCount = 0;
    };

    VulkanDevice& device_;
    VkCommandBuffer currentCmd_ = VK_NULL_HANDLE;

    // Pipeline UI (réutilise ui.vert / ui.frag existants)
    std::unique_ptr<Pipeline> pipeline_;

    // Projection ortho pour le viewport courant
    glm::mat4 projection_{1.0f};

    // Transform stack (SetTransform)
    glm::mat4 currentTransform_{1.0f};
    bool hasTransform_ = false;

    // Scissor
    bool scissorEnabled_ = false;
    VkRect2D scissorRect_{};

    // Géométries compilées (handle → data)
    std::unordered_map<Rml::CompiledGeometryHandle, CompiledGeometry> geometries_;
    Rml::CompiledGeometryHandle nextGeomHandle_ = 1;

    // Textures (handle → Texture)
    std::unordered_map<Rml::TextureHandle, std::unique_ptr<Texture>> textures_;
    Rml::TextureHandle nextTexHandle_ = 1;

    // Texture blanche 1x1 par défaut (quand pas de texture)
    std::unique_ptr<Texture> whiteTexture_;
};

} // namespace saida
```

**Points clés d'implémentation** :

- Le format vertex RmlUi (`Rml::Vertex`) = `{vec2 position, uint32 colour, vec2 tex_coord}`.
  C'est quasi-identique au format des shaders `ui.vert` / `ui.frag` existants.
  Adapter le layout de vertex si nécessaire (la couleur RmlUi est en RGBA8, pas en vec4 float).
- `CompileGeometry` → alloue un VBO + IBO via `Buffer` (GpuOnly, avec staging).
- `RenderGeometry` → bind pipeline, bind texture (ou blanche), push {translation + transform + projection}
  en push constant ou UBO, `vkCmdDrawIndexed`.
- `GenerateTexture` → crée une `Texture` Vulkan depuis des pixels RGBA.
- `LoadTexture` → charge une image depuis le filesystem via `stb_image` puis `GenerateTexture`.
- Le scissor mappe directement sur `vkCmdSetScissor`.
- `SetTransform` : RmlUi passe une matrice 4×4 pour les transforms CSS (transforms, transitions).

**Référence** : Le code dans `src/graphics/UIRenderer.cpp` fait déjà du rendu de quads UI via Vulkan.
S'en inspirer fortement. Les shaders `shaders/ui.vert` et `shaders/ui.frag` sont réutilisables.

### 4.2 — Implémenter `Rml::SystemInterface`

**Fichier** : `src/ui/RmlSystemInterface.hpp` / `.cpp` — trivial :

```cpp
class RmlSystemInterface : public Rml::SystemInterface {
public:
    double GetElapsedTime() override { return (double)saida::Time::elapsed(); }
    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override {
        if (type >= Rml::Log::LT_ERROR) saida::Log::error("[RmlUi] {}", message);
        else if (type >= Rml::Log::LT_WARNING) saida::Log::warn("[RmlUi] {}", message);
        else saida::Log::info("[RmlUi] {}", message);
        return true;
    }
};
```

### 4.3 — Implémenter `Rml::FileInterface`

**Fichier** : `src/ui/RmlFileInterface.hpp` / `.cpp`

Résout les chemins de fichiers (HTML, CSS, images, fonts) via le système de paths du moteur.

```cpp
class RmlFileInterface : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        // Chercher d'abord dans le projet (Project::rootPath() + path),
        // puis dans les assets moteur (Paths::assetPath(path)).
        std::string resolved = resolvePath(path);
        FILE* f = fopen(resolved.c_str(), "rb");
        return (Rml::FileHandle)f;
    }
    void Close(Rml::FileHandle file) override { fclose((FILE*)file); }
    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return fread(buffer, 1, size, (FILE*)file);
    }
    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return fseek((FILE*)file, offset, origin) == 0;
    }
    size_t Tell(Rml::FileHandle file) override { return ftell((FILE*)file); }
    size_t Length(Rml::FileHandle file) override {
        long pos = ftell((FILE*)file); fseek((FILE*)file, 0, SEEK_END);
        size_t len = ftell((FILE*)file); fseek((FILE*)file, pos, SEEK_SET);
        return len;
    }
};
```

### 4.4 — Initialisation globale de RmlUi

Dans `Engine.cpp` (au démarrage, après la création du `VulkanDevice`) :

```cpp
#include <RmlUi/Core.h>
#include "ui/RmlSystemInterface.hpp"
#include "ui/RmlRenderInterface.hpp"
#include "ui/RmlFileInterface.hpp"

// Créer les interfaces (les stocker en membres de Engine ou en globals)
rmlSystem_ = std::make_unique<RmlSystemInterface>();
rmlRender_ = std::make_unique<RmlRenderInterface>(device_);
rmlFile_   = std::make_unique<RmlFileInterface>();

Rml::SetSystemInterface(rmlSystem_.get());
Rml::SetRenderInterface(rmlRender_.get());
Rml::SetFileInterface(rmlFile_.get());
Rml::Initialise();

// Charger la police par défaut
Rml::LoadFontFace("assets/fonts/Inter-Regular.ttf");
Rml::LoadFontFace("assets/fonts/Inter-Bold.ttf", true);
```

Et au shutdown :
```cpp
Rml::Shutdown();
```

---

## Phase 5 — Nouveau WebCanvasNode (RmlUi + JS)

### 5.1 — Réécrire `WebCanvasNode`

Le `WebCanvasNode` passe d'Ultralight à RmlUi + QuickJS. L'API publique reste similaire.

```cpp
// src/scene/WebCanvasNode.hpp (réécrit)
#pragma once

#include "scene/Node.hpp"
#include <string>
#include <memory>
#include <vector>

namespace Rml { class Context; class ElementDocument; }
struct JSContext;

namespace saida {

class VulkanDevice;
class Texture;
class Buffer;
class ResourceManager;

class WebCanvasNode : public Node {
public:
    enum class Mode { ScreenSpace, WorldSpace };

    WebCanvasNode();
    ~WebCanvasNode() override;

    void init(VulkanDevice& device, uint32_t width, uint32_t height, Mode mode = Mode::ScreenSpace);

    // Charger du HTML (string ou fichier)
    void loadHTML(const std::string& html);
    void loadURL(const std::string& url);  // charge un fichier .rml/.html

    // Exécuter du JS dans le contexte de ce canvas
    void executeJS(const std::string& script);

    // Callback JS accessible depuis le HTML (onclick="ne.canvas.call('myFunc', ...)")
    using JSCallback = std::function<void(const std::vector<std::string>&)>;
    void registerFunction(const std::string& name, JSCallback callback);

    // Rendu : mettre à jour la texture si le document a changé
    // Mode ScreenSpace : rend directement dans le command buffer principal
    // Mode WorldSpace : rend dans une texture offscreen
    void render(VkCommandBuffer cmd);

    // Input forwarding
    void processMouseMove(int x, int y);
    void processMouseButton(int button, bool down);
    void processMouseScroll(int deltaX, int deltaY);
    void processKeyDown(int key, int modifiers);
    void processKeyUp(int key, int modifiers);
    void processTextInput(char character);

    // Accesseurs
    const std::string& url() const { return url_; }
    void setUrl(const std::string& url);
    void reload();
    Mode mode() const { return mode_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    Texture* texture() const { return texture_.get(); }  // WorldSpace only

    // Sérialisation (même format JSON qu'avant)
    const char* typeName() const override { return "WebCanvasNode"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

private:
    Mode mode_ = Mode::ScreenSpace;
    uint32_t width_ = 0, height_ = 0;
    std::string url_;

    // RmlUi
    Rml::Context* rmlContext_ = nullptr;        // un "viewport" RmlUi
    Rml::ElementDocument* document_ = nullptr;

    // QuickJS (pour le JS dans ce canvas)
    JSContext* jsContext_ = nullptr;

    // WorldSpace : rendu offscreen dans une texture
    std::unique_ptr<Texture> texture_;
    VkFramebuffer offscreenFB_ = VK_NULL_HANDLE;
    VkRenderPass offscreenPass_ = VK_NULL_HANDLE;
    // ...

    std::vector<std::string> startupScripts_;
};

} // namespace saida
```

### 5.2 — Rendu ScreenSpace vs WorldSpace

**ScreenSpace** :
- Le `Rml::Context` a la taille de la fenêtre.
- Dans `Engine::run`, après le tonemap et avant ImGui, appeler :
  ```cpp
  rmlContext->Update();   // layout + animation CSS
  rmlContext->Render();   // → appelle RmlRenderInterface (Vulkan draw calls dans le cmd courant)
  ```
- Pas besoin de texture intermédiaire — on dessine directement dans le framebuffer principal.

**WorldSpace** :
- Le `Rml::Context` a la taille de la texture (ex: 1024×768).
- Rendre dans un **framebuffer offscreen** (render target Vulkan), puis utiliser la texture
  résultante comme un matériau classique (plaquée sur un quad 3D).
- C'est le même mécanisme que l'ancien Ultralight mais **GPU-native** (plus de bitmap CPU → upload).

### 5.3 — Bridge JS ↔ RmlUi DOM

Le `jsContext_` du WebCanvasNode doit pouvoir manipuler le DOM RmlUi :

```javascript
// Dans le script d'un WebCanvasNode :
let btn = document.getElementById("start-btn");
btn.addEventListener("click", () => {
    ne.tree.changeScene("scenes/game.scene");
});
btn.style.backgroundColor = "#ff0000";
btn.innerHTML = "GO!";
```

**Implémentation** : Dans le JS du canvas, exposer un objet `document` qui wrappe `Rml::ElementDocument` :
- `document.getElementById(id)` → `rmlDocument->GetElementById(id)` → retourne un handle JS
- `element.addEventListener(event, fn)` → crée un `Rml::EventListener` qui appelle la fonction JS
- `element.style.setProperty(prop, val)` → `element->SetProperty(prop, val)`
- `element.innerHTML = "..."` → `element->SetInnerRML(html)`
- `element.classList.add/remove/toggle(cls)` → manipulation des classes CSS

C'est un subset du DOM standard, suffisant pour que les LLMs génèrent du code naturellement.

**Fichier** : `src/ui/RmlJSBridge.hpp` / `.cpp`

### 5.4 — Suppression de WebBridge.hpp/cpp

L'ancien `src/scene/WebBridge.hpp` / `.cpp` (basé sur JavaScriptCore/Ultralight) est remplacé
par le `RmlJSBridge`. Supprimer les anciens fichiers.

---

## Phase 6 — Hot-reload des scripts

### 6.1 — File watcher

**Fichier** : `src/scripting/FileWatcher.hpp` / `.cpp`

Surveille les fichiers `.js` du projet. Quand un fichier change :
- Trouve tous les `ScriptBehaviour` qui utilisent ce fichier.
- Appelle `scriptBehaviour->reload()` sur chacun.
- Trouve tous les `WebCanvasNode` dont l'URL pointe vers un fichier modifié.
- Appelle `webCanvas->reload()`.

**Implémentation** :
- Sous Windows (MSYS2) : utiliser `ReadDirectoryChangesW` (API Win32) ou, plus simple,
  un polling toutes les 500ms qui compare les `lastWriteTime` des fichiers surveillés.
- Le polling est plus simple et suffisant pour du dev (pas de dépendance externe).

```cpp
class FileWatcher {
public:
    void watchDirectory(const std::string& dir);
    // Appelé chaque frame (ou chaque N frames pour limiter le coût)
    void poll();
    // Callback quand un fichier change
    Signal<const std::string&> fileChanged;
};
```

### 6.2 — Intégration

Dans `Engine::run` :
```cpp
// Toutes les 30 frames (~0.5s à 60fps) :
if (frameCount_ % 30 == 0) {
    fileWatcher_.poll();
}
```

Les `ScriptBehaviour` et `WebCanvasNode` s'abonnent au signal `fileChanged` dans leur `onReady`.

---

## Phase 7 — Nettoyage : Supprimer Ultralight

### 7.1 — Fichiers à supprimer

| Fichier | Raison |
|---|---|
| `src/ui/WebEngine.hpp` / `.cpp` | Remplacé par `JSEngine` |
| `src/scene/WebBridge.hpp` / `.cpp` | Remplacé par `RmlJSBridge` |
| `ultralight-sdk.7z` | SDK propriétaire (18 Mo) |
| `ultralight_download.7z` | Doublon |
| `ultralight.log` | Log Ultralight |

### 7.2 — Fichiers à réécrire

| Fichier | Changement |
|---|---|
| `src/scene/WebCanvasNode.hpp` / `.cpp` | Phase 5 |
| `src/ui/UIInteractionSystem.cpp` | Adapter : forward input vers `RmlUi::Context::Process*` au lieu d'Ultralight |

### 7.3 — Fichiers à modifier

| Fichier | Changement |
|---|---|
| `CMakeLists.txt` | Retirer Ultralight/AppCore/JavaScriptCore. Ajouter rmlui, quickjs, freetype. Ajouter `src/scripting/` au target. |
| `src/Engine.cpp` | Retirer `WebEngine::get().update()`. Ajouter init RmlUi (Phase 4.4). Ajouter `JSEngine::get().executePendingJobs()`. Ajouter FileWatcher. Enregistrer `ScriptBehaviour` dans le `BehaviourRegistry`. |
| `src/Engine.hpp` | Ajouter membres : `RmlSystemInterface`, `RmlRenderInterface`, `RmlFileInterface`, `FileWatcher`. |
| `src/render/Renderer.cpp` | Dans le rendu, appeler `rmlRender->beginFrame/endFrame` + `rmlContext->Render()` pour les canvas ScreenSpace. |
| `src/editor/panels/InspectorPanel.cpp` | Ajouter l'inspecteur de `ScriptBehaviour` (champ `scriptPath` + bouton Reload). |
| `src/editor/EditorUI.cpp` | Menu "Add Behaviour > Script" pour ajouter un `ScriptBehaviour`. |

### 7.4 — CMakeLists.txt final (changements)

```cmake
# ============ RETIRER ============
# Tout ce qui mentionne : Ultralight, UltralightCore, AppCore, WebCore, JavaScriptCore
# Les include_directories vers ultralight-sdk
# Les target_link_libraries vers ces libs
# Les file(DOWNLOAD ...) ou ExternalProject pour Ultralight

# ============ AJOUTER ============
# QuickJS
file(GLOB QUICKJS_SRC third_party/quickjs/*.c)
add_library(quickjs STATIC ${QUICKJS_SRC})
target_include_directories(quickjs PUBLIC third_party/quickjs)
target_compile_definitions(quickjs PRIVATE _GNU_SOURCE)

# Freetype (pour RmlUi)
add_subdirectory(third_party/freetype)

# RmlUi
set(RMLUI_BACKEND "" CACHE STRING "" FORCE)
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_THIRDPARTY_CONTAINERS ON CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/rmlui)

# Sources scripting
set(SCRIPTING_SOURCES
    src/scripting/JSEngine.cpp
    src/scripting/JSBindings.cpp
    src/scripting/ScriptBehaviour.cpp
    src/scripting/FileWatcher.cpp
)
# Ajouter au target saida_engine
target_sources(saida_engine PRIVATE ${SCRIPTING_SOURCES})

# UI RmlUi
set(UI_RMLUI_SOURCES
    src/ui/RmlRenderInterface.cpp
    src/ui/RmlSystemInterface.cpp
    src/ui/RmlFileInterface.cpp
    src/ui/RmlJSBridge.cpp
    src/ui/UIInteractionSystem.cpp
)
target_sources(saida_engine PRIVATE ${UI_RMLUI_SOURCES})

target_link_libraries(saida_engine PUBLIC rmlui quickjs freetype)
```

---

## Phase 8 — Mises à jour documentation

### 8.1 — CLAUDE.md

- **Stack** : Ajouter RmlUi, QuickJS, freetype. Retirer Ultralight.
- **Architecture** :
  - Ajouter `src/scripting/` : `JSEngine` (runtime QuickJS central), `JSBindings` (API moteur en JS),
    `ScriptBehaviour` (behaviour en JS), `FileWatcher` (hot-reload).
  - Modifier `src/ui/` : `RmlRenderInterface` + `RmlSystemInterface` + `RmlFileInterface` + `RmlJSBridge`.
- **Étape 8b** : Marquer comme **faite** — scripting JS via QuickJS (remplace Lua).
- **Étape 12** : Marquer comme **réimplémentée** — RmlUi + QuickJS (remplace Ultralight).
- **Contrat de jeu** : Ajouter que les scripts `.js` suivent les mêmes 5 règles que les Behaviours C++.
- **Comment coder un jeu** : Ajouter la section JS (exemple de `ScriptBehaviour`, API `ne.*`, `self.*`).

### 8.2 — Nouveau fichier SCRIPTING.md

Créer un `SCRIPTING.md` à la racine qui documente :
- L'API JS complète (`ne.*`, `self.*`, `document.*`)
- Comment créer un script behaviour
- Comment faire de l'UI en HTML/CSS/JS
- Le hot-reload
- Les conventions (un fichier = un behaviour, pas de globals partagés entre scripts)

---

## Ordre d'exécution et estimation

```
Phase 0.1  Vendre QuickJS               (30 min)
Phase 0.2  Vendre freetype              (30 min)
Phase 0.3  Vendre RmlUi                 (30 min)
Phase 0.4  Vérifier compilation         (15 min)
                                         --- checkpoint : tout compile ---

Phase 1    JSEngine (runtime central)    (1h)
Phase 2    JSBindings (API ne.*)         (2-3h)
Phase 3    ScriptBehaviour               (2h)
Phase 3.3  BehaviourRegistry             (15 min)
                                         --- checkpoint : scripts .js fonctionnels ---
                                         --- test : un ScriptBehaviour qui log + bouge un cube ---

Phase 4.2  RmlSystemInterface            (15 min)
Phase 4.3  RmlFileInterface              (30 min)
Phase 4.1  RmlRenderInterface            (3-4h — la pièce principale)
Phase 4.4  Init RmlUi dans Engine        (30 min)
                                         --- checkpoint : RmlUi rend du HTML en Vulkan ---

Phase 5    WebCanvasNode (RmlUi)         (2-3h)
Phase 5.3  RmlJSBridge (DOM bindings)    (2h)
                                         --- checkpoint : UI HTML/CSS/JS fonctionnelle ---
                                         --- test : un menu HTML avec boutons qui changent de scène ---

Phase 6    FileWatcher (hot-reload)      (1h)
Phase 7    Nettoyage Ultralight          (1h)
Phase 3.4  Intégration éditeur           (1h)
Phase 8    Documentation                 (1h)
                                         --- DONE ---
```

**Total estimé : ~18-22h de travail effectif.**

---

## Risques et points d'attention

### Build / Toolchain

1. **MinGW + QuickJS** : QuickJS est du C pur, compile partout. Aucun risque.
2. **MinGW + RmlUi** : RmlUi supporte GCC mais est principalement testé MSVC/Clang.
   Risque modéré de warnings ou de code conditionnel à adapter. Tester tôt (Phase 0).
3. **MinGW + freetype** : Freetype est très portable. Aucun risque.
4. **Linkage statique** : Les 3 libs compilent en statique sans souci.

### Fonctionnel

5. **RmlUi CSS subset** : Pas de `grid` complet, pas de `position: sticky`, pas de `backdrop-filter`.
   Flexbox fonctionne. Animations CSS et transitions fonctionnent.
   **Mitigation** : documenter le subset dans CLAUDE.md pour que les LLMs génèrent du CSS compatible.

6. **Performance JS** : QuickJS est un interpréteur bytecode, pas un JIT.
   Pour du scripting de gameplay (~100 lignes de logique par frame), c'est largement suffisant.
   Si un jour un script est trop lent, le refactorer en C++ Behaviour.

7. **Handles de nœuds en JS** : Les `Node*` passés au JS peuvent devenir dangling si le nœud
   est détruit. **v1** : crash possible si un script garde une référence à un nœud mort.
   **v2** : ajouter un registre de handles avec weak refs.

8. **Hot-reload** : La version par polling (Phase 6) est simple et fiable.
   Les Promises/closures en cours seront perdues au reload — c'est acceptable.

9. **Rendu texte** : Nécessite des polices .ttf vendues. Sans police, RmlUi ne rend aucun texte.
   Vendre au minimum une police dans `assets/fonts/`.

### Architecture

10. **Un contexte JS par ScriptBehaviour** : Isole les scripts (pas de globals partagés entre scripts,
    ce qui est voulu par le contrat d'architecture). Coût mémoire faible (~50 Ko par contexte QuickJS).

11. **Autoloads en JS** : Un autoload peut être un `ScriptBehaviour` — la persistence fonctionne
    car le `SceneTree` garde le node vivant entre les changements de scène.

12. **Signaux JS → C++** : Le JS peut émettre des signaux customs qui sont captés par les `listen()`
    des behaviours C++ et vice-versa. Nécessite un bridge signal léger (à implémenter dans les bindings).

---

## Résumé des fichiers créés/modifiés

### Nouveaux fichiers (à créer)

```
src/scripting/
    JSEngine.hpp           Runtime QuickJS central (singleton)
    JSEngine.cpp
    JSBindings.hpp         API moteur exposée au JS (ne.*, self.*)
    JSBindings.cpp
    ScriptBehaviour.hpp    Behaviour dont la logique est un fichier .js
    ScriptBehaviour.cpp
    FileWatcher.hpp        Hot-reload des scripts .js
    FileWatcher.cpp

src/ui/
    RmlRenderInterface.hpp  Backend Vulkan pour RmlUi
    RmlRenderInterface.cpp
    RmlSystemInterface.hpp  Time + logging pour RmlUi
    RmlSystemInterface.cpp
    RmlFileInterface.hpp    Résolution de fichiers pour RmlUi
    RmlFileInterface.cpp
    RmlJSBridge.hpp         Bindings DOM (document.getElementById, etc.)
    RmlJSBridge.cpp

third_party/
    quickjs/               Sources QuickJS vendues
    rmlui/                 Sources RmlUi vendues
    freetype/              Sources freetype vendues

assets/fonts/
    Inter-Regular.ttf      Police par défaut
    Inter-Bold.ttf
```

### Fichiers modifiés

```
CMakeLists.txt             Retirer Ultralight, ajouter quickjs/rmlui/freetype
src/Engine.hpp             Nouveaux membres (RmlUi interfaces, FileWatcher)
src/Engine.cpp             Init RmlUi, tick JSEngine, tick FileWatcher, register ScriptBehaviour
src/render/Renderer.cpp    Appeler RmlUi render pour les canvas ScreenSpace
src/scene/WebCanvasNode.*  Réécrit (Phase 5)
src/ui/UIInteractionSystem.cpp  Adapter input → RmlUi
src/editor/panels/InspectorPanel.cpp  Inspecteur ScriptBehaviour
src/editor/EditorUI.cpp    Menu "Add Script Behaviour"
CLAUDE.md                  Mise à jour stack/architecture/étapes
```

### Fichiers supprimés

```
src/ui/WebEngine.hpp / .cpp          (remplacé par JSEngine)
src/scene/WebBridge.hpp / .cpp       (remplacé par RmlJSBridge)
ultralight-sdk.7z                    (SDK propriétaire)
ultralight_download.7z               (doublon)
ultralight.log                       (log Ultralight)
```
