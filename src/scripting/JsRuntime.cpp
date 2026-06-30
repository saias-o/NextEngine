#include "scripting/JsRuntime.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "scripting/JsContext.hpp"

#include <quickjs.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace saida {

namespace {
constexpr size_t kJsMemoryLimit = 64ull * 1024ull * 1024ull;
constexpr size_t kJsStackLimit = 1024ull * 1024ull;
std::vector<std::string>* gModuleDependencyCapture = nullptr;

void recordModuleDependency(const std::filesystem::path& path) {
    if (!gModuleDependencyCapture) return;

    std::error_code ec;
    std::filesystem::path absolute = std::filesystem::absolute(path, ec);
    std::string normalized = (ec ? path : absolute).generic_string();
    if (std::find(gModuleDependencyCapture->begin(), gModuleDependencyCapture->end(), normalized) == gModuleDependencyCapture->end()) {
        gModuleDependencyCapture->push_back(std::move(normalized));
    }
}

std::filesystem::path resolveModulePath(const char* baseName, const char* moduleName) {
    std::string normalized = pathFromFileUrl(moduleName ? moduleName : "");
    std::filesystem::path requested(normalized);

    if (requested.is_absolute() && std::filesystem::exists(requested)) return requested;
    if (std::filesystem::exists(requested)) return std::filesystem::absolute(requested);

    if (baseName && baseName[0] != '\0') {
        std::filesystem::path base(pathFromFileUrl(baseName));
        std::filesystem::path relative = base.parent_path() / requested;
        if (std::filesystem::exists(relative)) return std::filesystem::absolute(relative);
    }

    std::filesystem::path assetCandidate(assetPath(normalized));
    if (std::filesystem::exists(assetCandidate)) return assetCandidate;

    return requested;
}

char* jsModuleNormalize(JSContext* ctx, const char* baseName, const char* moduleName, void*) {
    std::filesystem::path resolved = resolveModulePath(baseName, moduleName);
    return js_strdup(ctx, resolved.generic_string().c_str());
}

JSModuleDef* jsModuleLoader(JSContext* ctx, const char* moduleName, void*) {
    std::filesystem::path path(pathFromFileUrl(moduleName ? moduleName : ""));
    std::ifstream file(path);
    if (!file.is_open()) {
        JS_ThrowReferenceError(ctx, "could not load module '%s'", moduleName ? moduleName : "");
        return nullptr;
    }
    recordModuleDependency(path);

    std::stringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    JSValue value = JS_Eval(ctx, source.c_str(), source.size(), path.generic_string().c_str(),
                            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(value)) {
        return nullptr;
    }

    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(value));
    JS_FreeValue(ctx, value);
    return module;
}
}

JsRuntime& JsRuntime::instance() {
    static JsRuntime runtime;
    return runtime;
}

JsRuntime::JsRuntime() {
    runtime_ = JS_NewRuntime();
    JS_SetMemoryLimit(runtime_, kJsMemoryLimit);
    JS_SetMaxStackSize(runtime_, kJsStackLimit);
    JS_SetModuleLoaderFunc(runtime_, jsModuleNormalize, jsModuleLoader, nullptr);
    Log::info("QuickJS runtime initialized");
}

JsRuntime::~JsRuntime() {
    if (runtime_) JS_FreeRuntime(runtime_);
    Log::info("QuickJS runtime destroyed");
}

std::unique_ptr<JsContext> JsRuntime::createContext() {
    return std::make_unique<JsContext>(*this);
}

void JsRuntime::executePendingJobs() {
    JSContext* ctx = nullptr;
    while (JS_ExecutePendingJob(runtime_, &ctx) > 0) {}
}

void JsRuntime::beginModuleDependencyCapture(std::vector<std::string>& paths) {
    paths.clear();
    gModuleDependencyCapture = &paths;
}

void JsRuntime::endModuleDependencyCapture() {
    gModuleDependencyCapture = nullptr;
}

} // namespace saida
