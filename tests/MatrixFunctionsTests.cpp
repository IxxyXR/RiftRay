#include "MatrixFunctions.h"

#include <cmath>
#include <iostream>

namespace
{
bool Near(float actual, float expected)
{
    return std::fabs(actual - expected) < 0.0001f;
}

bool Check(bool condition, const char* message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}
}

int main()
{
    bool passed = true;

    XrFovf symmetric = {};
    symmetric.angleLeft = -std::atan(1.f);
    symmetric.angleRight = std::atan(1.f);
    symmetric.angleDown = -std::atan(1.f);
    symmetric.angleUp = std::atan(1.f);
    const glm::mat4 symmetricProjection =
        makeProjectionFromXrFov(symmetric, .2f, 1000.f);
    passed &= Check(Near(symmetricProjection[0][0], 1.f),
        "symmetric projection horizontal scale");
    passed &= Check(Near(symmetricProjection[1][1], 1.f),
        "symmetric projection vertical scale");
    passed &= Check(Near(symmetricProjection[2][0], 0.f),
        "symmetric projection horizontal offset");
    passed &= Check(Near(symmetricProjection[2][1], 0.f),
        "symmetric projection vertical offset");

    XrFovf asymmetric = {};
    asymmetric.angleLeft = std::atan(-.5f);
    asymmetric.angleRight = std::atan(1.5f);
    asymmetric.angleDown = std::atan(-.75f);
    asymmetric.angleUp = std::atan(1.25f);
    const glm::mat4 asymmetricProjection =
        makeProjectionFromXrFov(asymmetric, .2f, 1000.f);
    passed &= Check(Near(asymmetricProjection[0][0], 1.f),
        "asymmetric projection horizontal scale");
    passed &= Check(Near(asymmetricProjection[1][1], 1.f),
        "asymmetric projection vertical scale");
    passed &= Check(Near(asymmetricProjection[2][0], .5f),
        "asymmetric projection horizontal offset");
    passed &= Check(Near(asymmetricProjection[2][1], .25f),
        "asymmetric projection vertical offset");

    XrPosef pose = {};
    pose.orientation.w = 1.f;
    pose.position = { 1.f, 2.f, 3.f };
    const glm::mat4 poseMatrix = makeMatrixFromXrPose(pose, 2.f);
    passed &= Check(Near(poseMatrix[3][0], 2.f) &&
        Near(poseMatrix[3][1], 4.f) && Near(poseMatrix[3][2], 6.f),
        "pose translation respects world scale");

    glm::vec3 rayOrigin;
    glm::vec3 rayDirection;
    GetXrEyeRayPosAndDir(pose, rayOrigin, rayDirection);
    passed &= Check(Near(rayOrigin.x, 1.f) && Near(rayOrigin.y, 2.f) &&
        Near(rayOrigin.z, 3.f), "ray origin follows pose");
    passed &= Check(Near(rayDirection.x, 0.f) && Near(rayDirection.y, 0.f) &&
        Near(rayDirection.z, -1.f), "identity pose looks down negative Z");

    return passed ? 0 : 1;
}
