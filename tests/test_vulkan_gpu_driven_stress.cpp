#include "Geometry.h"
#include "RenderSettings.h"
#include "VulkanMacOSSurfaceFactory.h"
#include "VulkanRenderBackend.h"
#include "VulkanSurface.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QWindow>

#include <array>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

struct StressContext {
    VulkanRenderBackend backend;
    QWindow window;
    VulkanSurface surface;

    bool initialize(uint32_t width, uint32_t height)
    {
        if (!backend.initializeContext(VulkanMacOSSurfaceFactory::requiredInstanceExtensions())) {
            std::fprintf(stderr, "initializeContext failed: %s\n",
                         backend.lastError().toUtf8().constData());
            return false;
        }

        window.setTitle(QStringLiteral("FEModelViewer Vulkan GPU-driven Stress Test"));
        window.resize(static_cast<int>(width), static_cast<int>(height));
        window.create();

        QString surfaceError;
        surface = VulkanMacOSSurfaceFactory::createSurface(
            backend.instance(), &window, &surfaceError);
        if (!surface.isValid()) {
            std::fprintf(stderr, "createSurface failed: %s\n",
                         surfaceError.toUtf8().constData());
            return false;
        }
        if (!backend.initializeDevice(surface.handle())) {
            std::fprintf(stderr, "initializeDevice failed: %s\n",
                         backend.lastError().toUtf8().constData());
            return false;
        }
        if (!recreateSwapchain(width, height)) {
            return false;
        }

        window.show();
        QCoreApplication::processEvents();
        return true;
    }

    bool recreateSwapchain(uint32_t width, uint32_t height)
    {
        backend.destroySwapchain();
        window.resize(static_cast<int>(width), static_cast<int>(height));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (!backend.initializeSwapchain(surface.handle(), width, height, false)) {
            std::fprintf(stderr, "initializeSwapchain(%u, %u) failed: %s\n",
                         width,
                         height,
                         backend.lastError().toUtf8().constData());
            return false;
        }
        return true;
    }
};

Mesh makeGridMesh(int divisions)
{
    Mesh mesh;
    const float step = 1.0f / static_cast<float>(divisions);
    for (int y = 0; y <= divisions; ++y) {
        for (int x = 0; x <= divisions; ++x) {
            mesh.vertices.push_back(-0.5f + step * static_cast<float>(x));
            mesh.vertices.push_back(-0.5f + step * static_cast<float>(y));
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(0.0f);
            mesh.vertices.push_back(1.0f);
        }
    }

    auto vertexIndex = [divisions](int x, int y) {
        return static_cast<uint32_t>(y * (divisions + 1) + x);
    };
    for (int y = 0; y < divisions; ++y) {
        for (int x = 0; x < divisions; ++x) {
            const uint32_t a = vertexIndex(x, y);
            const uint32_t b = vertexIndex(x + 1, y);
            const uint32_t c = vertexIndex(x + 1, y + 1);
            const uint32_t d = vertexIndex(x, y + 1);
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }

    for (int y = 0; y <= divisions; ++y) {
        for (int x = 0; x <= divisions; ++x) {
            mesh.edgeVertices.push_back(-0.5f + step * static_cast<float>(x));
            mesh.edgeVertices.push_back(-0.5f + step * static_cast<float>(y));
            mesh.edgeVertices.push_back(0.0f);
        }
    }
    for (int x = 0; x < divisions; ++x) {
        mesh.edgeIndices.push_back(vertexIndex(x, 0));
        mesh.edgeIndices.push_back(vertexIndex(x + 1, 0));
        mesh.edgeIndices.push_back(vertexIndex(x, divisions));
        mesh.edgeIndices.push_back(vertexIndex(x + 1, divisions));
    }
    for (int y = 0; y < divisions; ++y) {
        mesh.edgeIndices.push_back(vertexIndex(0, y));
        mesh.edgeIndices.push_back(vertexIndex(0, y + 1));
        mesh.edgeIndices.push_back(vertexIndex(divisions, y));
        mesh.edgeIndices.push_back(vertexIndex(divisions, y + 1));
    }
    return mesh;
}

VulkanMeshUploadOptions makeGridOptions(const Mesh& mesh, int pattern)
{
    VulkanMeshUploadOptions options;
    const size_t triangleCount = mesh.indices.size() / 3;
    options.triangleToElement.resize(triangleCount);
    options.triangleToPart.resize(triangleCount);
    for (size_t i = 0; i < triangleCount; ++i) {
        options.triangleToElement[i] = 2000 + static_cast<int>(i);
        options.triangleToPart[i] = static_cast<int>(i % 4);
    }

    const size_t edgeCount = mesh.edgeIndices.size() / 2;
    options.edgeToPart.resize(edgeCount);
    for (size_t i = 0; i < edgeCount; ++i) {
        options.edgeToPart[i] = static_cast<int>(i % 4);
    }

    options.partColors = {
        QVector3D(0.72f, 0.82f, 0.95f),
        QVector3D(0.92f, 0.66f, 0.64f),
        QVector3D(0.58f, 0.78f, 0.60f),
        QVector3D(0.95f, 0.80f, 0.45f)
    };

    switch (pattern % 5) {
    case 1:
        options.partVisibility[1] = false;
        options.partVisibility[3] = false;
        break;
    case 2:
        options.partVisibility[2] = false;
        for (size_t i = 0; i < triangleCount; i += 7) {
            options.hiddenElementIds.insert(options.triangleToElement[i]);
        }
        break;
    case 3:
        options.partVisibility[0] = false;
        options.partVisibility[1] = false;
        options.partVisibility[2] = false;
        options.partVisibility[3] = false;
        break;
    case 4:
        for (size_t i = 3; i < triangleCount; i += 11) {
            options.hiddenElementIds.insert(options.triangleToElement[i]);
        }
        break;
    default:
        break;
    }

    options.useVertexColor = true;
    options.vertexScalars.resize(mesh.vertices.size() / 6);
    for (size_t i = 0; i < options.vertexScalars.size(); ++i) {
        options.vertexScalars[i] = static_cast<float>(i % 97);
    }
    options.edgeScalars.resize(mesh.edgeVertices.size() / 3);
    for (size_t i = 0; i < options.edgeScalars.size(); ++i) {
        options.edgeScalars[i] = static_cast<float>(i % 97);
    }
    options.scalarMin = 0.0f;
    options.scalarMax = 96.0f;
    options.numBands = 12;
    return options;
}

bool uploadMeshWithRetry(VulkanRenderBackend& backend,
                         const Mesh& mesh,
                         const VulkanMeshUploadOptions& options,
                         const char* label)
{
    constexpr int maxAttempts = 3;
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        if (backend.uploadMesh(mesh, options)) {
            return true;
        }
        if (attempt == maxAttempts) {
            std::fprintf(stderr, "%s failed: %s\n",
                         label,
                         backend.lastError().toUtf8().constData());
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(300);
    }
    return false;
}

QString tokenField(const QString& text, const QString& key)
{
    const int start = text.indexOf(key);
    if (start < 0) {
        return {};
    }
    const int valueStart = start + key.size();
    int end = valueStart;
    while (end < text.size() && !text[end].isSpace() && text[end] != QLatin1Char('|')) {
        ++end;
    }
    return text.mid(valueStart, end - valueStart);
}

bool readCounter(const QString& diagnostics,
                 const QString& key,
                 uint64_t& value,
                 int iteration)
{
    bool ok = false;
    value = tokenField(diagnostics, key).toULongLong(&ok);
    if (!ok) {
        std::fprintf(stderr,
                     "iteration %d diagnostics missing counter %s: %s\n",
                     iteration,
                     key.toUtf8().constData(),
                     diagnostics.toUtf8().constData());
        return false;
    }
    return true;
}

bool verifyGpuDrivenDiagnostics(const VulkanRenderBackend& backend,
                                int iteration,
                                bool allHidden,
                                uint64_t& previousDispatches)
{
    const QString diagnostics = backend.renderDiagnostics();
    if (!diagnostics.contains(QStringLiteral("actual=GPU-driven Indirect")) ||
        !diagnostics.contains(QStringLiteral("lastFrameGpu=1")) ||
        !diagnostics.contains(QStringLiteral("lastPickGpu=1")) ||
        !diagnostics.contains(QStringLiteral("frustum=1")) ||
        !diagnostics.contains(QStringLiteral("v2=1")) ||
        !diagnostics.contains(QStringLiteral("cpuSurface=0")) ||
        !diagnostics.contains(QStringLiteral("cpuPoints=0")) ||
        !diagnostics.contains(QStringLiteral("sourceV2=")) ||
        !diagnostics.contains(QStringLiteral("visiblePointIdx=")) ||
        !diagnostics.contains(QStringLiteral("visibilityGpuMs="))) {
        std::fprintf(stderr,
                     "iteration %d diagnostics missing GPU-driven active state: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }

    uint64_t fallbacks = 0;
    if (!readCounter(diagnostics, QStringLiteral("fallbacks="), fallbacks, iteration)) {
        return false;
    }
    if (fallbacks != 0) {
        std::fprintf(stderr,
                     "iteration %d unexpectedly fell back: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }

    uint64_t dispatches = 0;
    if (!readCounter(diagnostics, QStringLiteral("dispatches="), dispatches, iteration)) {
        return false;
    }
    if (dispatches <= previousDispatches) {
        std::fprintf(stderr,
                     "iteration %d dispatch counter did not advance (%llu -> %llu): %s\n",
                     iteration,
                     static_cast<unsigned long long>(previousDispatches),
                     static_cast<unsigned long long>(dispatches),
                     diagnostics.toUtf8().constData());
        return false;
    }
    previousDispatches = dispatches;

    uint64_t visibleTris = 0;
    uint64_t visiblePoints = 0;
    uint64_t visibleEdges = 0;
    if (!readCounter(diagnostics, QStringLiteral("visibleTris="), visibleTris, iteration) ||
        !readCounter(diagnostics, QStringLiteral("visiblePoints="), visiblePoints, iteration) ||
        !readCounter(diagnostics, QStringLiteral("visibleEdges="), visibleEdges, iteration)) {
        return false;
    }
    if (allHidden && (visibleTris != 0 || visiblePoints != 0 || visibleEdges != 0)) {
        std::fprintf(stderr,
                     "iteration %d should have no visible GPU-driven geometry: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }
    if (!allHidden && visibleTris == 0) {
        std::fprintf(stderr,
                     "iteration %d should keep visible GPU-driven triangles: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        return 1;
    }
    qputenv("FEMODELVIEWER_CONFIG_DIR", settingsDir.path().toLocal8Bit());

    QGuiApplication app(argc, argv);
    QThread::msleep(300);
    RenderSettings::setPreferredVulkanDrawStrategy(VulkanDrawStrategy::GpuDrivenIndirect);

    StressContext context;
    if (!context.initialize(128, 96)) {
        return 2;
    }

    const Mesh mesh = makeGridMesh(30);
    VulkanMeshUploadOptions options = makeGridOptions(mesh, 0);
    if (!uploadMeshWithRetry(context.backend, mesh, options, "uploadMesh(initial grid)")) {
        return 3;
    }

    const std::array<ModelDisplayMode, 4> displayModes = {
        ModelDisplayMode::Solid,
        ModelDisplayMode::Wireframe,
        ModelDisplayMode::SolidWireframe,
        ModelDisplayMode::Points
    };
    uint64_t previousDispatches = 0;
    constexpr int iterations = 24;

    for (int iteration = 0; iteration < iterations; ++iteration) {
        options = makeGridOptions(mesh, iteration);
        const bool allHidden = (iteration % 5) == 3;
        if (!context.backend.updateGpuDrivenVisibilityState(options)) {
            std::fprintf(stderr, "updateGpuDrivenVisibilityState(%d) failed: %s\n",
                         iteration,
                         context.backend.lastError().toUtf8().constData());
            return 4;
        }

        const ModelDisplayMode mode = displayModes[static_cast<size_t>(iteration) % displayModes.size()];
        if (!context.backend.renderMeshFrame(QMatrix4x4(),
                                             0.04f,
                                             0.05f,
                                             0.07f,
                                             1.0f,
                                             QMatrix4x4(),
                                             mode)) {
            std::fprintf(stderr, "renderMeshFrame(%d) failed: %s\n",
                         iteration,
                         context.backend.lastError().toUtf8().constData());
            return 5;
        }

        int pickedElement = -2;
        if (!context.backend.pickElementAt(QMatrix4x4(), 96, 80, 48, 40, pickedElement)) {
            std::fprintf(stderr, "pickElementAt(%d) failed: %s\n",
                         iteration,
                         context.backend.lastError().toUtf8().constData());
            return 6;
        }
        if (pickedElement < -1) {
            std::fprintf(stderr, "pickElementAt(%d) returned invalid element id %d\n",
                         iteration,
                         pickedElement);
            return 7;
        }
        if (allHidden && pickedElement != -1) {
            std::fprintf(stderr, "pickElementAt(%d) should not pick hidden geometry, got %d\n",
                         iteration,
                         pickedElement);
            return 8;
        }

        if (!verifyGpuDrivenDiagnostics(context.backend, iteration, allHidden, previousDispatches)) {
            return 9;
        }

        if (((iteration + 1) % 8) == 0 && iteration + 1 < iterations) {
            const uint32_t width = static_cast<uint32_t>(128 + (iteration + 1) * 2);
            const uint32_t height = static_cast<uint32_t>(96 + (iteration + 1));
            if (!context.recreateSwapchain(width, height)) {
                return 10;
            }
            if (!uploadMeshWithRetry(context.backend, mesh, options, "uploadMesh(after recreate)")) {
                return 11;
            }
        }

        app.processEvents();
    }

    context.backend.destroySwapchain();
    return 0;
}
