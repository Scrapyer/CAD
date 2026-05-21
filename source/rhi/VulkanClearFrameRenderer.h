#pragma once

#include "VulkanBufferResource.h"
#include "VulkanCommandResource.h"
#include "VulkanDescriptorResource.h"
#include "VulkanDescriptorSetLayoutResource.h"
#include "VulkanFramebufferResource.h"
#include "VulkanPipelineResource.h"
#include "VulkanRenderPassResource.h"

#include <QString>
#include <QMatrix4x4>

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
                          const VkClearColorValue& clearColor);
    bool renderTriangleFrame(const VulkanDevice& device,
                             const VulkanSwapchain& swapchain,
                             const VkClearColorValue& clearColor,
                             const QMatrix4x4& axesMvp = QMatrix4x4());
    bool uploadMesh(const VulkanDevice& device, const Mesh& mesh, const VulkanMeshUploadOptions& options);
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
    bool renderMeshFrame(const VulkanDevice& device,
                         const VulkanSwapchain& swapchain,
                         const VkClearColorValue& clearColor,
                         const QMatrix4x4& mvp,
                         const QMatrix4x4& axesMvp = QMatrix4x4());
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

private:
    bool createImageViews(const VulkanDevice& device, const VulkanSwapchain& swapchain);
    bool createRenderPass(const VulkanDevice& device, VkFormat imageFormat);
    bool createDepthResources(const VulkanDevice& device, const VulkanSwapchain& swapchain);
    VkFormat findDepthFormat(const VulkanDevice& device) const;
    bool hasStencilComponent(VkFormat format) const;
    bool createBackgroundGraphicsPipeline(const VulkanDevice& device);
    bool createGraphicsPipeline(const VulkanDevice& device);
    bool createMeshGraphicsPipeline(const VulkanDevice& device);
    bool createIsoSurfaceGraphicsPipeline(const VulkanDevice& device);
    bool createLineGraphicsPipeline(const VulkanDevice& device);
    bool createPickRenderPass(const VulkanDevice& device);
    bool createPickGraphicsPipeline(const VulkanDevice& device);
    bool createPickResources(const VulkanDevice& device, uint32_t width, uint32_t height);
    void destroyPickResources(const VulkanDevice& device);
    bool createMeshScalarDescriptor(const VulkanDevice& device);
    bool createAxesIndicatorResource(const VulkanDevice& device);
    bool createShaderModule(const VulkanDevice& device, const QString& shaderPath, VkShaderModule& shaderModule);
    bool createImage(const VulkanDevice& device,
                     uint32_t width,
                     uint32_t height,
                     VkFormat format,
                     VkImageTiling tiling,
                     VkImageUsageFlags usage,
                     VkImage& image,
                     VkDeviceMemory& memory);
    bool createImageView(const VulkanDevice& device,
                         VkImage image,
                         VkFormat format,
                         VkImageAspectFlags aspectMask,
                         VkImageView& imageView);
    uint32_t findMemoryType(const VulkanDevice& device,
                            uint32_t typeFilter,
                            VkMemoryPropertyFlags properties) const;
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
                                 const QMatrix4x4& axesMvp = QMatrix4x4());
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

    std::vector<VkImageView> imageViews_;
    VulkanFramebufferResource swapchainFramebuffers_;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory_ = VK_NULL_HANDLE;
    VkImageView depthImageView_ = VK_NULL_HANDLE;
    VulkanRenderPassResource renderPass_;
    VulkanPipelineResource backgroundPipeline_;
    VulkanPipelineResource trianglePipeline_;
    VulkanPipelineResource meshPipeline_;
    VulkanPipelineResource isoSurfacePipeline_;
    VulkanDescriptorSetLayoutResource meshScalarSetLayout_;
    VulkanDescriptorResource meshScalarDescriptor_;
    VulkanPipelineResource linePipeline_;
    VulkanRenderPassResource pickRenderPass_;
    VulkanPipelineResource pickPipeline_;
    VkImage pickColorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory pickColorMemory_ = VK_NULL_HANDLE;
    VkImageView pickColorImageView_ = VK_NULL_HANDLE;
    VkImage pickDepthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory pickDepthMemory_ = VK_NULL_HANDLE;
    VkImageView pickDepthImageView_ = VK_NULL_HANDLE;
    VulkanFramebufferResource pickFramebuffer_;
    VkExtent2D pickExtent_ = {0, 0};
    VulkanBufferResource meshVertexResource_;
    VulkanBufferResource meshIndexResource_;
    uint32_t meshIndexCount_ = 0;
    bool meshUseVertexScalars_ = false;
    float meshScalarMin_ = 0.0f;
    float meshScalarMax_ = 1.0f;
    int meshNumBands_ = 10;
    std::vector<uint32_t> meshScalarSourceIndices_;
    VulkanBufferResource meshScalarResource_;
    uint32_t meshScalarCount_ = 0;
    VulkanBufferResource edgeVertexResource_;
    VulkanBufferResource edgeIndexResource_;
    uint32_t edgeIndexCount_ = 0;
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
    VulkanCommandResource commandResource_;
    VkSemaphore imageAvailableSemaphore_ = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore_ = VK_NULL_HANDLE;
    VkFence inFlightFence_ = VK_NULL_HANDLE;
    bool swapchainOutOfDate_ = false;
    QString lastError_;
};
