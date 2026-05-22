#include "Geometry.h"
#include "MetalRenderBackend.h"

#include <QGuiApplication>
#include <QMatrix4x4>
#include <QVector3D>
#include <QWindow>

#include <cstdio>

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);

    if (!MetalRenderBackend::isSystemAvailable()) {
        std::fprintf(stderr, "Metal system default device is unavailable\n");
        return 1;
    }

    QWindow window;
    window.setTitle(QStringLiteral("FEModelViewer Metal Layer Test"));
    window.resize(64, 64);
    window.create();
    window.show();
    app.processEvents();

    MetalRenderBackend backend;
    if (!backend.initializeLayer(&window, window.width(), window.height(), window.devicePixelRatio())) {
        std::fprintf(stderr, "initializeLayer failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 2;
    }
    if (!backend.hasLayer()) {
        return 3;
    }
    if (backend.info().renderer.isEmpty() || backend.info().version.isEmpty()) {
        return 4;
    }
    if (!backend.renderClearFrame(0.08f, 0.11f, 0.16f, 1.0f)) {
        std::fprintf(stderr, "renderClearFrame failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 5;
    }

    Mesh cube = Geometry::cube();
    cube.edgeVertices = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f,  0.5f, 0.5f,
        -0.5f,  0.5f, 0.5f
    };
    cube.edgeIndices = {0, 1, 1, 2, 2, 3, 3, 0};
    MetalMeshUploadOptions uploadOptions;
    uploadOptions.objectColor = QVector3D(0.48f, 0.72f, 0.76f);
    uploadOptions.triangleToElement.resize(cube.indices.size() / 3);
    uploadOptions.triangleToPart.resize(cube.indices.size() / 3);
    for (size_t i = 0; i < uploadOptions.triangleToPart.size(); ++i) {
        uploadOptions.triangleToElement[i] = 100 + static_cast<int>(i);
        uploadOptions.triangleToPart[i] = static_cast<int>(i % 2);
    }
    uploadOptions.edgeToPart = {0, 1, 0, 1};
    uploadOptions.partColors = {
        QVector3D(0.61f, 0.86f, 0.63f),
        QVector3D(0.94f, 0.56f, 0.66f)
    };
    uploadOptions.useVertexColor = true;
    uploadOptions.vertexScalars.resize(cube.vertices.size() / 6);
    for (size_t i = 0; i < uploadOptions.vertexScalars.size(); ++i) {
        uploadOptions.vertexScalars[i] = static_cast<float>(i);
    }
    uploadOptions.scalarMin = 0.0f;
    uploadOptions.scalarMax = static_cast<float>(uploadOptions.vertexScalars.size() - 1);
    uploadOptions.numBands = 8;
    uploadOptions.partVisibility[1] = false;
    if (!backend.uploadMesh(cube, uploadOptions)) {
        std::fprintf(stderr, "uploadMesh failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 7;
    }
    QMatrix4x4 mvp;
    mvp.perspective(45.0f, 1.0f, 0.01f, 10.0f);
    mvp.translate(0.0f, 0.0f, -2.0f);
    if (!backend.renderMeshFrame(mvp, QVector3D(0.48f, 0.72f, 0.76f))) {
        std::fprintf(stderr, "renderMeshFrame failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 8;
    }
    if (!backend.renderMeshFrame(mvp,
                                 QVector3D(0.48f, 0.72f, 0.76f),
                                 ModelDisplayMode::Wireframe)) {
        std::fprintf(stderr, "renderMeshFrame(wireframe) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 11;
    }
    if (!backend.renderMeshFrame(mvp,
                                 QVector3D(0.48f, 0.72f, 0.76f),
                                 ModelDisplayMode::SolidWireframe)) {
        std::fprintf(stderr, "renderMeshFrame(solid wireframe) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 12;
    }
    if (!backend.renderMeshFrame(mvp,
                                 QVector3D(0.48f, 0.72f, 0.76f),
                                 ModelDisplayMode::Points)) {
        std::fprintf(stderr, "renderMeshFrame(points) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 13;
    }
    int pickedElement = -1;
    if (!backend.pickElementAt(mvp, 32, 32, pickedElement)) {
        std::fprintf(stderr, "pickElementAt failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 17;
    }
    if (pickedElement < -1) {
        return 18;
    }

    uploadOptions.partVisibility[0] = false;
    if (!backend.uploadMesh(cube, uploadOptions)) {
        std::fprintf(stderr, "uploadMesh(hidden parts) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 14;
    }
    if (!backend.renderMeshFrame(mvp,
                                 QVector3D(0.48f, 0.72f, 0.76f),
                                 ModelDisplayMode::SolidWireframe)) {
        std::fprintf(stderr, "renderMeshFrame(hidden parts) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 15;
    }

    uploadOptions.partVisibility.clear();
    if (!backend.uploadMesh(cube, uploadOptions)) {
        std::fprintf(stderr, "uploadMesh(restore parts) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 16;
    }
    for (size_t i = 0; i < uploadOptions.vertexScalars.size(); ++i) {
        uploadOptions.vertexScalars[i] =
            static_cast<float>(uploadOptions.vertexScalars.size() - 1 - i);
    }
    if (!backend.uploadVertexScalars(uploadOptions.vertexScalars,
                                     uploadOptions.scalarMin,
                                     uploadOptions.scalarMax,
                                     uploadOptions.numBands,
                                     true)) {
        std::fprintf(stderr, "uploadVertexScalars failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 29;
    }
    if (!backend.renderMeshFrame(mvp,
                                 QVector3D(0.48f, 0.72f, 0.76f),
                                 ModelDisplayMode::SolidWireframe)) {
        std::fprintf(stderr, "renderMeshFrame(scalars) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 30;
    }
    const std::vector<float> selectionLine = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f
    };
    if (!backend.uploadSelectionLines(selectionLine)) {
        std::fprintf(stderr, "uploadSelectionLines failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 19;
    }
    if (!backend.uploadOverlayLines(cube.edgeVertices)) {
        std::fprintf(stderr, "uploadOverlayLines failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 22;
    }
    const std::vector<float> sliceLine = {
        -0.5f, 0.0f, 0.5f,
         0.5f, 0.0f, 0.5f
    };
    if (!backend.uploadSliceLines(sliceLine)) {
        std::fprintf(stderr, "uploadSliceLines failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 23;
    }
    if (!backend.uploadIsoSurfaceMesh(cube)) {
        std::fprintf(stderr, "uploadIsoSurfaceMesh failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 25;
    }
    if (!backend.uploadClipPreviewMesh(cube)) {
        std::fprintf(stderr, "uploadClipPreviewMesh failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 27;
    }
    if (!backend.renderMeshFrame(mvp,
                                 QVector3D(0.48f, 0.72f, 0.76f),
                                 ModelDisplayMode::SolidWireframe)) {
        std::fprintf(stderr, "renderMeshFrame(selection) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 20;
    }
    if (!backend.uploadSelectionLines({})) {
        std::fprintf(stderr, "uploadSelectionLines(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 21;
    }
    if (!backend.uploadOverlayLines({}) || !backend.uploadSliceLines({})) {
        std::fprintf(stderr, "clear overlay/slice lines failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 24;
    }
    if (!backend.uploadIsoSurfaceMesh(Mesh{})) {
        std::fprintf(stderr, "uploadIsoSurfaceMesh(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 26;
    }
    if (!backend.uploadClipPreviewMesh(Mesh{})) {
        std::fprintf(stderr, "uploadClipPreviewMesh(clear) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 28;
    }

    window.resize(96, 80);
    app.processEvents();
    backend.resizeLayer(window.width(), window.height(), window.devicePixelRatio());
    if (!backend.renderClearFrame(0.16f, 0.09f, 0.12f, 1.0f)) {
        std::fprintf(stderr, "renderClearFrame(resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 9;
    }
    if (!backend.renderMeshFrame(mvp, QVector3D(0.76f, 0.60f, 0.42f))) {
        std::fprintf(stderr, "renderMeshFrame(resize) failed: %s\n",
                     backend.lastError().toUtf8().constData());
        return 10;
    }

    backend.destroy();
    return 0;
}
