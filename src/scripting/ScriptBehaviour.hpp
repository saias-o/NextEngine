#pragma once

#include "core/FileWatcher.hpp"
#include "scene/Behaviour.hpp"
#include "scripting/JsContext.hpp"

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace saida {

struct ScriptProperty {
    enum class Type {
        Number,
        Boolean,
        String
    };

    std::string name;
    Type type = Type::Number;
    std::variant<double, bool, std::string> value = 0.0;
    bool exportedThisLoad = false;
};

class ScriptBehaviour : public Behaviour {
public:
    ~ScriptBehaviour() override;

    void setScriptPath(std::string path);
    const std::string& scriptPath() const { return scriptPath_; }

    bool reload();
    void exportNumberProperty(const std::string& name, double defaultValue);
    void exportBoolProperty(const std::string& name, bool defaultValue);
    void exportStringProperty(const std::string& name, const std::string& defaultValue);

    // Editor-facing accessors (the inspector UI lives in the editor's
    // InspectorRegistry, not in this gameplay behaviour).
    bool loaded() const { return loaded_; }
    bool hotReloadEnabled() const { return hotReloadEnabled_; }
    void setHotReloadEnabled(bool enabled) { hotReloadEnabled_ = enabled; }
    std::vector<ScriptProperty>& properties() { return properties_; }
    void applyProperty(const ScriptProperty& property) { applyPropertyToJs(property); }

    void onReady() override;
    void onUpdate(float dt) override;
    void onDestroy() override;
    void onEnable() override;
    void onDisable() override;

    const char* typeName() const override { return "ScriptBehaviour"; }
    void save(nlohmann::json& j) const override;
    void load(const nlohmann::json& j) override;

private:
    std::string resolveScriptPath() const;
    bool reloadContext(bool lifecycleReload);
    bool shouldLoadAsModule(const std::string& resolvedPath) const;
    bool callHook(const char* functionName);
    bool callHook(const char* functionName, double arg);
    void updateScriptWatchers(const std::string& resolvedPath, const std::vector<std::string>& moduleDependencies);
    bool scriptFilesChanged();
    void checkHotReload(float dt);
    void installScriptApi();
    void preparePropertyReload();
    void pruneUnexportedProperties();
    void applyAllPropertiesToJs();
    void applyPropertyToJs(const ScriptProperty& property);
    ScriptProperty* findProperty(const std::string& name);
    const ScriptProperty* findProperty(const std::string& name) const;
    void setPropertyValue(ScriptProperty& property, double value);
    void setPropertyValue(ScriptProperty& property, bool value);
    void setPropertyValue(ScriptProperty& property, const std::string& value);

    std::string scriptPath_;
    WatchedFile scriptWatcher_;
    std::vector<WatchedFile> moduleWatchers_;
    std::vector<ScriptProperty> properties_;
    std::unique_ptr<JsContext> context_;
    float hotReloadTimer_ = 0.0f;
    bool hotReloadEnabled_ = true;
    bool started_ = false;
    bool moduleMode_ = false;
    bool loaded_ = false;
};

} // namespace saida
