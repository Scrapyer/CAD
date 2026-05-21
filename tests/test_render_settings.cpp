#include "RenderBackendFactory.h"
#include "RenderSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
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
    if (!QFileInfo::exists(QDir(settingsDir.path()).filePath(QStringLiteral("settings.ini")))) {
        return 10;
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
