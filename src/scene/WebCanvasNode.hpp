#pragma once

#include "scene/Node.hpp"
#include "scene/WebBridge.hpp"
#include "graphics/Texture.hpp"
#include <Ultralight/CAPI.h>
#include <memory>
#include <string>

namespace ne {

class VulkanDevice;
class Buffer;
class ResourceManager;

class WebCanvasNode : public Node {
public:
    enum class Mode {
        ScreenSpace,
        WorldSpace
    };

    WebCanvasNode();
    ~WebCanvasNode() override;

    // Initialise la vue Web et la texture associée.
    // Doit être appelé après la création.
    void init(VulkanDevice& device, uint32_t width, uint32_t height, Mode mode = Mode::ScreenSpace);

    void loadHTML(const std::string& html);
    void loadURL(const std::string& url);
    void executeJS(const std::string& script);

    void fireMouseEvent(ULMouseEventType type, int x, int y, ULMouseButton button);
    void fireScrollEvent(int deltaX, int deltaY);

    const std::string& url() const { return url_; }
    void setUrl(const std::string& url);
    void reload();

    Mode mode() const { return mode_; }
    void setMode(Mode mode) { mode_ = mode; }

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    void addStartupScript(const std::string& script) { startupScripts_.push_back(script); }
    const std::vector<std::string>& startupScripts() const { return startupScripts_; }

    void updateTextureIfNeededAsync(VkCommandBuffer cmd);
    Texture* texture() const { return texture_.get(); }
    ULView view() const { return view_; }
    WebBridge& bridge() { return *bridge_; }

    const char* typeName() const override { return "WebCanvasNode"; }
    void serialize(nlohmann::json& j, ResourceManager& resources) const override;
    void deserialize(const nlohmann::json& j, ResourceManager& resources) override;

private:
    static void onWindowObjectReady(void* user_data, ULView caller, unsigned long long frame_id, bool is_main_frame, ULString url);
    static void onDOMReady(void* user_data, ULView caller, unsigned long long frame_id, bool is_main_frame, ULString url);
    static void onChangeCursor(void* user_data, ULView caller, ULCursor cursor);
    static void onChangeTitle(void* user_data, ULView caller, ULString title);
    static void onAddConsoleMessage(void* user_data, ULView caller, ULMessageSource source, ULMessageLevel level, ULString message, unsigned int line_number, unsigned int column_number, ULString source_id);

    Mode mode_ = Mode::ScreenSpace;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::string url_ = "";
    ULView view_ = nullptr;
    std::unique_ptr<Texture> texture_;
    std::unique_ptr<WebBridge> bridge_;

    VulkanDevice* device_ = nullptr;
    std::unique_ptr<Buffer> stagingBuffer_;
    std::vector<std::string> startupScripts_;
};

} // namespace ne
