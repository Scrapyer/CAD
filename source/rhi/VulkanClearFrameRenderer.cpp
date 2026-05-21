#include "VulkanClearFrameRenderer.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"
#include "VulkanMeshFramePass.h"
#include "VulkanPickPass.h"
#include "VulkanRenderBackend.h"
#include "VulkanSwapchain.h"
#include "Geometry.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

namespace {
struct VulkanMeshVertex {
    float position[3];
    float normal[3];
    float color[3];
    float pickColor[3];
};

bool isPartVisible(const VulkanMeshUploadOptions& options, int part)
{
    const auto it = options.partVisibility.find(part);
    return it == options.partVisibility.end() || it->second;
}

QVector3D triangleColor(const VulkanMeshUploadOptions& options, int part)
{
    if (part >= 0 && part < static_cast<int>(options.partColors.size())) {
        return options.partColors[static_cast<size_t>(part)];
    }
    return options.objectColor;
}

QVector3D vertexColor(const VulkanMeshUploadOptions& options, size_t sourceIndex, int part)
{
    if (options.useVertexColor && sourceIndex * 3 + 2 < options.vertexColors.size()) {
        return QVector3D(options.vertexColors[sourceIndex * 3 + 0],
                         options.vertexColors[sourceIndex * 3 + 1],
                         options.vertexColors[sourceIndex * 3 + 2]);
    }
    return triangleColor(options, part);
}

QVector3D idToPickColor(int id)
{
    if (id < 0) {
        return QVector3D(0.0f, 0.0f, 0.0f);
    }
    const int encoded = id + 1;
    const int r = encoded & 0xFF;
    const int g = (encoded >> 8) & 0xFF;
    const int b = (encoded >> 16) & 0xFF;
    return QVector3D(
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f);
}

} // namespace

VulkanClearFrameRenderer::~VulkanClearFrameRenderer() = default;

bool VulkanClearFrameRenderer::initialize(const VulkanDevice& device, const VulkanSwapchain& swapchain)
{
    lastError_.clear();
    if (!device.isInitialized() || !swapchain.isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device/swapchain is not initialized");
        return false;
    }

    destroy(device);

    return createImageViews(device, swapchain) &&
           createRenderPass(device, swapchain.imageFormat()) &&
           createBackgroundGraphicsPipeline(device) &&
           createGraphicsPipeline(device) &&
           createMeshGraphicsPipeline(device) &&
           createIsoSurfaceGraphicsPipeline(device) &&
           createLineGraphicsPipeline(device) &&
           createAxesIndicatorResource(device) &&
           createPickRenderPass(device) &&
           createPickGraphicsPipeline(device) &&
           createDepthResources(device, swapchain) &&
           createFramebuffers(device, swapchain) &&
           createCommandPool(device) &&
           createSyncObjects(device);
}

void VulkanClearFrameRenderer::destroy(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }

    if (inFlightFence_ != VK_NULL_HANDLE) {
        vkDestroyFence(vkDevice, inFlightFence_, nullptr);
        inFlightFence_ = VK_NULL_HANDLE;
    }
    if (renderFinishedSemaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(vkDevice, renderFinishedSemaphore_, nullptr);
        renderFinishedSemaphore_ = VK_NULL_HANDLE;
    }
    if (imageAvailableSemaphore_ != VK_NULL_HANDLE) {
        vkDestroySemaphore(vkDevice, imageAvailableSemaphore_, nullptr);
        imageAvailableSemaphore_ = VK_NULL_HANDLE;
    }
    commandResource_.destroy(device);
    destroyPickResources(device);
    swapchainFramebuffers_.destroy(device);
    if (depthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vkDevice, depthImageView_, nullptr);
        depthImageView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(vkDevice, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthImageMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, depthImageMemory_, nullptr);
        depthImageMemory_ = VK_NULL_HANDLE;
    }
    depthFormat_ = VK_FORMAT_UNDEFINED;
    trianglePipeline_.destroy(device);
    backgroundPipeline_.destroy(device);
    meshPipeline_.destroy(device);
    isoSurfacePipeline_.destroy(device);
    linePipeline_.destroy(device);
    pickPipeline_.destroy(device);
    meshScalarDescriptor_.destroy(device);
    meshScalarSetLayout_.destroy(device);
    destroyMeshBuffers(device);
    destroyIsoSurfaceBuffers(device);
    destroyClipPreviewBuffers(device);
    overlayLineVertexResource_.destroy(device);
    overlayLineVertexCount_ = 0;
    sliceLineVertexResource_.destroy(device);
    sliceLineVertexCount_ = 0;
    axesLineVertexResource_.destroy(device);
    axesLineVertexCount_ = 0;
    pickRenderPass_.destroy(device);
    renderPass_.destroy(device);
    for (VkImageView imageView : imageViews_) {
        vkDestroyImageView(vkDevice, imageView, nullptr);
    }
    imageViews_.clear();
}

bool VulkanClearFrameRenderer::renderClearFrame(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const VkClearColorValue& clearColor)
{
    lastError_.clear();
    swapchainOutOfDate_ = false;
    if (!isInitialized()) {
        lastError_ = QStringLiteral("VulkanClearFrameRenderer is not initialized");
        return false;
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    if (!acquireSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }

    if (imageIndex >= swapchainFramebuffers_.count()) {
        lastError_ = QStringLiteral("Swapchain image index is out of range");
        return false;
    }

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    if (!recordCommandBuffer(
            commandResource_.buffer(), swapchainFramebuffers_.framebuffer(imageIndex), swapchain.extent(), clearColor, false)) {
        return false;
    }
    vkResetFences(vkDevice, 1, &inFlightFence_);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandResource_.bufferAddress();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore_;

    VkResult result = vkQueueSubmit(device.graphicsQueue(), 1, &submitInfo, inFlightFence_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkQueueSubmit failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    if (!presentSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }

    return true;
}

bool VulkanClearFrameRenderer::renderTriangleFrame(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const VkClearColorValue& clearColor,
    const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    swapchainOutOfDate_ = false;
    if (!isInitialized() || trianglePipeline_.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan triangle pipeline is not initialized");
        return false;
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    if (!acquireSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }
    if (imageIndex >= swapchainFramebuffers_.count()) {
        lastError_ = QStringLiteral("Swapchain image index is out of range");
        return false;
    }

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    if (!recordCommandBuffer(commandResource_.buffer(),
                             swapchainFramebuffers_.framebuffer(imageIndex),
                             swapchain.extent(),
                             clearColor,
                             true,
                             axesMvp)) {
        return false;
    }
    vkResetFences(vkDevice, 1, &inFlightFence_);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandResource_.bufferAddress();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore_;

    VkResult result = vkQueueSubmit(device.graphicsQueue(), 1, &submitInfo, inFlightFence_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkQueueSubmit failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    if (!presentSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }

    return true;
}

bool VulkanClearFrameRenderer::uploadMesh(
    const VulkanDevice& device,
    const Mesh& mesh,
    const VulkanMeshUploadOptions& options)
{
    lastError_.clear();
    vkDeviceWaitIdle(device.device());
    destroyMeshBuffers(device);

    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    meshUseVertexScalars_ = options.useVertexColor && !options.vertexScalars.empty();
    meshScalarMin_ = options.scalarMin;
    meshScalarMax_ = options.scalarMax;
    meshNumBands_ = std::max(1, options.numBands);

    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    const size_t triangleCount = mesh.indices.size() / 3;
    std::vector<VulkanMeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<float> expandedScalars;
    meshScalarSourceIndices_.clear();
    vertices.reserve(triangleCount * 3);
    indices.reserve(triangleCount * 3);
    expandedScalars.reserve(triangleCount * 3);
    meshScalarSourceIndices_.reserve(triangleCount * 3);

    for (size_t tri = 0; tri < triangleCount; ++tri) {
        const int part = tri < options.triangleToPart.size()
            ? options.triangleToPart[tri]
            : -1;
        if (!isPartVisible(options, part)) {
            continue;
        }

        const int elementId = tri < options.triangleToElement.size()
            ? options.triangleToElement[tri]
            : static_cast<int>(tri);
        const QVector3D pickColor = idToPickColor(elementId);
        for (size_t corner = 0; corner < 3; ++corner) {
            const uint32_t sourceIndex = mesh.indices[tri * 3 + corner];
            if (sourceIndex >= sourceVertexCount) {
                continue;
            }
            const size_t base = static_cast<size_t>(sourceIndex) * 6;
            VulkanMeshVertex vertex{};
            vertex.position[0] = mesh.vertices[base + 0];
            vertex.position[1] = mesh.vertices[base + 1];
            vertex.position[2] = mesh.vertices[base + 2];
            vertex.normal[0] = mesh.vertices[base + 3];
            vertex.normal[1] = mesh.vertices[base + 4];
            vertex.normal[2] = mesh.vertices[base + 5];
            const QVector3D color = vertexColor(options, sourceIndex, part);
            vertex.color[0] = color.x();
            vertex.color[1] = color.y();
            vertex.color[2] = color.z();
            vertex.pickColor[0] = pickColor.x();
            vertex.pickColor[1] = pickColor.y();
            vertex.pickColor[2] = pickColor.z();
            indices.push_back(static_cast<uint32_t>(vertices.size()));
            vertices.push_back(vertex);
            expandedScalars.push_back(sourceIndex < options.vertexScalars.size()
                ? options.vertexScalars[sourceIndex]
                : 0.0f);
            meshScalarSourceIndices_.push_back(sourceIndex);
        }
    }

    if (vertices.empty() || indices.empty()) {
        return true;
    }

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanMeshVertex));
    if (!meshVertexResource_.uploadDeviceLocal(device,
                                               vertices.data(),
                                               vertexSize,
                                               VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                               commandResource_.pool(),
                                               device.graphicsQueue(),
                                               "mesh vertex",
                                               lastError_)) {
        return false;
    }

    const VkDeviceSize indexSize = static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));
    if (!meshIndexResource_.uploadDeviceLocal(device,
                                              indices.data(),
                                              indexSize,
                                              VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                              commandResource_.pool(),
                                              device.graphicsQueue(),
                                              "mesh index",
                                              lastError_)) {
        return false;
    }

    meshIndexCount_ = static_cast<uint32_t>(indices.size());
    meshScalarCount_ = static_cast<uint32_t>(expandedScalars.size());

    const VkDeviceSize scalarSize = static_cast<VkDeviceSize>(
        std::max<size_t>(expandedScalars.size(), 1) * sizeof(float));
    const void* scalarData = expandedScalars.empty()
        ? static_cast<const void*>(nullptr)
        : static_cast<const void*>(expandedScalars.data());
    float zeroScalar = 0.0f;
    if (expandedScalars.empty()) {
        scalarData = &zeroScalar;
    }
    if (!meshScalarResource_.uploadHostVisible(device,
                                               scalarData,
                                               scalarSize,
                                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                               "mesh scalar storage",
                                               lastError_)) {
        destroyMeshBuffers(device);
        return false;
    }
    if (!createMeshScalarDescriptor(device)) {
        destroyMeshBuffers(device);
        return false;
    }

    if (!mesh.edgeVertices.empty() && !mesh.edgeIndices.empty() && mesh.edgeVertices.size() % 3 == 0) {
        const VkDeviceSize edgeVertexSize =
            static_cast<VkDeviceSize>(mesh.edgeVertices.size() * sizeof(float));
        if (!edgeVertexResource_.uploadDeviceLocal(device,
                                                   mesh.edgeVertices.data(),
                                                   edgeVertexSize,
                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                   commandResource_.pool(),
                                                   device.graphicsQueue(),
                                                   "edge vertex",
                                                   lastError_)) {
            destroyMeshBuffers(device);
            return false;
        }

        std::vector<uint32_t> edgeIndices;
        edgeIndices.reserve(mesh.edgeIndices.size());
        const size_t edgeCount = mesh.edgeIndices.size() / 2;
        for (size_t edge = 0; edge < edgeCount; ++edge) {
            const int part = edge < options.edgeToPart.size()
                ? options.edgeToPart[edge]
                : -1;
            if (!isPartVisible(options, part)) {
                continue;
            }
            edgeIndices.push_back(mesh.edgeIndices[edge * 2]);
            edgeIndices.push_back(mesh.edgeIndices[edge * 2 + 1]);
        }
        if (edgeIndices.empty()) {
            return true;
        }
        const VkDeviceSize edgeIndexSize =
            static_cast<VkDeviceSize>(edgeIndices.size() * sizeof(uint32_t));
        if (!edgeIndexResource_.uploadDeviceLocal(device,
                                                  edgeIndices.data(),
                                                  edgeIndexSize,
                                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                  commandResource_.pool(),
                                                  device.graphicsQueue(),
                                                  "edge index",
                                                  lastError_)) {
            destroyMeshBuffers(device);
            return false;
        }
        edgeIndexCount_ = static_cast<uint32_t>(edgeIndices.size());
    }

    return true;
}

bool VulkanClearFrameRenderer::uploadVertexScalars(
    const VulkanDevice& device,
    const std::vector<float>& scalars,
    float minVal,
    float maxVal,
    int numBands,
    bool useScalars)
{
    lastError_.clear();
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!meshScalarResource_.isValid() || meshScalarSourceIndices_.empty()) {
        lastError_ = QStringLiteral("Vulkan scalar storage buffer is not initialized");
        return false;
    }

    vkDeviceWaitIdle(vkDevice);
    meshUseVertexScalars_ = useScalars && !scalars.empty();
    meshScalarMin_ = minVal;
    meshScalarMax_ = maxVal;
    meshNumBands_ = std::max(1, numBands);

    std::vector<float> expandedScalars(meshScalarSourceIndices_.size(), 0.0f);
    for (size_t i = 0; i < meshScalarSourceIndices_.size(); ++i) {
        const uint32_t sourceIndex = meshScalarSourceIndices_[i];
        if (sourceIndex < scalars.size()) {
            expandedScalars[i] = scalars[sourceIndex];
        }
    }

    const VkDeviceSize scalarSize = static_cast<VkDeviceSize>(
        std::max<size_t>(expandedScalars.size(), 1) * sizeof(float));
    const void* scalarData = expandedScalars.empty()
        ? static_cast<const void*>(nullptr)
        : static_cast<const void*>(expandedScalars.data());
    float zeroScalar = 0.0f;
    if (expandedScalars.empty()) {
        scalarData = &zeroScalar;
    }
    return meshScalarResource_.updateHostVisible(device,
                                                 scalarData,
                                                 scalarSize,
                                                 "mesh scalar storage",
                                                 lastError_);
}

bool VulkanClearFrameRenderer::uploadSelectionLines(
    const VulkanDevice& device,
    const std::vector<float>& lineVertices)
{
    lastError_.clear();
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }

    vkDeviceWaitIdle(vkDevice);
    selectionLineVertexResource_.destroy(device);
    selectionLineVertexCount_ = 0;

    if (lineVertices.empty()) {
        return true;
    }
    if (lineVertices.size() % 3 != 0) {
        lastError_ = QStringLiteral("Selection line vertex data is not position triplets");
        return false;
    }

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(lineVertices.size() * sizeof(float));
    if (!selectionLineVertexResource_.uploadHostVisible(device,
                                                        lineVertices.data(),
                                                        vertexSize,
                                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                        "selection line vertex",
                                                        lastError_)) {
        return false;
    }
    selectionLineVertexCount_ = static_cast<uint32_t>(lineVertices.size() / 3);
    return true;
}

bool VulkanClearFrameRenderer::uploadOverlayLines(
    const VulkanDevice& device,
    const std::vector<float>& lineVertices)
{
    lastError_.clear();
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }

    vkDeviceWaitIdle(vkDevice);
    overlayLineVertexResource_.destroy(device);
    overlayLineVertexCount_ = 0;

    if (lineVertices.empty()) {
        return true;
    }
    if (lineVertices.size() % 3 != 0) {
        lastError_ = QStringLiteral("Overlay line vertex data is not position triplets");
        return false;
    }

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(lineVertices.size() * sizeof(float));
    if (!overlayLineVertexResource_.uploadHostVisible(device,
                                                      lineVertices.data(),
                                                      vertexSize,
                                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                      "overlay line vertex",
                                                      lastError_)) {
        return false;
    }
    overlayLineVertexCount_ = static_cast<uint32_t>(lineVertices.size() / 3);
    return true;
}

bool VulkanClearFrameRenderer::uploadSliceLines(
    const VulkanDevice& device,
    const std::vector<float>& lineVertices)
{
    lastError_.clear();
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }

    vkDeviceWaitIdle(vkDevice);
    sliceLineVertexResource_.destroy(device);
    sliceLineVertexCount_ = 0;

    if (lineVertices.empty()) {
        return true;
    }
    if (lineVertices.size() % 3 != 0) {
        lastError_ = QStringLiteral("Slice line vertex data is not position triplets");
        return false;
    }

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(lineVertices.size() * sizeof(float));
    if (!sliceLineVertexResource_.uploadHostVisible(device,
                                                    lineVertices.data(),
                                                    vertexSize,
                                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                    "slice line vertex",
                                                    lastError_)) {
        return false;
    }
    sliceLineVertexCount_ = static_cast<uint32_t>(lineVertices.size() / 3);
    return true;
}

bool VulkanClearFrameRenderer::uploadIsoSurfaceMesh(const VulkanDevice& device, const Mesh& mesh)
{
    lastError_.clear();
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }

    vkDeviceWaitIdle(vkDevice);
    destroyIsoSurfaceBuffers(device);

    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    std::vector<VulkanMeshVertex> vertices;
    vertices.reserve(sourceVertexCount);
    for (size_t sourceIndex = 0; sourceIndex < sourceVertexCount; ++sourceIndex) {
        const size_t base = sourceIndex * 6;
        VulkanMeshVertex vertex{};
        vertex.position[0] = mesh.vertices[base + 0];
        vertex.position[1] = mesh.vertices[base + 1];
        vertex.position[2] = mesh.vertices[base + 2];
        vertex.normal[0] = mesh.vertices[base + 3];
        vertex.normal[1] = mesh.vertices[base + 4];
        vertex.normal[2] = mesh.vertices[base + 5];
        vertex.color[0] = 0.2f;
        vertex.color[1] = 0.8f;
        vertex.color[2] = 0.4f;
        vertices.push_back(vertex);
    }

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanMeshVertex));
    if (!isoSurfaceVertexResource_.uploadDeviceLocal(device,
                                                     vertices.data(),
                                                     vertexSize,
                                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                     commandResource_.pool(),
                                                     device.graphicsQueue(),
                                                     "iso surface vertex",
                                                     lastError_)) {
        return false;
    }

    const VkDeviceSize indexSize = static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t));
    if (!isoSurfaceIndexResource_.uploadDeviceLocal(device,
                                                    mesh.indices.data(),
                                                    indexSize,
                                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                    commandResource_.pool(),
                                                    device.graphicsQueue(),
                                                    "iso surface index",
                                                    lastError_)) {
        destroyIsoSurfaceBuffers(device);
        return false;
    }
    isoSurfaceIndexCount_ = static_cast<uint32_t>(mesh.indices.size());
    return true;
}

bool VulkanClearFrameRenderer::uploadClipPreviewMesh(const VulkanDevice& device, const Mesh& mesh)
{
    lastError_.clear();
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }

    vkDeviceWaitIdle(vkDevice);
    destroyClipPreviewBuffers(device);

    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    std::vector<VulkanMeshVertex> vertices;
    vertices.reserve(sourceVertexCount);
    for (size_t sourceIndex = 0; sourceIndex < sourceVertexCount; ++sourceIndex) {
        const size_t base = sourceIndex * 6;
        VulkanMeshVertex vertex{};
        vertex.position[0] = mesh.vertices[base + 0];
        vertex.position[1] = mesh.vertices[base + 1];
        vertex.position[2] = mesh.vertices[base + 2];
        vertex.normal[0] = mesh.vertices[base + 3];
        vertex.normal[1] = mesh.vertices[base + 4];
        vertex.normal[2] = mesh.vertices[base + 5];
        vertex.color[0] = 0.95f;
        vertex.color[1] = 0.58f;
        vertex.color[2] = 0.20f;
        vertices.push_back(vertex);
    }

    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanMeshVertex));
    if (!clipPreviewVertexResource_.uploadDeviceLocal(device,
                                                      vertices.data(),
                                                      vertexSize,
                                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                      commandResource_.pool(),
                                                      device.graphicsQueue(),
                                                      "clip preview vertex",
                                                      lastError_)) {
        return false;
    }

    const VkDeviceSize indexSize = static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t));
    if (!clipPreviewIndexResource_.uploadDeviceLocal(device,
                                                     mesh.indices.data(),
                                                     indexSize,
                                                     VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                     commandResource_.pool(),
                                                     device.graphicsQueue(),
                                                     "clip preview index",
                                                     lastError_)) {
        destroyClipPreviewBuffers(device);
        return false;
    }
    clipPreviewIndexCount_ = static_cast<uint32_t>(mesh.indices.size());

    if (!mesh.edgeVertices.empty()) {
        if (mesh.edgeVertices.size() % 3 != 0) {
            lastError_ = QStringLiteral("Clip preview line vertex data is not position triplets");
            destroyClipPreviewBuffers(device);
            return false;
        }
        const VkDeviceSize lineSize = static_cast<VkDeviceSize>(mesh.edgeVertices.size() * sizeof(float));
        if (!clipPreviewLineVertexResource_.uploadHostVisible(device,
                                                              mesh.edgeVertices.data(),
                                                              lineSize,
                                                              VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                              "clip preview line vertex",
                                                              lastError_)) {
            destroyClipPreviewBuffers(device);
            return false;
        }
        clipPreviewLineVertexCount_ = static_cast<uint32_t>(mesh.edgeVertices.size() / 3);
    }

    return true;
}

bool VulkanClearFrameRenderer::renderMeshFrame(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const VkClearColorValue& clearColor,
    const QMatrix4x4& mvp,
    const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    swapchainOutOfDate_ = false;
    if (!isInitialized() || meshPipeline_.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan mesh pipeline is not initialized");
        return false;
    }
    if (!meshVertexResource_.isValid() || !meshIndexResource_.isValid() || meshIndexCount_ == 0) {
        return renderTriangleFrame(device, swapchain, clearColor, axesMvp);
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    if (!acquireSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }
    if (imageIndex >= swapchainFramebuffers_.count()) {
        lastError_ = QStringLiteral("Swapchain image index is out of range");
        return false;
    }

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    if (!recordMeshCommandBuffer(commandResource_.buffer(),
                                 swapchainFramebuffers_.framebuffer(imageIndex),
                                 swapchain.extent(),
                                 clearColor,
                                 mvp,
                                 axesMvp)) {
        return false;
    }
    vkResetFences(vkDevice, 1, &inFlightFence_);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphore_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandResource_.bufferAddress();
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphore_;

    VkResult result = vkQueueSubmit(device.graphicsQueue(), 1, &submitInfo, inFlightFence_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkQueueSubmit failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    if (!presentSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }

    return true;
}

bool VulkanClearFrameRenderer::acquireSwapchainImage(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    uint32_t& imageIndex)
{
    VkResult result = vkAcquireNextImageKHR(
        device.device(),
        swapchain.swapchain(),
        UINT64_MAX,
        imageAvailableSemaphore_,
        VK_NULL_HANDLE,
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainOutOfDate_ = true;
        lastError_ = QStringLiteral("Vulkan swapchain is out of date");
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        swapchainOutOfDate_ = true;
        return true;
    }
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkAcquireNextImageKHR failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }
    return true;
}

bool VulkanClearFrameRenderer::presentSwapchainImage(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    uint32_t imageIndex)
{
    VkSwapchainKHR swapchainHandle = swapchain.swapchain();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphore_;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;

    VkResult result = vkQueuePresentKHR(device.presentQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        swapchainOutOfDate_ = true;
        lastError_ = QStringLiteral("Vulkan swapchain is out of date");
        return false;
    }
    if (result == VK_SUBOPTIMAL_KHR) {
        swapchainOutOfDate_ = true;
        return true;
    }
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkQueuePresentKHR failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }
    return true;
}

void VulkanClearFrameRenderer::recordAxesIndicator(
    VkCommandBuffer commandBuffer,
    VkExtent2D extent,
    const QMatrix4x4& axesMvp)
{
    if (linePipeline_.pipeline() == VK_NULL_HANDLE ||
        !axesLineVertexResource_.isValid() ||
        axesLineVertexCount_ < 6 ||
        extent.width == 0 ||
        extent.height == 0) {
        return;
    }

    const uint32_t margin = 8;
    const uint32_t availableWidth = extent.width > margin * 2 ? extent.width - margin * 2 : 0;
    const uint32_t availableHeight = extent.height > margin * 2 ? extent.height - margin * 2 : 0;
    const uint32_t axesSize = std::min<uint32_t>(120, std::min(availableWidth, availableHeight));
    if (axesSize == 0) {
        return;
    }

    VkViewport axesViewport{};
    axesViewport.x = static_cast<float>(margin);
    axesViewport.y = static_cast<float>(extent.height - axesSize - margin);
    axesViewport.width = static_cast<float>(axesSize);
    axesViewport.height = static_cast<float>(axesSize);
    axesViewport.minDepth = 0.0f;
    axesViewport.maxDepth = 1.0f;

    VkRect2D axesScissor{};
    axesScissor.offset = {
        static_cast<int32_t>(margin),
        static_cast<int32_t>(extent.height - axesSize - margin)
    };
    axesScissor.extent = {axesSize, axesSize};

    VkClearAttachment depthClear{};
    depthClear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthClear.clearValue.depthStencil = {1.0f, 0};
    VkClearRect clearRect{};
    clearRect.rect = axesScissor;
    clearRect.baseArrayLayer = 0;
    clearRect.layerCount = 1;
    vkCmdClearAttachments(commandBuffer, 1, &depthClear, 1, &clearRect);

    vkCmdSetViewport(commandBuffer, 0, 1, &axesViewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &axesScissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline_.pipeline());

    VkDeviceSize offsets[] = {0};
    VkBuffer axesVertexBuffer = axesLineVertexResource_.buffer();
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &axesVertexBuffer, offsets);

    const std::array<std::array<float, 4>, 3> axisColors = {{
        {{0.95f, 0.30f, 0.30f, 1.0f}},
        {{0.35f, 0.90f, 0.35f, 1.0f}},
        {{0.35f, 0.55f, 1.00f, 1.0f}}
    }};

    for (uint32_t axis = 0; axis < 3; ++axis) {
        std::array<float, 20> pushConstants{};
        std::memcpy(pushConstants.data(), axesMvp.constData(), 16 * sizeof(float));
        pushConstants[16] = axisColors[axis][0];
        pushConstants[17] = axisColors[axis][1];
        pushConstants[18] = axisColors[axis][2];
        pushConstants[19] = axisColors[axis][3];
        vkCmdPushConstants(commandBuffer,
                           linePipeline_.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           static_cast<uint32_t>(pushConstants.size() * sizeof(float)),
                           pushConstants.data());
        vkCmdDraw(commandBuffer, 2, 1, axis * 2, 0);
    }
}

void VulkanClearFrameRenderer::recordBackground(VkCommandBuffer commandBuffer, VkExtent2D extent)
{
    if (backgroundPipeline_.pipeline() == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0) {
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

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, backgroundPipeline_.pipeline());
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

bool VulkanClearFrameRenderer::renderPickFrame(
    const VulkanDevice& device,
    const QMatrix4x4& mvp,
    uint32_t width,
    uint32_t height)
{
    lastError_.clear();
    if (!isInitialized() || pickPipeline_.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan pick pipeline is not initialized");
        return false;
    }
    if (!meshVertexResource_.isValid() || !meshIndexResource_.isValid() || meshIndexCount_ == 0) {
        lastError_ = QStringLiteral("Vulkan mesh buffers are not initialized");
        return false;
    }

    width = std::max<uint32_t>(1, width);
    height = std::max<uint32_t>(1, height);
    if (!createPickResources(device, width, height)) {
        return false;
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    VulkanPickPass::Resources pickResources;
    pickResources.renderPass = pickRenderPass_.handle();
    pickResources.framebuffer = pickFramebuffer_.framebuffer(0);
    pickResources.colorImage = pickColorImage_;
    pickResources.pipeline = &pickPipeline_;
    pickResources.meshVertexResource = &meshVertexResource_;
    pickResources.meshIndexResource = &meshIndexResource_;
    pickResources.meshIndexCount = meshIndexCount_;
    if (!VulkanPickPass::record(commandResource_.buffer(),
                                pickExtent_,
                                mvp,
                                pickResources,
                                VK_NULL_HANDLE,
                                0,
                                0,
                                lastError_)) {
        return false;
    }
    vkResetFences(vkDevice, 1, &inFlightFence_);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandResource_.bufferAddress();

    VkResult result = vkQueueSubmit(device.graphicsQueue(), 1, &submitInfo, inFlightFence_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkQueueSubmit(pick) failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    return true;
}

bool VulkanClearFrameRenderer::pickElementAt(
    const VulkanDevice& device,
    const QMatrix4x4& mvp,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    int& elementId)
{
    elementId = -1;
    lastError_.clear();
    if (!isInitialized() || pickPipeline_.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan pick pipeline is not initialized");
        return false;
    }
    if (!meshVertexResource_.isValid() || !meshIndexResource_.isValid() || meshIndexCount_ == 0) {
        lastError_ = QStringLiteral("Vulkan mesh buffers are not initialized");
        return false;
    }

    width = std::max<uint32_t>(1, width);
    height = std::max<uint32_t>(1, height);
    x = std::min(x, width - 1);
    y = std::min(y, height - 1);

    if (!createPickResources(device, width, height)) {
        return false;
    }

    VulkanBufferResource readbackResource;
    unsigned char pixel[4] = {0, 0, 0, 0};
    if (!readbackResource.uploadHostVisible(device,
                                            pixel,
                                            sizeof(pixel),
                                            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            "pick readback",
                                            lastError_)) {
        return false;
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    VulkanPickPass::Resources pickResources;
    pickResources.renderPass = pickRenderPass_.handle();
    pickResources.framebuffer = pickFramebuffer_.framebuffer(0);
    pickResources.colorImage = pickColorImage_;
    pickResources.pipeline = &pickPipeline_;
    pickResources.meshVertexResource = &meshVertexResource_;
    pickResources.meshIndexResource = &meshIndexResource_;
    pickResources.meshIndexCount = meshIndexCount_;
    if (!VulkanPickPass::record(commandResource_.buffer(),
                                pickExtent_,
                                mvp,
                                pickResources,
                                readbackResource.buffer(),
                                x,
                                y,
                                lastError_)) {
        readbackResource.destroy(device);
        return false;
    }
    vkResetFences(vkDevice, 1, &inFlightFence_);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = commandResource_.bufferAddress();

    VkResult result = vkQueueSubmit(device.graphicsQueue(), 1, &submitInfo, inFlightFence_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkQueueSubmit(pick readback) failed: ") + VulkanContext::formatResult(result);
        readbackResource.destroy(device);
        return false;
    }

    result = vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkWaitForFences(pick readback) failed: ") + VulkanContext::formatResult(result);
        readbackResource.destroy(device);
        return false;
    }

    if (!readbackResource.readHostVisible(device, pixel, sizeof(pixel), "pick readback", lastError_)) {
        readbackResource.destroy(device);
        return false;
    }

    elementId = colorToId(pixel[0], pixel[1], pixel[2]);

    readbackResource.destroy(device);
    return true;
}

bool VulkanClearFrameRenderer::createImageViews(const VulkanDevice& device, const VulkanSwapchain& swapchain)
{
    imageViews_.resize(swapchain.images().size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < swapchain.images().size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchain.images()[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchain.imageFormat();
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(device.device(), &createInfo, nullptr, &imageViews_[i]);
        if (result != VK_SUCCESS) {
            lastError_ = QStringLiteral("vkCreateImageView failed: ") +
                VulkanContext::formatResult(result);
            return false;
        }
    }
    return true;
}

bool VulkanClearFrameRenderer::createRenderPass(const VulkanDevice& device, VkFormat imageFormat)
{
    depthFormat_ = findDepthFormat(device);
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        lastError_ = QStringLiteral("No supported Vulkan depth format");
        return false;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = imageFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    return renderPass_.create(device, createInfo, "main", lastError_);
}

bool VulkanClearFrameRenderer::createDepthResources(const VulkanDevice& device, const VulkanSwapchain& swapchain)
{
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        lastError_ = QStringLiteral("Vulkan depth format is undefined");
        return false;
    }

    if (!createImage(device,
                     swapchain.extent().width,
                     swapchain.extent().height,
                     depthFormat_,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     depthImage_,
                     depthImageMemory_)) {
        return false;
    }

    const VkImageAspectFlags aspectMask = hasStencilComponent(depthFormat_)
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;
    return createImageView(device, depthImage_, depthFormat_, aspectMask, depthImageView_);
}

bool VulkanClearFrameRenderer::createPickRenderPass(const VulkanDevice& device)
{
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        lastError_ = QStringLiteral("Vulkan depth format is undefined");
        return false;
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;

    return pickRenderPass_.create(device, createInfo, "pick", lastError_);
}

VkFormat VulkanClearFrameRenderer::findDepthFormat(const VulkanDevice& device) const
{
    const std::array<VkFormat, 3> candidates = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT
    };

    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(device.physicalDevice(), format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool VulkanClearFrameRenderer::hasStencilComponent(VkFormat format) const
{
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool VulkanClearFrameRenderer::createBackgroundGraphicsPipeline(const VulkanDevice& device)
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    const QString shaderDir = QString::fromUtf8(FERENDER_VULKAN_SHADER_DIR);
    const QString vertexShaderPath = shaderDir + QStringLiteral("/vulkan_background.vert.spv");
    const QString fragmentShaderPath = shaderDir + QStringLiteral("/vulkan_background.frag.spv");

    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    if (!createShaderModule(device, vertexShaderPath, vertexShader)) {
        return false;
    }
    if (!createShaderModule(device, fragmentShaderPath, fragmentShader)) {
        vkDestroyShaderModule(device.device(), vertexShader, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader;
    fragmentStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertexStage, fragmentStage};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.renderPass = renderPass_.handle();
    pipelineInfo.subpass = 0;

    const bool pipelineCreated = backgroundPipeline_.createGraphics(
        device, pipelineLayoutInfo, pipelineInfo, "background", lastError_);

    vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
    vkDestroyShaderModule(device.device(), vertexShader, nullptr);

    if (!pipelineCreated) {
        return false;
    }
    return true;
#else
    Q_UNUSED(device);
    return true;
#endif
}

bool VulkanClearFrameRenderer::createGraphicsPipeline(const VulkanDevice& device)
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    const QString shaderDir = QString::fromUtf8(FERENDER_VULKAN_SHADER_DIR);
    const QString vertexShaderPath = shaderDir + QStringLiteral("/vulkan_triangle.vert.spv");
    const QString fragmentShaderPath = shaderDir + QStringLiteral("/vulkan_triangle.frag.spv");

    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    if (!createShaderModule(device, vertexShaderPath, vertexShader)) {
        return false;
    }
    if (!createShaderModule(device, fragmentShaderPath, fragmentShader)) {
        vkDestroyShaderModule(device.device(), vertexShader, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader;
    fragmentStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertexStage, fragmentStage};

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.renderPass = renderPass_.handle();
    pipelineInfo.subpass = 0;

    const bool pipelineCreated = trianglePipeline_.createGraphics(
        device, pipelineLayoutInfo, pipelineInfo, "triangle", lastError_);

    vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
    vkDestroyShaderModule(device.device(), vertexShader, nullptr);

    if (!pipelineCreated) {
        return false;
    }

    return true;
#else
    Q_UNUSED(device);
    return true;
#endif
}

bool VulkanClearFrameRenderer::createMeshGraphicsPipeline(const VulkanDevice& device)
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    const QString shaderDir = QString::fromUtf8(FERENDER_VULKAN_SHADER_DIR);
    const QString vertexShaderPath = shaderDir + QStringLiteral("/vulkan_mesh.vert.spv");
    const QString fragmentShaderPath = shaderDir + QStringLiteral("/vulkan_mesh.frag.spv");

    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    if (!createShaderModule(device, vertexShaderPath, vertexShader)) {
        return false;
    }
    if (!createShaderModule(device, fragmentShaderPath, fragmentShader)) {
        vkDestroyShaderModule(device.device(), vertexShader, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader;
    fragmentStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertexStage, fragmentStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(VulkanMeshVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(VulkanMeshVertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(VulkanMeshVertex, normal);
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = offsetof(VulkanMeshVertex, color);
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 20 * sizeof(float);

    VkDescriptorSetLayoutBinding scalarBinding{};
    scalarBinding.binding = 0;
    scalarBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    scalarBinding.descriptorCount = 1;
    scalarBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.bindingCount = 1;
    descriptorSetLayoutInfo.pBindings = &scalarBinding;

    if (!meshScalarSetLayout_.create(
            device, descriptorSetLayoutInfo, "mesh scalar", lastError_)) {
        vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
        vkDestroyShaderModule(device.device(), vertexShader, nullptr);
        return false;
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    const VkDescriptorSetLayout meshScalarSetLayout = meshScalarSetLayout_.handle();
    pipelineLayoutInfo.pSetLayouts = &meshScalarSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.renderPass = renderPass_.handle();
    pipelineInfo.subpass = 0;

    const bool pipelineCreated = meshPipeline_.createGraphics(
        device, pipelineLayoutInfo, pipelineInfo, "mesh", lastError_);

    vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
    vkDestroyShaderModule(device.device(), vertexShader, nullptr);

    if (!pipelineCreated) {
        meshScalarSetLayout_.destroy(device);
        return false;
    }
    return true;
#else
    Q_UNUSED(device);
    return true;
#endif
}

bool VulkanClearFrameRenderer::createIsoSurfaceGraphicsPipeline(const VulkanDevice& device)
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    const QString shaderDir = QString::fromUtf8(FERENDER_VULKAN_SHADER_DIR);
    const QString vertexShaderPath = shaderDir + QStringLiteral("/vulkan_iso.vert.spv");
    const QString fragmentShaderPath = shaderDir + QStringLiteral("/vulkan_iso.frag.spv");

    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    if (!createShaderModule(device, vertexShaderPath, vertexShader)) {
        return false;
    }
    if (!createShaderModule(device, fragmentShaderPath, fragmentShader)) {
        vkDestroyShaderModule(device.device(), vertexShader, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader;
    fragmentStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertexStage, fragmentStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(VulkanMeshVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(VulkanMeshVertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(VulkanMeshVertex, normal);
    attributes[2].binding = 0;
    attributes[2].location = 2;
    attributes[2].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[2].offset = offsetof(VulkanMeshVertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 20 * sizeof(float);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.renderPass = renderPass_.handle();
    pipelineInfo.subpass = 0;

    const bool pipelineCreated = isoSurfacePipeline_.createGraphics(
        device, pipelineLayoutInfo, pipelineInfo, "iso surface", lastError_);

    vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
    vkDestroyShaderModule(device.device(), vertexShader, nullptr);

    if (!pipelineCreated) {
        return false;
    }
    return true;
#else
    Q_UNUSED(device);
    return true;
#endif
}

bool VulkanClearFrameRenderer::createLineGraphicsPipeline(const VulkanDevice& device)
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    const QString shaderDir = QString::fromUtf8(FERENDER_VULKAN_SHADER_DIR);
    const QString vertexShaderPath = shaderDir + QStringLiteral("/vulkan_line.vert.spv");
    const QString fragmentShaderPath = shaderDir + QStringLiteral("/vulkan_line.frag.spv");

    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    if (!createShaderModule(device, vertexShaderPath, vertexShader)) {
        return false;
    }
    if (!createShaderModule(device, fragmentShaderPath, fragmentShader)) {
        vkDestroyShaderModule(device.device(), vertexShader, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader;
    fragmentStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertexStage, fragmentStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = 3 * sizeof(float);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attribute{};
    attribute.binding = 0;
    attribute.location = 0;
    attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
    attribute.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attribute;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 20 * sizeof(float);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.renderPass = renderPass_.handle();
    pipelineInfo.subpass = 0;

    const bool pipelineCreated = linePipeline_.createGraphics(
        device, pipelineLayoutInfo, pipelineInfo, "line", lastError_);

    vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
    vkDestroyShaderModule(device.device(), vertexShader, nullptr);

    if (!pipelineCreated) {
        return false;
    }
    return true;
#else
    Q_UNUSED(device);
    return true;
#endif
}

bool VulkanClearFrameRenderer::createPickGraphicsPipeline(const VulkanDevice& device)
{
#if defined(FERENDER_VULKAN_SHADER_DIR)
    const QString shaderDir = QString::fromUtf8(FERENDER_VULKAN_SHADER_DIR);
    const QString vertexShaderPath = shaderDir + QStringLiteral("/vulkan_pick.vert.spv");
    const QString fragmentShaderPath = shaderDir + QStringLiteral("/vulkan_pick.frag.spv");

    VkShaderModule vertexShader = VK_NULL_HANDLE;
    VkShaderModule fragmentShader = VK_NULL_HANDLE;
    if (!createShaderModule(device, vertexShaderPath, vertexShader)) {
        return false;
    }
    if (!createShaderModule(device, fragmentShaderPath, fragmentShader)) {
        vkDestroyShaderModule(device.device(), vertexShader, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexShader;
    vertexStage.pName = "main";

    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentShader;
    fragmentStage.pName = "main";

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages = {vertexStage, fragmentStage};

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(VulkanMeshVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(VulkanMeshVertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 3;
    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[1].offset = offsetof(VulkanMeshVertex, pickColor);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = 16 * sizeof(float);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.renderPass = pickRenderPass_.handle();
    pipelineInfo.subpass = 0;

    const bool pipelineCreated = pickPipeline_.createGraphics(
        device, pipelineLayoutInfo, pipelineInfo, "pick", lastError_);

    vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
    vkDestroyShaderModule(device.device(), vertexShader, nullptr);

    if (!pipelineCreated) {
        return false;
    }
    return true;
#else
    Q_UNUSED(device);
    return true;
#endif
}

bool VulkanClearFrameRenderer::createShaderModule(
    const VulkanDevice& device,
    const QString& shaderPath,
    VkShaderModule& shaderModule)
{
    QFile shaderFile(shaderPath);
    if (!shaderFile.open(QIODevice::ReadOnly)) {
        lastError_ = QStringLiteral("Failed to open Vulkan shader: ") + shaderPath;
        return false;
    }

    const QByteArray shaderBytes = shaderFile.readAll();
    if (shaderBytes.isEmpty() || shaderBytes.size() % static_cast<int>(sizeof(uint32_t)) != 0) {
        lastError_ = QStringLiteral("Invalid Vulkan shader bytecode: ") + shaderPath;
        return false;
    }
    std::vector<uint32_t> shaderWords(static_cast<size_t>(shaderBytes.size()) / sizeof(uint32_t));
    std::memcpy(shaderWords.data(), shaderBytes.constData(), static_cast<size_t>(shaderBytes.size()));

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = shaderWords.size() * sizeof(uint32_t);
    createInfo.pCode = shaderWords.data();

    VkResult result = vkCreateShaderModule(device.device(), &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateShaderModule failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }
    return true;
}

bool VulkanClearFrameRenderer::createImage(
    const VulkanDevice& device,
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkImage& image,
    VkDeviceMemory& memory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult result = vkCreateImage(device.device(), &imageInfo, nullptr, &image);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateImage failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device.device(), image, &requirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        device,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    result = vkAllocateMemory(device.device(), &allocInfo, nullptr, &memory);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkAllocateMemory(image) failed: ") + VulkanContext::formatResult(result);
        vkDestroyImage(device.device(), image, nullptr);
        image = VK_NULL_HANDLE;
        return false;
    }

    result = vkBindImageMemory(device.device(), image, memory, 0);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkBindImageMemory failed: ") + VulkanContext::formatResult(result);
        vkFreeMemory(device.device(), memory, nullptr);
        vkDestroyImage(device.device(), image, nullptr);
        memory = VK_NULL_HANDLE;
        image = VK_NULL_HANDLE;
        return false;
    }

    return true;
}

bool VulkanClearFrameRenderer::createImageView(
    const VulkanDevice& device,
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectMask,
    VkImageView& imageView)
{
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = format;
    createInfo.subresourceRange.aspectMask = aspectMask;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    VkResult result = vkCreateImageView(device.device(), &createInfo, nullptr, &imageView);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateImageView failed: ") + VulkanContext::formatResult(result);
        return false;
    }
    return true;
}

uint32_t VulkanClearFrameRenderer::findMemoryType(
    const VulkanDevice& device,
    uint32_t typeFilter,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(device.physicalDevice(), &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

bool VulkanClearFrameRenderer::createMeshScalarDescriptor(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE || !meshScalarResource_.isValid()) {
        lastError_ = QStringLiteral("Vulkan scalar storage buffer is not initialized");
        return false;
    }

    const VkDeviceSize range = static_cast<VkDeviceSize>(
        std::max<uint32_t>(meshScalarCount_, 1) * sizeof(float));
    return meshScalarDescriptor_.createStorageBufferSet(device,
                                                        meshScalarSetLayout_.handle(),
                                                        meshScalarResource_.buffer(),
                                                        range,
                                                        lastError_);
}

bool VulkanClearFrameRenderer::createAxesIndicatorResource(const VulkanDevice& device)
{
    axesLineVertexResource_.destroy(device);
    axesLineVertexCount_ = 0;

    const std::array<float, 18> axesVertices = {
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f
    };
    if (!axesLineVertexResource_.uploadHostVisible(device,
                                                   axesVertices.data(),
                                                   static_cast<VkDeviceSize>(axesVertices.size() * sizeof(float)),
                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                   "axes indicator line vertex",
                                                   lastError_)) {
        return false;
    }
    axesLineVertexCount_ = static_cast<uint32_t>(axesVertices.size() / 3);
    return true;
}

void VulkanClearFrameRenderer::destroyMeshBuffers(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }
    meshScalarDescriptor_.destroy(device);
    meshIndexResource_.destroy(device);
    edgeIndexResource_.destroy(device);
    meshVertexResource_.destroy(device);
    meshScalarResource_.destroy(device);
    edgeVertexResource_.destroy(device);
    selectionLineVertexResource_.destroy(device);
    meshIndexCount_ = 0;
    meshUseVertexScalars_ = false;
    meshScalarMin_ = 0.0f;
    meshScalarMax_ = 1.0f;
    meshNumBands_ = 10;
    meshScalarSourceIndices_.clear();
    meshScalarCount_ = 0;
    edgeIndexCount_ = 0;
    selectionLineVertexCount_ = 0;
}

void VulkanClearFrameRenderer::destroyIsoSurfaceBuffers(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }
    isoSurfaceIndexResource_.destroy(device);
    isoSurfaceVertexResource_.destroy(device);
    isoSurfaceIndexCount_ = 0;
}

void VulkanClearFrameRenderer::destroyClipPreviewBuffers(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }
    clipPreviewIndexResource_.destroy(device);
    clipPreviewVertexResource_.destroy(device);
    clipPreviewLineVertexResource_.destroy(device);
    clipPreviewIndexCount_ = 0;
    clipPreviewLineVertexCount_ = 0;
}

bool VulkanClearFrameRenderer::createPickResources(const VulkanDevice& device, uint32_t width, uint32_t height)
{
    if (pickFramebuffer_.isValid() &&
        pickExtent_.width == width &&
        pickExtent_.height == height) {
        return true;
    }

    destroyPickResources(device);
    pickExtent_ = {width, height};

    if (!createImage(device,
                     width,
                     height,
                     VK_FORMAT_R8G8B8A8_UNORM,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                     pickColorImage_,
                     pickColorMemory_)) {
        return false;
    }
    if (!createImageView(
            device, pickColorImage_, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT, pickColorImageView_)) {
        return false;
    }

    if (!createImage(device,
                     width,
                     height,
                     depthFormat_,
                     VK_IMAGE_TILING_OPTIMAL,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                     pickDepthImage_,
                     pickDepthMemory_)) {
        return false;
    }
    const VkImageAspectFlags depthAspect = hasStencilComponent(depthFormat_)
        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
        : VK_IMAGE_ASPECT_DEPTH_BIT;
    if (!createImageView(device, pickDepthImage_, depthFormat_, depthAspect, pickDepthImageView_)) {
        return false;
    }

    const std::vector<VkImageView> attachments = {pickColorImageView_, pickDepthImageView_};
    return pickFramebuffer_.createSingle(device,
                                         pickRenderPass_.handle(),
                                         VkExtent2D{width, height},
                                         attachments,
                                         "pick",
                                         lastError_);
}

void VulkanClearFrameRenderer::destroyPickResources(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }
    pickFramebuffer_.destroy(device);
    if (pickDepthImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vkDevice, pickDepthImageView_, nullptr);
        pickDepthImageView_ = VK_NULL_HANDLE;
    }
    if (pickDepthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(vkDevice, pickDepthImage_, nullptr);
        pickDepthImage_ = VK_NULL_HANDLE;
    }
    if (pickDepthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, pickDepthMemory_, nullptr);
        pickDepthMemory_ = VK_NULL_HANDLE;
    }
    if (pickColorImageView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(vkDevice, pickColorImageView_, nullptr);
        pickColorImageView_ = VK_NULL_HANDLE;
    }
    if (pickColorImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(vkDevice, pickColorImage_, nullptr);
        pickColorImage_ = VK_NULL_HANDLE;
    }
    if (pickColorMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(vkDevice, pickColorMemory_, nullptr);
        pickColorMemory_ = VK_NULL_HANDLE;
    }
    pickExtent_ = {0, 0};
}

bool VulkanClearFrameRenderer::createFramebuffers(const VulkanDevice& device, const VulkanSwapchain& swapchain)
{
    if (depthImageView_ == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan depth image view is not initialized");
        return false;
    }

    std::vector<std::vector<VkImageView>> attachmentSets;
    attachmentSets.reserve(imageViews_.size());
    for (size_t i = 0; i < imageViews_.size(); ++i) {
        attachmentSets.push_back({imageViews_[i], depthImageView_});
    }
    return swapchainFramebuffers_.create(device,
                                         renderPass_.handle(),
                                         swapchain.extent(),
                                         attachmentSets,
                                         "swapchain",
                                         lastError_);
}

bool VulkanClearFrameRenderer::createCommandPool(const VulkanDevice& device)
{
    return commandResource_.create(device, lastError_);
}

bool VulkanClearFrameRenderer::createSyncObjects(const VulkanDevice& device)
{
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VkResult result = vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &imageAvailableSemaphore_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateSemaphore(imageAvailable) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    result = vkCreateSemaphore(device.device(), &semaphoreInfo, nullptr, &renderFinishedSemaphore_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateSemaphore(renderFinished) failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    result = vkCreateFence(device.device(), &fenceInfo, nullptr, &inFlightFence_);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkCreateFence failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    return true;
}

bool VulkanClearFrameRenderer::recordCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkFramebuffer framebuffer,
    VkExtent2D extent,
    const VkClearColorValue& clearColor,
    bool drawTriangle,
    const QMatrix4x4& axesMvp)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkBeginCommandBuffer failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_.handle();
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    recordBackground(commandBuffer, extent);
    if (drawTriangle) {
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

        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline_.pipeline());
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
    recordAxesIndicator(commandBuffer, extent, axesMvp);
    vkCmdEndRenderPass(commandBuffer);

    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkEndCommandBuffer failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    return true;
}

bool VulkanClearFrameRenderer::recordMeshCommandBuffer(
    VkCommandBuffer commandBuffer,
    VkFramebuffer framebuffer,
    VkExtent2D extent,
    const VkClearColorValue& clearColor,
    const QMatrix4x4& mvp,
    const QMatrix4x4& axesMvp)
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    VkResult result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkBeginCommandBuffer failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = clearColor;
    clearValues[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_.handle();
    renderPassInfo.framebuffer = framebuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = extent;
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    recordBackground(commandBuffer, extent);

    VulkanMeshFramePass::Resources meshFrameResources;
    meshFrameResources.meshPipeline = &meshPipeline_;
    meshFrameResources.isoSurfacePipeline = &isoSurfacePipeline_;
    meshFrameResources.linePipeline = &linePipeline_;
    meshFrameResources.meshScalarDescriptor = &meshScalarDescriptor_;
    meshFrameResources.meshVertexResource = &meshVertexResource_;
    meshFrameResources.meshIndexResource = &meshIndexResource_;
    meshFrameResources.meshIndexCount = meshIndexCount_;
    meshFrameResources.meshUseVertexScalars = meshUseVertexScalars_;
    meshFrameResources.meshScalarMin = meshScalarMin_;
    meshFrameResources.meshScalarMax = meshScalarMax_;
    meshFrameResources.meshNumBands = meshNumBands_;
    meshFrameResources.isoSurfaceVertexResource = &isoSurfaceVertexResource_;
    meshFrameResources.isoSurfaceIndexResource = &isoSurfaceIndexResource_;
    meshFrameResources.isoSurfaceIndexCount = isoSurfaceIndexCount_;
    meshFrameResources.clipPreviewVertexResource = &clipPreviewVertexResource_;
    meshFrameResources.clipPreviewIndexResource = &clipPreviewIndexResource_;
    meshFrameResources.clipPreviewIndexCount = clipPreviewIndexCount_;
    meshFrameResources.clipPreviewLineVertexResource = &clipPreviewLineVertexResource_;
    meshFrameResources.clipPreviewLineVertexCount = clipPreviewLineVertexCount_;
    meshFrameResources.overlayLineVertexResource = &overlayLineVertexResource_;
    meshFrameResources.overlayLineVertexCount = overlayLineVertexCount_;
    meshFrameResources.edgeVertexResource = &edgeVertexResource_;
    meshFrameResources.edgeIndexResource = &edgeIndexResource_;
    meshFrameResources.edgeIndexCount = edgeIndexCount_;
    meshFrameResources.sliceLineVertexResource = &sliceLineVertexResource_;
    meshFrameResources.sliceLineVertexCount = sliceLineVertexCount_;
    meshFrameResources.selectionLineVertexResource = &selectionLineVertexResource_;
    meshFrameResources.selectionLineVertexCount = selectionLineVertexCount_;
    VulkanMeshFramePass::record(commandBuffer, extent, mvp, meshFrameResources);

    recordAxesIndicator(commandBuffer, extent, axesMvp);

    vkCmdEndRenderPass(commandBuffer);

    result = vkEndCommandBuffer(commandBuffer);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkEndCommandBuffer failed: ") +
            VulkanContext::formatResult(result);
        return false;
    }

    return true;
}

int VulkanClearFrameRenderer::colorToId(unsigned char r, unsigned char g, unsigned char b) const
{
    if (r == 0 && g == 0 && b == 0) {
        return -1;
    }
    const int id = static_cast<int>(r) |
        (static_cast<int>(g) << 8) |
        (static_cast<int>(b) << 16);
    return id - 1;
}
