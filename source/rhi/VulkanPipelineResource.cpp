#include "VulkanPipelineResource.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"

bool VulkanPipelineResource::createGraphics(const VulkanDevice& device,
                                            const VkPipelineLayoutCreateInfo& layoutInfo,
                                            const VkGraphicsPipelineCreateInfo& pipelineInfo,
                                            const char* debugName,
                                            QString& lastError)
{
    destroy(device);

    VkResult result = vkCreatePipelineLayout(device.device(), &layoutInfo, nullptr, &layout_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreatePipelineLayout(%1) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        layout_ = VK_NULL_HANDLE;
        return false;
    }

    VkGraphicsPipelineCreateInfo createInfo = pipelineInfo;
    createInfo.layout = layout_;
    result = vkCreateGraphicsPipelines(
        device.device(), VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline_);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkCreateGraphicsPipelines(%1) failed: %2")
            .arg(QString::fromUtf8(debugName))
            .arg(VulkanContext::formatResult(result));
        destroy(device);
        return false;
    }

    return true;
}

void VulkanPipelineResource::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        layout_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        return;
    }

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(vkDevice, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }
    if (layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vkDevice, layout_, nullptr);
        layout_ = VK_NULL_HANDLE;
    }
}
