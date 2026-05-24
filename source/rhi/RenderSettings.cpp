#include "RenderSettings.h"

#include "RenderBackendFactory.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {
constexpr const char* kRenderBackendKey = "render/currentRhi";
constexpr const char* kVulkanDrawStrategyKey = "render/vulkanDrawStrategy";
constexpr const char* kVulkanDrawStrategyVersionKey = "render/vulkanDrawStrategyVersion";
constexpr const char* kConfigEnvVar = "FEMODELVIEWER_CONFIG_DIR";
constexpr const char* kConfigDirName = "config";
constexpr const char* kSettingsFileName = "settings.ini";
constexpr VulkanDrawStrategy kDefaultVulkanDrawStrategy = VulkanDrawStrategy::GpuDrivenIndirect;
constexpr int kVulkanDrawStrategyConfigVersion = 2;

QString configDirectoryPath()
{
    const QByteArray overridePath = qgetenv(kConfigEnvVar);
    if (!overridePath.isEmpty()) {
        return QString::fromLocal8Bit(overridePath);
    }

    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        return QDir(appDir).filePath(QString::fromLatin1(kConfigDirName));
    }
    return QDir::current().filePath(QString::fromLatin1(kConfigDirName));
}

QSettings makeSettings()
{
    const QString configDirPath = configDirectoryPath();
    QDir dir;
    dir.mkpath(configDirPath);
    const QString settingsPath = QDir(configDirPath).filePath(QString::fromLatin1(kSettingsFileName));
    return QSettings(settingsPath, QSettings::IniFormat);
}

QString normalizedKey(QString key)
{
    key = key.trimmed().toLower();
    key.replace(QLatin1Char('-'), QLatin1Char('_'));
    key.replace(QLatin1Char(' '), QLatin1Char('_'));
    return key;
}
}

RenderBackendKind RenderSettings::preferredBackend()
{
    QSettings settings = makeSettings();
    const QString key = settings.value(QString::fromLatin1(kRenderBackendKey),
                                       backendKey(RenderBackendKind::OpenGL)).toString();
    return backendFromKey(key);
}

void RenderSettings::setPreferredBackend(RenderBackendKind kind)
{
    QSettings settings = makeSettings();
    settings.setValue(QString::fromLatin1(kRenderBackendKey), backendKey(kind));
    settings.sync();
}

RenderBackendKind RenderSettings::effectiveBackend()
{
    const RenderBackendKind preferred = preferredBackend();
    if (isRenderBackendAvailable(preferred)) {
        return preferred;
    }
    return RenderBackendKind::OpenGL;
}

QString RenderSettings::backendKey(RenderBackendKind kind)
{
    switch (kind) {
    case RenderBackendKind::Vulkan:
        return QStringLiteral("vulkan");
    case RenderBackendKind::Metal:
        return QStringLiteral("metal");
    case RenderBackendKind::OpenGL:
    default:
        return QStringLiteral("opengl");
    }
}

RenderBackendKind RenderSettings::backendFromKey(const QString& key, RenderBackendKind fallback)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QStringLiteral("opengl") || normalized == QStringLiteral("gl")) {
        return RenderBackendKind::OpenGL;
    }
    if (normalized == QStringLiteral("vulkan") || normalized == QStringLiteral("vk")) {
        return RenderBackendKind::Vulkan;
    }
    if (normalized == QStringLiteral("metal") || normalized == QStringLiteral("mtl")) {
        return RenderBackendKind::Metal;
    }
    return fallback;
}

VulkanDrawStrategy RenderSettings::preferredVulkanDrawStrategy()
{
    QSettings settings = makeSettings();
    const int version = settings.value(QString::fromLatin1(kVulkanDrawStrategyVersionKey), 0).toInt();
    if (version < kVulkanDrawStrategyConfigVersion) {
        const QString existingKey = settings.value(QString::fromLatin1(kVulkanDrawStrategyKey)).toString();
        const QString normalized = normalizedKey(existingKey);
        const bool migrateToGpuDriven =
            normalized.isEmpty() ||
            normalized == QStringLiteral("default") ||
            normalized == QStringLiteral("traditional") ||
            normalized == QStringLiteral("legacy");
        if (migrateToGpuDriven) {
            settings.setValue(QString::fromLatin1(kVulkanDrawStrategyKey),
                              vulkanDrawStrategyKey(kDefaultVulkanDrawStrategy));
        }
        settings.setValue(QString::fromLatin1(kVulkanDrawStrategyVersionKey),
                          kVulkanDrawStrategyConfigVersion);
        settings.sync();
    }

    const QString key = settings.value(QString::fromLatin1(kVulkanDrawStrategyKey),
                                       vulkanDrawStrategyKey(kDefaultVulkanDrawStrategy)).toString();
    return vulkanDrawStrategyFromKey(key, kDefaultVulkanDrawStrategy);
}

void RenderSettings::setPreferredVulkanDrawStrategy(VulkanDrawStrategy strategy)
{
    QSettings settings = makeSettings();
    settings.setValue(QString::fromLatin1(kVulkanDrawStrategyKey), vulkanDrawStrategyKey(strategy));
    settings.setValue(QString::fromLatin1(kVulkanDrawStrategyVersionKey),
                      kVulkanDrawStrategyConfigVersion);
    settings.sync();
}

VulkanDrawStrategy RenderSettings::effectiveVulkanDrawStrategy()
{
    const VulkanDrawStrategy preferred = preferredVulkanDrawStrategy();
    if (isVulkanDrawStrategyAvailable(preferred)) {
        return preferred;
    }
    return VulkanDrawStrategy::Traditional;
}

bool RenderSettings::isVulkanDrawStrategyAvailable(VulkanDrawStrategy strategy)
{
    switch (strategy) {
    case VulkanDrawStrategy::Traditional:
    case VulkanDrawStrategy::GpuDrivenIndirect:
        return true;
    case VulkanDrawStrategy::MeshShader:
    default:
        return false;
    }
}

QString RenderSettings::vulkanDrawStrategyKey(VulkanDrawStrategy strategy)
{
    switch (strategy) {
    case VulkanDrawStrategy::GpuDrivenIndirect:
        return QStringLiteral("gpu_driven_indirect");
    case VulkanDrawStrategy::MeshShader:
        return QStringLiteral("mesh_shader");
    case VulkanDrawStrategy::Traditional:
    default:
        return QStringLiteral("traditional");
    }
}

VulkanDrawStrategy RenderSettings::vulkanDrawStrategyFromKey(
    const QString& key,
    VulkanDrawStrategy fallback)
{
    const QString normalized = normalizedKey(key);

    if (normalized == QStringLiteral("default")) {
        return fallback;
    }
    if (normalized == QStringLiteral("traditional") ||
        normalized == QStringLiteral("legacy")) {
        return VulkanDrawStrategy::Traditional;
    }
    if (normalized == QStringLiteral("gpu") ||
        normalized == QStringLiteral("gpu_driven") ||
        normalized == QStringLiteral("gpu_driven_indirect") ||
        normalized == QStringLiteral("indirect")) {
        return VulkanDrawStrategy::GpuDrivenIndirect;
    }
    if (normalized == QStringLiteral("mesh") ||
        normalized == QStringLiteral("mesh_shader") ||
        normalized == QStringLiteral("meshshader")) {
        return VulkanDrawStrategy::MeshShader;
    }
    return fallback;
}

QString RenderSettings::vulkanDrawStrategyName(VulkanDrawStrategy strategy)
{
    switch (strategy) {
    case VulkanDrawStrategy::GpuDrivenIndirect:
        return QStringLiteral("GPU-driven Indirect");
    case VulkanDrawStrategy::MeshShader:
        return QStringLiteral("Mesh Shader");
    case VulkanDrawStrategy::Traditional:
    default:
        return QStringLiteral("传统");
    }
}
