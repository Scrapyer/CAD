#pragma once

#include "VulkanBufferResource.h"
#include "VulkanCommandResource.h"
#include "VulkanDepthResource.h"
#include "VulkanDescriptorResource.h"
#include "VulkanDescriptorSetLayoutResource.h"
#include "VulkanFramePipelines.h"
#include "VulkanGpuDrivenMeshResources.h"
#include "VulkanGpuDrivenRuntimeStats.h"
#include "VulkanGpuDrivenTypes.h"
#include "VulkanMeshBufferResources.h"
#include "VulkanPickResources.h"
#include "VulkanRenderPassResource.h"
#include "VulkanSwapchainFrameResources.h"
#include "VulkanVisibilityComputePass.h"
#include "RenderBackend.h"

#include <QMatrix4x4>
#include <QString>
#include <QVector3D>

#include <vulkan/vulkan.h>

#include <vector>

struct Mesh;
struct VulkanMeshUploadOptions;
class VulkanDevice;
class VulkanSwapchain;

/**
 * @brief 最小 Vulkan 帧渲染器。
 *
 * 当前负责把 swapchain image 清成固定颜色，通过最小 graphics pipeline 绘制固定三角形，
 * 上传主网格/边线并用 push constant MVP 绘制传统 Vulkan 视口；拾取路径使用离屏
 * framebuffer 和 staging buffer 读回单像素颜色 ID。
 */
class VulkanClearFrameRenderer {
public:
    VulkanClearFrameRenderer() = default;
    ~VulkanClearFrameRenderer();

    VulkanClearFrameRenderer(const VulkanClearFrameRenderer&) = delete;
    VulkanClearFrameRenderer& operator=(const VulkanClearFrameRenderer&) = delete;

    bool initialize(const VulkanDevice& device, const VulkanSwapchain& swapchain);
    void destroy(const VulkanDevice& device);

    bool renderClearFrame(const VulkanDevice& device,
                         const VulkanSwapchain& swapchain,
                         const VkClearColorValue& clearColor,
                         const QMatrix4x4& axesMvp = QMatrix4x4());
    bool renderTriangleFrame(const VulkanDevice& device,
                             const VulkanSwapchain& swapchain,
                             const VkClearColorValue& clearColor,
                             const QMatrix4x4& axesMvp = QMatrix4x4());
    bool uploadMesh(const VulkanDevice& device, const Mesh& mesh, const VulkanMeshUploadOptions& options);
    bool updateGpuDrivenVisibilityState(const VulkanDevice& device,
                                        const VulkanMeshUploadOptions& options);
    bool uploadVertexScalars(const VulkanDevice& device,
                             const std::vector<float>& scalars,
                             float minVal,
                             float maxVal,
                             int numBands,
                             bool useScalars);
    bool uploadOverlayLines(const VulkanDevice& device, const std::vector<float>& lineVertices);
    bool uploadSliceLines(const VulkanDevice& device, const std::vector<float>& lineVertices);
    bool uploadIsoSurfaceMesh(const VulkanDevice& device, const Mesh& mesh);
    bool uploadClipPreviewMesh(const VulkanDevice& device, const Mesh& mesh);
    bool uploadSelectionLines(const VulkanDevice& device, const std::vector<float>& lineVertices);
    void setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor);
    void setViewportGridVisible(bool visible);
    void setViewportGridParams(float alpha, float minorStep, float fineAlpha);
    void setDevicePixelRatio(float devicePixelRatio);
    bool renderMeshFrame(const VulkanDevice& device,
                         const VulkanSwapchain& swapchain,
                         const VkClearColorValue& clearColor,
                         const QMatrix4x4& mvp,
                         const QMatrix4x4& axesMvp = QMatrix4x4(),
                         ModelDisplayMode displayMode = ModelDisplayMode::SolidWireframe);
    bool renderPickFrame(const VulkanDevice& device,
                         const QMatrix4x4& mvp,
                         uint32_t width,
                         uint32_t height);
    bool pickElementAt(const VulkanDevice& device,
                       const QMatrix4x4& mvp,
                       uint32_t width,
                       uint32_t height,
                       uint32_t x,
                       uint32_t y,
                       int& elementId);

    bool isInitialized() const { return renderPass_.isValid(); }
    bool needsSwapchainRecreate() const { return swapchainOutOfDate_; }
    const QString& lastError() const { return lastError_; }
    QString gpuDrivenDiagnostics() const;

private:
    bool createRenderPass(const VulkanDevice& device, VkFormat imageFormat);
    bool createDepthResources(const VulkanDevice& device, const VulkanSwapchain& swapchain);
    bool createBackgroundGraphicsPipeline(const VulkanDevice& device);
    bool createGraphicsPipeline(const VulkanDevice& device);
    bool createMeshGraphicsPipeline(const VulkanDevice& device);
    bool createGpuDrivenSurfaceDescriptorLayout(const VulkanDevice& device);
    bool createGpuDrivenMeshGraphicsPipeline(const VulkanDevice& device);
    bool createGpuDrivenPointGraphicsPipeline(const VulkanDevice& device);
    bool createPointGraphicsPipeline(const VulkanDevice& device);
    bool createIsoSurfaceGraphicsPipeline(const VulkanDevice& device);
    bool createLineGraphicsPipeline(const VulkanDevice& device);
    bool createAxesGraphicsPipeline(const VulkanDevice& device);
    bool createPickRenderPass(const VulkanDevice& device);
    bool createPickGraphicsPipeline(const VulkanDevice& device);
    bool createGpuDrivenPickGraphicsPipeline(const VulkanDevice& device);
    bool createMeshScalarDescriptor(const VulkanDevice& device);
    bool createGpuDrivenSurfaceDescriptor(const VulkanDevice& device);
    bool createAxesIndicatorResource(const VulkanDevice& device);
    bool createShaderModule(const VulkanDevice& device, const QString& shaderPath, VkShaderModule& shaderModule);
    bool uploadLineVerticesDeviceLocal(const VulkanDevice& device,
                                       VulkanBufferResource& resource,
                                       uint32_t& vertexCount,
                                       const std::vector<float>& lineVertices,
                                       const char* debugName);
    void destroyMeshBuffers(const VulkanDevice& device);
    void destroyIsoSurfaceBuffers(const VulkanDevice& device);
    void destroyClipPreviewBuffers(const VulkanDevice& device);
    bool createFramebuffers(const VulkanDevice& device, const VulkanSwapchain& swapchain);
    bool createCommandPool(const VulkanDevice& device);
    bool createSyncObjects(const VulkanDevice& device);
    bool recordCommandBuffer(VkCommandBuffer commandBuffer,
                             VkFramebuffer framebuffer,
                             VkExtent2D extent,
                             const VkClearColorValue& clearColor,
                             bool drawTriangle,
                             const QMatrix4x4& axesMvp = QMatrix4x4());
    bool recordMeshCommandBuffer(VkCommandBuffer commandBuffer,
                                 VkFramebuffer framebuffer,
                                 VkExtent2D extent,
                                 const VkClearColorValue& clearColor,
                                 const QMatrix4x4& mvp,
                                 const QMatrix4x4& axesMvp = QMatrix4x4(),
                                 ModelDisplayMode displayMode = ModelDisplayMode::SolidWireframe);
    bool prepareGpuDrivenVisibilityPass(const VulkanDevice& device);
    bool updateGpuDrivenFrameUniforms(const VulkanDevice& device,
                                      const QMatrix4x4& mvp,
                                      bool enableFrustumCulling,
                                      bool enablePointOutput);
    bool waitForInFlightAndCollectGpuDrivenStats(const VulkanDevice& device,
                                                 const char* label);
    void collectGpuDrivenObservability(const VulkanDevice& device);
    void ensureGpuDrivenTimestampQueryPool(const VulkanDevice& device);
    void recordGpuDrivenVisibilityPass(VkCommandBuffer commandBuffer);
    bool canUseGpuDrivenIndirect(const VulkanDevice& device,
                                 bool hasSurfaceMesh,
                                 QString& reason) const;
    bool shouldUseGpuDrivenIndirect() const;
    void setGpuDrivenFallback(const QString& reason);
    void syncGpuDrivenStatsFromResources();
    bool acquireSwapchainImage(const VulkanDevice& device,
                               const VulkanSwapchain& swapchain,
                               uint32_t& imageIndex);
    bool presentSwapchainImage(const VulkanDevice& device,
                               const VulkanSwapchain& swapchain,
                               uint32_t imageIndex);
    void recordAxesIndicator(VkCommandBuffer commandBuffer,
                             VkExtent2D extent,
                             const QMatrix4x4& axesMvp);
    void recordBackground(VkCommandBuffer commandBuffer, VkExtent2D extent);
    int colorToId(unsigned char r, unsigned char g, unsigned char b) const;

    VulkanSwapchainFrameResources swapchainFrameResources_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VulkanDepthResource depthResource_;
    VulkanRenderPassResource renderPass_;
    VulkanFramePipelines pipelines_;
    VulkanDescriptorSetLayoutResource meshScalarSetLayout_;
    VulkanDescriptorSetLayoutResource gpuDrivenSurfaceSetLayout_;
    VulkanRenderPassResource pickRenderPass_;
    VulkanPickResources pickResources_;
    VulkanMeshBufferResources meshResources_;
    VulkanGpuDrivenMeshResources gpuDrivenMeshResources_;
    VulkanVisibilityComputePass visibilityComputePass_;
    VulkanBufferResource isoSurfaceVertexResource_;
    VulkanBufferResource isoSurfaceIndexResource_;
    uint32_t isoSurfaceIndexCount_ = 0;
    VulkanBufferResource clipPreviewVertexResource_;
    VulkanBufferResource clipPreviewIndexResource_;
    uint32_t clipPreviewIndexCount_ = 0;
    VulkanBufferResource clipPreviewLineVertexResource_;
    uint32_t clipPreviewLineVertexCount_ = 0;
    VulkanBufferResource overlayLineVertexResource_;
    uint32_t overlayLineVertexCount_ = 0;
    VulkanBufferResource sliceLineVertexResource_;
    uint32_t sliceLineVertexCount_ = 0;
    VulkanBufferResource selectionLineVertexResource_;
    uint32_t selectionLineVertexCount_ = 0;
    VulkanBufferResource axesLineVertexResource_;
    uint32_t axesLineVertexCount_ = 0;
    VulkanBufferResource axesSolidVertexResource_;
    uint32_t axesSolidVertexCount_ = 0;
    VulkanCommandResource commandResource_;
    VkQueryPool gpuDrivenTimestampQueryPool_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore_ = VK_NULL_HANDLE;
    VkFence inFlightFence_ = VK_NULL_HANDLE;
    bool swapchainOutOfDate_ = false;
    QVector3D backgroundTopColor_{0.38f, 0.45f, 0.58f};
    QVector3D backgroundBottomColor_{0.68f, 0.74f, 0.82f};
    bool viewportGridVisible_ = true;
    float viewportGridMinorStep_ = 24.0f;
    float viewportGridFineAlpha_ = 0.0f;
    float devicePixelRatio_ = 1.0f;
    bool gpuDrivenRequestedForMesh_ = false;
    bool gpuDrivenActive_ = false;
    bool gpuDrivenUseSurfaceV2_ = false;
    bool gpuDrivenUseVertexColor_ = false;
    bool gpuDrivenPointOutputEnabled_ = false;
    bool gpuDrivenVisibilityDescriptorDirty_ = true;
    bool gpuDrivenTimestampQueryPending_ = false;
    bool gpuDrivenReadbackPending_ = false;
    VulkanGpuDrivenRuntimeStats gpuDrivenStats_;
    std::vector<VulkanGpuDrivenSourceVertex> gpuDrivenSourceVertexCpuCache_;
    QString gpuDrivenFallbackReason_;
    QString lastError_;
};
