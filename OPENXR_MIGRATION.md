# RiftRay: OpenXR Migration Plan

## Overview

This document is a step-by-step plan for migrating RiftRay from the proprietary Oculus PC SDK (LibOVR 32.0.0) to the Khronos OpenXR standard. The Meta PC runtime already supports OpenXR natively, so no runtime changes are needed — only the application code changes.

The migration touches five areas:
1. Build system (CMakeLists.txt)
2. Session init/teardown (`initHMD`, `initVR`, `exitVR`)
3. Frame loop and swap chains (`displayHMD`, `HudQuad`)
4. Matrix utilities (`MatrixFunctions.cpp`)
5. Input handling (`HandleRemote`, `HandleXboxController`, `HandleTouchControllers`)

---

## Step 1 — Dependencies (CMakeLists.txt)

### Remove
- The entire `ovr_sdk_win_32.0.0/` directory can be deleted after migration.
- In `CMakeLists.txt`, remove the `IF(USE_OCULUSSDK)` block (lines 38–61):
  - `OCULUSSDK_ROOT`, all `INCLUDE_DIRECTORIES` pointing into it
  - `LINK_DIRECTORIES` for LibOVR
  - `SET(OVR_LIBS LibOVR.lib)`
  - `ADD_DEFINITIONS(-DUSE_OCULUSSDK)`
- Remove `"OCULUSSDK_ROOT=${OCULUSSDK_ROOT}"` from the `INVOKEPYTHON(copyDLLs.py ...)` call.
- Remove the `SET(USE_OCULUSSDK TRUE ...)` cache variable.

### Add
Add OpenXR via vcpkg (already set up in this project):

**vcpkg.json** — add to `"dependencies"`:
```json
"openxr"
```

**CMakeLists.txt** — replace the removed OVR block with:
```cmake
find_package(OpenXR CONFIG REQUIRED)
SET(PLATFORM_LIBS
    opengl32.lib glu32.lib
    OpenXR::openxr_loader
    ${ANT_LIBS}
    Winmm.lib
)
ADD_DEFINITIONS(-DUSE_OPENXR)
```

No DLL copy step is needed — `openxr_loader.dll` ships with the Meta/Oculus runtime, not with the app.

---

## Step 2 — Global Variables

In `main_glfw_ovrsdk13.cpp`, replace the OVR globals (lines 43–59) with OpenXR equivalents:

```cpp
// Before (OVR)
ovrSession g_session;
ovrHmdDesc m_Hmd;
ovrTextureSwapChain g_textureSwapChain[ovrEye_Count];
ovrPosef m_eyePoses[ovrEye_Count];
ovrMirrorTexture g_mirrorTexture = nullptr;
ovrPerfHudMode m_perfHudMode = ovrPerfHud_Off;
ovrInputState lastRemoteInputState = { 0 };
ovrInputState lastXboxControllerInputState = { 0 };
ovrInputState lastTouchInputState = { 0 };
```

```cpp
// After (OpenXR)
XrInstance       g_xrInstance   = XR_NULL_HANDLE;
XrSystemId       g_xrSystemId   = XR_NULL_SYSTEM_ID;
XrSession        g_xrSession    = XR_NULL_HANDLE;
XrSpace          g_xrAppSpace   = XR_NULL_HANDLE;  // LOCAL or STAGE reference space
XrSwapchain      g_xrSwapchain[2] = {};            // one per eye
uint32_t         g_swapchainWidth[2] = {};
uint32_t         g_swapchainHeight[2] = {};
XrView           g_views[2]     = {};              // per-frame view poses + FOV
bool             g_sessionRunning = false;

// Input actions (see Step 6)
XrActionSet      g_actionSet    = XR_NULL_HANDLE;
XrAction         g_thumbstickLeftAction  = XR_NULL_HANDLE;
XrAction         g_thumbstickRightAction = XR_NULL_HANDLE;
XrAction         g_triggerLeftAction     = XR_NULL_HANDLE;
XrAction         g_triggerRightAction    = XR_NULL_HANDLE;
XrAction         g_buttonAAction         = XR_NULL_HANDLE;
XrAction         g_buttonBAction         = XR_NULL_HANDLE;
XrAction         g_buttonXAction         = XR_NULL_HANDLE;
XrAction         g_buttonYAction         = XR_NULL_HANDLE;
XrAction         g_menuAction            = XR_NULL_HANDLE;
XrAction         g_rThumbClickAction     = XR_NULL_HANDLE;
```

The `m_eyePoses` array is replaced by `g_views[2]`, which is populated each frame by `xrLocateViews()`.

---

## Step 3 — Session Init/Teardown

### `initHMD()` — before GL context

Replace `ovr_Initialize` / `ovr_Create` with:

```cpp
void initHMD()
{
    // 1. Create XrInstance
    const char* extensions[] = {
        "XR_KHR_opengl_enable"
    };
    XrInstanceCreateInfo ici = { XR_TYPE_INSTANCE_CREATE_INFO };
    strcpy(ici.applicationInfo.applicationName, "RiftRay");
    ici.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = extensions;
    if (XR_FAILED(xrCreateInstance(&ici, &g_xrInstance))) {
        LOG_ERROR("xrCreateInstance failed - running in non-VR mode");
        g_hasHMD = false;
        return;
    }

    // 2. Get system (HMD)
    XrSystemGetInfo sgi = { XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (XR_FAILED(xrGetSystem(g_xrInstance, &sgi, &g_xrSystemId))) {
        LOG_ERROR("xrGetSystem failed - no HMD detected");
        g_hasHMD = false;
        return;
    }

    g_hasHMD = true;

    // Log HMD name
    XrSystemProperties props = { XR_TYPE_SYSTEM_PROPERTIES };
    xrGetSystemProperties(g_xrInstance, g_xrSystemId, &props);
    LOG_INFO("HMD detected: %s", props.systemName);
}
```

Note: `xrCreateSession` requires an active GL context, so it moves into `initVR()`.

### `initVR()` — after GL context

Replace swap chain creation and `ovr_CreateMirrorTextureGL` with:

```cpp
void initVR()
{
    if (!g_hasHMD) return;

    // 1. Create session with OpenGL binding
    // The exact struct depends on platform (Win32 + WGL):
    XrGraphicsBindingOpenGLWin32KHR glBinding = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    glBinding.hDC   = wglGetCurrentDC();
    glBinding.hGLRC = wglGetCurrentContext();

    XrSessionCreateInfo sci = { XR_TYPE_SESSION_CREATE_INFO };
    sci.next     = &glBinding;
    sci.systemId = g_xrSystemId;
    if (XR_FAILED(xrCreateSession(g_xrInstance, &sci, &g_xrSession))) {
        LOG_ERROR("xrCreateSession failed");
        return;
    }

    // 2. Create a reference space (equivalent to tracking origin)
    XrReferenceSpaceCreateInfo rsci = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL; // or STAGE
    rsci.poseInReferenceSpace = { {0,0,0,1}, {0,0,0} };     // identity
    xrCreateReferenceSpace(g_xrSession, &rsci, &g_xrAppSpace);

    // 3. Query recommended eye texture sizes
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(g_xrInstance, g_xrSystemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr);
    std::vector<XrViewConfigurationView> vcViews(viewCount,
        { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrEnumerateViewConfigurationViews(g_xrInstance, g_xrSystemId,
        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, vcViews.data());

    // 4. Create one swapchain per eye
    for (uint32_t eye = 0; eye < 2; ++eye)
    {
        const auto& vcv = vcViews[eye];
        g_swapchainWidth[eye]  = vcv.recommendedImageRectWidth;
        g_swapchainHeight[eye] = vcv.recommendedImageRectHeight;

        XrSwapchainCreateInfo swci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
        swci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        swci.format      = GL_SRGB8_ALPHA8;  // equivalent to OVR_FORMAT_R8G8B8A8_UNORM_SRGB
        swci.sampleCount = 1;
        swci.width       = g_swapchainWidth[eye];
        swci.height      = g_swapchainHeight[eye];
        swci.faceCount   = 1;
        swci.arraySize   = 1;
        swci.mipCount    = 1;
        xrCreateSwapchain(g_xrSession, &swci, &g_xrSwapchain[eye]);

        // Enumerate images and set up FBOs (same GL setup as before)
        uint32_t imgCount = 0;
        xrEnumerateSwapchainImages(g_xrSwapchain[eye], 0, &imgCount, nullptr);
        std::vector<XrSwapchainImageOpenGLKHR> images(imgCount,
            { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
        xrEnumerateSwapchainImages(g_xrSwapchain[eye], imgCount, &imgCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));

        // Store texture IDs; FBO assembly is identical to existing code
        // (glGenFramebuffers, glFramebufferTexture2D, depth renderbuffer, etc.)
        // ... [existing FBO setup code, just feeding images[i].image as the GL tex ID]
    }

    // 5. Mirror texture: OpenXR has no equivalent.
    // Instead, after rendering, blit m_swapFBO[0] (left eye) directly to the
    // default framebuffer in the mirror blit step at the end of displayHMD().
    // The existing BlitLeftEyeRenderToUndistortedMirrorTexture() can be adapted.

    // 6. HudQuad swap chain — same process, just call xrCreateSwapchain separately
    const XrExtent2Di hudSz = { 600, 600 };
    g_tweakbarQuad.initGL(g_xrSession, hudSz);

    // 7. Begin session
    XrSessionBeginInfo sbi = { XR_TYPE_SESSION_BEGIN_INFO };
    sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    xrBeginSession(g_xrSession, &sbi);

    // 8. Set up input actions (see Step 6)
    initXRInput();

    g_hmdVisible = true;
}
```

### `exitVR()`

```cpp
void exitVR()
{
    if (!g_hasHMD) return;

    for (int eye = 0; eye < 2; ++eye)
        xrDestroySwapchain(g_xrSwapchain[eye]);

    xrDestroySpace(g_xrAppSpace);
    xrEndSession(g_xrSession);
    xrDestroySession(g_xrSession);
    xrDestroyInstance(g_xrInstance);
}
```

---

## Step 4 — Frame Loop (`displayHMD`)

The frame loop structure changes significantly. OpenXR requires an explicit `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame` sequence, and session state must be polled via an event queue.

### Event polling (replaces `ovr_GetSessionStatus`)

Add a call to `pollXREvents()` at the top of `displayHMD` (or in the main loop):

```cpp
void pollXREvents()
{
    XrEventDataBuffer event = { XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(g_xrInstance, &event) == XR_SUCCESS)
    {
        switch (event.type)
        {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto& e = reinterpret_cast<XrEventDataSessionStateChanged&>(event);
            if (e.state == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo sbi = { XR_TYPE_SESSION_BEGIN_INFO };
                sbi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                xrBeginSession(g_xrSession, &sbi);
                g_sessionRunning = true;
            }
            else if (e.state == XR_SESSION_STATE_STOPPING) {
                xrEndSession(g_xrSession);
                g_sessionRunning = false;
            }
            else if (e.state == XR_SESSION_STATE_EXITING ||
                     e.state == XR_SESSION_STATE_LOSS_PENDING) {
                glfwSetWindowShouldClose(g_pMirrorWindow, 1);
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
            // Equivalent to sessionStatus.ShouldRecenter — just let the space update naturally
            break;
        default:
            break;
        }
        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
}
```

### Render loop

```cpp
void displayHMD()
{
    if (!g_hasHMD || !g_sessionRunning) {
        displayMonitor();
        return;
    }

    pollXREvents();

    // 1. Wait for the optimal render time
    XrFrameWaitInfo fwi = { XR_TYPE_FRAME_WAIT_INFO };
    XrFrameState frameState = { XR_TYPE_FRAME_STATE };
    xrWaitFrame(g_xrSession, &fwi, &frameState);

    // 2. Begin the frame
    XrFrameBeginInfo fbi = { XR_TYPE_FRAME_BEGIN_INFO };
    xrBeginFrame(g_xrSession, &fbi);

    std::vector<XrCompositionLayerBaseHeader*> layers;
    XrCompositionLayerProjection projLayer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
    XrCompositionLayerProjectionView projViews[2] = {};

    if (frameState.shouldRender)
    {
        // 3. Locate views (replaces ovr_GetEyePoses)
        XrViewLocateInfo vli = { XR_TYPE_VIEW_LOCATE_INFO };
        vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        vli.displayTime = frameState.predictedDisplayTime;
        vli.space = g_xrAppSpace;

        XrViewState viewState = { XR_TYPE_VIEW_STATE };
        uint32_t viewCount = 2;
        XrView views[2] = { {XR_TYPE_VIEW}, {XR_TYPE_VIEW} };
        xrLocateViews(g_xrSession, &vli, &viewState, 2, &viewCount, views);
        memcpy(g_views, views, sizeof(views));

        storeHmdPose(views[0].pose);  // adapted from storeHmdPose(ovrPosef)

        // 4. Render each eye
        for (int eye = 0; eye < 2; ++eye)
        {
            // Acquire swapchain image
            XrSwapchainImageAcquireInfo aqInfo = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
            uint32_t imgIndex = 0;
            xrAcquireSwapchainImage(g_xrSwapchain[eye], &aqInfo, &imgIndex);

            XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
            waitInfo.timeout = XR_INFINITE_DURATION;
            xrWaitSwapchainImage(g_xrSwapchain[eye], &waitInfo);

            // Bind FBO (same as existing code, using imgIndex to pick texture)
            // ... existing glBindFramebuffer, glViewport, glClear, render, etc.

            // Build projection matrix from OpenXR FOV angles
            const XrFovf& fov = views[eye].fov;
            const glm::mat4 proj = makeProjectionFromFov(fov, 0.2f, 1000.0f);
            const glm::mat4 mview =
                makeWorldToChassisMatrix() *
                makeMatrixFromXrPose(views[eye].pose, m_headSize);
            g_pScene->RenderForOneEye(
                glm::value_ptr(glm::inverse(mview)),
                glm::value_ptr(proj));

            // Release swapchain image
            XrSwapchainImageReleaseInfo relInfo = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
            xrReleaseSwapchainImage(g_xrSwapchain[eye], &relInfo);

            // Fill projection view for layer submission
            projViews[eye] = { XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW };
            projViews[eye].pose    = views[eye].pose;
            projViews[eye].fov     = views[eye].fov;
            projViews[eye].subImage.swapchain        = g_xrSwapchain[eye];
            projViews[eye].subImage.imageRect.offset = { 0, 0 };
            projViews[eye].subImage.imageRect.extent = {
                (int32_t)g_swapchainWidth[eye],
                (int32_t)g_swapchainHeight[eye]
            };
        }

        // FBO scale / cinemascope: keep using viewport/scissor exactly as now,
        // just pass a smaller imageRect.extent in projViews[eye].subImage.imageRect.

        // 5. HudQuad layer (direct equivalent to ovrLayerQuad)
        XrCompositionLayerQuad quadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
        if (g_tweakbarQuad.m_showQuadInWorld)
        {
            g_tweakbarQuad.SetHmdEyeRay(views[0].pose);
            g_tweakbarQuad.DrawToQuad();

            quadLayer.space         = g_xrAppSpace;
            quadLayer.pose          = g_tweakbarQuad.GetXrPose();  // XrPosef equivalent
            quadLayer.size          = { 1.f, 1.f };
            quadLayer.subImage.swapchain        = g_tweakbarQuad.m_xrSwapchain;
            quadLayer.subImage.imageRect        = { {0,0}, {600,600} };
            layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quadLayer));
        }

        projLayer.space     = g_xrAppSpace;
        projLayer.viewCount = 2;
        projLayer.views     = projViews;
        layers.insert(layers.begin(),
            reinterpret_cast<XrCompositionLayerBaseHeader*>(&projLayer));

        // 6. Mirror to desktop window: blit left eye swapchain FBO to default framebuffer
        glBindFramebuffer(GL_READ_FRAMEBUFFER, m_swapFBO[0].id);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(
            0, 0, g_swapchainWidth[0], g_swapchainHeight[0],
            0, 0, g_mirrorWindowSz.x, g_mirrorWindowSz.y,
            GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // 7. End frame — submit all layers
    XrFrameEndInfo fei = { XR_TYPE_FRAME_END_INFO };
    fei.displayTime          = frameState.predictedDisplayTime;
    fei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    fei.layerCount           = static_cast<uint32_t>(layers.size());
    fei.layers               = layers.empty() ? nullptr : layers.data();
    xrEndFrame(g_xrSession, &fei);

    ++g_frameIndex;
}
```

---

## Step 5 — Matrix Utilities (`MatrixFunctions.cpp`)

`XrPosef` is structurally identical to `ovrPosef` — same layout, same coordinate system (right-handed, Y-up). Only the type names change.

```cpp
// Before
glm::mat4 makeMatrixFromPose(const ovrPosef& eyePose, float headSize)
{
    const ovrVector3f& p = eyePose.Position;
    const ovrQuatf&    q = eyePose.Orientation;
    ...
}
```

```cpp
// After
glm::mat4 makeMatrixFromXrPose(const XrPosef& eyePose, float headSize)
{
    const XrVector3f&    p = eyePose.position;     // lowercase field names in OpenXR
    const XrQuaternionf& q = eyePose.orientation;
    return glm::translate(glm::mat4(1.f), headSize * glm::vec3(p.x, p.y, p.z))
         * glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z));
}

void GetHMDEyeRayPosAndDir(const XrPosef& pose, glm::vec3& ro, glm::vec3& rd)
{
    // Body identical, just use makeMatrixFromXrPose
}
```

Replace `ovrMatrix4f_Projection` with a local helper (no longer provided by the SDK):

```cpp
// OpenXR gives raw FOV angles; build the projection matrix manually
glm::mat4 makeProjectionFromFov(const XrFovf& fov, float nearZ, float farZ)
{
    const float l = tanf(fov.angleLeft);
    const float r = tanf(fov.angleRight);
    const float d = tanf(fov.angleDown);
    const float u = tanf(fov.angleUp);
    const float w = r - l;
    const float h = u - d;

    glm::mat4 m(0.f);
    m[0][0] =  2.f / w;
    m[2][0] = (r + l) / w;
    m[1][1] =  2.f / h;
    m[2][1] = (u + d) / h;
    m[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    m[3][2] = -(2.f * farZ * nearZ) / (farZ - nearZ);
    m[2][3] = -1.f;
    return m;
}
```

`makeGlmMatrixFromOvrMatrix` and `makeOVRMatrixFromGlmMatrix` can be deleted — they only existed to convert between LibOVR's row-major matrix and glm's column-major matrix. OpenXR has no matrix type; just use glm directly.

---

## Step 6 — Input Handling

This is the largest rewrite. OpenXR uses a declarative **action system** rather than polling raw button bitmasks. All input logic in `HandleRemote`, `HandleXboxController`, and `HandleTouchControllers` moves into this system.

### Setup (`initXRInput`) — called once from `initVR`

```cpp
void initXRInput()
{
    // 1. Create action set
    XrActionSetCreateInfo asci = { XR_TYPE_ACTION_SET_CREATE_INFO };
    strcpy(asci.actionSetName, "gameplay");
    strcpy(asci.localizedActionSetName, "Gameplay");
    xrCreateActionSet(g_xrInstance, &asci, &g_actionSet);

    // 2. Define subaction paths for left/right hand
    XrPath handPaths[2];
    xrStringToPath(g_xrInstance, "/user/hand/left",  &handPaths[0]);
    xrStringToPath(g_xrInstance, "/user/hand/right", &handPaths[1]);

    // Helper lambda to create an action
    auto makeAction = [&](XrAction& action, const char* name,
                          XrActionType type, uint32_t subCount = 0,
                          const XrPath* subPaths = nullptr)
    {
        XrActionCreateInfo aci = { XR_TYPE_ACTION_CREATE_INFO };
        aci.actionType = type;
        strcpy(aci.actionName, name);
        strcpy(aci.localizedActionName, name);
        aci.countSubactionPaths = subCount;
        aci.subactionPaths = subPaths;
        xrCreateAction(g_actionSet, &aci, &action);
    };

    makeAction(g_thumbstickLeftAction,  "thumbstick_left",  XR_ACTION_TYPE_VECTOR2F_INPUT, 2, handPaths);
    makeAction(g_thumbstickRightAction, "thumbstick_right", XR_ACTION_TYPE_VECTOR2F_INPUT, 2, handPaths);
    makeAction(g_triggerLeftAction,     "trigger_left",     XR_ACTION_TYPE_FLOAT_INPUT,    2, handPaths);
    makeAction(g_triggerRightAction,    "trigger_right",    XR_ACTION_TYPE_FLOAT_INPUT,    2, handPaths);
    makeAction(g_buttonAAction,         "button_a",         XR_ACTION_TYPE_BOOLEAN_INPUT);
    makeAction(g_buttonBAction,         "button_b",         XR_ACTION_TYPE_BOOLEAN_INPUT);
    makeAction(g_buttonXAction,         "button_x",         XR_ACTION_TYPE_BOOLEAN_INPUT);
    makeAction(g_buttonYAction,         "button_y",         XR_ACTION_TYPE_BOOLEAN_INPUT);
    makeAction(g_menuAction,            "menu",             XR_ACTION_TYPE_BOOLEAN_INPUT);
    makeAction(g_rThumbClickAction,     "rthumb_click",     XR_ACTION_TYPE_BOOLEAN_INPUT);

    // 3. Suggest bindings for Meta Touch controllers
    XrPath touchProfile;
    xrStringToPath(g_xrInstance,
        "/interaction_profiles/oculus/touch_controller", &touchProfile);

    auto path = [&](const char* s) {
        XrPath p; xrStringToPath(g_xrInstance, s, &p); return p;
    };

    XrActionSuggestedBinding bindings[] = {
        { g_thumbstickLeftAction,  path("/user/hand/left/input/thumbstick")  },
        { g_thumbstickRightAction, path("/user/hand/right/input/thumbstick") },
        { g_triggerLeftAction,     path("/user/hand/left/input/trigger/value")  },
        { g_triggerRightAction,    path("/user/hand/right/input/trigger/value") },
        { g_buttonAAction,         path("/user/hand/right/input/a/click") },
        { g_buttonBAction,         path("/user/hand/right/input/b/click") },
        { g_buttonXAction,         path("/user/hand/left/input/x/click")  },
        { g_buttonYAction,         path("/user/hand/left/input/y/click")  },
        { g_menuAction,            path("/user/hand/left/input/menu/click") },
        { g_rThumbClickAction,     path("/user/hand/right/input/thumbstick/click") },
    };

    XrInteractionProfileSuggestedBinding suggested = {
        XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
    suggested.interactionProfile     = touchProfile;
    suggested.countSuggestedBindings = static_cast<uint32_t>(std::size(bindings));
    suggested.suggestedBindings      = bindings;
    xrSuggestInteractionProfileBindings(g_xrInstance, &suggested);

    // 4. Attach action set to session
    XrSessionActionSetsAttachInfo attachInfo = {
        XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &g_actionSet;
    xrAttachSessionActionSets(g_xrSession, &attachInfo);
}
```

### Per-frame input (`HandleTouchControllers` replacement)

Call `xrSyncActions` once per frame, then read state:

```cpp
void HandleTouchControllers()
{
    if (!g_hasHMD || !g_sessionRunning) return;

    // Sync
    XrActiveActionSet activeSet = { g_actionSet, XR_NULL_PATH };
    XrActionsSyncInfo syncInfo = { XR_TYPE_ACTIONS_SYNC_INFO };
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    xrSyncActions(g_xrSession, &syncInfo);

    // Helper to read a bool action
    auto getBool = [&](XrAction action) -> bool {
        XrActionStateBoolean s = { XR_TYPE_ACTION_STATE_BOOLEAN };
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = action;
        xrGetActionStateBoolean(g_xrSession, &gi, &s);
        return s.isActive && s.currentState;
    };
    auto getBoolChanged = [&](XrAction action, bool& last) -> bool {
        bool cur = getBool(action);
        bool changed = cur && !last;
        last = cur;
        return changed;
    };
    auto getFloat = [&](XrAction action) -> float {
        XrActionStateFloat s = { XR_TYPE_ACTION_STATE_FLOAT };
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = action;
        xrGetActionStateFloat(g_xrSession, &gi, &s);
        return s.isActive ? s.currentState : 0.f;
    };
    auto getVec2 = [&](XrAction action) -> XrVector2f {
        XrActionStateVector2f s = { XR_TYPE_ACTION_STATE_VECTOR2F };
        XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
        gi.action = action;
        xrGetActionStateVector2f(g_xrSession, &gi, &s);
        return s.isActive ? s.currentState : XrVector2f{0,0};
    };

    // Button A — Enter/Exit shader
    static bool lastA = false;
    if (getBoolChanged(g_buttonAAction, lastA))
        g_gallery.ToggleShaderWorld();

    // Button B — Toggle tweakbar
    static bool lastB = false;
    if (getBoolChanged(g_buttonBAction, lastB))
        g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;

    // Button X — Click in tweakbar (press/release)
    static bool lastX = false;
    bool curX = getBool(g_buttonXAction);
    if (curX && !lastX)  g_tweakbarQuad.MouseClick(1);
    if (!curX && lastX)  g_tweakbarQuad.MouseClick(0);
    lastX = curX;

    // Button Y — Reset position
    static bool lastY = false;
    if (getBoolChanged(g_buttonYAction, lastY))
        m_chassisPos = glm::vec3(0.f, 1.f, 0.f);

    // Menu button — Recenter (OpenXR handles this via reference space; simplest is to
    // reset chassis position/yaw)
    static bool lastMenu = false;
    if (getBoolChanged(g_menuAction, lastMenu))
        m_chassisPos = glm::vec3(0.f, 1.f, 0.f);

    // Right thumbstick click — hold/move tweakbar
    static bool lastRThumb = false;
    bool curRThumb = getBool(g_rThumbClickAction);
    if (curRThumb && !lastRThumb)
        g_tweakbarQuad.SetHoldingFlag(g_views[0].pose, true);
    if (!curRThumb && lastRThumb)
        g_tweakbarQuad.SetHoldingFlag(g_views[0].pose, false);
    lastRThumb = curRThumb;

    // Left thumbstick — movement
    XrVector2f ls = getVec2(g_thumbstickLeftAction);
    const float deadzone = 0.2f;
    glm::vec3 touchMove(0.f);
    if (fabsf(ls.x) > deadzone || fabsf(ls.y) > deadzone) {
        touchMove += ls.x * glm::vec3(1.f, 0.f, 0.f);
        touchMove -= ls.y * glm::vec3(0.f, 0.f, 1.f);
    }

    // Triggers — vertical movement
    float lt = getFloat(g_triggerLeftAction);
    float rt = getFloat(g_triggerRightAction);
    const float trigThreshold = 0.3f;
    if (lt > trigThreshold) touchMove += glm::vec3(0.f, -lt, 0.f);
    if (rt > trigThreshold) touchMove += glm::vec3(0.f,  rt, 0.f);
    m_touchMove = touchMove;

    // Right thumbstick — smooth/snap turn
    XrVector2f rs = getVec2(g_thumbstickRightAction);
    if (!m_snapTurn && fabsf(rs.x) > deadzone) {
        m_joystickYaw = rs.x * 0.75f;
    }
    else if (m_snapTurn) {
        static bool snapReady = true;
        const float snapThreshold = 0.7f;
        if (fabsf(rs.x) > snapThreshold && snapReady) {
            m_chassisYaw += (rs.x > 0) ? 0.3f : -0.3f;
            snapReady = false;
        }
        else if (fabsf(rs.x) < deadzone) {
            snapReady = true;
        }
    }
}
```

`HandleRemote` and `HandleXboxController` can be removed — Oculus Remote is discontinued and Xbox controller input can be handled via GLFW joystick API (unchanged from current code) or by adding an Xbox interaction profile to the action set. The functionality is already covered above.

---

## Step 7 — HudQuad Changes

`HudQuad` stores `ovrSession` and `ovrTextureSwapChain`. These need updating:

```cpp
// HudQuad.h — change member types
XrSession   m_xrSession  = XR_NULL_HANDLE;
XrSwapchain m_xrSwapchain = XR_NULL_HANDLE;
XrPosef     m_QuadPoseCenter = {};            // replaces ovrPosef
```

`initGL` / `exitGL` use the same `xrCreateSwapchain` / `xrDestroySwapchain` pattern shown in Step 3. The GL FBO assembly code is identical.

`_PrepareToDrawToQuad` / `_FinalizeDrawToQuad` swap:
- `ovr_GetTextureSwapChainCurrentIndex` → `xrAcquireSwapchainImage` + `xrWaitSwapchainImage`
- `ovr_CommitTextureSwapChain` → `xrReleaseSwapchainImage`

`SetHoldingFlag(ovrPosef, bool)` and `SetHmdEyeRay(ovrPosef)` change signature to `XrPosef`. Field names change from `Position`/`Orientation` to `position`/`orientation` (lowercase). The math is unchanged.

`GetPose()` returns `XrPosef` instead of `ovrPosef`.

---

## Step 8 — `TogglePerfHud` and `RecenterPoseCB`

- `TogglePerfHud` uses `ovr_SetInt(g_session, OVR_PERF_HUD_MODE, ...)` — there is no OpenXR equivalent. Remove this feature or leave the button as a no-op. The Meta debug layer provides its own perf overlay.
- `RecenterPoseCB` calls `ovr_RecenterTrackingOrigin` — replace with a chassis position/yaw reset (`m_chassisPos = glm::vec3(0,1,0); m_chassisYaw = 0;`) since OpenXR manages the tracking origin automatically.

---

## Step 9 — Headers

Replace in every file that includes OVR headers:

```cpp
// Remove:
#include <OVR_CAPI.h>
#include <OVR_CAPI_GL.h>

// Add:
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>   // for XrGraphicsBindingOpenGLWin32KHR etc.
```

Replace the `#ifdef USE_OCULUSSDK` guards in `MatrixFunctions.cpp` with `#ifdef USE_OPENXR`.

---

## Summary of Files to Change

| File | Change |
|------|--------|
| `CMakeLists.txt` | Remove LibOVR, add `OpenXR::openxr_loader` via vcpkg |
| `vcpkg.json` | Add `"openxr"` dependency |
| `src/main_glfw_ovrsdk13.cpp` | All OVR globals, `initHMD`, `initVR`, `exitVR`, `displayHMD`, `HandleRemote` (remove), `HandleXboxController` (remove), `HandleTouchControllers`, `timestep`, `TogglePerfHud`, `RecenterPoseCB` |
| `src/Util/GL/MatrixFunctions.h/.cpp` | Replace `ovrPosef` → `XrPosef`, remove OVR matrix converters, add `makeProjectionFromFov` |
| `src/Scene/HudQuad.h/.cpp` | Replace `ovrSession`/`ovrTextureSwapChain`/`ovrPosef` throughout |

## Files That Do Not Need to Change

- All shader files
- `ShaderGalleryScene`, `Scene`, and `IScene` — no VR SDK awareness
- `FBO`, `Timer`, `Logger`, `AppDirectories`
- AntTweakBar integration (`AntQuad`, `MousingQuad`) — only touches HudQuad's quad geometry, not VR types

---

## Notes

- **`ovrProjection_None` flag** in `ovrMatrix4f_Projection`: the replacement `makeProjectionFromFov` produces an equivalent standard perspective matrix with no special flags.
- **FBO scale / cinemascope**: the existing viewport/scissor approach works unchanged. When building `projViews[eye].subImage.imageRect`, pass the scaled extent rather than the full texture size to inform the compositor of the rendered region.
- **`XR_KHR_opengl_enable`** extension must be available on the runtime. The Meta runtime supports it. Check the result of `xrEnumerateInstanceExtensionProperties` if adding a graceful fallback is desired.
- **Coordinate system**: OpenXR and OVR both use right-handed, Y-up coordinates. No coordinate system transforms are needed.
- **`ovrEye_Count`** (= 2) → just use the literal `2` or `XR_EYE_COUNT` (not a standard constant; define your own).
