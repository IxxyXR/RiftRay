// MatrixFunctions.h

#pragma once

#include <glm/glm.hpp>

#ifdef USE_OCULUSSDK
#include <OVR_CAPI.h>
#endif

#ifdef RIFTRAY_HAS_OPENXR
#include <openxr/openxr.h>
#endif

glm::mat4 makeChassisMatrix_glm(
    float chassisYaw,
    float chassisPitch,
    float chassisRoll,
    glm::vec3 chassisPos);

#ifdef USE_OCULUSSDK
glm::mat4 makeMatrixFromPose(const ovrPosef& eyePose, float headSize = 1.f);
glm::mat4 makeGlmMatrixFromOvrMatrix(const ovrMatrix4f& om);
ovrMatrix4f makeOVRMatrixFromGlmMatrix(const glm::mat4& gm);
void GetHMDEyeRayPosAndDir(const ovrPosef& pose, glm::vec3& ro, glm::vec3& rd);
#endif

#ifdef RIFTRAY_HAS_OPENXR
glm::mat4 makeMatrixFromXrPose(const XrPosef& pose, float worldScale = 1.f);
glm::mat4 makeProjectionFromXrFov(const XrFovf& fov, float nearZ, float farZ);
void GetXrEyeRayPosAndDir(const XrPosef& pose, glm::vec3& ro, glm::vec3& rd);
#endif
