// OpenXRBackend.cpp

#include "OpenXRBackend.h"

#include "Logger.h"
#include "OpenXRGraphicsBinding.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace
{
const char* const kOpenXRLogPrefix = "[RiftRay OpenXR]";
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
    , framebufferValidated()
    , framebuffer()
    , imageAcquired(false)
    , acquiredImageIndex(0)
    , renderRect()
{
}

OpenXRBackend::InputState::InputState()
    : move{ 0.f, 0.f }
    , turn{ 0.f, 0.f }
    , leftTrigger(0.f)
    , rightTrigger(0.f)
    , toggleShader(false)
    , toggleHud(false)
    , click(false)
    , reset(false)
    , menu(false)
    , hold(false)
    , aimPoseValid(false)
    , aimPose(IdentityPose())
{
}

OpenXRBackend::OpenXRBackend()
    : m_instance(XR_NULL_HANDLE)
    , m_systemId(XR_NULL_SYSTEM_ID)
    , m_session(XR_NULL_HANDLE)
    , m_appSpace(XR_NULL_HANDLE)
    , m_viewSpace(XR_NULL_HANDLE)
    , m_environmentBlendMode(XR_ENVIRONMENT_BLEND_MODE_OPAQUE)
    , m_sessionState(XR_SESSION_STATE_UNKNOWN)
    , m_sessionRunning(false)
    , m_graphicsBinding(CreatePlatformOpenXRGraphicsBinding())
    , m_actionSet(XR_NULL_HANDLE)
    , m_moveAction(XR_NULL_HANDLE)
    , m_turnAction(XR_NULL_HANDLE)
    , m_leftTriggerAction(XR_NULL_HANDLE)
    , m_rightTriggerAction(XR_NULL_HANDLE)
    , m_toggleShaderAction(XR_NULL_HANDLE)
    , m_toggleHudAction(XR_NULL_HANDLE)
    , m_clickAction(XR_NULL_HANDLE)
    , m_resetAction(XR_NULL_HANDLE)
    , m_menuAction(XR_NULL_HANDLE)
    , m_holdAction(XR_NULL_HANDLE)
    , m_aimPoseAction(XR_NULL_HANDLE)
    , m_aimSpace(XR_NULL_HANDLE)
    , m_inputState()
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
    if (!m_graphicsBinding)
    {
        LOG_ERROR("%s No graphics binding exists for this platform",
            kOpenXRLogPrefix);
        return false;
    }
    const char* const requiredExtension =
        m_graphicsBinding->RequiredExtensionName();
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
        [requiredExtension](const XrExtensionProperties& extension)
        {
            return std::strcmp(extension.extensionName, requiredExtension) == 0;
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
        m_graphicsBinding->RequiredExtensionName()
    };
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

    uint32_t blendModeCount = 0;
    result = xrEnumerateEnvironmentBlendModes(
        m_instance, m_systemId, kViewConfiguration,
        0, &blendModeCount, NULL);
    if (XR_FAILED(result) || blendModeCount == 0)
    {
        LOG_ERROR("%s Unable to enumerate environment blend modes: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        Shutdown();
        return false;
    }
    std::vector<XrEnvironmentBlendMode> blendModes(blendModeCount);
    result = xrEnumerateEnvironmentBlendModes(
        m_instance, m_systemId, kViewConfiguration,
        blendModeCount, &blendModeCount, blendModes.data());
    if (XR_FAILED(result))
    {
        Shutdown();
        return false;
    }
    const std::vector<XrEnvironmentBlendMode>::const_iterator opaque =
        std::find(
            blendModes.begin(), blendModes.end(),
            XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
    m_environmentBlendMode = opaque != blendModes.end()
        ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : blendModes.front();

    XrSystemProperties systemProperties = { XR_TYPE_SYSTEM_PROPERTIES };
    result = xrGetSystemProperties(m_instance, m_systemId, &systemProperties);
    if (XR_SUCCEEDED(result))
    {
        LOG_INFO("%s System: %s", kOpenXRLogPrefix, systemProperties.systemName);
    }

    return true;
}

bool OpenXRBackend::InitializeSession()
{
    if (!IsInstanceReady() || !HasSystem() || !m_graphicsBinding)
        return false;
    if (IsSessionReady())
        return true;

    std::string graphicsDiagnostic;
    XrResult result = m_graphicsBinding->PrepareSession(
        m_instance, m_systemId, graphicsDiagnostic);
    if (!graphicsDiagnostic.empty())
        LOG_INFO("%s %s", kOpenXRLogPrefix, graphicsDiagnostic.c_str());
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Graphics binding preparation failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    XrSessionCreateInfo createInfo = { XR_TYPE_SESSION_CREATE_INFO };
    createInfo.next = m_graphicsBinding->SessionCreateNext();
    createInfo.systemId = m_systemId;
    result = xrCreateSession(m_instance, &createInfo, &m_session);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrCreateSession failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        m_session = XR_NULL_HANDLE;
        return false;
    }

    if (!CreateReferenceSpace() || !CreateActions() || !CreateViewSwapchains())
    {
        ShutdownSession();
        return false;
    }

    LOG_INFO("%s Graphics session and stereo swapchains created", kOpenXRLogPrefix);
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
    createInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    const XrResult viewResult = xrCreateReferenceSpace(
        m_session, &createInfo, &m_viewSpace);
    if (XR_FAILED(viewResult))
    {
        LOG_ERROR("%s Unable to create VIEW reference space: %s",
            kOpenXRLogPrefix, DescribeResult(viewResult).c_str());
        m_viewSpace = XR_NULL_HANDLE;
        return false;
    }
    return true;
}

bool OpenXRBackend::CreateAction(
    XrActionType type, const char* name, const char* localizedName,
    XrAction& action)
{
    XrActionCreateInfo createInfo = { XR_TYPE_ACTION_CREATE_INFO };
    createInfo.actionType = type;
    std::snprintf(
        createInfo.actionName, XR_MAX_ACTION_NAME_SIZE, "%s", name);
    std::snprintf(
        createInfo.localizedActionName,
        XR_MAX_LOCALIZED_ACTION_NAME_SIZE, "%s", localizedName);
    const XrResult result = xrCreateAction(m_actionSet, &createInfo, &action);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to create action %s: %s",
            kOpenXRLogPrefix, name, DescribeResult(result).c_str());
        action = XR_NULL_HANDLE;
        return false;
    }
    return true;
}

bool OpenXRBackend::SuggestBindings(
    const char* interactionProfile,
    const std::vector<std::pair<XrAction, const char*> >& bindings)
{
    XrPath profilePath = XR_NULL_PATH;
    XrResult result = xrStringToPath(
        m_instance, interactionProfile, &profilePath);
    if (XR_FAILED(result))
        return false;

    std::vector<XrActionSuggestedBinding> suggestions;
    suggestions.reserve(bindings.size());
    for (size_t index = 0; index < bindings.size(); ++index)
    {
        XrPath bindingPath = XR_NULL_PATH;
        result = xrStringToPath(m_instance, bindings[index].second, &bindingPath);
        if (XR_FAILED(result))
            return false;
        XrActionSuggestedBinding suggestion = {
            bindings[index].first, bindingPath
        };
        suggestions.push_back(suggestion);
    }

    XrInteractionProfileSuggestedBinding suggested = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING
    };
    suggested.interactionProfile = profilePath;
    suggested.suggestedBindings = suggestions.data();
    suggested.countSuggestedBindings =
        static_cast<uint32_t>(suggestions.size());
    result = xrSuggestInteractionProfileBindings(m_instance, &suggested);
    if (XR_FAILED(result))
    {
        LOG_INFO("%s Interaction profile %s was not accepted: %s",
            kOpenXRLogPrefix, interactionProfile, DescribeResult(result).c_str());
        return false;
    }
    return true;
}

bool OpenXRBackend::CreateActions()
{
    XrActionSetCreateInfo setInfo = { XR_TYPE_ACTION_SET_CREATE_INFO };
    std::snprintf(
        setInfo.actionSetName, XR_MAX_ACTION_SET_NAME_SIZE, "%s", "gameplay");
    std::snprintf(
        setInfo.localizedActionSetName,
        XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE, "%s", "RiftRay controls");
    setInfo.priority = 0;
    XrResult result = xrCreateActionSet(m_instance, &setInfo, &m_actionSet);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to create action set: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        m_actionSet = XR_NULL_HANDLE;
        return false;
    }

    if (!CreateAction(XR_ACTION_TYPE_VECTOR2F_INPUT,
            "move", "Move", m_moveAction) ||
        !CreateAction(XR_ACTION_TYPE_VECTOR2F_INPUT,
            "turn", "Turn", m_turnAction) ||
        !CreateAction(XR_ACTION_TYPE_FLOAT_INPUT,
            "left_trigger", "Move down", m_leftTriggerAction) ||
        !CreateAction(XR_ACTION_TYPE_FLOAT_INPUT,
            "right_trigger", "Move up", m_rightTriggerAction) ||
        !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT,
            "toggle_shader", "Enter or exit shader", m_toggleShaderAction) ||
        !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT,
            "toggle_hud", "Toggle controls", m_toggleHudAction) ||
        !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT,
            "click", "Click controls", m_clickAction) ||
        !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT,
            "reset", "Reset position", m_resetAction) ||
        !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT,
            "menu", "Recenter", m_menuAction) ||
        !CreateAction(XR_ACTION_TYPE_BOOLEAN_INPUT,
            "hold", "Move controls", m_holdAction) ||
        !CreateAction(XR_ACTION_TYPE_POSE_INPUT,
            "aim_pose", "Aim pose", m_aimPoseAction))
        return false;

    SuggestBindings(
        "/interaction_profiles/oculus/touch_controller",
        {
            { m_moveAction, "/user/hand/left/input/thumbstick" },
            { m_turnAction, "/user/hand/right/input/thumbstick" },
            { m_leftTriggerAction, "/user/hand/left/input/trigger/value" },
            { m_rightTriggerAction, "/user/hand/right/input/trigger/value" },
            { m_toggleShaderAction, "/user/hand/right/input/a/click" },
            { m_toggleHudAction, "/user/hand/right/input/b/click" },
            { m_clickAction, "/user/hand/left/input/x/click" },
            { m_resetAction, "/user/hand/left/input/y/click" },
            { m_menuAction, "/user/hand/left/input/menu/click" },
            { m_holdAction, "/user/hand/right/input/thumbstick/click" },
            { m_aimPoseAction, "/user/hand/right/input/aim/pose" }
        });
    SuggestBindings(
        "/interaction_profiles/valve/index_controller",
        {
            { m_moveAction, "/user/hand/left/input/thumbstick" },
            { m_turnAction, "/user/hand/right/input/thumbstick" },
            { m_leftTriggerAction, "/user/hand/left/input/trigger/value" },
            { m_rightTriggerAction, "/user/hand/right/input/trigger/value" },
            { m_toggleShaderAction, "/user/hand/right/input/a/click" },
            { m_toggleHudAction, "/user/hand/right/input/b/click" },
            { m_clickAction, "/user/hand/left/input/a/click" },
            { m_resetAction, "/user/hand/left/input/b/click" },
            { m_holdAction, "/user/hand/right/input/thumbstick/click" },
            { m_aimPoseAction, "/user/hand/right/input/aim/pose" }
        });
    SuggestBindings(
        "/interaction_profiles/khr/simple_controller",
        {
            { m_toggleShaderAction, "/user/hand/right/input/select/click" },
            { m_menuAction, "/user/hand/right/input/menu/click" },
            { m_aimPoseAction, "/user/hand/right/input/aim/pose" }
        });

    XrSessionActionSetsAttachInfo attachInfo = {
        XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO
    };
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_actionSet;
    result = xrAttachSessionActionSets(m_session, &attachInfo);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to attach action set: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    XrActionSpaceCreateInfo spaceInfo = {
        XR_TYPE_ACTION_SPACE_CREATE_INFO
    };
    spaceInfo.action = m_aimPoseAction;
    spaceInfo.poseInActionSpace = IdentityPose();
    result = xrCreateActionSpace(m_session, &spaceInfo, &m_aimSpace);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to create aim action space: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        m_aimSpace = XR_NULL_HANDLE;
        return false;
    }
    LOG_INFO("%s Gameplay action set attached", kOpenXRLogPrefix);
    return true;
}

bool OpenXRBackend::ReadBooleanAction(XrAction action, bool& value) const
{
    XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getInfo.action = action;
    XrActionStateBoolean state = { XR_TYPE_ACTION_STATE_BOOLEAN };
    const XrResult result = xrGetActionStateBoolean(m_session, &getInfo, &state);
    value = XR_SUCCEEDED(result) && state.isActive && state.currentState;
    return XR_SUCCEEDED(result);
}

bool OpenXRBackend::ReadFloatAction(XrAction action, float& value) const
{
    XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getInfo.action = action;
    XrActionStateFloat state = { XR_TYPE_ACTION_STATE_FLOAT };
    const XrResult result = xrGetActionStateFloat(m_session, &getInfo, &state);
    value = XR_SUCCEEDED(result) && state.isActive ? state.currentState : 0.f;
    return XR_SUCCEEDED(result);
}

bool OpenXRBackend::ReadVector2Action(XrAction action, XrVector2f& value) const
{
    XrActionStateGetInfo getInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    getInfo.action = action;
    XrActionStateVector2f state = { XR_TYPE_ACTION_STATE_VECTOR2F };
    const XrResult result = xrGetActionStateVector2f(m_session, &getInfo, &state);
    value = XR_SUCCEEDED(result) && state.isActive
        ? state.currentState : XrVector2f{ 0.f, 0.f };
    return XR_SUCCEEDED(result);
}

bool OpenXRBackend::SyncInput()
{
    m_inputState.aimPoseValid = false;
    if (!m_sessionRunning || m_actionSet == XR_NULL_HANDLE)
        return false;
    if (m_sessionState != XR_SESSION_STATE_FOCUSED)
    {
        m_inputState = InputState();
        return true;
    }

    XrActiveActionSet activeSet = { m_actionSet, XR_NULL_PATH };
    XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    XrResult result = xrSyncActions(m_session, &syncInfo);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s xrSyncActions failed: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    bool success = true;
    success &= ReadVector2Action(m_moveAction, m_inputState.move);
    success &= ReadVector2Action(m_turnAction, m_inputState.turn);
    success &= ReadFloatAction(m_leftTriggerAction, m_inputState.leftTrigger);
    success &= ReadFloatAction(m_rightTriggerAction, m_inputState.rightTrigger);
    success &= ReadBooleanAction(m_toggleShaderAction, m_inputState.toggleShader);
    success &= ReadBooleanAction(m_toggleHudAction, m_inputState.toggleHud);
    success &= ReadBooleanAction(m_clickAction, m_inputState.click);
    success &= ReadBooleanAction(m_resetAction, m_inputState.reset);
    success &= ReadBooleanAction(m_menuAction, m_inputState.menu);
    success &= ReadBooleanAction(m_holdAction, m_inputState.hold);

    XrActionStateGetInfo poseGetInfo = { XR_TYPE_ACTION_STATE_GET_INFO };
    poseGetInfo.action = m_aimPoseAction;
    XrActionStatePose poseState = { XR_TYPE_ACTION_STATE_POSE };
    result = xrGetActionStatePose(m_session, &poseGetInfo, &poseState);
    success &= XR_SUCCEEDED(result);
    if (XR_SUCCEEDED(result) && poseState.isActive &&
        m_aimSpace != XR_NULL_HANDLE && m_frameState.predictedDisplayTime != 0)
    {
        XrSpaceLocation location = { XR_TYPE_SPACE_LOCATION };
        result = xrLocateSpace(
            m_aimSpace, m_appSpace, m_frameState.predictedDisplayTime, &location);
        const XrSpaceLocationFlags required =
            XR_SPACE_LOCATION_POSITION_VALID_BIT |
            XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
        if (XR_SUCCEEDED(result) &&
            (location.locationFlags & required) == required)
        {
            m_inputState.aimPose = location.pose;
            m_inputState.aimPoseValid = true;
        }
    }
    return success;
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

    result = m_graphicsBinding->EnumerateSwapchainTextures(
        target.swapchain, target.textures);
    if (XR_FAILED(result))
    {
        LOG_ERROR("%s Unable to enumerate swapchain images: %s",
            kOpenXRLogPrefix, DescribeResult(result).c_str());
        return false;
    }

    const uint32_t imageCount = static_cast<uint32_t>(target.textures.size());
    target.framebufferValidated.assign(imageCount, false);
    for (uint32_t image = 0; image < imageCount; ++image)
    {
        glBindTexture(GL_TEXTURE_2D, target.textures[image]);
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
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer.id);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER,
        target.framebuffer.depth);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    if (!target.framebufferValidated[target.acquiredImageIndex])
    {
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            LOG_ERROR("%s View framebuffer is incomplete: 0x%x",
                kOpenXRLogPrefix, status);
            EndView(viewIndex);
            return false;
        }
        target.framebufferValidated[target.acquiredImageIndex] = true;
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

    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer.id);
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

bool OpenXRBackend::EndFrame(
    const XrCompositionLayerQuad* quadLayer, bool submitProjection)
{
    if (!m_frameBegun)
        return false;
    ReleaseOutstandingImages();

    std::vector<XrCompositionLayerProjectionView> projectionViews;
    XrCompositionLayerProjection projectionLayer = {
        XR_TYPE_COMPOSITION_LAYER_PROJECTION
    };
    std::vector<const XrCompositionLayerBaseHeader*> layers;

    if (submitProjection && m_frameState.shouldRender == XR_TRUE && m_viewsLocated)
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
    endInfo.environmentBlendMode = m_environmentBlendMode;
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

int64_t OpenXRBackend::GetColorFormat() const
{
    return m_viewTargets.empty() ? 0 : m_viewTargets[0].format;
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

    if (m_aimSpace != XR_NULL_HANDLE)
    {
        xrDestroySpace(m_aimSpace);
        m_aimSpace = XR_NULL_HANDLE;
    }
    if (m_viewSpace != XR_NULL_HANDLE)
    {
        xrDestroySpace(m_viewSpace);
        m_viewSpace = XR_NULL_HANDLE;
    }
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
    m_inputState = InputState();
}

void OpenXRBackend::Shutdown()
{
    ShutdownSession();
    if (m_actionSet != XR_NULL_HANDLE)
    {
        xrDestroyActionSet(m_actionSet);
        m_actionSet = XR_NULL_HANDLE;
    }
    m_moveAction = XR_NULL_HANDLE;
    m_turnAction = XR_NULL_HANDLE;
    m_leftTriggerAction = XR_NULL_HANDLE;
    m_rightTriggerAction = XR_NULL_HANDLE;
    m_toggleShaderAction = XR_NULL_HANDLE;
    m_toggleHudAction = XR_NULL_HANDLE;
    m_clickAction = XR_NULL_HANDLE;
    m_resetAction = XR_NULL_HANDLE;
    m_menuAction = XR_NULL_HANDLE;
    m_holdAction = XR_NULL_HANDLE;
    m_aimPoseAction = XR_NULL_HANDLE;
    m_systemId = XR_NULL_SYSTEM_ID;
    m_environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
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
