#include "RenderBackendFactory.h"
#if defined(FERENDER_HAS_VULKAN_RHI)
#include "VulkanRenderBackend.h"
#include "VulkanSurface.h"
#if defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
#include "VulkanMacOSSurfaceFactory.h"
#endif
#endif

#include <QString>

#include <cstring>
#include <cstdio>
#include <memory>

int main()
{
    if (!isRenderBackendAvailable(RenderBackendKind::OpenGL)) {
        return 1;
    }
    if (std::strcmp(renderBackendName(RenderBackendKind::OpenGL), "OpenGL") != 0) {
        return 2;
    }
    if (std::strcmp(renderBackendName(RenderBackendKind::Vulkan), "Vulkan") != 0) {
        return 3;
    }
    if (std::strcmp(renderBackendName(RenderBackendKind::Metal), "Metal") != 0) {
        return 13;
    }

    std::unique_ptr<IRenderBackend> glBackend = createRenderBackend(RenderBackendKind::OpenGL);
    if (!glBackend) {
        return 4;
    }

    if (isRenderBackendAvailable(RenderBackendKind::Vulkan)) {
        std::unique_ptr<IRenderBackend> vulkanBackend = createRenderBackend(RenderBackendKind::Vulkan);
        if (!vulkanBackend) {
            return 5;
        }
        vulkanBackend->initialize();
        if (vulkanBackend->info().renderer.isEmpty() || vulkanBackend->info().version.isEmpty()) {
            return 6;
        }
        vulkanBackend.reset();
    }

    std::unique_ptr<IRenderBackend> metalBackend = createRenderBackend(RenderBackendKind::Metal);
    if (!metalBackend) {
        return 14;
    }
    metalBackend->initialize();
#if defined(FERENDER_HAS_METAL_RHI)
    if (isRenderBackendAvailable(RenderBackendKind::Metal) &&
        (metalBackend->info().renderer.isEmpty() || metalBackend->info().version.isEmpty())) {
        return 15;
    }
#else
    if (isRenderBackendAvailable(RenderBackendKind::Metal)) {
        return 16;
    }
#endif
    metalBackend.reset();

#if defined(FERENDER_HAS_VULKAN_RHI)
    VulkanRenderBackend directVulkanBackend;
#if defined(FERENDER_HAS_MACOS_VULKAN_SURFACE)
    const std::vector<const char*> requiredSurfaceExtensions =
        VulkanMacOSSurfaceFactory::requiredInstanceExtensions();
#else
    const std::vector<const char*> requiredSurfaceExtensions;
#endif
    if (!directVulkanBackend.initializeContext(requiredSurfaceExtensions)) {
        std::fprintf(stderr, "initializeContext failed: %s\n",
                     directVulkanBackend.lastError().toUtf8().constData());
        return 7;
    }
    if (directVulkanBackend.instance() == VK_NULL_HANDLE) {
        return 8;
    }
    if (!directVulkanBackend.initializeDevice()) {
        std::fprintf(stderr, "initializeDevice failed: %s\n",
                     directVulkanBackend.lastError().toUtf8().constData());
        return 9;
    }
    if (directVulkanBackend.device() == VK_NULL_HANDLE) {
        return 10;
    }
    if (directVulkanBackend.initializeSwapchain(VK_NULL_HANDLE, 1, 1)) {
        return 11;
    }

    VulkanSurface emptySurface;
    if (emptySurface.isValid()) {
        return 12;
    }
#endif

    return 0;
}
