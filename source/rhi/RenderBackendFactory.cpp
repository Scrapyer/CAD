#include "RenderBackendFactory.h"

#include "OpenGLRenderBackend.h"
#if defined(FERENDER_HAS_VULKAN_RHI)
#include "VulkanRenderBackend.h"
#endif

std::unique_ptr<IRenderBackend> createRenderBackend(RenderBackendKind kind)
{
    switch (kind) {
#if defined(FERENDER_HAS_VULKAN_RHI)
    case RenderBackendKind::Vulkan:
        return std::make_unique<VulkanRenderBackend>();
#endif
    case RenderBackendKind::OpenGL:
    default:
        return std::make_unique<OpenGLRenderBackend>();
    }
}

bool isRenderBackendAvailable(RenderBackendKind kind)
{
    switch (kind) {
    case RenderBackendKind::OpenGL:
        return true;
    case RenderBackendKind::Vulkan:
#if defined(FERENDER_HAS_VULKAN_RHI)
        return true;
#else
        return false;
#endif
    default:
        return false;
    }
}

const char* renderBackendName(RenderBackendKind kind)
{
    switch (kind) {
    case RenderBackendKind::OpenGL:
        return "OpenGL";
    case RenderBackendKind::Vulkan:
        return "Vulkan";
    default:
        return "Unknown";
    }
}
