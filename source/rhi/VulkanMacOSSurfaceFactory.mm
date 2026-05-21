#include "VulkanMacOSSurfaceFactory.h"

#include "VulkanContext.h"

#include <QWindow>

#include <vulkan/vulkan_metal.h>

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

std::vector<const char*> VulkanMacOSSurfaceFactory::requiredInstanceExtensions()
{
    return {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_EXT_METAL_SURFACE_EXTENSION_NAME
    };
}

VulkanSurface VulkanMacOSSurfaceFactory::createSurface(VkInstance instance, QWindow* window, QString* error)
{
    if (error) {
        error->clear();
    }
    if (instance == VK_NULL_HANDLE) {
        if (error) {
            *error = QStringLiteral("Vulkan instance is null");
        }
        return {};
    }
    if (!window) {
        if (error) {
            *error = QStringLiteral("QWindow is null");
        }
        return {};
    }

    if (!window->handle()) {
        window->create();
    }

    WId nativeId = window->winId();
    if (!nativeId) {
        if (error) {
            *error = QStringLiteral("QWindow has no native view");
        }
        return {};
    }

    NSView* nativeView = reinterpret_cast<NSView*>(nativeId);
    if (![nativeView isKindOfClass:[NSView class]]) {
        if (error) {
            *error = QStringLiteral("QWindow native handle is not an NSView");
        }
        return {};
    }

    [nativeView setWantsLayer:YES];
    CALayer* layer = [nativeView layer];
    if (![layer isKindOfClass:[CAMetalLayer class]]) {
        CAMetalLayer* metalLayer = [CAMetalLayer layer];
        metalLayer.frame = nativeView.bounds;
        NSScreen* screen = nativeView.window.screen ?: [NSScreen mainScreen];
        metalLayer.contentsScale = screen ? screen.backingScaleFactor : 1.0;
        [nativeView setLayer:metalLayer];
        layer = metalLayer;
    }

    CAMetalLayer* metalLayer = static_cast<CAMetalLayer*>(layer);
    VkMetalSurfaceCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
    createInfo.pLayer = metalLayer;

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    PFN_vkCreateMetalSurfaceEXT createMetalSurface =
        reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
            vkGetInstanceProcAddr(instance, "vkCreateMetalSurfaceEXT"));
    if (!createMetalSurface) {
        if (error) {
            *error = QStringLiteral("vkCreateMetalSurfaceEXT is unavailable");
        }
        return {};
    }

    VkResult result = createMetalSurface(instance, &createInfo, nullptr, &surface);
    if (result != VK_SUCCESS) {
        if (error) {
            *error = QStringLiteral("vkCreateMetalSurfaceEXT failed: ") +
                VulkanContext::formatResult(result);
        }
        return {};
    }

    return VulkanSurface(instance, surface);
}
