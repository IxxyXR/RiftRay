// main_glfw_openxr.cpp
// With humongous thanks to cThrough 2014 (Daniel Dekkers)
// Get a window created with GL context and OpenXR backend initialized,
// then hand off display to the Scene class.

#include <GL/glew.h>
#if defined(_WIN32)
#  define NOMINMAX
#  include <Windows.h>
#endif
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stdio.h>
#include <string.h>
#include <vector>
#include <algorithm>
#include "FBO.h"
#include "Timer.h"
#include "Logger.h"
#include "version.h"

#include "Scene.h"
#include "ShaderGalleryScene.h"
#include "StringFunctions.h"

#ifdef USE_ANTTWEAKBAR
#  include <AntTweakBar.h>
#endif

#include "HudQuad.h"
#include "AntQuad.h"
#include "MatrixFunctions.h"
#include "AppDirectories.h"
#include "XR/OpenXRBackend.h"

Timer g_timer;
double g_lastFrameTime = 0.0;
GLFWwindow* g_pMirrorWindow = NULL;
glm::ivec2 g_mirrorWindowSz(1200, 900);

OpenXRBackend g_openXR;
bool g_hasHMD = false;

XrPosef m_eyePoses[2] = {};
glm::vec3 m_touchMove = glm::vec3(0.f);

IScene* g_pScene = NULL;
ShaderGalleryScene g_gallery;

AntQuad g_tweakbarQuad;
#ifdef USE_ANTTWEAKBAR
TwBar* g_pMainTweakbar = NULL;
TwBar* g_pShaderTweakbar = NULL;
#endif

float m_fboScale = 1.f;
float m_cinemaScope = 0.f;

int which_mouse_button = -1;
int m_keyStates[GLFW_KEY_LAST];
glm::vec3 m_keyboardMove(0.f);
float m_keyboardYaw = 0.f;
glm::vec3 m_chassisPos(0.f, 1.f, 0.f);
float m_chassisYaw = 0.f;
float m_headSize = 1.f;
glm::vec3 m_hmdRo;
glm::vec3 m_hmdRd;
bool m_snapTurn = true;
bool g_loadShadertoysRecursive = false;

int g_joystickIdx = -1;
glm::vec3 m_joystickMove = glm::vec3(0.f);
glm::vec3 m_remoteMove = glm::vec3(0.f);
float m_joystickYaw = 0.f;

void TogglePerfHud()
{
    LOG_INFO("[RiftRay OpenXR] Runtime performance HUDs are controlled externally");
}

static void TW_CALL RecenterPoseCB(void*) { m_chassisPos = glm::vec3(0.f, 1.f, 0.f); m_chassisYaw = 0.f; }
static void TW_CALL ResetPositionCB(void*) { m_chassisPos = glm::vec3(0.f, 1.f, 0.f); g_gallery.ResetPositionAndYaw(); }
static void TW_CALL TogglePerfHUDCB(void*) { TogglePerfHud(); }
static void TW_CALL ToggleShaderWorldCB(void*) { g_gallery.ToggleShaderWorld(); }
static void TW_CALL HideTweakbarCB(void*) { g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld; }
static void TW_CALL ResetTimerCB(void *clientData) { static_cast<ShaderGalleryScene *>(clientData)->ResetTimer(); }

void initAnt()
{
    ///@note Bad size errors will be thrown if this is not called before bar creation.
    TwWindowSize(g_mirrorWindowSz.x, g_mirrorWindowSz.y);

    TwDefine(" GLOBAL fontsize=3 ");

    // Create a tweak bar
    g_pMainTweakbar = TwNewBar("TweakBar");
    g_pShaderTweakbar = TwNewBar("ShaderTweakBar");
    g_gallery.m_pMainTweakbar = g_pMainTweakbar;
    g_gallery.m_pShaderTweakbar = g_pShaderTweakbar;

    TwDefine(" GLOBAL fontsize=3 ");
    TwDefine(" TweakBar size='300 580' ");
    TwDefine(" TweakBar position='10 10' ");
    TwDefine(" ShaderTweakBar size='300 420' ");
    TwDefine(" ShaderTweakBar position='290 170' ");

    TwAddVarRW(g_pMainTweakbar, "FBO Scale", TW_TYPE_FLOAT, &m_fboScale,
        " min=0.05 max=1.0 step=0.005 group='Performance' ");
    TwAddVarRW(g_pMainTweakbar, "Cinemascope", TW_TYPE_FLOAT, &m_cinemaScope,
        " min=0.05 max=1.0 step=0.005 group='Performance' ");
    TwAddButton(g_pMainTweakbar, "Toggle Perf HUD", TogglePerfHUDCB, NULL, " group='Performance' ");

    TwAddVarRW(g_pMainTweakbar, "Snap Turn", TW_TYPE_BOOLCPP, &m_snapTurn, "  group='Controls' ");
    TwAddButton(g_pMainTweakbar, "Reset Position", ResetPositionCB, NULL, " group='Controls' ");
    TwAddButton(g_pMainTweakbar, "Recenter Pose", RecenterPoseCB, NULL, " group='Controls' ");
    TwAddButton(g_pMainTweakbar, "Hide Tweakbar", HideTweakbarCB, NULL, " group='Controls' ");

    TwAddVarRW(g_pMainTweakbar, "Chassis Pos X", TW_TYPE_FLOAT, &m_chassisPos.x,
        " min=-10 max=10 step=0.05 group='Controls' ");
    TwAddVarRW(g_pMainTweakbar, "Chassis Pos Y", TW_TYPE_FLOAT, &m_chassisPos.y,
        " min=-10 max=10 step=0.05 group='Controls' ");
    TwAddVarRW(g_pMainTweakbar, "Chassis Pos Z", TW_TYPE_FLOAT, &m_chassisPos.z,
        " min=-10 max=10 step=0.05 group='Controls' ");
    TwAddVarRW(g_pMainTweakbar, "Chassis Yaw", TW_TYPE_FLOAT, &m_chassisYaw,
        " min=-10 max=10 step=0.05 group='Controls' ");

    TwAddVarRW(g_pMainTweakbar, "Animated Thumbnails", TW_TYPE_BOOLCPP, &g_gallery.m_globalShadertoyState.animatedThumbnails, "  group='Gallery' ");
    TwAddVarRW(g_pMainTweakbar, "Panes As Portals", TW_TYPE_BOOLCPP, &g_gallery.m_globalShadertoyState.panesAsPortals, "  group='Gallery' ");

    TwAddButton(g_pMainTweakbar, "Enter/Exit Shader", ToggleShaderWorldCB, NULL, " group='Shader' ");
    TwAddVarRW(g_pMainTweakbar, "headSize", TW_TYPE_FLOAT, &m_headSize,
        " label='headSize' precision=4 min=0.0001 step=0.001 group='Shader' ");
    TwAddButton(g_pMainTweakbar, "Reset Timer", ResetTimerCB, &g_gallery,
        " label='Reset Timer' group='Shader' ");
}

///@brief Can be called before GL context is initialized.
void initHMD()
{
    g_hasHMD = g_openXR.Initialize();
    if (!g_hasHMD)
    {
        LOG_INFO("[RiftRay OpenXR] OpenXR unavailable; using monitor mode");
    }
}

///@brief Called once a GL context has been set up.
void initVR()
{
    if (!g_hasHMD)
    {
        LOG_INFO("No HMD available - VR features disabled");
        return;
    }

    if (!g_openXR.InitializeSession())
    {
        LOG_ERROR("[RiftRay OpenXR] Session initialization failed; using monitor mode");
        g_hasHMD = false;
        return;
    }

    if (!g_tweakbarQuad.initGL(
        g_openXR.GetSession(), g_openXR.GetColorFormat(), 600, 600))
    {
        LOG_ERROR("[RiftRay OpenXR] HUD quad initialization failed");
    }
}

glm::mat4 makeWorldToChassisMatrix()
{
    return makeChassisMatrix_glm(m_chassisYaw, 0.f, 0.f, m_chassisPos);
}

void storeHmdPose(const XrPosef& eyePose)
{
    m_hmdRo.x = eyePose.position.x + m_chassisPos.x;
    m_hmdRo.y = eyePose.position.y + m_chassisPos.y;
    m_hmdRo.z = eyePose.position.z + m_chassisPos.z;

    const glm::mat4 w2eye =
        makeWorldToChassisMatrix() * makeMatrixFromXrPose(eyePose, m_headSize);
    const glm::vec4 lookFwd(0.f, 0.f, -1.f, 0.f);
    const glm::vec4 rotvec = w2eye * lookFwd;
    m_hmdRd.x = rotvec.x;
    m_hmdRd.y = rotvec.y;
    m_hmdRd.z = rotvec.z;
}

// Display the old-fashioned way, to a monoscopic viewport on a desktop monitor.
void displayMonitor()
{
    if (g_pScene == NULL)
        return;

    const glm::mat4 mview = makeWorldToChassisMatrix();
    const glm::ivec2 vp = g_mirrorWindowSz;
    const glm::mat4 persp = glm::perspective(
        90.f,
        static_cast<float>(vp.x) / static_cast<float>(vp.y),
        .004f,
        500.f);
    glViewport(0, 0, vp.x, vp.y);
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    g_pScene->RenderForOneEye(glm::value_ptr(glm::inverse(mview)), glm::value_ptr(persp));

    m_hmdRo = m_chassisPos;
    m_hmdRd = glm::vec3(0.f, 0.f, -1.f);
}

// Display to an HMD through the active OpenXR runtime.
void displayHMD()
{
    if (!g_hasHMD)
    {
        displayMonitor();
        return;
    }

    bool shouldExit = false;
    if (!g_openXR.PollEvents(shouldExit))
    {
        displayMonitor();
        return;
    }
    if (shouldExit)
    {
        glfwSetWindowShouldClose(g_pMirrorWindow, 1);
        return;
    }
    if (!g_openXR.IsSessionRunning())
    {
        displayMonitor();
        return;
    }
    if (!g_openXR.BeginFrame())
    {
        displayMonitor();
        return;
    }
    g_openXR.SyncInput();
    if (!g_openXR.ShouldRender() || !g_openXR.LocateViews())
    {
        g_openXR.EndFrame(NULL, false);
        displayMonitor();
        return;
    }

    const uint32_t viewCount = g_openXR.GetViewCount();
    for (uint32_t eye = 0; eye < 2; ++eye)
        m_eyePoses[eye] = g_openXR.GetView(eye).pose;
    storeHmdPose(m_eyePoses[0]);

    bool renderedAllViews = true;
    for (uint32_t eye = 0; eye < viewCount; ++eye)
    {
        const XrView& view = g_openXR.GetView(eye);
        if (!g_openXR.BeginView(eye))
        {
            renderedAllViews = false;
            break;
        }

        const FBO& framebuffer = g_openXR.GetViewFramebuffer(eye);
        const int fullWidth = static_cast<int>(g_openXR.GetViewWidth(eye));
        const int fullHeight = static_cast<int>(g_openXR.GetViewHeight(eye));
        const float scale = std::max(.15f, std::min(1.f, m_fboScale));
        const int renderWidth = std::max(1, static_cast<int>(fullWidth * scale));
        const int renderHeight = std::max(1, static_cast<int>(fullHeight * scale));
        const int renderX = (fullWidth - renderWidth) / 2;
        const int renderY = (fullHeight - renderHeight) / 2;

        glViewport(0, 0, fullWidth, fullHeight);
        glClearColor(.3f, .3f, .3f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_FRAMEBUFFER_SRGB);
        glViewport(renderX, renderY, renderWidth, renderHeight);

        const int scissorInset = static_cast<int>(
            .5f * m_cinemaScope * static_cast<float>(renderHeight));
        glScissor(
            renderX, renderY + scissorInset,
            renderWidth, std::max(1, renderHeight - 2 * scissorInset));
        glEnable(GL_SCISSOR_TEST);
        glEnable(GL_DEPTH_TEST);

        const glm::mat4 modelView =
            makeWorldToChassisMatrix() *
            makeMatrixFromXrPose(view.pose, m_headSize);
        const glm::mat4 projection =
            makeProjectionFromXrFov(view.fov, .2f, 1000.f);
        g_pScene->RenderForOneEye(
            glm::value_ptr(glm::inverse(modelView)),
            glm::value_ptr(projection));
        glDisable(GL_SCISSOR_TEST);

        g_openXR.SetViewRenderRect(
            eye, renderX, renderY, renderWidth, renderHeight);

        if (eye == 0)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer.id);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            glBlitFramebuffer(
                renderX, renderY, renderX + renderWidth, renderY + renderHeight,
                0, 0, g_mirrorWindowSz.x, g_mirrorWindowSz.y,
                GL_COLOR_BUFFER_BIT, GL_LINEAR);
        }

        if (!g_openXR.EndView(eye))
        {
            renderedAllViews = false;
            break;
        }
    }

    XrCompositionLayerQuad quadLayer = { XR_TYPE_COMPOSITION_LAYER_QUAD };
    const XrCompositionLayerQuad* quadLayerPointer = NULL;
    if (renderedAllViews && g_tweakbarQuad.m_showQuadInWorld &&
        g_tweakbarQuad.GetSwapchain() != XR_NULL_HANDLE)
    {
        const OpenXRBackend::InputState& input = g_openXR.GetInputState();
        g_tweakbarQuad.SetHmdEyeRay(
            input.aimPoseValid ? input.aimPose : m_eyePoses[0]);
        g_tweakbarQuad.DrawToQuad();
        quadLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
        quadLayer.space = g_openXR.GetAppSpace();
        quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quadLayer.subImage.swapchain = g_tweakbarQuad.GetSwapchain();
        quadLayer.subImage.imageRect.offset = { 0, 0 };
        quadLayer.subImage.imageRect.extent = {
            static_cast<int32_t>(g_tweakbarQuad.GetWidth()),
            static_cast<int32_t>(g_tweakbarQuad.GetHeight()) };
        quadLayer.subImage.imageArrayIndex = 0;
        quadLayer.pose = g_tweakbarQuad.GetPose();
        quadLayer.size = { 1.f, 1.f };
        quadLayerPointer = &quadLayer;
    }
    g_openXR.EndFrame(quadLayerPointer, renderedAllViews);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, g_mirrorWindowSz.x, g_mirrorWindowSz.y);

#ifdef USE_ANTTWEAKBAR
    if (g_tweakbarQuad.m_showQuadInWorld)
    {
        TwDraw();
    }
#endif
}

void exitVR()
{
    if (g_tweakbarQuad.GetSwapchain() != XR_NULL_HANDLE)
        g_tweakbarQuad.exitGL();
    g_openXR.Shutdown();
    g_hasHMD = false;
}

static void ErrorCallback(int p_Error, const char* p_Description)
{
    (void)p_Error;
    (void)p_Description;
    LOG_INFO("ERROR: %d, %s", p_Error, p_Description);
}

void keyboard(GLFWwindow* pWindow, int key, int codes, int action, int mods)
{
    (void)pWindow;
    (void)codes;

    if ((key > -1) && (key <= GLFW_KEY_LAST))
    {
        m_keyStates[key] = action;
    }

    switch (key)
    {
        default: break;
        case GLFW_KEY_BACKSLASH:
        {
            if (action == GLFW_PRESS) g_tweakbarQuad.MouseClick(1);
            else if (action == GLFW_RELEASE) g_tweakbarQuad.MouseClick(0);
        }
        break;
        case GLFW_KEY_SLASH:
        {
            if (action == GLFW_PRESS)  g_tweakbarQuad.SetHoldingFlag(m_eyePoses[0], true);
            else if (action == GLFW_RELEASE) g_tweakbarQuad.SetHoldingFlag(m_eyePoses[0], false);
        }
        break;
    }

    const float yawIncr = 0.3f;
    if (action == GLFW_PRESS)
    {
    switch (key)
    {
        default:
            break;

        case GLFW_KEY_1:
            if (m_snapTurn == true)
            {
                m_chassisYaw -= yawIncr;
            }
            break;
        case GLFW_KEY_3:
            if (m_snapTurn == true)
            {
                m_chassisYaw += yawIncr;
            }
            break;

        case GLFW_KEY_SPACE:
            RecenterPoseCB(NULL);
            break;

        case GLFW_KEY_R:
            m_chassisPos = glm::vec3(0.f, 1.f, 0.f);
            break;

        case GLFW_KEY_BACKSPACE:
            TogglePerfHud();
            break;

        case GLFW_KEY_TAB:
            g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
            break;

        case GLFW_KEY_ENTER:
            g_gallery.ToggleShaderWorld();
            break;

        case GLFW_KEY_SLASH:
            break;

        case GLFW_KEY_ESCAPE:
            glfwSetWindowShouldClose(g_pMirrorWindow, 1);
            break;
    }
    }

    // Handle keyboard movement(WASD keys)
    const glm::vec3 forward(0.f, 0.f, -1.f);
    const glm::vec3 up(0.f, 1.f, 0.f);
    const glm::vec3 right(1.f, 0.f, 0.f);
    glm::vec3 keyboardMove(0.0f, 0.0f, 0.0f);
    float keyboardYaw = 0.f;
    if (m_keyStates['W'] != GLFW_RELEASE) { keyboardMove += forward; }
    if (m_keyStates['S'] != GLFW_RELEASE) { keyboardMove -= forward; }
    if (m_keyStates['A'] != GLFW_RELEASE) { keyboardMove -= right; }
    if (m_keyStates['D'] != GLFW_RELEASE) { keyboardMove += right; }
    if (m_keyStates['Q'] != GLFW_RELEASE) { keyboardMove -= up; }
    if (m_keyStates['E'] != GLFW_RELEASE) { keyboardMove += up; }
    if (m_keyStates[GLFW_KEY_UP] != GLFW_RELEASE) { keyboardMove += forward; }
    if (m_keyStates[GLFW_KEY_DOWN] != GLFW_RELEASE) { keyboardMove -= forward; }
    if (m_keyStates[GLFW_KEY_LEFT] != GLFW_RELEASE) { keyboardMove -= right; }
    if (m_keyStates[GLFW_KEY_RIGHT] != GLFW_RELEASE) { keyboardMove += right; }
    if (m_keyStates['1'] != GLFW_RELEASE) { keyboardYaw -= 1.f; }
    if (m_keyStates['3'] != GLFW_RELEASE) { keyboardYaw += 1.f; }

    float mag = 1.0f;
    if (m_keyStates[GLFW_KEY_LEFT_SHIFT] != GLFW_RELEASE)
        mag *= 0.1f;
    if (m_keyStates[GLFW_KEY_LEFT_CONTROL] != GLFW_RELEASE)
        mag *= 10.0f;
    m_keyboardMove = mag * keyboardMove;
    m_keyboardYaw = mag * keyboardYaw;
}

///@brief Check all available joysticks for an Xbox Controller
/// and store its idx in g_joystickIdx.
/// Unfortunately, this operation is too time-consuming to call every frame
/// in a VR app. The workaround is to call it on key press, space or 'G'.
void FindPreferredJoystick()
{
    g_joystickIdx = -1;
    for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; ++i)
    {
        if (GL_FALSE == glfwJoystickPresent(i))
            continue;

        const char* pJoyName = glfwGetJoystickName(i);
        if (pJoyName == NULL)
            continue;

        int numAxes = 0;
        int numButtons = 0;
        glfwGetJoystickAxes(i, &numAxes);
        glfwGetJoystickButtons(i, &numButtons);
        LOG_INFO("Glfw found Joystick #%d: %s w/ %d axes, %d buttons", i, pJoyName, numAxes, numButtons);

        // Take an educated guess that this is an Xbox controller - glfw's
        // id string says "Microsoft PC Joystick" for most gamepad types.
        ///@todo Why does GLFW on Linux return a different, more descriptive string?
        if (numAxes == 5 && numButtons == 14)
        {
            g_joystickIdx = i;
            return;
        }
    }
}

void joystick_XboxController(
    int, // joyidx
    const float* pAxisStates,
    int numAxes,
    const unsigned char* pButtonStates,
    int numButtons,
    const char* pLastButtonStates)
{
    //ASSERT(numAxes == 5);
    //ASSERT(numButtons == 14);
    if (numAxes != 5)
        return;
    if (numButtons != 14)
        return;

    // Xbox controller layout in glfw:
    // numAxes 5, numButtons 14
    // 0 A (down position)
    // 1 B (right position)
    // 2 X (left position)
    // 3 Y (up position)
    // 4 L bumper
    // 5 R bumper
    // 6 Back (left center)
    // 7 Start (right center)
    // 8 Left stick push
    // 9 Right stick push
    // 10 Dpad Up
    // 11 Dpad right
    // 12 Dpad down
    // 13 Dpad left
    // Axis 0 1 Left stick x y
    // Axis 2 triggers, left positive right negative
    // Axis 3 4 right stick y x

    glm::vec3 joystickMove(0.0f, 0.0f, 0.0f);
    // Xbox controller Left stick controls movement
    if (numAxes >= 2)
    {
        const float x_move = pAxisStates[0];
        const float y_move = pAxisStates[1];
        const glm::vec3 forward(0.f, 0.f, -1.f);
        const glm::vec3 right(1.f, 0.f, 0.f);
        const float deadzone = 0.5f;
        if (fabs(x_move) > deadzone)
            joystickMove += x_move * right;
        if (fabs(y_move) > deadzone)
            joystickMove -= y_move * forward;
    }

    if (pButtonStates[0] == GLFW_PRESS) // A button
        joystickMove += glm::vec3(0.f, 1.f, 0.f);
    if (pButtonStates[1] == GLFW_PRESS) // B button
        joystickMove += glm::vec3(0.f, -1.f, 0.f);

    float mag = 1.f;
    if (numAxes > 2)
    {
        // Xbox left and right analog triggers control speed
        mag = pow(10.f, pAxisStates[2]);
    }
    m_joystickMove = mag * joystickMove;

    // Right stick controls yaw
    ///@todo Pitch, Roll(instant nausea!)
    if (numAxes > 3)
    {
        float x_move = pAxisStates[4];
        const glm::vec3 up(0.f, 1.f, 0.f);
        const float deadzone = 0.2f;
        if (fabs(x_move) < deadzone)
            x_move = 0.f;
        m_joystickYaw = 0.75f * static_cast<float>(x_move);
    }

    // Check for recent button pushes
    const float f = 0.9f;
    for (int i = 0; i<numButtons; ++i)
    {
        const bool pressed = (pButtonStates[i] == GLFW_PRESS) &&
            (pLastButtonStates[i] != GLFW_PRESS);
        const bool released = (pButtonStates[i] != GLFW_PRESS) &&
            (pLastButtonStates[i] == GLFW_PRESS);
        if (pressed)
        {
            if (i == 13) // Dpad left
            {
                m_fboScale *= f;
                m_fboScale = std::max(.05f, m_fboScale);
            }
            if (i == 11) // Dpad right
            {
                m_fboScale /= f;
                m_fboScale = std::min(1.f, m_fboScale);
            }
            if (i == 10) // Dpad up
            {
                m_cinemaScope += 0.1f;
                m_cinemaScope = std::min(.95f, m_cinemaScope);
            }
            if (i == 12) // Dpad down
            {
                m_cinemaScope -= 0.1f;
                m_cinemaScope = std::max(0.f, m_cinemaScope);
            }
            if (i == 4) // Left Bumper
            {
                RecenterPoseCB(NULL);
            }
            if (i == 5) // Right Bumper
            {
                m_chassisPos = glm::vec3(0.f, 1.f, 0.f);
            }
            if (i == 7) // Start
            {
                g_gallery.ToggleShaderWorld();
            }
            if (i == 3) // Y button
            {
                g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
            }
        }
        if (pressed || released)
        {
            if (i == 2) // X button
            {
                g_tweakbarQuad.MouseClick(pressed ? 1 : 0);
            }
        }
    }
}

void joystick()
{
    if (g_joystickIdx == -1)
        return;

    static char s_lastButtons[256] = { 0 };

    ///@todo Handle multiple joysticks

    ///@todo Do these calls take time? We can move them out if so
    int joyStick1Present = glfwJoystickPresent(g_joystickIdx);
    if (joyStick1Present != GL_TRUE)
    {
        return;
    }

    // Poll joystick
    int numAxes = 0;
    const float* pAxisStates = glfwGetJoystickAxes(g_joystickIdx, &numAxes);
    int numButtons = 0;
    const unsigned char* pButtonStates = glfwGetJoystickButtons(g_joystickIdx, &numButtons);

    // Take an educated guess that this is an Xbox controller - glfw's
    // id string says "Microsoft PC Joystick" for most gamepad types.
    ///@todo Why does GLFW on Linux return a different, more descriptive string?
    if (numAxes == 5 && numButtons == 14)
    {
        joystick_XboxController(g_joystickIdx, pAxisStates, numAxes, pButtonStates, numButtons, s_lastButtons);
    }
    memcpy(s_lastButtons, pButtonStates, numButtons);
}

void mouseDown(GLFWwindow* pWindow, int button, int action, int mods)
{
    (void)pWindow;
    (void)mods;

    // Hold right button and press left
    if ((action == GLFW_PRESS) &&
        (button == GLFW_MOUSE_BUTTON_LEFT) &&
        (which_mouse_button == GLFW_MOUSE_BUTTON_RIGHT))
    {
        g_gallery.ToggleShaderWorld();
        return;
    }

    which_mouse_button = button;
    if (action == GLFW_RELEASE)
    {
        which_mouse_button = -1;
    }

    if ((action == GLFW_PRESS) && (button == GLFW_MOUSE_BUTTON_MIDDLE))
    {
        g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
        return;
    }

    if (g_tweakbarQuad.m_showQuadInWorld)
    {
        g_tweakbarQuad.MouseClick(action); ///@todo button id
    }
}

void mouseMove(GLFWwindow* pWindow, double xd, double yd)
{
    glfwGetCursorPos(pWindow, &xd, &yd);
    const int x = static_cast<int>(xd);
    const int y = static_cast<int>(yd);

    // Manual pointer capture - do not allow it to leave mousing quad //window bounds.
    {
        double xc = xd;
        double yc = yd;
        xc = std::max(0., xc);
        yc = std::max(0., yc);
        xc = std::min(xc, 600.); // static_cast<double>(g_mirrorWindowSz.x));
        yc = std::min(yc, 600.); // static_cast<double>(g_mirrorWindowSz.y));
        glfwSetCursorPos(pWindow, xc, yc);
    }

    g_tweakbarQuad.MouseMotion(x, y);
}

void mouseWheel(GLFWwindow* pWindow, double x, double y)
{
    (void)pWindow;
    (void)x;

    const float delta = static_cast<float>(y);
    const float incr = 0.05f;

    if (which_mouse_button == GLFW_MOUSE_BUTTON_LEFT)
    {
        float fbosc = m_fboScale;
        fbosc += incr * delta;
        fbosc = std::max(.15f, fbosc);
        fbosc = std::min(1.f, fbosc);
        m_fboScale = fbosc;
    }
    else
    {
        float cscope = m_cinemaScope;
        cscope += incr * delta;
        cscope = std::max(0.05f, cscope);
        cscope = std::min(1.f, cscope);
        m_cinemaScope = cscope;
    }
}

#if 0
// Legacy LibOVR input retained only until the OpenXR action checkpoint compiles.
void HandleRemote()
{
    if (!g_hasHMD)
        return;

    ovrInputState currentRemoteInputState;
    ovr_GetInputState(g_session, ovrControllerType_Remote, &currentRemoteInputState);
    const unsigned int b = currentRemoteInputState.Buttons;
    const unsigned int b0 = lastRemoteInputState.Buttons;
    const int toggleShaderButton = ovrButton_Enter;
    const int toggleMenuButton = ovrButton_Back;
    if (b & toggleShaderButton)
    {
        if (!(b0 & toggleShaderButton))
        {
            if (g_tweakbarQuad.m_showQuadInWorld == true)
            {
                g_tweakbarQuad.MouseClick(1);
            }
            else
            {
                g_gallery.ToggleShaderWorld();
            }
        }
    }
    else if (!(b & toggleShaderButton))
    {
        if (b0 & toggleShaderButton)
        {
            // Released button
            if (g_tweakbarQuad.m_showQuadInWorld == true)
            {
                g_tweakbarQuad.MouseClick(0);
            }
            else
            {
            }
        }
    }

    if (b & toggleMenuButton)
    {
        if (!(b0 & toggleMenuButton))
        {
            g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
        }
    }
    else if (!(b & toggleMenuButton))
    {
        if (b0 & toggleMenuButton)
        {
            // Released button
        }
    }

    glm::vec3 remoteMove(0.0f, 0.0f, 0.0f);
    if (b & ovrButton_Up) { remoteMove += glm::vec3(0.f, 0.f, -1.f); }
    if (b & ovrButton_Down) { remoteMove += glm::vec3(0.f, 0.f, 1.f); }
    if (b & ovrButton_Left) { remoteMove += glm::vec3(-1.f, 0.f, 0.f); }
    if (b & ovrButton_Right) { remoteMove += glm::vec3(1.f, 0.f, 0.f); }
    m_remoteMove = remoteMove;

    lastRemoteInputState = currentRemoteInputState;
}

void HandleXboxController()
{
    if (!g_hasHMD)
        return;

    ovrInputState currentXboxControllerInputState;
    ovr_GetInputState(g_session, ovrControllerType_XBox, &currentXboxControllerInputState);
    const unsigned int b = currentXboxControllerInputState.Buttons;
    const unsigned int b0 = lastXboxControllerInputState.Buttons;
    const int32_t Abut = ovrButton_A;
    const int32_t toggleShaderButton = ovrButton_Enter;
    const int32_t toggleTweakbarButton = ovrButton_Y;
    const int32_t togglePerfHudButton = ovrButton_Back;
    const int32_t holdTweakbarButton = ovrButton_RThumb;

    if (b & toggleShaderButton)
    {
        if (!(b0 & toggleShaderButton))
        {
            g_gallery.ToggleShaderWorld();
        }
    }

    if (b & toggleTweakbarButton)
    {
        if (!(b0 & toggleTweakbarButton))
        {
            g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
        }
    }

    if (b & Abut)
    {
        if (!(b0 & Abut))
        {
            g_tweakbarQuad.MouseClick(1);
        }
    }
    else if (!(b & Abut))
    {
        if (b0 & Abut)
        {
            g_tweakbarQuad.MouseClick(0);
        }
    }

    if ((b & toggleTweakbarButton) && !(b0 & toggleTweakbarButton))
    {
        g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
    }
    if ((b & togglePerfHudButton) && !(b0 & togglePerfHudButton))
    {
        TogglePerfHud();
    }

    if (b & holdTweakbarButton)
    {
        if (!(b0 & holdTweakbarButton))
        {
            g_tweakbarQuad.SetHoldingFlag(m_eyePoses[0], true);
        }
    }
    else if (!(b & holdTweakbarButton))
    {
        if (b0 & holdTweakbarButton)
        {
            g_tweakbarQuad.SetHoldingFlag(m_eyePoses[0], false);
        }
    }

    lastXboxControllerInputState = currentXboxControllerInputState;
}

// Oculus Touch controller input
void HandleTouchControllers()
{
    if (!g_hasHMD)
        return;

    ovrInputState currentTouchInputState;
    ovr_GetInputState(g_session, ovrControllerType_Touch, &currentTouchInputState);
    const unsigned int b = currentTouchInputState.Buttons;
    const unsigned int b0 = lastTouchInputState.Buttons;
    
    // Button mappings:
    // ovrButton_A, ovrButton_B (right controller)
    // ovrButton_X, ovrButton_Y (left controller)
    // ovrButton_Enter (menu button)
    // ovrButton_LThumb, ovrButton_RThumb (thumbstick press)
    
    // A button - Enter/Exit shader
    if ((b & ovrButton_A) && !(b0 & ovrButton_A))
    {
        g_gallery.ToggleShaderWorld();
    }
    
    // B button - Toggle tweakbar
    if ((b & ovrButton_B) && !(b0 & ovrButton_B))
    {
        g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
    }
    
    // X button - Click in tweakbar
    if (b & ovrButton_X)
    {
        if (!(b0 & ovrButton_X))
        {
            g_tweakbarQuad.MouseClick(1);
        }
    }
    else if (!(b & ovrButton_X))
    {
        if (b0 & ovrButton_X)
        {
            g_tweakbarQuad.MouseClick(0);
        }
    }
    
    // Y button - Reset position
    if ((b & ovrButton_Y) && !(b0 & ovrButton_Y))
    {
        m_chassisPos = glm::vec3(0.f, 1.f, 0.f);
    }
    
    // Menu button - Recenter
    if ((b & ovrButton_Enter) && !(b0 & ovrButton_Enter))
    {
        ovr_RecenterTrackingOrigin(g_session);
    }
    
    // Right thumbstick press - Hold/move tweakbar
    if (b & ovrButton_RThumb)
    {
        if (!(b0 & ovrButton_RThumb))
        {
            g_tweakbarQuad.SetHoldingFlag(m_eyePoses[0], true);
        }
    }
    else if (!(b & ovrButton_RThumb))
    {
        if (b0 & ovrButton_RThumb)
        {
            g_tweakbarQuad.SetHoldingFlag(m_eyePoses[0], false);
        }
    }
    
    // Left thumbstick - Movement
    glm::vec3 touchMove(0.0f, 0.0f, 0.0f);
    const float deadzone = 0.2f;
    const float x_move = currentTouchInputState.Thumbstick[ovrHand_Left].x;
    const float y_move = currentTouchInputState.Thumbstick[ovrHand_Left].y;
    
    if (fabs(x_move) > deadzone || fabs(y_move) > deadzone)
    {
        const glm::vec3 forward(0.f, 0.f, -1.f);
        const glm::vec3 right(1.f, 0.f, 0.f);
        touchMove += x_move * right;
        touchMove -= y_move * forward;
    }
    
    // Triggers for vertical movement
    const float leftTrigger = currentTouchInputState.IndexTrigger[ovrHand_Left];
    const float rightTrigger = currentTouchInputState.IndexTrigger[ovrHand_Right];
    const float triggerThreshold = 0.3f;
    
    if (leftTrigger > triggerThreshold)
    {
        touchMove += glm::vec3(0.f, -leftTrigger, 0.f);
    }
    if (rightTrigger > triggerThreshold)
    {
        touchMove += glm::vec3(0.f, rightTrigger, 0.f);
    }
    
    m_touchMove = touchMove;
    
    // Right thumbstick for smooth turning (if not snap turn mode)
    const float rx_move = currentTouchInputState.Thumbstick[ovrHand_Right].x;
    if (!m_snapTurn && fabs(rx_move) > deadzone)
    {
        m_joystickYaw = rx_move * 0.75f;
    }
    // Snap turn with right thumbstick
    else if (m_snapTurn)
    {
        static bool snapTurnReady = true;
        const float snapThreshold = 0.7f;
        const float yawIncr = 0.3f;
        
        if (fabs(rx_move) > snapThreshold && snapTurnReady)
        {
            if (rx_move > 0)
                m_chassisYaw += yawIncr;
            else
                m_chassisYaw -= yawIncr;
            snapTurnReady = false;
        }
        else if (fabs(rx_move) < deadzone)
        {
            snapTurnReady = true;
        }
    }
    
    lastTouchInputState = currentTouchInputState;
}

#endif

void HandleOpenXRInput()
{
    if (!g_hasHMD)
        return;

    static OpenXRBackend::InputState previous;
    const OpenXRBackend::InputState& current = g_openXR.GetInputState();

    if (current.toggleShader && !previous.toggleShader)
        g_gallery.ToggleShaderWorld();
    if (current.toggleHud && !previous.toggleHud)
        g_tweakbarQuad.m_showQuadInWorld = !g_tweakbarQuad.m_showQuadInWorld;
    if (current.click != previous.click)
        g_tweakbarQuad.MouseClick(current.click ? 1 : 0);
    if (current.reset && !previous.reset)
        m_chassisPos = glm::vec3(0.f, 1.f, 0.f);
    if (current.menu && !previous.menu)
        RecenterPoseCB(NULL);

    const XrPosef pointerPose = current.aimPoseValid
        ? current.aimPose : m_eyePoses[0];
    if (current.hold != previous.hold)
        g_tweakbarQuad.SetHoldingFlag(pointerPose, current.hold);
    if (current.aimPoseValid)
        g_tweakbarQuad.SetHmdEyeRay(current.aimPose);

    const float deadzone = .2f;
    glm::vec3 touchMove(0.f);
    if (fabs(current.move.x) > deadzone || fabs(current.move.y) > deadzone)
        touchMove += glm::vec3(current.move.x, 0.f, -current.move.y);
    if (current.leftTrigger > .3f)
        touchMove.y -= current.leftTrigger;
    if (current.rightTrigger > .3f)
        touchMove.y += current.rightTrigger;
    m_touchMove = touchMove;

    if (!m_snapTurn)
    {
        m_joystickYaw = fabs(current.turn.x) > deadzone
            ? current.turn.x * .75f : 0.f;
    }
    else
    {
        static bool snapTurnReady = true;
        if (fabs(current.turn.x) > .7f && snapTurnReady)
        {
            m_chassisYaw += current.turn.x > 0.f ? .3f : -.3f;
            snapTurnReady = false;
        }
        else if (fabs(current.turn.x) < deadzone)
        {
            snapTurnReady = true;
        }
    }
    previous = current;
}

void timestep()
{
    const double absT = g_timer.seconds();
    const double dt = absT - g_lastFrameTime;
    g_lastFrameTime = absT;
    if (g_pScene != NULL)
    {
        g_pScene->timestep(absT, dt);
    }

    // Move in the direction the viewer is facing.
    const glm::vec3 move_dt = (m_keyboardMove + m_joystickMove + m_remoteMove + m_touchMove) * m_headSize * static_cast<float>(dt);
    glm::mat4 moveTxfm = makeWorldToChassisMatrix();

    if (g_hasHMD && g_openXR.IsSessionRunning())
        moveTxfm *= makeMatrixFromXrPose(m_eyePoses[0], m_headSize);

    const glm::vec4 mv4 = moveTxfm * glm::vec4(move_dt, 0.f);
    m_chassisPos += glm::vec3(mv4);

    // Yaw control - snap turn is handled directly in keyboard function
    if (m_snapTurn == false)
    {
        const float rotSpeed = 10.f;
        m_chassisYaw += (m_keyboardYaw + m_joystickYaw) * static_cast<float>(dt);
    }

    HandleOpenXRInput();
}

void resize(GLFWwindow* pWindow, int w, int h)
{
    (void)pWindow;
    g_mirrorWindowSz.x = w;
    g_mirrorWindowSz.y = h;
}

void LoadConfigFile()
{
    const std::string cgfFile = "../RiftRay.cfg";

    std::ifstream file;
    file.open(cgfFile.c_str(), std::ios::in);
    if (!file.is_open())
        return;

    std::string line;
    while (std::getline(file, line))
    {
        const std::vector<std::string> toks = split(line, '=');
        if (toks.size() < 2)
            continue;
        const std::string& t = toks[0];
        if (!t.compare("DynamicallyScaleFBO"))
        {
            const int v = atoi(toks[1].c_str());
            //g_dynamicallyScaleFBO = (v != 0);
        }
        else if (!t.compare("LoadShadertoysRecursively"))
        {
            const int v = atoi(toks[1].c_str());
            g_loadShadertoysRecursive = (v != 0);
        }
        else if (!t.compare("FboMinimumScale"))
        {
            const float v = static_cast<float>(atof(toks[1].c_str()));
            //m_fboMinScale = v;
        }
        else if (!t.compare("AnimatedThumbnails"))
        {
            const int at = static_cast<int>(atof(toks[1].c_str()));
            g_gallery.m_globalShadertoyState.animatedThumbnails = (at != 0);
        }
        else if (!t.compare("PanesAsPortals"))
        {
            const int pp = static_cast<int>(atof(toks[1].c_str()));
            g_gallery.m_globalShadertoyState.panesAsPortals = (pp != 0);
        }
        else if (!t.compare("ThumbnailFboSize"))
        {
            const int ts = static_cast<int>(atof(toks[1].c_str()));
            g_gallery.m_paneDimensionPixels = ts;
        }
    }
    file.close();
}

void StartShaderLoad()
{
    g_gallery.LoadTextureLibrary();
    g_gallery.DiscoverShaders(g_loadShadertoysRecursive);

    ///@todo It would save some time to compile all these shaders in parallel on
    /// a multicore machine. Even cooler would be compiling them in a background thread
    /// while display is running, but trying that yields large frame rate drops
    /// which would make the VR experience unacceptably uncomfortable.
    //g_app.LoadTextureLibrary();
    g_gallery.CompileShaders();
    g_gallery.RearrangePanes();
    g_gallery.RenderThumbnails();
}

// OpenGL debug callback
void GLAPIENTRY myCallback(
    GLenum source, GLenum type, GLuint id, GLenum severity,
    GLsizei length, const GLchar *msg,
    const void *data)
{
    switch (severity)
    {
    case GL_DEBUG_SEVERITY_HIGH:
    case GL_DEBUG_SEVERITY_MEDIUM:
    case GL_DEBUG_SEVERITY_LOW:
        LOG_INFO("[[GL Debug]] %x %x %x %x %s", source, type, id, severity, msg);
        break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        break;
    }
}

int main(int argc, char** argv)
{
    LoadConfigFile();
    initHMD();

    glfwSetErrorCallback(ErrorCallback);
    if (!glfwInit())
    {
        exit(EXIT_FAILURE);
    }
    glfwWindowHint(GLFW_DEPTH_BITS, 16);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);// : GLFW_OPENGL_COMPAT_PROFILE);
    //glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#ifdef _DEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif

    const std::string windowName = "RiftRay v" + std::string(pRiftRayVersion);
    GLFWwindow* l_Window = glfwCreateWindow(g_mirrorWindowSz.x, g_mirrorWindowSz.y, windowName.c_str(), NULL, NULL);
    if (!l_Window)
    {
        LOG_ERROR("Glfw failed to create a window. Exiting.");
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    glfwMakeContextCurrent(l_Window);
    glfwSetKeyCallback(l_Window, keyboard);
    glfwSetMouseButtonCallback(l_Window, mouseDown);
    glfwSetCursorPosCallback(l_Window, mouseMove);
    glfwSetScrollCallback(l_Window, mouseWheel);
    glfwSetWindowSizeCallback(l_Window, resize);
    glfwSetInputMode(l_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    g_pMirrorWindow = l_Window;

    memset(m_keyStates, 0, GLFW_KEY_LAST*sizeof(int));
    FindPreferredJoystick();

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK)
    {
        LOG_INFO("glewInit() error.");
        exit(EXIT_FAILURE);
    }

#ifdef _DEBUG
    // Debug callback initialization
    // Must be done *after* glew initialization.
    glDebugMessageCallback(myCallback, NULL);
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);
    glDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 0,
        GL_DEBUG_SEVERITY_NOTIFICATION, -1 , "Start debugging");
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif

#ifdef USE_ANTTWEAKBAR
    TwInit(TW_OPENGL_CORE, NULL);
    initAnt();
#endif

    g_pScene = &g_gallery;// new ShaderGalleryScene();
    if (g_pScene != NULL)
    {
        g_pScene->initGL();
    }

    g_gallery.SetHmdPositionPointer(&m_hmdRo);
    g_gallery.SetHmdDirectionPointer(&m_hmdRd);
    g_gallery.SetChassisPosPointer(&m_chassisPos);
    g_gallery.SetChassisYawPointer(&m_chassisYaw);
    g_gallery.SetHeadSizePointer(&m_headSize);

    initVR();
    StartShaderLoad();
    glfwSwapInterval(0);
    while (!glfwWindowShouldClose(l_Window))
    {
        glfwPollEvents();
        joystick();
        timestep();

#ifdef USE_ANTTWEAKBAR
        TwRefreshBar(g_pMainTweakbar);
        TwRefreshBar(g_pShaderTweakbar);
#endif
        g_gallery.RenderPrePass();
        displayHMD();
        glfwSwapBuffers(l_Window);
    }
    exitVR();
    g_pScene->exitGL();
    glfwDestroyWindow(l_Window);
    glfwTerminate();
    exit(EXIT_SUCCESS);
}
