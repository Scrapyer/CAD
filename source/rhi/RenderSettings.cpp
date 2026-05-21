#include "RenderSettings.h"

#include "RenderBackendFactory.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

namespace {
constexpr const char* kRenderBackendKey = "render/currentRhi";
constexpr const char* kConfigEnvVar = "FEMODELVIEWER_CONFIG_DIR";
constexpr const char* kConfigDirName = "config";
constexpr const char* kSettingsFileName = "settings.ini";

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
    return fallback;
}
