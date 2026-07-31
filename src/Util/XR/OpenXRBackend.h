// OpenXRBackend.h

#pragma once

#include <GL/glew.h>
#include <openxr/openxr.h>

#include "FBO.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

class OpenXRGraphicsBinding;

/// Owns RiftRay's shared OpenXR policy and current-platform GL session resources.
/// Scene rendering continues to consume GLM matrices.
class OpenXRBackend
{
public:
    struct InputState
    {
        InputState();

        XrVector2f move;
        XrVector2f turn;
        float leftTrigger;
        float rightTrigger;
        bool toggleShader;
        bool toggleHud;
        bool click;
        bool reset;
        bool menu;
        bool hold;
        bool aimPoseValid;
        XrPosef aimPose;
    };

    OpenXRBackend();
    ~OpenXRBackend();

    /// Instance and HMD system discovery. Safe to call before creating GL.
    bool Initialize();

    /// Creates a session for the current platform GL context and stereo swapchains.
    bool InitializeSession();

    /// Polls all pending runtime events and applies session state transitions.
    bool PollEvents(bool& shouldExit);

    /// Waits for and begins one frame. Returns false when no session is running.
    bool BeginFrame();

    /// Locates stereo views at the current frame's predicted display time.
    bool LocateViews();

    /// Synchronizes the OpenXR action set and locates the right-hand aim pose.
    bool SyncInput();

    /// Acquires, waits for, and binds one view's swapchain image/FBO.
    bool BeginView(uint32_t viewIndex);

    /// Unbinds and releases one view's acquired swapchain image.
    bool EndView(uint32_t viewIndex);

    /// Updates the submitted image rectangle after FBO scale/cinemascope changes.
    void SetViewRenderRect(
        uint32_t viewIndex, int32_t x, int32_t y, int32_t width, int32_t height);

    /// Submits the projection layer plus an optional already-populated quad layer.
    bool EndFrame(
        const XrCompositionLayerQuad* quadLayer = NULL,
        bool submitProjection = true);

    void ShutdownSession();
    void Shutdown();

    bool IsInstanceReady() const { return m_instance != XR_NULL_HANDLE; }
    bool HasSystem() const { return m_systemId != XR_NULL_SYSTEM_ID; }
    bool IsSessionReady() const { return m_session != XR_NULL_HANDLE; }
    bool IsSessionRunning() const { return m_sessionRunning; }
    bool ShouldRender() const { return m_frameBegun && m_frameState.shouldRender == XR_TRUE; }

    XrInstance GetInstance() const { return m_instance; }
    XrSystemId GetSystemId() const { return m_systemId; }
    XrSession GetSession() const { return m_session; }
    XrSpace GetAppSpace() const { return m_appSpace; }
    XrTime GetPredictedDisplayTime() const { return m_frameState.predictedDisplayTime; }
    XrSessionState GetSessionState() const { return m_sessionState; }

    uint32_t GetViewCount() const { return static_cast<uint32_t>(m_views.size()); }
    const XrView& GetView(uint32_t viewIndex) const { return m_views.at(viewIndex); }
    const FBO& GetViewFramebuffer(uint32_t viewIndex) const;
    uint32_t GetViewWidth(uint32_t viewIndex) const;
    uint32_t GetViewHeight(uint32_t viewIndex) const;
    int64_t GetColorFormat() const;
    const InputState& GetInputState() const { return m_inputState; }

    std::string DescribeResult(XrResult result) const;

private:
    struct ViewTarget
    {
        ViewTarget();

        XrSwapchain swapchain;
        int64_t format;
        uint32_t width;
        uint32_t height;
        std::vector<GLuint> textures;
        FBO framebuffer;
        bool imageAcquired;
        uint32_t acquiredImageIndex;
        XrRect2Di renderRect;
    };

    bool HasRequiredExtensions() const;
    bool CreateReferenceSpace();
    bool CreateActions();
    bool CreateAction(
        XrActionType type, const char* name, const char* localizedName,
        XrAction& action);
    bool SuggestBindings(
        const char* interactionProfile,
        const std::vector<std::pair<XrAction, const char*> >& bindings);
    bool ReadBooleanAction(XrAction action, bool& value) const;
    bool ReadFloatAction(XrAction action, float& value) const;
    bool ReadVector2Action(XrAction action, XrVector2f& value) const;
    bool CreateViewSwapchains();
    bool CreateViewTarget(
        ViewTarget& target, const XrViewConfigurationView& configuration, int64_t format);
    int64_t ChooseSwapchainFormat() const;
    void ReleaseOutstandingImages();
    bool HandleSessionStateChanged(const XrEventDataSessionStateChanged& event, bool& shouldExit);

    XrInstance m_instance;
    XrSystemId m_systemId;
    XrSession m_session;
    XrSpace m_appSpace;
    XrSpace m_viewSpace;
    XrEnvironmentBlendMode m_environmentBlendMode;
    XrSessionState m_sessionState;
    bool m_sessionRunning;

    std::unique_ptr<OpenXRGraphicsBinding> m_graphicsBinding;

    XrActionSet m_actionSet;
    XrAction m_moveAction;
    XrAction m_turnAction;
    XrAction m_leftTriggerAction;
    XrAction m_rightTriggerAction;
    XrAction m_toggleShaderAction;
    XrAction m_toggleHudAction;
    XrAction m_clickAction;
    XrAction m_resetAction;
    XrAction m_menuAction;
    XrAction m_holdAction;
    XrAction m_aimPoseAction;
    XrSpace m_aimSpace;
    InputState m_inputState;

    std::vector<XrViewConfigurationView> m_viewConfigurations;
    std::vector<XrView> m_views;
    std::vector<ViewTarget> m_viewTargets;
    XrViewState m_viewState;
    bool m_viewsLocated;

    XrFrameState m_frameState;
    bool m_frameBegun;

    OpenXRBackend(const OpenXRBackend&);
    OpenXRBackend& operator=(const OpenXRBackend&);
};
