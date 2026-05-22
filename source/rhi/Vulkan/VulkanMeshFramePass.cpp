#include "VulkanMeshFramePass.h"

#include "VulkanBufferResource.h"
#include "VulkanDescriptorResource.h"
#include "VulkanPipelineResource.h"

#include <array>
#include <cstring>

namespace {
void pushMvpColorConstants(VkCommandBuffer commandBuffer,
                           VkPipelineLayout layout,
                           const QMatrix4x4& mvp,
                           const std::array<float, 4>& color)
{
    std::array<float, 20> pushConstants{};
    std::memcpy(pushConstants.data(), mvp.constData(), 16 * sizeof(float));
    pushConstants[16] = color[0];
    pushConstants[17] = color[1];
    pushConstants[18] = color[2];
    pushConstants[19] = color[3];
    vkCmdPushConstants(commandBuffer,
                       layout,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       static_cast<uint32_t>(pushConstants.size() * sizeof(float)),
                       pushConstants.data());
}

void recordIndexedSurfaceDraw(VkCommandBuffer commandBuffer,
                              const VulkanPipelineResource* pipeline,
                              const VulkanBufferResource* vertexResource,
                              const VulkanBufferResource* indexResource,
                              uint32_t indexCount,
                              const QMatrix4x4& mvp,
                              const std::array<float, 4>& color)
{
    if (pipeline == nullptr ||
        vertexResource == nullptr ||
        indexResource == nullptr ||
        !pipeline->isValid() ||
        !vertexResource->isValid() ||
        !indexResource->isValid() ||
        indexCount == 0) {
        return;
    }

    VkDeviceSize offsets[] = {0};
    VkBuffer vertexBuffer = vertexResource->buffer();
    VkBuffer indexBuffer = indexResource->buffer();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
    pushMvpColorConstants(commandBuffer, pipeline->layout(), mvp, color);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void recordLineDraw(VkCommandBuffer commandBuffer,
                    const VulkanPipelineResource* pipeline,
                    const VulkanBufferResource* vertexResource,
                    uint32_t vertexCount,
                    const QMatrix4x4& mvp,
                    const std::array<float, 4>& color)
{
    if (pipeline == nullptr ||
        vertexResource == nullptr ||
        !pipeline->isValid() ||
        !vertexResource->isValid() ||
        vertexCount == 0) {
        return;
    }

    VkDeviceSize offsets[] = {0};
    VkBuffer vertexBuffer = vertexResource->buffer();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
    pushMvpColorConstants(commandBuffer, pipeline->layout(), mvp, color);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
}

void recordIndexedLineDraw(VkCommandBuffer commandBuffer,
                           const VulkanPipelineResource* pipeline,
                           const VulkanBufferResource* vertexResource,
                           const VulkanBufferResource* indexResource,
                           uint32_t indexCount,
                           const QMatrix4x4& mvp,
                           const std::array<float, 4>& color)
{
    if (pipeline == nullptr ||
        vertexResource == nullptr ||
        indexResource == nullptr ||
        !pipeline->isValid() ||
        !vertexResource->isValid() ||
        !indexResource->isValid() ||
        indexCount == 0) {
        return;
    }

    VkDeviceSize offsets[] = {0};
    VkBuffer vertexBuffer = vertexResource->buffer();
    VkBuffer indexBuffer = indexResource->buffer();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
    pushMvpColorConstants(commandBuffer, pipeline->layout(), mvp, color);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}
} // namespace

void VulkanMeshFramePass::record(VkCommandBuffer commandBuffer,
                                 VkExtent2D extent,
                                 const QMatrix4x4& mvp,
                                 const Resources& resources)
{
    if (resources.meshPipeline == nullptr ||
        resources.meshVertexResource == nullptr ||
        resources.meshIndexResource == nullptr ||
        !resources.meshPipeline->isValid() ||
        !resources.meshVertexResource->isValid() ||
        !resources.meshIndexResource->isValid() ||
        resources.meshIndexCount == 0) {
        return;
    }

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
    VkBuffer meshVertexBuffer = resources.meshVertexResource->buffer();
    VkBuffer meshIndexBuffer = resources.meshIndexResource->buffer();

    const bool drawSurface =
        resources.displayMode == ModelDisplayMode::Solid ||
        resources.displayMode == ModelDisplayMode::SolidWireframe;
    const bool drawEdges =
        resources.displayMode == ModelDisplayMode::Wireframe ||
        resources.displayMode == ModelDisplayMode::SolidWireframe;
    const bool drawPoints = resources.displayMode == ModelDisplayMode::Points;

    if (drawSurface) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.meshPipeline->pipeline());
        std::array<float, 20> meshPushConstants{};
        std::memcpy(meshPushConstants.data(), mvp.constData(), 16 * sizeof(float));
        meshPushConstants[16] = resources.meshScalarMin;
        meshPushConstants[17] = resources.meshScalarMax;
        meshPushConstants[18] = static_cast<float>(resources.meshNumBands);
        meshPushConstants[19] = resources.meshUseVertexScalars ? 1.0f : 0.0f;
        vkCmdPushConstants(commandBuffer,
                           resources.meshPipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           static_cast<uint32_t>(meshPushConstants.size() * sizeof(float)),
                           meshPushConstants.data());
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &meshVertexBuffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, meshIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        if (resources.meshScalarDescriptor != nullptr && resources.meshScalarDescriptor->isValid()) {
            VkDescriptorSet meshDescriptorSet = resources.meshScalarDescriptor->descriptorSet();
            vkCmdBindDescriptorSets(commandBuffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    resources.meshPipeline->layout(),
                                    0,
                                    1,
                                    &meshDescriptorSet,
                                    0,
                                    nullptr);
        }
        vkCmdDrawIndexed(commandBuffer, resources.meshIndexCount, 1, 0, 0, 0);
    }

    if (drawPoints) {
        recordLineDraw(commandBuffer,
                       resources.pointPipeline,
                       resources.meshVertexResource,
                       resources.meshIndexCount,
                       mvp,
                       {{0.58f, 0.78f, 0.74f, 1.0f}});
    }

    recordIndexedSurfaceDraw(commandBuffer,
                             resources.isoSurfacePipeline,
                             resources.isoSurfaceVertexResource,
                             resources.isoSurfaceIndexResource,
                             resources.isoSurfaceIndexCount,
                             mvp,
                             {{0.2f, 0.8f, 0.4f, 0.75f}});
    recordIndexedSurfaceDraw(commandBuffer,
                             resources.isoSurfacePipeline,
                             resources.clipPreviewVertexResource,
                             resources.clipPreviewIndexResource,
                             resources.clipPreviewIndexCount,
                             mvp,
                             {{0.95f, 0.58f, 0.20f, 0.28f}});
    recordLineDraw(commandBuffer,
                   resources.linePipeline,
                   resources.overlayLineVertexResource,
                   resources.overlayLineVertexCount,
                   mvp,
                   {{0.5f, 0.5f, 0.5f, 0.35f}});
    if (drawEdges) {
        recordIndexedLineDraw(commandBuffer,
                              resources.linePipeline,
                              resources.edgeVertexResource,
                              resources.edgeIndexResource,
                              resources.edgeIndexCount,
                              mvp,
                              {{0.2f, 0.2f, 0.22f, 1.0f}});
    }
    recordLineDraw(commandBuffer,
                   resources.linePipeline,
                   resources.sliceLineVertexResource,
                   resources.sliceLineVertexCount,
                   mvp,
                   {{1.0f, 0.2f, 0.2f, 1.0f}});
    recordLineDraw(commandBuffer,
                   resources.linePipeline,
                   resources.clipPreviewLineVertexResource,
                   resources.clipPreviewLineVertexCount,
                   mvp,
                   {{0.95f, 0.58f, 0.20f, 0.8f}});
    recordLineDraw(commandBuffer,
                   resources.linePipeline,
                   resources.selectionLineVertexResource,
                   resources.selectionLineVertexCount,
                   mvp,
                   {{1.0f, 0.78f, 0.18f, 1.0f}});
}
