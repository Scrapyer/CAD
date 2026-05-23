#include "VulkanFramePipelines.h"

#include "VulkanDevice.h"

void VulkanFramePipelines::destroy(const VulkanDevice& device)
{
    triangle.destroy(device);
    background.destroy(device);
    mesh.destroy(device);
    point.destroy(device);
    isoSurface.destroy(device);
    line.destroy(device);
    axes.destroy(device);
    pick.destroy(device);
}
