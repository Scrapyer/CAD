#include "RenderBackendFactory.h"
#include "RenderSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        return 1;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());

    QSettings settings(QStringLiteral("FEModelViewer"), QStringLiteral("FEModelViewer"));
    settings.clear();

    if (RenderSettings::preferredBackend() != RenderBackendKind::OpenGL) {
        return 2;
    }
    if (RenderSettings::backendKey(RenderBackendKind::OpenGL) != QStringLiteral("opengl")) {
        return 3;
    }
    if (RenderSettings::backendKey(RenderBackendKind::Vulkan) != QStringLiteral("vulkan")) {
        return 4;
    }
    if (RenderSettings::backendFromKey(QStringLiteral("VK")) != RenderBackendKind::Vulkan) {
        return 5;
    }
    if (RenderSettings::backendFromKey(QStringLiteral("gl")) != RenderBackendKind::OpenGL) {
        return 6;
    }

    RenderSettings::setPreferredBackend(RenderBackendKind::Vulkan);
    if (RenderSettings::preferredBackend() != RenderBackendKind::Vulkan) {
        return 7;
    }

    const RenderBackendKind expectedEffective = isRenderBackendAvailable(RenderBackendKind::Vulkan)
        ? RenderBackendKind::Vulkan
        : RenderBackendKind::OpenGL;
    if (RenderSettings::effectiveBackend() != expectedEffective) {
        return 8;
    }

    RenderSettings::setPreferredBackend(RenderBackendKind::OpenGL);
    if (RenderSettings::preferredBackend() != RenderBackendKind::OpenGL) {
        return 9;
    }

    return 0;
}
