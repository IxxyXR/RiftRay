// OpenXRBackend.h

#pragma once

#include <openxr/openxr.h>

#include <string>

/// Owns the platform-independent OpenXR instance and HMD system discovery state.
/// Graphics binding, session, swapchains, and actions will be added in later phases.
class OpenXRBackend
{
public:
    OpenXRBackend();
    ~OpenXRBackend();

    bool Initialize();
    void Shutdown();

    bool IsInstanceReady() const { return m_instance != XR_NULL_HANDLE; }
    bool HasSystem() const { return m_systemId != XR_NULL_SYSTEM_ID; }
    XrInstance GetInstance() const { return m_instance; }
    XrSystemId GetSystemId() const { return m_systemId; }

    std::string DescribeResult(XrResult result) const;

private:
    bool HasRequiredExtensions() const;

    XrInstance m_instance;
    XrSystemId m_systemId;

    OpenXRBackend(const OpenXRBackend&);
    OpenXRBackend& operator=(const OpenXRBackend&);
};
