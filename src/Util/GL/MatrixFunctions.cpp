// MatrixFunctions.cpp

#include "MatrixFunctions.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>

glm::mat4 makeChassisMatrix_glm(
    float chassisYaw,
    float chassisPitch,
    float chassisRoll,
    glm::vec3 chassisPos)
{
    return
        glm::translate(glm::mat4(1.f), chassisPos)
        * glm::rotate(glm::mat4(1.f), -chassisYaw, glm::vec3(0,1,0))
        * glm::rotate(glm::mat4(1.f), -chassisRoll, glm::vec3(0,0,1))
        * glm::rotate(glm::mat4(1.f), -chassisPitch, glm::vec3(1,0,0))
        ;
}

#ifdef USE_OCULUSSDK
/// Turn ovrPosef of the HMD into a glm matrix. Pose is delivered by libOVR per-frame
/// as close to render time as possible to reduce latency.
glm::mat4 makeMatrixFromPose(const ovrPosef& eyePose, float headSize)
{
    const ovrVector3f& p = eyePose.Position;
    const ovrQuatf& q = eyePose.Orientation;
    return glm::translate(glm::mat4(1.f), headSize * glm::vec3(p.x, p.y, p.z))
        * glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z));
}

glm::mat4 makeGlmMatrixFromOvrMatrix(const ovrMatrix4f& om)
{
    glm::mat4 gm;
    memcpy(
        glm::value_ptr(gm),
        reinterpret_cast<const float*>(&om.M[0][0]),
        16 * sizeof(float));
    return glm::transpose(gm);
}

///@return an equivalent OVR matrix to the given glm one.
/// OVR uses DX's left-handed convention, so transpose is necessary.
ovrMatrix4f makeOVRMatrixFromGlmMatrix(const glm::mat4& glm_m)
{
    ovrMatrix4f ovr_m;
    memcpy(
        reinterpret_cast<float*>(&ovr_m.M[0][0]),
        glm::value_ptr(glm::transpose(glm_m)),
        16 * sizeof(float));
    return ovr_m; // copied on return
}

///@return the unit vector along negative z transformed by the given pose
void GetHMDEyeRayPosAndDir(const ovrPosef& pose, glm::vec3& ro, glm::vec3& rd)
{
    const glm::mat4 poseMtx = makeMatrixFromPose(pose, 1.f);
    const glm::vec4 origin(0.f, 0.f, 0.f, 1.f);
    const glm::vec4 lookFwd(0.f, 0.f, -1.f, 0.f);
    const glm::vec4 o4 = poseMtx * origin;
    const glm::vec4 d4 = poseMtx * lookFwd;
    ro = glm::vec3(o4.x, o4.y, o4.z);
    rd = glm::vec3(d4.x, d4.y, d4.z);
}
#endif

#ifdef RIFTRAY_HAS_OPENXR
/// Convert an OpenXR pose into the world transform consumed by RiftRay scenes.
/// OpenXR and RiftRay both use right-handed coordinates with -Z forward.
glm::mat4 makeMatrixFromXrPose(const XrPosef& pose, float worldScale)
{
    const XrVector3f& p = pose.position;
    const XrQuaternionf& q = pose.orientation;
    return glm::translate(
        glm::mat4(1.f), worldScale * glm::vec3(p.x, p.y, p.z))
        * glm::mat4_cast(glm::quat(q.w, q.x, q.y, q.z));
}

/// Build an OpenGL right-handed projection matrix from OpenXR's asymmetric FOV.
glm::mat4 makeProjectionFromXrFov(const XrFovf& fov, float nearZ, float farZ)
{
    const float left = std::tan(fov.angleLeft);
    const float right = std::tan(fov.angleRight);
    const float down = std::tan(fov.angleDown);
    const float up = std::tan(fov.angleUp);
    const float width = right - left;
    const float height = up - down;

    glm::mat4 projection(0.f);
    projection[0][0] = 2.f / width;
    projection[1][1] = 2.f / height;
    projection[2][0] = (right + left) / width;
    projection[2][1] = (up + down) / height;
    projection[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    projection[2][3] = -1.f;
    projection[3][2] = -(2.f * farZ * nearZ) / (farZ - nearZ);
    return projection;
}

void GetXrEyeRayPosAndDir(const XrPosef& pose, glm::vec3& ro, glm::vec3& rd)
{
    const glm::mat4 poseMatrix = makeMatrixFromXrPose(pose);
    const glm::vec4 origin = poseMatrix * glm::vec4(0.f, 0.f, 0.f, 1.f);
    const glm::vec4 direction = poseMatrix * glm::vec4(0.f, 0.f, -1.f, 0.f);
    ro = glm::vec3(origin);
    rd = glm::normalize(glm::vec3(direction));
}
#endif
