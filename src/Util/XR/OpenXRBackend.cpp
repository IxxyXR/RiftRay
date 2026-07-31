// OpenXRBackend.cpp

#include "OpenXRBackend.h"

#include "Logger.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>

namespace
{
const char* const kOpenXRLogPrefix = "[RiftRay OpenXR]";
const char* const kRequiredGraphicsExtension = "XR_KHR_opengl_enable";
}

OpenXRBackend::OpenXRBackend()
    : m_instance(XR_NULL_HANDLE)
    , m_systemId(XR_NULL_SYSTEM_ID)
{
}

OpenXRBackend::~OpenXRBackend()
{
    Shutdown();
}

bool OpenXRBackend::HasRequiredExtensions() const
{
    uint32_t extensionCount = 0;
    XrResult result = xrEnumerateInstanceExtensionProperties(
        NULL, 0, &extensionCount, NULL);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to enumerate extension count: %d",
            kOpenXRLogPrefix, static_cast<int>(result));
        return false;
    }

    std::vector<XrExtensionProperties> extensions(
        extensionCount, XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES });
    result = xrEnumerateInstanceExtensionProperties(
        NULL, extensionCount, &extensionCount, extensions.data());
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to enumerate extensions: %d",
            kOpenXRLogPrefix, static_cast<int>(result));
        return false;
    }

    const char* const requiredExtension = kRequiredGraphicsExtension;
    const bool found = std::find_if(
        extensions.begin(), extensions.end(),
        [requiredExtension](const XrExtensionProperties& extension)
        {
            return std::string(extension.extensionName) == requiredExtension;
        }) != extensions.end();
    if (!found)
    {
        LOG_ERROR("%s Runtime does not expose required extension %s",
            kOpenXRLogPrefix, requiredExtension);
    }
    return found;
}

bool OpenXRBackend::Initialize()
{
    Shutdown();

    if (!HasRequiredExtensions())
        return false;

    const char* const enabledExtensions[] = {
        kRequiredGraphicsExtension
    };

    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    std::snprintf(
        createInfo.applicationInfo.applicationName,
        XR_MAX_APPLICATION_NAME_SIZE,
        "%s",
        "RiftRay");
    std::snprintf(
        createInfo.applicationInfo.engineName,
        XR_MAX_ENGINE_NAME_SIZE,
        "%s",
        "RiftRay native renderer");
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    createInfo.enabledExtensionCount = 1;
    createInfo.enabledExtensionNames = enabledExtensions;

    XrResult result = xrCreateInstance(&createInfo, &m_instance);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrCreateInstance failed: %d",
            kOpenXRLogPrefix, static_cast<int>(result));
        m_instance = XR_NULL_HANDLE;
        return false;
    }

    XrInstanceProperties instanceProperties = { XR_TYPE_INSTANCE_PROPERTIES };
    result = xrGetInstanceProperties(m_instance, &instanceProperties);
    if (XR_SUCCEEDED(result))
    {
        LOG_INFO("%s Runtime: %s (%u.%u.%u)",
            kOpenXRLogPrefix,
            instanceProperties.runtimeName,
            XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
            XR_VERSION_MINOR(instanceProperties.runtimeVersion),
            XR_VERSION_PATCH(instanceProperties.runtimeVersion));
    }

    XrSystemGetInfo systemInfo = { XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    result = xrGetSystem(m_instance, &systemInfo, &m_systemId);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s No head-mounted display system: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        Shutdown();
        return false;
    }

    XrSystemProperties systemProperties = { XR_TYPE_SYSTEM_PROPERTIES };
    result = xrGetSystemProperties(m_instance, m_systemId, &systemProperties);
    if (XR_SUCCEEDED(result))
    {
        LOG_INFO("%s System: %s", kOpenXRLogPrefix, systemProperties.systemName);
    }

    return true;
}

void OpenXRBackend::Shutdown()
{
    m_systemId = XR_NULL_SYSTEM_ID;
    if (m_instance != XR_NULL_HANDLE)
    {
        const XrResult result = xrDestroyInstance(m_instance);
        if (XR_FAILED(result))
        {
            LOG_ERROR("%s xrDestroyInstance failed: %s",
                kOpenXRLogPrefix, DescribeResult(result).c_str());
        }
        m_instance = XR_NULL_HANDLE;
    }
}

std::string OpenXRBackend::DescribeResult(XrResult result) const
{
    if (m_instance != XR_NULL_HANDLE)
    {
        char resultText[XR_MAX_RESULT_STRING_SIZE] = {};
        if (XR_SUCCEEDED(xrResultToString(m_instance, result, resultText)))
            return resultText;
    }

    std::ostringstream stream;
    stream << static_cast<int>(result);
    return stream.str();
}
