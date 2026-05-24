#include "VulkanMeshFramePass.h"

#include "VulkanBufferResource.h"
#include "VulkanDescriptorResource.h"
#include "VulkanPipelineResource.h"

#include <array>
#include <cstring>

namespace {
struct GpuDrivenMeshV2PushConstants {
    float mvp[16] = {};
    float contour[4] = {};
    uint32_t useVertexColor = 0;
    uint32_t partStateCount = 0;
    uint32_t pad0 = 0;
    uint32_t pad1 = 0;
};

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

void pushMvpColorContourConstants(VkCommandBuffer commandBuffer,
                                  VkPipelineLayout layout,
                                  const QMatrix4x4& mvp,
                                  const std::array<float, 4>& color,
                                  const std::array<float, 4>& contour)
{
    std::array<float, 24> pushConstants{};
    std::memcpy(pushConstants.data(), mvp.constData(), 16 * sizeof(float));
    for (size_t i = 0; i < color.size(); ++i) {
        pushConstants[16 + i] = color[i];
        pushConstants[20 + i] = contour[i];
    }
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
    pushMvpColorContourConstants(commandBuffer, pipeline->layout(), mvp, color, {{0.0f, 1.0f, 1.0f, 0.0f}});
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
}

void recordIndexedLineDraw(VkCommandBuffer commandBuffer,
                           const VulkanPipelineResource* pipeline,
                           const VulkanBufferResource* vertexResource,
                           const VulkanBufferResource* indexResource,
                           uint32_t indexCount,
                           const QMatrix4x4& mvp,
                           const std::array<float, 4>& color,
                           const std::array<float, 4>& contour)
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
    pushMvpColorContourConstants(commandBuffer, pipeline->layout(), mvp, color, contour);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void recordGpuDrivenIndexedLineDraw(VkCommandBuffer commandBuffer,
                                    const VulkanPipelineResource* pipeline,
                                    const VulkanBufferResource* vertexResource,
                                    const VulkanBufferResource* indexResource,
                                    const VulkanBufferResource* indirectCommandResource,
                                    const QMatrix4x4& mvp,
                                    const std::array<float, 4>& color,
                                    const std::array<float, 4>& contour)
{
    if (pipeline == nullptr ||
        vertexResource == nullptr ||
        indexResource == nullptr ||
        indirectCommandResource == nullptr ||
        !pipeline->isValid() ||
        !vertexResource->isValid() ||
        !indexResource->isValid() ||
        !indirectCommandResource->isValid()) {
        return;
    }

    VkDeviceSize offsets[] = {0};
    VkBuffer vertexBuffer = vertexResource->buffer();
    VkBuffer indexBuffer = indexResource->buffer();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
    pushMvpColorContourConstants(commandBuffer, pipeline->layout(), mvp, color, contour);
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer, offsets);
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer,
                             indirectCommandResource->buffer(),
                             0,
                             1,
                             sizeof(VkDrawIndexedIndirectCommand));
}

void recordGpuDrivenSurfaceV2Draw(VkCommandBuffer commandBuffer,
                                  const VulkanPipelineResource* pipeline,
                                  const VulkanDescriptorResource* descriptor,
                                  const VulkanBufferResource* indexResource,
                                  const VulkanBufferResource* indirectCommandResource,
                                  const QMatrix4x4& mvp,
                                  bool useVertexColor,
                                  uint32_t partStateCount,
                                  const std::array<float, 4>& contour)
{
    if (pipeline == nullptr ||
        descriptor == nullptr ||
        indexResource == nullptr ||
        indirectCommandResource == nullptr ||
        !pipeline->isValid() ||
        !descriptor->isValid() ||
        !indexResource->isValid() ||
        !indirectCommandResource->isValid()) {
        return;
    }

    GpuDrivenMeshV2PushConstants pushConstants{};
    std::memcpy(pushConstants.mvp, mvp.constData(), sizeof(pushConstants.mvp));
    for (size_t i = 0; i < contour.size(); ++i) {
        pushConstants.contour[i] = contour[i];
    }
    pushConstants.useVertexColor = useVertexColor ? 1u : 0u;
    pushConstants.partStateCount = partStateCount;

    VkDescriptorSet descriptorSet = descriptor->descriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
    vkCmdPushConstants(commandBuffer,
                       pipeline->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       sizeof(pushConstants),
                       &pushConstants);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->layout(),
                            0,
                            1,
                            &descriptorSet,
                            0,
                            nullptr);
    vkCmdBindIndexBuffer(commandBuffer, indexResource->buffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer,
                             indirectCommandResource->buffer(),
                             0,
                             1,
                             sizeof(VkDrawIndexedIndirectCommand));
}

void recordGpuDrivenPointV2Draw(VkCommandBuffer commandBuffer,
                                const VulkanPipelineResource* pipeline,
                                const VulkanDescriptorResource* descriptor,
                                const VulkanBufferResource* indexResource,
                                const VulkanBufferResource* indirectCommandResource,
                                const QMatrix4x4& mvp,
                                const std::array<float, 4>& color,
                                const std::array<float, 4>& contour)
{
    if (pipeline == nullptr ||
        descriptor == nullptr ||
        indexResource == nullptr ||
        indirectCommandResource == nullptr ||
        !pipeline->isValid() ||
        !descriptor->isValid() ||
        !indexResource->isValid() ||
        !indirectCommandResource->isValid()) {
        return;
    }

    VkDescriptorSet descriptorSet = descriptor->descriptorSet();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->pipeline());
    pushMvpColorContourConstants(commandBuffer, pipeline->layout(), mvp, color, contour);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->layout(),
                            0,
                            1,
                            &descriptorSet,
                            0,
                            nullptr);
    vkCmdBindIndexBuffer(commandBuffer, indexResource->buffer(), 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexedIndirect(commandBuffer,
                             indirectCommandResource->buffer(),
                             0,
                             1,
                             sizeof(VkDrawIndexedIndirectCommand));
}
} // namespace

void VulkanMeshFramePass::record(VkCommandBuffer commandBuffer,
                                 VkExtent2D extent,
                                 const QMatrix4x4& mvp,
                                 const Resources& resources)
{
    if (commandBuffer == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0) {
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
    VkBuffer meshVertexBuffer = resources.meshVertexResource && resources.meshVertexResource->isValid()
        ? resources.meshVertexResource->buffer()
        : VK_NULL_HANDLE;
    VkBuffer meshIndexBuffer = resources.meshIndexResource && resources.meshIndexResource->isValid()
        ? resources.meshIndexResource->buffer()
        : VK_NULL_HANDLE;
    VkBuffer gpuDrivenVertexBuffer =
        resources.gpuDrivenVertexResource && resources.gpuDrivenVertexResource->isValid()
        ? resources.gpuDrivenVertexResource->buffer()
        : VK_NULL_HANDLE;
    VkBuffer gpuDrivenVisibleIndexBuffer =
        resources.gpuDrivenVisibleIndexResource && resources.gpuDrivenVisibleIndexResource->isValid()
        ? resources.gpuDrivenVisibleIndexResource->buffer()
        : VK_NULL_HANDLE;
    VkBuffer gpuDrivenIndirectCommandBuffer =
        resources.gpuDrivenIndirectCommandResource && resources.gpuDrivenIndirectCommandResource->isValid()
        ? resources.gpuDrivenIndirectCommandResource->buffer()
        : VK_NULL_HANDLE;
    VkBuffer gpuDrivenVisiblePointIndexBuffer =
        resources.gpuDrivenVisiblePointIndexResource && resources.gpuDrivenVisiblePointIndexResource->isValid()
        ? resources.gpuDrivenVisiblePointIndexResource->buffer()
        : VK_NULL_HANDLE;
    VkBuffer gpuDrivenPointIndirectCommandBuffer =
        resources.gpuDrivenPointIndirectCommandResource && resources.gpuDrivenPointIndirectCommandResource->isValid()
        ? resources.gpuDrivenPointIndirectCommandResource->buffer()
        : VK_NULL_HANDLE;

    const bool drawSurface =
        resources.displayMode == ModelDisplayMode::Solid ||
        resources.displayMode == ModelDisplayMode::SolidWireframe;
    const bool drawEdges =
        resources.displayMode == ModelDisplayMode::Wireframe ||
        resources.displayMode == ModelDisplayMode::SolidWireframe;
    const bool drawPoints = resources.displayMode == ModelDisplayMode::Points;
    const bool canDrawGpuDrivenSurfaceV2 =
        resources.useGpuDrivenSurfaceV2 &&
        resources.gpuDrivenMeshPipelineV2 != nullptr &&
        resources.gpuDrivenSurfaceDescriptorV2 != nullptr &&
        resources.gpuDrivenMeshPipelineV2->isValid() &&
        resources.gpuDrivenSurfaceDescriptorV2->isValid() &&
        gpuDrivenVisibleIndexBuffer != VK_NULL_HANDLE &&
        gpuDrivenIndirectCommandBuffer != VK_NULL_HANDLE;
    const bool canDrawGpuDrivenPointV2 =
        resources.useGpuDrivenSurfaceV2 &&
        resources.gpuDrivenPointPipelineV2 != nullptr &&
        resources.gpuDrivenSurfaceDescriptorV2 != nullptr &&
        resources.gpuDrivenPointPipelineV2->isValid() &&
        resources.gpuDrivenSurfaceDescriptorV2->isValid() &&
        gpuDrivenVisiblePointIndexBuffer != VK_NULL_HANDLE &&
        gpuDrivenPointIndirectCommandBuffer != VK_NULL_HANDLE;

    if (drawSurface &&
        canDrawGpuDrivenSurfaceV2) {
        const std::array<float, 4> meshContour = {{
            resources.meshScalarMin,
            resources.meshScalarMax,
            static_cast<float>(resources.meshNumBands),
            resources.meshUseVertexScalars ? 1.0f : 0.0f
        }};
        recordGpuDrivenSurfaceV2Draw(commandBuffer,
                                     resources.gpuDrivenMeshPipelineV2,
                                     resources.gpuDrivenSurfaceDescriptorV2,
                                     resources.gpuDrivenVisibleIndexResource,
                                     resources.gpuDrivenIndirectCommandResource,
                                     mvp,
                                     resources.gpuDrivenUseVertexColor,
                                     resources.gpuDrivenPartStateCount,
                                     meshContour);
    } else if (drawSurface &&
        resources.useGpuDrivenIndirect &&
        resources.meshPipeline != nullptr &&
        resources.meshPipeline->isValid() &&
        gpuDrivenVertexBuffer != VK_NULL_HANDLE &&
        gpuDrivenVisibleIndexBuffer != VK_NULL_HANDLE &&
        gpuDrivenIndirectCommandBuffer != VK_NULL_HANDLE) {
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
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &gpuDrivenVertexBuffer, offsets);
        vkCmdBindIndexBuffer(commandBuffer, gpuDrivenVisibleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
        const VulkanDescriptorResource* scalarDescriptor =
            resources.gpuDrivenScalarDescriptor != nullptr && resources.gpuDrivenScalarDescriptor->isValid()
            ? resources.gpuDrivenScalarDescriptor
            : resources.meshScalarDescriptor;
        if (scalarDescriptor != nullptr && scalarDescriptor->isValid()) {
            VkDescriptorSet meshDescriptorSet = scalarDescriptor->descriptorSet();
            vkCmdBindDescriptorSets(commandBuffer,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    resources.meshPipeline->layout(),
                                    0,
                                    1,
                                    &meshDescriptorSet,
                                    0,
                                    nullptr);
        }
        vkCmdDrawIndexedIndirect(commandBuffer,
                                 gpuDrivenIndirectCommandBuffer,
                                 0,
                                 1,
                                 sizeof(VkDrawIndexedIndirectCommand));
    } else if (drawSurface &&
        resources.meshPipeline != nullptr &&
        resources.meshPipeline->isValid() &&
        meshVertexBuffer != VK_NULL_HANDLE &&
        meshIndexBuffer != VK_NULL_HANDLE &&
        resources.meshIndexCount > 0) {
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
        if (canDrawGpuDrivenPointV2) {
            recordGpuDrivenPointV2Draw(commandBuffer,
                                       resources.gpuDrivenPointPipelineV2,
                                       resources.gpuDrivenSurfaceDescriptorV2,
                                       resources.gpuDrivenVisiblePointIndexResource,
                                       resources.gpuDrivenPointIndirectCommandResource,
                                       mvp,
                                       {{0.58f, 0.78f, 0.74f, 1.0f}},
                                       {{resources.meshScalarMin,
                                         resources.meshScalarMax,
                                         static_cast<float>(resources.meshNumBands),
                                         resources.meshUseVertexScalars ? 1.0f : 0.0f}});
        } else if (resources.useGpuDrivenIndirect &&
            resources.pointPipeline != nullptr &&
            resources.pointPipeline->isValid() &&
            gpuDrivenVertexBuffer != VK_NULL_HANDLE &&
            gpuDrivenVisibleIndexBuffer != VK_NULL_HANDLE &&
            gpuDrivenIndirectCommandBuffer != VK_NULL_HANDLE) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, resources.pointPipeline->pipeline());
            pushMvpColorContourConstants(commandBuffer,
                                         resources.pointPipeline->layout(),
                                         mvp,
                                         {{0.58f, 0.78f, 0.74f, 1.0f}},
                                         {{0.0f, 1.0f, 1.0f, 0.0f}});
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &gpuDrivenVertexBuffer, offsets);
            vkCmdBindIndexBuffer(commandBuffer, gpuDrivenVisibleIndexBuffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexedIndirect(commandBuffer,
                                     gpuDrivenIndirectCommandBuffer,
                                     0,
                                     1,
                                     sizeof(VkDrawIndexedIndirectCommand));
        } else {
            recordLineDraw(commandBuffer,
                           resources.pointPipeline,
                           resources.pointVertexResource,
                           resources.pointVertexCount,
                           mvp,
                           {{0.58f, 0.78f, 0.74f, 1.0f}});
        }
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
        const std::array<float, 4> edgeContour = {{
            resources.edgeScalarMin,
            resources.edgeScalarMax,
            static_cast<float>(resources.edgeNumBands),
            resources.edgeUseVertexScalars ? 1.0f : 0.0f
        }};
        if (resources.useGpuDrivenEdges) {
            recordGpuDrivenIndexedLineDraw(commandBuffer,
                                           resources.linePipeline,
                                           resources.gpuDrivenEdgeVertexResource,
                                           resources.gpuDrivenVisibleEdgeIndexResource,
                                           resources.gpuDrivenEdgeIndirectCommandResource,
                                           mvp,
                                           {{0.2f, 0.2f, 0.22f, 1.0f}},
                                           edgeContour);
        } else {
            recordIndexedLineDraw(commandBuffer,
                                  resources.linePipeline,
                                  resources.edgeVertexResource,
                                  resources.edgeIndexResource,
                                  resources.edgeIndexCount,
                                  mvp,
                                  {{0.2f, 0.2f, 0.22f, 1.0f}},
                                  edgeContour);
        }
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
