// AntQuad.h

#pragma once

#ifdef _WIN32
#  define WINDOWS_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif
#include <GL/glew.h>

#include "MousingQuad.h"

///@brief Draws an AntTweakBar to a quad
class AntQuad : public MousingQuad
{
public:
    AntQuad();
    virtual ~AntQuad();

    virtual bool initGL(XrSession session, int64_t format, uint32_t width, uint32_t height);
    virtual void exitGL();
    virtual void DrawToQuad();
    virtual void MouseClick(int state);
    virtual void MouseMotion(int x, int y);
    virtual void SetHmdEyeRay(XrPosef pose);

protected:

private: // Disallow copy ctor and assignment operator
    AntQuad(const AntQuad&);
    AntQuad& operator=(const AntQuad&);
};
