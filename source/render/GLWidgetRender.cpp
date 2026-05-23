#include "GLWidget.h"
#include "Theme.h"
#include "ColorBarOverlay.h"
#include "OpenGLRenderBackend.h"
#include "ScreenSpacePicking.h"
#include "ViewportGridMetrics.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

void GLWidget::paintOpenGLFrame() {
    auto* glBackend = openGLBackend();
    if (!glBackend) {
        return;
    }

    // 恢复 GL 状态（QPainter 可能在上一帧末尾修改了 viewport/深度/混合等）
    glBackend->beginFrame(width(), height(), devicePixelRatio());

    processDeferredPicks();

    if (needsUpload_) uploadMesh();
    rebuildPartVisibilityIbo();
    if (needsColorUpload_) uploadColors();
    if (edgeVisibilityDirty_) rebuildEdgeIbo();

    renderBackground();

    // 无数据时只绘制坐标轴
    if (mesh_.indices.empty() && mesh_.edgeIndices.empty()) {
        drawAxesIndicator();
        return;
    }

    // ── 计算变换矩阵 ──
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 model = glm::mat4(1.0f);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view * model;
    glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

    // ── 设置着色器和公共 uniform ──
    shader_->bind();

    float nm[9];
    const float* src = glm::value_ptr(normalMat);
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r)
            nm[r * 3 + c] = src[c * 3 + r];
    glm::vec3 eyePos = cam_.eye();
    SceneFrameUniforms frameUniforms;
    frameUniforms.mvp = QMatrix4x4(glm::value_ptr(glm::transpose(mvp)));
    frameUniforms.model = QMatrix4x4(glm::value_ptr(glm::transpose(model)));
    frameUniforms.normalMatrix = QMatrix3x3(nm);
    frameUniforms.lightDir = QVector3D(-0.4f, -0.7f, -0.5f);
    frameUniforms.viewPos = QVector3D(eyePos.x, eyePos.y, eyePos.z);
    frameUniforms.contourMode = useVertexColor_ && colorBarVisible_;
    frameUniforms.scalarMin = scalarMin_;
    frameUniforms.scalarMax = scalarMax_;
    frameUniforms.numBands = numBands_;
    frameUniforms.surfaceAlpha = 1.0f;
    frameUniforms.triPartTextureUnit = 0;
    glBackend->setSceneFrameUniforms(*shader_, frameUniforms);

    // 绑定 triToPart texture buffer 到纹理单元 0
    if (triPartTextureBuffer_)
        glBackend->bindTextureBufferToUnit(*triPartTextureBuffer_, 0);

    // ── 逐步渲染 ──
    renderMainMesh();
    renderMeshEdges();
    renderMeshPoints();
    updateSelectionHighlight();
    renderOverlayMesh();
    renderClipPreview();
    renderSliceLines();
    renderIsoSurface();
    renderSelectionHighlight();

    shader_->release();

    drawAxesIndicator();
    render2DOverlays(mvp);
    updateFpsStats();
}

// ============================================================
// paintGL 渲染子步骤
// ============================================================


void GLWidget::processDeferredPicks() {
    if (pickPointPending_) {
        pickPointPending_ = false;
        pickAtPoint(pendingPickPos_, pendingPickCtrl_);
    }
    if (pickRectPending_) {
        pickRectPending_ = false;
        pickInRect(pendingPickRect_);
    }
    if (deselectPointPending_) {
        deselectPointPending_ = false;
        deselectAtPoint(pendingDeselectPos_);
    }
    if (deselectRectPending_) {
        deselectRectPending_ = false;
        deselectInRect(pendingDeselectRect_);
    }
}


void GLWidget::rebuildPartVisibilityIbo() {
    if (!partVisibilityDirty_ || allTriIndices_.empty()) return;
    partVisibilityDirty_ = false;

    std::vector<unsigned int> filtered;
    filtered.reserve(allTriIndices_.size());
    std::vector<float> filteredTriPart;
    int triCount = static_cast<int>(allTriIndices_.size() / 3);
    for (int t = 0; t < triCount; ++t) {
        const int elementId = t < static_cast<int>(triToElem_.size()) ? triToElem_[t] : -1;
        if (elementId >= 0 && hiddenElementIds_.count(elementId) > 0) {
            continue;
        }
        int part = (t < static_cast<int>(triToPart_.size())) ? triToPart_[t] : -1;
        if (part >= 0) {
            auto it = partVisibility_.find(part);
            if (it != partVisibility_.end() && !it->second)
                continue;
        }
        filtered.push_back(allTriIndices_[t * 3]);
        filtered.push_back(allTriIndices_[t * 3 + 1]);
        filtered.push_back(allTriIndices_[t * 3 + 2]);
        filteredTriPart.push_back(static_cast<float>(part));
    }
    activeIndexCount_ = static_cast<int>(filtered.size());
    auto* glBackend = openGLBackend();
        if (meshResource_) {
            glBackend->uploadMeshIndexBuffer(
                *meshResource_,
                filtered.data(),
                static_cast<int>(filtered.size() * sizeof(unsigned int)));
        }
    if (triPartTextureBuffer_) {
        glBackend->uploadTextureBuffer(
            *triPartTextureBuffer_,
            filteredTriPart.data(),
            static_cast<int>(filteredTriPart.size() * sizeof(float)));
    }
}


void GLWidget::renderBackground() {
    if (!backgroundGeometry_) return;

    auto* glBackend = openGLBackend();
    glBackend->clearDepthBuffer();
    bgShader_->bind();
    const ViewportGridMetrics gridMetrics =
        computeViewportGridMetrics(modelSize_, cam_.distance, viewportGridVisible_);
    bgShader_->setUniformValue("uGridParams",
                               gridMetrics.alpha,
                               gridMetrics.minorStep,
                               gridMetrics.fineAlpha,
                               0.0f);
    ScenePassState passState;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = false;
    passState.restoredDepthTestEnabled = true;
    glBackend->drawArraysPass(*backgroundGeometry_, ScenePrimitive::Triangles, 0, 6, passState);
    bgShader_->release();
}


void GLWidget::renderMainMesh() {
    int count = activeIndexCount_;
    const bool isoActive = isoIndexCount_ > 0;
    if (count <= 0 || isoActive) return;
    if (displayMode_ == ModelDisplayMode::Wireframe ||
        displayMode_ == ModelDisplayMode::Points) {
        return;
    }

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(color_.x, color_.y, color_.z);
    drawUniforms.wireframe = false;
    drawUniforms.useVertexColor = useVertexColor_ || !partColors_.empty();
    ScenePassState passState;
    passState.applyPolygonOffsetFill = true;
    passState.polygonOffsetFillEnabled = true;
    passState.restoredPolygonOffsetFillEnabled = false;
    passState.applyPolygonMode = true;
    passState.polygonMode = ScenePolygonMode::Fill;
    passState.restoredPolygonMode = ScenePolygonMode::Fill;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Elements;
    pass.primitive = ScenePrimitive::Triangles;
    pass.count = count;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *meshResource_);
}


void GLWidget::renderMeshEdges() {
    if (!edgeResource_) return;
    if (displayMode_ == ModelDisplayMode::Solid ||
        displayMode_ == ModelDisplayMode::Points) {
        return;
    }

    auto* glBackend = openGLBackend();

    int count = activeIndexCount_;
    const float wireAlpha = displayMode_ == ModelDisplayMode::Wireframe ? 1.0f : 0.85f;

    if (activeEdgeIndexCount_ > 0) {
        const int edgeVertCount = static_cast<int>(mesh_.edgeVertices.size() / 3);
        const bool useEdgeContour =
            useVertexColor_ &&
            colorBarVisible_ &&
            edgeVertCount > 0 &&
            static_cast<int>(edgeScalars_.size()) == edgeVertCount;
        float lineW = (count == 0) ? 3.0f : 1.25f;
        float alpha = (count == 0) ? 1.0f : wireAlpha;
        SceneDrawUniforms drawUniforms;
        drawUniforms.color = (count == 0)
            ? QVector3D(color_.x, color_.y, color_.z)
            : QVector3D(0.2f, 0.2f, 0.22f);
        drawUniforms.wireframe = true;
        drawUniforms.useVertexColor = useEdgeContour;
        drawUniforms.overrideContourMode = true;
        drawUniforms.contourMode = useEdgeContour;
        drawUniforms.wireAlpha = alpha;
        ScenePassState passState;
        passState.applyLineWidth = true;
        passState.lineWidth = lineW;
        passState.restoredLineWidth = 1.0f;
        if (alpha < 1.0f) {
            passState.applyBlend = true;
            passState.blendEnabled = true;
            passState.restoredBlendEnabled = false;
        }
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Elements;
        pass.primitive = ScenePrimitive::Lines;
        pass.count = activeEdgeIndexCount_;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *edgeResource_);
    } else if (count > 0) {
        SceneDrawUniforms drawUniforms;
        drawUniforms.color = QVector3D(0.2f, 0.2f, 0.22f);
        drawUniforms.wireframe = true;
        drawUniforms.useVertexColor = false;
        drawUniforms.overrideContourMode = true;
        drawUniforms.contourMode = false;
        drawUniforms.wireAlpha = wireAlpha;
        ScenePassState passState;
        passState.applyLineWidth = true;
        passState.lineWidth = 1.0f;
        passState.restoredLineWidth = 1.0f;
        passState.applyPolygonMode = true;
        passState.polygonMode = ScenePolygonMode::Line;
        passState.restoredPolygonMode = ScenePolygonMode::Fill;
        if (wireAlpha < 1.0f) {
            passState.applyBlend = true;
            passState.blendEnabled = true;
            passState.restoredBlendEnabled = false;
        }
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Elements;
        pass.primitive = ScenePrimitive::Triangles;
        pass.count = count;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *meshResource_);
    }
}


void GLWidget::renderMeshPoints()
{
    const int count = activeIndexCount_;
    const bool isoActive = isoIndexCount_ > 0;
    if (displayMode_ != ModelDisplayMode::Points || count <= 0 || isoActive || !meshResource_) {
        return;
    }

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(color_.x, color_.y, color_.z);
    drawUniforms.wireframe = false;
    drawUniforms.useVertexColor = useVertexColor_ || !partColors_.empty();
    ScenePassState passState;
    passState.applyPointSize = true;
    passState.pointSize = 4.0f;
    passState.restoredPointSize = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Elements;
    pass.primitive = ScenePrimitive::Points;
    pass.count = count;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *meshResource_);
}


void GLWidget::updateSelectionHighlight() {
    if (selectionDirty_) {
        std::vector<float> hlVerts;
        int hlMode = 0;

        if (!selection_.selectedElements.empty() && !triToElem_.empty()) {
            partEdgeCacheValid_ = false;
            rebuildSelectionEdges();
            hlMode = 0;
        } else if (!selection_.selectedNodes.empty()) {
            std::unordered_map<int, int> nodeToFirstVertex;
            if (!vertexToNode_.empty()) {
                for (int i = 0; i < static_cast<int>(vertexToNode_.size()); ++i) {
                    int nid = vertexToNode_[i];
                    if (nid >= 0 && nodeToFirstVertex.find(nid) == nodeToFirstVertex.end())
                        nodeToFirstVertex[nid] = i;
                }
            }
            for (int nid : selection_.selectedNodes) {
                int vi = -1;
                if (!nodeToFirstVertex.empty()) {
                    auto it = nodeToFirstVertex.find(nid);
                    if (it != nodeToFirstVertex.end()) vi = it->second;
                } else {
                    vi = nid;
                }
                if (vi >= 0 && vi * 6 + 2 < static_cast<int>(mesh_.vertices.size())) {
                    hlVerts.push_back(mesh_.vertices[vi * 6]);
                    hlVerts.push_back(mesh_.vertices[vi * 6 + 1]);
                    hlVerts.push_back(mesh_.vertices[vi * 6 + 2]);
                }
            }
            selEdgeVertCount_ = static_cast<int>(hlVerts.size() / 3);
            auto* glBackend = openGLBackend();
            glBackend->uploadLineVertices(
                *selectionEdgeResource_,
                hlVerts.data(),
                static_cast<int>(hlVerts.size() * sizeof(float)));
            hlMode = 1;
        }
        selectionDirty_ = false;
        silhouetteDirty_ = false;
        selHlMode_ = hlMode;
    } else if (silhouetteDirty_ && partEdgeCacheValid_ &&
               pickMode_ == PickMode::Part && selection_.hasSelection()) {
        updateSilhouetteFromCache();
        silhouetteDirty_ = false;
    }
}


void GLWidget::renderOverlayMesh() {
    if (!overlayVisible_ || overlayMesh_.edgeVertices.empty() || !overlayResource_) return;

    if (overlayNeedsUpload_) {
        overlayNeedsUpload_ = false;
        auto* glBackend = openGLBackend();
        glBackend->uploadLineVertices(
            *overlayResource_,
            overlayMesh_.edgeVertices.data(),
            static_cast<int>(overlayMesh_.edgeVertices.size() * sizeof(float)));
        overlayVertCount_ = static_cast<int>(overlayMesh_.edgeVertices.size() / 3);
    }
    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(0.5f, 0.5f, 0.5f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 0.3f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyBlend = true;
    passState.blendEnabled = true;
    passState.restoredBlendEnabled = false;
    passState.applyLineWidth = true;
    passState.lineWidth = 1.0f;
    passState.restoredLineWidth = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Arrays;
    pass.primitive = ScenePrimitive::Lines;
    pass.count = overlayVertCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *overlayResource_);
}


void GLWidget::renderClipPreview() {
    if (!clipPreviewVisible_ ||
        clipPreviewIndexCount_ <= 0 ||
        !clipPreviewResource_ ||
        !clipPreviewEdgeResource_) {
        return;
    }

    if (clipPreviewNeedsUpload_) {
        clipPreviewNeedsUpload_ = false;
        auto* glBackend = openGLBackend();
        glBackend->uploadMeshBuffers(
            *clipPreviewResource_,
            clipPreviewMesh_.vertices.data(),
            static_cast<int>(clipPreviewMesh_.vertices.size() * sizeof(float)),
            clipPreviewMesh_.indices.data(),
            static_cast<int>(clipPreviewMesh_.indices.size() * sizeof(unsigned int)));
        glBackend->uploadLineVertices(
            *clipPreviewEdgeResource_,
            clipPreviewMesh_.edgeVertices.data(),
            static_cast<int>(clipPreviewMesh_.edgeVertices.size() * sizeof(float)));
    }

    auto* glBackend = openGLBackend();

    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(0.95f, 0.58f, 0.20f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 0.8f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyBlend = true;
    passState.blendEnabled = true;
    passState.restoredBlendEnabled = false;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = true;
    passState.restoredDepthTestEnabled = true;
    passState.applyDepthWrite = true;
    passState.depthWriteEnabled = false;
    passState.restoredDepthWriteEnabled = true;
    passState.applyLineWidth = true;
    passState.lineWidth = 2.0f;
    passState.restoredLineWidth = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Arrays;
    pass.primitive = ScenePrimitive::Lines;
    pass.count = clipPreviewEdgeVertCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *clipPreviewEdgeResource_);
    glBackend->setSceneSurfaceAlpha(*shader_, 1.0f);
}


void GLWidget::renderSliceLines() {
    if (sliceVertCount_ <= 0 || !sliceResource_) return;

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(1.0f, 0.2f, 0.2f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 1.0f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = false;
    passState.restoredDepthTestEnabled = true;
    passState.applyLineWidth = true;
    passState.lineWidth = 2.0f;
    passState.restoredLineWidth = 1.0f;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Arrays;
    pass.primitive = ScenePrimitive::Lines;
    pass.count = sliceVertCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *sliceResource_);
}


void GLWidget::renderIsoSurface() {
    if (isoIndexCount_ <= 0 || !isoResource_) return;

    if (isoNeedsUpload_) {
        isoNeedsUpload_ = false;
        auto* glBackend = openGLBackend();
        glBackend->uploadMeshBuffers(
            *isoResource_,
            isoMesh_.vertices.data(),
            static_cast<int>(isoMesh_.vertices.size() * sizeof(float)),
            isoMesh_.indices.data(),
            static_cast<int>(isoMesh_.indices.size() * sizeof(unsigned int)));
    }
    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(0.2f, 0.8f, 0.4f);
    drawUniforms.wireframe = false;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 0.75f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    drawUniforms.overrideSurfaceAlpha = true;
    drawUniforms.surfaceAlpha = 0.75f;
    ScenePassState passState;
    passState.applyBlend = true;
    passState.blendEnabled = true;
    passState.restoredBlendEnabled = false;
    passState.applyCullFace = true;
    passState.cullFaceEnabled = false;
    passState.restoredCullFaceEnabled = true;
    OpenGLScenePass pass;
    pass.program = shader_;
    pass.drawKind = SceneDrawKind::Elements;
    pass.primitive = ScenePrimitive::Triangles;
    pass.count = isoIndexCount_;
    pass.uniforms = drawUniforms;
    pass.state = passState;
    glBackend->drawScenePass(pass, *isoResource_);
    glBackend->setSceneSurfaceAlpha(*shader_, 1.0f);
}


void GLWidget::renderSelectionHighlight() {
    if (selEdgeVertCount_ <= 0 || !selection_.hasSelection() ||
        !selectionEdgeResource_) {
        return;
    }

    auto* glBackend = openGLBackend();
    SceneDrawUniforms drawUniforms;
    drawUniforms.color = QVector3D(1.0f, 0.78f, 0.0f);
    drawUniforms.wireframe = true;
    drawUniforms.useVertexColor = false;
    drawUniforms.wireAlpha = 1.0f;
    drawUniforms.overrideContourMode = true;
    drawUniforms.contourMode = false;
    ScenePassState passState;
    passState.applyDepthTest = true;
    passState.depthTestEnabled = false;
    passState.restoredDepthTestEnabled = true;

    if (selHlMode_ == 1) {
        passState.applyPointSize = true;
        passState.pointSize = 8.0f;
        passState.restoredPointSize = 1.0f;
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Arrays;
        pass.primitive = ScenePrimitive::Points;
        pass.count = selEdgeVertCount_;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *selectionEdgeResource_);
    } else {
        passState.applyLineWidth = true;
        passState.lineWidth = 2.5f;
        passState.restoredLineWidth = 1.0f;
        OpenGLScenePass pass;
        pass.program = shader_;
        pass.drawKind = SceneDrawKind::Arrays;
        pass.primitive = ScenePrimitive::Lines;
        pass.count = selEdgeVertCount_;
        pass.uniforms = drawUniforms;
        pass.state = passState;
        glBackend->drawScenePass(pass, *selectionEdgeResource_);
    }
}


void GLWidget::updateFpsStats() {
    frameCount_++;
    qint64 elapsed = fpsTimer_.elapsed();
    if (elapsed >= 500) {
        fps_ = frameCount_ * 1000.0f / elapsed;
        frameTime_ = elapsed / static_cast<float>(frameCount_);
        frameCount_ = 0;
        fpsTimer_.restart();
    }
}




void GLWidget::uploadColors() {
    needsColorUpload_ = false;

    // 云图模式下颜色由片段着色器从标量值生成，不需要更新
    if (useVertexColor_) return;

    if (triToPart_.empty()) return;

    // 上传 triToPart 到 texture buffer（供片段着色器用 gl_PrimitiveID 查表）
    if (triPartDirty_) {
        triPartDirty_ = false;
        std::vector<float> triPartData(triToPart_.size());
        for (size_t i = 0; i < triToPart_.size(); ++i)
            triPartData[i] = static_cast<float>(triToPart_[i]);
        auto* glBackend = openGLBackend();
        if (triPartTextureBuffer_) {
            glBackend->uploadTextureBuffer(
                *triPartTextureBuffer_,
                triPartData.data(),
                static_cast<int>(triPartData.size() * sizeof(float)));
        }
    }
}


void GLWidget::rebuildEdgeIbo() {
    edgeVisibilityDirty_ = false;
    if (allEdgeIndices_.empty()) return;

    std::vector<unsigned int> filtered;
    filtered.reserve(allEdgeIndices_.size());
    int edgeCount = static_cast<int>(allEdgeIndices_.size() / 2);
    for (int e = 0; e < edgeCount; ++e) {
        const int elementId = e < static_cast<int>(mesh_.edgeToElement.size())
            ? mesh_.edgeToElement[e]
            : -1;
        if (elementId >= 0 && hiddenElementIds_.count(elementId) > 0) {
            continue;
        }
        int part = (e < static_cast<int>(edgeToPart_.size())) ? edgeToPart_[e] : -1;
        if (part >= 0) {
            auto it = partVisibility_.find(part);
            if (it != partVisibility_.end() && !it->second)
                continue;   // 该部件不可见，跳过此边
        }
        filtered.push_back(allEdgeIndices_[e * 2]);
        filtered.push_back(allEdgeIndices_[e * 2 + 1]);
    }
    activeEdgeIndexCount_ = static_cast<int>(filtered.size());

    if (edgeResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadEdgeIndexBuffer(
            *edgeResource_,
            filtered.data(),
            static_cast<int>(filtered.size() * sizeof(unsigned int)));
    }
}


void GLWidget::uploadMesh() {
    needsUpload_ = false;
    if (!meshResource_) return;

    auto* glBackend = openGLBackend();

    activeIndexCount_ = static_cast<int>(mesh_.indices.size());
    glBackend->uploadMeshBuffers(*meshResource_,
                                 mesh_.vertices.data(),
                                 static_cast<int>(mesh_.vertices.size() * sizeof(float)),
                                 mesh_.indices.data(),
                                 static_cast<int>(mesh_.indices.size() * sizeof(unsigned int)));

    // ── 颜色缓冲（per-vertex，默认为 color_） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultColors(vertCount * 3);
        for (int v = 0; v < vertCount; ++v) {
            defaultColors[v * 3 + 0] = color_.x;
            defaultColors[v * 3 + 1] = color_.y;
            defaultColors[v * 3 + 2] = color_.z;
        }
        glBackend->uploadMeshColorBuffer(*meshResource_,
                                         defaultColors.data(),
                                         static_cast<int>(defaultColors.size() * sizeof(float)));
    }

    // ── 标量值缓冲（per-vertex，默认全 0） ──
    {
        int vertCount = static_cast<int>(mesh_.vertices.size() / 6);
        std::vector<float> defaultScalars(vertCount, 0.0f);
        glBackend->uploadMeshScalarBuffer(*meshResource_,
                                          defaultScalars.data(),
                                          static_cast<int>(defaultScalars.size() * sizeof(float)));
    }

    // ── 上传边线数据（如果有） ──
    edgeIndexCount_ = static_cast<int>(mesh_.edgeIndices.size());
    activeEdgeIndexCount_ = edgeIndexCount_;
    if (edgeIndexCount_ > 0 && edgeResource_) {
        glBackend->uploadEdgeBuffers(
            *edgeResource_,
            mesh_.edgeVertices.data(),
            static_cast<int>(mesh_.edgeVertices.size() * sizeof(float)),
            mesh_.edgeIndices.data(),
            static_cast<int>(mesh_.edgeIndices.size() * sizeof(unsigned int)));
        const int edgeVertCount = static_cast<int>(mesh_.edgeVertices.size() / 3);
        const std::vector<float> defaultEdgeScalars(edgeVertCount, 0.0f);
        glBackend->uploadEdgeScalarBuffer(
            *edgeResource_,
            defaultEdgeScalars.data(),
            static_cast<int>(defaultEdgeScalars.size() * sizeof(float)));
    }
}


