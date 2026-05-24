#include "VulkanPickPass.h"

#include "VulkanBufferResource.h"
#include "VulkanContext.h"
#include "VulkanDescriptorResource.h"
#include "VulkanPipelineResource.h"

#include <array>

bool VulkanPickPass::record(VkCommandBuffer commandBuffer,
                            VkExtent2D extent,
                            const QMatrix4x4& mvp,
                            const Resources& resources,
                            VkBuffer readbackBuffer,
                            uint32_t readbackX,
                            uint32_t readbackY,
                            const std::function<void(VkCommandBuffer)>& beforeRenderPass,
                            QString& lastError)
{
    const bool useGpuDrivenV2 =
        resources.useGpuDrivenSurfaceV2 &&
        resources.gpuDrivenPipelineV2 != nullptr &&
        resources.gpuDrivenSurfaceDescriptorV2 != nullptr &&
        resources.gpuDrivenVisibleIndexResource != nullptr &&
        resources.gpuDrivenIndirectCommandResource != nullptr &&
        resources.gpuDrivenPipelineV2->isValid() &&
        resources.gpuDrivenSurfaceDescriptorV2->isValid() &&
        resources.gpuDrivenVisibleIndexResource->isValid() &&
        resources.gpuDrivenIndirectCommandResource->isValid();
    const bool useGpuDrivenIndirect =
        !useGpuDrivenV2 &&
        resources.useGpuDrivenIndirect &&
        resources.gpuDrivenVertexResource != nullptr &&
        resources.gpuDrivenVisibleIndexResource != nullptr &&
        resources.gpuDrivenIndirectCommandResource != nullptr &&
        resources.gpuDrivenVertexResource->isValid() &&
        resources.gpuDrivenVisibleIndexResource->isValid() &&
        resources.gpuDrivenIndirectCommandResource->isValid();
    const bool useTraditional =
        resources.meshVertexResource != nullptr &&
        resources.meshIndexResource != nullptr &&
        resources.meshVertexResource->isValid() &&
        resources.meshIndexResource->isValid() &&
        resources.meshIndexCount > 0;
    if (resources.renderPass == VK_NULL_HANDLE ||
        resources.framebuffer == VK_NULL_HANDLE ||
        resources.colorImage == VK_NULL_HANDLE ||
        ((!useGpuDrivenV2 &&
          (resources.pipeline == nullptr || !resources.pipeline->isValid())) ||
         (useGpuDrivenV2 &&
          (resources.gpuDrivenPipelineV2 == nullptr || !resources.gpuDrivenPipelineV2->isValid()))) ||
        (!useGpuDrivenV2 && !useGpuDrivenIndirect && !useTraditional)) {
        lastError = QStringLiteral("Vulkan pick pass resources are not initialized");
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkBeginCommandBuffer(pick) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    if (beforeRenderPass) {
        beforeRenderPass(commandBuffer);
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = resources.renderPass;
    renderPassInfo.framebuffer = resources.framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;

    VkDeviceSize offsets[] = {0};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    const VulkanPipelineResource* activePipeline =
        useGpuDrivenV2 ? resources.gpuDrivenPipelineV2 : resources.pipeline;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline->pipeline());
    vkCmdPushConstants(commandBuffer,
                       activePipeline->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT,
                       0,
                       16 * sizeof(float),
                       mvp.constData());
    if (useGpuDrivenV2) {
        VkDescriptorSet descriptorSet = resources.gpuDrivenSurfaceDescriptorV2->descriptorSet();
        VkBuffer meshIndexBuffer = resources.gpuDrivenVisibleIndexResource->buffer();
        vkCmdBindDescriptorSets(commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                activePipeline->layout(),
                                0,
                                1,
                                &descriptorSet,
                                0,
                                nullptr);
        vkCmdBindIndexBuffer(commandBuffer, meshIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commandBuffer,
                                 resources.gpuDrivenIndirectCommandResource->buffer(),
                                 0,
                                 1,
                                 sizeof(VkDrawIndexedIndirectCommand));
    } else if (useGpuDrivenIndirect) {
        VkBuffer meshVertexBuffer = resources.gpuDrivenVertexResource->buffer();
        VkBuffer meshIndexBuffer = resources.gpuDrivenVisibleIndexResource->buffer();
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &meshVertexBuffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, meshIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexedIndirect(commandBuffer,
                                 resources.gpuDrivenIndirectCommandResource->buffer(),
                                 0,
                                 1,
                                 sizeof(VkDrawIndexedIndirectCommand));
    } else {
        VkBuffer meshVertexBuffer = resources.meshVertexResource->buffer();
        VkBuffer meshIndexBuffer = resources.meshIndexResource->buffer();
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &meshVertexBuffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, meshIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, resources.meshIndexCount, 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (readbackBuffer != VK_NULL_HANDLE) {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = resources.colorImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(commandBuffer,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = 0;
        copyRegion.bufferImageHeight = 0;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0;
        copyRegion.imageSubresource.baseArrayLayer = 0;
        copyRegion.imageSubresource.layerCount = 1;
        copyRegion.imageOffset = {
            static_cast<int32_t>(readbackX),
            static_cast<int32_t>(readbackY),
            0
        };
        copyRegion.imageExtent = {1, 1, 1};

        vkCmdCopyImageToBuffer(commandBuffer,
                               resources.colorImage,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readbackBuffer,
                               1,
                               &copyRegion);
    }

    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        lastError = QStringLiteral("vkEndCommandBuffer(pick) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    return true;
}
