#include "VulkanClearFrameRenderer.h"

#include "VulkanContext.h"
#include "VulkanDevice.h"
#include "VulkanMeshFramePass.h"
#include "VulkanPickPass.h"
#include "VulkanRenderBackend.h"
#include "VulkanStagingUploadContext.h"
#include "VulkanSwapchain.h"
#include "Geometry.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace {
constexpr size_t kMaxInteractiveEdgeIndices = 8000000;
constexpr uint32_t kAxesVerticesPerAxis = 26;
constexpr float kPi = 3.14159265358979323846f;

struct VulkanMeshVertex {
    float position[3];
    float normal[3];
    float color[3];
    float pickColor[3];
};

struct VulkanLineVertex {
    float position[3];
    float scalar;
};

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalize(const Vec3& v)
{
    const float len2 = dot(v, v);
    if (len2 <= 1.0e-12f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return scale(v, 1.0f / std::sqrt(len2));
}

std::vector<VulkanLineVertex> makeLineVertices(const std::vector<float>& positions)
{
    std::vector<VulkanLineVertex> vertices;
    if (positions.size() % 3 != 0) {
        return vertices;
    }
    vertices.resize(positions.size() / 3);
    for (size_t i = 0; i < vertices.size(); ++i) {
        vertices[i].position[0] = positions[i * 3 + 0];
        vertices[i].position[1] = positions[i * 3 + 1];
        vertices[i].position[2] = positions[i * 3 + 2];
        vertices[i].scalar = 0.0f;
    }
    return vertices;
}

std::vector<float> buildAxesLineVertices()
{
    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(kAxesVerticesPerAxis * 3 * 3));
    auto appendLine = [&vertices](const Vec3& a, const Vec3& b) {
        vertices.insert(vertices.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
    };

    const std::array<Vec3, 3> dirs = {{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    }};
    const std::array<Vec3, 3> ups = {{
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f}
    }};

    constexpr float shaftLength = 0.78f;
    constexpr float tipLength = 1.0f;
    constexpr float shaftRadius = 0.025f;
    constexpr float arrowRadius = 0.12f;
    for (size_t axis = 0; axis < dirs.size(); ++axis) {
        const Vec3 dir = dirs[axis];
        const Vec3 right = cross(dir, ups[axis]);
        const Vec3 up = cross(right, dir);
        const Vec3 origin{0.0f, 0.0f, 0.0f};
        const Vec3 shaftEnd = scale(dir, shaftLength);
        const Vec3 tip = scale(dir, tipLength);
        const std::array<Vec3, 4> base = {{
            add(shaftEnd, scale(right, arrowRadius)),
            add(shaftEnd, scale(up, arrowRadius)),
            add(shaftEnd, scale(right, -arrowRadius)),
            add(shaftEnd, scale(up, -arrowRadius))
        }};

        appendLine(origin, shaftEnd);
        appendLine(scale(right, shaftRadius), add(shaftEnd, scale(right, shaftRadius)));
        appendLine(scale(up, shaftRadius), add(shaftEnd, scale(up, shaftRadius)));
        appendLine(scale(right, -shaftRadius), add(shaftEnd, scale(right, -shaftRadius)));
        appendLine(scale(up, -shaftRadius), add(shaftEnd, scale(up, -shaftRadius)));
        for (const Vec3& p : base) {
            appendLine(tip, p);
        }
        for (size_t i = 0; i < base.size(); ++i) {
            appendLine(base[i], base[(i + 1) % base.size()]);
        }
    }
    return vertices;
}

std::vector<VulkanMeshVertex> buildAxesSolidVertices()
{
    std::vector<VulkanMeshVertex> vertices;
    vertices.reserve(3 * 24 * 12 + 8 * 12 * 6);

    auto appendVertex = [&vertices](const Vec3& position, const Vec3& normal, const Vec3& color) {
        VulkanMeshVertex vertex{};
        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.position[2] = position.z;
        const Vec3 n = normalize(normal);
        vertex.normal[0] = n.x;
        vertex.normal[1] = n.y;
        vertex.normal[2] = n.z;
        vertex.color[0] = color.x;
        vertex.color[1] = color.y;
        vertex.color[2] = color.z;
        vertices.push_back(vertex);
    };
    auto appendTri = [&appendVertex](const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& color) {
        const Vec3 normal = normalize(cross(sub(b, a), sub(c, a)));
        appendVertex(a, normal, color);
        appendVertex(b, normal, color);
        appendVertex(c, normal, color);
    };

    constexpr int segs = 24;
    constexpr float shaftLen = 0.70f;
    constexpr float shaftRadius = 0.028f;
    constexpr float coneRadius = 0.10f;
    constexpr float coneLen = 0.30f;
    constexpr float ballRadius = 0.065f;
    const std::array<Vec3, 3> dirs = {{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    }};
    const std::array<Vec3, 3> ups = {{
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f}
    }};
    const std::array<Vec3, 3> colors = {{
        {0.95f, 0.30f, 0.30f},
        {0.35f, 0.90f, 0.35f},
        {0.35f, 0.55f, 1.00f}
    }};

    for (size_t axis = 0; axis < dirs.size(); ++axis) {
        const Vec3 dir = dirs[axis];
        const Vec3 right = normalize(cross(dir, ups[axis]));
        const Vec3 up = normalize(cross(right, dir));
        const Vec3 color = colors[axis];
        const Vec3 shaftColor = scale(color, 0.85f);
        for (int i = 0; i < segs; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segs);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segs);
            const Vec3 radial0 = add(scale(right, std::cos(a0)), scale(up, std::sin(a0)));
            const Vec3 radial1 = add(scale(right, std::cos(a1)), scale(up, std::sin(a1)));
            const Vec3 b0 = scale(radial0, shaftRadius);
            const Vec3 b1 = scale(radial1, shaftRadius);
            const Vec3 t0 = add(scale(dir, shaftLen), b0);
            const Vec3 t1 = add(scale(dir, shaftLen), b1);
            appendVertex(b0, radial0, shaftColor);
            appendVertex(t0, radial0, shaftColor);
            appendVertex(t1, radial1, shaftColor);
            appendVertex(b0, radial0, shaftColor);
            appendVertex(t1, radial1, shaftColor);
            appendVertex(b1, radial1, shaftColor);

            const Vec3 coneBase0 = add(scale(dir, shaftLen), scale(radial0, coneRadius));
            const Vec3 coneBase1 = add(scale(dir, shaftLen), scale(radial1, coneRadius));
            const Vec3 tip = scale(dir, shaftLen + coneLen);
            appendTri(tip, coneBase0, coneBase1, color);
            appendTri(scale(dir, shaftLen), coneBase1, coneBase0, scale(color, 0.55f));
        }
    }

    const Vec3 ballColor{0.82f, 0.82f, 0.85f};
    constexpr int ballRings = 8;
    constexpr int ballSectors = 12;
    for (int r = 0; r < ballRings; ++r) {
        const float phi0 = kPi * static_cast<float>(r) / static_cast<float>(ballRings) - kPi * 0.5f;
        const float phi1 = kPi * static_cast<float>(r + 1) / static_cast<float>(ballRings) - kPi * 0.5f;
        for (int s = 0; s < ballSectors; ++s) {
            const float theta0 = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(ballSectors);
            const float theta1 = 2.0f * kPi * static_cast<float>(s + 1) / static_cast<float>(ballSectors);
            const Vec3 p00 = scale({std::cos(phi0) * std::cos(theta0), std::sin(phi0), std::cos(phi0) * std::sin(theta0)}, ballRadius);
            const Vec3 p10 = scale({std::cos(phi1) * std::cos(theta0), std::sin(phi1), std::cos(phi1) * std::sin(theta0)}, ballRadius);
            const Vec3 p01 = scale({std::cos(phi0) * std::cos(theta1), std::sin(phi0), std::cos(phi0) * std::sin(theta1)}, ballRadius);
            const Vec3 p11 = scale({std::cos(phi1) * std::cos(theta1), std::sin(phi1), std::cos(phi1) * std::sin(theta1)}, ballRadius);
            appendVertex(p00, p00, ballColor);
            appendVertex(p10, p10, ballColor);
            appendVertex(p11, p11, ballColor);
            appendVertex(p00, p00, ballColor);
            appendVertex(p11, p11, ballColor);
            appendVertex(p01, p01, ballColor);
        }
    }

    return vertices;
}

bool isPartVisible(const VulkanMeshUploadOptions& options, int part)
{
    const auto it = options.partVisibility.find(part);
    return it == options.partVisibility.end() || it->second;
}

bool isElementVisible(const VulkanMeshUploadOptions& options, int elementId)
{
    return elementId < 0 || options.hiddenElementIds.count(elementId) == 0;
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

    return createRenderPass(device, swapchain.imageFormat()) &&
           createBackgroundGraphicsPipeline(device) &&
           createGraphicsPipeline(device) &&
           createMeshGraphicsPipeline(device) &&
           createPointGraphicsPipeline(device) &&
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
    pickResources_.destroy(device);
    swapchainFrameResources_.destroy(device);
    depthResource_.destroy(device);
    depthFormat_ = VK_FORMAT_UNDEFINED;
    pipelines_.destroy(device);
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
    axesSolidVertexResource_.destroy(device);
    axesSolidVertexCount_ = 0;
    pickRenderPass_.destroy(device);
    renderPass_.destroy(device);
}

bool VulkanClearFrameRenderer::renderClearFrame(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const VkClearColorValue& clearColor,
    const QMatrix4x4& axesMvp)
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

    if (imageIndex >= swapchainFrameResources_.count()) {
        lastError_ = QStringLiteral("Swapchain image index is out of range");
        return false;
    }

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    if (!recordCommandBuffer(commandResource_.buffer(),
                             swapchainFrameResources_.framebuffer(imageIndex),
                             swapchain.extent(),
                             clearColor,
                             false,
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

bool VulkanClearFrameRenderer::renderTriangleFrame(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const VkClearColorValue& clearColor,
    const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    swapchainOutOfDate_ = false;
    if (!isInitialized() || pipelines_.triangle.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan triangle pipeline is not initialized");
        return false;
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    if (!acquireSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }
    if (imageIndex >= swapchainFrameResources_.count()) {
        lastError_ = QStringLiteral("Swapchain image index is out of range");
        return false;
    }

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    if (!recordCommandBuffer(commandResource_.buffer(),
                             swapchainFrameResources_.framebuffer(imageIndex),
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

    const bool hasSurfaceMesh =
        !mesh.vertices.empty() && !mesh.indices.empty() && mesh.vertices.size() % 6 == 0;
    const bool hasEdgeMesh =
        !mesh.edgeVertices.empty() && !mesh.edgeIndices.empty() && mesh.edgeVertices.size() % 3 == 0;
    if (!hasSurfaceMesh && !hasEdgeMesh) {
        return true;
    }

    meshResources_.meshUseVertexScalars =
        hasSurfaceMesh && options.useVertexColor && !options.vertexScalars.empty();
    meshResources_.meshScalarMin = options.scalarMin;
    meshResources_.meshScalarMax = options.scalarMax;
    meshResources_.meshNumBands = std::max(1, options.numBands);
    meshResources_.edgeUseVertexScalars =
        hasEdgeMesh &&
        options.useVertexColor &&
        options.edgeScalars.size() == mesh.edgeVertices.size() / 3;
    meshResources_.edgeScalarMin = options.scalarMin;
    meshResources_.edgeScalarMax = options.scalarMax;
    meshResources_.edgeNumBands = std::max(1, options.numBands);

    const size_t sourceVertexCount = mesh.vertices.size() / 6;
    const size_t triangleCount = mesh.indices.size() / 3;
    std::vector<VulkanMeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<float> expandedScalars;
    meshResources_.meshScalarSourceIndices.clear();
    if (hasSurfaceMesh) {
        vertices.reserve(triangleCount * 3);
        indices.reserve(triangleCount * 3);
        expandedScalars.reserve(triangleCount * 3);
        meshResources_.meshScalarSourceIndices.reserve(triangleCount * 3);

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
            if (!isElementVisible(options, elementId)) {
                continue;
            }
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
                meshResources_.meshScalarSourceIndices.push_back(sourceIndex);
            }
        }
    }

    VulkanStagingUploadContext uploadContext;
    if (!vertices.empty() && !indices.empty()) {
        const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanMeshVertex));
        if (!uploadContext.uploadBuffer(device,
                                        meshResources_.meshVertexResource,
                                        vertices.data(),
                                        vertexSize,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        "mesh vertex",
                                        lastError_)) {
            uploadContext.discard(device);
            destroyMeshBuffers(device);
            return false;
        }

        const VkDeviceSize indexSize = static_cast<VkDeviceSize>(indices.size() * sizeof(uint32_t));
        if (!uploadContext.uploadBuffer(device,
                                        meshResources_.meshIndexResource,
                                        indices.data(),
                                        indexSize,
                                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                        "mesh index",
                                        lastError_)) {
            uploadContext.discard(device);
            destroyMeshBuffers(device);
            return false;
        }
    }

    if (hasEdgeMesh) {
        if (mesh.edgeIndices.size() > kMaxInteractiveEdgeIndices) {
            // 超大模型的全量边线会带来第二遍海量 draw，默认只保留表面和选中高亮线。
            meshResources_.edgeVertexResource.destroy(device);
            meshResources_.edgeIndexResource.destroy(device);
            meshResources_.edgeIndexCount = 0;
        } else {
            std::vector<uint32_t> edgeIndices;
            edgeIndices.reserve(mesh.edgeIndices.size());
            const size_t edgeCount = mesh.edgeIndices.size() / 2;
            for (size_t edge = 0; edge < edgeCount; ++edge) {
                const int part = edge < options.edgeToPart.size()
                    ? options.edgeToPart[edge]
                    : -1;
                const int elementId = edge < mesh.edgeToElement.size()
                    ? mesh.edgeToElement[edge]
                    : -1;
                if (!isElementVisible(options, elementId)) {
                    continue;
                }
                if (!isPartVisible(options, part)) {
                    continue;
                }
                edgeIndices.push_back(mesh.edgeIndices[edge * 2]);
                edgeIndices.push_back(mesh.edgeIndices[edge * 2 + 1]);
            }
            if (edgeIndices.empty()) {
                meshResources_.edgeVertexResource.destroy(device);
                meshResources_.edgeIndexResource.destroy(device);
                meshResources_.edgeIndexCount = 0;
            } else {
                std::vector<VulkanLineVertex> edgeVertices = makeLineVertices(mesh.edgeVertices);
                if (meshResources_.edgeUseVertexScalars) {
                    for (size_t i = 0; i < edgeVertices.size(); ++i) {
                        edgeVertices[i].scalar = options.edgeScalars[i];
                    }
                }
                const VkDeviceSize edgeVertexSize =
                    static_cast<VkDeviceSize>(edgeVertices.size() * sizeof(VulkanLineVertex));
                if (!uploadContext.uploadBuffer(device,
                                                meshResources_.edgeVertexResource,
                                                edgeVertices.data(),
                                                edgeVertexSize,
                                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                "edge vertex",
                                                lastError_)) {
                    uploadContext.discard(device);
                    destroyMeshBuffers(device);
                    return false;
                }
                const VkDeviceSize edgeIndexSize =
                    static_cast<VkDeviceSize>(edgeIndices.size() * sizeof(uint32_t));
                if (!uploadContext.uploadBuffer(device,
                                                meshResources_.edgeIndexResource,
                                                edgeIndices.data(),
                                                edgeIndexSize,
                                                VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                                "edge index",
                                                lastError_)) {
                    uploadContext.discard(device);
                    destroyMeshBuffers(device);
                    return false;
                }
                meshResources_.edgeIndexCount = static_cast<uint32_t>(edgeIndices.size());
            }
        }
    }

    if (!uploadContext.submit(device, commandResource_.pool(), device.graphicsQueue(), lastError_)) {
        destroyMeshBuffers(device);
        return false;
    }

    meshResources_.meshIndexCount = static_cast<uint32_t>(indices.size());
    meshResources_.meshScalarCount = static_cast<uint32_t>(expandedScalars.size());

    if (!vertices.empty() && !indices.empty()) {
        const VkDeviceSize scalarSize = static_cast<VkDeviceSize>(
            std::max<size_t>(expandedScalars.size(), 1) * sizeof(float));
        const void* scalarData = expandedScalars.empty()
            ? static_cast<const void*>(nullptr)
            : static_cast<const void*>(expandedScalars.data());
        float zeroScalar = 0.0f;
        if (expandedScalars.empty()) {
            scalarData = &zeroScalar;
        }
        if (!meshResources_.meshScalarResource.uploadHostVisible(device,
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
    if (!meshResources_.meshScalarResource.isValid() || meshResources_.meshScalarSourceIndices.empty()) {
        lastError_ = QStringLiteral("Vulkan scalar storage buffer is not initialized");
        return false;
    }

    vkDeviceWaitIdle(vkDevice);
    meshResources_.meshUseVertexScalars = useScalars && !scalars.empty();
    meshResources_.meshScalarMin = minVal;
    meshResources_.meshScalarMax = maxVal;
    meshResources_.meshNumBands = std::max(1, numBands);

    std::vector<float> expandedScalars(meshResources_.meshScalarSourceIndices.size(), 0.0f);
    for (size_t i = 0; i < meshResources_.meshScalarSourceIndices.size(); ++i) {
        const uint32_t sourceIndex = meshResources_.meshScalarSourceIndices[i];
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
    return meshResources_.meshScalarResource.updateHostVisible(device,
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
    return uploadLineVerticesDeviceLocal(device,
                                         selectionLineVertexResource_,
                                         selectionLineVertexCount_,
                                         lineVertices,
                                         "Selection line vertex");
}

void VulkanClearFrameRenderer::setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor)
{
    backgroundTopColor_ = topColor;
    backgroundBottomColor_ = bottomColor;
}

void VulkanClearFrameRenderer::setViewportGridVisible(bool visible)
{
    viewportGridVisible_ = visible;
}

void VulkanClearFrameRenderer::setViewportGridParams(float alpha, float minorStep, float fineAlpha)
{
    viewportGridVisible_ = alpha > 0.0f;
    viewportGridMinorStep_ = minorStep;
    viewportGridFineAlpha_ = fineAlpha;
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
    return uploadLineVerticesDeviceLocal(device,
                                         overlayLineVertexResource_,
                                         overlayLineVertexCount_,
                                         lineVertices,
                                         "Overlay line vertex");
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
    return uploadLineVerticesDeviceLocal(device,
                                         sliceLineVertexResource_,
                                         sliceLineVertexCount_,
                                         lineVertices,
                                         "Slice line vertex");
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

    VulkanStagingUploadContext uploadContext;
    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanMeshVertex));
    if (!uploadContext.uploadBuffer(device,
                                    isoSurfaceVertexResource_,
                                    vertices.data(),
                                    vertexSize,
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    "iso surface vertex",
                                    lastError_)) {
        uploadContext.discard(device);
        destroyIsoSurfaceBuffers(device);
        return false;
    }

    const VkDeviceSize indexSize = static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t));
    if (!uploadContext.uploadBuffer(device,
                                    isoSurfaceIndexResource_,
                                    mesh.indices.data(),
                                    indexSize,
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    "iso surface index",
                                    lastError_)) {
        uploadContext.discard(device);
        destroyIsoSurfaceBuffers(device);
        return false;
    }
    if (!uploadContext.submit(device, commandResource_.pool(), device.graphicsQueue(), lastError_)) {
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

    VulkanStagingUploadContext uploadContext;
    const VkDeviceSize vertexSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanMeshVertex));
    if (!uploadContext.uploadBuffer(device,
                                    clipPreviewVertexResource_,
                                    vertices.data(),
                                    vertexSize,
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    "clip preview vertex",
                                    lastError_)) {
        uploadContext.discard(device);
        destroyClipPreviewBuffers(device);
        return false;
    }

    const VkDeviceSize indexSize = static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t));
    if (!uploadContext.uploadBuffer(device,
                                    clipPreviewIndexResource_,
                                    mesh.indices.data(),
                                    indexSize,
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                    "clip preview index",
                                    lastError_)) {
        uploadContext.discard(device);
        destroyClipPreviewBuffers(device);
        return false;
    }

    if (!mesh.edgeVertices.empty()) {
        if (mesh.edgeVertices.size() % 3 != 0) {
            lastError_ = QStringLiteral("Clip preview line vertex data is not position triplets");
            uploadContext.discard(device);
            destroyClipPreviewBuffers(device);
            return false;
        }
        const VkDeviceSize lineSize = static_cast<VkDeviceSize>(mesh.edgeVertices.size() * sizeof(float));
        if (!uploadContext.uploadBuffer(device,
                                        clipPreviewLineVertexResource_,
                                        mesh.edgeVertices.data(),
                                        lineSize,
                                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                        "clip preview line vertex",
                                        lastError_)) {
            uploadContext.discard(device);
            destroyClipPreviewBuffers(device);
            return false;
        }
        clipPreviewLineVertexCount_ = static_cast<uint32_t>(mesh.edgeVertices.size() / 3);
    }

    if (!uploadContext.submit(device, commandResource_.pool(), device.graphicsQueue(), lastError_)) {
        destroyClipPreviewBuffers(device);
        return false;
    }
    clipPreviewIndexCount_ = static_cast<uint32_t>(mesh.indices.size());
    return true;
}

bool VulkanClearFrameRenderer::renderMeshFrame(
    const VulkanDevice& device,
    const VulkanSwapchain& swapchain,
    const VkClearColorValue& clearColor,
    const QMatrix4x4& mvp,
    const QMatrix4x4& axesMvp,
    ModelDisplayMode displayMode)
{
    lastError_.clear();
    swapchainOutOfDate_ = false;
    if (!isInitialized() || pipelines_.mesh.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan mesh pipeline is not initialized");
        return false;
    }
    if (!meshResources_.meshVertexResource.isValid() || !meshResources_.meshIndexResource.isValid() || meshResources_.meshIndexCount == 0) {
        return renderClearFrame(device, swapchain, clearColor, axesMvp);
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    if (!acquireSwapchainImage(device, swapchain, imageIndex)) {
        return false;
    }
    if (imageIndex >= swapchainFrameResources_.count()) {
        lastError_ = QStringLiteral("Swapchain image index is out of range");
        return false;
    }

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    if (!recordMeshCommandBuffer(commandResource_.buffer(),
                                 swapchainFrameResources_.framebuffer(imageIndex),
                                 swapchain.extent(),
                                 clearColor,
                                 mvp,
                                 axesMvp,
                                 displayMode)) {
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
    if ((pipelines_.line.pipeline() == VK_NULL_HANDLE && pipelines_.isoSurface.pipeline() == VK_NULL_HANDLE) ||
        extent.width == 0 ||
        extent.height == 0) {
        return;
    }

    const uint32_t margin = 8;
    const uint32_t availableWidth = extent.width > margin * 2 ? extent.width - margin * 2 : 0;
    const uint32_t availableHeight = extent.height > margin * 2 ? extent.height - margin * 2 : 0;
    const uint32_t axesSize = std::min<uint32_t>(152, std::min(availableWidth, availableHeight));
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

    VkDeviceSize offsets[] = {0};
    if (pipelines_.isoSurface.pipeline() != VK_NULL_HANDLE &&
        axesSolidVertexResource_.isValid() &&
        axesSolidVertexCount_ > 0) {
        std::array<float, 20> pushConstants{};
        std::memcpy(pushConstants.data(), axesMvp.constData(), 16 * sizeof(float));
        pushConstants[16] = 0.0f;
        pushConstants[17] = 0.0f;
        pushConstants[18] = 0.0f;
        pushConstants[19] = 1.0f;
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.isoSurface.pipeline());
        vkCmdPushConstants(commandBuffer,
                           pipelines_.isoSurface.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           static_cast<uint32_t>(pushConstants.size() * sizeof(float)),
                           pushConstants.data());
        VkBuffer axesSolidVertexBuffer = axesSolidVertexResource_.buffer();
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &axesSolidVertexBuffer, offsets);
        vkCmdDraw(commandBuffer, axesSolidVertexCount_, 1, 0, 0);
    }

    if (pipelines_.line.pipeline() == VK_NULL_HANDLE ||
        !axesLineVertexResource_.isValid() ||
        axesLineVertexCount_ < 6) {
        return;
    }

    const std::array<std::array<float, 4>, 3> axisColors = {{
        {{0.95f, 0.30f, 0.30f, 1.0f}},
        {{0.35f, 0.90f, 0.35f, 1.0f}},
        {{0.35f, 0.55f, 1.00f, 1.0f}}
    }};

    const uint32_t axisVertexCount = axesLineVertexCount_ / 3;
    if (axisVertexCount < 2) {
        return;
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.line.pipeline());
    VkBuffer axesVertexBuffer = axesLineVertexResource_.buffer();
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &axesVertexBuffer, offsets);

    for (uint32_t axis = 0; axis < 3; ++axis) {
        std::array<float, 24> pushConstants{};
        std::memcpy(pushConstants.data(), axesMvp.constData(), 16 * sizeof(float));
        pushConstants[16] = axisColors[axis][0];
        pushConstants[17] = axisColors[axis][1];
        pushConstants[18] = axisColors[axis][2];
        pushConstants[19] = axisColors[axis][3];
        pushConstants[20] = 0.0f;
        pushConstants[21] = 1.0f;
        pushConstants[22] = 1.0f;
        pushConstants[23] = 0.0f;
        vkCmdPushConstants(commandBuffer,
                           pipelines_.line.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0,
                           static_cast<uint32_t>(pushConstants.size() * sizeof(float)),
                           pushConstants.data());
        vkCmdDraw(commandBuffer, axisVertexCount, 1, axis * axisVertexCount, 0);
    }
}

void VulkanClearFrameRenderer::recordBackground(VkCommandBuffer commandBuffer, VkExtent2D extent)
{
    if (pipelines_.background.pipeline() == VK_NULL_HANDLE || extent.width == 0 || extent.height == 0) {
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
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.background.pipeline());
    std::array<float, 12> pushConstants = {
        backgroundBottomColor_.x(), backgroundBottomColor_.y(), backgroundBottomColor_.z(), 1.0f,
        backgroundTopColor_.x(), backgroundTopColor_.y(), backgroundTopColor_.z(), 1.0f,
        viewportGridVisible_ ? 1.0f : 0.0f, viewportGridMinorStep_, viewportGridFineAlpha_, 0.0f
    };
    vkCmdPushConstants(commandBuffer,
                       pipelines_.background.layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0,
                       static_cast<uint32_t>(pushConstants.size() * sizeof(float)),
                       pushConstants.data());
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

bool VulkanClearFrameRenderer::renderPickFrame(
    const VulkanDevice& device,
    const QMatrix4x4& mvp,
    uint32_t width,
    uint32_t height)
{
    lastError_.clear();
    if (!isInitialized() || pipelines_.pick.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan pick pipeline is not initialized");
        return false;
    }
    if (!meshResources_.meshVertexResource.isValid() || !meshResources_.meshIndexResource.isValid() || meshResources_.meshIndexCount == 0) {
        lastError_ = QStringLiteral("Vulkan mesh buffers are not initialized");
        return false;
    }

    width = std::max<uint32_t>(1, width);
    height = std::max<uint32_t>(1, height);
    if (!pickResources_.create(device, pickRenderPass_.handle(), depthFormat_, width, height, lastError_)) {
        return false;
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    VulkanPickPass::Resources pickResources;
    pickResources.renderPass = pickRenderPass_.handle();
    pickResources.framebuffer = pickResources_.framebuffer();
    pickResources.colorImage = pickResources_.colorImage();
    pickResources.pipeline = &pipelines_.pick;
    pickResources.meshVertexResource = &meshResources_.meshVertexResource;
    pickResources.meshIndexResource = &meshResources_.meshIndexResource;
    pickResources.meshIndexCount = meshResources_.meshIndexCount;
    if (!VulkanPickPass::record(commandResource_.buffer(),
                                pickResources_.extent(),
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
    if (!isInitialized() || pipelines_.pick.pipeline() == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan pick pipeline is not initialized");
        return false;
    }
    if (!meshResources_.meshVertexResource.isValid() || !meshResources_.meshIndexResource.isValid() || meshResources_.meshIndexCount == 0) {
        lastError_ = QStringLiteral("Vulkan mesh buffers are not initialized");
        return false;
    }

    width = std::max<uint32_t>(1, width);
    height = std::max<uint32_t>(1, height);
    x = std::min(x, width - 1);
    y = std::min(y, height - 1);

    if (!pickResources_.create(device, pickRenderPass_.handle(), depthFormat_, width, height, lastError_)) {
        return false;
    }

    unsigned char pixel[4] = {0, 0, 0, 0};
    if (!pickResources_.ensureReadbackBuffer(device, lastError_)) {
        return false;
    }

    VkDevice vkDevice = device.device();
    vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);

    if (!commandResource_.resetCommandBuffer(device, lastError_)) {
        return false;
    }
    VulkanPickPass::Resources pickResources;
    pickResources.renderPass = pickRenderPass_.handle();
    pickResources.framebuffer = pickResources_.framebuffer();
    pickResources.colorImage = pickResources_.colorImage();
    pickResources.pipeline = &pipelines_.pick;
    pickResources.meshVertexResource = &meshResources_.meshVertexResource;
    pickResources.meshIndexResource = &meshResources_.meshIndexResource;
    pickResources.meshIndexCount = meshResources_.meshIndexCount;
    if (!VulkanPickPass::record(commandResource_.buffer(),
                                pickResources_.extent(),
                                mvp,
                                pickResources,
                                pickResources_.readbackBuffer(),
                                x,
                                y,
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
        lastError_ = QStringLiteral("vkQueueSubmit(pick readback) failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    result = vkWaitForFences(vkDevice, 1, &inFlightFence_, VK_TRUE, UINT64_MAX);
    if (result != VK_SUCCESS) {
        lastError_ = QStringLiteral("vkWaitForFences(pick readback) failed: ") + VulkanContext::formatResult(result);
        return false;
    }

    if (!pickResources_.readPixel(device, pixel, lastError_)) {
        return false;
    }

    elementId = colorToId(pixel[0], pixel[1], pixel[2]);
    return true;
}

bool VulkanClearFrameRenderer::createRenderPass(const VulkanDevice& device, VkFormat imageFormat)
{
    depthFormat_ = VulkanDepthResource::findDepthFormat(device);
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
    return depthResource_.create(device,
                                 swapchain.extent().width,
                                 swapchain.extent().height,
                                 depthFormat_,
                                 "main",
                                 lastError_);
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
    VkPushConstantRange backgroundPushRange{};
    backgroundPushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    backgroundPushRange.offset = 0;
    backgroundPushRange.size = sizeof(float) * 12;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &backgroundPushRange;

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

    const bool pipelineCreated = pipelines_.background.createGraphics(
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

    const bool pipelineCreated = pipelines_.triangle.createGraphics(
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

    const bool pipelineCreated = pipelines_.mesh.createGraphics(
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

    const bool pipelineCreated = pipelines_.isoSurface.createGraphics(
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
    binding.stride = sizeof(VulkanLineVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(VulkanLineVertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32_SFLOAT;
    attributes[1].offset = offsetof(VulkanLineVertex, scalar);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

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
    pushConstantRange.size = 24 * sizeof(float);

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

    const bool pipelineCreated = pipelines_.line.createGraphics(
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

bool VulkanClearFrameRenderer::createPointGraphicsPipeline(const VulkanDevice& device)
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
    binding.stride = sizeof(VulkanMeshVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attributes{};
    attributes[0].binding = 0;
    attributes[0].location = 0;
    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributes[0].offset = offsetof(VulkanMeshVertex, position);
    attributes[1].binding = 0;
    attributes[1].location = 1;
    attributes[1].format = VK_FORMAT_R32_SFLOAT;
    attributes[1].offset = offsetof(VulkanMeshVertex, color);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

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
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

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
    pushConstantRange.size = 24 * sizeof(float);

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

    const bool pipelineCreated = pipelines_.point.createGraphics(
        device, pipelineLayoutInfo, pipelineInfo, "point", lastError_);

    vkDestroyShaderModule(device.device(), fragmentShader, nullptr);
    vkDestroyShaderModule(device.device(), vertexShader, nullptr);

    return pipelineCreated;
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

    const bool pipelineCreated = pipelines_.pick.createGraphics(
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

bool VulkanClearFrameRenderer::createMeshScalarDescriptor(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE || !meshResources_.meshScalarResource.isValid()) {
        lastError_ = QStringLiteral("Vulkan scalar storage buffer is not initialized");
        return false;
    }

    const VkDeviceSize range = static_cast<VkDeviceSize>(
        std::max<uint32_t>(meshResources_.meshScalarCount, 1) * sizeof(float));
    return meshResources_.meshScalarDescriptor.createStorageBufferSet(device,
                                                        meshScalarSetLayout_.handle(),
                                                        meshResources_.meshScalarResource.buffer(),
                                                        range,
                                                        lastError_);
}

bool VulkanClearFrameRenderer::createAxesIndicatorResource(const VulkanDevice& device)
{
    axesLineVertexResource_.destroy(device);
    axesLineVertexCount_ = 0;
    axesSolidVertexResource_.destroy(device);
    axesSolidVertexCount_ = 0;

    const std::vector<VulkanLineVertex> axesVertices = makeLineVertices(buildAxesLineVertices());
    if (!axesLineVertexResource_.uploadHostVisible(device,
                                                   axesVertices.data(),
                                                   static_cast<VkDeviceSize>(axesVertices.size() * sizeof(VulkanLineVertex)),
                                                   VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                   "axes indicator line vertex",
                                                   lastError_)) {
        return false;
    }
    axesLineVertexCount_ = static_cast<uint32_t>(axesVertices.size());

    const std::vector<VulkanMeshVertex> axesSolidVertices = buildAxesSolidVertices();
    if (!axesSolidVertexResource_.uploadHostVisible(device,
                                                    axesSolidVertices.data(),
                                                    static_cast<VkDeviceSize>(axesSolidVertices.size() * sizeof(VulkanMeshVertex)),
                                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                                    "axes indicator solid vertex",
                                                    lastError_)) {
        return false;
    }
    axesSolidVertexCount_ = static_cast<uint32_t>(axesSolidVertices.size());
    return true;
}

bool VulkanClearFrameRenderer::uploadLineVerticesDeviceLocal(
    const VulkanDevice& device,
    VulkanBufferResource& resource,
    uint32_t& vertexCount,
    const std::vector<float>& lineVertices,
    const char* debugName)
{
    resource.destroy(device);
    vertexCount = 0;

    if (lineVertices.empty()) {
        return true;
    }
    if (lineVertices.size() % 3 != 0) {
        lastError_ = QStringLiteral("%1 data is not position triplets")
            .arg(QString::fromUtf8(debugName));
        return false;
    }

    std::vector<VulkanLineVertex> vertices = makeLineVertices(lineVertices);
    VulkanStagingUploadContext uploadContext;
    const VkDeviceSize vertexSize =
        static_cast<VkDeviceSize>(vertices.size() * sizeof(VulkanLineVertex));
    if (!uploadContext.uploadBuffer(device,
                                    resource,
                                    vertices.data(),
                                    vertexSize,
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                    debugName,
                                    lastError_)) {
        uploadContext.discard(device);
        resource.destroy(device);
        return false;
    }
    if (!uploadContext.submit(device, commandResource_.pool(), device.graphicsQueue(), lastError_)) {
        resource.destroy(device);
        return false;
    }
    vertexCount = static_cast<uint32_t>(vertices.size());
    return true;
}

void VulkanClearFrameRenderer::destroyMeshBuffers(const VulkanDevice& device)
{
    VkDevice vkDevice = device.device();
    if (vkDevice == VK_NULL_HANDLE) {
        return;
    }
    meshResources_.destroy(device);
    selectionLineVertexResource_.destroy(device);
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

bool VulkanClearFrameRenderer::createFramebuffers(const VulkanDevice& device, const VulkanSwapchain& swapchain)
{
    return swapchainFrameResources_.create(device,
                                           swapchain,
                                           renderPass_.handle(),
                                           depthResource_.imageView(),
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
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines_.triangle.pipeline());
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
    const QMatrix4x4& axesMvp,
    ModelDisplayMode displayMode)
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
    meshFrameResources.meshPipeline = &pipelines_.mesh;
    meshFrameResources.isoSurfacePipeline = &pipelines_.isoSurface;
    meshFrameResources.linePipeline = &pipelines_.line;
    meshFrameResources.pointPipeline = &pipelines_.point;
    meshFrameResources.displayMode = displayMode;
    meshFrameResources.meshScalarDescriptor = &meshResources_.meshScalarDescriptor;
    meshFrameResources.meshVertexResource = &meshResources_.meshVertexResource;
    meshFrameResources.meshIndexResource = &meshResources_.meshIndexResource;
    meshFrameResources.meshIndexCount = meshResources_.meshIndexCount;
    meshFrameResources.meshUseVertexScalars = meshResources_.meshUseVertexScalars;
    meshFrameResources.meshScalarMin = meshResources_.meshScalarMin;
    meshFrameResources.meshScalarMax = meshResources_.meshScalarMax;
    meshFrameResources.meshNumBands = meshResources_.meshNumBands;
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
    meshFrameResources.edgeVertexResource = &meshResources_.edgeVertexResource;
    meshFrameResources.edgeIndexResource = &meshResources_.edgeIndexResource;
    meshFrameResources.edgeIndexCount = meshResources_.edgeIndexCount;
    meshFrameResources.edgeUseVertexScalars = meshResources_.edgeUseVertexScalars;
    meshFrameResources.edgeScalarMin = meshResources_.edgeScalarMin;
    meshFrameResources.edgeScalarMax = meshResources_.edgeScalarMax;
    meshFrameResources.edgeNumBands = meshResources_.edgeNumBands;
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
