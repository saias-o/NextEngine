#include "scripting/JsEngineBindings.hpp"

#include "audio/AudioManager.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Reflection.hpp"
#include "core/Time.hpp"
#include "graphics/ResourceManager.hpp"
#include "project/AssetLoader.hpp"
#include "scene/Behaviour.hpp"
#include "scene/Node.hpp"
#include "scene/SceneTree.hpp"
#ifndef SAIDA_RHI_WEBGPU
#include "scene/UITextNode.hpp"
#endif
#include "scripting/JsContext.hpp"
#include "scripting/JsTimerBindings.hpp"

#include <quickjs.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace saida {

namespace {

JSClassID gAssetHandleClassId = 0;
SceneTree* treeFromJs(JSContext* ctx);

AssetHandle* assetHandleFromJs(JSContext* ctx, JSValueConst value) {
    return static_cast<AssetHandle*>(JS_GetOpaque2(ctx, value, gAssetHandleClassId));
}

void jsAssetHandleFinalizer(JSRuntime*, JSValueConst value) {
    delete static_cast<AssetHandle*>(JS_GetOpaque(value, gAssetHandleClassId));
}

JSValue jsAssetHandleState(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    AssetHandle* handle = assetHandleFromJs(ctx, self);
    return handle ? JS_NewString(ctx, assetLoadStateName(handle->state())) : JS_EXCEPTION;
}

JSValue jsAssetHandleReady(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    AssetHandle* handle = assetHandleFromJs(ctx, self);
    return handle ? JS_NewBool(ctx, handle->ready()) : JS_EXCEPTION;
}

JSValue jsAssetHandleFailed(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    AssetHandle* handle = assetHandleFromJs(ctx, self);
    return handle ? JS_NewBool(ctx, handle->failed()) : JS_EXCEPTION;
}

JSValue jsAssetHandleError(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    AssetHandle* handle = assetHandleFromJs(ctx, self);
    return handle ? JS_NewString(ctx, handle->error().c_str()) : JS_EXCEPTION;
}

JSValue jsAssetHandleSize(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    AssetHandle* handle = assetHandleFromJs(ctx, self);
    return handle ? JS_NewInt64(ctx, static_cast<int64_t>(handle->bytes().size())) : JS_EXCEPTION;
}

JSValue jsAssetHandleId(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    AssetHandle* handle = assetHandleFromJs(ctx, self);
    return handle ? JS_NewBigUint64(ctx, handle->id()) : JS_EXCEPTION;
}

JSValue jsAssetHandleRelease(JSContext* ctx, JSValueConst self, int, JSValueConst*) {
    AssetHandle* handle = assetHandleFromJs(ctx, self);
    if (!handle) return JS_EXCEPTION;
    handle->reset();
    return JS_UNDEFINED;
}

AssetLoadPriority readAssetPriority(JSContext* ctx, int argc, JSValueConst* argv) {
    if (argc < 2 || JS_IsUndefined(argv[1])) return AssetLoadPriority::Normal;
    const char* value = JS_ToCString(ctx, argv[1]);
    if (!value) return AssetLoadPriority::Normal;
    const std::string priority(value);
    JS_FreeCString(ctx, value);
    if (priority == "low") return AssetLoadPriority::Low;
    if (priority == "high") return AssetLoadPriority::High;
    if (priority == "critical") return AssetLoadPriority::Critical;
    return AssetLoadPriority::Normal;
}

JSValue jsAssetsLoad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) return JS_ThrowInternalError(ctx, "assets.load requires a mounted SceneTree");
    if (argc < 1) return JS_ThrowTypeError(ctx, "assets.load(path[, priority]) requires a path");
    const char* raw = JS_ToCString(ctx, argv[0]);
    if (!raw) return JS_EXCEPTION;
    const std::string path(raw);
    JS_FreeCString(ctx, raw);
    const std::filesystem::path normalized = std::filesystem::path(path).lexically_normal();
    if (path.empty() || normalized.is_absolute() || normalized.begin()->string() == "..")
        return JS_ThrowTypeError(ctx, "asset path must stay inside the project");

    AssetHandle handle = tree->resources().assetLoader().request(
        normalized.generic_string(), AssetType::Unknown,
        readAssetPriority(ctx, argc, argv));
    if (!handle) return JS_ThrowInternalError(ctx, "asset loader is unavailable");
    JSValue object = JS_NewObjectClass(ctx, gAssetHandleClassId);
    if (JS_IsException(object)) return object;
    JS_SetOpaque(object, new AssetHandle(std::move(handle)));
    return object;
}

JSValue jsAudioPlay(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "audio.play(aliasName)");
    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_EXCEPTION;
    const bool ok = AudioManager::get().play(name) != kInvalidAudioID;
    JS_FreeCString(ctx, name);
    return JS_NewBool(ctx, ok);
}

// ---- storage: persistance minimale par slot (fichiers saves/<slot>.json) ----
// Le JS sérialise lui-même (JSON.stringify/parse) ; le moteur ne stocke que des
// chaînes. Chemin résolu sous la racine projet → identique dans l'éditeur, le
// runtime packagé (racine = dossier de l'exe) et le player web (MEMFS).

bool validStorageSlot(const std::string& slot) {
    if (slot.empty() || slot.size() > 64) return false;
    for (char c : slot) {
        const unsigned char uc = static_cast<unsigned char>(c);
        if (!std::isalnum(uc) && c != '_' && c != '-') return false;
    }
    return true;
}

std::filesystem::path storageSlotPath(JSContext* ctx, int argc, JSValueConst* argv,
                                      JSValue& outError) {
    outError = JS_UNDEFINED;
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) {
        outError = JS_ThrowInternalError(ctx, "storage requires a mounted SceneTree");
        return {};
    }
    if (argc < 1) {
        outError = JS_ThrowTypeError(ctx, "storage: missing slot name");
        return {};
    }
    const char* raw = JS_ToCString(ctx, argv[0]);
    if (!raw) {
        outError = JS_EXCEPTION;
        return {};
    }
    const std::string slot(raw);
    JS_FreeCString(ctx, raw);
    if (!validStorageSlot(slot)) {
        outError = JS_ThrowTypeError(
            ctx, "storage: slot must match [A-Za-z0-9_-]{1,64}");
        return {};
    }
    return std::filesystem::path(tree->resolveProjectPath("saves")) / (slot + ".json");
}

JSValue jsStorageSave(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue error;
    const std::filesystem::path path = storageSlotPath(ctx, argc, argv, error);
    if (path.empty()) return error;
    if (argc < 2) return JS_ThrowTypeError(ctx, "storage.save(slot, jsonString)");
    const char* value = JS_ToCString(ctx, argv[1]);
    if (!value) return JS_EXCEPTION;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    bool ok;
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        ok = static_cast<bool>(file << value);
    }
    JS_FreeCString(ctx, value);
    if (!ok) Log::warn("[JS] storage.save: cannot write ", path.string());
#ifdef __EMSCRIPTEN__
    // saves/ vit sur IDBFS (monté par le shell) : flush vers IndexedDB.
    if (ok) EM_ASM({ FS.syncfs(false, function() {}); });
#endif
    return JS_NewBool(ctx, ok);
}

JSValue jsStorageLoad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue error;
    const std::filesystem::path path = storageSlotPath(ctx, argc, argv, error);
    if (path.empty()) return error;
    std::ifstream file(path, std::ios::binary);
    if (!file) return JS_NULL;
    std::ostringstream content;
    content << file.rdbuf();
    const std::string text = content.str();
    return JS_NewStringLen(ctx, text.data(), text.size());
}

JSValue jsStorageHas(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue error;
    const std::filesystem::path path = storageSlotPath(ctx, argc, argv, error);
    if (path.empty()) return error;
    std::error_code ec;
    return JS_NewBool(ctx, std::filesystem::is_regular_file(path, ec));
}

JSValue jsStorageRemove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    JSValue error;
    const std::filesystem::path path = storageSlotPath(ctx, argc, argv, error);
    if (path.empty()) return error;
    std::error_code ec;
    const bool removed = std::filesystem::remove(path, ec);
#ifdef __EMSCRIPTEN__
    if (removed) EM_ASM({ FS.syncfs(false, function() {}); });
#endif
    return JS_NewBool(ctx, removed);
}

void installAssetHandleClass(JSContext* ctx) {
    JSRuntime* runtime = JS_GetRuntime(ctx);
    if (gAssetHandleClassId == 0) JS_NewClassID(runtime, &gAssetHandleClassId);
    JSClassDef def{};
    def.class_name = "SaidaAssetHandle";
    def.finalizer = jsAssetHandleFinalizer;
    JS_NewClass(runtime, gAssetHandleClassId, &def);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "state", JS_NewCFunction(ctx, jsAssetHandleState, "state", 0));
    JS_SetPropertyStr(ctx, proto, "ready", JS_NewCFunction(ctx, jsAssetHandleReady, "ready", 0));
    JS_SetPropertyStr(ctx, proto, "failed", JS_NewCFunction(ctx, jsAssetHandleFailed, "failed", 0));
    JS_SetPropertyStr(ctx, proto, "error", JS_NewCFunction(ctx, jsAssetHandleError, "error", 0));
    JS_SetPropertyStr(ctx, proto, "size", JS_NewCFunction(ctx, jsAssetHandleSize, "size", 0));
    JS_SetPropertyStr(ctx, proto, "id", JS_NewCFunction(ctx, jsAssetHandleId, "id", 0));
    JS_SetPropertyStr(ctx, proto, "release", JS_NewCFunction(ctx, jsAssetHandleRelease, "release", 0));
    JS_SetClassProto(ctx, gAssetHandleClassId, proto);
}

Behaviour* behaviourFromJs(JSContext* ctx) {
    return static_cast<Behaviour*>(JS_GetContextOpaque(ctx));
}

Node* nodeFromJs(JSContext* ctx) {
    Behaviour* behaviour = behaviourFromJs(ctx);
    return behaviour ? behaviour->node() : nullptr;
}

SceneTree* treeFromJs(JSContext* ctx) {
    Behaviour* behaviour = behaviourFromJs(ctx);
    return behaviour ? behaviour->tree() : nullptr;
}

bool toNumber(JSContext* ctx, JSValueConst value, double& out) {
    return JS_ToFloat64(ctx, &out, value) == 0;
}

bool readVec3Args(JSContext* ctx, int argc, JSValueConst* argv, glm::vec3& out) {
    if (argc >= 1 && JS_IsObject(argv[0])) {
        JSValue x = JS_GetPropertyStr(ctx, argv[0], "x");
        JSValue y = JS_GetPropertyStr(ctx, argv[0], "y");
        JSValue z = JS_GetPropertyStr(ctx, argv[0], "z");

        double vx = 0.0;
        double vy = 0.0;
        double vz = 0.0;
        bool ok = toNumber(ctx, x, vx) && toNumber(ctx, y, vy) && toNumber(ctx, z, vz);

        JS_FreeValue(ctx, x);
        JS_FreeValue(ctx, y);
        JS_FreeValue(ctx, z);

        if (!ok) return false;
        out = glm::vec3(static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz));
        return true;
    }

    if (argc < 3) return false;

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    if (!toNumber(ctx, argv[0], x) || !toNumber(ctx, argv[1], y) || !toNumber(ctx, argv[2], z)) {
        return false;
    }

    out = glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    return true;
}

JSValue makeVec3(JSContext* ctx, const glm::vec3& v) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, v.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, v.y));
    JS_SetPropertyStr(ctx, obj, "z", JS_NewFloat64(ctx, v.z));
    return obj;
}

JSValue makeVec2(JSContext* ctx, const glm::vec2& v) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "x", JS_NewFloat64(ctx, v.x));
    JS_SetPropertyStr(ctx, obj, "y", JS_NewFloat64(ctx, v.y));
    return obj;
}

bool readActionName(JSContext* ctx, int argc, JSValueConst* argv, int index, std::string& out) {
    if (index >= argc) return false;
    const char* action = JS_ToCString(ctx, argv[index]);
    if (!action) return false;
    out = action;
    JS_FreeCString(ctx, action);
    return !out.empty();
}

JSValue jsNodeGetName(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    Node* node = nodeFromJs(ctx);
    return JS_NewString(ctx, node ? node->name().c_str() : "");
}

JSValue jsNodeSetName(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node || argc < 1) return JS_NewBool(ctx, false);

    const char* name = JS_ToCString(ctx, argv[0]);
    bool ok = name != nullptr;
    if (ok) node->setName(name);
    JS_FreeCString(ctx, name);
    return JS_NewBool(ctx, ok);
}

JSValue jsNodeGetPosition(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    Node* node = nodeFromJs(ctx);
    return makeVec3(ctx, node ? node->transform().position : glm::vec3(0.0f));
}

JSValue jsNodeSetPosition(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node) return JS_NewBool(ctx, false);

    glm::vec3 value(0.0f);
    if (!readVec3Args(ctx, argc, argv, value)) return JS_NewBool(ctx, false);

    node->transform().position = value;
    return JS_NewBool(ctx, true);
}

JSValue jsNodeTranslate(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node) return JS_NewBool(ctx, false);

    glm::vec3 value(0.0f);
    if (!readVec3Args(ctx, argc, argv, value)) return JS_NewBool(ctx, false);

    node->transform().position += value;
    return JS_NewBool(ctx, true);
}

JSValue jsNodeSetEnabled(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node || argc < 1) return JS_NewBool(ctx, false);

    int enabled = JS_ToBool(ctx, argv[0]);
    if (enabled < 0) return JS_EXCEPTION;
    node->setEnabled(enabled != 0);
    return JS_NewBool(ctx, true);
}

JSValue jsNodeQueueFree(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    if (Node* node = nodeFromJs(ctx)) {
        node->queueFree();
    }
    return JS_UNDEFINED;
}

// Les nœuds UI ne sont pas encore portés sur le player web : là-bas ils se
// dégradent en Node générique et setText/getText deviennent des no-ops
// (warning unique), pour que le même script tourne sur les deux plateformes.
JSValue jsNodeSetText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
#ifdef SAIDA_RHI_WEBGPU
    (void)argc; (void)argv;
    static bool warned = false;
    if (!warned) {
        warned = true;
        Log::warn("[JS] node.setText: UI nodes are not ported to the web player yet (no-op)");
    }
    return JS_UNDEFINED;
#else
    Node* node = nodeFromJs(ctx);
    auto* text = dynamic_cast<UITextNode*>(node);
    if (!text)
        return JS_ThrowTypeError(ctx, "node.setText: script is not attached to a UITextNode");
    if (argc < 1) return JS_ThrowTypeError(ctx, "node.setText(text)");
    const char* value = JS_ToCString(ctx, argv[0]);
    if (!value) return JS_EXCEPTION;
    text->setText(value);
    JS_FreeCString(ctx, value);
    return JS_UNDEFINED;
#endif
}

JSValue jsNodeGetText(JSContext* ctx, JSValueConst, int, JSValueConst*) {
#ifdef SAIDA_RHI_WEBGPU
    return JS_NewString(ctx, "");
#else
    auto* text = dynamic_cast<UITextNode*>(nodeFromJs(ctx));
    if (!text)
        return JS_ThrowTypeError(ctx, "node.getText: script is not attached to a UITextNode");
    return JS_NewString(ctx, text->text().c_str());
#endif
}

JSValue jsNodeAddToGroup(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node || argc < 1) return JS_NewBool(ctx, false);

    const char* group = JS_ToCString(ctx, argv[0]);
    bool ok = group != nullptr;
    if (ok) node->addToGroup(group);
    JS_FreeCString(ctx, group);
    return JS_NewBool(ctx, ok);
}

JSValue jsNodeRemoveFromGroup(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node || argc < 1) return JS_NewBool(ctx, false);

    const char* group = JS_ToCString(ctx, argv[0]);
    bool ok = group != nullptr;
    if (ok) node->removeFromGroup(group);
    JS_FreeCString(ctx, group);
    return JS_NewBool(ctx, ok);
}

JSValue jsNodeIsInGroup(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node || argc < 1) return JS_NewBool(ctx, false);

    const char* group = JS_ToCString(ctx, argv[0]);
    bool ok = group && node->isInGroup(group);
    JS_FreeCString(ctx, group);
    return JS_NewBool(ctx, ok);
}

JSValue jsTimeDelta(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewFloat64(ctx, Time::delta());
}

JSValue jsTimeElapsed(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewFloat64(ctx, Time::elapsed());
}

JSValue jsInputIsHeld(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string action;
    if (!readActionName(ctx, argc, argv, 0, action)) return JS_NewBool(ctx, false);
    return JS_NewBool(ctx, Input::isActionHeld(action));
}

JSValue jsInputJustPressed(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string action;
    if (!readActionName(ctx, argc, argv, 0, action)) return JS_NewBool(ctx, false);
    return JS_NewBool(ctx, Input::isActionJustPressed(action));
}

JSValue jsInputJustReleased(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string action;
    if (!readActionName(ctx, argc, argv, 0, action)) return JS_NewBool(ctx, false);
    return JS_NewBool(ctx, Input::isActionJustReleased(action));
}

// Outil de test/CI : pilote une action sans clavier (voir Input::injectAction).
JSValue jsInputInject(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string action;
    if (!readActionName(ctx, argc, argv, 0, action)) return JS_NewBool(ctx, false);
    double strength = 1.0;
    if (argc >= 2 && JS_ToFloat64(ctx, &strength, argv[1]) != 0) return JS_EXCEPTION;
    Input::injectAction(action, static_cast<float>(strength));
    return JS_NewBool(ctx, true);
}

JSValue jsInputStrength(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string action;
    if (!readActionName(ctx, argc, argv, 0, action)) return JS_NewFloat64(ctx, 0.0);
    return JS_NewFloat64(ctx, Input::getActionStrength(action));
}

JSValue jsInputAxis(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string negative;
    std::string positive;
    if (!readActionName(ctx, argc, argv, 0, negative) || !readActionName(ctx, argc, argv, 1, positive)) {
        return JS_NewFloat64(ctx, 0.0);
    }
    return JS_NewFloat64(ctx, Input::getAxis(negative, positive));
}

JSValue jsInputVector(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    std::string left;
    std::string right;
    std::string down;
    std::string up;
    if (!readActionName(ctx, argc, argv, 0, left) ||
        !readActionName(ctx, argc, argv, 1, right) ||
        !readActionName(ctx, argc, argv, 2, down) ||
        !readActionName(ctx, argc, argv, 3, up)) {
        return makeVec2(ctx, glm::vec2(0.0f));
    }
    return makeVec2(ctx, Input::getVector(left, right, down, up));
}

JSValue jsInputMousePosition(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return makeVec2(ctx, Input::mousePosition());
}

JSValue jsInputMouseDelta(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return makeVec2(ctx, Input::mouseDelta());
}

JSValue jsTreeChangeScene(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree || argc < 1) return JS_NewBool(ctx, false);

    const char* path = JS_ToCString(ctx, argv[0]);
    bool ok = path != nullptr;
    if (ok) tree->changeScene(path);
    JS_FreeCString(ctx, path);
    return JS_NewBool(ctx, ok);
}

JSValue jsTreeReloadScene(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    if (SceneTree* tree = treeFromJs(ctx)) {
        tree->reloadScene();
        return JS_NewBool(ctx, true);
    }
    return JS_NewBool(ctx, false);
}

JSValue jsTreeQuit(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    if (SceneTree* tree = treeFromJs(ctx)) {
        tree->quit();
        return JS_NewBool(ctx, true);
    }
    return JS_NewBool(ctx, false);
}

JSValue jsTreeSetPaused(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree || argc < 1) return JS_NewBool(ctx, false);

    int paused = JS_ToBool(ctx, argv[0]);
    if (paused < 0) return JS_EXCEPTION;
    tree->setPaused(paused != 0);
    return JS_NewBool(ctx, true);
}

JSValue jsTreePaused(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    SceneTree* tree = treeFromJs(ctx);
    return JS_NewBool(ctx, tree && tree->paused());
}

struct SignalHit { void* obj = nullptr; const reflect::SignalDesc* desc = nullptr; };

SignalHit findSignalOnNode(Node* node, const std::string& name) {
    if (!node) return {};
    auto& reg = reflect::TypeRegistry::instance();
    if (const auto* d = reg.find(node->typeName()))
        if (const auto* s = d->findSignal(name)) return {node, s};
    for (const auto& b : node->behaviours())
        if (const auto* d = reg.find(b->typeName() ? b->typeName() : ""))
            if (const auto* s = d->findSignal(name)) return {b.get(), s};
    return {};
}

JSValue jsonToJs(JSContext* ctx, const nlohmann::json& v) {
    if (v.is_boolean()) return JS_NewBool(ctx, v.get<bool>());
    if (v.is_number_integer()) return JS_NewInt64(ctx, v.get<int64_t>());
    if (v.is_number()) return JS_NewFloat64(ctx, v.get<double>());
    if (v.is_string()) return JS_NewString(ctx, v.get<std::string>().c_str());
    return JS_NULL;
}

nlohmann::json jsToJson(JSContext* ctx, JSValueConst v) {
    if (JS_IsBool(v)) return JS_ToBool(ctx, v) != 0;
    if (JS_IsNumber(v)) { double d = 0.0; JS_ToFloat64(ctx, &d, v); return d; }
    if (JS_IsString(v)) {
        const char* s = JS_ToCString(ctx, v);
        nlohmann::json j = s ? std::string(s) : std::string();
        JS_FreeCString(ctx, s);
        return j;
    }
    return nullptr;
}

JSValue jsNodeOn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    JsContext* self = JsContext::fromRaw(ctx);
    if (!node || !self || argc < 2 || !JS_IsFunction(ctx, argv[1])) return JS_NewBool(ctx, false);

    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_NewBool(ctx, false);
    std::string signalName = name;
    JS_FreeCString(ctx, name);

    SignalHit hit = findSignalOnNode(node, signalName);
    if (!hit.desc) {
        Log::warn("[JS] node.on: no signal '", signalName, "' on node '", node->name(), "'");
        return JS_NewBool(ctx, false);
    }

    // Retain one reference to the callback; the subscription frees it on context
    // teardown (hot-reload/destroy), which also disconnects this handler.
    JSValue callback = JS_DupValue(ctx, argv[1]);
    Connection conn = hit.desc->connect(hit.obj, [ctx, callback](const nlohmann::json& args) {
        std::vector<JSValue> jsArgs;
        jsArgs.reserve(args.size());
        for (const auto& a : args) jsArgs.push_back(jsonToJs(ctx, a));
        JSValue result = JS_Call(ctx, callback, JS_UNDEFINED,
                                 static_cast<int>(jsArgs.size()), jsArgs.data());
        if (JS_IsException(result)) {
            JSValue exc = JS_GetException(ctx);
            const char* msg = JS_ToCString(ctx, exc);
            Log::error("[JS] signal handler threw: ", msg ? msg : "unknown");
            JS_FreeCString(ctx, msg);
            JS_FreeValue(ctx, exc);
        }
        JS_FreeValue(ctx, result);
        for (JSValue v : jsArgs) JS_FreeValue(ctx, v);
    });
    self->retainSignalSubscription(std::move(conn), callback);
    return JS_NewBool(ctx, true);
}

JSValue jsNodeEmit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    Node* node = nodeFromJs(ctx);
    if (!node || argc < 1) return JS_NewBool(ctx, false);

    const char* name = JS_ToCString(ctx, argv[0]);
    if (!name) return JS_NewBool(ctx, false);
    std::string signalName = name;
    JS_FreeCString(ctx, name);

    SignalHit hit = findSignalOnNode(node, signalName);
    if (!hit.desc || !hit.desc->emit) {
        Log::warn("[JS] node.emit: no signal '", signalName, "' on node '", node->name(), "'");
        return JS_NewBool(ctx, false);
    }

    nlohmann::json arr = nlohmann::json::array();
    for (int i = 1; i < argc; ++i) arr.push_back(jsToJson(ctx, argv[i]));
    hit.desc->emit(hit.obj, arr);
    return JS_NewBool(ctx, true);
}

} // namespace

void JsEngineBindings::installForBehaviour(JsContext& context, Behaviour& behaviour) {
    JSContext* ctx = context.raw();
    context.setOpaque(&behaviour);

    JSValue global = JS_GetGlobalObject(ctx);
    installAssetHandleClass(ctx);

    JSValue node = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, node, "getName", JS_NewCFunction(ctx, jsNodeGetName, "getName", 0));
    JS_SetPropertyStr(ctx, node, "setName", JS_NewCFunction(ctx, jsNodeSetName, "setName", 1));
    JS_SetPropertyStr(ctx, node, "getPosition", JS_NewCFunction(ctx, jsNodeGetPosition, "getPosition", 0));
    JS_SetPropertyStr(ctx, node, "setPosition", JS_NewCFunction(ctx, jsNodeSetPosition, "setPosition", 3));
    JS_SetPropertyStr(ctx, node, "translate", JS_NewCFunction(ctx, jsNodeTranslate, "translate", 3));
    JS_SetPropertyStr(ctx, node, "setEnabled", JS_NewCFunction(ctx, jsNodeSetEnabled, "setEnabled", 1));
    JS_SetPropertyStr(ctx, node, "queueFree", JS_NewCFunction(ctx, jsNodeQueueFree, "queueFree", 0));
    JS_SetPropertyStr(ctx, node, "setText", JS_NewCFunction(ctx, jsNodeSetText, "setText", 1));
    JS_SetPropertyStr(ctx, node, "getText", JS_NewCFunction(ctx, jsNodeGetText, "getText", 0));
    JS_SetPropertyStr(ctx, node, "addToGroup", JS_NewCFunction(ctx, jsNodeAddToGroup, "addToGroup", 1));
    JS_SetPropertyStr(ctx, node, "removeFromGroup", JS_NewCFunction(ctx, jsNodeRemoveFromGroup, "removeFromGroup", 1));
    JS_SetPropertyStr(ctx, node, "isInGroup", JS_NewCFunction(ctx, jsNodeIsInGroup, "isInGroup", 1));
    JS_SetPropertyStr(ctx, node, "on", JS_NewCFunction(ctx, jsNodeOn, "on", 2));
    JS_SetPropertyStr(ctx, node, "emit", JS_NewCFunction(ctx, jsNodeEmit, "emit", 1));
    JS_SetPropertyStr(ctx, global, "node", node);

    JSValue time = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, time, "delta", JS_NewCFunction(ctx, jsTimeDelta, "delta", 0));
    JS_SetPropertyStr(ctx, time, "elapsed", JS_NewCFunction(ctx, jsTimeElapsed, "elapsed", 0));
    JsTimerBindings::install(context, behaviour, time);
    JS_SetPropertyStr(ctx, global, "time", time);

    JSValue input = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, input, "isHeld", JS_NewCFunction(ctx, jsInputIsHeld, "isHeld", 1));
    JS_SetPropertyStr(ctx, input, "justPressed", JS_NewCFunction(ctx, jsInputJustPressed, "justPressed", 1));
    JS_SetPropertyStr(ctx, input, "justReleased", JS_NewCFunction(ctx, jsInputJustReleased, "justReleased", 1));
    JS_SetPropertyStr(ctx, input, "strength", JS_NewCFunction(ctx, jsInputStrength, "strength", 1));
    JS_SetPropertyStr(ctx, input, "axis", JS_NewCFunction(ctx, jsInputAxis, "axis", 2));
    JS_SetPropertyStr(ctx, input, "vector", JS_NewCFunction(ctx, jsInputVector, "vector", 4));
    JS_SetPropertyStr(ctx, input, "inject", JS_NewCFunction(ctx, jsInputInject, "inject", 2));
    JS_SetPropertyStr(ctx, input, "mousePosition", JS_NewCFunction(ctx, jsInputMousePosition, "mousePosition", 0));
    JS_SetPropertyStr(ctx, input, "mouseDelta", JS_NewCFunction(ctx, jsInputMouseDelta, "mouseDelta", 0));
    JS_SetPropertyStr(ctx, global, "input", input);

    JSValue tree = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, tree, "changeScene", JS_NewCFunction(ctx, jsTreeChangeScene, "changeScene", 1));
    JS_SetPropertyStr(ctx, tree, "reloadScene", JS_NewCFunction(ctx, jsTreeReloadScene, "reloadScene", 0));
    JS_SetPropertyStr(ctx, tree, "quit", JS_NewCFunction(ctx, jsTreeQuit, "quit", 0));
    JS_SetPropertyStr(ctx, tree, "setPaused", JS_NewCFunction(ctx, jsTreeSetPaused, "setPaused", 1));
    JS_SetPropertyStr(ctx, tree, "paused", JS_NewCFunction(ctx, jsTreePaused, "paused", 0));
    JS_SetPropertyStr(ctx, global, "tree", tree);

    JSValue assets = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, assets, "load", JS_NewCFunction(ctx, jsAssetsLoad, "load", 2));
    JS_SetPropertyStr(ctx, global, "assets", assets);

    JSValue audio = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, audio, "play", JS_NewCFunction(ctx, jsAudioPlay, "play", 1));
    JS_SetPropertyStr(ctx, global, "audio", audio);

    JSValue storage = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, storage, "save", JS_NewCFunction(ctx, jsStorageSave, "save", 2));
    JS_SetPropertyStr(ctx, storage, "load", JS_NewCFunction(ctx, jsStorageLoad, "load", 1));
    JS_SetPropertyStr(ctx, storage, "has", JS_NewCFunction(ctx, jsStorageHas, "has", 1));
    JS_SetPropertyStr(ctx, storage, "remove", JS_NewCFunction(ctx, jsStorageRemove, "remove", 1));
    JS_SetPropertyStr(ctx, global, "storage", storage);

    JS_FreeValue(ctx, global);
}

} // namespace saida
