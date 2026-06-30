#include "scripting/JsContext.hpp"

#include "core/Log.hpp"
#include "scripting/JsRuntime.hpp"

#include <quickjs.h>

#include <unordered_map>

namespace saida {

namespace {
// Raw QuickJS context → owning wrapper, so native bindings can reach the wrapper
// to register signal subscriptions (the QuickJS opaque already holds the
// Behaviour pointer, so we cannot stash the wrapper there too).
std::unordered_map<JSContext*, JsContext*>& contextRegistry() {
    static std::unordered_map<JSContext*, JsContext*> registry;
    return registry;
}

JSValue jsConsoleLog(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv, int magic) {
    std::string out;
    for (int i = 0; i < argc; ++i) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (i > 0) out += " ";
        out += str ? str : "<value>";
        JS_FreeCString(ctx, str);
    }

    if (magic == 1) Log::warn("[JS] ", out);
    else if (magic == 2) Log::error("[JS] ", out);
    else Log::info("[JS] ", out);
    return JS_UNDEFINED;
}
} // namespace

JsContext::JsContext(JsRuntime& runtime) : runtime_(runtime) {
    ctx_ = JS_NewContext(runtime_.raw());
    contextRegistry()[ctx_] = this;
    installConsole();
}

JsContext::~JsContext() {
    if (ctx_) {
        // Drop signal subscriptions BEFORE freeing the context: disconnect first
        // (so a pending emit can't reach a freed callback), then release the
        // retained JS callbacks while the context is still valid.
        clearSignalSubscriptions();
        contextRegistry().erase(ctx_);
        JS_FreeValue(ctx_, moduleNamespace_);
        JS_FreeContext(ctx_);
    }
}

JsContext* JsContext::fromRaw(JSContext* ctx) {
    auto& reg = contextRegistry();
    auto it = reg.find(ctx);
    return it != reg.end() ? it->second : nullptr;
}

void JsContext::retainSignalSubscription(Connection&& connection, JSValue callback) {
    signalSubs_.push_back({std::move(connection), callback});
}

void JsContext::clearSignalSubscriptions() {
    for (auto& sub : signalSubs_) {
        sub.connection.disconnect();
        JS_FreeValue(ctx_, sub.callback);
    }
    signalSubs_.clear();
}

bool JsContext::eval(const std::string& source, const std::string& filename) {
    JSValue value = JS_Eval(ctx_, source.c_str(), source.size(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(value)) {
        JS_FreeValue(ctx_, value);
        logException(filename);
        return false;
    }
    JS_FreeValue(ctx_, value);
    runtime_.executePendingJobs();
    return true;
}

bool JsContext::evalModule(const std::string& source, const std::string& filename) {
    JS_FreeValue(ctx_, moduleNamespace_);
    moduleNamespace_ = JS_UNDEFINED;

    JSValue compiled = JS_Eval(ctx_, source.c_str(), source.size(), filename.c_str(),
                               JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(compiled)) {
        JS_FreeValue(ctx_, compiled);
        logException(filename);
        return false;
    }

    if (JS_VALUE_GET_TAG(compiled) != JS_TAG_MODULE) {
        JS_FreeValue(ctx_, compiled);
        Log::error("[JS] ", filename, ": compiled value is not a module");
        return false;
    }

    auto* module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(compiled));
    if (JS_ResolveModule(ctx_, compiled) < 0) {
        JS_FreeValue(ctx_, compiled);
        logException(filename);
        return false;
    }

    JSValue result = JS_EvalFunction(ctx_, compiled);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx_, result);
        logException(filename);
        return false;
    }
    JS_FreeValue(ctx_, result);

    moduleNamespace_ = JS_GetModuleNamespace(ctx_, module);
    if (JS_IsException(moduleNamespace_)) {
        JS_FreeValue(ctx_, moduleNamespace_);
        moduleNamespace_ = JS_UNDEFINED;
        logException(filename);
        return false;
    }

    runtime_.executePendingJobs();
    return true;
}

bool JsContext::compileCheck(const std::string& source, const std::string& filename,
                             bool asModule, std::string& errorOut) {
    int flags = JS_EVAL_FLAG_COMPILE_ONLY | (asModule ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);
    JSValue compiled = JS_Eval(ctx_, source.c_str(), source.size(), filename.c_str(), flags);
    bool ok = !JS_IsException(compiled);
    if (!ok) {
        JSValue exc = JS_GetException(ctx_);
        const char* message = JS_ToCString(ctx_, exc);
        errorOut = message ? message : "unknown compile error";
        JS_FreeCString(ctx_, message);
        JS_FreeValue(ctx_, exc);
    }
    JS_FreeValue(ctx_, compiled);
    return ok;
}

bool JsContext::callGlobal(const char* functionName) {
    return callGlobalImpl(functionName, 0, nullptr);
}

bool JsContext::callGlobal(const char* functionName, double arg) {
    JSValue value = JS_NewFloat64(ctx_, arg);
    bool ok = callGlobalImpl(functionName, 1, &value);
    JS_FreeValue(ctx_, value);
    return ok;
}

bool JsContext::callModuleExport(const char* functionName) {
    return callModuleExportImpl(functionName, 0, nullptr);
}

bool JsContext::callModuleExport(const char* functionName, double arg) {
    JSValue value = JS_NewFloat64(ctx_, arg);
    bool ok = callModuleExportImpl(functionName, 1, &value);
    JS_FreeValue(ctx_, value);
    return ok;
}

void JsContext::setOpaque(void* opaque) {
    JS_SetContextOpaque(ctx_, opaque);
}

void JsContext::installConsole() {
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue console = JS_NewObject(ctx_);
    JS_SetPropertyStr(ctx_, console, "log", JS_NewCFunctionMagic(ctx_, jsConsoleLog, "log", 1, JS_CFUNC_generic_magic, 0));
    JS_SetPropertyStr(ctx_, console, "warn", JS_NewCFunctionMagic(ctx_, jsConsoleLog, "warn", 1, JS_CFUNC_generic_magic, 1));
    JS_SetPropertyStr(ctx_, console, "error", JS_NewCFunctionMagic(ctx_, jsConsoleLog, "error", 1, JS_CFUNC_generic_magic, 2));
    JS_SetPropertyStr(ctx_, global, "console", console);
    JS_FreeValue(ctx_, global);
}

bool JsContext::callGlobalImpl(const char* functionName, int argc, JSValue* argv) {
    JSValue global = JS_GetGlobalObject(ctx_);
    JSValue fn = JS_GetPropertyStr(ctx_, global, functionName);
    if (JS_IsUndefined(fn) || JS_IsNull(fn)) {
        JS_FreeValue(ctx_, fn);
        JS_FreeValue(ctx_, global);
        return true;
    }
    if (!JS_IsFunction(ctx_, fn)) {
        Log::warn("[JS] global '", functionName, "' is not a function");
        JS_FreeValue(ctx_, fn);
        JS_FreeValue(ctx_, global);
        return false;
    }

    JSValue result = JS_Call(ctx_, fn, global, argc, argv);
    JS_FreeValue(ctx_, fn);
    JS_FreeValue(ctx_, global);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx_, result);
        logException(functionName);
        return false;
    }
    JS_FreeValue(ctx_, result);
    runtime_.executePendingJobs();
    return true;
}

bool JsContext::callModuleExportImpl(const char* functionName, int argc, JSValue* argv) {
    if (JS_IsUndefined(moduleNamespace_) || JS_IsNull(moduleNamespace_)) {
        return true;
    }

    JSValue fn = JS_GetPropertyStr(ctx_, moduleNamespace_, functionName);
    if (JS_IsUndefined(fn) || JS_IsNull(fn)) {
        JS_FreeValue(ctx_, fn);
        return true;
    }
    if (!JS_IsFunction(ctx_, fn)) {
        Log::warn("[JS] module export '", functionName, "' is not a function");
        JS_FreeValue(ctx_, fn);
        return false;
    }

    JSValue result = JS_Call(ctx_, fn, moduleNamespace_, argc, argv);
    JS_FreeValue(ctx_, fn);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx_, result);
        logException(functionName);
        return false;
    }
    JS_FreeValue(ctx_, result);
    runtime_.executePendingJobs();
    return true;
}

void JsContext::logException(const std::string& filename) {
    JSValue exc = JS_GetException(ctx_);
    const char* message = JS_ToCString(ctx_, exc);
    Log::error("[JS] ", filename, ": ", message ? message : "unknown exception");
    JS_FreeCString(ctx_, message);

    if (JS_IsObject(exc)) {
        JSValue stack = JS_GetPropertyStr(ctx_, exc, "stack");
        if (!JS_IsUndefined(stack)) {
            const char* stackText = JS_ToCString(ctx_, stack);
            if (stackText) Log::error("[JS] stack:\n", stackText);
            JS_FreeCString(ctx_, stackText);
        }
        JS_FreeValue(ctx_, stack);
    }

    JS_FreeValue(ctx_, exc);
}

} // namespace saida
