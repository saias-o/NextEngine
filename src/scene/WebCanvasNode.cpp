#include "scene/WebCanvasNode.hpp"

#include "core/Log.hpp"
#include "core/Paths.hpp"
#include "core/Time.hpp"
#include "graphics/Buffer.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/VulkanDevice.hpp"
#include "scripting/JsContext.hpp"
#include "scripting/JsRuntime.hpp"
#include "ui/RmlUiRenderInterface.hpp"
#include "ui/RmlUiRuntime.hpp"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <quickjs.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <functional>
#include <sstream>
#include <vector>

namespace ne {

namespace {
std::atomic<uint64_t> gNextWebCanvasId{1};
constexpr float kHotReloadCheckIntervalSeconds = 0.5f;

float hotReloadPhase(const std::string& key) {
    if (key.empty()) return 0.0f;
    size_t bucket = std::hash<std::string>{}(key) % 1000u;
    return (static_cast<float>(bucket) / 1000.0f) * kHotReloadCheckIntervalSeconds;
}

class RmlDependencyCapture {
public:
    explicit RmlDependencyCapture(std::vector<std::string>& dependencies) {
        RmlUiRuntime::beginFileDependencyCapture(dependencies);
    }

    ~RmlDependencyCapture() {
        RmlUiRuntime::endFileDependencyCapture();
    }

    RmlDependencyCapture(const RmlDependencyCapture&) = delete;
    RmlDependencyCapture& operator=(const RmlDependencyCapture&) = delete;
};

WebCanvasNode* webCanvasFromJs(JSContext* ctx) {
    return static_cast<WebCanvasNode*>(JS_GetContextOpaque(ctx));
}

std::string escapeRmlText(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

JSValue jsDocumentSetText(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* canvas = webCanvasFromJs(ctx);
    if (!canvas || argc < 2) return JS_NewBool(ctx, false);

    const char* id = JS_ToCString(ctx, argv[0]);
    const char* text = JS_ToCString(ctx, argv[1]);
    bool ok = id && text && canvas->setElementText(id, text);
    JS_FreeCString(ctx, id);
    JS_FreeCString(ctx, text);
    return JS_NewBool(ctx, ok);
}

JSValue jsDocumentSetHTML(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    auto* canvas = webCanvasFromJs(ctx);
    if (!canvas || argc < 2) return JS_NewBool(ctx, false);

    const char* id = JS_ToCString(ctx, argv[0]);
    const char* html = JS_ToCString(ctx, argv[1]);
    bool ok = id && html && canvas->setElementRml(id, html);
    JS_FreeCString(ctx, id);
    JS_FreeCString(ctx, html);
    return JS_NewBool(ctx, ok);
}

JSValue jsDocumentReload(JSContext* ctx, JSValueConst, int, JSValueConst*) {
    if (auto* canvas = webCanvasFromJs(ctx)) {
        canvas->reload();
    }
    return JS_UNDEFINED;
}

} // namespace

WebCanvasNode::WebCanvasNode() = default;

WebCanvasNode::~WebCanvasNode() {
    if (rmlContext_) {
        rmlContext_->UnloadAllDocuments();
        rmlContext_->Update();
        Rml::RemoveContext(rmlContextName_);
    }
}

void WebCanvasNode::init(VulkanDevice& device, uint32_t width, uint32_t height, Mode mode) {
    width_ = width;
    height_ = height;
    mode_ = mode;
    device_ = &device;
    createPlaceholderTexture();
    ensureRmlContext();
    ensureJsContext();
    loadDocumentFromState();
}

void WebCanvasNode::loadHTML(const std::string& html) {
    url_.clear();
    html_ = html;
    documentWatchers_.clear();
    ensureRmlContext();
    loadDocumentFromState();
    markUiDirty();
    ensureJsContext();
    runStartupScripts();
}

void WebCanvasNode::loadURL(const std::string& url) {
    url_ = url;
    hotReloadTimer_ = hotReloadPhase(url_);
    html_.clear();
    documentWatchers_.clear();
    ensureRmlContext();
    loadDocumentFromState();
    markUiDirty();
    ensureJsContext();
    runStartupScripts();
}

void WebCanvasNode::setUrl(const std::string& url) {
    loadURL(url);
}

void WebCanvasNode::reload() {
    if (!url_.empty()) {
        loadURL(url_);
    } else if (!html_.empty()) {
        loadHTML(html_);
    }
}

void WebCanvasNode::executeJS(const std::string& script) {
    ensureJsContext();
    if (jsContext_ && jsContext_->eval(script, url_.empty() ? "<web-canvas>" : url_)) {
        markUiDirty();
    }
}

bool WebCanvasNode::setElementText(const std::string& id, const std::string& text) {
    return setElementRml(id, escapeRmlText(text));
}

bool WebCanvasNode::setElementRml(const std::string& id, const std::string& rml) {
    if (!document_) return false;
    Rml::Element* element = document_->GetElementById(id);
    if (!element) return false;
    element->SetInnerRML(rml);
    if (rmlContext_) rmlContext_->Update();
    markUiDirty();
    return true;
}

void WebCanvasNode::fireMouseEvent(MouseEvent type, int x, int y, MouseButton button) {
    if (!rmlContext_) return;

    constexpr int kNoModifiers = 0;
    constexpr int kLeftButton = 0;
    rmlContext_->ProcessMouseMove(x, y, kNoModifiers);

    if (button != MouseButton::Left) return;
    if (type == MouseEvent::Down) {
        rmlContext_->ProcessMouseButtonDown(kLeftButton, kNoModifiers);
    } else if (type == MouseEvent::Up) {
        rmlContext_->ProcessMouseButtonUp(kLeftButton, kNoModifiers);
    }
    markUiDirty();
}

void WebCanvasNode::fireScrollEvent(int deltaX, int deltaY) {
    if (!rmlContext_) return;
    constexpr int kNoModifiers = 0;
    rmlContext_->ProcessMouseWheel(Rml::Vector2f(static_cast<float>(deltaX), static_cast<float>(deltaY)), kNoModifiers);
    markUiDirty();
}

void WebCanvasNode::updateTextureIfNeededAsync(VkCommandBuffer cmd) {
    checkHotReload();
    if (!device_ || !texture_ || !rmlContext_ || !uiDirty_) return;

    rmlContext_->Update();
    RmlUiRenderInterface* renderer = RmlUiRuntime::renderer();
    if (!renderer) return;

    renderer->beginFrame(width_, height_);
    std::vector<std::string> renderDependencies;
    {
        RmlDependencyCapture capture(renderDependencies);
        rmlContext_->Render();
    }
    renderer->endFrame();
    appendDocumentWatchers(renderDependencies);

    const std::vector<uint8_t>& pixels = renderer->pixels();
    if (pixels.empty()) return;

    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(pixels.size());
    if (!stagingBuffer_ || stagingBuffer_->size() != byteCount) {
        stagingBuffer_ = std::make_unique<Buffer>(*device_, byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, MemoryUsage::HostVisible);
    }
    stagingBuffer_->write(pixels.data(), byteCount);
    texture_->updatePixelsAsync(cmd, *stagingBuffer_, width_, height_);
    uiDirty_ = false;
}

void WebCanvasNode::serialize(nlohmann::json& j, ResourceManager& resources) const {
    Node::serialize(j, resources);
    j["width"] = width_;
    j["height"] = height_;
    j["mode"] = static_cast<int>(mode_);
    j["url"] = url_;
    j["html"] = html_;
    j["hotReload"] = hotReloadEnabled_;
    j["startupScripts"] = startupScripts_;
}

void WebCanvasNode::deserialize(const nlohmann::json& j, ResourceManager& resources) {
    Node::deserialize(j, resources);
    width_ = j.value("width", 800u);
    height_ = j.value("height", 600u);
    mode_ = static_cast<Mode>(j.value("mode", static_cast<int>(Mode::ScreenSpace)));
    url_ = j.value("url", "");
    hotReloadTimer_ = hotReloadPhase(url_);
    html_ = j.value("html", "");
    hotReloadEnabled_ = j.value("hotReload", true);
    if (j.contains("startupScripts")) {
        startupScripts_ = j["startupScripts"].get<std::vector<std::string>>();
    }

    init(resources.device(), width_, height_, mode_);
    reload();
}

void WebCanvasNode::ensureJsContext() {
    if (!jsContext_) {
        jsContext_ = JsRuntime::instance().createContext();
        jsContext_->setOpaque(this);
        installDocumentBindings();
    }
}

void WebCanvasNode::ensureRmlContext() {
    if (!RmlUiRuntime::ensureInitialized()) return;

    if (rmlContext_) {
        rmlContext_->SetDimensions({static_cast<int>(width_), static_cast<int>(height_)});
        markUiDirty();
        return;
    }

    std::ostringstream name;
    name << "WebCanvas_" << gNextWebCanvasId.fetch_add(1);
    rmlContextName_ = name.str();
    rmlContext_ = Rml::CreateContext(
        rmlContextName_,
        {static_cast<int>(width_), static_cast<int>(height_)},
        RmlUiRuntime::renderInterface());

    if (!rmlContext_) {
        Log::error("[WebCanvas] failed to create RmlUi context");
    } else {
        markUiDirty();
    }
}

void WebCanvasNode::markUiDirty() {
    uiDirty_ = true;
}

void WebCanvasNode::installDocumentBindings() {
    if (!jsContext_) return;

    JSContext* ctx = jsContext_->raw();
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, document, "setText", JS_NewCFunction(ctx, jsDocumentSetText, "setText", 2));
    JS_SetPropertyStr(ctx, document, "setHTML", JS_NewCFunction(ctx, jsDocumentSetHTML, "setHTML", 2));
    JS_SetPropertyStr(ctx, document, "reload", JS_NewCFunction(ctx, jsDocumentReload, "reload", 0));
    JS_SetPropertyStr(ctx, global, "document", document);
    JS_FreeValue(ctx, global);
}

bool WebCanvasNode::loadDocumentFromState(bool keepExistingOnFailure) {
    if (!rmlContext_) return false;

    Rml::ElementDocument* nextDocument = nullptr;
    std::vector<std::string> dependencies;

    {
        RmlDependencyCapture capture(dependencies);
        if (!url_.empty()) {
            nextDocument = rmlContext_->LoadDocument(url_);
        } else if (!html_.empty()) {
            nextDocument = rmlContext_->LoadDocumentFromMemory(html_, "<web-canvas>");
        }
    }

    if (!nextDocument) {
        if (!url_.empty() || !html_.empty()) Log::warn("[WebCanvas] failed to load document");
        if (!keepExistingOnFailure && document_) {
            rmlContext_->UnloadDocument(document_);
            document_ = nullptr;
            rmlContext_->Update();
            documentWatchers_.clear();
            markUiDirty();
        }
        return false;
    }

    if (document_) {
        rmlContext_->UnloadDocument(document_);
    }
    document_ = nextDocument;
    document_->Show();
    rmlContext_->Update();
    updateDocumentWatchers(dependencies);
    markUiDirty();
    return true;
}

std::string WebCanvasNode::resolveUrlPath() const {
    if (url_.empty()) return {};

    std::filesystem::path candidate(pathFromFileUrl(url_));
    if (candidate.is_absolute() && std::filesystem::exists(candidate)) return candidate.string();
    if (std::filesystem::exists(candidate)) return std::filesystem::absolute(candidate).string();

    std::filesystem::path rootRelative(assetPath(candidate.string()));
    if (std::filesystem::exists(rootRelative)) return rootRelative.string();

    return {};
}

void WebCanvasNode::updateDocumentWatchers(const std::vector<std::string>& dependencies) {
    dependencyPaths_ = dependencies;
    std::string rootPath = resolveUrlPath();
    if (!rootPath.empty() && std::find(dependencyPaths_.begin(), dependencyPaths_.end(), rootPath) == dependencyPaths_.end()) {
        dependencyPaths_.push_back(std::move(rootPath));
    }
    rebuildDocumentWatchers();
}

void WebCanvasNode::appendDocumentWatchers(const std::vector<std::string>& dependencies) {
    bool changed = false;
    for (const auto& dependency : dependencies) {
        if (std::find(dependencyPaths_.begin(), dependencyPaths_.end(), dependency) == dependencyPaths_.end()) {
            dependencyPaths_.push_back(dependency);
            changed = true;
        }
    }
    if (changed) rebuildDocumentWatchers();
}

void WebCanvasNode::rebuildDocumentWatchers() {
    documentWatchers_.clear();

    documentWatchers_.reserve(dependencyPaths_.size());
    for (const auto& path : dependencyPaths_) {
        WatchedFile watcher;
        if (watcher.watch(path)) {
            documentWatchers_.push_back(std::move(watcher));
        }
    }
}

void WebCanvasNode::checkHotReload() {
    if (!hotReloadEnabled_ || url_.empty()) return;

    hotReloadTimer_ += Time::unscaledDelta();
    if (hotReloadTimer_ < kHotReloadCheckIntervalSeconds) return;
    hotReloadTimer_ = 0.0f;

    if (documentWatchers_.empty()) {
        std::vector<std::string> dependencies;
        updateDocumentWatchers(dependencies);
        return;
    }

    for (auto& watcher : documentWatchers_) {
        if (watcher.pollChanged()) {
            Log::info("[WebCanvas] hot reload ", watcher.path());
            if (loadDocumentFromState(true)) {
                runStartupScripts();
            } else {
                Log::warn("[WebCanvas] keeping previous document after reload failure");
            }
            return;
        }
    }
}

void WebCanvasNode::runStartupScripts() {
    for (const auto& script : startupScripts_) {
        executeJS(script);
    }
}

void WebCanvasNode::createPlaceholderTexture() {
    if (!device_ || width_ == 0 || height_ == 0) return;

    constexpr uint32_t kMinPlaceholderExtent = 2;
    uint32_t w = std::max(width_, kMinPlaceholderExtent);
    uint32_t h = std::max(height_, kMinPlaceholderExtent);

    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            size_t i = (static_cast<size_t>(y) * w + x) * 4;
            bool a = ((x / 32u) + (y / 32u)) % 2u == 0u;
            pixels[i + 0] = a ? 34 : 22;
            pixels[i + 1] = a ? 44 : 28;
            pixels[i + 2] = a ? 58 : 38;
            pixels[i + 3] = 255;
        }
    }

    texture_ = std::make_unique<Texture>(*device_, pixels.data(), w, h, VK_FORMAT_R8G8B8A8_SRGB, false);
    stagingBuffer_.reset();
    markUiDirty();
}

} // namespace ne
