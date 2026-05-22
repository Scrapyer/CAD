#pragma once

#include <QString>

#include <vulkan/vulkan.h>

#include <vector>

/**
 * @brief Vulkan instance 生命周期封装。
 */
class VulkanContext {
public:
    struct CreateInfo {
        const char* applicationName = "FEModelViewer";
        const char* engineName = "FERender";
        std::vector<const char*> requiredExtensions;
        bool enablePortabilityEnumeration = true;
    };

    VulkanContext() = default;
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    bool initialize();
    bool initialize(const CreateInfo& createInfo);
    void destroy();

    bool isInitialized() const { return instance_ != VK_NULL_HANDLE; }
    VkInstance instance() const { return instance_; }
    uint32_t apiVersion() const { return apiVersion_; }
    const QString& apiVersionText() const { return apiVersionText_; }
    const QString& lastError() const { return lastError_; }

    static QString formatVersion(uint32_t version);
    static QString formatResult(VkResult result);
    static uint32_t applicationApiVersion();

private:
    VkInstance instance_ = VK_NULL_HANDLE;
    uint32_t apiVersion_ = VK_API_VERSION_1_0;
    QString apiVersionText_;
    QString lastError_;
};
