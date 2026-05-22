#include "Geometry.h"
#include "RenderBackendFactory.h"
#include "RenderSettings.h"
#include "RenderViewport.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>

#include <cassert>
#include <cstdio>
#include <vector>

static void pumpEvents(QApplication& app, int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms) {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(10);
    }
}

static Mesh makeMeshWithFrontEdges()
{
    Mesh mesh = Geometry::cube();
    mesh.edgeVertices = {
        -0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f, -0.5f, 0.5f,
         0.5f,  0.5f, 0.5f,
         0.5f,  0.5f, 0.5f,
        -0.5f,  0.5f, 0.5f,
        -0.5f,  0.5f, 0.5f,
        -0.5f, -0.5f, 0.5f,
    };
    mesh.edgeIndices = {0, 1, 1, 2, 2, 3, 3, 0};
    return mesh;
}

static std::vector<int> makeTriangleIds(const Mesh& mesh)
{
    std::vector<int> ids(mesh.indices.size() / 3);
    for (size_t i = 0; i < ids.size(); ++i) {
        ids[i] = 100 + static_cast<int>(i);
    }
    return ids;
}

static std::vector<int> makeTriangleParts(const Mesh& mesh)
{
    std::vector<int> parts(mesh.indices.size() / 3);
    for (size_t i = 0; i < parts.size(); ++i) {
        parts[i] = static_cast<int>(i % 2);
    }
    return parts;
}

static std::vector<int> makeVertexNodeIds(const Mesh& mesh)
{
    std::vector<int> ids(mesh.vertices.size() / 6);
    for (size_t i = 0; i < ids.size(); ++i) {
        ids[i] = static_cast<int>(i);
    }
    return ids;
}

static void pushMappings(RenderViewport& viewport, const Mesh& mesh)
{
    viewport.setTriangleToElementMap(makeTriangleIds(mesh));
    viewport.setVertexToNodeMap(makeVertexNodeIds(mesh));
    viewport.setTriangleToPartMap(makeTriangleParts(mesh));
    viewport.setEdgeToPartMap({0, 1, 0, 1});
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir settingsDir;
    assert(settingsDir.isValid());
    qputenv("FEMODELVIEWER_CONFIG_DIR", settingsDir.path().toLocal8Bit());
    RenderSettings::setPreferredBackend(RenderBackendKind::OpenGL);

    RenderViewport viewport;
    viewport.resize(360, 260);
    viewport.show();
    pumpEvents(app, 250);

    Mesh mesh = makeMeshWithFrontEdges();
    viewport.setMesh(mesh);
    pushMappings(viewport, mesh);
    viewport.fitToModel(glm::vec3(0.0f), 1.0f);
    viewport.setOverlayMesh(mesh);
    viewport.setOverlayVisible(true);
    viewport.setSliceLines({
        -0.5f, 0.0f, 0.0f,
         0.5f, 0.0f, 0.0f
    });
    viewport.setIsoSurfaceMesh(mesh);
    viewport.setClipPlanePreview(glm::vec3(-0.5f),
                                 glm::vec3(0.5f),
                                 glm::vec3(0.0f),
                                 glm::vec3(0.0f, 0.0f, 1.0f));
    viewport.setVertexScalars(std::vector<float>(mesh.vertices.size() / 6, 0.5f),
                              0.0f,
                              1.0f,
                              6);
    viewport.setPartVisibility(1, false);
    viewport.selectByIds(PickMode::Element, {100});
    viewport.refresh();
    pumpEvents(app, 300);

    if (isRenderBackendAvailable(RenderBackendKind::Vulkan)) {
        viewport.setPreferredRenderBackend(RenderBackendKind::Vulkan);
        pumpEvents(app, 250);
        assert(viewport.requestedRenderBackendKind() == RenderBackendKind::Vulkan);
        assert(viewport.activeRenderBackendKind() == RenderBackendKind::OpenGL);
        viewport.refresh();
        pumpEvents(app, 250);
    }

    // 模拟 MainWindow 重新加载模型前的清理顺序，确保 Vulkan 缓存资源可清空并重建。
    viewport.clearSliceLines();
    viewport.clearIsoSurface();
    viewport.clearClipPlanePreview();
    viewport.setOverlayVisible(false);
    viewport.setUseVertexColor(false);

    Mesh reloaded = Geometry::tetrahedron();
    viewport.setMesh(reloaded);
    viewport.setTriangleToElementMap(makeTriangleIds(reloaded));
    viewport.setTriangleToPartMap(makeTriangleParts(reloaded));
    viewport.fitToModel(glm::vec3(0.0f), 1.0f);
    viewport.setPickMode(PickMode::Part);
    viewport.highlightParts({0});
    viewport.refresh();
    pumpEvents(app, 300);

    viewport.setPreferredRenderBackend(RenderBackendKind::OpenGL);
    pumpEvents(app, 250);
    assert(viewport.activeRenderBackendKind() == RenderBackendKind::OpenGL);

    if (isRenderBackendAvailable(RenderBackendKind::Vulkan)) {
        viewport.setPreferredRenderBackend(RenderBackendKind::Vulkan);
        pumpEvents(app, 250);
        assert(viewport.requestedRenderBackendKind() == RenderBackendKind::Vulkan);
        assert(viewport.activeRenderBackendKind() == RenderBackendKind::OpenGL);
    }

    if (isRenderBackendAvailable(RenderBackendKind::Metal)) {
        RenderSettings::setPreferredBackend(RenderBackendKind::Metal);
        RenderViewport metalViewport;
        metalViewport.resize(240, 180);
        metalViewport.show();
        pumpEvents(app, 300);
        assert(metalViewport.requestedRenderBackendKind() == RenderBackendKind::Metal);
        assert(metalViewport.activeRenderBackendKind() == RenderBackendKind::Metal);
        Mesh metalMesh = makeMeshWithFrontEdges();
        metalViewport.setMesh(metalMesh);
        pushMappings(metalViewport, metalMesh);
        metalViewport.fitToModel(glm::vec3(0.0f), 1.0f);
        metalViewport.setOverlayMesh(metalMesh);
        metalViewport.setOverlayVisible(true);
        metalViewport.setSliceLines({
            -0.5f, 0.0f, 0.0f,
             0.5f, 0.0f, 0.0f
        });
        metalViewport.setIsoSurfaceMesh(metalMesh);
        metalViewport.setClipPlanePreview(glm::vec3(-0.5f),
                                          glm::vec3(0.5f),
                                          glm::vec3(0.0f),
                                          glm::vec3(0.0f, 0.0f, 1.0f));
        std::vector<float> scalars(metalMesh.vertices.size() / 6);
        for (size_t i = 0; i < scalars.size(); ++i) {
            scalars[i] = static_cast<float>(i);
        }
        metalViewport.setVertexScalars(scalars, 0.0f, static_cast<float>(scalars.size()), 5);
        metalViewport.setPartVisibility(1, false);
        metalViewport.selectByIds(PickMode::Element, {100});
        metalViewport.selectByIds(PickMode::Node, {0});
        metalViewport.selectByIds(PickMode::Part, {0});
        metalViewport.setModelDisplayMode(ModelDisplayMode::Points);
        metalViewport.refresh();
        pumpEvents(app, 100);
        assert(!metalViewport.glRenderer().isEmpty());
        assert(metalViewport.renderDiagnostics().contains(QStringLiteral("Drawable:")));
        assert(metalViewport.renderDiagnostics().contains(QStringLiteral("Layer: OK")));
        metalViewport.clearSliceLines();
        metalViewport.clearIsoSurface();
        metalViewport.clearClipPlanePreview();
        metalViewport.setUseVertexColor(false);
        metalViewport.setOverlayVisible(false);
        metalViewport.hide();
        pumpEvents(app, 100);
    }

    viewport.hide();
    pumpEvents(app, 100);
    printf("RenderViewport state cleanup/deferred RHI switch test passed.\\n");
    return 0;
}
