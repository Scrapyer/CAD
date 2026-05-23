#include "OpenGLRenderBackend.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QObject>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QString>
#include <cstdint>
#include <utility>

namespace {

QString glString(QOpenGLFunctions* funcs, GLenum name)
{
    const auto* value = reinterpret_cast<const char*>(funcs->glGetString(name));
    return value ? QString::fromLatin1(value) : QString();
}

QByteArray loadShaderSource(const QString& resourcePath)
{
    QFile f(resourcePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning("OpenGLRenderBackend: failed to load shader %s", qPrintable(resourcePath));
        return {};
    }
    return f.readAll();
}

GLenum toGLPrimitive(ScenePrimitive primitive)
{
    switch (primitive) {
    case ScenePrimitive::Lines:
        return GL_LINES;
    case ScenePrimitive::Points:
        return GL_POINTS;
    case ScenePrimitive::Triangles:
    default:
        return GL_TRIANGLES;
    }
}

GLenum toGLPolygonMode(ScenePolygonMode mode)
{
    switch (mode) {
    case ScenePolygonMode::Line:
        return GL_LINE;
    case ScenePolygonMode::Fill:
    default:
        return GL_FILL;
    }
}

void applyPassState(const OpenGLRenderBackend& backend, const ScenePassState& state)
{
    if (state.applyBlend) {
        if (state.blendEnabled) backend.setAlphaBlend();
        else backend.setBlendEnabled(false);
    }
    if (state.applyDepthTest) backend.setDepthTestEnabled(state.depthTestEnabled);
    if (state.applyDepthWrite) backend.setDepthWriteEnabled(state.depthWriteEnabled);
    if (state.applyCullFace) backend.setCullFaceEnabled(state.cullFaceEnabled);
    if (state.applyLineWidth) backend.setLineWidth(state.lineWidth);
    if (state.applyPointSize) backend.setPointSize(state.pointSize);
    if (state.applyPolygonMode) backend.setPolygonMode(state.polygonMode);
    if (state.applyPolygonOffsetFill) {
        backend.setPolygonOffsetFillEnabled(state.polygonOffsetFillEnabled,
                                            state.polygonOffsetFactor,
                                            state.polygonOffsetUnits);
    }
}

void restorePassState(const OpenGLRenderBackend& backend, const ScenePassState& state)
{
    if (state.applyPolygonOffsetFill) {
        backend.setPolygonOffsetFillEnabled(state.restoredPolygonOffsetFillEnabled);
    }
    if (state.applyPolygonMode) backend.setPolygonMode(state.restoredPolygonMode);
    if (state.applyPointSize) backend.setPointSize(state.restoredPointSize);
    if (state.applyLineWidth) backend.setLineWidth(state.restoredLineWidth);
    if (state.applyCullFace) backend.setCullFaceEnabled(state.restoredCullFaceEnabled);
    if (state.applyDepthWrite) backend.setDepthWriteEnabled(state.restoredDepthWriteEnabled);
    if (state.applyDepthTest) backend.setDepthTestEnabled(state.restoredDepthTestEnabled);
    if (state.applyBlend) backend.setBlendEnabled(state.restoredBlendEnabled);
}

void deleteTextureBufferObjects(GLuint& buffer, GLuint& texture)
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (ctx) {
        QOpenGLFunctions* funcs = ctx->functions();
        if (buffer != 0) funcs->glDeleteBuffers(1, &buffer);
        if (texture != 0) funcs->glDeleteTextures(1, &texture);
    }
    buffer = 0;
    texture = 0;
}

} // namespace

OpenGLPositionColorGeometry::OpenGLPositionColorGeometry()
    : vao(std::make_unique<QOpenGLVertexArrayObject>()),
      vbo(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer))
{
}

OpenGLPositionColorGeometry::~OpenGLPositionColorGeometry() = default;

OpenGLPositionColorGeometry::OpenGLPositionColorGeometry(OpenGLPositionColorGeometry&&) noexcept = default;

OpenGLPositionColorGeometry& OpenGLPositionColorGeometry::operator=(
    OpenGLPositionColorGeometry&&) noexcept = default;

OpenGLFramebuffer::OpenGLFramebuffer() = default;

OpenGLFramebuffer::~OpenGLFramebuffer() = default;

OpenGLFramebuffer::OpenGLFramebuffer(OpenGLFramebuffer&&) noexcept = default;

OpenGLFramebuffer& OpenGLFramebuffer::operator=(OpenGLFramebuffer&&) noexcept = default;

bool OpenGLFramebuffer::isValid() const
{
    return fbo && fbo->isValid() && fbo->handle() != 0;
}

OpenGLVertexArrayResource::OpenGLVertexArrayResource()
    : vao(std::make_unique<QOpenGLVertexArrayObject>())
{
}

OpenGLVertexArrayResource::~OpenGLVertexArrayResource() = default;

OpenGLVertexArrayResource::OpenGLVertexArrayResource(
    OpenGLVertexArrayResource&&) noexcept = default;

OpenGLVertexArrayResource& OpenGLVertexArrayResource::operator=(
    OpenGLVertexArrayResource&&) noexcept = default;

bool OpenGLVertexArrayResource::isValid() const
{
    return vao && vao->isCreated() && vao->objectId() != 0;
}

GLuint OpenGLVertexArrayResource::objectId() const
{
    return isValid() ? vao->objectId() : 0;
}

OpenGLTextureBufferResource::OpenGLTextureBufferResource() = default;

OpenGLTextureBufferResource::~OpenGLTextureBufferResource()
{
    deleteTextureBufferObjects(buffer, texture);
}

OpenGLTextureBufferResource::OpenGLTextureBufferResource(
    OpenGLTextureBufferResource&& other) noexcept
    : buffer(std::exchange(other.buffer, 0)),
      texture(std::exchange(other.texture, 0))
{
}

OpenGLTextureBufferResource& OpenGLTextureBufferResource::operator=(
    OpenGLTextureBufferResource&& other) noexcept
{
    if (this != &other) {
        deleteTextureBufferObjects(buffer, texture);
        buffer = std::exchange(other.buffer, 0);
        texture = std::exchange(other.texture, 0);
    }
    return *this;
}

bool OpenGLTextureBufferResource::isValid() const
{
    return buffer != 0 && texture != 0;
}

OpenGLMeshResource::OpenGLMeshResource()
    : vao(std::make_unique<QOpenGLVertexArrayObject>()),
      vertexBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer)),
      indexBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer)),
      colorBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer)),
      scalarBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer))
{
}

OpenGLMeshResource::~OpenGLMeshResource() = default;

OpenGLMeshResource::OpenGLMeshResource(OpenGLMeshResource&&) noexcept = default;

OpenGLMeshResource& OpenGLMeshResource::operator=(OpenGLMeshResource&&) noexcept = default;

bool OpenGLMeshResource::isValid() const
{
    return vao && vao->isCreated() && vertexBuffer && vertexBuffer->isCreated();
}

GLuint OpenGLMeshResource::vertexBufferId() const
{
    return (vertexBuffer && vertexBuffer->isCreated()) ? vertexBuffer->bufferId() : 0;
}

OpenGLEdgeResource::OpenGLEdgeResource()
    : vao(std::make_unique<QOpenGLVertexArrayObject>()),
      vertexBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer)),
      indexBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::IndexBuffer)),
      scalarBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer))
{
}

OpenGLEdgeResource::~OpenGLEdgeResource() = default;

OpenGLEdgeResource::OpenGLEdgeResource(OpenGLEdgeResource&&) noexcept = default;

OpenGLEdgeResource& OpenGLEdgeResource::operator=(OpenGLEdgeResource&&) noexcept = default;

bool OpenGLEdgeResource::isValid() const
{
    return vao && vao->isCreated() && vertexBuffer && vertexBuffer->isCreated();
}

OpenGLLineResource::OpenGLLineResource()
    : vao(std::make_unique<QOpenGLVertexArrayObject>()),
      vertexBuffer(std::make_unique<QOpenGLBuffer>(QOpenGLBuffer::VertexBuffer))
{
}

OpenGLLineResource::~OpenGLLineResource() = default;

OpenGLLineResource::OpenGLLineResource(OpenGLLineResource&&) noexcept = default;

OpenGLLineResource& OpenGLLineResource::operator=(OpenGLLineResource&&) noexcept = default;

bool OpenGLLineResource::isValid() const
{
    return vao && vao->isCreated() && vertexBuffer && vertexBuffer->isCreated();
}

void OpenGLRenderBackend::initialize()
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        info_ = {};
        return;
    }

    QOpenGLFunctions* funcs = ctx->functions();
    funcs->initializeOpenGLFunctions();

    info_.renderer = glString(funcs, GL_RENDERER);
    info_.version = glString(funcs, GL_VERSION);
    info_.shadingLanguageVersion = glString(funcs, GL_SHADING_LANGUAGE_VERSION);
    info_.vendor = glString(funcs, GL_VENDOR);
}

QOpenGLShaderProgram* OpenGLRenderBackend::createShaderProgram(
    QObject* owner,
    const QString& vertexResource,
    const QString& fragmentResource) const
{
    auto* program = new QOpenGLShaderProgram(owner);
    program->addShaderFromSourceCode(QOpenGLShader::Vertex, loadShaderSource(vertexResource));
    program->addShaderFromSourceCode(QOpenGLShader::Fragment, loadShaderSource(fragmentResource));
    program->link();
    return program;
}

void OpenGLRenderBackend::initializeDefaultState() const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    QOpenGLFunctions* funcs = ctx->functions();
    funcs->glEnable(GL_DEPTH_TEST);
    funcs->glEnable(GL_MULTISAMPLE);
    funcs->glEnable(GL_LINE_SMOOTH);
    funcs->glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
}

void OpenGLRenderBackend::setSceneFrameUniforms(
    QOpenGLShaderProgram& program,
    const SceneFrameUniforms& uniforms) const
{
    program.setUniformValue("uMVP", uniforms.mvp);
    program.setUniformValue("uModel", uniforms.model);
    program.setUniformValue("uNormalMat", uniforms.normalMatrix);
    program.setUniformValue("uLightDir", uniforms.lightDir);
    program.setUniformValue("uViewPos", uniforms.viewPos);
    program.setUniformValue("uContourMode", uniforms.contourMode);
    program.setUniformValue("uScalarMin", uniforms.scalarMin);
    program.setUniformValue("uScalarMax", uniforms.scalarMax);
    program.setUniformValue("uNumBands", uniforms.numBands);
    program.setUniformValue("uSurfaceAlpha", uniforms.surfaceAlpha);
    program.setUniformValue("uTriPartMap", uniforms.triPartTextureUnit);
}

void OpenGLRenderBackend::setSceneDrawUniforms(
    QOpenGLShaderProgram& program,
    const SceneDrawUniforms& uniforms) const
{
    program.setUniformValue("uColor", uniforms.color);
    program.setUniformValue("uWireframe", uniforms.wireframe);
    program.setUniformValue("uUseVertexColor", uniforms.useVertexColor);
    program.setUniformValue("uWireAlpha", uniforms.wireAlpha);
    if (uniforms.overrideContourMode) {
        program.setUniformValue("uContourMode", uniforms.contourMode);
    }
    if (uniforms.overrideSurfaceAlpha) {
        program.setUniformValue("uSurfaceAlpha", uniforms.surfaceAlpha);
    }
}

void OpenGLRenderBackend::setSceneContourMode(QOpenGLShaderProgram& program, bool enabled) const
{
    program.setUniformValue("uContourMode", enabled);
}

void OpenGLRenderBackend::setSceneSurfaceAlpha(QOpenGLShaderProgram& program, float alpha) const
{
    program.setUniformValue("uSurfaceAlpha", alpha);
}

void OpenGLRenderBackend::setMvpUniform(QOpenGLShaderProgram& program, const QMatrix4x4& mvp) const
{
    program.setUniformValue("uMVP", mvp);
}

void OpenGLRenderBackend::drawScenePass(const OpenGLScenePass& pass) const
{
    if (!pass.program || !pass.vao || pass.count <= 0) return;

    setSceneDrawUniforms(*pass.program, pass.uniforms);
    applyPassState(*this, pass.state);
    if (pass.drawKind == SceneDrawKind::Arrays) {
        drawArrays(*pass.vao, toGLPrimitive(pass.primitive), pass.first, pass.count);
    } else {
        drawElements(*pass.vao, toGLPrimitive(pass.primitive), pass.count);
    }
    restorePassState(*this, pass.state);
}

void OpenGLRenderBackend::drawScenePass(OpenGLScenePass pass, OpenGLMeshResource& mesh) const
{
    pass.vao = vertexArray(mesh);
    drawScenePass(pass);
}

void OpenGLRenderBackend::drawScenePass(OpenGLScenePass pass, OpenGLEdgeResource& edge) const
{
    pass.vao = vertexArray(edge);
    drawScenePass(pass);
}

void OpenGLRenderBackend::drawScenePass(OpenGLScenePass pass, OpenGLLineResource& line) const
{
    pass.vao = vertexArray(line);
    drawScenePass(pass);
}

void OpenGLRenderBackend::drawArraysPass(QOpenGLVertexArrayObject& vao,
                                         ScenePrimitive primitive,
                                         int first,
                                         int count,
                                         const ScenePassState& state) const
{
    applyPassState(*this, state);
    drawArrays(vao, toGLPrimitive(primitive), first, count);
    restorePassState(*this, state);
}

void OpenGLRenderBackend::drawArraysPass(const OpenGLPositionColorGeometry& geometry,
                                         ScenePrimitive primitive,
                                         int first,
                                         int count,
                                         const ScenePassState& state) const
{
    if (!geometry.vao || count <= 0) return;
    drawArraysPass(*geometry.vao, primitive, first, count, state);
}

void OpenGLRenderBackend::drawElementsPass(QOpenGLVertexArrayObject& vao,
                                           ScenePrimitive primitive,
                                           int count,
                                           const ScenePassState& state) const
{
    applyPassState(*this, state);
    drawElements(vao, toGLPrimitive(primitive), count);
    restorePassState(*this, state);
}

void OpenGLRenderBackend::drawSceneArraysPass(
    QOpenGLShaderProgram& program,
    QOpenGLVertexArrayObject& vao,
    ScenePrimitive primitive,
    int first,
    int count,
    const SceneDrawUniforms& uniforms,
    const ScenePassState& state) const
{
    setSceneDrawUniforms(program, uniforms);
    applyPassState(*this, state);
    drawArrays(vao, toGLPrimitive(primitive), first, count);
    restorePassState(*this, state);
}

void OpenGLRenderBackend::drawSceneElementsPass(
    QOpenGLShaderProgram& program,
    QOpenGLVertexArrayObject& vao,
    ScenePrimitive primitive,
    int count,
    const SceneDrawUniforms& uniforms,
    const ScenePassState& state) const
{
    setSceneDrawUniforms(program, uniforms);
    applyPassState(*this, state);
    drawElements(vao, toGLPrimitive(primitive), count);
    restorePassState(*this, state);
}

void OpenGLRenderBackend::setViewport(int x, int y, int width, int height) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    ctx->functions()->glViewport(x, y, width, height);
}

void OpenGLRenderBackend::beginFrame(int width, int height, int devicePixelRatio) const
{
    setViewport(0, 0, width * devicePixelRatio, height * devicePixelRatio);
    setDepthTestEnabled(true);
    glDepthFunc(GL_LESS);
    setBlendEnabled(false);
}

void OpenGLRenderBackend::clear(GLbitfield mask) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    ctx->functions()->glClear(mask);
}

void OpenGLRenderBackend::clearDepthBuffer() const
{
    clear(GL_DEPTH_BUFFER_BIT);
}

void OpenGLRenderBackend::setDepthTestEnabled(bool enabled) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    if (enabled) ctx->functions()->glEnable(GL_DEPTH_TEST);
    else ctx->functions()->glDisable(GL_DEPTH_TEST);
}

void OpenGLRenderBackend::setBlendEnabled(bool enabled) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    if (enabled) ctx->functions()->glEnable(GL_BLEND);
    else ctx->functions()->glDisable(GL_BLEND);
}

void OpenGLRenderBackend::setCullFaceEnabled(bool enabled) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    if (enabled) ctx->functions()->glEnable(GL_CULL_FACE);
    else ctx->functions()->glDisable(GL_CULL_FACE);
}

void OpenGLRenderBackend::setDepthWriteEnabled(bool enabled) const
{
    glDepthMask(enabled ? GL_TRUE : GL_FALSE);
}

void OpenGLRenderBackend::setAlphaBlend() const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    QOpenGLFunctions* funcs = ctx->functions();
    funcs->glEnable(GL_BLEND);
    funcs->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void OpenGLRenderBackend::setLineWidth(float width) const
{
    glLineWidth(width);
}

void OpenGLRenderBackend::setPointSize(float size) const
{
    glPointSize(size);
}

void OpenGLRenderBackend::setPolygonMode(ScenePolygonMode mode) const
{
    glPolygonMode(GL_FRONT_AND_BACK, toGLPolygonMode(mode));
}

void OpenGLRenderBackend::setPolygonOffsetFillEnabled(bool enabled, float factor, float units) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    QOpenGLFunctions* funcs = ctx->functions();
    if (enabled) {
        funcs->glEnable(GL_POLYGON_OFFSET_FILL);
        funcs->glPolygonOffset(factor, units);
    } else {
        funcs->glDisable(GL_POLYGON_OFFSET_FILL);
    }
}

void OpenGLRenderBackend::bindTextureBufferToUnit(GLuint texture, int textureUnit) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    QOpenGLFunctions* funcs = ctx->functions();
    funcs->glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + textureUnit));
    funcs->glBindTexture(GL_TEXTURE_BUFFER, texture);
}

void OpenGLRenderBackend::bindTextureBufferToUnit(
    const OpenGLTextureBufferResource& resource,
    int textureUnit) const
{
    if (!resource.isValid()) return;
    bindTextureBufferToUnit(resource.texture, textureUnit);
}

void OpenGLRenderBackend::createVertexArray(QOpenGLVertexArrayObject& vao) const
{
    if (!vao.isCreated()) vao.create();
}

void OpenGLRenderBackend::createVertexArray(OpenGLVertexArrayResource& vao) const
{
    if (vao.vao)
        createVertexArray(*vao.vao);
}

void OpenGLRenderBackend::createMeshResource(OpenGLMeshResource& mesh) const
{
    if (mesh.vao) createVertexArray(*mesh.vao);
    if (mesh.vertexBuffer) createBuffer(*mesh.vertexBuffer);
    if (mesh.indexBuffer) createBuffer(*mesh.indexBuffer);
    if (mesh.colorBuffer) createBuffer(*mesh.colorBuffer);
    if (mesh.scalarBuffer) createBuffer(*mesh.scalarBuffer);
}

void OpenGLRenderBackend::createEdgeResource(OpenGLEdgeResource& edge) const
{
    if (edge.vao) createVertexArray(*edge.vao);
    if (edge.vertexBuffer) createBuffer(*edge.vertexBuffer);
    if (edge.indexBuffer) createBuffer(*edge.indexBuffer);
    if (edge.scalarBuffer) createBuffer(*edge.scalarBuffer);
}

void OpenGLRenderBackend::createLineResource(OpenGLLineResource& line) const
{
    if (line.vao) createVertexArray(*line.vao);
    if (line.vertexBuffer) createBuffer(*line.vertexBuffer);
}

void OpenGLRenderBackend::createBuffer(QOpenGLBuffer& buffer) const
{
    if (!buffer.isCreated()) buffer.create();
}

QOpenGLBuffer* OpenGLRenderBackend::createIndexBuffer() const
{
    auto* buffer = new QOpenGLBuffer(QOpenGLBuffer::IndexBuffer);
    buffer->create();
    return buffer;
}

void OpenGLRenderBackend::createTextureBufferObjects(GLuint& buffer, GLuint& texture) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    QOpenGLFunctions* funcs = ctx->functions();
    funcs->glGenBuffers(1, &buffer);
    funcs->glGenTextures(1, &texture);
}

void OpenGLRenderBackend::createTextureBufferResource(
    OpenGLTextureBufferResource& resource) const
{
    if (resource.isValid()) return;
    createTextureBufferObjects(resource.buffer, resource.texture);
}

void OpenGLRenderBackend::uploadIndexBuffer(QOpenGLVertexArrayObject& vao,
                                            QOpenGLBuffer*& indexBuffer,
                                            const unsigned int* data,
                                            int byteSize) const
{
    if (!indexBuffer) {
        indexBuffer = createIndexBuffer();
    }

    createVertexArray(vao);
    vao.bind();
    indexBuffer->bind();
    indexBuffer->allocate(data, byteSize);
    vao.release();
}

void OpenGLRenderBackend::uploadMeshIndexBuffer(OpenGLMeshResource& mesh,
                                                const unsigned int* data,
                                                int byteSize) const
{
    if (!mesh.vao || !mesh.indexBuffer) return;
    createMeshResource(mesh);
    mesh.vao->bind();
    mesh.indexBuffer->bind();
    mesh.indexBuffer->allocate(data, byteSize);
    mesh.vao->release();
}

void OpenGLRenderBackend::uploadEdgeIndexBuffer(OpenGLEdgeResource& edge,
                                                const unsigned int* data,
                                                int byteSize) const
{
    if (!edge.vao || !edge.indexBuffer) return;
    createEdgeResource(edge);
    edge.vao->bind();
    edge.indexBuffer->bind();
    edge.indexBuffer->allocate(data, byteSize);
    edge.vao->release();
}

void OpenGLRenderBackend::uploadMeshColorBuffer(OpenGLMeshResource& mesh,
                                                const float* data,
                                                int byteSize) const
{
    if (!mesh.vao || !mesh.colorBuffer) return;
    uploadFloatAttributeBuffer(*mesh.vao,
                               *mesh.colorBuffer,
                               2,
                               3,
                               data,
                               byteSize,
                               3 * static_cast<int>(sizeof(float)));
}

void OpenGLRenderBackend::uploadMeshScalarBuffer(OpenGLMeshResource& mesh,
                                                 const float* data,
                                                 int byteSize) const
{
    if (!mesh.vao || !mesh.scalarBuffer) return;
    uploadFloatAttributeBuffer(*mesh.vao,
                               *mesh.scalarBuffer,
                               3,
                               1,
                               data,
                               byteSize,
                               static_cast<int>(sizeof(float)));
}

void OpenGLRenderBackend::uploadEdgeScalarBuffer(OpenGLEdgeResource& edge,
                                                 const float* data,
                                                 int byteSize) const
{
    if (!edge.vao || !edge.scalarBuffer) return;
    uploadFloatAttributeBuffer(*edge.vao,
                               *edge.scalarBuffer,
                               3,
                               1,
                               data,
                               byteSize,
                               static_cast<int>(sizeof(float)));
}

void OpenGLRenderBackend::uploadFloatAttributeBuffer(QOpenGLVertexArrayObject& vao,
                                                     QOpenGLBuffer& buffer,
                                                     int location,
                                                     int components,
                                                     const float* data,
                                                     int byteSize,
                                                     int strideBytes,
                                                     int offsetBytes) const
{
    createVertexArray(vao);
    createBuffer(buffer);
    vao.bind();
    buffer.bind();
    buffer.allocate(data, byteSize);
    bindFloatAttributeBuffer(vao, buffer, location, components, strideBytes, offsetBytes);
}

void OpenGLRenderBackend::bindFloatAttributeBuffer(QOpenGLVertexArrayObject& vao,
                                                   QOpenGLBuffer& buffer,
                                                   int location,
                                                   int components,
                                                   int strideBytes,
                                                   int offsetBytes) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    QOpenGLFunctions* funcs = ctx->functions();
    createVertexArray(vao);
    createBuffer(buffer);
    vao.bind();
    buffer.bind();
    funcs->glVertexAttribPointer(location,
                                 components,
                                 GL_FLOAT,
                                 GL_FALSE,
                                 strideBytes,
                                 reinterpret_cast<void*>(offsetBytes));
    funcs->glEnableVertexAttribArray(location);
    vao.release();
}

void OpenGLRenderBackend::disableVertexAttribute(QOpenGLVertexArrayObject& vao, int location) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    createVertexArray(vao);
    vao.bind();
    ctx->functions()->glDisableVertexAttribArray(location);
    vao.release();
}

void OpenGLRenderBackend::uploadMeshBuffers(QOpenGLVertexArrayObject& vao,
                                            QOpenGLBuffer& vertexBuffer,
                                            QOpenGLBuffer*& indexBuffer,
                                            const float* vertexData,
                                            int vertexByteSize,
                                            const unsigned int* indexData,
                                            int indexByteSize) const
{
    uploadFloatAttributeBuffer(
        vao,
        vertexBuffer,
        0,
        3,
        vertexData,
        vertexByteSize,
        6 * static_cast<int>(sizeof(float)));
    bindFloatAttributeBuffer(
        vao,
        vertexBuffer,
        1,
        3,
        6 * static_cast<int>(sizeof(float)),
        3 * static_cast<int>(sizeof(float)));
    uploadIndexBuffer(vao, indexBuffer, indexData, indexByteSize);
}

void OpenGLRenderBackend::uploadMeshBuffers(OpenGLMeshResource& mesh,
                                            const float* vertexData,
                                            int vertexByteSize,
                                            const unsigned int* indexData,
                                            int indexByteSize) const
{
    if (!mesh.vao || !mesh.vertexBuffer || !mesh.indexBuffer) return;
    uploadFloatAttributeBuffer(
        *mesh.vao,
        *mesh.vertexBuffer,
        0,
        3,
        vertexData,
        vertexByteSize,
        6 * static_cast<int>(sizeof(float)));
    bindFloatAttributeBuffer(
        *mesh.vao,
        *mesh.vertexBuffer,
        1,
        3,
        6 * static_cast<int>(sizeof(float)),
        3 * static_cast<int>(sizeof(float)));
    uploadMeshIndexBuffer(mesh, indexData, indexByteSize);
}

void OpenGLRenderBackend::uploadEdgeBuffers(OpenGLEdgeResource& edge,
                                            const float* vertexData,
                                            int vertexByteSize,
                                            const unsigned int* indexData,
                                            int indexByteSize) const
{
    if (!edge.vao || !edge.vertexBuffer || !edge.indexBuffer) return;
    uploadFloatAttributeBuffer(*edge.vao,
                               *edge.vertexBuffer,
                               0,
                               3,
                               vertexData,
                               vertexByteSize,
                               3 * static_cast<int>(sizeof(float)));
    uploadEdgeIndexBuffer(edge, indexData, indexByteSize);
    disableVertexAttribute(*edge.vao, 1);
}

void OpenGLRenderBackend::uploadLineVertices(OpenGLLineResource& line,
                                             const float* vertexData,
                                             int vertexByteSize) const
{
    if (!line.vao || !line.vertexBuffer) return;
    uploadFloatAttributeBuffer(*line.vao,
                               *line.vertexBuffer,
                               0,
                               3,
                               vertexData,
                               vertexByteSize,
                               3 * static_cast<int>(sizeof(float)));
    disableVertexAttribute(*line.vao, 1);
}

QOpenGLVertexArrayObject* OpenGLRenderBackend::vertexArray(OpenGLMeshResource& mesh) const
{
    return mesh.vao.get();
}

const QOpenGLVertexArrayObject* OpenGLRenderBackend::vertexArray(
    const OpenGLMeshResource& mesh) const
{
    return mesh.vao.get();
}

QOpenGLVertexArrayObject* OpenGLRenderBackend::vertexArray(OpenGLEdgeResource& edge) const
{
    return edge.vao.get();
}

const QOpenGLVertexArrayObject* OpenGLRenderBackend::vertexArray(
    const OpenGLEdgeResource& edge) const
{
    return edge.vao.get();
}

QOpenGLVertexArrayObject* OpenGLRenderBackend::vertexArray(OpenGLLineResource& line) const
{
    return line.vao.get();
}

const QOpenGLVertexArrayObject* OpenGLRenderBackend::vertexArray(
    const OpenGLLineResource& line) const
{
    return line.vao.get();
}

void OpenGLRenderBackend::uploadTextureBuffer(GLuint buffer,
                                              GLuint texture,
                                              const float* data,
                                              int byteSize) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    QOpenGLFunctions* funcs = ctx->functions();
    funcs->glBindBuffer(GL_TEXTURE_BUFFER, buffer);
    funcs->glBufferData(GL_TEXTURE_BUFFER, byteSize, data, GL_STATIC_DRAW);
    funcs->glBindTexture(GL_TEXTURE_BUFFER, texture);
    auto glTexBufferFn = reinterpret_cast<void(*)(GLenum, GLenum, GLuint)>(
        ctx->getProcAddress("glTexBuffer"));
    if (glTexBufferFn) glTexBufferFn(GL_TEXTURE_BUFFER, GL_R32F, buffer);
    funcs->glBindBuffer(GL_TEXTURE_BUFFER, 0);
}

void OpenGLRenderBackend::uploadTextureBuffer(OpenGLTextureBufferResource& resource,
                                              const float* data,
                                              int byteSize) const
{
    if (!resource.isValid())
        createTextureBufferResource(resource);
    uploadTextureBuffer(resource.buffer, resource.texture, data, byteSize);
}

void OpenGLRenderBackend::uploadPositionColorBuffer(QOpenGLVertexArrayObject& vao,
                                                    QOpenGLBuffer& vbo,
                                                    const float* data,
                                                    int byteSize,
                                                    int positionComponents) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    QOpenGLFunctions* funcs = ctx->functions();
    const int stride = (positionComponents + 3) * static_cast<int>(sizeof(float));

    createVertexArray(vao);
    createBuffer(vbo);
    vao.bind();
    vbo.bind();
    vbo.allocate(data, byteSize);
    funcs->glVertexAttribPointer(0, positionComponents, GL_FLOAT, GL_FALSE, stride, nullptr);
    funcs->glEnableVertexAttribArray(0);
    funcs->glVertexAttribPointer(
        1,
        3,
        GL_FLOAT,
        GL_FALSE,
        stride,
        reinterpret_cast<void*>(positionComponents * sizeof(float)));
    funcs->glEnableVertexAttribArray(1);
    vao.release();
}

void OpenGLRenderBackend::uploadPositionColorGeometry(OpenGLPositionColorGeometry& geometry,
                                                      const float* data,
                                                      int byteSize,
                                                      int positionComponents) const
{
    if (!geometry.vao || !geometry.vbo) return;
    uploadPositionColorBuffer(*geometry.vao, *geometry.vbo, data, byteSize, positionComponents);
    geometry.vertexCount = byteSize / ((positionComponents + 3) * static_cast<int>(sizeof(float)));
    geometry.positionComponents = positionComponents;
}

void OpenGLRenderBackend::updatePositionColorGeometry(OpenGLPositionColorGeometry& geometry,
                                                      const float* data,
                                                      int byteSize) const
{
    if (!geometry.vbo || !geometry.vbo->isCreated()) return;
    geometry.vbo->bind();
    geometry.vbo->write(0, data, byteSize);
    geometry.vbo->release();
    geometry.vertexCount = byteSize / ((geometry.positionComponents + 3) *
                                       static_cast<int>(sizeof(float)));
}

void OpenGLRenderBackend::drawArrays(QOpenGLVertexArrayObject& vao,
                                      GLenum mode,
                                      int first,
                                      int count) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    vao.bind();
    ctx->functions()->glDrawArrays(mode, first, count);
    vao.release();
}

void OpenGLRenderBackend::drawElements(QOpenGLVertexArrayObject& vao, GLenum mode, int count) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;

    vao.bind();
    ctx->functions()->glDrawElements(mode, count, GL_UNSIGNED_INT, nullptr);
    vao.release();
}

void OpenGLRenderBackend::resizeFramebuffer(OpenGLFramebuffer& framebuffer,
                                            int width,
                                            int height) const
{
    if (width <= 0 || height <= 0) {
        framebuffer.fbo.reset();
        framebuffer.width = 0;
        framebuffer.height = 0;
        return;
    }

    if (framebuffer.fbo && framebuffer.width == width && framebuffer.height == height)
        return;

    framebuffer.fbo = std::make_unique<QOpenGLFramebufferObject>(
        width,
        height,
        QOpenGLFramebufferObject::Depth);
    framebuffer.width = width;
    framebuffer.height = height;
}

bool OpenGLRenderBackend::renderPickBuffer(const OpenGLFramebuffer& framebuffer,
                                           const OpenGLVertexArrayResource& vertexArray,
                                           const OpenGLMeshResource& mesh,
                                           int viewportWidth,
                                           int viewportHeight,
                                           GLuint program,
                                           const unsigned int* indices,
                                           int indexCount,
                                           const float* mvpData,
                                           const std::vector<PickDrawItem>& drawItems) const
{
    if (!vertexArray.isValid() || !mesh.isValid()) return false;
    return renderPickBuffer(framebuffer,
                            viewportWidth,
                            viewportHeight,
                            program,
                            vertexArray.objectId(),
                            mesh.vertexBufferId(),
                            indices,
                            indexCount,
                            mvpData,
                            drawItems);
}

bool OpenGLRenderBackend::renderPickBuffer(const OpenGLFramebuffer& framebuffer,
                                           int viewportWidth,
                                           int viewportHeight,
                                           GLuint program,
                                           GLuint vertexArray,
                                           GLuint vertexBuffer,
                                           const unsigned int* indices,
                                           int indexCount,
                                           const float* mvpData,
                                           const std::vector<PickDrawItem>& drawItems) const
{
    if (!framebuffer.isValid()) return false;
    return renderPickBuffer(framebuffer.fbo->handle(),
                            viewportWidth,
                            viewportHeight,
                            program,
                            vertexArray,
                            vertexBuffer,
                            indices,
                            indexCount,
                            mvpData,
                            drawItems);
}

bool OpenGLRenderBackend::renderPickBuffer(GLuint framebuffer,
                                           int viewportWidth,
                                           int viewportHeight,
                                           GLuint program,
                                           GLuint vertexArray,
                                           GLuint vertexBuffer,
                                           const unsigned int* indices,
                                           int indexCount,
                                           const float* mvpData,
                                           const std::vector<PickDrawItem>& drawItems) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx || framebuffer == 0 || program == 0 || vertexArray == 0 ||
        vertexBuffer == 0 || !indices || indexCount <= 0 || !mvpData) {
        return false;
    }

    auto glBindVAO = reinterpret_cast<void(APIENTRY*)(GLuint)>(
        ctx->getProcAddress("glBindVertexArray"));
    auto glUseProgram = reinterpret_cast<void(APIENTRY*)(GLuint)>(
        ctx->getProcAddress("glUseProgram"));
    auto glGetUniformLocation = reinterpret_cast<GLint(APIENTRY*)(GLuint, const char*)>(
        ctx->getProcAddress("glGetUniformLocation"));
    auto glUniformMatrix4fv = reinterpret_cast<void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*)>(
        ctx->getProcAddress("glUniformMatrix4fv"));
    auto glUniform3f = reinterpret_cast<void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat)>(
        ctx->getProcAddress("glUniform3f"));
    auto glGenBuffers = reinterpret_cast<void(APIENTRY*)(GLsizei, GLuint*)>(
        ctx->getProcAddress("glGenBuffers"));
    auto glDeleteBuffers = reinterpret_cast<void(APIENTRY*)(GLsizei, const GLuint*)>(
        ctx->getProcAddress("glDeleteBuffers"));

    if (!glBindVAO || !glUseProgram || !glGetUniformLocation ||
        !glUniformMatrix4fv || !glUniform3f || !glGenBuffers || !glDeleteBuffers) {
        return false;
    }

    QOpenGLFunctions* funcs = ctx->functions();

    GLint prevFbo = 0;
    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevElementArrayBuffer = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLfloat prevClearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    GLboolean prevDepthTest = GL_FALSE;
    GLboolean prevBlend = GL_FALSE;
    funcs->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    funcs->glGetIntegerv(GL_VIEWPORT, prevViewport);
    funcs->glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
    funcs->glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    funcs->glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    funcs->glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    funcs->glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prevElementArrayBuffer);
    funcs->glGetBooleanv(GL_DEPTH_TEST, &prevDepthTest);
    funcs->glGetBooleanv(GL_BLEND, &prevBlend);

    funcs->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    funcs->glViewport(0, 0, viewportWidth, viewportHeight);
    funcs->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    funcs->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    funcs->glEnable(GL_DEPTH_TEST);
    funcs->glDisable(GL_BLEND);

    glUseProgram(program);

    const GLint mvpLoc = glGetUniformLocation(program, "uMVP");
    const GLint pickColorLoc = glGetUniformLocation(program, "uPickColor");
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvpData);

    glBindVAO(vertexArray);
    funcs->glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    funcs->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    funcs->glEnableVertexAttribArray(0);

    GLuint rawIndexBuffer = 0;
    glGenBuffers(1, &rawIndexBuffer);
    funcs->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, rawIndexBuffer);
    funcs->glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                        static_cast<GLsizeiptr>(indexCount * sizeof(unsigned int)),
                        indices,
                        GL_STATIC_DRAW);

    for (const PickDrawItem& item : drawItems) {
        if (item.indexCount <= 0) continue;
        glUniform3f(pickColorLoc, item.color[0], item.color[1], item.color[2]);
        funcs->glDrawElements(GL_TRIANGLES,
                              item.indexCount,
                              GL_UNSIGNED_INT,
                              reinterpret_cast<void*>(
                                  static_cast<intptr_t>(item.startIndex * sizeof(unsigned int))));
    }

    funcs->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDeleteBuffers(1, &rawIndexBuffer);

    funcs->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    funcs->glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    funcs->glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
    glBindVAO(static_cast<GLuint>(prevVao));
    glUseProgram(static_cast<GLuint>(prevProgram));
    funcs->glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    funcs->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prevElementArrayBuffer));
    if (prevDepthTest) funcs->glEnable(GL_DEPTH_TEST);
    else funcs->glDisable(GL_DEPTH_TEST);
    if (prevBlend) funcs->glEnable(GL_BLEND);
    else funcs->glDisable(GL_BLEND);

    return true;
}

bool OpenGLRenderBackend::readFramebufferPixel(const OpenGLFramebuffer& framebuffer,
                                               int x,
                                               int y,
                                               unsigned char pixel[4]) const
{
    if (!framebuffer.isValid()) return false;
    return readFramebufferPixel(framebuffer.fbo->handle(), x, y, pixel);
}

bool OpenGLRenderBackend::readFramebufferPixel(GLuint framebuffer,
                                               int x,
                                               int y,
                                               unsigned char pixel[4]) const
{
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx || framebuffer == 0 || !pixel) return false;

    QOpenGLFunctions* funcs = ctx->functions();
    GLint prevFbo = 0;
    funcs->glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    funcs->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    funcs->glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    funcs->glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
    return true;
}
