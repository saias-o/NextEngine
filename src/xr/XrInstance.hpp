#pragma once

#include <vulkan/vulkan.h>
#include <openxr/openxr.h>

namespace ne::xr {

// Throw a std::runtime_error (with the XrResult name) when `r` is a failure.
void check(XrResult r, const char* what);
inline bool succeeded(XrResult r) { return r >= 0; }

// Owns the connection to the OpenXR runtime: the XrInstance and the selected
// head-mounted XrSystemId. Also caches the XR_KHR_vulkan_enable2 entry points
// (extension functions are not exported by the loader, only reachable via
// xrGetInstanceProcAddr). No graphics state here — just the runtime handle that
// XrVulkanBinding and Session borrow. RAII; non-copyable.
class Instance {
public:
    Instance();
    ~Instance();
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    // Probe whether an HMD + active runtime are present: spins up a throwaway
    // instance, queries the system, tears it down. Never throws — returns false
    // on any failure (no runtime, no headset, no vulkan_enable2).
    static bool headsetPresent();

    XrInstance handle() const { return instance_; }
    XrSystemId systemId() const { return systemId_; }

    // Cached vulkan_enable2 procs, stored as the generic XR proc type (core, so
    // this header needs no platform include). XrVulkanBinding casts them back.
    PFN_xrVoidFunction pfnGetVulkanGraphicsRequirements2 = nullptr;
    PFN_xrVoidFunction pfnCreateVulkanInstance = nullptr;
    PFN_xrVoidFunction pfnGetVulkanGraphicsDevice2 = nullptr;
    PFN_xrVoidFunction pfnCreateVulkanDevice = nullptr;

private:
    void loadFunctions();

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId systemId_ = XR_NULL_SYSTEM_ID;
};

} // namespace ne::xr
