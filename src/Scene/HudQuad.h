// HudQuad.h

#pragma once

#ifdef _WIN32
#  define WINDOWS_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <openxr/openxr.h>
#include "FBO.h"

#include <vector>

///@brief A flat quad displayed in-world as an OpenXR compositor layer.
class HudQuad
{
public:
    HudQuad();
    virtual ~HudQuad();

    virtual bool initGL(XrSession session, int64_t format, uint32_t width, uint32_t height);
    virtual void exitGL();
    virtual void DrawToQuad();
    virtual void SetHoldingFlag(XrPosef pose, bool f);
    virtual XrPosef GetPose() const { return m_QuadPoseCenter; }
    virtual void SetHmdEyeRay(XrPosef pose);

    XrSwapchain GetSwapchain() const { return m_swapChain; }
    uint32_t GetWidth() const { return static_cast<uint32_t>(m_fbo.w); }
    uint32_t GetHeight() const { return static_cast<uint32_t>(m_fbo.h); }

    virtual bool GetPaneRayIntersectionCoordinates(
        const glm::mat4& quadPoseMatrix, ///< [in] Quad's pose in world space
        glm::vec3 origin3, ///< [in] Ray origin
        glm::vec3 dir3, ///< [in] Ray direction(normalized)
        glm::vec2& planePtOut, ///< [out] Intersection point in XY plane coordinates
        float& tParamOut); ///< [out] t parameter of ray intersection (ro + t*dt)

    XrPosef m_QuadPoseCenter;
    XrSwapchain m_swapChain;
    XrSession m_session;
    bool m_showQuadInWorld;

protected:
    bool _PrepareToDrawToQuad();
    bool _FinalizeDrawToQuad();

    FBO m_fbo;
    glm::vec2 m_quadSize;
    std::vector<GLuint> m_swapchainTextures;
    std::vector<bool> m_framebufferValidated;
    bool m_imageAcquired;
    uint32_t m_acquiredImageIndex;

    // Movement state
    bool m_holding;
    glm::vec3 m_planePositionAtGrab;
    glm::vec3 m_hitPtPositionAtGrab;
    float m_hitPtTParam;

private: // Disallow copy ctor and assignment operator
    HudQuad(const HudQuad&);
    HudQuad& operator=(const HudQuad&);
};
