#include "MetalRenderBackend.h"

#include "MetalAttachmentResourceBuilder.h"
#include "MetalDeviceFactory.h"
#include "MetalDrawableFrameSubmitter.h"
#include "MetalLineUpload.h"
#include "MetalMeshFramePass.h"
#include "MetalMeshFramePassBuilder.h"
#include "MetalMeshScalarUpdater.h"
#include "MetalPipelineFactory.h"
#include "MetalPickPass.h"
#include "MetalRenderPassFactory.h"
#include "MetalShaderSources.h"
#include "MetalShaderTypes.h"
#include "MetalSurfaceUploadBuilder.h"
#include "MetalSurfaceUploader.h"
#include "MetalUniformUtils.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <vector>

#import <Metal/Metal.h>

namespace {

constexpr size_t kMetalFrameUniformAlignment = 256;
constexpr int kMetalFrameUniformSlotCount = 3;
constexpr int kAxesVerticesPerAxis = 26;
constexpr float kPi = 3.14159265358979323846f;

struct Vec3 {
    float x;
    float y;
    float z;
};

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& v, float s) { return {v.x * s, v.y * s, v.z * s}; }
Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 normalize(const Vec3& v)
{
    const float len2 = dot(v, v);
    if (len2 <= 1.0e-12f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return scale(v, 1.0f / std::sqrt(len2));
}

size_t alignMetalUniformOffset(size_t size)
{
    return (size + kMetalFrameUniformAlignment - 1) &
        ~(kMetalFrameUniformAlignment - 1);
}

size_t writeFrameUniform(void* contents, size_t baseOffset, size_t& cursor, const void* data, size_t size)
{
    const size_t offset = baseOffset + cursor;
    std::memcpy(static_cast<char*>(contents) + offset, data, size);
    cursor += alignMetalUniformOffset(size);
    return offset;
}

void writeDrawUniform(void* contents,
                      size_t baseOffset,
                      size_t& cursor,
                      MetalMeshFramePassDraw& draw)
{
    draw.uniformOffset = writeFrameUniform(contents,
                                           baseOffset,
                                           cursor,
                                           &draw.uniforms,
                                           sizeof(draw.uniforms));
}

std::vector<float> buildAxesLineVertices()
{
    std::vector<float> vertices;
    vertices.reserve(static_cast<size_t>(kAxesVerticesPerAxis * 3 * 3));
    auto appendLine = [&vertices](const Vec3& a, const Vec3& b) {
        vertices.insert(vertices.end(), {a.x, a.y, a.z, b.x, b.y, b.z});
    };

    const std::array<Vec3, 3> dirs = {{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    }};
    const std::array<Vec3, 3> ups = {{
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f}
    }};

    constexpr float shaftLength = 0.78f;
    constexpr float tipLength = 1.0f;
    constexpr float shaftRadius = 0.025f;
    constexpr float arrowRadius = 0.12f;
    for (size_t axis = 0; axis < dirs.size(); ++axis) {
        const Vec3 dir = dirs[axis];
        const Vec3 right = cross(dir, ups[axis]);
        const Vec3 up = cross(right, dir);
        const Vec3 origin{0.0f, 0.0f, 0.0f};
        const Vec3 shaftEnd = scale(dir, shaftLength);
        const Vec3 tip = scale(dir, tipLength);
        const std::array<Vec3, 4> base = {{
            add(shaftEnd, scale(right, arrowRadius)),
            add(shaftEnd, scale(up, arrowRadius)),
            add(shaftEnd, scale(right, -arrowRadius)),
            add(shaftEnd, scale(up, -arrowRadius))
        }};

        appendLine(origin, shaftEnd);
        appendLine(scale(right, shaftRadius), add(shaftEnd, scale(right, shaftRadius)));
        appendLine(scale(up, shaftRadius), add(shaftEnd, scale(up, shaftRadius)));
        appendLine(scale(right, -shaftRadius), add(shaftEnd, scale(right, -shaftRadius)));
        appendLine(scale(up, -shaftRadius), add(shaftEnd, scale(up, -shaftRadius)));
        for (const Vec3& p : base) {
            appendLine(tip, p);
        }
        for (size_t i = 0; i < base.size(); ++i) {
            appendLine(base[i], base[(i + 1) % base.size()]);
        }
    }
    return vertices;
}

std::vector<MetalMeshVertex> buildAxesSolidVertices()
{
    std::vector<MetalMeshVertex> vertices;
    vertices.reserve(3 * 24 * 12 + 8 * 12 * 6);

    auto appendVertex = [&vertices](const Vec3& position, const Vec3& normal, const Vec3& color) {
        MetalMeshVertex vertex{};
        vertex.position[0] = position.x;
        vertex.position[1] = position.y;
        vertex.position[2] = position.z;
        const Vec3 n = normalize(normal);
        vertex.normal[0] = n.x;
        vertex.normal[1] = n.y;
        vertex.normal[2] = n.z;
        vertex.color[0] = color.x;
        vertex.color[1] = color.y;
        vertex.color[2] = color.z;
        vertices.push_back(vertex);
    };
    auto appendTri = [&appendVertex](const Vec3& a,
                                     const Vec3& b,
                                     const Vec3& c,
                                     const Vec3& color) {
        const Vec3 normal = normalize(cross(sub(b, a), sub(c, a)));
        appendVertex(a, normal, color);
        appendVertex(b, normal, color);
        appendVertex(c, normal, color);
    };

    constexpr int segs = 24;
    constexpr float shaftLen = 0.70f;
    constexpr float shaftRadius = 0.028f;
    constexpr float coneRadius = 0.10f;
    constexpr float coneLen = 0.30f;
    constexpr float ballRadius = 0.065f;
    const std::array<Vec3, 3> dirs = {{
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f}
    }};
    const std::array<Vec3, 3> ups = {{
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f}
    }};
    const std::array<Vec3, 3> colors = {{
        {0.95f, 0.30f, 0.30f},
        {0.35f, 0.90f, 0.35f},
        {0.35f, 0.55f, 1.00f}
    }};

    for (size_t axis = 0; axis < dirs.size(); ++axis) {
        const Vec3 dir = dirs[axis];
        const Vec3 right = normalize(cross(dir, ups[axis]));
        const Vec3 up = normalize(cross(right, dir));
        const Vec3 color = colors[axis];
        const Vec3 shaftColor = scale(color, 0.85f);
        for (int i = 0; i < segs; ++i) {
            const float a0 = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(segs);
            const float a1 = 2.0f * kPi * static_cast<float>(i + 1) / static_cast<float>(segs);
            const Vec3 radial0 = add(scale(right, std::cos(a0)), scale(up, std::sin(a0)));
            const Vec3 radial1 = add(scale(right, std::cos(a1)), scale(up, std::sin(a1)));
            const Vec3 b0 = scale(radial0, shaftRadius);
            const Vec3 b1 = scale(radial1, shaftRadius);
            const Vec3 t0 = add(scale(dir, shaftLen), b0);
            const Vec3 t1 = add(scale(dir, shaftLen), b1);
            appendVertex(b0, radial0, shaftColor);
            appendVertex(t0, radial0, shaftColor);
            appendVertex(t1, radial1, shaftColor);
            appendVertex(b0, radial0, shaftColor);
            appendVertex(t1, radial1, shaftColor);
            appendVertex(b1, radial1, shaftColor);

            const Vec3 coneBase0 = add(scale(dir, shaftLen), scale(radial0, coneRadius));
            const Vec3 coneBase1 = add(scale(dir, shaftLen), scale(radial1, coneRadius));
            const Vec3 tip = scale(dir, shaftLen + coneLen);
            appendTri(tip, coneBase0, coneBase1, color);
            appendTri(scale(dir, shaftLen), coneBase1, coneBase0, scale(color, 0.55f));
        }
    }

    const Vec3 ballColor{0.82f, 0.82f, 0.85f};
    constexpr int ballRings = 8;
    constexpr int ballSectors = 12;
    for (int r = 0; r < ballRings; ++r) {
        const float phi0 = kPi * static_cast<float>(r) / static_cast<float>(ballRings) - kPi * 0.5f;
        const float phi1 = kPi * static_cast<float>(r + 1) / static_cast<float>(ballRings) - kPi * 0.5f;
        for (int s = 0; s < ballSectors; ++s) {
            const float theta0 = 2.0f * kPi * static_cast<float>(s) / static_cast<float>(ballSectors);
            const float theta1 = 2.0f * kPi * static_cast<float>(s + 1) / static_cast<float>(ballSectors);
            const Vec3 p00 = scale({std::cos(phi0) * std::cos(theta0), std::sin(phi0), std::cos(phi0) * std::sin(theta0)}, ballRadius);
            const Vec3 p10 = scale({std::cos(phi1) * std::cos(theta0), std::sin(phi1), std::cos(phi1) * std::sin(theta0)}, ballRadius);
            const Vec3 p01 = scale({std::cos(phi0) * std::cos(theta1), std::sin(phi0), std::cos(phi0) * std::sin(theta1)}, ballRadius);
            const Vec3 p11 = scale({std::cos(phi1) * std::cos(theta1), std::sin(phi1), std::cos(phi1) * std::sin(theta1)}, ballRadius);
            appendVertex(p00, p00, ballColor);
            appendVertex(p10, p10, ballColor);
            appendVertex(p11, p11, ballColor);
            appendVertex(p00, p00, ballColor);
            appendVertex(p11, p11, ballColor);
            appendVertex(p01, p01, ballColor);
        }
    }

    return vertices;
}

} // namespace

MetalRenderBackend::~MetalRenderBackend()
{
    destroy();
}

bool MetalRenderBackend::isSystemAvailable()
{
    return isMetalSystemDeviceAvailable();
}

void MetalRenderBackend::initialize()
{
    if (initialized_) {
        return;
    }

    MetalDeviceContext context = createMetalDeviceContext();
    info_ = context.info;
    lastError_ = context.error;
    if (context.device && context.commandQueue) {
        device_.adopt(context.device);
        commandQueue_.adopt(context.commandQueue);
        lastError_.clear();
    }
    initialized_ = true;
}

bool MetalRenderBackend::ensureBackgroundPipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalBackgroundShaderSource,
        QStringLiteral("background"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::None,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          backgroundPipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureMeshPipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalMeshShaderSource,
        QStringLiteral("mesh"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Mesh,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          meshPipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureIsoSurfacePipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalMeshShaderSource,
        QStringLiteral("iso surface"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Mesh,
        true
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          isoSurfacePipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureLinePipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalLineShaderSource,
        QStringLiteral("line"),
        static_cast<unsigned long>(MTLPixelFormatBGRA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Line,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          linePipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensurePickPipeline()
{
    const MetalRenderPipelineConfig config{
        kMetalPickShaderSource,
        QStringLiteral("pick"),
        static_cast<unsigned long>(MTLPixelFormatRGBA8Unorm),
        static_cast<unsigned long>(MTLPixelFormatDepth32Float),
        MetalPipelineVertexLayout::Pick,
        false
    };
    return ensureMetalRenderPipelineState(device_.handle(),
                                          config,
                                          pickPipelineState_,
                                          lastError_);
}

bool MetalRenderBackend::ensureAxesResources()
{
    if (axesLineVertexBuffer_.isValid() &&
        axesLineVertexCount_ >= kAxesVerticesPerAxis * 3 &&
        axesSolidVertexBuffer_.isValid() &&
        axesSolidVertexCount_ > 0) {
        return true;
    }
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    const std::vector<float> axesVertices = buildAxesLineVertices();
    if (!axesLineVertexBuffer_.upload(device_.handle(),
                                      axesVertices.data(),
                                      axesVertices.size() * sizeof(float),
                                      QStringLiteral("axes line"),
                                      lastError_)) {
        axesLineVertexCount_ = 0;
        return false;
    }
    axesLineVertexCount_ = static_cast<int>(axesVertices.size() / 3);

    const std::vector<MetalMeshVertex> axesSolidVertices = buildAxesSolidVertices();
    if (!axesSolidVertexBuffer_.upload(device_.handle(),
                                       axesSolidVertices.data(),
                                       axesSolidVertices.size() * sizeof(MetalMeshVertex),
                                       QStringLiteral("axes solid"),
                                       lastError_)) {
        axesSolidVertexCount_ = 0;
        return false;
    }
    axesSolidVertexCount_ = static_cast<int>(axesSolidVertices.size());
    return true;
}

bool MetalRenderBackend::ensureDepthResources()
{
    return ensureMetalFrameAttachmentResources(device_.handle(),
                                               drawableSize_,
                                               depthStencilState_,
                                               overlayDepthStencilState_,
                                               depthTexture_,
                                               lastError_);
}

bool MetalRenderBackend::ensurePickResources()
{
    return ensureMetalPickAttachmentResources(device_.handle(),
                                              drawableSize_,
                                              pickColorTexture_,
                                              pickDepthTexture_,
                                              pickReadbackBuffer_,
                                              lastError_);
}

bool MetalRenderBackend::attachLayer(void* metalLayer, const QSize& drawableSize)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }
    if (!metalLayer) {
        lastError_ = QStringLiteral("Metal layer is null");
        return false;
    }

    destroyLayer();
    metalLayer_.retain(metalLayer);
    updateDrawableSize(drawableSize);
    return true;
}

void MetalRenderBackend::updateDrawableSize(const QSize& drawableSize)
{
    if (!metalLayer_.isValid()) {
        return;
    }

    drawableSize_ = QSize(std::max(1, drawableSize.width()),
                          std::max(1, drawableSize.height()));
    destroyDepthResources();
    destroyPickResources();
}

void MetalRenderBackend::setBackgroundGradient(const QVector3D& topColor, const QVector3D& bottomColor)
{
    backgroundTopColor_ = topColor;
    backgroundBottomColor_ = bottomColor;
}

void MetalRenderBackend::setViewportGridVisible(bool visible)
{
    viewportGridVisible_ = visible;
}

void MetalRenderBackend::setViewportGridParams(float alpha, float minorStep, float fineAlpha)
{
    viewportGridVisible_ = alpha > 0.0f;
    viewportGridMinorStep_ = minorStep;
    viewportGridFineAlpha_ = fineAlpha;
}

bool MetalRenderBackend::uploadMesh(const Mesh& mesh, const MetalMeshUploadOptions& options)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    destroyMeshResources();
    destroyEdgeResources();
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    meshUseVertexScalars_ = options.useVertexColor && !options.vertexScalars.empty();
    meshScalarMin_ = options.scalarMin;
    meshScalarMax_ = options.scalarMax;
    meshNumBands_ = std::max(1, options.numBands);
    MetalMeshUploadData uploadData = buildMetalMeshUploadData(mesh, options);
    meshVertexCpuCache_ = uploadData.vertices;
    meshScalarSourceIndices_ = uploadData.scalarSourceIndices;
    const MetalMeshBufferTargets targets{
        &meshVertexBuffer_,
        &meshIndexBuffer_,
        &pointVertexBuffer_,
        &edgeVertexBuffer_,
        &edgeIndexBuffer_,
        &meshVertexCount_,
        &meshIndexCount_,
        &edgeVertexCount_,
        &edgeIndexCount_
    };
    if (!uploadMetalMeshBuffers(device_.handle(),
                                commandQueue_.handle(),
                                mesh,
                                uploadData,
                                targets,
                                lastError_)) {
        destroyMeshResources();
        destroyEdgeResources();
        return false;
    }

    return true;
}

bool MetalRenderBackend::uploadVertexScalars(const std::vector<float>& scalars,
                                             float minVal,
                                             float maxVal,
                                             int numBands,
                                             bool useScalars)
{
    lastError_.clear();
    meshUseVertexScalars_ = useScalars && !scalars.empty();
    meshScalarMin_ = minVal;
    meshScalarMax_ = maxVal;
    meshNumBands_ = std::max(1, numBands);
    if (!meshUseVertexScalars_) {
        return true;
    }
    if (!meshVertexBuffer_.isValid() || meshScalarSourceIndices_.empty()) {
        lastError_ = QStringLiteral("Metal mesh scalar buffer is not initialized");
        return false;
    }

    const size_t count = std::min(meshScalarSourceIndices_.size(),
                                  static_cast<size_t>(std::max(0, meshVertexCount_)));
    if (!meshVertexCpuCache_.empty()) {
        for (size_t i = 0; i < count && i < meshVertexCpuCache_.size(); ++i) {
            const unsigned int sourceIndex = meshScalarSourceIndices_[i];
            meshVertexCpuCache_[i].scalar = sourceIndex < scalars.size() ? scalars[sourceIndex] : 0.0f;
        }
    }

    if (!meshVertexBuffer_.isHostVisible()) {
        if (meshVertexCpuCache_.empty()) {
            lastError_ = QStringLiteral("Metal mesh vertex CPU cache is empty");
            return false;
        }
        return meshVertexBuffer_.uploadPrivate(device_.handle(),
                                               commandQueue_.handle(),
                                               meshVertexCpuCache_.data(),
                                               meshVertexCpuCache_.size() * sizeof(MetalMeshVertex),
                                               QStringLiteral("mesh vertex scalar update"),
                                               lastError_);
    }

    @autoreleasepool {
        return updateMetalMeshVertexScalars(meshVertexBuffer_.handle(),
                                            meshVertexCount_,
                                            meshScalarSourceIndices_,
                                            scalars,
                                            lastError_);
    }
}

bool MetalRenderBackend::uploadLineBuffer(const std::vector<float>& lineVertices,
                                          MetalBufferResource& buffer,
                                          int& vertexCount,
                                          const QString& label)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    return uploadMetalLineVertices(device_.handle(), lineVertices, buffer, vertexCount, label, lastError_);
}

bool MetalRenderBackend::uploadOverlayLines(const std::vector<float>& lineVertices)
{
    return uploadLineBuffer(lineVertices,
                            overlayVertexBuffer_,
                            overlayVertexCount_,
                            QStringLiteral("overlay"));
}

bool MetalRenderBackend::uploadSliceLines(const std::vector<float>& lineVertices)
{
    return uploadLineBuffer(lineVertices,
                            sliceVertexBuffer_,
                            sliceVertexCount_,
                            QStringLiteral("slice"));
}

bool MetalRenderBackend::uploadIsoSurfaceMesh(const Mesh& mesh)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    destroyIsoSurfaceResources();
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    MetalSurfaceUploadData uploadData =
        buildMetalSurfaceUploadData(mesh, QVector3D(0.2f, 0.8f, 0.4f));
    const MetalSurfaceBufferTargets targets{
        &isoSurfaceVertexBuffer_,
        &isoSurfaceIndexBuffer_,
        &isoSurfaceIndexCount_
    };
    if (!uploadMetalSurfaceBuffers(device_.handle(),
                                   commandQueue_.handle(),
                                   uploadData,
                                   targets,
                                   QStringLiteral("iso surface"),
                                   lastError_)) {
        destroyIsoSurfaceResources();
        return false;
    }

    return true;
}

bool MetalRenderBackend::uploadClipPreviewMesh(const Mesh& mesh)
{
    lastError_.clear();
    initialize();
    if (!initialized_ || !device_.isValid()) {
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Metal backend is not initialized");
        }
        return false;
    }

    destroyClipPreviewResources();
    if (mesh.vertices.empty() || mesh.indices.empty() || mesh.vertices.size() % 6 != 0) {
        return true;
    }

    MetalSurfaceUploadData uploadData =
        buildMetalSurfaceUploadData(mesh, QVector3D(0.35f, 0.55f, 1.0f));
    const MetalSurfaceBufferTargets surfaceTargets{
        &clipPreviewVertexBuffer_,
        &clipPreviewIndexBuffer_,
        &clipPreviewIndexCount_
    };
    const MetalClipPreviewLineTargets lineTargets{
        &clipPreviewLineVertexBuffer_,
        &clipPreviewLineVertexCount_
    };
    if (!uploadMetalClipPreviewBuffers(device_.handle(),
                                       commandQueue_.handle(),
                                       mesh,
                                       uploadData,
                                       surfaceTargets,
                                       lineTargets,
                                       lastError_)) {
        destroyClipPreviewResources();
        return false;
    }

    return true;
}

bool MetalRenderBackend::uploadSelectionLines(const std::vector<float>& lineVertices)
{
    return uploadLineBuffer(lineVertices,
                            selectionVertexBuffer_,
                            selectionVertexCount_,
                            QStringLiteral("selection"));
}

bool MetalRenderBackend::renderClearFrame(float red,
                                         float green,
                                         float blue,
                                         float alpha,
                                         const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        lastError_ = QStringLiteral("Metal backend is not initialized");
        return false;
    }
    if (!metalLayer_.isValid()) {
        lastError_ = QStringLiteral("Metal layer is not initialized");
        return false;
    }

    const MetalMeshFrameDrawFlags drawFlags{
        .surface = false,
        .edges = false,
        .points = false,
        .isoSurface = false,
        .clipPreview = false,
        .overlay = false,
        .slice = false,
        .clipPreviewLines = false,
        .selection = false,
        .axes = !axesMvp.isIdentity() && drawableSize_.width() > 16 && drawableSize_.height() > 16,
        .axesSolid = !axesMvp.isIdentity() && drawableSize_.width() > 16 && drawableSize_.height() > 16
    };
    if (!ensureMeshFrameResources(drawFlags)) {
        return false;
    }

    const MetalMeshFrameResourceHandles resources = buildMeshFrameResourceHandles();
    const MetalMeshFrameUniformSet frameUniforms =
        buildMetalMeshFrameUniformSet(QMatrix4x4(),
                                      QVector3D(0.0f, 0.0f, 0.0f),
                                      0.0f,
                                      1.0f,
                                      std::max(1, meshNumBands_),
                                      false,
                                      axesMvp);
    MetalMeshFramePassInputs framePass = buildMetalMeshFramePassInputs(drawFlags,
                                                                      resources,
                                                                      frameUniforms);
    if (!prepareFrameUniformBuffer(framePass)) {
        return false;
    }

    @autoreleasepool {
        return submitMetalDrawableFrame(metalLayer_.handle(),
                                        commandQueue_.handle(),
                                        depthTexture_.handle(),
                                        red,
                                        green,
                                        blue,
                                        alpha,
                                        framePass,
                                        lastError_);
    }
}

MetalMeshFrameDrawFlags MetalRenderBackend::buildMeshFrameDrawFlags(
    ModelDisplayMode displayMode,
    const QMatrix4x4& axesMvp) const
{
    MetalMeshFrameDrawFlags drawFlags;
    drawFlags.surface = displayMode == ModelDisplayMode::Solid ||
        displayMode == ModelDisplayMode::SolidWireframe;
    drawFlags.edges =
        (displayMode == ModelDisplayMode::Wireframe ||
         displayMode == ModelDisplayMode::SolidWireframe) &&
        edgeIndexCount_ > 0 && edgeVertexBuffer_.isValid() && edgeIndexBuffer_.isValid();
    drawFlags.points = displayMode == ModelDisplayMode::Points && pointVertexBuffer_.isValid();
    drawFlags.overlay = overlayVertexCount_ > 0 && overlayVertexBuffer_.isValid();
    drawFlags.slice = sliceVertexCount_ > 0 && sliceVertexBuffer_.isValid();
    drawFlags.isoSurface =
        isoSurfaceIndexCount_ > 0 &&
        isoSurfaceVertexBuffer_.isValid() &&
        isoSurfaceIndexBuffer_.isValid();
    drawFlags.clipPreview =
        clipPreviewIndexCount_ > 0 &&
        clipPreviewVertexBuffer_.isValid() &&
        clipPreviewIndexBuffer_.isValid();
    drawFlags.clipPreviewLines =
        clipPreviewLineVertexCount_ > 0 && clipPreviewLineVertexBuffer_.isValid();
    drawFlags.selection =
        selectionVertexCount_ > 0 && selectionVertexBuffer_.isValid();
    drawFlags.axes =
        !axesMvp.isIdentity() && drawableSize_.width() > 16 && drawableSize_.height() > 16;
    drawFlags.axesSolid = drawFlags.axes;
    return drawFlags;
}

bool MetalRenderBackend::ensureMeshFrameResources(const MetalMeshFrameDrawFlags& drawFlags)
{
    if (!ensureBackgroundPipeline()) {
        return false;
    }
    if ((drawFlags.surface || drawFlags.axesSolid) && !ensureMeshPipeline()) {
        return false;
    }
    if ((drawFlags.edges ||
         drawFlags.points ||
         drawFlags.overlay ||
         drawFlags.slice ||
         drawFlags.clipPreviewLines ||
         drawFlags.selection ||
         drawFlags.axes) &&
        !ensureLinePipeline()) {
        return false;
    }
    if (drawFlags.axes && !ensureAxesResources()) {
        return false;
    }
    if ((drawFlags.isoSurface || drawFlags.clipPreview) && !ensureIsoSurfacePipeline()) {
        return false;
    }
    return ensureDepthResources();
}

MetalMeshFrameResourceHandles MetalRenderBackend::buildMeshFrameResourceHandles() const
{
    MetalMeshFrameResourceHandles frameResources;
    frameResources.backgroundPipelineState = backgroundPipelineState_.handle();
    frameResources.meshPipelineState = meshPipelineState_.handle();
    frameResources.isoSurfacePipelineState = isoSurfacePipelineState_.handle();
    frameResources.linePipelineState = linePipelineState_.handle();
    frameResources.depthStencilState = depthStencilState_.handle();
    frameResources.overlayDepthStencilState = overlayDepthStencilState_.handle();
    frameResources.drawableSize = drawableSize_;
    frameResources.backgroundTopColor = backgroundTopColor_;
    frameResources.backgroundBottomColor = backgroundBottomColor_;
    frameResources.backgroundGridVisible = viewportGridVisible_;
    frameResources.backgroundGridMinorStep = viewportGridMinorStep_;
    frameResources.backgroundGridFineAlpha = viewportGridFineAlpha_;
    frameResources.meshVertexBuffer = meshVertexBuffer_.handle();
    frameResources.meshIndexBuffer = meshIndexBuffer_.handle();
    frameResources.meshVertexCount = meshVertexCount_;
    frameResources.meshIndexCount = meshIndexCount_;
    frameResources.pointVertexBuffer = pointVertexBuffer_.handle();
    frameResources.edgeVertexBuffer = edgeVertexBuffer_.handle();
    frameResources.edgeIndexBuffer = edgeIndexBuffer_.handle();
    frameResources.edgeVertexCount = edgeVertexCount_;
    frameResources.edgeIndexCount = edgeIndexCount_;
    frameResources.isoSurfaceVertexBuffer = isoSurfaceVertexBuffer_.handle();
    frameResources.isoSurfaceIndexBuffer = isoSurfaceIndexBuffer_.handle();
    frameResources.isoSurfaceIndexCount = isoSurfaceIndexCount_;
    frameResources.clipPreviewVertexBuffer = clipPreviewVertexBuffer_.handle();
    frameResources.clipPreviewIndexBuffer = clipPreviewIndexBuffer_.handle();
    frameResources.clipPreviewIndexCount = clipPreviewIndexCount_;
    frameResources.overlayVertexBuffer = overlayVertexBuffer_.handle();
    frameResources.overlayVertexCount = overlayVertexCount_;
    frameResources.sliceVertexBuffer = sliceVertexBuffer_.handle();
    frameResources.sliceVertexCount = sliceVertexCount_;
    frameResources.clipPreviewLineVertexBuffer = clipPreviewLineVertexBuffer_.handle();
    frameResources.clipPreviewLineVertexCount = clipPreviewLineVertexCount_;
    frameResources.selectionVertexBuffer = selectionVertexBuffer_.handle();
    frameResources.selectionVertexCount = selectionVertexCount_;
    frameResources.axesLineVertexBuffer = axesLineVertexBuffer_.handle();
    frameResources.axesLineVertexCount = axesLineVertexCount_;
    frameResources.axesSolidVertexBuffer = axesSolidVertexBuffer_.handle();
    frameResources.axesSolidVertexCount = axesSolidVertexCount_;
    return frameResources;
}

bool MetalRenderBackend::ensurePickFrameResources()
{
    return ensurePickPipeline() && ensureDepthResources() && ensurePickResources();
}

MetalPickPassResourceHandles MetalRenderBackend::buildPickPassResourceHandles() const
{
    MetalPickPassResourceHandles resources;
    resources.commandQueue = commandQueue_.handle();
    resources.colorTexture = pickColorTexture_.handle();
    resources.depthTexture = pickDepthTexture_.handle();
    resources.readbackBuffer = pickReadbackBuffer_.handle();
    resources.depthStencilState = depthStencilState_.handle();
    resources.pickPipelineState = pickPipelineState_.handle();
    resources.meshVertexBuffer = meshVertexBuffer_.handle();
    resources.meshIndexBuffer = meshIndexBuffer_.handle();
    resources.meshIndexCount = meshIndexCount_;
    return resources;
}

bool MetalRenderBackend::prepareFrameUniformBuffer(MetalMeshFramePassInputs& framePass)
{
    const size_t uniformStride = alignMetalUniformOffset(sizeof(MetalMeshUniforms));
    const size_t backgroundStride = alignMetalUniformOffset(sizeof(MetalBackgroundUniforms));
    const size_t slotSize = backgroundStride + uniformStride * 13;
    const size_t totalSize = slotSize * kMetalFrameUniformSlotCount;

    if (!frameUniformBuffer_.isValid() || frameUniformBuffer_.sizeBytes() < totalSize) {
        if (!frameUniformBuffer_.allocate(device_.handle(),
                                          totalSize,
                                          QStringLiteral("frame uniforms"),
                                          lastError_)) {
            return false;
        }
        frameUniformSlotSize_ = slotSize;
        frameUniformSlotIndex_ = 0;
    }

    id<MTLBuffer> buffer = static_cast<id<MTLBuffer>>(frameUniformBuffer_.handle());
    void* contents = [buffer contents];
    if (!contents) {
        lastError_ = QStringLiteral("Metal frame uniform buffer is not mappable");
        return false;
    }

    const size_t baseOffset = frameUniformSlotSize_ *
        static_cast<size_t>(frameUniformSlotIndex_);
    frameUniformSlotIndex_ = (frameUniformSlotIndex_ + 1) % kMetalFrameUniformSlotCount;
    size_t cursor = 0;

    framePass.uniformBuffer = frameUniformBuffer_.handle();
    framePass.backgroundUniformOffset = writeFrameUniform(contents,
                                                          baseOffset,
                                                          cursor,
                                                          &framePass.backgroundUniforms,
                                                          sizeof(framePass.backgroundUniforms));
    writeDrawUniform(contents, baseOffset, cursor, framePass.surface);
    writeDrawUniform(contents, baseOffset, cursor, framePass.edges);
    writeDrawUniform(contents, baseOffset, cursor, framePass.points);
    writeDrawUniform(contents, baseOffset, cursor, framePass.isoSurface);
    writeDrawUniform(contents, baseOffset, cursor, framePass.clipPreview);
    writeDrawUniform(contents, baseOffset, cursor, framePass.overlay);
    writeDrawUniform(contents, baseOffset, cursor, framePass.slice);
    writeDrawUniform(contents, baseOffset, cursor, framePass.clipPreviewLines);
    writeDrawUniform(contents, baseOffset, cursor, framePass.selection);
    writeDrawUniform(contents, baseOffset, cursor, framePass.axesSolid);

    const std::array<std::array<float, 4>, 3> axisColors = {{
        {{0.95f, 0.30f, 0.30f, 1.0f}},
        {{0.35f, 0.90f, 0.35f, 1.0f}},
        {{0.35f, 0.55f, 1.00f, 1.0f}}
    }};
    for (size_t axis = 0; axis < axisColors.size(); ++axis) {
        MetalMeshUniforms axesUniforms = framePass.axes.uniforms;
        setMetalUniformColor(axesUniforms,
                             axisColors[axis][0],
                             axisColors[axis][1],
                             axisColors[axis][2],
                             axisColors[axis][3]);
        framePass.axesUniformOffsets[axis] = writeFrameUniform(contents,
                                                               baseOffset,
                                                               cursor,
                                                               &axesUniforms,
                                                               sizeof(axesUniforms));
    }

    return true;
}

bool MetalRenderBackend::renderMeshFrame(const QMatrix4x4& mvp,
                                         const QVector3D& objectColor,
                                         ModelDisplayMode displayMode,
                                         float red,
                                         float green,
                                         float blue,
                                         float alpha,
                                         const QMatrix4x4& axesMvp)
{
    lastError_.clear();
    if (meshIndexCount_ <= 0 || !meshVertexBuffer_.isValid() || !meshIndexBuffer_.isValid()) {
        return renderClearFrame(red, green, blue, alpha, axesMvp);
    }
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        lastError_ = QStringLiteral("Metal backend is not initialized");
        return false;
    }
    if (!metalLayer_.isValid()) {
        lastError_ = QStringLiteral("Metal layer is not initialized");
        return false;
    }
    const MetalMeshFrameDrawFlags drawFlags = buildMeshFrameDrawFlags(displayMode, axesMvp);
    if (!ensureMeshFrameResources(drawFlags)) {
        return false;
    }

    const MetalMeshFrameUniformSet frameUniforms =
        buildMetalMeshFrameUniformSet(mvp,
                                      objectColor,
                                      meshScalarMin_,
                                      meshScalarMax_,
                                      meshNumBands_,
                                      meshUseVertexScalars_,
                                      axesMvp);
    const MetalMeshFrameResourceHandles frameResources = buildMeshFrameResourceHandles();
    MetalMeshFramePassInputs framePass =
        buildMetalMeshFramePassInputs(drawFlags, frameResources, frameUniforms);
    if (!prepareFrameUniformBuffer(framePass)) {
        return false;
    }

    @autoreleasepool {
        return submitMetalDrawableFrame(metalLayer_.handle(),
                                        commandQueue_.handle(),
                                        depthTexture_.handle(),
                                        red,
                                        green,
                                        blue,
                                        alpha,
                                        framePass,
                                        lastError_);
    }
}

bool MetalRenderBackend::pickElementAt(const QMatrix4x4& mvp, int x, int y, int& elementId)
{
    lastError_.clear();
    elementId = -1;
    if (meshIndexCount_ <= 0 || !meshVertexBuffer_.isValid() || !meshIndexBuffer_.isValid()) {
        return true;
    }
    if (!initialized_ || !device_.isValid() || !commandQueue_.isValid()) {
        lastError_ = QStringLiteral("Metal backend is not initialized");
        return false;
    }
    if (!ensurePickFrameResources()) {
        return false;
    }

    const int pickX = std::clamp(x, 0, std::max(0, drawableSize_.width() - 1));
    const int pickY = std::clamp(y, 0, std::max(0, drawableSize_.height() - 1));
    const MetalPickPassResourceHandles resources = buildPickPassResourceHandles();
    const MetalPickPassInputs pickPass =
        buildMetalPickPassInputs(resources, mvp, pickX, pickY);

    @autoreleasepool {
        if (!executeMetalPickPass(pickPass, elementId, lastError_)) {
            return false;
        }
    }

    return true;
}

void MetalRenderBackend::destroyLayer()
{
    metalLayer_.destroy();
    drawableSize_ = QSize();
    destroyDepthResources();
    destroyPickResources();
}

void MetalRenderBackend::destroyMeshResources()
{
    meshVertexBuffer_.destroy();
    meshIndexBuffer_.destroy();
    pointVertexBuffer_.destroy();
    meshVertexCount_ = 0;
    meshIndexCount_ = 0;
    meshUseVertexScalars_ = false;
    meshScalarMin_ = 0.0f;
    meshScalarMax_ = 1.0f;
    meshNumBands_ = 10;
    meshVertexCpuCache_.clear();
    meshScalarSourceIndices_.clear();
}

void MetalRenderBackend::destroyEdgeResources()
{
    edgeVertexBuffer_.destroy();
    edgeIndexBuffer_.destroy();
    edgeVertexCount_ = 0;
    edgeIndexCount_ = 0;
}

void MetalRenderBackend::destroyIsoSurfaceResources()
{
    isoSurfaceVertexBuffer_.destroy();
    isoSurfaceIndexBuffer_.destroy();
    isoSurfaceIndexCount_ = 0;
}

void MetalRenderBackend::destroyClipPreviewResources()
{
    clipPreviewVertexBuffer_.destroy();
    clipPreviewIndexBuffer_.destroy();
    clipPreviewLineVertexBuffer_.destroy();
    clipPreviewIndexCount_ = 0;
    clipPreviewLineVertexCount_ = 0;
}

void MetalRenderBackend::destroyLineBuffer(MetalBufferResource& buffer, int& vertexCount)
{
    buffer.destroy();
    vertexCount = 0;
}

void MetalRenderBackend::destroyOverlayResources()
{
    destroyLineBuffer(overlayVertexBuffer_, overlayVertexCount_);
}

void MetalRenderBackend::destroySliceResources()
{
    destroyLineBuffer(sliceVertexBuffer_, sliceVertexCount_);
}

void MetalRenderBackend::destroySelectionResources()
{
    destroyLineBuffer(selectionVertexBuffer_, selectionVertexCount_);
}

void MetalRenderBackend::destroyAxesResources()
{
    destroyLineBuffer(axesLineVertexBuffer_, axesLineVertexCount_);
    destroyLineBuffer(axesSolidVertexBuffer_, axesSolidVertexCount_);
}

void MetalRenderBackend::destroyDepthResources()
{
    depthTexture_.destroy();
}

void MetalRenderBackend::destroyPickResources()
{
    pickColorTexture_.destroy();
    pickDepthTexture_.destroy();
    pickReadbackBuffer_.destroy();
}

void MetalRenderBackend::destroy()
{
    destroyLayer();
    destroyMeshResources();
    destroyEdgeResources();
    destroyIsoSurfaceResources();
    destroyClipPreviewResources();
    destroyOverlayResources();
    destroySliceResources();
    destroySelectionResources();
    destroyAxesResources();
    frameUniformBuffer_.destroy();
    frameUniformSlotSize_ = 0;
    frameUniformSlotIndex_ = 0;
    backgroundPipelineState_.destroy();
    meshPipelineState_.destroy();
    isoSurfacePipelineState_.destroy();
    linePipelineState_.destroy();
    pickPipelineState_.destroy();
    depthStencilState_.destroy();
    overlayDepthStencilState_.destroy();
    commandQueue_.destroy();
    device_.destroy();
    initialized_ = false;
    info_ = {};
    lastError_.clear();
}
