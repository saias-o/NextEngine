#include "graphics/UIRenderer.hpp"
#include "core/Profiler.hpp"
#include "graphics/ResourceManager.hpp"
#include "graphics/Pipeline.hpp"
#include "scene/Scene.hpp"
#include "scene/UICanvasNode.hpp"
#include "scene/UITextNode.hpp"
#ifndef SAIDA_RHI_WEBGPU
#include "scene/UIColorNode.hpp"
#include "scene/UIImageNode.hpp"
#include "scene/UIButtonNode.hpp"
#include "scene/UIToggleNode.hpp"
#include "scene/WebCanvasNode.hpp"
#endif
#include "core/Log.hpp"
#include "core/Paths.hpp"

#include <algorithm>
#include <cmath>

#ifdef SAIDA_RHI_WEBGPU
#include "ui/RmlUiRenderInterface.hpp"
#include "ui/RmlUiRuntime.hpp"

#include <RmlUi/Core.h>

#include <iomanip>
#include <sstream>
#endif

namespace saida {

struct UIPushConstants {
    glm::vec2 position;
    glm::vec2 size;
    glm::vec2 screenSize;
    uint32_t textureId;
    uint32_t hasTexture;
    glm::vec4 color;
    glm::vec2 corners[4];
    uint32_t useCorners;
    uint32_t _pad;
};

UIRenderer::UIRenderer(rhi::Device& device, ResourceManager& resources, rhi::Format colorFormat)
    : device_(device), resources_(resources) {
    Pipeline::Desc desc;
    desc.vertPath = shaderPath("ui.vert.spv");
    desc.fragPath = shaderPath("ui.frag.spv");
    desc.colorFormats = {colorFormat};
#ifdef SAIDA_RHI_WEBGPU
    textureLayout_ = std::make_unique<rhi::BindGroupLayout>(device_,
        std::vector<rhi::webgpu::BindGroupLayoutEntry>{
            {0, rhi::BindingType::SampledTexture, rhi::ShaderStages::Fragment},
            {1, rhi::BindingType::Sampler, rhi::ShaderStages::Fragment},
        });
    desc.bindGroupLayouts = {textureLayout_.get()};
#else
    desc.bindGroupLayouts = {resources_.globalMaterialSetLayout()};  // raw bindless set
#endif
    desc.vertexInput = false;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.cullMode = rhi::CullMode::None;
    desc.blendMode = rhi::BlendMode::Alpha;
    desc.topology = rhi::Topology::TriangleStrip;
    desc.pushConstantSize = sizeof(UIPushConstants);
    pipeline_ = std::make_unique<Pipeline>(device_, desc);

    Log::info("UIRenderer: pipeline created");
}

UIRenderer::~UIRenderer() {
#ifdef SAIDA_RHI_WEBGPU
    if (legacyContext_) {
        legacyContext_->UnloadAllDocuments();
        legacyContext_->Update();
        Rml::RemoveContext(legacyContextName_);
    }
#endif
}

#ifdef SAIDA_RHI_WEBGPU
namespace {

std::string escapeRmlText(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (char c : text) {
        switch (c) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        default: escaped += c; break;
        }
    }
    return escaped;
}

uint64_t fnv1a(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

void appendTextElements(Node& parent, std::ostringstream& out) {
    for (const auto& child : parent.children()) {
        if (!child->isActiveInHierarchy()) continue;
        if (auto* text = dynamic_cast<UITextNode*>(child.get())) {
            float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
            text->getGlobalRect(x, y, width, height);
            const glm::vec4 color = glm::clamp(text->color(), glm::vec4(0.0f), glm::vec4(1.0f));
            out << "<div style=\"position:absolute;left:" << x << "px;top:" << y
                << "px;width:" << width << "px;height:" << height
                << "px;font-family:NextSans;font-size:" << text->fontSize()
                << "px;line-height:" << height << "px;color:rgba("
                << static_cast<int>(std::round(color.r * 255.0f)) << ','
                << static_cast<int>(std::round(color.g * 255.0f)) << ','
                << static_cast<int>(std::round(color.b * 255.0f)) << ','
                << static_cast<int>(std::round(color.a * 255.0f))
                << ");overflow:hidden;\">"
                << escapeRmlText(text->text()) << "</div>";
        }
        appendTextElements(*child, out);
    }
}

} // namespace

void UIRenderer::gatherLegacyWebUI(UICanvasNode& canvas, glm::vec2 viewportSize) {
    const uint32_t width = std::max(1u, static_cast<uint32_t>(std::round(viewportSize.x)));
    const uint32_t height = std::max(1u, static_cast<uint32_t>(std::round(viewportSize.y)));

    for (auto& child : canvas.children()) {
        if (auto* uiChild = dynamic_cast<UINode*>(child.get())) {
            uiChild->updateTransforms(0.0f, 0.0f,
                                      static_cast<float>(width), static_cast<float>(height));
        }
    }

    std::ostringstream markup;
    markup << std::fixed << std::setprecision(3)
           << "<rml><head><style>body{margin:0;width:100%;height:100%;background:transparent;}"
              "div{box-sizing:border-box;}</style></head><body>";
    appendTextElements(canvas, markup);
    markup << "</body></rml>";
    const std::string documentMarkup = markup.str();
    uint64_t contentHash = fnv1a(documentMarkup);
    contentHash ^= static_cast<uint64_t>(width) << 32;
    contentHash ^= height;

    if (!RmlUiRuntime::ensureInitialized()) return;
    auto* context = legacyContext_;
    if (!context) {
        std::ostringstream name;
        name << "LegacyWebUI_" << reinterpret_cast<uintptr_t>(this);
        legacyContextName_ = name.str();
        context = Rml::CreateContext(legacyContextName_,
                                     {static_cast<int>(width), static_cast<int>(height)},
                                     RmlUiRuntime::renderInterface());
        legacyContext_ = context;
        legacyUiHash_ = 0;
    } else if (legacyUiWidth_ != width || legacyUiHeight_ != height) {
        context->SetDimensions({static_cast<int>(width), static_cast<int>(height)});
        legacyUiHash_ = 0;
    }
    if (!context) {
        Log::error("UIRenderer: failed to create the Web RmlUi context");
        return;
    }

    const bool needsRaster = contentHash != legacyUiHash_ || !legacyUiTexture_ ||
        legacyUiTexture_->width() != width || legacyUiTexture_->height() != height;
    if (contentHash != legacyUiHash_) {
        if (legacyDocument_) {
            context->UnloadDocument(legacyDocument_);
            legacyDocument_ = nullptr;
        }
        Rml::ElementDocument* document =
            context->LoadDocumentFromMemory(documentMarkup, "<legacy-ui>");
        if (!document) {
            Log::error("UIRenderer: failed to build the legacy UI document");
            return;
        }
        document->Show();
        legacyDocument_ = document;
        legacyUiHash_ = contentHash;
        legacyUiWidth_ = width;
        legacyUiHeight_ = height;
    }

    if (needsRaster) {
        context->Update();
        RmlUiRenderInterface* rasterizer = RmlUiRuntime::renderer();
        if (!rasterizer) return;
        rasterizer->beginFrame(width, height);
        context->Render();
        rasterizer->endFrame();
        const std::vector<uint8_t>& pixels = rasterizer->pixels();
        if (pixels.empty()) return;

        if (!loggedLegacyUiRaster_) {
            size_t visiblePixels = 0;
            uint32_t minX = width, minY = height, maxX = 0, maxY = 0;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const size_t i = (static_cast<size_t>(y) * width + x) * 4 + 3;
                    if (pixels[i] == 0) continue;
                    ++visiblePixels;
                    minX = std::min(minX, x);
                    minY = std::min(minY, y);
                    maxX = std::max(maxX, x);
                    maxY = std::max(maxY, y);
                }
            }
            Log::info("UIRenderer: Web RmlUi raster contains ", visiblePixels,
                      " visible pixel(s), bbox=", minX, ",", minY, " -> ", maxX,
                      ",", maxY, " at ", width, "x", height);
            loggedLegacyUiRaster_ = true;
        }

        if (!legacyUiTexture_ || legacyUiTexture_->width() != width || legacyUiTexture_->height() != height) {
            legacyUiTexture_ = std::make_unique<rhi::Texture>(
                device_, pixels.data(), width, height, rhi::Format::RGBA8Unorm, false);
            legacyUiTextureGroup_.reset();
        } else {
            legacyUiTexture_->updatePixels(pixels.data(), pixels.size());
        }
        if (!legacyUiTextureGroup_) {
            rhi::BindGroupEntry textureEntry;
            textureEntry.binding = 0;
            textureEntry.view = legacyUiTexture_->imageView();
            rhi::BindGroupEntry samplerEntry;
            samplerEntry.binding = 1;
            samplerEntry.sampler = legacyUiTexture_->sampler();
            legacyUiTextureGroup_ = std::make_unique<rhi::BindGroup>(
                *textureLayout_, std::vector<rhi::BindGroupEntry>{textureEntry, samplerEntry});
        }
    }

    UIDrawCmd cmd{};
    cmd.position = {0.0f, 0.0f};
    cmd.size = {static_cast<float>(width), static_cast<float>(height)};
    cmd.color = glm::vec4(1.0f);
    cmd.hasTexture = 1;
    cmd.textureGroup = legacyUiTextureGroup_.get();
    drawCmds_.push_back(cmd);
}
#endif

void UIRenderer::gatherUI(Scene& scene, glm::vec2 viewportSize) {
    SAIDA_PROFILE_FUNCTION();
    drawCmds_.clear();
    webNodesToUpdate_.clear();
    
    UICanvasNode* canvas = scene.uiCanvas();
#ifndef SAIDA_RHI_WEBGPU
    for (WebCanvasNode* wcn : scene.webCanvases()) {
        if (!wcn || !wcn->isActiveInHierarchy()) continue;
        if (wcn->mode() == WebCanvasNode::Mode::ScreenSpace) {
            glm::vec2 pos = wcn->screenPosition();
            glm::vec2 size = wcn->screenSize();
            bool fillsViewport = viewportSize.x > 0.0f && viewportSize.y > 0.0f &&
                std::abs(pos.x) < 0.5f && std::abs(pos.y) < 0.5f &&
                std::abs(size.x - static_cast<float>(wcn->width())) < 0.5f &&
                std::abs(size.y - static_cast<float>(wcn->height())) < 0.5f;
            if (fillsViewport) {
                uint32_t targetWidth = std::max(1u, static_cast<uint32_t>(std::round(viewportSize.x)));
                uint32_t targetHeight = std::max(1u, static_cast<uint32_t>(std::round(viewportSize.y)));
                if (wcn->width() != targetWidth || wcn->height() != targetHeight) {
                    wcn->resize(targetWidth, targetHeight);
                }
            }
        }
        webNodesToUpdate_.push_back(wcn);
        if (wcn->mode() == WebCanvasNode::Mode::ScreenSpace && wcn->texture()) {
            UIDrawCmd cmd{};
            cmd.position = wcn->screenPosition();
            cmd.size = wcn->screenSize();
            cmd.color = glm::vec4(1.0f);
            cmd.textureId = resources_.ensureBindlessTextureIndex(wcn->texture());
            cmd.hasTexture = 1;
            cmd.sortOrder = wcn->renderOrder();
            drawCmds_.push_back(cmd);
        }
    }
#endif

#ifndef SAIDA_RHI_WEBGPU
    if (!loggedWebCanvasGather_ && !webNodesToUpdate_.empty()) {
        Log::info("UIRenderer: gathered ", webNodesToUpdate_.size(),
                  " WebCanvas node(s), ", drawCmds_.size(), " screen draw command(s)");
        loggedWebCanvasGather_ = true;
    }
#endif

    if (canvas && canvas->isActiveInHierarchy()) {
#ifdef SAIDA_RHI_WEBGPU
        gatherLegacyWebUI(*canvas, viewportSize);
#else
        for (auto& child : canvas->children()) {
            if (UINode* uiChild = dynamic_cast<UINode*>(child.get())) {
                traverseUI(uiChild);
            }
        }
#endif
    }

    std::stable_sort(drawCmds_.begin(), drawCmds_.end(), [](const UIDrawCmd& a, const UIDrawCmd& b) {
        return a.sortOrder < b.sortOrder;
    });

    SAIDA_PROFILE_COUNTER("UI/WebCanvases", webNodesToUpdate_.size());
    SAIDA_PROFILE_COUNTER("UI/DrawCommands", drawCmds_.size());
}

#ifndef SAIDA_RHI_WEBGPU
void UIRenderer::traverseUI(UINode* node) {
    if (!node->isActiveInHierarchy()) return;

    float globalX = node->globalX();
    float globalY = node->globalY();

    float drawX = globalX - (node->width() * node->pivotX());
    float drawY = globalY - (node->height() * node->pivotY());

    UIDrawCmd cmd{};
    cmd.position = {drawX, drawY};
    cmd.size = {node->width(), node->height()};
    cmd.color = glm::vec4(1.0f);
    cmd.textureId = 0;
    cmd.hasTexture = 0;

    bool draw = false;

    if (auto colorNode = dynamic_cast<UIColorNode*>(node)) {
        cmd.color = colorNode->color();
        draw = true;
    } else if (auto imageNode = dynamic_cast<UIImageNode*>(node)) {
        Texture* tex = resources_.getTexture(imageNode->texture());
        if (tex) {
            cmd.textureId = resources_.ensureBindlessTextureIndex(tex);
            cmd.hasTexture = 1;
            draw = true;
        }
    } else if (auto btnNode = dynamic_cast<UIButtonNode*>(node)) {
        cmd.color = btnNode->currentColor();
        draw = true;
    } else if (auto toggleNode = dynamic_cast<UIToggleNode*>(node)) {
        cmd.color = toggleNode->currentColor();
        draw = true;
    }

    if (draw) {
        drawCmds_.push_back(cmd);
    }

    for (auto& child : node->children()) {
        if (UINode* uiChild = dynamic_cast<UINode*>(child.get())) {
            traverseUI(uiChild);
        }
    }
}
#endif

void UIRenderer::updateAsyncTextures(rhi::CommandEncoder& encoder) {
    SAIDA_PROFILE_FUNCTION();
#ifdef SAIDA_RHI_WEBGPU
    (void)encoder;
#else
    for (auto* wcn : webNodesToUpdate_) {
        wcn->updateTextureIfNeededAsync(encoder);
    }
#endif
}

void UIRenderer::recordCommands(rhi::RenderPassEncoder& rp, uint32_t width, uint32_t height,
                                glm::vec2 viewportOffset, glm::vec2 viewportSize) {
    SAIDA_PROFILE_FUNCTION();
    if (drawCmds_.empty()) return;
    if (viewportSize.x <= 0.0f || viewportSize.y <= 0.0f) {
        viewportSize = {static_cast<float>(width), static_cast<float>(height)};
    }

    rp.setPipeline(*pipeline_);
    rp.setViewport(0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));

    const int32_t scissorX = static_cast<int32_t>(std::max(0.0f, std::floor(viewportOffset.x)));
    const int32_t scissorY = static_cast<int32_t>(std::max(0.0f, std::floor(viewportOffset.y)));
    rp.setScissor(scissorX, scissorY,
        std::min(width - static_cast<uint32_t>(scissorX),
                 static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize.x)))),
        std::min(height - static_cast<uint32_t>(scissorY),
                 static_cast<uint32_t>(std::max(1.0f, std::round(viewportSize.y)))));

    // Set 0 matches ui.frag: bindless table on Vulkan, one texture on WebGPU.
#ifndef SAIDA_RHI_WEBGPU
    rp.setBindGroup(0, resources_.globalMaterialSet());
#endif

    UIPushConstants push{};
    push.screenSize = {static_cast<float>(width), static_cast<float>(height)};

    for (const auto& drawCmd : drawCmds_) {
#ifdef SAIDA_RHI_WEBGPU
        if (!drawCmd.textureGroup) continue;
        rp.setBindGroup(0, *drawCmd.textureGroup);
#endif
        push.position = viewportOffset + drawCmd.position;
        push.size = drawCmd.size;
        push.color = drawCmd.color;
        push.textureId = drawCmd.textureId;
        push.hasTexture = drawCmd.hasTexture;
        push.useCorners = drawCmd.useCorners;
        for (int i = 0; i < 4; ++i) push.corners[i] = drawCmd.corners[i];

        rp.setPushConstants(&push, sizeof(UIPushConstants));
        rp.draw(4);
    }
}

} // namespace saida
