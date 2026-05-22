#include "Geometry.h"
#include "VulkanMacOSSurfaceFactory.h"
#include "VulkanRenderBackend.h"
#include "VulkanSurface.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QThread>
#include <QWindow>

#include <cstdio>

namespace {

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
        options.triangleToElement[i] = 1000 + static_cast<int>(i);
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
            std::fprintf(stderr, "%s failed: %s\n", label, backend.lastError().toUtf8().constData());
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(500);
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    QThread::msleep(500);

    VulkanRenderBackend backend;
    if (!backend.initializeContext(VulkanMacOSSurfaceFactory::requiredInstanceExtensions())) {
        std::fprintf(stderr, "initializeContext failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 1;
    }

    QWindow window;
    window.setTitle(QStringLiteral("FEModelViewer Vulkan Surface Test"));
    window.resize(64, 64);
    window.create();

    QString surfaceError;
    VulkanSurface surface =
        VulkanMacOSSurfaceFactory::createSurface(backend.instance(), &window, &surfaceError);
    if (!surface.isValid()) {
        std::fprintf(stderr, "createSurface failed: %s\n", surfaceError.toUtf8().constData());
        return 2;
    }

    if (!backend.initializeDevice(surface.handle())) {
        std::fprintf(stderr, "initializeDevice(surface) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 3;
    }
    if (backend.device() == VK_NULL_HANDLE) {
        return 4;
    }

    if (!backend.initializeSwapchain(surface.handle(),
                                     static_cast<uint32_t>(window.width()),
                                     static_cast<uint32_t>(window.height()))) {
        std::fprintf(stderr, "initializeSwapchain failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 5;
    }
    if (!backend.hasSwapchain() || backend.swapchainImageCount() <= 0) {
        return 6;
    }

    window.show();
    app.processEvents();

    if (!backend.renderClearFrame(0.08f, 0.11f, 0.16f, 1.0f)) {
        std::fprintf(stderr, "renderClearFrame failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 7;
    }
#if defined(FERENDER_HAS_VULKAN_TRIANGLE_PIPELINE)
    if (!backend.renderTriangleFrame()) {
        std::fprintf(stderr, "renderTriangleFrame failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 8;
    }
    backend.destroySwapchain();
    if (!backend.initializeSwapchain(surface.handle(), 96, 80)) {
        std::fprintf(stderr, "initializeSwapchain(resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 29;
    }
    if (!backend.hasSwapchain() || backend.swapchainImageCount() <= 0) {
        return 30;
    }
    if (!backend.renderTriangleFrame()) {
        std::fprintf(stderr, "renderTriangleFrame(resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 31;
    }
    Mesh cube = Geometry::cube();
    cube.edgeVertices = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f,  0.5f, 0.5f,
        -0.5f,  0.5f, 0.5f
    };
    cube.edgeIndices = {0, 1, 1, 2, 2, 3, 3, 0};
    VulkanMeshUploadOptions uploadOptions;
    uploadOptions.triangleToElement.resize(cube.indices.size() / 3, 0);
    uploadOptions.triangleToPart.resize(cube.indices.size() / 3, 0);
    for (size_t i = 0; i < uploadOptions.triangleToPart.size(); ++i) {
        uploadOptions.triangleToElement[i] = 100 + static_cast<int>(i);
        uploadOptions.triangleToPart[i] = static_cast<int>(i % 2);
    }
    uploadOptions.edgeToPart = {0, 1, 0, 1};
    uploadOptions.partColors = {
        QVector3D(0.61f, 0.86f, 0.63f),
        QVector3D(0.94f, 0.56f, 0.66f)
    };
    uploadOptions.partVisibility[1] = false;
    uploadOptions.useVertexColor = true;
    uploadOptions.vertexScalars.resize(cube.vertices.size() / 6);
    for (size_t i = 0; i < uploadOptions.vertexScalars.size(); ++i) {
        uploadOptions.vertexScalars[i] = static_cast<float>(i);
    }
    uploadOptions.scalarMin = 0.0f;
    uploadOptions.scalarMax = static_cast<float>(uploadOptions.vertexScalars.size() - 1);
    uploadOptions.numBands = 9;
    if (!uploadMeshWithRetry(backend, cube, uploadOptions, "uploadMesh")) {
        return 9;
    }
    for (size_t i = 0; i < uploadOptions.vertexScalars.size(); ++i) {
        uploadOptions.vertexScalars[i] = static_cast<float>(uploadOptions.vertexScalars.size() - 1 - i);
    }
    if (!backend.uploadVertexScalars(uploadOptions.vertexScalars,
                                     uploadOptions.scalarMin,
                                     uploadOptions.scalarMax,
                                     uploadOptions.numBands,
                                     true)) {
        std::fprintf(stderr, "uploadVertexScalars failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 18;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 10;
    }
    if (!backend.renderPickFrame(QMatrix4x4(), 64, 64)) {
        std::fprintf(stderr, "renderPickFrame failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 11;
    }
    int pickedElement = -1;
    if (!backend.pickElementAt(QMatrix4x4(), 64, 64, 32, 32, pickedElement)) {
        std::fprintf(stderr, "pickElementAt failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 12;
    }
    if (pickedElement < -1) {
        return 13;
    }
    backend.destroySwapchain();
    if (!backend.initializeSwapchain(surface.handle(), 128, 96)) {
        std::fprintf(stderr, "initializeSwapchain(after pick resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 50;
    }
    if (!uploadMeshWithRetry(backend, cube, uploadOptions, "uploadMesh(after pick resize)")) {
        return 51;
    }
    if (!backend.renderPickFrame(QMatrix4x4(), 128, 96)) {
        std::fprintf(stderr, "renderPickFrame(after pick resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 52;
    }
    if (!backend.pickElementAt(QMatrix4x4(), 128, 96, 64, 48, pickedElement)) {
        std::fprintf(stderr, "pickElementAt(after pick resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 53;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(after pick resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 54;
    }
    VulkanMeshUploadOptions hiddenOptions = uploadOptions;
    hiddenOptions.partVisibility[0] = false;
    hiddenOptions.partVisibility[1] = false;
    if (!uploadMeshWithRetry(backend, cube, hiddenOptions, "uploadMesh(hidden parts)")) {
        return 40;
    }
    if (backend.renderPickFrame(QMatrix4x4(), 64, 64)) {
        std::fprintf(stderr, "renderPickFrame(hidden parts) unexpectedly succeeded\n");
        return 41;
    }
    if (!uploadMeshWithRetry(backend, cube, uploadOptions, "uploadMesh(restore visible parts)")) {
        return 42;
    }
    const std::vector<float> overlayLine = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f,  0.5f, 0.5f
    };
    if (backend.uploadOverlayLines({0.0f, 1.0f})) {
        std::fprintf(stderr, "uploadOverlayLines(invalid) unexpectedly succeeded\n");
        return 43;
    }
    if (!backend.uploadOverlayLines(overlayLine)) {
        std::fprintf(stderr, "uploadOverlayLines failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 14;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(overlay) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 15;
    }
    if (!backend.uploadOverlayLines({})) {
        std::fprintf(stderr, "uploadOverlayLines(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 16;
    }
    const std::vector<float> sliceLine = {
        -0.5f, 0.0f, 0.0f,
         0.5f, 0.0f, 0.0f
    };
    if (backend.uploadSliceLines({0.0f, 1.0f})) {
        std::fprintf(stderr, "uploadSliceLines(invalid) unexpectedly succeeded\n");
        return 44;
    }
    if (!backend.uploadSliceLines(sliceLine)) {
        std::fprintf(stderr, "uploadSliceLines failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 20;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(slice) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 21;
    }
    if (!backend.uploadSliceLines({})) {
        std::fprintf(stderr, "uploadSliceLines(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 22;
    }
    if (!backend.uploadIsoSurfaceMesh(cube)) {
        std::fprintf(stderr, "uploadIsoSurfaceMesh failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 23;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(iso) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 24;
    }
    if (!backend.uploadIsoSurfaceMesh(Mesh{})) {
        std::fprintf(stderr, "uploadIsoSurfaceMesh(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 25;
    }
    if (!backend.uploadClipPreviewMesh(cube)) {
        std::fprintf(stderr, "uploadClipPreviewMesh failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 26;
    }
    Mesh invalidClip = cube;
    invalidClip.edgeVertices = {0.0f, 1.0f};
    if (backend.uploadClipPreviewMesh(invalidClip)) {
        std::fprintf(stderr, "uploadClipPreviewMesh(invalid line) unexpectedly succeeded\n");
        return 45;
    }
    if (!backend.uploadClipPreviewMesh(cube)) {
        std::fprintf(stderr, "uploadClipPreviewMesh(restore after invalid) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 46;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(clip preview) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 27;
    }
    if (!backend.uploadClipPreviewMesh(Mesh{})) {
        std::fprintf(stderr, "uploadClipPreviewMesh(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 28;
    }
    const std::vector<float> selectionLine = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f
    };
    if (backend.uploadSelectionLines({0.0f, 1.0f})) {
        std::fprintf(stderr, "uploadSelectionLines(invalid) unexpectedly succeeded\n");
        return 47;
    }
    if (!backend.uploadSelectionLines(selectionLine)) {
        std::fprintf(stderr, "uploadSelectionLines failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 17;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(selection) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 18;
    }
    if (!backend.uploadSelectionLines({})) {
        std::fprintf(stderr, "uploadSelectionLines(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 19;
    }
    if (!backend.uploadOverlayLines(overlayLine)) {
        std::fprintf(stderr, "uploadOverlayLines(combo) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 32;
    }
    if (!backend.uploadSliceLines(sliceLine)) {
        std::fprintf(stderr, "uploadSliceLines(combo) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 33;
    }
    if (!backend.uploadIsoSurfaceMesh(cube)) {
        std::fprintf(stderr, "uploadIsoSurfaceMesh(combo) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 34;
    }
    if (!backend.uploadClipPreviewMesh(cube)) {
        std::fprintf(stderr, "uploadClipPreviewMesh(combo) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 35;
    }
    if (!backend.uploadSelectionLines(selectionLine)) {
        std::fprintf(stderr, "uploadSelectionLines(combo) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 36;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(combo overlay/slice/iso/clip/selection) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 37;
    }
    if (!backend.pickElementAt(QMatrix4x4(), 64, 64, 16, 16, pickedElement)) {
        std::fprintf(stderr, "pickElementAt(combo) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 48;
    }
    if (pickedElement < -1) {
        return 49;
    }
    if (!backend.uploadOverlayLines({}) ||
        !backend.uploadSliceLines({}) ||
        !backend.uploadIsoSurfaceMesh(Mesh{}) ||
        !backend.uploadClipPreviewMesh(Mesh{}) ||
        !backend.uploadSelectionLines({})) {
        std::fprintf(stderr, "clear combo resources failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 38;
    }
    if (!backend.renderMeshFrame()) {
        std::fprintf(stderr, "renderMeshFrame(combo cleared) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 39;
    }
    Mesh grid = makeGridMesh(40);
    for (int iteration = 0; iteration < 4; ++iteration) {
        const bool hideOddParts = (iteration % 2) == 1;
        VulkanMeshUploadOptions gridOptions = makeGridOptions(grid, hideOddParts);
        char uploadLabel[64];
        std::snprintf(uploadLabel, sizeof(uploadLabel), "uploadMesh(grid stress %d)", iteration);
        if (!uploadMeshWithRetry(backend, grid, gridOptions, uploadLabel)) {
            return 55;
        }
        if (!backend.uploadVertexScalars(gridOptions.vertexScalars,
                                         gridOptions.scalarMin,
                                         gridOptions.scalarMax,
                                         gridOptions.numBands,
                                         true)) {
            std::fprintf(stderr, "uploadVertexScalars(grid stress %d) failed: %s\n",
                         iteration,
                         backend.lastError().toUtf8().constData());
            return 56;
        }
        if (!backend.uploadOverlayLines(overlayLine) ||
            !backend.uploadSliceLines(sliceLine) ||
            !backend.uploadSelectionLines(selectionLine)) {
            std::fprintf(stderr, "upload dynamic lines(grid stress %d) failed: %s\n",
                         iteration,
                         backend.lastError().toUtf8().constData());
            return 57;
        }
        if (!backend.renderMeshFrame()) {
            std::fprintf(stderr, "renderMeshFrame(grid stress %d) failed: %s\n",
                         iteration,
                         backend.lastError().toUtf8().constData());
            return 58;
        }
        if (!backend.pickElementAt(QMatrix4x4(),
                                   static_cast<uint32_t>(96 + iteration * 8),
                                   static_cast<uint32_t>(80 + iteration * 8),
                                   static_cast<uint32_t>(48 + iteration * 4),
                                   static_cast<uint32_t>(40 + iteration * 4),
                                   pickedElement)) {
            std::fprintf(stderr, "pickElementAt(grid stress %d) failed: %s\n",
                         iteration,
                         backend.lastError().toUtf8().constData());
            return 59;
        }
        backend.destroySwapchain();
        if (!backend.initializeSwapchain(surface.handle(),
                                         static_cast<uint32_t>(112 + iteration * 16),
                                         static_cast<uint32_t>(88 + iteration * 12))) {
            std::fprintf(stderr, "initializeSwapchain(grid stress %d) failed: %s\n",
                         iteration,
                         backend.lastError().toUtf8().constData());
            return 60;
        }
        app.processEvents();
    }
    VulkanMeshUploadOptions finalGridOptions = makeGridOptions(grid, false);
    if (!uploadMeshWithRetry(backend, grid, finalGridOptions, "uploadMesh(final grid stress restore)")) {
        return 61;
    }
    if (!backend.uploadOverlayLines({}) ||
        !backend.uploadSliceLines({}) ||
        !backend.uploadSelectionLines({}) ||
        !backend.renderMeshFrame()) {
        std::fprintf(stderr, "final grid stress restore failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 61;
    }
#endif

    app.processEvents();

    backend.destroySwapchain();
    return 0;
}
