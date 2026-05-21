#pragma once

#include "RenderBackend.h"

#include <QMatrix4x4>
#include <QtGui/qopengl.h>
#include <memory>
#include <vector>

class QObject;
class QOpenGLBuffer;
class QOpenGLFramebufferObject;
class QOpenGLShaderProgram;
class QOpenGLVertexArrayObject;
class QString;

struct OpenGLPositionColorGeometry {
    OpenGLPositionColorGeometry();
    ~OpenGLPositionColorGeometry();

    OpenGLPositionColorGeometry(const OpenGLPositionColorGeometry&) = delete;
    OpenGLPositionColorGeometry& operator=(const OpenGLPositionColorGeometry&) = delete;
    OpenGLPositionColorGeometry(OpenGLPositionColorGeometry&&) noexcept;
    OpenGLPositionColorGeometry& operator=(OpenGLPositionColorGeometry&&) noexcept;

    std::unique_ptr<QOpenGLVertexArrayObject> vao;
    std::unique_ptr<QOpenGLBuffer> vbo;
    int vertexCount = 0;
    int positionComponents = 3;
};

struct OpenGLFramebuffer {
    OpenGLFramebuffer();
    ~OpenGLFramebuffer();

    OpenGLFramebuffer(const OpenGLFramebuffer&) = delete;
    OpenGLFramebuffer& operator=(const OpenGLFramebuffer&) = delete;
    OpenGLFramebuffer(OpenGLFramebuffer&&) noexcept;
    OpenGLFramebuffer& operator=(OpenGLFramebuffer&&) noexcept;

    bool isValid() const;

    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    int width = 0;
    int height = 0;
};

struct OpenGLVertexArrayResource {
    OpenGLVertexArrayResource();
    ~OpenGLVertexArrayResource();

    OpenGLVertexArrayResource(const OpenGLVertexArrayResource&) = delete;
    OpenGLVertexArrayResource& operator=(const OpenGLVertexArrayResource&) = delete;
    OpenGLVertexArrayResource(OpenGLVertexArrayResource&&) noexcept;
    OpenGLVertexArrayResource& operator=(OpenGLVertexArrayResource&&) noexcept;

    bool isValid() const;
    GLuint objectId() const;

    std::unique_ptr<QOpenGLVertexArrayObject> vao;
};

struct OpenGLTextureBufferResource {
    OpenGLTextureBufferResource();
    ~OpenGLTextureBufferResource();

    OpenGLTextureBufferResource(const OpenGLTextureBufferResource&) = delete;
    OpenGLTextureBufferResource& operator=(const OpenGLTextureBufferResource&) = delete;
    OpenGLTextureBufferResource(OpenGLTextureBufferResource&& other) noexcept;
    OpenGLTextureBufferResource& operator=(OpenGLTextureBufferResource&& other) noexcept;

    bool isValid() const;

    GLuint buffer = 0;
    GLuint texture = 0;
};

struct OpenGLMeshResource {
    OpenGLMeshResource();
    ~OpenGLMeshResource();

    OpenGLMeshResource(const OpenGLMeshResource&) = delete;
    OpenGLMeshResource& operator=(const OpenGLMeshResource&) = delete;
    OpenGLMeshResource(OpenGLMeshResource&&) noexcept;
    OpenGLMeshResource& operator=(OpenGLMeshResource&&) noexcept;

    bool isValid() const;
    GLuint vertexBufferId() const;

    std::unique_ptr<QOpenGLVertexArrayObject> vao;
    std::unique_ptr<QOpenGLBuffer> vertexBuffer;
    std::unique_ptr<QOpenGLBuffer> indexBuffer;
    std::unique_ptr<QOpenGLBuffer> colorBuffer;
    std::unique_ptr<QOpenGLBuffer> scalarBuffer;
};

struct OpenGLEdgeResource {
    OpenGLEdgeResource();
    ~OpenGLEdgeResource();

    OpenGLEdgeResource(const OpenGLEdgeResource&) = delete;
    OpenGLEdgeResource& operator=(const OpenGLEdgeResource&) = delete;
    OpenGLEdgeResource(OpenGLEdgeResource&&) noexcept;
    OpenGLEdgeResource& operator=(OpenGLEdgeResource&&) noexcept;

    bool isValid() const;

    std::unique_ptr<QOpenGLVertexArrayObject> vao;
    std::unique_ptr<QOpenGLBuffer> vertexBuffer;
    std::unique_ptr<QOpenGLBuffer> indexBuffer;
};

struct OpenGLLineResource {
    OpenGLLineResource();
    ~OpenGLLineResource();

    OpenGLLineResource(const OpenGLLineResource&) = delete;
    OpenGLLineResource& operator=(const OpenGLLineResource&) = delete;
    OpenGLLineResource(OpenGLLineResource&&) noexcept;
    OpenGLLineResource& operator=(OpenGLLineResource&&) noexcept;

    bool isValid() const;

    std::unique_ptr<QOpenGLVertexArrayObject> vao;
    std::unique_ptr<QOpenGLBuffer> vertexBuffer;
};

struct OpenGLScenePass {
    QOpenGLShaderProgram* program = nullptr;
    QOpenGLVertexArrayObject* vao = nullptr;
    SceneDrawKind drawKind = SceneDrawKind::Elements;
    ScenePrimitive primitive = ScenePrimitive::Triangles;
    int first = 0;
    int count = 0;
    SceneDrawUniforms uniforms;
    ScenePassState state;
};

/**
 * @brief OpenGL 渲染后端。
 *
 * 当前负责 OpenGL 上下文信息、默认状态、shader program 创建、基础资源创建、
 * GPU 数据上传、uniform 写入、pass 状态应用和绘制命令。
 */
class OpenGLRenderBackend final : public IRenderBackend {
public:
    void initialize() override;
    const RenderBackendInfo& info() const override { return info_; }

    /** @brief 创建并链接一组顶点/片段 shader。 */
    QOpenGLShaderProgram* createShaderProgram(QObject* owner,
                                              const QString& vertexResource,
                                              const QString& fragmentResource) const;

    /** @brief 设置当前 OpenGL 后端的默认全局状态。 */
    void initializeDefaultState() const;

    /** @brief 设置 scene shader 每帧公共 uniform。 */
    void setSceneFrameUniforms(QOpenGLShaderProgram& program,
                               const SceneFrameUniforms& uniforms) const;

    /** @brief 设置 scene shader 单次绘制 uniform。 */
    void setSceneDrawUniforms(QOpenGLShaderProgram& program,
                              const SceneDrawUniforms& uniforms) const;

    /** @brief 单独设置 contour mode，用于恢复或特殊绘制。 */
    void setSceneContourMode(QOpenGLShaderProgram& program, bool enabled) const;

    /** @brief 单独设置 surface alpha，用于恢复或特殊绘制。 */
    void setSceneSurfaceAlpha(QOpenGLShaderProgram& program, float alpha) const;

    /** @brief 设置只包含 uMVP 的简单 shader uniform。 */
    void setMvpUniform(QOpenGLShaderProgram& program, const QMatrix4x4& mvp) const;

    /** @brief 执行聚合后的 scene pass 描述。 */
    void drawScenePass(const OpenGLScenePass& pass) const;

    /** @brief 使用后端托管主网格资源执行 scene pass。 */
    void drawScenePass(OpenGLScenePass pass, OpenGLMeshResource& mesh) const;

    /** @brief 使用后端托管边线资源执行 scene pass。 */
    void drawScenePass(OpenGLScenePass pass, OpenGLEdgeResource& edge) const;

    /** @brief 使用后端托管数组线段/点资源执行 scene pass。 */
    void drawScenePass(OpenGLScenePass pass, OpenGLLineResource& line) const;

    /** @brief 执行一次不写 scene uniform 的数组绘制 pass。 */
    void drawArraysPass(QOpenGLVertexArrayObject& vao,
                        ScenePrimitive primitive,
                        int first,
                        int count,
                        const ScenePassState& state) const;

    /** @brief 执行一次不写 scene uniform 的数组几何绘制 pass。 */
    void drawArraysPass(const OpenGLPositionColorGeometry& geometry,
                        ScenePrimitive primitive,
                        int first,
                        int count,
                        const ScenePassState& state) const;

    /** @brief 执行一次不写 scene uniform 的索引绘制 pass。 */
    void drawElementsPass(QOpenGLVertexArrayObject& vao,
                          ScenePrimitive primitive,
                          int count,
                          const ScenePassState& state) const;

    /** @brief 执行一次 scene shader 数组绘制 pass。 */
    void drawSceneArraysPass(QOpenGLShaderProgram& program,
                             QOpenGLVertexArrayObject& vao,
                             ScenePrimitive primitive,
                             int first,
                             int count,
                             const SceneDrawUniforms& uniforms,
                             const ScenePassState& state) const;

    /** @brief 执行一次 scene shader 索引绘制 pass。 */
    void drawSceneElementsPass(QOpenGLShaderProgram& program,
                               QOpenGLVertexArrayObject& vao,
                               ScenePrimitive primitive,
                               int count,
                               const SceneDrawUniforms& uniforms,
                               const ScenePassState& state) const;

    /** @brief 设置视口。 */
    void setViewport(int x, int y, int width, int height) const;

    /** @brief 开始一帧常规绘制，恢复 viewport、深度和混合状态。 */
    void beginFrame(int width, int height, int devicePixelRatio) const;

    /** @brief 清理指定缓冲。 */
    void clear(GLbitfield mask) const;

    /** @brief 清理深度缓冲。 */
    void clearDepthBuffer() const;

    /** @brief 开关常用 OpenGL 状态。 */
    void setDepthTestEnabled(bool enabled) const;
    void setBlendEnabled(bool enabled) const;
    void setCullFaceEnabled(bool enabled) const;
    void setDepthWriteEnabled(bool enabled) const;
    void setAlphaBlend() const;
    void setLineWidth(float width) const;
    void setPointSize(float size) const;
    void setPolygonMode(ScenePolygonMode mode) const;
    void setPolygonOffsetFillEnabled(bool enabled, float factor = 1.0f, float units = 1.0f) const;

    /** @brief 绑定 texture buffer 到指定纹理单元。 */
    void bindTextureBufferToUnit(GLuint texture, int textureUnit) const;

    /** @brief 绑定后端托管 texture buffer 到指定纹理单元。 */
    void bindTextureBufferToUnit(const OpenGLTextureBufferResource& resource,
                                 int textureUnit) const;

    /** @brief 创建 VAO。 */
    void createVertexArray(QOpenGLVertexArrayObject& vao) const;

    /** @brief 创建后端托管 VAO。 */
    void createVertexArray(OpenGLVertexArrayResource& vao) const;

    /** @brief 创建主网格资源。 */
    void createMeshResource(OpenGLMeshResource& mesh) const;

    /** @brief 创建边线资源。 */
    void createEdgeResource(OpenGLEdgeResource& edge) const;

    /** @brief 创建数组线段/点资源。 */
    void createLineResource(OpenGLLineResource& line) const;

    /** @brief 创建 VBO 或其他已指定类型的 QOpenGLBuffer。 */
    void createBuffer(QOpenGLBuffer& buffer) const;

    /** @brief 创建索引缓冲对象，调用方持有返回指针。 */
    QOpenGLBuffer* createIndexBuffer() const;

    /** @brief 创建原生 texture buffer 对象对。 */
    void createTextureBufferObjects(GLuint& buffer, GLuint& texture) const;

    /** @brief 创建后端托管 texture buffer 对象对。 */
    void createTextureBufferResource(OpenGLTextureBufferResource& resource) const;

    /** @brief 上传索引缓冲；若指针为空则自动创建 IndexBuffer。 */
    void uploadIndexBuffer(QOpenGLVertexArrayObject& vao,
                           QOpenGLBuffer*& indexBuffer,
                           const unsigned int* data,
                           int byteSize) const;

    /** @brief 上传主网格索引缓冲。 */
    void uploadMeshIndexBuffer(OpenGLMeshResource& mesh,
                               const unsigned int* data,
                               int byteSize) const;

    /** @brief 上传边线索引缓冲。 */
    void uploadEdgeIndexBuffer(OpenGLEdgeResource& edge,
                               const unsigned int* data,
                               int byteSize) const;

    /** @brief 上传主网格颜色属性。 */
    void uploadMeshColorBuffer(OpenGLMeshResource& mesh,
                               const float* data,
                               int byteSize) const;

    /** @brief 上传主网格标量属性。 */
    void uploadMeshScalarBuffer(OpenGLMeshResource& mesh,
                                const float* data,
                                int byteSize) const;

    /** @brief 上传单个 float 顶点属性缓冲。 */
    void uploadFloatAttributeBuffer(QOpenGLVertexArrayObject& vao,
                                    QOpenGLBuffer& buffer,
                                    int location,
                                    int components,
                                    const float* data,
                                    int byteSize,
                                    int strideBytes,
                                    int offsetBytes = 0) const;

    /** @brief 在当前 VAO 中绑定已有 float 顶点属性缓冲，不重新上传数据。 */
    void bindFloatAttributeBuffer(QOpenGLVertexArrayObject& vao,
                                  QOpenGLBuffer& buffer,
                                  int location,
                                  int components,
                                  int strideBytes,
                                  int offsetBytes = 0) const;

    /** @brief 禁用当前 VAO 中的顶点属性。 */
    void disableVertexAttribute(QOpenGLVertexArrayObject& vao, int location) const;

    /** @brief 上传 [position(3) + normal(3)] 网格顶点和索引。 */
    void uploadMeshBuffers(QOpenGLVertexArrayObject& vao,
                           QOpenGLBuffer& vertexBuffer,
                           QOpenGLBuffer*& indexBuffer,
                           const float* vertexData,
                           int vertexByteSize,
                           const unsigned int* indexData,
                           int indexByteSize) const;

    /** @brief 上传主网格 [position(3) + normal(3)] 顶点和索引。 */
    void uploadMeshBuffers(OpenGLMeshResource& mesh,
                           const float* vertexData,
                           int vertexByteSize,
                           const unsigned int* indexData,
                           int indexByteSize) const;

    /** @brief 上传边线 position(3) 顶点和索引。 */
    void uploadEdgeBuffers(OpenGLEdgeResource& edge,
                           const float* vertexData,
                           int vertexByteSize,
                           const unsigned int* indexData,
                           int indexByteSize) const;

    /** @brief 上传 position(3) 数组线段/点顶点。 */
    void uploadLineVertices(OpenGLLineResource& line,
                            const float* vertexData,
                            int vertexByteSize) const;

    /** @brief 返回主网格 VAO。 */
    QOpenGLVertexArrayObject* vertexArray(OpenGLMeshResource& mesh) const;
    const QOpenGLVertexArrayObject* vertexArray(const OpenGLMeshResource& mesh) const;

    /** @brief 返回边线 VAO。 */
    QOpenGLVertexArrayObject* vertexArray(OpenGLEdgeResource& edge) const;
    const QOpenGLVertexArrayObject* vertexArray(const OpenGLEdgeResource& edge) const;

    /** @brief 返回数组线段/点 VAO。 */
    QOpenGLVertexArrayObject* vertexArray(OpenGLLineResource& line) const;
    const QOpenGLVertexArrayObject* vertexArray(const OpenGLLineResource& line) const;

    /** @brief 上传 texture buffer 数据并绑定到纹理对象。 */
    void uploadTextureBuffer(GLuint buffer,
                             GLuint texture,
                             const float* data,
                             int byteSize) const;

    /** @brief 上传后端托管 texture buffer 数据并绑定到纹理对象。 */
    void uploadTextureBuffer(OpenGLTextureBufferResource& resource,
                             const float* data,
                             int byteSize) const;

    /**
     * @brief 上传 position + color 交错布局缓冲。
     *
     * layout(location=0) 为 position，layout(location=1) 为 color。
     * positionComponents 可为 2（背景全屏四边形）或 3（坐标轴）。
     */
    void uploadPositionColorBuffer(QOpenGLVertexArrayObject& vao,
                                   QOpenGLBuffer& vbo,
                                   const float* data,
                                   int byteSize,
                                   int positionComponents) const;

    /** @brief 上传后端托管的 position + color 交错布局几何。 */
    void uploadPositionColorGeometry(OpenGLPositionColorGeometry& geometry,
                                     const float* data,
                                     int byteSize,
                                     int positionComponents) const;

    /** @brief 更新后端托管 position + color 几何的数据内容。 */
    void updatePositionColorGeometry(OpenGLPositionColorGeometry& geometry,
                                     const float* data,
                                     int byteSize) const;

    /** @brief 绑定 VAO 并绘制数组。 */
    void drawArrays(QOpenGLVertexArrayObject& vao, GLenum mode, int first, int count) const;

    /** @brief 绑定 VAO 并绘制索引。 */
    void drawElements(QOpenGLVertexArrayObject& vao, GLenum mode, int count) const;

    /** @brief 调整后端托管 framebuffer 尺寸。 */
    void resizeFramebuffer(OpenGLFramebuffer& framebuffer, int width, int height) const;

    /** @brief 渲染拾取缓冲，内部使用原始 GL 调用保存并恢复关键状态。 */
    bool renderPickBuffer(const OpenGLFramebuffer& framebuffer,
                          const OpenGLVertexArrayResource& vertexArray,
                          const OpenGLMeshResource& mesh,
                          int viewportWidth,
                          int viewportHeight,
                          GLuint program,
                          const unsigned int* indices,
                          int indexCount,
                          const float* mvpData,
                          const std::vector<PickDrawItem>& drawItems) const;

    /** @brief 渲染拾取缓冲，内部使用原始 GL 调用保存并恢复关键状态。 */
    bool renderPickBuffer(const OpenGLFramebuffer& framebuffer,
                          int viewportWidth,
                          int viewportHeight,
                          GLuint program,
                          GLuint vertexArray,
                          GLuint vertexBuffer,
                          const unsigned int* indices,
                          int indexCount,
                          const float* mvpData,
                          const std::vector<PickDrawItem>& drawItems) const;

    /** @brief 渲染拾取缓冲，内部使用原始 GL 调用保存并恢复关键状态。 */
    bool renderPickBuffer(GLuint framebuffer,
                          int viewportWidth,
                          int viewportHeight,
                          GLuint program,
                          GLuint vertexArray,
                          GLuint vertexBuffer,
                          const unsigned int* indices,
                          int indexCount,
                          const float* mvpData,
                          const std::vector<PickDrawItem>& drawItems) const;

    /** @brief 从指定 framebuffer 读取一个 RGBA 像素，并恢复原 framebuffer 绑定。 */
    bool readFramebufferPixel(const OpenGLFramebuffer& framebuffer,
                              int x,
                              int y,
                              unsigned char pixel[4]) const;

    /** @brief 从指定 framebuffer 读取一个 RGBA 像素，并恢复原 framebuffer 绑定。 */
    bool readFramebufferPixel(GLuint framebuffer,
                              int x,
                              int y,
                              unsigned char pixel[4]) const;

private:
    RenderBackendInfo info_;
};
