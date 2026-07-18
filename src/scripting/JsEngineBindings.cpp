#include "scripting/JsEngineBindings.hpp"

#include "audio/AudioManager.hpp"
#include "core/AtomicFile.hpp"
#include "core/Input.hpp"
#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/PlatformCaps.hpp"
#include "core/Reflection.hpp"
#include "core/Time.hpp"
#include "graphics/ResourceManager.hpp"
#include "physics/CollisionObjectNode.hpp"
#include "physics/PhysicsWorld.hpp"
#include "project/AssetLoader.hpp"
#include "project/PlayerStorage.hpp"
#include "scene/Behaviour.hpp"
#include "scene/Node.hpp"
#include "scene/Scene.hpp"
#include "scene/SceneTree.hpp"
#include "scene/UITextNode.hpp"
#include "scripting/JsContext.hpp"
#include "scripting/ScriptBehaviour.hpp"
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
JSValue makeNodeRef(JSContext* ctx, Node* target);

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

// ---- storage: persistance par slot déléguée à PlayerStorage --------------
// Le JS sérialise lui-même (JSON.stringify/parse) ; le moteur ne stocke que des
// chaînes opaques. PlayerStorage ajoute l'enveloppe versionnée, la metadata de
// slot, la séparation progression/préférences et les quotas ; il refuse une
// sauvegarde future ou falsifiée au lieu de la mal relire. Les répertoires sont
// résolus sous la racine projet → identiques dans l'éditeur, le runtime packagé
// (racine = dossier de l'exe) et le player web (IDBFS/MEMFS).
//
// storage.* opère sur la progression, storage.prefs.* sur les préférences.
// Le dernier échec d'une opération non levée (quota, io, corruption) est
// consultable via storage.lastError() ; save/remove renvoient un booléen pour
// préserver le contrat historique.

// Dernier statut/diagnostic d'une opération storage du contexte courant.
thread_local StorageStatus gLastStorageStatus = StorageStatus::Ok;
thread_local std::string gLastStorageError;

void recordStorageResult(const StorageResult& r) {
    gLastStorageStatus = r.status;
    gLastStorageError = r.error;
}

PlayerStorage storageFor(SceneTree& tree) {
    // Jeu packagé : saves/prefs sous le dossier utilisateur de l'OS (jamais à côté
    // de l'exe). Éditeur/dev et web : racine projet (userSaveRoot() renvoie vide).
    const std::string base = userSaveRoot();
    if (!base.empty())
        return PlayerStorage(std::filesystem::path(base) / "saves",
                             std::filesystem::path(base) / "prefs");
    return PlayerStorage(std::filesystem::path(tree.resolveProjectPath("saves")),
                         std::filesystem::path(tree.resolveProjectPath("prefs")));
}

void syncfsAfterMutation() {
#ifdef __EMSCRIPTEN__
    // saves/ et prefs/ vivent sur IDBFS (monté par le shell) : flush vers
    // IndexedDB après toute écriture durable.
    EM_ASM({ FS.syncfs(false, function() {}); });
#else
    (void)0;
#endif
}

// Lit le slot (argv[0]) en le validant ; renvoie false et pose outError si absent
// ou vide. Ne valide pas le motif : PlayerStorage le fait et renvoie un statut.
bool readSlotArg(JSContext* ctx, int argc, JSValueConst* argv, std::string& outSlot,
                 JSValue& outError) {
    outError = JS_UNDEFINED;
    if (argc < 1) {
        outError = JS_ThrowTypeError(ctx, "storage: missing slot name");
        return false;
    }
    const char* raw = JS_ToCString(ctx, argv[0]);
    if (!raw) {
        outError = JS_EXCEPTION;
        return false;
    }
    outSlot.assign(raw);
    JS_FreeCString(ctx, raw);
    return true;
}

SceneTree* storageTree(JSContext* ctx, JSValue& outError) {
    outError = JS_UNDEFINED;
    SceneTree* tree = treeFromJs(ctx);
    if (!tree)
        outError = JS_ThrowInternalError(ctx, "storage requires a mounted SceneTree");
    return tree;
}

JSValue metaToJs(JSContext* ctx, const StorageMeta& meta) {
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "kind", JS_NewString(ctx, toString(meta.kind)));
    JS_SetPropertyStr(ctx, o, "bytes", JS_NewInt64(ctx, meta.bytes));
    JS_SetPropertyStr(ctx, o, "savedAt", JS_NewInt64(ctx, meta.savedAt));
    JS_SetPropertyStr(ctx, o, "dataVersion", JS_NewInt32(ctx, meta.dataVersion));
    JS_SetPropertyStr(ctx, o, "schema", JS_NewInt32(ctx, meta.schema));
    return o;
}

JSValue jsStorageSaveKind(JSContext* ctx, StorageKind kind, int argc,
                          JSValueConst* argv) {
    JSValue error;
    SceneTree* tree = storageTree(ctx, error);
    if (!tree) return error;
    std::string slot;
    if (!readSlotArg(ctx, argc, argv, slot, error)) return error;
    if (argc < 2) return JS_ThrowTypeError(ctx, "storage.save(slot, jsonString)");
    const char* value = JS_ToCString(ctx, argv[1]);
    if (!value) return JS_EXCEPTION;
    // Version applicative optionnelle (migrations côté jeu), défaut 0.
    int dataVersion = 0;
    if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2]))
        JS_ToInt32(ctx, &dataVersion, argv[2]);

    PlayerStorage store = storageFor(*tree);
    const StorageResult r = store.save(kind, slot, value, dataVersion);
    JS_FreeCString(ctx, value);
    recordStorageResult(r);
    if (!r)
        Log::warn("[JS] storage.save: ", toString(r.status), ": ", r.error);
    else
        syncfsAfterMutation();
    return JS_NewBool(ctx, static_cast<bool>(r));
}

JSValue jsStorageLoadKind(JSContext* ctx, StorageKind kind, int argc,
                          JSValueConst* argv) {
    JSValue error;
    SceneTree* tree = storageTree(ctx, error);
    if (!tree) return error;
    std::string slot;
    if (!readSlotArg(ctx, argc, argv, slot, error)) return error;
    const StorageResult r = storageFor(*tree).load(kind, slot);
    recordStorageResult(r);
    if (r.status == StorageStatus::NotFound) return JS_NULL;
    if (!r) {
        // Enveloppe corrompue/refusée ou slot invalide : diagnostic loggé, null
        // renvoyé (consultable via storage.lastError()).
        Log::warn("[JS] storage.load: ", toString(r.status), ": ", r.error);
        return JS_NULL;
    }
    return JS_NewStringLen(ctx, r.payload.data(), r.payload.size());
}

JSValue jsStorageHasKind(JSContext* ctx, StorageKind kind, int argc,
                         JSValueConst* argv) {
    JSValue error;
    SceneTree* tree = storageTree(ctx, error);
    if (!tree) return error;
    std::string slot;
    if (!readSlotArg(ctx, argc, argv, slot, error)) return error;
    return JS_NewBool(ctx, storageFor(*tree).has(kind, slot));
}

JSValue jsStorageRemoveKind(JSContext* ctx, StorageKind kind, int argc,
                            JSValueConst* argv) {
    JSValue error;
    SceneTree* tree = storageTree(ctx, error);
    if (!tree) return error;
    std::string slot;
    if (!readSlotArg(ctx, argc, argv, slot, error)) return error;
    PlayerStorage store = storageFor(*tree);
    const StorageResult r = store.remove(kind, slot);
    recordStorageResult(r);
    if (r.status == StorageStatus::IoError)
        Log::warn("[JS] storage.remove: ", r.error);
    if (r.found) syncfsAfterMutation();
    return JS_NewBool(ctx, r.found);
}

JSValue jsStorageInfoKind(JSContext* ctx, StorageKind kind, int argc,
                          JSValueConst* argv) {
    JSValue error;
    SceneTree* tree = storageTree(ctx, error);
    if (!tree) return error;
    std::string slot;
    if (!readSlotArg(ctx, argc, argv, slot, error)) return error;
    const StorageResult r = storageFor(*tree).info(kind, slot);
    recordStorageResult(r);
    if (!r.found || !r) return JS_NULL;
    return metaToJs(ctx, r.meta);
}

JSValue jsStorageListKind(JSContext* ctx, StorageKind kind) {
    JSValue error;
    SceneTree* tree = storageTree(ctx, error);
    if (!tree) return error;
    const std::vector<std::string> slots = storageFor(*tree).list(kind);
    JSValue arr = JS_NewArray(ctx);
    uint32_t i = 0;
    for (const std::string& s : slots)
        JS_SetPropertyUint32(ctx, arr, i++, JS_NewStringLen(ctx, s.data(), s.size()));
    return arr;
}

// storage.* (progression)
JSValue jsStorageSave(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageSaveKind(ctx, StorageKind::Progress, argc, argv);
}
JSValue jsStorageLoad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageLoadKind(ctx, StorageKind::Progress, argc, argv);
}
JSValue jsStorageHas(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageHasKind(ctx, StorageKind::Progress, argc, argv);
}
JSValue jsStorageRemove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageRemoveKind(ctx, StorageKind::Progress, argc, argv);
}
JSValue jsStorageInfo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageInfoKind(ctx, StorageKind::Progress, argc, argv);
}
JSValue jsStorageList(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return jsStorageListKind(ctx, StorageKind::Progress);
}

// storage.prefs.* (préférences)
JSValue jsPrefSave(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageSaveKind(ctx, StorageKind::Preference, argc, argv);
}
JSValue jsPrefLoad(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageLoadKind(ctx, StorageKind::Preference, argc, argv);
}
JSValue jsPrefHas(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageHasKind(ctx, StorageKind::Preference, argc, argv);
}
JSValue jsPrefRemove(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageRemoveKind(ctx, StorageKind::Preference, argc, argv);
}
JSValue jsPrefInfo(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return jsStorageInfoKind(ctx, StorageKind::Preference, argc, argv);
}
JSValue jsPrefList(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return jsStorageListKind(ctx, StorageKind::Preference);
}

JSValue jsStorageLastError(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    if (gLastStorageStatus == StorageStatus::Ok) return JS_NULL;
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "status",
                      JS_NewString(ctx, toString(gLastStorageStatus)));
    JS_SetPropertyStr(ctx, o, "message",
                      JS_NewStringLen(ctx, gLastStorageError.data(),
                                      gLastStorageError.size()));
    return o;
}

JSValue jsAssetsStats(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) return JS_ThrowInternalError(ctx, "assets.stats requires a mounted SceneTree");
    const AssetLoadStats stats = tree->resources().assetLoader().stats();
    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "live", JS_NewInt64(ctx, stats.live));
    JS_SetPropertyStr(ctx, o, "queued", JS_NewInt64(ctx, stats.queued));
    JS_SetPropertyStr(ctx, o, "loading", JS_NewInt64(ctx, stats.loading));
    JS_SetPropertyStr(ctx, o, "ready", JS_NewInt64(ctx, stats.ready));
    JS_SetPropertyStr(ctx, o, "failed", JS_NewInt64(ctx, stats.failed));
    JS_SetPropertyStr(ctx, o, "residentBytes",
                      JS_NewInt64(ctx, static_cast<int64_t>(stats.residentBytes)));
    JS_SetPropertyStr(ctx, o, "budgetBytes",
                      JS_NewInt64(ctx, static_cast<int64_t>(stats.budgetBytes)));
    // Octets GPU des ressources résidentes (textures/meshes chargés par asset)
    // — champ additif (compat OK), critère de fuite du chantier 3 en E2E.
    JS_SetPropertyStr(ctx, o, "gpuResidentBytes",
                      JS_NewInt64(ctx, static_cast<int64_t>(tree->resources().gpuResidentBytes())));
    return o;
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

JSValue jsNodeSetText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
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
}

JSValue jsNodeGetText(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    auto* text = dynamic_cast<UITextNode*>(nodeFromJs(ctx));
    if (!text)
        return JS_ThrowTypeError(ctx, "node.getText: script is not attached to a UITextNode");
    return JS_NewString(ctx, text->text().c_str());
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

JSValue jsTreeAutoload(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) return JS_ThrowInternalError(ctx, "tree.autoload requires a mounted SceneTree");
    if (argc < 1) return JS_ThrowTypeError(ctx, "tree.autoload(name)");
    const char* raw = JS_ToCString(ctx, argv[0]);
    if (!raw) return JS_EXCEPTION;
    Node* target = tree->autoloadNode(raw);
    JS_FreeCString(ctx, raw);
    return target ? makeNodeRef(ctx, target) : JS_NULL;
}

JSValue jsTreeFirstInGroup(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) return JS_ThrowInternalError(ctx, "tree.firstInGroup requires a mounted SceneTree");
    if (argc < 1) return JS_ThrowTypeError(ctx, "tree.firstInGroup(name)");
    const char* raw = JS_ToCString(ctx, argv[0]);
    if (!raw) return JS_EXCEPTION;
    Node* target = tree->firstInGroup(raw);
    JS_FreeCString(ctx, raw);
    return target ? makeNodeRef(ctx, target) : JS_NULL;
}

JSValue jsTreeNodesInGroup(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) return JS_ThrowInternalError(ctx, "tree.nodesInGroup requires a mounted SceneTree");
    if (argc < 1) return JS_ThrowTypeError(ctx, "tree.nodesInGroup(name)");
    const char* raw = JS_ToCString(ctx, argv[0]);
    if (!raw) return JS_EXCEPTION;
    const std::vector<Node*>& nodes = tree->group(raw);
    JS_FreeCString(ctx, raw);
    JSValue result = JS_NewArray(ctx);
    for (uint32_t i = 0; i < nodes.size(); ++i)
        JS_SetPropertyUint32(ctx, result, i, makeNodeRef(ctx, nodes[i]));
    return result;
}

JSValue jsTreeNodeById(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) return JS_ThrowInternalError(ctx, "tree.nodeById requires a mounted SceneTree");
    if (argc < 1) return JS_ThrowTypeError(ctx, "tree.nodeById(id)");
    uint64_t id = 0;
    if (JS_ToBigUint64(ctx, &id, argv[0]) != 0) return JS_EXCEPTION;
    Node* target = tree->nodeById(id);
    return target ? makeNodeRef(ctx, target) : JS_NULL;
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
    if (v.is_null()) return JS_NULL;
    if (v.is_boolean()) return JS_NewBool(ctx, v.get<bool>());
    if (v.is_number_integer()) return JS_NewInt64(ctx, v.get<int64_t>());
    if (v.is_number_unsigned()) return JS_NewFloat64(ctx, static_cast<double>(v.get<uint64_t>()));
    if (v.is_number()) return JS_NewFloat64(ctx, v.get<double>());
    if (v.is_string()) return JS_NewString(ctx, v.get<std::string>().c_str());
    if (v.is_array()) {
        JSValue array = JS_NewArray(ctx);
        uint32_t index = 0;
        for (const auto& item : v)
            JS_SetPropertyUint32(ctx, array, index++, jsonToJs(ctx, item));
        return array;
    }
    JSValue object = JS_NewObject(ctx);
    for (auto it = v.begin(); it != v.end(); ++it)
        JS_SetPropertyStr(ctx, object, it.key().c_str(), jsonToJs(ctx, it.value()));
    return object;
}

bool jsToJson(JSContext* ctx, JSValueConst v, nlohmann::json& out) {
    if (JS_IsUndefined(v) || JS_IsNull(v)) {
        out = nullptr;
        return true;
    }
    if (JS_IsBool(v)) {
        out = JS_ToBool(ctx, v) != 0;
        return true;
    }
    if (JS_IsNumber(v)) {
        double d = 0.0;
        if (JS_ToFloat64(ctx, &d, v) != 0) return false;
        out = d;
        return true;
    }
    if (JS_IsString(v)) {
        const char* s = JS_ToCString(ctx, v);
        if (!s) return false;
        out = std::string(s);
        JS_FreeCString(ctx, s);
        return true;
    }

    JSValue encoded = JS_JSONStringify(ctx, v, JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(encoded)) {
        JS_FreeValue(ctx, encoded);
        JSValue exception = JS_GetException(ctx);
        JS_FreeValue(ctx, exception);
        return false;
    }
    const char* text = JS_ToCString(ctx, encoded);
    if (!text) {
        JS_FreeValue(ctx, encoded);
        return false;
    }
    out = nlohmann::json::parse(text, nullptr, false);
    JS_FreeCString(ctx, text);
    JS_FreeValue(ctx, encoded);
    return !out.is_discarded();
}

JSValue subscribeNodeSignal(JSContext* ctx, Node* node, int argc, JSValueConst* argv) {
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

JSValue emitNodeSignal(JSContext* ctx, Node* node, int argc, JSValueConst* argv) {
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
    for (int i = 1; i < argc; ++i) {
        nlohmann::json value;
        if (!jsToJson(ctx, argv[i], value))
            return JS_ThrowTypeError(ctx, "signal arguments must be JSON-compatible");
        arr.push_back(std::move(value));
    }
    hit.desc->emit(hit.obj, arr);
    return JS_NewBool(ctx, true);
}

JSValue jsNodeOn(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return subscribeNodeSignal(ctx, nodeFromJs(ctx), argc, argv);
}

JSValue jsNodeEmit(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    return emitNodeSignal(ctx, nodeFromJs(ctx), argc, argv);
}

Node* nodeRefTarget(JSContext* ctx, JSValueConst* data) {
    SceneTree* tree = treeFromJs(ctx);
    if (!tree) return nullptr;
    uint64_t id = 0;
    if (JS_ToBigUint64(ctx, &id, data[0]) != 0) return nullptr;
    return tree->nodeById(id);
}

enum NodeRefMethod {
    NodeRefValid,
    NodeRefGetName,
    NodeRefSetName,
    NodeRefGetPosition,
    NodeRefSetPosition,
    NodeRefTranslate,
    NodeRefSetEnabled,
    NodeRefQueueFree,
    NodeRefSetText,
    NodeRefGetText,
    NodeRefAddToGroup,
    NodeRefRemoveFromGroup,
    NodeRefIsInGroup,
};

JSValue jsNodeRefMethod(JSContext* ctx, JSValueConst, int argc,
                        JSValueConst* argv, int magic, JSValueConst* data) {
    Node* target = nodeRefTarget(ctx, data);
    if (magic == NodeRefValid) return JS_NewBool(ctx, target != nullptr);
    if (!target) return JS_ThrowReferenceError(ctx, "NodeRef target no longer exists");

    switch (magic) {
    case NodeRefGetName:
        return JS_NewString(ctx, target->name().c_str());
    case NodeRefSetName: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "NodeRef.setName(name)");
        const char* value = JS_ToCString(ctx, argv[0]);
        if (!value) return JS_EXCEPTION;
        target->setName(value);
        JS_FreeCString(ctx, value);
        return JS_NewBool(ctx, true);
    }
    case NodeRefGetPosition:
        return makeVec3(ctx, target->transform().position);
    case NodeRefSetPosition:
    case NodeRefTranslate: {
        glm::vec3 value(0.0f);
        if (!readVec3Args(ctx, argc, argv, value))
            return JS_ThrowTypeError(ctx, "expected ({x,y,z}) or (x,y,z)");
        if (magic == NodeRefSetPosition) target->transform().position = value;
        else target->transform().position += value;
        return JS_NewBool(ctx, true);
    }
    case NodeRefSetEnabled: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "NodeRef.setEnabled(enabled)");
        const int enabled = JS_ToBool(ctx, argv[0]);
        if (enabled < 0) return JS_EXCEPTION;
        target->setEnabled(enabled != 0);
        return JS_NewBool(ctx, true);
    }
    case NodeRefQueueFree:
        target->queueFree();
        return JS_UNDEFINED;
    case NodeRefSetText: {
        auto* text = dynamic_cast<UITextNode*>(target);
        if (!text) return JS_ThrowTypeError(ctx, "NodeRef target is not a UITextNode");
        if (argc < 1) return JS_ThrowTypeError(ctx, "NodeRef.setText(text)");
        const char* value = JS_ToCString(ctx, argv[0]);
        if (!value) return JS_EXCEPTION;
        text->setText(value);
        JS_FreeCString(ctx, value);
        return JS_UNDEFINED;
    }
    case NodeRefGetText: {
        auto* text = dynamic_cast<UITextNode*>(target);
        if (!text) return JS_ThrowTypeError(ctx, "NodeRef target is not a UITextNode");
        return JS_NewString(ctx, text->text().c_str());
    }
    case NodeRefAddToGroup:
    case NodeRefRemoveFromGroup:
    case NodeRefIsInGroup: {
        if (argc < 1) return JS_ThrowTypeError(ctx, "NodeRef group method requires a name");
        const char* group = JS_ToCString(ctx, argv[0]);
        if (!group) return JS_EXCEPTION;
        bool result = true;
        if (magic == NodeRefAddToGroup) target->addToGroup(group);
        else if (magic == NodeRefRemoveFromGroup) target->removeFromGroup(group);
        else result = target->isInGroup(group);
        JS_FreeCString(ctx, group);
        return JS_NewBool(ctx, result);
    }
    default:
        return JS_ThrowInternalError(ctx, "unknown NodeRef method");
    }
}

JSValue jsNodeRefOn(JSContext* ctx, JSValueConst, int argc,
                    JSValueConst* argv, int, JSValueConst* data) {
    Node* target = nodeRefTarget(ctx, data);
    if (!target) return JS_ThrowReferenceError(ctx, "NodeRef target no longer exists");
    return subscribeNodeSignal(ctx, target, argc, argv);
}

JSValue jsNodeRefEmit(JSContext* ctx, JSValueConst, int argc,
                      JSValueConst* argv, int, JSValueConst* data) {
    Node* target = nodeRefTarget(ctx, data);
    if (!target) return JS_ThrowReferenceError(ctx, "NodeRef target no longer exists");
    return emitNodeSignal(ctx, target, argc, argv);
}

JSValue jsNodeRefCall(JSContext* ctx, JSValueConst, int argc,
                      JSValueConst* argv, int, JSValueConst* data) {
    Node* target = nodeRefTarget(ctx, data);
    if (!target) return JS_ThrowReferenceError(ctx, "NodeRef target no longer exists");
    if (argc < 1) return JS_ThrowTypeError(ctx, "NodeRef.call(exportName, ...args)");

    const char* rawName = JS_ToCString(ctx, argv[0]);
    if (!rawName) return JS_EXCEPTION;
    const std::string exportName(rawName);
    JS_FreeCString(ctx, rawName);

    nlohmann::json args = nlohmann::json::array();
    for (int i = 1; i < argc; ++i) {
        nlohmann::json value;
        if (!jsToJson(ctx, argv[i], value))
            return JS_ThrowTypeError(ctx, "NodeRef.call arguments must be JSON-compatible");
        args.push_back(std::move(value));
    }

    for (const auto& behaviour : target->behaviours()) {
        auto* script = dynamic_cast<ScriptBehaviour*>(behaviour.get());
        if (!script) continue;
        nlohmann::json result;
        const ScriptCallStatus status = script->callExport(exportName, args, result);
        if (status == ScriptCallStatus::Succeeded) return jsonToJs(ctx, result);
        if (status == ScriptCallStatus::Failed)
            return JS_ThrowInternalError(ctx, "NodeRef.call('%s') failed", exportName.c_str());
    }
    return JS_ThrowTypeError(ctx, "NodeRef target has no callable export '%s'",
                             exportName.c_str());
}

JSValue makeNodeRef(JSContext* ctx, Node* target);  // défini plus bas

// ---- physics: queries de scène (raycast / overlap) -----------------------
// Parité desktop/web : le player web embarque le même Jolt (wasm). Le monde
// physique n'existe qu'en Play avec au moins un corps; sans monde, les queries
// répondent « rien » (null / liste vide) plutôt qu'une erreur — un script peut
// interroger une scène sans physique sans se protéger.

PhysicsWorld* physicsFromJs(JSContext* ctx) {
    SceneTree* tree = treeFromJs(ctx);
    return tree ? tree->world().physics() : nullptr;
}

// Lit un {x,y,z} à l'index donné; jette une TypeError sinon.
bool readVec3At(JSContext* ctx, int argc, JSValueConst* argv, int index, glm::vec3& out) {
    if (index >= argc || !JS_IsObject(argv[index])) return false;
    return readVec3Args(ctx, 1, argv + index, out);
}

// Options communes { hitSensors?: bool, ignoreSelf?: bool } (défauts: false, true).
struct JsQueryOptions {
    bool hitSensors = false;
    bool ignoreSelf = true;
};

JsQueryOptions readQueryOptions(JSContext* ctx, int argc, JSValueConst* argv, int index) {
    JsQueryOptions out;
    if (index >= argc || !JS_IsObject(argv[index])) return out;
    JSValue sensors = JS_GetPropertyStr(ctx, argv[index], "hitSensors");
    if (!JS_IsUndefined(sensors)) out.hitSensors = JS_ToBool(ctx, sensors) > 0;
    JS_FreeValue(ctx, sensors);
    JSValue self = JS_GetPropertyStr(ctx, argv[index], "ignoreSelf");
    if (!JS_IsUndefined(self)) out.ignoreSelf = JS_ToBool(ctx, self) > 0;
    JS_FreeValue(ctx, self);
    return out;
}

// Le corps du nœud appelant (ou de son ancêtre le plus proche) — exclu par
// défaut des queries pour qu'un script ne se touche pas lui-même.
JPH::BodyID callerBodyId(JSContext* ctx) {
    for (Node* n = nodeFromJs(ctx); n; n = n->parent())
        if (CollisionObjectNode* body = n->asCollisionObject()) return body->bodyId();
    return JPH::BodyID();
}

QueryFilter makeQueryFilter(JSContext* ctx, const JsQueryOptions& opts) {
    QueryFilter filter;
    filter.hitSensors = opts.hitSensors;
    if (opts.ignoreSelf) filter.ignore = callerBodyId(ctx);
    return filter;
}

JSValue physicsNodeRef(JSContext* ctx, PhysicsWorld& world, JPH::BodyID body) {
    auto* node = static_cast<CollisionObjectNode*>(world.bodyUserData(body));
    return node ? makeNodeRef(ctx, node) : JS_NULL;
}

// physics.raycast(origin, direction, maxDistance, opts?) →
//   null | { point, normal, distance, node: NodeRef|null }
JSValue jsPhysicsRaycast(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    glm::vec3 origin(0.0f);
    glm::vec3 direction(0.0f);
    double maxDistance = 0.0;
    if (!readVec3At(ctx, argc, argv, 0, origin) ||
        !readVec3At(ctx, argc, argv, 1, direction) ||
        argc < 3 || !toNumber(ctx, argv[2], maxDistance)) {
        return JS_ThrowTypeError(
            ctx, "physics.raycast(origin{x,y,z}, direction{x,y,z}, maxDistance, opts?)");
    }
    if (glm::dot(direction, direction) < 1e-12f || !(maxDistance > 0.0))
        return JS_NULL;

    PhysicsWorld* world = physicsFromJs(ctx);
    if (!world) return JS_NULL;

    const QueryFilter filter = makeQueryFilter(ctx, readQueryOptions(ctx, argc, argv, 3));
    const RaycastHit hit = world->raycast(origin, glm::normalize(direction),
                                          static_cast<float>(maxDistance), filter);
    if (!hit.hit) return JS_NULL;

    JSValue o = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, o, "point", makeVec3(ctx, hit.point));
    JS_SetPropertyStr(ctx, o, "normal", makeVec3(ctx, hit.normal));
    JS_SetPropertyStr(ctx, o, "distance", JS_NewFloat64(ctx, hit.distance));
    JS_SetPropertyStr(ctx, o, "node", physicsNodeRef(ctx, *world, hit.body));
    return o;
}

// physics.overlapSphere(center, radius, opts?) → [NodeRef...]
JSValue jsPhysicsOverlapSphere(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    glm::vec3 center(0.0f);
    double radius = 0.0;
    if (!readVec3At(ctx, argc, argv, 0, center) ||
        argc < 2 || !toNumber(ctx, argv[1], radius)) {
        return JS_ThrowTypeError(ctx,
                                 "physics.overlapSphere(center{x,y,z}, radius, opts?)");
    }

    JSValue arr = JS_NewArray(ctx);
    PhysicsWorld* world = physicsFromJs(ctx);
    if (!world || !(radius > 0.0)) return arr;

    const QueryFilter filter = makeQueryFilter(ctx, readQueryOptions(ctx, argc, argv, 2));
    uint32_t i = 0;
    for (JPH::BodyID body : world->overlapSphere(center, static_cast<float>(radius), filter)) {
        JSValue ref = physicsNodeRef(ctx, *world, body);
        if (JS_IsNull(ref)) continue;  // corps sans nœud propriétaire
        JS_SetPropertyUint32(ctx, arr, i++, ref);
    }
    return arr;
}

// physics.available() → true dès que la capacité physique existe sur la
// plateforme (le monde lui-même est créé paresseusement en Play).
JSValue jsPhysicsAvailable(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    return JS_NewBool(ctx, platform::has(platform::Capability::Physics));
}

void setNodeRefMethod(JSContext* ctx, JSValue object, NodeId id,
                      const char* name, JSCFunctionData* fn, int length,
                      int magic = 0) {
    JSValue data = JS_NewBigUint64(ctx, id);
    JSValue method = JS_NewCFunctionData(ctx, fn, length, magic, 1, &data);
    JS_FreeValue(ctx, data);
    JS_SetPropertyStr(ctx, object, name, method);
}

JSValue makeNodeRef(JSContext* ctx, Node* target) {
    if (!target) return JS_NULL;
    JSValue object = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, object, "id", JS_NewBigUint64(ctx, target->id()));
    setNodeRefMethod(ctx, object, target->id(), "valid", jsNodeRefMethod, 0, NodeRefValid);
    setNodeRefMethod(ctx, object, target->id(), "getName", jsNodeRefMethod, 0, NodeRefGetName);
    setNodeRefMethod(ctx, object, target->id(), "setName", jsNodeRefMethod, 1, NodeRefSetName);
    setNodeRefMethod(ctx, object, target->id(), "getPosition", jsNodeRefMethod, 0, NodeRefGetPosition);
    setNodeRefMethod(ctx, object, target->id(), "setPosition", jsNodeRefMethod, 3, NodeRefSetPosition);
    setNodeRefMethod(ctx, object, target->id(), "translate", jsNodeRefMethod, 3, NodeRefTranslate);
    setNodeRefMethod(ctx, object, target->id(), "setEnabled", jsNodeRefMethod, 1, NodeRefSetEnabled);
    setNodeRefMethod(ctx, object, target->id(), "queueFree", jsNodeRefMethod, 0, NodeRefQueueFree);
    setNodeRefMethod(ctx, object, target->id(), "setText", jsNodeRefMethod, 1, NodeRefSetText);
    setNodeRefMethod(ctx, object, target->id(), "getText", jsNodeRefMethod, 0, NodeRefGetText);
    setNodeRefMethod(ctx, object, target->id(), "addToGroup", jsNodeRefMethod, 1, NodeRefAddToGroup);
    setNodeRefMethod(ctx, object, target->id(), "removeFromGroup", jsNodeRefMethod, 1, NodeRefRemoveFromGroup);
    setNodeRefMethod(ctx, object, target->id(), "isInGroup", jsNodeRefMethod, 1, NodeRefIsInGroup);
    setNodeRefMethod(ctx, object, target->id(), "on", jsNodeRefOn, 2);
    setNodeRefMethod(ctx, object, target->id(), "emit", jsNodeRefEmit, 1);
    setNodeRefMethod(ctx, object, target->id(), "call", jsNodeRefCall, 1);
    return object;
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
    JS_SetPropertyStr(ctx, tree, "autoload", JS_NewCFunction(ctx, jsTreeAutoload, "autoload", 1));
    JS_SetPropertyStr(ctx, tree, "firstInGroup", JS_NewCFunction(ctx, jsTreeFirstInGroup, "firstInGroup", 1));
    JS_SetPropertyStr(ctx, tree, "nodesInGroup", JS_NewCFunction(ctx, jsTreeNodesInGroup, "nodesInGroup", 1));
    JS_SetPropertyStr(ctx, tree, "nodeById", JS_NewCFunction(ctx, jsTreeNodeById, "nodeById", 1));
    JS_SetPropertyStr(ctx, global, "tree", tree);

    JSValue assets = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, assets, "load", JS_NewCFunction(ctx, jsAssetsLoad, "load", 2));
    JS_SetPropertyStr(ctx, assets, "stats", JS_NewCFunction(ctx, jsAssetsStats, "stats", 0));
    JS_SetPropertyStr(ctx, global, "assets", assets);

    JSValue audio = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, audio, "play", JS_NewCFunction(ctx, jsAudioPlay, "play", 1));
    JS_SetPropertyStr(ctx, global, "audio", audio);

    JSValue physics = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, physics, "available", JS_NewCFunction(ctx, jsPhysicsAvailable, "available", 0));
    JS_SetPropertyStr(ctx, physics, "raycast", JS_NewCFunction(ctx, jsPhysicsRaycast, "raycast", 4));
    JS_SetPropertyStr(ctx, physics, "overlapSphere", JS_NewCFunction(ctx, jsPhysicsOverlapSphere, "overlapSphere", 3));
    JS_SetPropertyStr(ctx, global, "physics", physics);

    JSValue storage = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, storage, "save", JS_NewCFunction(ctx, jsStorageSave, "save", 3));
    JS_SetPropertyStr(ctx, storage, "load", JS_NewCFunction(ctx, jsStorageLoad, "load", 1));
    JS_SetPropertyStr(ctx, storage, "has", JS_NewCFunction(ctx, jsStorageHas, "has", 1));
    JS_SetPropertyStr(ctx, storage, "remove", JS_NewCFunction(ctx, jsStorageRemove, "remove", 1));
    JS_SetPropertyStr(ctx, storage, "info", JS_NewCFunction(ctx, jsStorageInfo, "info", 1));
    JS_SetPropertyStr(ctx, storage, "list", JS_NewCFunction(ctx, jsStorageList, "list", 0));
    JS_SetPropertyStr(ctx, storage, "lastError", JS_NewCFunction(ctx, jsStorageLastError, "lastError", 0));
    // storage.prefs.* : préférences, namespace séparé de la progression.
    JSValue prefs = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, prefs, "save", JS_NewCFunction(ctx, jsPrefSave, "save", 3));
    JS_SetPropertyStr(ctx, prefs, "load", JS_NewCFunction(ctx, jsPrefLoad, "load", 1));
    JS_SetPropertyStr(ctx, prefs, "has", JS_NewCFunction(ctx, jsPrefHas, "has", 1));
    JS_SetPropertyStr(ctx, prefs, "remove", JS_NewCFunction(ctx, jsPrefRemove, "remove", 1));
    JS_SetPropertyStr(ctx, prefs, "info", JS_NewCFunction(ctx, jsPrefInfo, "info", 1));
    JS_SetPropertyStr(ctx, prefs, "list", JS_NewCFunction(ctx, jsPrefList, "list", 0));
    JS_SetPropertyStr(ctx, storage, "prefs", prefs);
    JS_SetPropertyStr(ctx, global, "storage", storage);

    JS_FreeValue(ctx, global);
}

} // namespace saida
