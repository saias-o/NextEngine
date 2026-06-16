#pragma once

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace ne { class VulkanDevice; }

namespace ne::xr {

class Instance;
class Swapchain;

// Everything one eye needs to be rendered this frame: the acquired XR image to
// draw into, and the per-eye camera matrices (already in the engine's Vulkan
// conventions). The render callback fills `image` and leaves it in
// COLOR_ATTACHMENT_OPTIMAL.
struct EyeView {
    VkImage image;
    VkImageView imageView;
    VkExtent2D extent;
    glm::mat4 view;
    glm::mat4 projection;
    glm::vec3 eyePosition;
};

// The OpenXR session: its reference space, the PRIMARY_STEREO views, the per-eye
// swapchains, and the frame loop (xrWaitFrame → locateViews → per-eye render →
// xrEndFrame). This is the XR presentation seam — the analogue of the desktop
// acquire/submit/present in Renderer::drawFrame. It owns the command buffers and
// fence used to submit eye renders. Borrows the Instance and VulkanDevice.
class Session {
public:
    Session(Instance& instance, VulkanDevice& device);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Drain the OpenXR event queue and advance the session state machine
    // (begin/end session on READY/STOPPING). Returns false once the app must quit.
    bool pollEvents();
    bool running() const { return running_; }

    // Records the whole stereo frame into one command buffer. All eye images are
    // already acquired/waited (layout in: UNDEFINED; the callback must leave each
    // in COLOR_ATTACHMENT_OPTIMAL). One callback per frame (not per eye) so the
    // renderer can draw both eyes in a single multiview pass.
    using RenderFrameFn = std::function<void(VkCommandBuffer cmd,
                                             const std::vector<EyeView>& eyes)>;

    // One complete XR frame. Handles pacing via xrWaitFrame even when the runtime
    // says not to render (keeps the compositor happy). Safe to call only while
    // running(); a no-op otherwise.
    void renderFrame(const RenderFrameFn& render);

    // Per-eye render extent (recommended by the runtime). Valid after construction.
    VkExtent2D eyeExtent() const;
    int64_t colorFormat() const { return colorFormat_; }

    // Recenter the reference space (locomotion/teleport). `position` + `yawRadians`
    // (about +Y) place the player rig; the runtime then reports head/controllers in
    // this frame, so all poses come back world-space and the compositor stays
    // consistent. No-op if unchanged. Cheap (only recreates the XrSpace on change).
    void setReferenceOffset(const glm::vec3& position, float yawRadians);

    // Latest head pose (updated each rendered frame), for driving the engine
    // camera / audio listener.
    glm::mat4 headView() const { return headView_; }
    glm::vec3 headPosition() const { return headPosition_; }
    glm::quat headOrientation() const { return headOrientation_; }

    uint32_t viewCount() const { return static_cast<uint32_t>(swapchains_.size()); }

private:
    void createSession();
    void createReferenceSpace();
    void enumerateViewConfig();
    void enumerateBlendModes();
    int64_t chooseColorFormat() const;
    void createSwapchains();
    void createCommandResources();
    void onSessionStateChanged(XrSessionState state);

    Instance& instance_;
    VulkanDevice& device_;

    XrSession session_ = XR_NULL_HANDLE;
    XrSpace appSpace_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    static constexpr XrViewConfigurationType kViewConfig =
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    std::vector<XrViewConfigurationView> viewConfigs_;
    std::vector<std::unique_ptr<Swapchain>> swapchains_;
    int64_t colorFormat_ = 0;

    // Environment blend: OPAQUE (VR) by default; an alpha-blend/additive mode is
    // selected for passthrough (AR) when the runtime + view config offer one.
    XrEnvironmentBlendMode opaqueMode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    XrEnvironmentBlendMode passthroughMode_ = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    bool passthroughSupported_ = false;

    // Per-frame-slot command buffer + fence (double-buffered) for eye submits.
    static constexpr uint32_t kFrameSlots = 2;
    std::vector<VkCommandBuffer> cmdBuffers_;
    std::vector<VkFence> fences_;
    uint32_t frameSlot_ = 0;

    bool running_ = false;        // between xrBeginSession and xrEndSession
    bool exitRequested_ = false;

    glm::mat4 headView_{1.0f};
    glm::vec3 headPosition_{0.0f};
    glm::quat headOrientation_{1.0f, 0.0f, 0.0f, 0.0f};

    float nearZ_ = 0.05f;
    float farZ_ = 1000.0f;

    // Player rig offset applied to the reference space (set via setReferenceOffset).
    glm::vec3 originPos_{0.0f};
    float originYaw_ = 0.0f;
};

} // namespace ne::xr
