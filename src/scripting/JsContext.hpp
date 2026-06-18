#pragma once

#include <quickjs.h>

#include <string>

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

private:
    void installConsole();
    bool callGlobalImpl(const char* functionName, int argc, JSValue* argv);
    bool callModuleExportImpl(const char* functionName, int argc, JSValue* argv);
    void logException(const std::string& filename);

    JsRuntime& runtime_;
    JSContext* ctx_ = nullptr;
    JSValue moduleNamespace_ = JS_UNDEFINED;
};

} // namespace ne
