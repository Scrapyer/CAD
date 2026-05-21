#include "Geometry.h"
#include "VulkanMacOSSurfaceFactory.h"
#include "VulkanRenderBackend.h"
#include "VulkanSurface.h"

#include <QGuiApplication>
#include <QWindow>

#include <cstdio>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

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
    if (!backend.uploadMesh(cube, uploadOptions)) {
        std::fprintf(stderr, "uploadMesh failed: %s\n",
                     backend.lastError().toUtf8().constData());
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
    VulkanMeshUploadOptions hiddenOptions = uploadOptions;
    hiddenOptions.partVisibility[0] = false;
    hiddenOptions.partVisibility[1] = false;
    if (!backend.uploadMesh(cube, hiddenOptions)) {
        std::fprintf(stderr, "uploadMesh(hidden parts) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 40;
    }
    if (backend.renderPickFrame(QMatrix4x4(), 64, 64)) {
        std::fprintf(stderr, "renderPickFrame(hidden parts) unexpectedly succeeded\n");
        return 41;
    }
    if (!backend.uploadMesh(cube, uploadOptions)) {
        std::fprintf(stderr, "uploadMesh(restore visible parts) failed: %s\n",
                     backend.lastError().toUtf8().constData());
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
#endif

    app.processEvents();

    backend.destroySwapchain();
    return 0;
}
