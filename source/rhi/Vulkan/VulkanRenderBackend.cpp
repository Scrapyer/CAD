#include "VulkanRenderBackend.h"

#include "VulkanContext.h"
#include "VulkanClearFrameRenderer.h"
#include "VulkanDevice.h"
#include "VulkanSwapchain.h"

VulkanRenderBackend::VulkanRenderBackend()
    : context_(std::make_unique<VulkanContext>()),
      device_(std::make_unique<VulkanDevice>()),
      swapchain_(std::make_unique<VulkanSwapchain>()),
      clearFrameRenderer_(std::make_unique<VulkanClearFrameRenderer>())
{
}

VulkanRenderBackend::~VulkanRenderBackend()
{
    destroySwapchain();
}

void VulkanRenderBackend::initialize()
{
    if (initialized_) {
        return;
    }

    initializeDevice();
}

bool VulkanRenderBackend::initializeContext(const std::vector<const char*>& requiredExtensions)
{
    if (context_->isInitialized()) {
        return true;
    }

    info_.renderer = QStringLiteral("Vulkan");
    info_.version = VulkanContext::formatVersion(VulkanContext::applicationApiVersion());
    info_.shadingLanguageVersion = QStringLiteral("SPIR-V");
    info_.vendor = QStringLiteral("Unknown");
    lastError_.clear();

    VulkanContext::CreateInfo createInfo;
    createInfo.requiredExtensions = requiredExtensions;
    if (!context_->initialize(createInfo)) {
        lastError_ = context_->lastError();
        info_.renderer = QStringLiteral("Vulkan unavailable");
        info_.version = lastError_;
        initialized_ = true;
        return false;
    }
    return true;
}

bool VulkanRenderBackend::initializeDevice(VkSurfaceKHR surface)
{
    lastError_.clear();

    if (!initializeContext()) {
        return false;
    }

    if (device_->isInitialized() && surface != VK_NULL_HANDLE && device_->surface() != surface) {
        if (swapchain_->isInitialized()) {
            swapchain_->destroy(*device_);
        }
        device_->destroy();
    }

    if (!device_->initialize(*context_, surface)) {
        lastError_ = device_->lastError();
        info_.renderer = QStringLiteral("No Vulkan physical device");
        info_.version = lastError_;
        initialized_ = true;
        return false;
    }

    info_ = device_->info();
    initialized_ = true;
    return true;
}

bool VulkanRenderBackend::initializeSwapchain(VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync)
{
    lastError_.clear();

    if (surface == VK_NULL_HANDLE) {
        lastError_ = QStringLiteral("Vulkan surface is null");
        return false;
    }

    if (!initializeDevice(surface)) {
        return false;
    }

    if (swapchain_->isInitialized()) {
        swapchain_->destroy(*device_);
    }

    VulkanSwapchain::CreateInfo createInfo;
    createInfo.surface = surface;
    createInfo.width = width;
    createInfo.height = height;
    createInfo.vsync = vsync;

    if (!swapchain_->initialize(*context_, *device_, createInfo)) {
        lastError_ = swapchain_->lastError();
        return false;
    }
    if (!clearFrameRenderer_->initialize(*device_, *swapchain_)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }

    return true;
}

bool VulkanRenderBackend::uploadMesh(const Mesh& mesh, const VulkanMeshUploadOptions& options)
{
    lastError_.clear();
    if (!device_ || !device_->isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->uploadMesh(*device_, mesh, options)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::uploadVertexScalars(
    const std::vector<float>& scalars,
    float minVal,
    float maxVal,
    int numBands,
    bool useScalars)
{
    lastError_.clear();
    if (!device_ || !device_->isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->uploadVertexScalars(
            *device_, scalars, minVal, maxVal, numBands, useScalars)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::uploadOverlayLines(const std::vector<float>& lineVertices)
{
    lastError_.clear();
    if (!device_ || !device_->isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->uploadOverlayLines(*device_, lineVertices)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::uploadSliceLines(const std::vector<float>& lineVertices)
{
    lastError_.clear();
    if (!device_ || !device_->isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->uploadSliceLines(*device_, lineVertices)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::uploadIsoSurfaceMesh(const Mesh& mesh)
{
    lastError_.clear();
    if (!device_ || !device_->isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->uploadIsoSurfaceMesh(*device_, mesh)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::uploadClipPreviewMesh(const Mesh& mesh)
{
    lastError_.clear();
    if (!device_ || !device_->isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->uploadClipPreviewMesh(*device_, mesh)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::uploadSelectionLines(const std::vector<float>& lineVertices)
{
    lastError_.clear();
    if (!device_ || !device_->isInitialized()) {
        lastError_ = QStringLiteral("Vulkan device is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->uploadSelectionLines(*device_, lineVertices)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

void VulkanRenderBackend::setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor)
{
    backgroundTopColor_ = topColor;
    backgroundBottomColor_ = bottomColor;
    if (clearFrameRenderer_) {
        clearFrameRenderer_->setBackgroundGradient(backgroundTopColor_, backgroundBottomColor_);
    }
}

void VulkanRenderBackend::setViewportGridVisible(bool visible)
{
    viewportGridVisible_ = visible;
    if (clearFrameRenderer_) {
        clearFrameRenderer_->setViewportGridParams(visible ? 1.0f : 0.0f,
                                                   viewportGridMinorStep_,
                                                   viewportGridFineAlpha_);
    }
}

void VulkanRenderBackend::setViewportGridParams(float alpha, float minorStep, float fineAlpha)
{
    viewportGridVisible_ = alpha > 0.0f;
    viewportGridMinorStep_ = minorStep;
    viewportGridFineAlpha_ = fineAlpha;
    if (clearFrameRenderer_) {
        clearFrameRenderer_->setViewportGridParams(alpha, minorStep, fineAlpha);
    }
}

bool VulkanRenderBackend::renderClearFrame(float red,
                                          float green,
                                          float blue,
                                          float alpha,
                                          const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    if (!hasSwapchain()) {
        lastError_ = QStringLiteral("Vulkan swapchain is not initialized");
        return false;
    }

    VkClearColorValue clearColor{};
    clearColor.float32[0] = red;
    clearColor.float32[1] = green;
    clearColor.float32[2] = blue;
    clearColor.float32[3] = alpha;

    if (!clearFrameRenderer_->renderClearFrame(*device_, *swapchain_, clearColor, axesMvp)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::renderTriangleFrame(
    float red,
    float green,
    float blue,
    float alpha,
    const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    if (!hasSwapchain()) {
        lastError_ = QStringLiteral("Vulkan swapchain is not initialized");
        return false;
    }

    VkClearColorValue clearColor{};
    clearColor.float32[0] = red;
    clearColor.float32[1] = green;
    clearColor.float32[2] = blue;
    clearColor.float32[3] = alpha;

    if (!clearFrameRenderer_->renderTriangleFrame(*device_, *swapchain_, clearColor, axesMvp)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::renderMeshFrame(
    const QMatrix4x4& mvp,
    float red,
    float green,
    float blue,
    float alpha,
    const QMatrix4x4& axesMvp,
    ModelDisplayMode displayMode)
{
    lastError_.clear();
    if (!hasSwapchain()) {
        lastError_ = QStringLiteral("Vulkan swapchain is not initialized");
        return false;
    }

    VkClearColorValue clearColor{};
    clearColor.float32[0] = red;
    clearColor.float32[1] = green;
    clearColor.float32[2] = blue;
    clearColor.float32[3] = alpha;

    if (!clearFrameRenderer_->renderMeshFrame(*device_, *swapchain_, clearColor, mvp, axesMvp, displayMode)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::renderPickFrame(const QMatrix4x4& mvp, uint32_t width, uint32_t height)
{
    lastError_.clear();
    if (!hasSwapchain()) {
        lastError_ = QStringLiteral("Vulkan swapchain is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->renderPickFrame(*device_, mvp, width, height)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

bool VulkanRenderBackend::pickElementAt(
    const QMatrix4x4& mvp,
    uint32_t width,
    uint32_t height,
    uint32_t x,
    uint32_t y,
    int& elementId)
{
    lastError_.clear();
    if (!hasSwapchain()) {
        lastError_ = QStringLiteral("Vulkan swapchain is not initialized");
        return false;
    }
    if (!clearFrameRenderer_->pickElementAt(*device_, mvp, width, height, x, y, elementId)) {
        lastError_ = clearFrameRenderer_->lastError();
        return false;
    }
    return true;
}

void VulkanRenderBackend::destroySwapchain()
{
    if (device_ && device_->isInitialized() && clearFrameRenderer_) {
        clearFrameRenderer_->destroy(*device_);
    }
    if (swapchain_ && device_ && device_->isInitialized() && swapchain_->isInitialized()) {
        swapchain_->destroy(*device_);
    }
}

bool VulkanRenderBackend::hasSwapchain() const
{
    return swapchain_ && swapchain_->isInitialized();
}

bool VulkanRenderBackend::needsSwapchainRecreate() const
{
    return clearFrameRenderer_ && clearFrameRenderer_->needsSwapchainRecreate();
}

int VulkanRenderBackend::swapchainImageCount() const
{
    return swapchain_ ? static_cast<int>(swapchain_->images().size()) : 0;
}

VkInstance VulkanRenderBackend::instance() const
{
    return context_ ? context_->instance() : VK_NULL_HANDLE;
}

VkPhysicalDevice VulkanRenderBackend::physicalDevice() const
{
    return device_ ? device_->physicalDevice() : VK_NULL_HANDLE;
}

VkDevice VulkanRenderBackend::device() const
{
    return device_ ? device_->device() : VK_NULL_HANDLE;
}
