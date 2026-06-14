#pragma once

// Internal include used only by the xr/*.cpp translation units: it pulls in the
// OpenXR platform header, which on Windows references LARGE_INTEGER (and the
// Vulkan-binding structs reference Vulkan types). It MUST come before any use of
// the XR_KHR_vulkan_enable2 / Win32 structs, so this header fixes the include
// order in one place. Kept out of the public xr headers so <windows.h> never
// leaks into the rest of the engine (only XrInstance/XrSwapchain/XrSession/
// XrVulkanBinding .cpp need it).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <unknwn.h>   // IUnknown — referenced by openxr_platform.h's Win32 structs

#include <vulkan/vulkan.h>

#define XR_USE_GRAPHICS_API_VULKAN
#define XR_USE_PLATFORM_WIN32
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
