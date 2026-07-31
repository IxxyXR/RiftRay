// HudQuad.cpp

#include "HudQuad.h"

#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr_platform.h>

#include "Logger.h"
#include "MatrixFunctions.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/intersect.hpp>

HudQuad::HudQuad()
: m_QuadPoseCenter()
, m_swapChain(XR_NULL_HANDLE)
, m_session(XR_NULL_HANDLE)
, m_showQuadInWorld(true)
, m_fbo()
, m_quadSize(1.f)
, m_swapchainTextures()
, m_imageAcquired(false)
, m_acquiredImageIndex(0)
, m_holding(false)
, m_planePositionAtGrab(0.f)
, m_hitPtPositionAtGrab(0.f)
, m_hitPtTParam(-1.f)
{
    m_QuadPoseCenter.orientation.x = 0.129206583f;
    m_QuadPoseCenter.orientation.y = 0.0310291424f;
    m_QuadPoseCenter.orientation.z = 0.000810863741f;
    m_QuadPoseCenter.orientation.w = -0.991131783f;
    m_QuadPoseCenter.position = { 0.f, -.375f, -.75f };
}

HudQuad::~HudQuad()
{
}

bool HudQuad::initGL(
    XrSession session, int64_t format, uint32_t width, uint32_t height)
{
    m_session = session;
    XrSwapchainCreateInfo createInfo = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
    createInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.format = format;
    createInfo.sampleCount = 1;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.faceCount = 1;
    createInfo.arraySize = 1;
    createInfo.mipCount = 1;
    XrResult result = xrCreateSwapchain(session, &createInfo, &m_swapChain);
    if (XR_FAILED(result))
    {
        LOG_ERROR("[RiftRay OpenXR] HudQuad swapchain creation failed: %d", result);
        m_swapChain = XR_NULL_HANDLE;
        return false;
    }

    uint32_t imageCount = 0;
    result = xrEnumerateSwapchainImages(m_swapChain, 0, &imageCount, NULL);
    if (XR_FAILED(result) || imageCount == 0)
    {
        LOG_ERROR("[RiftRay OpenXR] HudQuad has no swapchain images");
        exitGL();
        return false;
    }

    std::vector<XrSwapchainImageOpenGLKHR> images(
        imageCount,
        XrSwapchainImageOpenGLKHR{ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
    result = xrEnumerateSwapchainImages(
        m_swapChain, imageCount, &imageCount,
        reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    if (XR_FAILED(result))
    {
        LOG_ERROR("[RiftRay OpenXR] HudQuad image enumeration failed: %d", result);
        exitGL();
        return false;
    }
    m_swapchainTextures.resize(imageCount);
    for (uint32_t image = 0; image < imageCount; ++image)
    {
        m_swapchainTextures[image] = images[image].image;
        glBindTexture(GL_TEXTURE_2D, images[image].image);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    m_fbo.w = width;
    m_fbo.h = height;
    glGenFramebuffers(1, &m_fbo.id);

    glGenRenderbuffers(1, &m_fbo.depth);
    glBindRenderbuffer(GL_RENDERBUFFER, m_fbo.depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, width, height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    return true;
}

void HudQuad::exitGL()
{
    if (m_imageAcquired)
        _FinalizeDrawToQuad();
    FBO& f = m_fbo;
    if (f.id != 0)
        glDeleteFramebuffers(1, &f.id), f.id = 0;
    if (f.depth != 0)
        glDeleteRenderbuffers(1, &f.depth), f.depth = 0;
    if (m_swapChain != XR_NULL_HANDLE)
    {
        xrDestroySwapchain(m_swapChain);
        m_swapChain = XR_NULL_HANDLE;
    }
    m_swapchainTextures.clear();
    m_session = XR_NULL_HANDLE;
}

///@brief Called from the UI to indicate whether the user is holding and dragging
/// the quad around in world space.
void HudQuad::SetHoldingFlag(XrPosef pose, bool f)
{
    if (f == false)
    {
        m_holding = false;
        return;
    }
    glm::vec3 ro, rd;
    GetXrEyeRayPosAndDir(pose, ro, rd);
    const glm::mat4 quadposeMatrix = makeMatrixFromXrPose(GetPose());

    glm::vec2 planePt;
    float tParam;
    const bool hit = GetPaneRayIntersectionCoordinates(quadposeMatrix, ro, rd, planePt, tParam);
    if (hit == true)
    {
        if ((f == true) && (m_holding == false))
        {
            // Just grabbed; store quad's pose at start
            m_holding = true;
            m_hitPtTParam = tParam;
            const XrVector3f& tx = m_QuadPoseCenter.position;
            m_planePositionAtGrab = glm::vec3(tx.x, tx.y, tx.z);
            m_hitPtPositionAtGrab = ro + m_hitPtTParam*rd;
        }
    }
}

bool HudQuad::_PrepareToDrawToQuad()
{
    XrSwapchainImageAcquireInfo acquireInfo = {
        XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO
    };
    XrResult result = xrAcquireSwapchainImage(
        m_swapChain, &acquireInfo, &m_acquiredImageIndex);
    if (XR_FAILED(result))
        return false;
    m_imageAcquired = true;

    XrSwapchainImageWaitInfo waitInfo = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = xrWaitSwapchainImage(m_swapChain, &waitInfo);
    if (XR_FAILED(result))
    {
        _FinalizeDrawToQuad();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo.id);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        m_swapchainTextures[m_acquiredImageIndex], 0);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_fbo.depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    {
        _FinalizeDrawToQuad();
        return false;
    }
    glViewport(0, 0, m_fbo.w, m_fbo.h);
    return true;
}

void HudQuad::DrawToQuad()
{
    if (!_PrepareToDrawToQuad())
        return;
    {
        const float g = .3f;
        glClearColor(g, g, g, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    _FinalizeDrawToQuad();
}

bool HudQuad::_FinalizeDrawToQuad()
{
    if (!m_imageAcquired)
        return true;
    glFramebufferTexture2D(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    XrSwapchainImageReleaseInfo releaseInfo = {
        XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO
    };
    const XrResult result = xrReleaseSwapchainImage(m_swapChain, &releaseInfo);
    m_imageAcquired = false;
    return XR_SUCCEEDED(result);
}

///@param [out] planePtOut Intersection point on plane in local normalized coordinates
///@param [out] tParamOut T parameter value along intersection ray
///@return true if ray hits pane quad, false otherwise
bool HudQuad::GetPaneRayIntersectionCoordinates(
    const glm::mat4& quadPoseMatrix, ///< [in] Quad's pose in world space
    glm::vec3 origin3, ///< [in] Ray origin
    glm::vec3 dir3, ///< [in] Ray direction(normalized)
    glm::vec2& planePtOut, ///< [out] Intersection point in XY plane coordinates
    float& tParamOut) ///< [out] t parameter of ray intersection (ro + t*dt)
{
    if (m_showQuadInWorld == false)
        return false;

    // Standard OpenXR quad layer coordinates.
    glm::vec3 pts[] = {
        glm::vec3(-.5f*m_quadSize.x, -.5f*m_quadSize.y, 0.f),
        glm::vec3( .5f*m_quadSize.x, -.5f*m_quadSize.y, 0.f),
        glm::vec3( .5f*m_quadSize.x,  .5f*m_quadSize.y, 0.f),
        glm::vec3(-.5f*m_quadSize.x,  .5f*m_quadSize.y, 0.f),
    };
    for (int i = 0; i < 4; ++i)
    {
        glm::vec4 p4 = glm::vec4(pts[i], 1.f);
        p4 = quadPoseMatrix * p4;
        pts[i] = glm::vec3(p4);
    }

    glm::vec3 retval1(0.f);
    glm::vec3 retval2(0.f);
    const bool hit1 = glm::intersectLineTriangle(origin3, dir3, pts[0], pts[1], pts[2], retval1);
    const bool hit2 = glm::intersectLineTriangle(origin3, dir3, pts[0], pts[2], pts[3], retval2);
    if (!(hit1 || hit2))
        return false;

    glm::vec3 hitval(0.f);
    glm::vec3 cartesianpos(0.f);
    if (hit1)
    {
        hitval = retval1;
        // At this point, retval1 or retval2 contains hit data returned from glm::intersectLineTriangle.
        // This does not appear to be raw - y and z appear to be barycentric coordinates.
        // Fill out the x coord with the barycentric identity then convert using simple weighted sum.
        cartesianpos =
            (1.f - hitval.y - hitval.z) * pts[0] +
            hitval.y * pts[1] +
            hitval.z * pts[2];
    }
    else if (hit2)
    {
        hitval = retval2;
        cartesianpos =
            (1.f - hitval.y - hitval.z) * pts[0] +
            hitval.y * pts[2] +
            hitval.z * pts[3];
    }

    // Store the t param along controller ray of the hit in the Transformation
    // Did you know that x stores the t param val? I couldn't find this in docs anywhere.
    const float tParam = hitval.x;
    tParamOut = tParam;
    if (tParam < 0.f)
        return false; // Behind the origin

    const glm::vec3 v1 = pts[1] - pts[0]; // x axis
    const glm::vec3 v2 = pts[3] - pts[0]; // y axis
    const float len = glm::length(v1); // v2 length should be equal
    const glm::vec3 vh = (cartesianpos - pts[0]) / len;
    planePtOut = glm::vec2(
        glm::dot(v1 / len, vh),
        1.f - glm::dot(v2 / len, vh) // y coord flipped by convention
        );

    return true;
}

///@brief Update the latest position of the HMD - used for grabbing the quad
/// with a key then glancing while holding it to move the quad in space.
///@note Writes to m_layerQuad.QuadPoseCenter
void HudQuad::SetHmdEyeRay(XrPosef pose)
{
    glm::vec3 ro, rd;
    GetXrEyeRayPosAndDir(pose, ro, rd);

    if (m_holding == true)
    {
        const glm::vec3 rayPt = ro + m_hitPtTParam * rd;
        const glm::vec3 movement = rayPt - m_hitPtPositionAtGrab;
        const glm::vec3 quadLocation = m_planePositionAtGrab + movement;
        m_QuadPoseCenter.position.x = quadLocation.x;
        m_QuadPoseCenter.position.y = quadLocation.y;
        m_QuadPoseCenter.position.z = quadLocation.z;
        m_QuadPoseCenter.orientation = pose.orientation;
    }
}
