#include "RenderSettings.h"

#include "RenderBackendFactory.h"

#include <QSettings>

namespace {
constexpr const char* kSettingsOrg = "FEModelViewer";
constexpr const char* kSettingsApp = "FEModelViewer";
constexpr const char* kRenderBackendKey = "render/currentRhi";

QSettings makeSettings()
{
    return QSettings(QString::fromLatin1(kSettingsOrg), QString::fromLatin1(kSettingsApp));
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
