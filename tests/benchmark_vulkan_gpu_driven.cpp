#include "Geometry.h"
#include "RenderSettings.h"
#include "VulkanGpuDrivenUploadBuilder.h"
#include "VulkanMacOSSurfaceFactory.h"
#include "VulkanRenderBackend.h"
#include "VulkanSurface.h"

#include <QCoreApplication>
#include <QByteArray>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QThread>
#include <QWindow>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {
constexpr const char* kDisableGpuDrivenV2Env = "FEMODELVIEWER_VULKAN_GPU_DRIVEN_DISABLE_V2";

enum class BenchmarkStrategy {
    Traditional,
    GpuDrivenIndirectV1,
    GpuDrivenIndirectV2
};

struct BenchmarkContext {
    VulkanRenderBackend backend;
    QWindow window;
    VulkanSurface surface;

    bool initialize()
    {
        if (!backend.initializeContext(VulkanMacOSSurfaceFactory::requiredInstanceExtensions())) {
            std::fprintf(stderr, "initializeContext failed: %s\n",
                         backend.lastError().toUtf8().constData());
            return false;
        }

        window.setTitle(QStringLiteral("FEModelViewer Vulkan GPU-driven Benchmark"));
        window.resize(128, 96);
        window.create();

        QString surfaceError;
        surface = VulkanMacOSSurfaceFactory::createSurface(
            backend.instance(), &window, &surfaceError);
        if (!surface.isValid()) {
            std::fprintf(stderr, "createSurface failed: %s\n", surfaceError.toUtf8().constData());
            return false;
        }
        if (!backend.initializeDevice(surface.handle())) {
            std::fprintf(stderr, "initializeDevice failed: %s\n",
                         backend.lastError().toUtf8().constData());
            return false;
        }
        if (!backend.initializeSwapchain(surface.handle(),
                                         static_cast<uint32_t>(window.width()),
                                         static_cast<uint32_t>(window.height()),
                                         false)) {
            std::fprintf(stderr, "initializeSwapchain failed: %s\n",
                         backend.lastError().toUtf8().constData());
            return false;
        }
        window.show();
        QCoreApplication::processEvents();
        return true;
    }
};

struct BenchmarkResult {
    BenchmarkStrategy strategy = BenchmarkStrategy::Traditional;
    int divisions = 0;
    size_t sourceVertexCount = 0;
    size_t triangleCount = 0;
    size_t edgeCount = 0;
    size_t gpuDrivenV1SurfaceBytes = 0;
    size_t gpuDrivenV2SurfaceBytes = 0;
    double gpuDrivenV2SavingsPercent = 0.0;
    double uploadMs = 0.0;
    QString gpuUploadMs;
    double visibilityUpdateMs = 0.0;
    double renderAverageMs = 0.0;
    double pickFrameAverageMs = 0.0;
    double pickMs = 0.0;
    QString visibilityGpuMs;
    QString visibleTriangles;
    QString visiblePoints;
    QString visibleEdges;
    QString activeV2;
    QString sourceV2;
    QString cpuSurface;
    QString cpuPoints;
    int pickedElement = -1;
    QString diagnostics;
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

VulkanMeshUploadOptions makeGridOptions(const Mesh& mesh, bool hideOddParts)
{
    VulkanMeshUploadOptions options;
    const size_t triangleCount = mesh.indices.size() / 3;
    options.triangleToElement.resize(triangleCount);
    options.triangleToPart.resize(triangleCount);
    for (size_t i = 0; i < triangleCount; ++i) {
        options.triangleToElement[i] = 100000 + static_cast<int>(i);
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
    if (hideOddParts) {
        options.partVisibility[1] = false;
        options.partVisibility[3] = false;
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

double elapsedMs(QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1000000.0;
}

QString pipeField(const QString& text, const QString& key)
{
    const int start = text.indexOf(key);
    if (start < 0) {
        return {};
    }
    const int valueStart = start + key.size();
    int end = text.indexOf(QStringLiteral(" |"), valueStart);
    if (end < 0) {
        end = text.size();
    }
    return text.mid(valueStart, end - valueStart).trimmed();
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

QString csvEscape(QString value)
{
    value.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QStringLiteral("\"") + value + QStringLiteral("\"");
}

QString benchmarkStrategyKey(BenchmarkStrategy strategy)
{
    switch (strategy) {
    case BenchmarkStrategy::GpuDrivenIndirectV1:
        return QStringLiteral("gpu_driven_indirect_v1");
    case BenchmarkStrategy::GpuDrivenIndirectV2:
        return QStringLiteral("gpu_driven_indirect_v2");
    case BenchmarkStrategy::Traditional:
    default:
        return QStringLiteral("traditional");
    }
}

VulkanDrawStrategy renderStrategy(BenchmarkStrategy strategy)
{
    return strategy == BenchmarkStrategy::Traditional
        ? VulkanDrawStrategy::Traditional
        : VulkanDrawStrategy::GpuDrivenIndirect;
}

void configureBenchmarkStrategy(BenchmarkStrategy strategy)
{
    RenderSettings::setPreferredVulkanDrawStrategy(renderStrategy(strategy));
    if (strategy == BenchmarkStrategy::GpuDrivenIndirectV1) {
        qputenv(kDisableGpuDrivenV2Env, QByteArrayLiteral("1"));
    } else {
        qunsetenv(kDisableGpuDrivenV2Env);
    }
}

bool isGpuDrivenStrategy(BenchmarkStrategy strategy)
{
    return strategy == BenchmarkStrategy::GpuDrivenIndirectV1 ||
        strategy == BenchmarkStrategy::GpuDrivenIndirectV2;
}

bool runCase(BenchmarkStrategy strategy, int divisions, BenchmarkResult& result)
{
    configureBenchmarkStrategy(strategy);

    BenchmarkContext context;
    if (!context.initialize()) {
        return false;
    }

    Mesh mesh = makeGridMesh(divisions);
    VulkanMeshUploadOptions visibleOptions = makeGridOptions(mesh, false);
    VulkanMeshUploadOptions hiddenOptions = makeGridOptions(mesh, true);
    const VulkanGpuDrivenUploadData gpuDrivenV1Data =
        buildVulkanGpuDrivenUploadData(mesh, visibleOptions);
    const VulkanGpuDrivenUploadV2Data gpuDrivenV2Data =
        buildVulkanGpuDrivenUploadV2Data(mesh, visibleOptions);

    result.strategy = strategy;
    result.divisions = divisions;
    result.sourceVertexCount = mesh.vertices.size() / 6;
    result.triangleCount = mesh.indices.size() / 3;
    result.edgeCount = mesh.edgeIndices.size() / 2;
    result.gpuDrivenV1SurfaceBytes =
        gpuDrivenV1Data.vertices.size() * sizeof(VulkanGpuDrivenMeshVertex) +
        gpuDrivenV1Data.triangles.size() * sizeof(VulkanGpuDrivenTriangleMeta) +
        gpuDrivenV1Data.expandedScalars.size() * sizeof(float);
    result.gpuDrivenV2SurfaceBytes = gpuDrivenV2Data.staticSurfaceBytes();
    if (result.gpuDrivenV1SurfaceBytes > 0) {
        result.gpuDrivenV2SavingsPercent =
            100.0 * (1.0 - static_cast<double>(result.gpuDrivenV2SurfaceBytes) /
                         static_cast<double>(result.gpuDrivenV1SurfaceBytes));
    }

    QElapsedTimer timer;
    timer.start();
    if (!context.backend.uploadMesh(mesh, visibleOptions)) {
        std::fprintf(stderr, "uploadMesh(%d, %s) failed: %s\n",
                     divisions,
                     benchmarkStrategyKey(strategy).toUtf8().constData(),
                     context.backend.lastError().toUtf8().constData());
        return false;
    }
    result.uploadMs = elapsedMs(timer);

    // Warm up swapchain/image acquisition once before measuring repeated frames.
    if (!context.backend.renderMeshFrame(QMatrix4x4(),
                                         0.04f,
                                         0.05f,
                                         0.07f,
                                         1.0f,
                                         QMatrix4x4(),
                                         ModelDisplayMode::SolidWireframe)) {
        std::fprintf(stderr, "warmup renderMeshFrame(%d) failed: %s\n",
                     divisions,
                     context.backend.lastError().toUtf8().constData());
        return false;
    }

    timer.restart();
    if (isGpuDrivenStrategy(strategy)) {
        if (!context.backend.updateGpuDrivenVisibilityState(hiddenOptions)) {
            std::fprintf(stderr, "updateGpuDrivenVisibilityState(%d) failed: %s\n",
                         divisions,
                         context.backend.lastError().toUtf8().constData());
            return false;
        }
    } else if (!context.backend.uploadMesh(mesh, hiddenOptions)) {
        std::fprintf(stderr, "uploadMesh hidden(%d) failed: %s\n",
                     divisions,
                     context.backend.lastError().toUtf8().constData());
        return false;
    }
    result.visibilityUpdateMs = elapsedMs(timer);

    constexpr int kFrameIterations = 5;
    double totalRenderMs = 0.0;
    for (int i = 0; i < kFrameIterations; ++i) {
        timer.restart();
        if (!context.backend.renderMeshFrame(QMatrix4x4(),
                                             0.04f,
                                             0.05f,
                                             0.07f,
                                             1.0f,
                                             QMatrix4x4(),
                                             ModelDisplayMode::SolidWireframe)) {
            std::fprintf(stderr, "renderMeshFrame(%d, iter %d) failed: %s\n",
                         divisions,
                         i,
                         context.backend.lastError().toUtf8().constData());
            return false;
        }
        totalRenderMs += elapsedMs(timer);
        QCoreApplication::processEvents();
    }
    result.renderAverageMs = totalRenderMs / static_cast<double>(kFrameIterations);

    // Offscreen pick draw gives a stable no-present timing point. The later pickElementAt()
    // includes the same draw plus 1x1 image readback.
    double totalPickFrameMs = 0.0;
    for (int i = 0; i < kFrameIterations; ++i) {
        timer.restart();
        if (!context.backend.renderPickFrame(QMatrix4x4(), 128, 96)) {
            std::fprintf(stderr, "renderPickFrame(%d, iter %d) failed: %s\n",
                         divisions,
                         i,
                         context.backend.lastError().toUtf8().constData());
            return false;
        }
        totalPickFrameMs += elapsedMs(timer);
    }
    result.pickFrameAverageMs = totalPickFrameMs / static_cast<double>(kFrameIterations);

    timer.restart();
    int pickedElement = -1;
    if (!context.backend.pickElementAt(QMatrix4x4(), 128, 96, 64, 48, pickedElement)) {
        std::fprintf(stderr, "pickElementAt(%d) failed: %s\n",
                     divisions,
                     context.backend.lastError().toUtf8().constData());
        return false;
    }
    result.pickMs = elapsedMs(timer);
    result.pickedElement = pickedElement;
    result.diagnostics = context.backend.renderDiagnostics();
    result.gpuUploadMs = tokenField(result.diagnostics, QStringLiteral("gpuUploadMs="));
    result.visibilityGpuMs = tokenField(result.diagnostics, QStringLiteral("visibilityGpuMs="));
    result.visibleTriangles = tokenField(result.diagnostics, QStringLiteral("visibleTris="));
    result.visiblePoints = tokenField(result.diagnostics, QStringLiteral("visiblePoints="));
    result.visibleEdges = tokenField(result.diagnostics, QStringLiteral("visibleEdges="));
    result.activeV2 = tokenField(result.diagnostics, QStringLiteral("v2="));
    result.sourceV2 = tokenField(result.diagnostics, QStringLiteral("sourceV2="));
    result.cpuSurface = tokenField(result.diagnostics, QStringLiteral("cpuSurface="));
    result.cpuPoints = tokenField(result.diagnostics, QStringLiteral("cpuPoints="));

    context.backend.destroySwapchain();
    return true;
}

void printHeader()
{
    std::printf("strategy,divisions,source_vertices,triangles,edges,gpu_v1_surface_bytes,gpu_v2_surface_bytes,gpu_v2_savings_pct,upload_ms,gpu_upload_ms,visibility_update_ms,render_avg_ms,pick_frame_avg_ms,pick_ms,visibility_gpu_ms,visible_triangles,visible_points,visible_edges,picked_element,actual,v2,source_v2,cpu_surface,cpu_points,fallbacks,dispatches,diagnostics\n");
}

void printResult(const BenchmarkResult& result)
{
    const QString strategyName = benchmarkStrategyKey(result.strategy);
    const QString actual = pipeField(result.diagnostics, QStringLiteral("actual="));
    const QString fallbacks = tokenField(result.diagnostics, QStringLiteral("fallbacks="));
    const QString dispatches = tokenField(result.diagnostics, QStringLiteral("dispatches="));
    QString row = QStringLiteral("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14,%15,%16,%17,%18,%19,%20,%21,%22,%23,%24,%25,%26,%27")
                      .arg(strategyName)
                      .arg(result.divisions)
                      .arg(result.sourceVertexCount)
                      .arg(result.triangleCount)
                      .arg(result.edgeCount)
                      .arg(result.gpuDrivenV1SurfaceBytes)
                      .arg(result.gpuDrivenV2SurfaceBytes)
                      .arg(result.gpuDrivenV2SavingsPercent, 0, 'f', 2)
                      .arg(result.uploadMs, 0, 'f', 3)
                      .arg(result.gpuUploadMs.isEmpty() ? QStringLiteral("0") : result.gpuUploadMs)
                      .arg(result.visibilityUpdateMs, 0, 'f', 3)
                      .arg(result.renderAverageMs, 0, 'f', 3)
                      .arg(result.pickFrameAverageMs, 0, 'f', 3)
                      .arg(result.pickMs, 0, 'f', 3)
                      .arg(result.visibilityGpuMs.isEmpty() ? QStringLiteral("0") : result.visibilityGpuMs)
                      .arg(result.visibleTriangles.isEmpty() ? QStringLiteral("0") : result.visibleTriangles)
                      .arg(result.visiblePoints.isEmpty() ? QStringLiteral("0") : result.visiblePoints)
                      .arg(result.visibleEdges.isEmpty() ? QStringLiteral("0") : result.visibleEdges)
                      .arg(result.pickedElement)
                      .arg(csvEscape(actual))
                      .arg(result.activeV2.isEmpty() ? QStringLiteral("0") : result.activeV2)
                      .arg(result.sourceV2.isEmpty() ? QStringLiteral("0") : result.sourceV2)
                      .arg(result.cpuSurface.isEmpty() ? QStringLiteral("0") : result.cpuSurface)
                      .arg(result.cpuPoints.isEmpty() ? QStringLiteral("0") : result.cpuPoints)
                      .arg(fallbacks.isEmpty() ? QStringLiteral("0") : fallbacks)
                      .arg(dispatches.isEmpty() ? QStringLiteral("0") : dispatches)
                      .arg(csvEscape(result.diagnostics));
    const QByteArray utf8 = row.toUtf8();
    std::printf("%s\n", utf8.constData());
}

std::vector<int> parseDivisions(int argc, char** argv)
{
    std::vector<int> divisions;
    for (int i = 1; i < argc; ++i) {
        const int value = std::atoi(argv[i]);
        if (value > 0) {
            divisions.push_back(value);
        }
    }
    if (divisions.empty()) {
        divisions = {20, 40, 80, 160, 320};
    }
    return divisions;
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir settingsDir;
    if (!settingsDir.isValid()) {
        std::fprintf(stderr, "Failed to create temporary settings directory\n");
        return 1;
    }
    qputenv("FEMODELVIEWER_CONFIG_DIR", settingsDir.path().toLocal8Bit());

    QGuiApplication app(argc, argv);
    QThread::msleep(300);

#if !defined(FERENDER_HAS_VULKAN_TRIANGLE_PIPELINE)
    std::fprintf(stderr, "Vulkan mesh pipeline is disabled because glslc was not available at configure time\n");
    return 2;
#else
    const std::vector<int> divisions = parseDivisions(argc, argv);
    printHeader();
    for (int division : divisions) {
        for (BenchmarkStrategy strategy : {BenchmarkStrategy::Traditional,
                                           BenchmarkStrategy::GpuDrivenIndirectV1,
                                           BenchmarkStrategy::GpuDrivenIndirectV2}) {
            BenchmarkResult result;
            if (!runCase(strategy, division, result)) {
                return 3;
            }
            printResult(result);
        }
    }
    return 0;
#endif
}
