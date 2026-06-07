#pragma once

#include <Ultralight/CAPI.h>
#include <JavaScriptCore/JavaScript.h>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>

namespace ne {

class WebCanvasNode;

// Bridge entre JavaScript (Ultralight) et C++ (Engine/Behaviours)
class WebBridge {
public:
    explicit WebBridge(WebCanvasNode* canvas);
    ~WebBridge();

    using JSCallback = std::function<void(const std::vector<std::string>&)>;
    
    // Enregistre une fonction globale qui pourra être appelée depuis le JS
    // Ex: registerFunction("onStartClick", [](auto args) { ... });
    void registerFunction(const std::string& name, JSCallback callback);

    // Execute un script Javascript
    void executeJS(const std::string& script);

    // Initialise les bindings. Doit être appelé lors du WindowObjectReady.
    void initBindings();

private:
    static JSValueRef jsCallbackWrapper(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception);

    WebCanvasNode* canvas_;
    std::unordered_map<std::string, JSCallback> callbacks_;
};

} // namespace ne
