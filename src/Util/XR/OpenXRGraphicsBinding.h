// OpenXRGraphicsBinding.h

#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

/// Platform graphics adapter used by the shared OpenXR session/frame policy.
class OpenXRGraphicsBinding
{
public:
    virtual ~OpenXRGraphicsBinding() {}

    virtual const char* RequiredExtensionName() const = 0;

    /// Captures the current platform GL context and validates runtime requirements.
    virtual XrResult PrepareSession(
        XrInstance instance, XrSystemId systemId,
        std::string& diagnostic) = 0;

    /// Returns the platform binding chained into XrSessionCreateInfo::next.
    virtual const void* SessionCreateNext() const = 0;

    /// Extracts GL/GLES texture identifiers from runtime-owned swapchain images.
    virtual XrResult EnumerateSwapchainTextures(
        XrSwapchain swapchain, std::vector<uint32_t>& textures) const = 0;
};

std::unique_ptr<OpenXRGraphicsBinding> CreatePlatformOpenXRGraphicsBinding();
