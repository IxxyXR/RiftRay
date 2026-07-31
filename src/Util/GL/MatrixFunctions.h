// MatrixFunctions.h

#pragma once

#include <glm/glm.hpp>

#include <openxr/openxr.h>

glm::mat4 makeChassisMatrix_glm(
    float chassisYaw,
    float chassisPitch,
    float chassisRoll,
    glm::vec3 chassisPos);

glm::mat4 makeMatrixFromXrPose(const XrPosef& pose, float worldScale = 1.f);
glm::mat4 makeProjectionFromXrFov(const XrFovf& fov, float nearZ, float farZ);
void GetXrEyeRayPosAndDir(const XrPosef& pose, glm::vec3& ro, glm::vec3& rd);
