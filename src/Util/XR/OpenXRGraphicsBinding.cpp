// OpenXRGraphicsBinding.cpp

#include "OpenXRGraphicsBinding.h"

#include <cstdio>

#if defined(_WIN32)

#include <GL/glew.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <Unknwn.h>
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL
#include <openxr/openxr_platform.h>

class WglOpenXRGraphicsBinding : public OpenXRGraphicsBinding
{
public:
    WglOpenXRGraphicsBinding()
        : m_binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR }
    {
    }

    const char* RequiredExtensionName() const override
    {
        return XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
    }

    XrResult PrepareSession(
        XrInstance instance, XrSystemId systemId,
        std::string& diagnostic) override
    {
        PFN_xrVoidFunction function = NULL;
        XrResult result = xrGetInstanceProcAddr(
            instance, "xrGetOpenGLGraphicsRequirementsKHR", &function);
        if (XR_FAILED(result) || function == NULL)
            return XR_FAILED(result) ? result : XR_ERROR_FUNCTION_UNSUPPORTED;

        XrGraphicsRequirementsOpenGLKHR requirements = {
            XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR
        };
        const PFN_xrGetOpenGLGraphicsRequirementsKHR getRequirements =
            reinterpret_cast<PFN_xrGetOpenGLGraphicsRequirementsKHR>(function);
        result = getRequirements(instance, systemId, &requirements);
        if (XR_FAILED(result))
            return result;

        m_binding.hDC = wglGetCurrentDC();
        m_binding.hGLRC = wglGetCurrentContext();
        if (m_binding.hDC == NULL || m_binding.hGLRC == NULL)
        {
            diagnostic = "No current WGL context for session creation";
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        }

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        const XrVersion current = XR_MAKE_VERSION(major, minor, 0);
        if (current < requirements.minApiVersionSupported ||
            current > requirements.maxApiVersionSupported)
        {
            char message[256] = {};
            std::snprintf(
                message, sizeof(message),
                "OpenGL %d.%d is outside runtime range %u.%u through %u.%u",
                major, minor,
                XR_VERSION_MAJOR(requirements.minApiVersionSupported),
                XR_VERSION_MINOR(requirements.minApiVersionSupported),
                XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
                XR_VERSION_MINOR(requirements.maxApiVersionSupported));
            diagnostic = message;
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        }

        char message[128] = {};
        std::snprintf(
            message, sizeof(message),
            "OpenGL %d.%d satisfies runtime graphics requirements",
            major, minor);
        diagnostic = message;
        return XR_SUCCESS;
    }

    const void* SessionCreateNext() const override
    {
        return &m_binding;
    }

    XrResult EnumerateSwapchainTextures(
        XrSwapchain swapchain, std::vector<uint32_t>& textures) const override
    {
        uint32_t imageCount = 0;
        XrResult result = xrEnumerateSwapchainImages(
            swapchain, 0, &imageCount, NULL);
        if (XR_FAILED(result) || imageCount == 0)
            return XR_FAILED(result) ? result : XR_ERROR_RUNTIME_FAILURE;

        std::vector<XrSwapchainImageOpenGLKHR> images(
            imageCount,
            XrSwapchainImageOpenGLKHR{ XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
        result = xrEnumerateSwapchainImages(
            swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
        if (XR_FAILED(result))
            return result;

        textures.resize(imageCount);
        for (uint32_t image = 0; image < imageCount; ++image)
            textures[image] = images[image].image;
        return XR_SUCCESS;
    }

private:
    XrGraphicsBindingOpenGLWin32KHR m_binding;
};

#elif defined(__ANDROID__)

#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <openxr/openxr_platform.h>

class EglOpenXRGraphicsBinding : public OpenXRGraphicsBinding
{
public:
    EglOpenXRGraphicsBinding()
        : m_binding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR }
    {
    }

    const char* RequiredExtensionName() const override
    {
        return XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME;
    }

    XrResult PrepareSession(
        XrInstance instance, XrSystemId systemId,
        std::string& diagnostic) override
    {
        PFN_xrVoidFunction function = NULL;
        XrResult result = xrGetInstanceProcAddr(
            instance, "xrGetOpenGLESGraphicsRequirementsKHR", &function);
        if (XR_FAILED(result) || function == NULL)
            return XR_FAILED(result) ? result : XR_ERROR_FUNCTION_UNSUPPORTED;

        XrGraphicsRequirementsOpenGLESKHR requirements = {
            XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR
        };
        const PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements =
            reinterpret_cast<PFN_xrGetOpenGLESGraphicsRequirementsKHR>(function);
        result = getRequirements(instance, systemId, &requirements);
        if (XR_FAILED(result))
            return result;

        m_binding.display = eglGetCurrentDisplay();
        m_binding.context = eglGetCurrentContext();
        if (m_binding.display == EGL_NO_DISPLAY ||
            m_binding.context == EGL_NO_CONTEXT)
        {
            diagnostic = "No current EGL context for session creation";
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        }

        EGLint configId = 0;
        if (eglQueryContext(
                m_binding.display, m_binding.context,
                EGL_CONFIG_ID, &configId) != EGL_TRUE)
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        const EGLint attributes[] = { EGL_CONFIG_ID, configId, EGL_NONE };
        EGLint configCount = 0;
        if (eglChooseConfig(
                m_binding.display, attributes, &m_binding.config,
                1, &configCount) != EGL_TRUE || configCount != 1)
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;

        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        const XrVersion current = XR_MAKE_VERSION(major, minor, 0);
        if (current < requirements.minApiVersionSupported ||
            current > requirements.maxApiVersionSupported)
        {
            diagnostic = "OpenGL ES version is outside the runtime range";
            return XR_ERROR_GRAPHICS_DEVICE_INVALID;
        }
        diagnostic = "OpenGL ES satisfies runtime graphics requirements";
        return XR_SUCCESS;
    }

    const void* SessionCreateNext() const override
    {
        return &m_binding;
    }

    XrResult EnumerateSwapchainTextures(
        XrSwapchain swapchain, std::vector<uint32_t>& textures) const override
    {
        uint32_t imageCount = 0;
        XrResult result = xrEnumerateSwapchainImages(
            swapchain, 0, &imageCount, NULL);
        if (XR_FAILED(result) || imageCount == 0)
            return XR_FAILED(result) ? result : XR_ERROR_RUNTIME_FAILURE;

        std::vector<XrSwapchainImageOpenGLESKHR> images(
            imageCount,
            XrSwapchainImageOpenGLESKHR{
                XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR });
        result = xrEnumerateSwapchainImages(
            swapchain, imageCount, &imageCount,
            reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
        if (XR_FAILED(result))
            return result;

        textures.resize(imageCount);
        for (uint32_t image = 0; image < imageCount; ++image)
            textures[image] = images[image].image;
        return XR_SUCCESS;
    }

private:
    XrGraphicsBindingOpenGLESAndroidKHR m_binding;
};

#endif

std::unique_ptr<OpenXRGraphicsBinding> CreatePlatformOpenXRGraphicsBinding()
{
#if defined(_WIN32)
    return std::unique_ptr<OpenXRGraphicsBinding>(
        new WglOpenXRGraphicsBinding());
#elif defined(__ANDROID__)
    return std::unique_ptr<OpenXRGraphicsBinding>(
        new EglOpenXRGraphicsBinding());
#else
    return std::unique_ptr<OpenXRGraphicsBinding>();
#endif
}
