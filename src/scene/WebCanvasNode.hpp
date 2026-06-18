#pragma once

#include "core/FileWatcher.hpp"
#include "graphics/Texture.hpp"
#include "scene/Node.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ne {

class JsContext;
class Buffer;
class ResourceManager;
class VulkanDevice;

} // namespace ne

namespace Rml {
class Context;
class ElementDocument;
}

namespace ne {

class WebCanvasNode : public Node {
public:
    enum class Mode {
        ScreenSpace,
        WorldSpace
    };

    enum class MouseEvent {
        Down,
        Up,
        Move
    };

    enum class MouseButton {
        None,
        Left
    };

    WebCanvasNode();
    ~WebCanvasNode() override;

    void init(VulkanDevice& device, uint32_t width, uint32_t height, Mode mode = Mode::ScreenSpace);

    void loadHTML(const std::string& html);
    void loadURL(const std::string& url);
    void executeJS(const std::string& script);
    bool setElementText(const std::string& id, const std::string& text);
    bool setElementRml(const std::string& id, const std::string& rml);

    void fireMouseEvent(MouseEvent type, int x, int y, MouseButton button);
    void fireScrollEvent(int deltaX, int deltaY);

    const std::string& url() const { return url_; }
    void setUrl(const std::string& url);
    void reload();
    bool hotReloadEnabled() const { return hotReloadEnabled_; }
    void setHotReloadEnabled(bool enabled) { hotReloadEnabled_ = enabled; }

    Mode mode() const { return mode_; }
    void setMode(Mode mode) { mode_ = mode; }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    void addStartupScript(const std::string& script) { startupScripts_.push_back(script); }
    const std::vector<std::string>& startupScripts() const { return startupScripts_; }

    void updateTextureIfNeededAsync(VkCommandBuffer cmd);
    Texture* texture() const { return texture_.get(); }

    const char* typeName() const override { return "WebCanvasNode"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

private:
    void ensureJsContext();
    void ensureRmlContext();
    void markUiDirty();
    void installDocumentBindings();
    bool loadDocumentFromState(bool keepExistingOnFailure = false);
    std::string resolveUrlPath() const;
    void updateDocumentWatchers(const std::vector<std::string>& dependencies);
    void appendDocumentWatchers(const std::vector<std::string>& dependencies);
    void rebuildDocumentWatchers();
    void checkHotReload();
    void runStartupScripts();
    void createPlaceholderTexture();

    Mode mode_ = Mode::ScreenSpace;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::string url_;
    std::string html_;

    VulkanDevice* device_ = nullptr;
    std::unique_ptr<Texture> texture_;
    std::unique_ptr<Buffer> stagingBuffer_;
    std::unique_ptr<JsContext> jsContext_;
    std::string rmlContextName_;
    Rml::Context* rmlContext_ = nullptr;
    Rml::ElementDocument* document_ = nullptr;
    bool uiDirty_ = true;
    bool hotReloadEnabled_ = true;
    float hotReloadTimer_ = 0.0f;
    std::vector<std::string> dependencyPaths_;
    std::vector<WatchedFile> documentWatchers_;
    std::vector<std::string> startupScripts_;
};

} // namespace ne
