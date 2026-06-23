#pragma once

#include "core/Signal.hpp"

#include <quickjs.h>

#include <string>
#include <vector>

struct JSContext;

namespace ne {

class JsRuntime;

class JsContext {
public:
    explicit JsContext(JsRuntime& runtime);
    ~JsContext();

    JsContext(const JsContext&) = delete;
    JsContext& operator=(const JsContext&) = delete;

    bool eval(const std::string& source, const std::string& filename = "<eval>");
    bool evalModule(const std::string& source, const std::string& filename);
    bool callGlobal(const char* functionName);
    bool callGlobal(const char* functionName, double arg);
    bool callModuleExport(const char* functionName);
    bool callModuleExport(const char* functionName, double arg);

    JSContext* raw() const { return ctx_; }
    void setOpaque(void* opaque);

    // Recover the owning wrapper from a raw QuickJS context (used by native
    // bindings that need to register signal subscriptions). Null if unknown.
    static JsContext* fromRaw(JSContext* ctx);

    // Keep a JS callback subscribed to a reflected signal alive for as long as
    // this context lives. On destruction the connection is dropped and the
    // callback freed — so a hot-reload (which rebuilds the context) cleanly
    // disconnects every `node.on(...)` handler.
    void retainSignalSubscription(Connection&& connection, JSValue callback);

private:
    void installConsole();
    void clearSignalSubscriptions();
    bool callGlobalImpl(const char* functionName, int argc, JSValue* argv);
    bool callModuleExportImpl(const char* functionName, int argc, JSValue* argv);
    void logException(const std::string& filename);

    struct SignalSubscription {
        Connection connection;
        JSValue callback;
    };

    JsRuntime& runtime_;
    JSContext* ctx_ = nullptr;
    JSValue moduleNamespace_ = JS_UNDEFINED;
    std::vector<SignalSubscription> signalSubs_;
};

} // namespace ne
