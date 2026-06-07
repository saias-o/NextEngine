#include "scene/WebBridge.hpp"
#include "scene/WebCanvasNode.hpp"
#include "core/Log.hpp"
#include <cstring>

namespace ne {

WebBridge::WebBridge(WebCanvasNode* canvas) : canvas_(canvas) {}

WebBridge::~WebBridge() = default;

void WebBridge::registerFunction(const std::string& name, JSCallback callback) {
    callbacks_[name] = std::move(callback);
}

void WebBridge::executeJS(const std::string& script) {
    if (canvas_) {
        canvas_->executeJS(script);
    }
}

void WebBridge::initBindings() {
    if (!canvas_ || !canvas_->view()) return;

    JSContextRef ctx = ulViewLockJSContext(canvas_->view());
    JSObjectRef globalObj = JSContextGetGlobalObject(ctx);

    // Create a JS class for our functions so we can pass 'this' as private data
    JSClassDefinition classDef = kJSClassDefinitionEmpty;
    classDef.className = "WebBridgeFunction";
    classDef.callAsFunction = jsCallbackWrapper;
    JSClassRef funcClass = JSClassCreate(&classDef);

    for (const auto& pair : callbacks_) {
        JSStringRef nameStr = JSStringCreateWithUTF8CString(pair.first.c_str());
        
        // Create the function object with 'this' as private data
        JSObjectRef funcObj = JSObjectMake(ctx, funcClass, this);
        
        // Attaching the name string to the object isn't directly supported via private data (since we used 'this'),
        // so we can create another object if needed, but since all functions go to jsCallbackWrapper, 
        // we can set a custom property on the funcObj to store its name, or use multiple JSClassDefs.
        // Actually, we can just store the name as a JS property on the function object.
        JSStringRef propName = JSStringCreateWithUTF8CString("__ne_func_name");
        JSValueRef nameVal = JSValueMakeString(ctx, nameStr);
        JSObjectSetProperty(ctx, funcObj, propName, nameVal, kJSPropertyAttributeNone, nullptr);
        JSStringRelease(propName);

        // Bind the function to the global object
        JSObjectSetProperty(ctx, globalObj, nameStr, funcObj, kJSPropertyAttributeNone, nullptr);
        JSStringRelease(nameStr);
    }

    JSClassRelease(funcClass);
    ulViewUnlockJSContext(canvas_->view());
}

JSValueRef WebBridge::jsCallbackWrapper(JSContextRef ctx, JSObjectRef function, JSObjectRef thisObject, size_t argumentCount, const JSValueRef arguments[], JSValueRef* exception) {
    WebBridge* bridge = static_cast<WebBridge*>(JSObjectGetPrivate(function));
    if (!bridge) return JSValueMakeUndefined(ctx);

    // Get the function name
    JSStringRef propName = JSStringCreateWithUTF8CString("__ne_func_name");
    JSValueRef nameVal = JSObjectGetProperty(ctx, function, propName, nullptr);
    JSStringRelease(propName);

    if (JSValueIsString(ctx, nameVal)) {
        JSStringRef nameStr = JSValueToStringCopy(ctx, nameVal, nullptr);
        size_t maxSize = JSStringGetMaximumUTF8CStringSize(nameStr);
        std::string name(maxSize, '\0');
        JSStringGetUTF8CString(nameStr, name.data(), maxSize);
        name.resize(strlen(name.data())); // Remove trailing nulls
        JSStringRelease(nameStr);

        auto it = bridge->callbacks_.find(name);
        if (it != bridge->callbacks_.end()) {
            std::vector<std::string> args;
            for (size_t i = 0; i < argumentCount; ++i) {
                JSStringRef argStr = JSValueToStringCopy(ctx, arguments[i], nullptr);
                size_t argSize = JSStringGetMaximumUTF8CStringSize(argStr);
                std::string arg(argSize, '\0');
                JSStringGetUTF8CString(argStr, arg.data(), argSize);
                arg.resize(strlen(arg.data()));
                JSStringRelease(argStr);
                args.push_back(arg);
            }
            // Execute callback
            it->second(args);
        }
    }

    return JSValueMakeUndefined(ctx);
}

} // namespace ne
