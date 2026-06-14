#include "xr/XrPlatform.hpp"   // must precede the XR headers (include order)
#include "xr/XrInstance.hpp"

#include "core/Log.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace ne::xr {

namespace {

// The single instance extension we require: OpenXR drives Vulkan instance/device
// creation so it can pick the GPU the headset is attached to.
const char* kRequiredExtensions[] = {XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME};

const char* resultName(XrInstance instance, XrResult r) {
    static char buf[XR_MAX_RESULT_STRING_SIZE];
    if (instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(instance, r, buf)))
        return buf;
    std::snprintf(buf, sizeof(buf), "XrResult(%d)", static_cast<int>(r));
    return buf;
}

// Create an instance + resolve the HMD system. Returns true on full success.
// Used both by the throwaway probe and the real ctor (which then keeps them).
bool createInstanceAndSystem(XrInstance& outInstance, XrSystemId& outSystem) {
    XrInstanceCreateInfo ci{};
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    std::strncpy(ci.applicationInfo.applicationName, "NextEngine",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    ci.applicationInfo.applicationVersion = 1;
    std::strncpy(ci.applicationInfo.engineName, "NextEngine",
                 XR_MAX_ENGINE_NAME_SIZE - 1);
    ci.applicationInfo.engineVersion = 1;
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    ci.enabledExtensionCount = static_cast<uint32_t>(std::size(kRequiredExtensions));
    ci.enabledExtensionNames = kRequiredExtensions;

    XrResult r = xrCreateInstance(&ci, &outInstance);
    if (XR_FAILED(r)) return false;  // no runtime / extension unsupported

    XrSystemGetInfo sysInfo{};
    sysInfo.type = XR_TYPE_SYSTEM_GET_INFO;
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = xrGetSystem(outInstance, &sysInfo, &outSystem);
    if (XR_FAILED(r)) {  // runtime present but no headset connected
        xrDestroyInstance(outInstance);
        outInstance = XR_NULL_HANDLE;
        return false;
    }
    return true;
}

} // namespace

void check(XrResult r, const char* what) {
    if (XR_FAILED(r))
        throw std::runtime_error(std::string("OpenXR: ") + what + " failed: " +
                                 resultName(XR_NULL_HANDLE, r));
}

bool Instance::headsetPresent() {
    XrInstance instance = XR_NULL_HANDLE;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    if (!createInstanceAndSystem(instance, system)) return false;
    xrDestroyInstance(instance);
    return true;
}

Instance::Instance() {
    if (!createInstanceAndSystem(instance_, systemId_))
        throw std::runtime_error("OpenXR: failed to create instance / find HMD system");

    XrInstanceProperties props{};
    props.type = XR_TYPE_INSTANCE_PROPERTIES;
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance_, &props)))
        Log::info("OpenXR runtime: ", props.runtimeName);

    XrSystemProperties sysProps{};
    sysProps.type = XR_TYPE_SYSTEM_PROPERTIES;
    if (XR_SUCCEEDED(xrGetSystemProperties(instance_, systemId_, &sysProps)))
        Log::info("XR system: ", sysProps.systemName);

    loadFunctions();
}

Instance::~Instance() {
    if (instance_ != XR_NULL_HANDLE) xrDestroyInstance(instance_);
}

void Instance::loadFunctions() {
    auto load = [this](const char* name, PFN_xrVoidFunction& out) {
        check(xrGetInstanceProcAddr(instance_, name, &out), name);
    };
    load("xrGetVulkanGraphicsRequirements2KHR", pfnGetVulkanGraphicsRequirements2);
    load("xrCreateVulkanInstanceKHR", pfnCreateVulkanInstance);
    load("xrGetVulkanGraphicsDevice2KHR", pfnGetVulkanGraphicsDevice2);
    load("xrCreateVulkanDeviceKHR", pfnCreateVulkanDevice);
}

} // namespace ne::xr
