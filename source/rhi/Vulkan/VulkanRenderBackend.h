#pragma once

#include "RenderBackend.h"

#include <QMatrix4x4>
#include <QVector3D>

#include <vulkan/vulkan.h>

#include <memory>
#include <unordered_map>
#include <vector>

class VulkanContext;
class VulkanClearFrameRenderer;
class VulkanDevice;
class VulkanSwapchain;
struct Mesh;

struct VulkanMeshUploadOptions {
    QVector3D objectColor{0.48f, 0.72f, 0.76f};
    std::vector<int> triangleToElement;
    std::vector<int> triangleToPart;
    std::vector<int> edgeToPart;
    std::vector<QVector3D> partColors;
    std::unordered_map<int, bool> partVisibility;
    bool useVertexColor = false;
    std::vector<float> vertexColors;
    std::vector<float> vertexScalars;
    float scalarMin = 0.0f;
    float scalarMax = 1.0f;
    int numBands = 10;
};

/**
 * @brief Vulkan 渲染后端骨架。
 *
 * 当前负责 Vulkan instance、逻辑设备、基础队列、swapchain、主网格/边线管线和
 * 单像素拾取读回；descriptor、云图和后处理资源会在后续迁入。
 */
class VulkanRenderBackend final : public IRenderBackend {
public:
    VulkanRenderBackend();
    ~VulkanRenderBackend() override;

    VulkanRenderBackend(const VulkanRenderBackend&) = delete;
    VulkanRenderBackend& operator=(const VulkanRenderBackend&) = delete;

    void initialize() override;
    const RenderBackendInfo& info() const override { return info_; }

    bool initializeContext(const std::vector<const char*>& requiredExtensions = {});
    bool initializeDevice(VkSurfaceKHR surface = VK_NULL_HANDLE);
    bool initializeSwapchain(VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync = true);
    bool uploadMesh(const Mesh& mesh, const VulkanMeshUploadOptions& options = {});
    bool uploadVertexScalars(const std::vector<float>& scalars,
                             float minVal,
                             float maxVal,
                             int numBands,
                             bool useScalars);
    bool uploadOverlayLines(const std::vector<float>& lineVertices);
    bool uploadSliceLines(const std::vector<float>& lineVertices);
    bool uploadIsoSurfaceMesh(const Mesh& mesh);
    bool uploadClipPreviewMesh(const Mesh& mesh);
    bool uploadSelectionLines(const std::vector<float>& lineVertices);
    void setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor);
    void setViewportGridVisible(bool visible);
    void setViewportGridParams(float alpha, float minorStep, float fineAlpha);
    bool renderClearFrame(float red,
                         float green,
                         float blue,
                         float alpha = 1.0f,
                         const QMatrix4x4& axesMvp = QMatrix4x4());
    bool renderTriangleFrame(float red = 0.04f,
                             float green = 0.05f,
                             float blue = 0.07f,
                             float alpha = 1.0f,
                             const QMatrix4x4& axesMvp = QMatrix4x4());
    bool renderMeshFrame(const QMatrix4x4& mvp = QMatrix4x4(),
                         float red = 0.04f,
                         float green = 0.05f,
                         float blue = 0.07f,
                         float alpha = 1.0f,
                         const QMatrix4x4& axesMvp = QMatrix4x4(),
                         ModelDisplayMode displayMode = ModelDisplayMode::SolidWireframe);
    bool renderPickFrame(const QMatrix4x4& mvp, uint32_t width, uint32_t height);
    bool pickElementAt(const QMatrix4x4& mvp,
                       uint32_t width,
                       uint32_t height,
                       uint32_t x,
                       uint32_t y,
                       int& elementId);
    void destroySwapchain();

    bool isInitialized() const { return initialized_; }
    bool hasSwapchain() const;
    bool needsSwapchainRecreate() const;
    int swapchainImageCount() const;
    VkInstance instance() const;
    VkPhysicalDevice physicalDevice() const;
    VkDevice device() const;
    const QString& lastError() const { return lastError_; }

private:
    RenderBackendInfo info_;
    std::unique_ptr<VulkanContext> context_;
    std::unique_ptr<VulkanDevice> device_;
    std::unique_ptr<VulkanSwapchain> swapchain_;
    std::unique_ptr<VulkanClearFrameRenderer> clearFrameRenderer_;
    QVector3D backgroundTopColor_{0.38f, 0.45f, 0.58f};
    QVector3D backgroundBottomColor_{0.68f, 0.74f, 0.82f};
    bool viewportGridVisible_ = true;
    float viewportGridMinorStep_ = 24.0f;
    float viewportGridFineAlpha_ = 0.0f;
    bool initialized_ = false;
    QString lastError_;
};
