#include "scene/WebCanvasNode.hpp"
#include "ui/WebEngine.hpp"
#include "graphics/VulkanDevice.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Buffer.hpp"
#include "core/Log.hpp"
#include <Ultralight/CAPI/CAPI_MouseEvent.h>
#include <Ultralight/CAPI/CAPI_ScrollEvent.h>
#include <nlohmann/json.hpp>
#include <vector>

namespace ne {

WebCanvasNode::WebCanvasNode() {
    bridge_ = std::make_unique<WebBridge>(this);
}

WebCanvasNode::~WebCanvasNode() {
    if (view_) {
        ulDestroyView(view_);
    }
}

void WebCanvasNode::init(VulkanDevice& device, uint32_t width, uint32_t height, Mode mode) {
    width_ = width;
    height_ = height;
    mode_ = mode;
    device_ = &device;

    ULViewConfig viewConfig = ulCreateViewConfig();
    ulViewConfigSetIsTransparent(viewConfig, true);
    
    view_ = ulCreateView(WebEngine::get().renderer(), width, height, viewConfig, WebEngine::get().defaultSession());
    ulDestroyViewConfig(viewConfig);

    ulViewSetWindowObjectReadyCallback(view_, onWindowObjectReady, this);
    ulViewSetDOMReadyCallback(view_, onDOMReady, this);
    ulViewSetChangeCursorCallback(view_, onChangeCursor, this);
    ulViewSetChangeTitleCallback(view_, onChangeTitle, this);
    ulViewSetAddConsoleMessageCallback(view_, onAddConsoleMessage, this);

    std::vector<uint8_t> dummy(width * height * 4, 255);
    texture_ = std::make_unique<Texture>(device, dummy.data(), width, height, VK_FORMAT_R8G8B8A8_SRGB, false);

    ULConfig config = ulCreateConfig();
}

void WebCanvasNode::loadHTML(const std::string& html) {
    if (!view_) return;
    ULString ulHtml = ulCreateString(html.c_str());
    ulViewLoadHTML(view_, ulHtml);
    ulDestroyString(ulHtml);
}

void WebCanvasNode::loadURL(const std::string& url) {
    url_ = url;
    if (!view_) return;
    ULString ulUrl = ulCreateString(url.c_str());
    ulViewLoadURL(view_, ulUrl);
    ulDestroyString(ulUrl);
}

void WebCanvasNode::setUrl(const std::string& url) {
    loadURL(url);
}

void WebCanvasNode::reload() {
    if (!url_.empty()) {
        loadURL(url_);
    }
}

void WebCanvasNode::executeJS(const std::string& script) {
    if (!view_) return;
    ULString ulScript = ulCreateString(script.c_str());
    ulViewEvaluateScript(view_, ulScript, nullptr);
    ulDestroyString(ulScript);
}

void WebCanvasNode::fireMouseEvent(ULMouseEventType type, int x, int y, ULMouseButton button) {
    if (!view_) return;
    ULMouseEvent evt = ulCreateMouseEvent(type, x, y, button);
    ulViewFireMouseEvent(view_, evt);
    ulDestroyMouseEvent(evt);
}

void WebCanvasNode::fireScrollEvent(int deltaX, int deltaY) {
    if (!view_) return;
    ULScrollEvent evt = ulCreateScrollEvent(kScrollEventType_ScrollByPixel, deltaX, deltaY);
    ulViewFireScrollEvent(view_, evt);
    ulDestroyScrollEvent(evt);
}

void WebCanvasNode::updateTextureIfNeededAsync(VkCommandBuffer cmd) {
    if (!view_ || !texture_ || !device_) return;

    ULSurface surface = ulViewGetSurface(view_);
    if (!surface) return;

    ULBitmap bitmap = ulBitmapSurfaceGetBitmap(surface);
    if (!bitmap) return;

    if (ulBitmapIsEmpty(bitmap)) return;

    void* pixels = ulBitmapLockPixels(bitmap);
    size_t size = ulBitmapGetSize(bitmap);
    uint32_t w = ulBitmapGetWidth(bitmap);
    uint32_t h = ulBitmapGetHeight(bitmap);

    if (pixels && size > 0) {
        if (!stagingBuffer_ || stagingBuffer_->size() < size) {
            stagingBuffer_ = std::make_unique<Buffer>(*device_, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::HostVisible);
        }
        
        stagingBuffer_->write(pixels, size);
        texture_->updatePixelsAsync(cmd, *stagingBuffer_, w, h);
    }
    
    ulBitmapUnlockPixels(bitmap);
}

void WebCanvasNode::serialize(nlohmann::json& j, ResourceManager& resources) const {
    Node::serialize(j, resources);
    j["width"] = width_;
    j["height"] = height_;
    j["mode"] = static_cast<int>(mode_);
    j["url"] = url_;
    j["startupScripts"] = startupScripts_;
}

void WebCanvasNode::deserialize(const nlohmann::json& j, ResourceManager& resources) {
    Node::deserialize(j, resources);
    width_ = j.value("width", 800);
    height_ = j.value("height", 600);
    mode_ = static_cast<Mode>(j.value("mode", static_cast<int>(Mode::ScreenSpace)));
    
    if (j.contains("startupScripts")) {
        startupScripts_ = j["startupScripts"].get<std::vector<std::string>>();
    }

    url_ = j.value("url", "");
    
    init(resources.device(), width_, height_, mode_);
    if (!url_.empty()) {
        loadURL(url_);
    }
}

void WebCanvasNode::onWindowObjectReady(void* user_data, ULView caller, unsigned long long frame_id, bool is_main_frame, ULString url) {
    if (is_main_frame) {
        WebCanvasNode* node = static_cast<WebCanvasNode*>(user_data);
        node->bridge_->initBindings();
    }
}

void WebCanvasNode::onDOMReady(void* user_data, ULView caller, unsigned long long frame_id, bool is_main_frame, ULString url) {
    WebCanvasNode* node = static_cast<WebCanvasNode*>(user_data);
    for (const auto& script : node->startupScripts_) {
        ULString ulScript = ulCreateString(script.c_str());
        ulViewEvaluateScript(caller, ulScript, nullptr);
        ulDestroyString(ulScript);
    }
}

void WebCanvasNode::onChangeCursor(void* user_data, ULView caller, ULCursor cursor) {}
void WebCanvasNode::onChangeTitle(void* user_data, ULView caller, ULString title) {}
void WebCanvasNode::onAddConsoleMessage(void* user_data, ULView caller, ULMessageSource source, ULMessageLevel level, ULString message, unsigned int line_number, unsigned int column_number, ULString source_id) {}

} // namespace ne
