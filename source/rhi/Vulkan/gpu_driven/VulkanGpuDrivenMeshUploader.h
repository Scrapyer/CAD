#pragma once

#include <QString>
#include <vulkan/vulkan.h>

class VulkanDevice;
class VulkanGpuDrivenMeshResources;
class VulkanStagingUploadContext;
struct VulkanGpuDrivenUploadData;
struct VulkanGpuDrivenUploadV2Data;
struct VulkanGpuDrivenVisibilityStateData;

/**
 * @brief 将 GPU-driven 上传数据写入 Vulkan buffer 资源组。
 */
bool uploadVulkanGpuDrivenMeshResources(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    VulkanStagingUploadContext& uploadContext,
    const VulkanGpuDrivenUploadData& uploadData,
    VkDescriptorSetLayout scalarSetLayout,
    bool uploadSurfaceV1,
    QString& lastError);

bool uploadVulkanGpuDrivenMeshV2SidecarResources(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    VulkanStagingUploadContext& uploadContext,
    const VulkanGpuDrivenUploadV2Data& uploadData,
    QString& lastError);

bool uploadVulkanGpuDrivenMeshV2Resources(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    VulkanStagingUploadContext& uploadContext,
    const VulkanGpuDrivenUploadV2Data& uploadData,
    QString& lastError);

bool updateVulkanGpuDrivenVisibilityState(
    const VulkanDevice& device,
    VulkanGpuDrivenMeshResources& resources,
    const VulkanGpuDrivenVisibilityStateData& stateData,
    bool& descriptorDirty,
    QString& lastError);
