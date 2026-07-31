// OpenXRBackend.cpp

#include "OpenXRBackend.h"

#include "Logger.h"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  define XR_USE_PLATFORM_WIN32
#  define XR_USE_GRAPHICS_API_OPENGL
#  include <openxr/openxr_platform.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace
{
const char* const kOpenXRLogPrefix = "[RiftRay OpenXR]";
const char* const kRequiredGraphicsExtension = "XR_KHR_opengl_enable";
const XrViewConfigurationType kViewConfiguration =
    XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
const uint32_t kExpectedViewCount = 2;

XrPosef IdentityPose()
{
    XrPosef pose = {};
    pose.orientation.w = 1.f;
    return pose;
}
}

OpenXRBackend::ViewTarget::ViewTarget()
    : swapchain(XR_NULL_HANDLE)
    , format(0)
    , width(0)
    , height(0)
    , textures()
    , framebuffer()
    , imageAcquired(false)
    , acquiredImageIndex(0)
    , renderRect()
{
}

OpenXRBackend::OpenXRBackend()
    : m_instance(XR_NULL_HANDLE)
    , m_systemId(XR_NULL_SYSTEM_ID)
    , m_session(XR_NULL_HANDLE)
    , m_appSpace(XR_NULL_HANDLE)
    , m_sessionState(XR_SESSION_STATE_UNKNOWN)
    , m_sessionRunning(false)
    , m_getOpenGLGraphicsRequirements(NULL)
    , m_viewConfigurations()
    , m_views()
    , m_viewTargets()
    , m_viewState{ XR_TYPE_VIEW_STATE }
    , m_viewsLocated(false)
    , m_frameState{ XR_TYPE_FRAME_STATE }
    , m_frameBegun(false)
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

    const bool found = std::find_if(
        extensions.begin(), extensions.end(),
        [](const XrExtensionProperties& extension)
        {
            return std::strcmp(extension.extensionName, kRequiredGraphicsExtension) == 0;
        }) != extensions.end();
    if (!found)
    {
        LOG_ERROR("%s Runtime does not expose required extension %s",
            kOpenXRLogPrefix, kRequiredGraphicsExtension);
    }
    return found;
}

bool OpenXRBackend::Initialize()
{
    Shutdown();

    if (!HasRequiredExtensions())
        return false;

    const char* const enabledExtensions[] = { kRequiredGraphicsExtension };
    XrInstanceCreateInfo createInfo = { XR_TYPE_INSTANCE_CREATE_INFO };
    std::snprintf(
        createInfo.applicationInfo.applicationName,
        XR_MAX_APPLICATION_NAME_SIZE, "%s", "RiftRay");
    std::snprintf(
        createInfo.applicationInfo.engineName,
        XR_MAX_ENGINE_NAME_SIZE, "%s", "RiftRay native renderer");
    createInfo.applicationInfo.applicationVersion = 1;
    createInfo.applicationInfo.engineVersion = 1;
    createInfo.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
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

    if (!LoadOpenGLFunctions())
    {
        Shutdown();
        return false;
    }
    return true;
}

bool OpenXRBackend::LoadOpenGLFunctions()
{
    PFN_xrVoidFunction function = NULL;
    const XrResult result = xrGetInstanceProcAddr(
        m_instance, "xrGetOpenGLGraphicsRequirementsKHR", &function);
    if (XR_FAILED(result) || function == NULL)
    {
        LOG_ERROR("%s Unable to load xrGetOpenGLGraphicsRequirementsKHR: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }
    m_getOpenGLGraphicsRequirements = function;
    return true;
}

bool OpenXRBackend::ValidateOpenGLRequirements() const
{
    XrGraphicsRequirementsOpenGLKHR requirements = {
        XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR
    };
    const PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements =
        reinterpret_cast<PFN_xrGetOpenGLGraphicsRequirementsKHR>(
            m_getOpenGLGraphicsRequirements);
    const XrResult result = getRequirements(
        m_instance, m_systemId, &requirements);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s OpenGL graphics requirements query failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    const XrVersion currentVersion = XR_MAKE_VERSION(major, minor, 0);
    if (currentVersion < requirements.minApiVersionSupported ||
        currentVersion > requirements.maxApiVersionSupported)
    {
        LOG_ERROR("%s OpenGL %d.%d is outside runtime range %u.%u through %u.%u",
            kOpenXRLogPrefix,
            major, minor,
            XR_VERSION_MAJOR(requirements.minApiVersionSupported),
            XR_VERSION_MINOR(requirements.minApiVersionSupported),
            XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
            XR_VERSION_MINOR(requirements.maxApiVersionSupported));
        return false;
    }

    LOG_INFO("%s OpenGL %d.%d satisfies runtime graphics requirements",
        kOpenXRLogPrefix, major, minor);
    return true;
}

bool OpenXRBackend::InitializeSession()
{
    if (!IsInstanceReady() || !HasSystem() ||
        m_getOpenGLGraphicsRequirements == NULL)
        return false;
    if (IsSessionReady())
        return true;
    if (wglGetCurrentDC() == NULL || wglGetCurrentContext() == NULL)
    {
        LOG_ERROR("%s No current WGL context for session creation", kOpenXRLogPrefix);
        return false;
    }
    if (!ValidateOpenGLRequirements())
        return false;

    XrGraphicsBindingOpenGLWin32KHR binding = {
        XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR
    };
    binding.hDC = wglGetCurrentDC();
    binding.hGLRC = wglGetCurrentContext();

    XrSessionCreateInfo createInfo = { XR_TYPE_SESSION_CREATE_INFO };
    createInfo.next = &binding;
    createInfo.systemId = m_systemId;
    XrResult result = xrCreateSession(m_instance, &createInfo, &m_session);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrCreateSession failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        m_session = XR_NULL_HANDLE;
        return false;
    }

    if (!CreateReferenceSpace() || !CreateViewSwapchains())
    {
        ShutdownSession();
        return false;
    }

    LOG_INFO("%s WGL session and stereo swapchains created", kOpenXRLogPrefix);
    return true;
}

bool OpenXRBackend::CreateReferenceSpace()
{
    XrReferenceSpaceCreateInfo createInfo = {
        XR_TYPE_REFERENCE_SPACE_CREATE_INFO
    };
    createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    createInfo.poseInReferenceSpace = IdentityPose();
    const XrResult result = xrCreateReferenceSpace(
        m_session, &createInfo, &m_appSpace);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to create LOCAL reference space: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        m_appSpace = XR_NULL_HANDLE;
        return false;
    }
    return true;
}

int64_t OpenXRBackend::ChooseSwapchainFormat() const
{
    uint32_t formatCount = 0;
    XrResult result = xrEnumerateSwapchainFormats(
        m_session, 0, &formatCount, NULL);
    if (XR_FAILED(result) || formatCount == 0)
        return 0;

    std::vector<int64_t> formats(formatCount);
    result = xrEnumerateSwapchainFormats(
        m_session, formatCount, &formatCount, formats.data());
    if (XR_FAILED(result))
        return 0;

    const int64_t preferences[] = { GL_SRGB8_ALPHA8, GL_RGBA8 };
    for (size_t preference = 0;
        preference < sizeof(preferences) / sizeof(preferences[0]); ++preference)
    {
        if (std::find(formats.begin(), formats.end(), preferences[preference]) != formats.end())
            return preferences[preference];
    }
    return 0;
}

bool OpenXRBackend::CreateViewSwapchains()
{
    uint32_t viewCount = 0;
    XrResult result = xrEnumerateViewConfigurationViews(
        m_instance, m_systemId, kViewConfiguration,
        0, &viewCount, NULL);
    if (XR_FAILED(result) || viewCount != kExpectedViewCount)
    {
        LOG_ERROR("%s PRIMARY_STEREO view count is %u; expected %u",
            kOpenXRLogPrefix, viewCount, kExpectedViewCount);
        return false;
    }

    m_viewConfigurations.assign(
        viewCount, XrViewConfigurationView{ XR_TYPE_VIEW_CONFIGURATION_VIEW });
    result = xrEnumerateViewConfigurationViews(
        m_instance, m_systemId, kViewConfiguration,
        viewCount, &viewCount, m_viewConfigurations.data());
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to enumerate view configuration: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    const int64_t format = ChooseSwapchainFormat();
    if (format == 0)
    {
        LOG_ERROR("%s Runtime offers neither GL_SRGB8_ALPHA8 nor GL_RGBA8",
            kOpenXRLogPrefix);
        return false;
    }

    m_views.assign(viewCount, XrView{ XR_TYPE_VIEW });
    m_viewTargets.resize(viewCount);
    for (uint32_t view = 0; view < viewCount; ++view)
    {
        if (!CreateViewTarget(m_viewTargets[view], m_viewConfigurations[view], format))
            return false;
    }
    return true;
}

bool OpenXRBackend::CreateViewTarget(
    ViewTarget& target,
    const XrViewConfigurationView& configuration,
    int64_t format)
{
    target.width = configuration.recommendedImageRectWidth;
    target.height = configuration.recommendedImageRectHeight;
    target.format = format;

    XrSwapchainCreateInfo createInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.format = format;
    createInfo.sampleCount = 1;
    createInfo.width = target.width;
    createInfo.height = target.height;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;
    XrResult result = xrCreateSwapchain(m_session, &createInfo, &target.swapchain);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to create %ux%u view swapchain: %s",
            kOpenXRLogPrefix, target.width, target.height,
            DescribeResult(result).c_str());
        return false;
    }

    uint32_t imageCount = 0;
    result = xrEnumerateSwapchainImages(
        target.swapchain, 0, &imageCount, NULL);
    if (XR_FAILED(result) || imageCount == 0)
        return false;

    std::vector<XrSwapchainImageOpenGLKHR> images(
        imageCount, XrSwapchainImageOpenGLKHR{ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
    result = xrEnumerateSwapchainImages(
        target.swapchain, imageCount, &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    if (XR_FAILED(result))
        return false;

    target.textures.resize(imageCount);
    for (uint32_t image = 0; image < imageCount; ++image)
    {
        target.textures[image] = images[image].image;
        glBindTexture(GL_TEXTURE_2D, images[image].image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    target.framebuffer.w = target.width;
    target.framebuffer.h = target.height;
    glGenFramebuffers(1, &target.framebuffer.id);
    glGenRenderbuffers(1, &target.framebuffer.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, target.framebuffer.depth);
    glRenderbufferStorage(
        GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, target.width, target.height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    target.renderRect.offset.x = 0;
    target.renderRect.offset.y = 0;
    target.renderRect.extent.width = target.width;
    target.renderRect.extent.height = target.height;

    LOG_INFO("%s View swapchain: %ux%u, %u images, format 0x%llx",
        kOpenXRLogPrefix, target.width, target.height, imageCount,
        static_cast<unsigned long long>(format));
    return true;
}

bool OpenXRBackend::HandleSessionStateChanged(
    const XrEventDataSessionStateChanged& event, bool& shouldExit)
{
    if (event.session != XR_NULL_HANDLE && event.session != m_session)
        return true;

    m_sessionState = event.state;
    LOG_INFO("%s Session state changed to %d",
        kOpenXRLogPrefix, static_cast<int>(m_sessionState));

    if (m_sessionState == XR_SESSION_STATE_READY && !m_sessionRunning)
    {
        XrSessionBeginInfo beginInfo = { XR_TYPE_SESSION_BEGIN_INFO };
        beginInfo.primaryViewConfigurationType = kViewConfiguration;
        const XrResult result = xrBeginSession(m_session, &beginInfo);
        if (XR_FAILED(result))
        {
            LOG_ERROR("%s xrBeginSession failed: %s",
                kOpenXRLogPrefix, DescribeResult(result).c_str());
            return false;
        }
        m_sessionRunning = true;
    }
    else if (m_sessionState == XR_SESSION_STATE_STOPPING && m_sessionRunning)
    {
        const XrResult result = xrEndSession(m_session);
        if (XR_FAILED(result))
        {
            LOG_ERROR("%s xrEndSession failed: %s",
                kOpenXRLogPrefix, DescribeResult(result).c_str());
            return false;
        }
        m_sessionRunning = false;
    }
    else if (m_sessionState == XR_SESSION_STATE_EXITING ||
        m_sessionState == XR_SESSION_STATE_LOSS_PENDING)
    {
        shouldExit = true;
        m_sessionRunning = false;
    }
    return true;
}

bool OpenXRBackend::PollEvents(bool& shouldExit)
{
    shouldExit = false;
    if (!IsInstanceReady())
        return true;

    XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
    XrResult result = XR_SUCCESS;
    while ((result = xrPollEvent(m_instance, &event)) == XR_SUCCESS)
    {
        if (event.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED)
        {
            const XrEventDataSessionStateChanged* changed =
                reinterpret_cast<const XrEventDataSessionStateChanged*>(&event);
            if (!HandleSessionStateChanged(*changed, shouldExit))
                return false;
        }
        else if (event.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING)
        {
            shouldExit = true;
        }

        event = XrEventDataBuffer{ XR_TYPE_EVENT_DATA_BUFFER };
    }

    if (result != XR_EVENT_UNAVAILABLE)
    {
        LOG_ERROR("%s xrPollEvent failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }
    return true;
}

bool OpenXRBackend::BeginFrame()
{
    if (!m_sessionRunning || m_frameBegun)
        return false;

    m_frameState = XrFrameState{ XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo waitInfo = { XR_TYPE_FRAME_WAIT_INFO };
    XrResult result = xrWaitFrame(m_session, &waitInfo, &m_frameState);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrWaitFrame failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    XrFrameBeginInfo beginInfo = { XR_TYPE_FRAME_BEGIN_INFO };
    result = xrBeginFrame(m_session, &beginInfo);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrBeginFrame failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }
    m_frameBegun = true;
    m_viewsLocated = false;
    return true;
}

bool OpenXRBackend::LocateViews()
{
    if (!m_frameBegun)
        return false;

    XrViewLocateInfo locateInfo = { XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = kViewConfiguration;
    locateInfo.displayTime = m_frameState.predictedDisplayTime;
    locateInfo.space = m_appSpace;
    m_viewState = XrViewState{ XR_TYPE_VIEW_STATE };
    uint32_t viewCount = 0;
    const XrResult result = xrLocateViews(
        m_session, &locateInfo, &m_viewState,
        static_cast<uint32_t>(m_views.size()), &viewCount, m_views.data());
    if (XR_FAILED(result) || viewCount != m_views.size())
    {
        LOG_ERROR("%s xrLocateViews failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    const XrViewStateFlags requiredFlags =
        XR_VIEW_STATE_POSITION_VALID_BIT | XR_VIEW_STATE_ORIENTATION_VALID_BIT;
    if ((m_viewState.viewStateFlags & requiredFlags) != requiredFlags)
    {
        LOG_INFO("%s View pose is not currently valid", kOpenXRLogPrefix);
        return false;
    }
    m_viewsLocated = true;
    return true;
}

bool OpenXRBackend::BeginView(uint32_t viewIndex)
{
    if (!m_frameBegun || !m_viewsLocated ||
        m_frameState.shouldRender != XR_TRUE || viewIndex >= m_viewTargets.size())
        return false;
    ViewTarget& target = m_viewTargets[viewIndex];
    if (target.imageAcquired)
        return false;

    XrSwapchainImageAcquireInfo acquireInfo = {
        XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO
    };
    XrResult result = xrAcquireSwapchainImage(
        target.swapchain, &acquireInfo, &target.acquiredImageIndex);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrAcquireSwapchainImage failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }
    target.imageAcquired = true;

    XrSwapchainImageWaitInfo waitInfo = {
        XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO
    };
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = xrWaitSwapchainImage(target.swapchain, &waitInfo);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrWaitSwapchainImage failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        EndView(viewIndex);
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer.id);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        target.textures[target.acquiredImageIndex], 0);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
        target.framebuffer.depth);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_ERROR("%s View framebuffer is incomplete: 0x%x",
            kOpenXRLogPrefix, status);
        EndView(viewIndex);
        return false;
    }
    return true;
}

bool OpenXRBackend::EndView(uint32_t viewIndex)
{
    if (viewIndex >= m_viewTargets.size())
        return false;
    ViewTarget& target = m_viewTargets[viewIndex];
    if (!target.imageAcquired)
        return true;

    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    XrSwapchainImageReleaseInfo releaseInfo = {
        XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
    };
    const XrResult result = xrReleaseSwapchainImage(
        target.swapchain, &releaseInfo);
    target.imageAcquired = false;
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrReleaseSwapchainImage failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }
    return true;
}

void OpenXRBackend::SetViewRenderRect(
    uint32_t viewIndex, int32_t x, int32_t y, int32_t width, int32_t height)
{
    if (viewIndex >= m_viewTargets.size())
        return;
    ViewTarget& target = m_viewTargets[viewIndex];
    target.renderRect.offset.x = x;
    target.renderRect.offset.y = y;
    target.renderRect.extent.width = width;
    target.renderRect.extent.height = height;
}

void OpenXRBackend::ReleaseOutstandingImages()
{
    for (uint32_t view = 0; view < m_viewTargets.size(); ++view)
    {
        if (m_viewTargets[view].imageAcquired)
            EndView(view);
    }
}

bool OpenXRBackend::EndFrame(const XrCompositionLayerQuad* quadLayer)
{
    if (!m_frameBegun)
        return false;
    ReleaseOutstandingImages();

    std::vector<XrCompositionLayerProjectionView> projectionViews;
    XrCompositionLayerProjection projectionLayer = {
        XR_TYPE_COMPOSITION_LAYER_PROJECTION
    };
    std::vector<const XrCompositionLayerBaseHeader*> layers;

    if (m_frameState.shouldRender == XR_TRUE && m_viewsLocated)
    {
        projectionViews.assign(
            m_views.size(),
            XrCompositionLayerProjectionView{
                XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW });
        for (uint32_t view = 0; view < m_views.size(); ++view)
        {
            projectionViews[view].pose = m_views[view].pose;
            projectionViews[view].fov = m_views[view].fov;
            projectionViews[view].subImage.swapchain = m_viewTargets[view].swapchain;
            projectionViews[view].subImage.imageRect = m_viewTargets[view].renderRect;
            projectionViews[view].subImage.imageArrayIndex = 0;
        }
        projectionLayer.space = m_appSpace;
        projectionLayer.viewCount = static_cast<uint32_t>(projectionViews.size());
        projectionLayer.views = projectionViews.data();
        layers.push_back(
            reinterpret_cast<const XrCompositionLayerBaseHeader*>(&projectionLayer));
        if (quadLayer != NULL)
        {
            layers.push_back(
                reinterpret_cast<const XrCompositionLayerBaseHeader*>(quadLayer));
        }
    }

    XrFrameEndInfo endInfo = { XR_TYPE_FRAME_END_INFO };
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = static_cast<uint32_t>(layers.size());
    endInfo.layers = layers.empty() ? NULL : layers.data();
    const XrResult result = xrEndFrame(m_session, &endInfo);
    m_frameBegun = false;
    m_viewsLocated = false;
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrEndFrame failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }
    return true;
}

const FBO& OpenXRBackend::GetViewFramebuffer(uint32_t viewIndex) const
{
    return m_viewTargets.at(viewIndex).framebuffer;
}

uint32_t OpenXRBackend::GetViewWidth(uint32_t viewIndex) const
{
    return m_viewTargets.at(viewIndex).width;
}

uint32_t OpenXRBackend::GetViewHeight(uint32_t viewIndex) const
{
    return m_viewTargets.at(viewIndex).height;
}

void OpenXRBackend::ShutdownSession()
{
    ReleaseOutstandingImages();
    m_frameBegun = false;
    m_viewsLocated = false;
    m_sessionRunning = false;
    m_sessionState = XR_SESSION_STATE_UNKNOWN;

    for (size_t view = 0; view < m_viewTargets.size(); ++view)
    {
        ViewTarget& target = m_viewTargets[view];
        if (target.framebuffer.id != 0)
            glDeleteFramebuffers(1, &target.framebuffer.id);
        if (target.framebuffer.depth != 0)
            glDeleteRenderbuffers(1, &target.framebuffer.depth);
        if (target.swapchain != XR_NULL_HANDLE)
            xrDestroySwapchain(target.swapchain);
    }
    m_viewTargets.clear();
    m_views.clear();
    m_viewConfigurations.clear();

    if (m_appSpace != XR_NULL_HANDLE)
    {
        xrDestroySpace(m_appSpace);
        m_appSpace = XR_NULL_HANDLE;
    }
    if (m_session != XR_NULL_HANDLE)
    {
        xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }
}

void OpenXRBackend::Shutdown()
{
    ShutdownSession();
    m_systemId = XR_NULL_SYSTEM_ID;
    m_getOpenGLGraphicsRequirements = NULL;
    if (m_instance != XR_NULL_HANDLE)
    {
        const XrResult result = xrDestroyInstance(m_instance);
        if (XR_FAILED(result))
        {
            LOG_ERROR("%s xrDestroyInstance failed: %d",
                kOpenXRLogPrefix, static_cast<int>(result));
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
