#pragma once

#include <QString>
#include <vulkan/vulkan.h>

class VulkanDevice;
class VulkanGpuDrivenMeshResources;

/**
 * @brief GPU-driven visibility compute pass。
 *
 * 负责创建 descriptor/pipeline，并在 command buffer 中录制可见三角筛选 dispatch。
 */
class VulkanVisibilityComputePass {
public:
    VulkanVisibilityComputePass() = default;
    ~VulkanVisibilityComputePass() = default;

    VulkanVisibilityComputePass(const VulkanVisibilityComputePass&) = delete;
    VulkanVisibilityComputePass& operator=(const VulkanVisibilityComputePass&) = delete;

    bool ensureInitialized(const VulkanDevice& device, QString& lastError);
    bool updateDescriptorSet(const VulkanDevice& device,
                             const VulkanGpuDrivenMeshResources& resources,
                             bool useSurfaceV2,
                             QString& lastError);
    void record(VkCommandBuffer commandBuffer,
                const VulkanGpuDrivenMeshResources& resources,
                bool useSurfaceV2) const;
    void destroy(const VulkanDevice& device);

    bool isInitialized() const
    {
        return descriptorSetLayout_ != VK_NULL_HANDLE &&
            pipelineLayout_ != VK_NULL_HANDLE &&
            pipeline_ != VK_NULL_HANDLE;
    }
    bool hasDescriptorSet() const { return descriptorSet_ != VK_NULL_HANDLE; }

private:
    bool createShaderModule(const VulkanDevice& device,
                            const QString& shaderPath,
                            VkShaderModule& shaderModule,
                            QString& lastError) const;
    bool createDescriptorSetLayout(const VulkanDevice& device, QString& lastError);
    bool createPipeline(const VulkanDevice& device,
                        const QString& shaderPath,
                        VkPipeline& pipeline,
                        const char* debugName,
                        QString& lastError);

    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline pipelineV2_ = VK_NULL_HANDLE;
};
