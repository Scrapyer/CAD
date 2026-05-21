#pragma once

#include <QMatrix4x4>
#include <QString>

#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanBufferResource;
class VulkanPipelineResource;

/**
 * @brief Vulkan 离屏拾取 pass 录制器。
 *
 * 只负责编码可见三角形到拾取 framebuffer，并在需要时把 1x1 像素复制到 readback
 * buffer；render pass、framebuffer、image 和 buffer 生命周期仍由外层资源对象管理。
 */
class VulkanPickPass {
public:
    struct Resources {
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkImage colorImage = VK_NULL_HANDLE;
        const VulkanPipelineResource* pipeline = nullptr;
        const VulkanBufferResource* meshVertexResource = nullptr;
        const VulkanBufferResource* meshIndexResource = nullptr;
        uint32_t meshIndexCount = 0;
    };

    static bool record(VkCommandBuffer commandBuffer,
                       VkExtent2D extent,
                       const QMatrix4x4& mvp,
                       const Resources& resources,
                       VkBuffer readbackBuffer,
                       uint32_t readbackX,
                       uint32_t readbackY,
                       QString& lastError);
};
