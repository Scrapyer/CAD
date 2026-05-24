#include "Geometry.h"
#include "RenderSettings.h"
#include "VulkanMacOSSurfaceFactory.h"
#include "VulkanRenderBackend.h"
#include "VulkanSurface.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QWindow>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

namespace {

struct SoakContext {
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

        window.setTitle(QStringLiteral("FEModelViewer Vulkan GPU-driven Soak"));
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
        options.triangleToElement[i] = 500000 + static_cast<int>(i);
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
    switch (pattern % 6) {
    case 1:
        options.partVisibility[1] = false;
        break;
    case 2:
        options.partVisibility[1] = false;
        options.partVisibility[3] = false;
        break;
    case 3:
        options.partVisibility[2] = false;
        for (size_t i = 0; i < triangleCount; i += 13) {
            options.hiddenElementIds.insert(options.triangleToElement[i]);
        }
        break;
    case 4:
        for (size_t i = 5; i < triangleCount; i += 17) {
            options.hiddenElementIds.insert(options.triangleToElement[i]);
        }
        break;
    case 5:
        options.partVisibility[0] = false;
        options.partVisibility[1] = false;
        options.partVisibility[2] = false;
        options.partVisibility[3] = false;
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

bool verifyDiagnostics(const QString& diagnostics,
                       int iteration,
                       bool allHidden,
                       uint64_t& previousDispatches,
                       uint64_t& maxVisibleTriangles,
                       uint64_t& maxVisiblePoints,
                       uint64_t& maxVisibleEdges)
{
    if (!diagnostics.contains(QStringLiteral("actual=GPU-driven Indirect")) ||
        !diagnostics.contains(QStringLiteral("lastFrameGpu=1")) ||
        !diagnostics.contains(QStringLiteral("lastPickGpu=1")) ||
        !diagnostics.contains(QStringLiteral("visibilityGpuMs="))) {
        std::fprintf(stderr,
                     "iteration %d diagnostics missing active GPU-driven state: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }

    uint64_t fallbacks = 0;
    uint64_t dispatches = 0;
    uint64_t visibleTriangles = 0;
    uint64_t visiblePoints = 0;
    uint64_t visibleEdges = 0;
    if (!readCounter(diagnostics, QStringLiteral("fallbacks="), fallbacks, iteration) ||
        !readCounter(diagnostics, QStringLiteral("dispatches="), dispatches, iteration) ||
        !readCounter(diagnostics, QStringLiteral("visibleTris="), visibleTriangles, iteration) ||
        !readCounter(diagnostics, QStringLiteral("visiblePoints="), visiblePoints, iteration) ||
        !readCounter(diagnostics, QStringLiteral("visibleEdges="), visibleEdges, iteration)) {
        return false;
    }
    if (fallbacks != 0 || dispatches <= previousDispatches) {
        std::fprintf(stderr, "iteration %d invalid fallback/dispatch state: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }
    if (allHidden && (visibleTriangles != 0 || visiblePoints != 0 || visibleEdges != 0)) {
        std::fprintf(stderr, "iteration %d should hide all GPU-driven geometry: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }
    if (!allHidden && visibleTriangles == 0) {
        std::fprintf(stderr, "iteration %d unexpectedly has no visible triangles: %s\n",
                     iteration,
                     diagnostics.toUtf8().constData());
        return false;
    }

    previousDispatches = dispatches;
    maxVisibleTriangles = std::max(maxVisibleTriangles, visibleTriangles);
    maxVisiblePoints = std::max(maxVisiblePoints, visiblePoints);
    maxVisibleEdges = std::max(maxVisibleEdges, visibleEdges);
    return true;
}

int positiveArg(int argc, char** argv, int index, int fallback)
{
    if (argc <= index) {
        return fallback;
    }
    const int value = std::atoi(argv[index]);
    return value > 0 ? value : fallback;
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

    const int iterations = positiveArg(argc, argv, 1, 240);
    const int divisions = positiveArg(argc, argv, 2, 80);

    SoakContext context;
    if (!context.initialize(160, 120)) {
        return 2;
    }

    const Mesh mesh = makeGridMesh(divisions);
    VulkanMeshUploadOptions options = makeGridOptions(mesh, 0);
    if (!context.backend.uploadMesh(mesh, options)) {
        std::fprintf(stderr, "uploadMesh(initial) failed: %s\n",
                     context.backend.lastError().toUtf8().constData());
        return 3;
    }

    const std::array<ModelDisplayMode, 4> displayModes = {
        ModelDisplayMode::Solid,
        ModelDisplayMode::Wireframe,
        ModelDisplayMode::SolidWireframe,
        ModelDisplayMode::Points
    };
    uint64_t previousDispatches = 0;
    uint64_t maxVisibleTriangles = 0;
    uint64_t maxVisiblePoints = 0;
    uint64_t maxVisibleEdges = 0;
    int swapchainRecreateCount = 0;
    int pickedElement = -1;
    QElapsedTimer timer;
    timer.start();

    for (int iteration = 0; iteration < iterations; ++iteration) {
        options = makeGridOptions(mesh, iteration);
        const bool allHidden = (iteration % 6) == 5;
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

        if (!context.backend.pickElementAt(QMatrix4x4(), 128, 96, 64, 48, pickedElement)) {
            std::fprintf(stderr, "pickElementAt(%d) failed: %s\n",
                         iteration,
                         context.backend.lastError().toUtf8().constData());
            return 6;
        }
        if (allHidden && pickedElement != -1) {
            std::fprintf(stderr, "iteration %d should not pick hidden geometry, got %d\n",
                         iteration,
                         pickedElement);
            return 7;
        }

        const QString diagnostics = context.backend.renderDiagnostics();
        if (!verifyDiagnostics(diagnostics,
                               iteration,
                               allHidden,
                               previousDispatches,
                               maxVisibleTriangles,
                               maxVisiblePoints,
                               maxVisibleEdges)) {
            return 8;
        }

        if (((iteration + 1) % 60) == 0 && iteration + 1 < iterations) {
            const uint32_t width = static_cast<uint32_t>(160 + ((iteration / 60) % 3) * 24);
            const uint32_t height = static_cast<uint32_t>(120 + ((iteration / 60) % 3) * 16);
            if (!context.recreateSwapchain(width, height)) {
                return 9;
            }
            if (!context.backend.uploadMesh(mesh, options)) {
                std::fprintf(stderr, "uploadMesh(after recreate %d) failed: %s\n",
                             iteration,
                             context.backend.lastError().toUtf8().constData());
                return 10;
            }
            ++swapchainRecreateCount;
        }

        app.processEvents();
    }

    const QString finalDiagnostics = context.backend.renderDiagnostics();
    const double elapsedMs = static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
    std::printf("iterations,divisions,triangles,edges,recreates,elapsed_ms,max_visible_triangles,max_visible_points,max_visible_edges,final_diagnostics\n");
    std::printf("%d,%d,%zu,%zu,%d,%.3f,%llu,%llu,%llu,\"%s\"\n",
                iterations,
                divisions,
                mesh.indices.size() / 3,
                mesh.edgeIndices.size() / 2,
                swapchainRecreateCount,
                elapsedMs,
                static_cast<unsigned long long>(maxVisibleTriangles),
                static_cast<unsigned long long>(maxVisiblePoints),
                static_cast<unsigned long long>(maxVisibleEdges),
                finalDiagnostics.toUtf8().constData());

    context.backend.destroySwapchain();
    return 0;
}
