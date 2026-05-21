#include "VulkanSurface.h"

VulkanSurface::VulkanSurface(VkInstance instance, VkSurfaceKHR surface, bool ownsSurface)
    : instance_(instance),
      surface_(surface),
      ownsSurface_(ownsSurface)
{
}

VulkanSurface::~VulkanSurface()
{
    reset();
}

VulkanSurface::VulkanSurface(VulkanSurface&& other) noexcept
    : instance_(other.instance_),
      surface_(other.surface_),
      ownsSurface_(other.ownsSurface_)
{
    other.instance_ = VK_NULL_HANDLE;
    other.surface_ = VK_NULL_HANDLE;
    other.ownsSurface_ = true;
}

VulkanSurface& VulkanSurface::operator=(VulkanSurface&& other) noexcept
{
    if (this == &other) {
        return *this;
    }

    reset();
    instance_ = other.instance_;
    surface_ = other.surface_;
    ownsSurface_ = other.ownsSurface_;
    other.instance_ = VK_NULL_HANDLE;
    other.surface_ = VK_NULL_HANDLE;
    other.ownsSurface_ = true;
    return *this;
}

void VulkanSurface::reset(VkInstance instance, VkSurfaceKHR surface, bool ownsSurface)
{
    if (surface_ != VK_NULL_HANDLE && ownsSurface_ && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }

    instance_ = instance;
    surface_ = surface;
    ownsSurface_ = ownsSurface;
}

VkSurfaceKHR VulkanSurface::release()
{
    VkSurfaceKHR released = surface_;
    surface_ = VK_NULL_HANDLE;
    ownsSurface_ = true;
    return released;
}
