#include "RenderBackendFactory.h"
#include "RenderSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        return 1;
    }

    qputenv("FEMODELVIEWER_CONFIG_DIR", settingsDir.path().toLocal8Bit());

    if (RenderSettings::preferredBackend() != RenderBackendKind::OpenGL) {
        return 2;
    }
    if (RenderSettings::backendKey(RenderBackendKind::OpenGL) != QStringLiteral("opengl")) {
        return 3;
    }
    if (RenderSettings::backendKey(RenderBackendKind::Vulkan) != QStringLiteral("vulkan")) {
        return 4;
    }
    if (RenderSettings::backendKey(RenderBackendKind::Metal) != QStringLiteral("metal")) {
        return 11;
    }
    if (RenderSettings::backendFromKey(QStringLiteral("VK")) != RenderBackendKind::Vulkan) {
        return 5;
    }
    if (RenderSettings::backendFromKey(QStringLiteral("gl")) != RenderBackendKind::OpenGL) {
        return 6;
    }
    if (RenderSettings::backendFromKey(QStringLiteral("mtl")) != RenderBackendKind::Metal) {
        return 12;
    }
    if (RenderSettings::preferredVulkanDrawStrategy() != VulkanDrawStrategy::GpuDrivenIndirect) {
        return 15;
    }
    if (RenderSettings::vulkanDrawStrategyKey(VulkanDrawStrategy::Traditional) != QStringLiteral("traditional")) {
        return 16;
    }
    if (RenderSettings::vulkanDrawStrategyKey(VulkanDrawStrategy::GpuDrivenIndirect) != QStringLiteral("gpu_driven_indirect")) {
        return 17;
    }
    if (RenderSettings::vulkanDrawStrategyFromKey(QStringLiteral("gpu-driven")) != VulkanDrawStrategy::GpuDrivenIndirect) {
        return 18;
    }
    if (RenderSettings::vulkanDrawStrategyFromKey(QStringLiteral("default"),
                                                  VulkanDrawStrategy::GpuDrivenIndirect) !=
        VulkanDrawStrategy::GpuDrivenIndirect) {
        return 26;
    }
    if (RenderSettings::vulkanDrawStrategyFromKey(QStringLiteral("mesh shader")) != VulkanDrawStrategy::MeshShader) {
        return 19;
    }
    if (!RenderSettings::isVulkanDrawStrategyAvailable(VulkanDrawStrategy::Traditional)) {
        return 20;
    }
    if (!RenderSettings::isVulkanDrawStrategyAvailable(VulkanDrawStrategy::GpuDrivenIndirect)) {
        return 21;
    }
    if (RenderSettings::isVulkanDrawStrategyAvailable(VulkanDrawStrategy::MeshShader)) {
        return 25;
    }

    QTemporaryDir legacySettingsDir;
    if (!legacySettingsDir.isValid()) {
        return 27;
    }
    {
        QSettings legacySettings(QDir(legacySettingsDir.path()).filePath(QStringLiteral("settings.ini")),
                                 QSettings::IniFormat);
        legacySettings.setValue(QStringLiteral("render/vulkanDrawStrategy"),
                                QStringLiteral("traditional"));
        legacySettings.sync();
    }
    qputenv("FEMODELVIEWER_CONFIG_DIR", legacySettingsDir.path().toLocal8Bit());
    if (RenderSettings::preferredVulkanDrawStrategy() != VulkanDrawStrategy::GpuDrivenIndirect) {
        return 28;
    }
    {
        QSettings migratedSettings(QDir(legacySettingsDir.path()).filePath(QStringLiteral("settings.ini")),
                                   QSettings::IniFormat);
        if (migratedSettings.value(QStringLiteral("render/vulkanDrawStrategy")).toString() !=
            QStringLiteral("gpu_driven_indirect")) {
            return 29;
        }
    }
    RenderSettings::setPreferredVulkanDrawStrategy(VulkanDrawStrategy::Traditional);
    if (RenderSettings::preferredVulkanDrawStrategy() != VulkanDrawStrategy::Traditional) {
        return 30;
    }
    qputenv("FEMODELVIEWER_CONFIG_DIR", settingsDir.path().toLocal8Bit());

    RenderSettings::setPreferredBackend(RenderBackendKind::Vulkan);
    if (RenderSettings::preferredBackend() != RenderBackendKind::Vulkan) {
        return 7;
    }
    if (!QFileInfo::exists(QDir(settingsDir.path()).filePath(QStringLiteral("settings.ini")))) {
        return 10;
    }

    const RenderBackendKind expectedEffective = isRenderBackendAvailable(RenderBackendKind::Vulkan)
        ? RenderBackendKind::Vulkan
        : RenderBackendKind::OpenGL;
    if (RenderSettings::effectiveBackend() != expectedEffective) {
        return 8;
    }
    RenderSettings::setPreferredVulkanDrawStrategy(VulkanDrawStrategy::GpuDrivenIndirect);
    if (RenderSettings::preferredVulkanDrawStrategy() != VulkanDrawStrategy::GpuDrivenIndirect) {
        return 22;
    }
    if (RenderSettings::effectiveVulkanDrawStrategy() != VulkanDrawStrategy::GpuDrivenIndirect) {
        return 23;
    }
    RenderSettings::setPreferredVulkanDrawStrategy(VulkanDrawStrategy::Traditional);
    if (RenderSettings::effectiveVulkanDrawStrategy() != VulkanDrawStrategy::Traditional) {
        return 24;
    }

    RenderSettings::setPreferredBackend(RenderBackendKind::Metal);
    if (RenderSettings::preferredBackend() != RenderBackendKind::Metal) {
        return 13;
    }
    const RenderBackendKind expectedMetalEffective = isRenderBackendAvailable(RenderBackendKind::Metal)
        ? RenderBackendKind::Metal
        : RenderBackendKind::OpenGL;
    if (RenderSettings::effectiveBackend() != expectedMetalEffective) {
        return 14;
    }

    RenderSettings::setPreferredBackend(RenderBackendKind::OpenGL);
    if (RenderSettings::preferredBackend() != RenderBackendKind::OpenGL) {
        return 9;
    }

    return 0;
}
