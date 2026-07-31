// OpenXRBackend.h

#pragma once

#include <GL/glew.h>
#include <openxr/openxr.h>

#include "FBO.h"

#include <string>
#include <vector>

/// Owns RiftRay's platform-independent OpenXR state and the Windows OpenGL
/// session resources. Scene rendering continues to consume GLM matrices.
class OpenXRBackend
{
public:
    OpenXRBackend();
    ~OpenXRBackend();

    /// Instance and HMD system discovery. Safe to call before creating GL.
    bool Initialize();

    /// Creates a session for the current WGL context and stereo swapchains.
    bool InitializeSession();

    /// Polls all pending runtime events and applies session state transitions.
    bool PollEvents(bool& shouldExit);

    /// Waits for and begins one frame. Returns false when no session is running.
    bool BeginFrame();

    /// Locates stereo views at the current frame's predicted display time.
    bool LocateViews();

    /// Acquires, waits for, and binds one view's swapchain image/FBO.
    bool BeginView(uint32_t viewIndex);

    /// Unbinds and releases one view's acquired swapchain image.
    bool EndView(uint32_t viewIndex);

    /// Updates the submitted image rectangle after FBO scale/cinemascope changes.
    void SetViewRenderRect(
        uint32_t viewIndex, int32_t x, int32_t y, int32_t width, int32_t height);

    /// Submits the projection layer plus an optional already-populated quad layer.
    bool EndFrame(const XrCompositionLayerQuad* quadLayer = NULL);

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
    bool LoadOpenGLFunctions();
    bool ValidateOpenGLRequirements() const;
    bool CreateReferenceSpace();
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
    XrSessionState m_sessionState;
    bool m_sessionRunning;

    PFN_xrVoidFunction m_getOpenGLGraphicsRequirements;

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
