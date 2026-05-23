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

glm::vec3 GLWidget::idToColor(int id) {
    // 将单元 ID+1 编码为 RGB 颜色（0 表示无命中）
    id += 1;
    int r = (id      ) & 0xFF;
    int g = (id >>  8) & 0xFF;
    int b = (id >> 16) & 0xFF;
    return glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
}


int GLWidget::colorToId(unsigned char r, unsigned char g, unsigned char b) {
    if (r == 0 && g == 0 && b == 0) return -1;  // 背景
    int id = r | (g << 8) | (b << 16);
    return id - 1;
}


int GLWidget::edgeElementAtPoint(const QPointF& pos, const glm::mat4& mvp, float thresholdPx) const
{
    return ScreenSpacePicking::edgeElementAtPoint(
        mesh_,
        pos,
        mvp,
        static_cast<float>(width()),
        static_cast<float>(height()),
        true,
        thresholdPx,
        [this](int elementId) { return isElementVisibleForSelection(elementId); });
}


int GLWidget::closestNodeForElement(int elementId, const QPointF& pos, const glm::mat4& mvp) const
{
    return ScreenSpacePicking::closestNodeForElement(mesh_,
                                                     triToElem_,
                                                     vertexToNode_,
                                                     elementId,
                                                     pos,
                                                     mvp,
                                                     static_cast<float>(width()),
                                                     static_cast<float>(height()),
                                                     true);
}


void GLWidget::renderPickBuffer(const glm::mat4& mvp) {
    if (!pickFramebuffer_ || !pickFramebuffer_->isValid() ||
        !pickVertexArray_ || !pickVertexArray_->isValid() ||
        !meshResource_ || !meshResource_->isValid() ||
        triToElem_.empty()) {
        return;
    }

    int dpr = devicePixelRatio();

    // 逐单元绘制，跳过隐藏部件
    std::vector<PickDrawItem> drawItems;
    int triCount = static_cast<int>(triToElem_.size());
    drawItems.reserve(triCount);
    int i = 0;
    while (i < triCount) {
        int elemId = triToElem_[i];
        int start = i;
        while (i < triCount && triToElem_[i] == elemId) ++i;

        if (!isTriangleVisible(start)) {
            continue;
        }

        PickDrawItem item;
        item.startIndex = start * 3;
        item.indexCount = (i - start) * 3;
        glm::vec3 c = idToColor(elemId);
        item.color[0] = c.x;
        item.color[1] = c.y;
        item.color[2] = c.z;
        drawItems.push_back(item);
    }

    auto* glBackend = openGLBackend();
    glBackend->renderPickBuffer(*pickFramebuffer_,
                                *pickVertexArray_,
                                *meshResource_,
                                width() * dpr,
                                height() * dpr,
                                pickShader_->programId(),
                                allTriIndices_.data(),
                                static_cast<int>(allTriIndices_.size()),
                                glm::value_ptr(mvp),
                                drawItems);
}


void GLWidget::pickAtPoint(const QPoint& pos, bool ctrlHeld) {
    // 注意：此函数现在仅在 paintGL() 内调用，GL 上下文已由 Qt 管理，
    // 无需手动 makeCurrent/doneCurrent。

    // 渲染拾取缓冲
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;

    int elemId = -1;
    if (pickFramebuffer_ && pickFramebuffer_->isValid() && !triToElem_.empty()) {
        renderPickBuffer(mvp);

        // 读取点击位置像素（使用原始 GL 调用，避免 Qt FBO 状态追踪污染）
        unsigned char pixel[4] = {0};
        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;  // OpenGL Y 轴翻转
        auto* glBackend = openGLBackend();
        glBackend->readFramebufferPixel(*pickFramebuffer_, px, py, pixel);
        elemId = colorToId(pixel[0], pixel[1], pixel[2]);
    }
    if (elemId < 0) {
        elemId = edgeElementAtPoint(pos, mvp, 8.0f);
    }

    if (pickMode_ == PickMode::Node) {
        // ── 节点拾取：找到点击处最近的顶点 ──
        int closestNode = closestNodeForElement(elemId, pos, mvp);
        if (!ctrlHeld) {
            selection_.clear();
            if (closestNode >= 0) selection_.selectedNodes.insert(closestNode);
        } else {
            if (closestNode >= 0) selection_.toggleNode(closestNode);
        }

    } else if (pickMode_ == PickMode::Part) {
        // ── 部件拾取：命中单元 → elemToPart_ O(1) 查找部件索引 ──
        int hitPart = -1;
        if (elemId >= 0 && !elemToPart_.empty()) {
            auto it = elemToPart_.find(elemId);
            if (it != elemToPart_.end()) hitPart = it->second;
        }
        if (!ctrlHeld) {
            selection_.clear();
            if (hitPart >= 0) selectPart(hitPart);
        } else {
            if (hitPart >= 0) {
                if (isPartFullySelected(hitPart))
                    deselectPart(hitPart);
                else
                    selectPart(hitPart);
            }
        }

    } else {
        // ── 单元拾取 ──
        if (!ctrlHeld) {
            selection_.clear();
            if (elemId >= 0) selection_.selectedElements.insert(elemId);
        } else {
            if (elemId >= 0) selection_.toggleElement(elemId);
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        PickMode emitMode = pickMode_;
        if (!selection_.selectedNodes.empty()) {
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        } else {
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        }
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(emitMode, static_cast<int>(ids.size()), ids);
    }
    // 部件模式：发射选中的部件索引列表，同步模型树
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
            if (isPartFullySelected(pi))
                pickedParts.push_back(pi);
        }
        emit partsPicked(pickedParts);
    }
}


void GLWidget::pickInRect(const QRect& rect) {
    if (triToElem_.empty() && mesh_.elemEdgeToElement.empty()) return;

    // 注意：此函数现在仅在 paintGL() 内调用，GL 上下文已由 Qt 管理。

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 mvp = projection * cam_.viewMatrix();

    // 框选范围转换为 NDC 坐标
    float ndcL = (2.0f * rect.left() / width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    auto pointInside = [&](const glm::vec3& p) {
        glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
        if (clip.w <= 0) return false;
        float sx = clip.x / clip.w;
        float sy = clip.y / clip.w;
        return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
    };

    selection_.clear();

    if (pickMode_ == PickMode::Node) {
        // 节点模式：只遍历可见三角形的顶点，避免隐藏部件被框选
        std::unordered_set<int> addedNodes;
        auto pointInside = [&](const glm::vec3& p) {
            glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
            if (clip.w <= 0) return false;
            float sx = clip.x / clip.w;
            float sy = clip.y / clip.w;
            return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
        };
        const int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec3 p(mesh_.vertices[vi * 6],
                            mesh_.vertices[vi * 6 + 1],
                            mesh_.vertices[vi * 6 + 2]);
                if (pointInside(p)) {
                    int nodeId = (vi < static_cast<int>(vertexToNode_.size())) ? vertexToNode_[vi] : static_cast<int>(vi);
                    if (nodeId >= 0 && addedNodes.insert(nodeId).second)
                        selection_.selectedNodes.insert(nodeId);
                }
            }
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId) ||
                edge >= static_cast<int>(mesh_.elemEdgeNodeIds.size())) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
            if (node0 >= 0 && pointInside(p0) && addedNodes.insert(node0).second)
                selection_.selectedNodes.insert(node0);
            if (node1 >= 0 && pointInside(p1) && addedNodes.insert(node1).second)
                selection_.selectedNodes.insert(node1);
        }
    } else if (pickMode_ == PickMode::Part) {
        // 部件模式：框内三角形 → 收集部件索引 → 选中这些部件所有单元
        std::unordered_set<int> hitParts;
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6],
                             mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside && t < static_cast<int>(triToPart_.size()) && triToPart_[t] >= 0) {
                hitParts.insert(triToPart_[t]);
            }
        }
        for (int p : hitParts) {
            selectPart(p);
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                const auto partIt = elemToPart_.find(elementId);
                if (partIt != elemToPart_.end()) {
                    selectPart(partIt->second);
                }
            }
        }
    } else {
        // 单元模式：遍历所有三角形，如果三角形任意一个顶点在框内则选中该单元
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            int elemId = triToElem_[t];
            if (selection_.isElementSelected(elemId)) continue;  // 已选中，跳过

            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6],
                             mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) {
                    anyInside = true;
                    break;
                }
            }
            if (anyInside) {
                selection_.selectedElements.insert(elemId);
            }
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (elementId < 0 ||
                selection_.isElementSelected(elementId) ||
                !isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                selection_.selectedElements.insert(elementId);
            }
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        PickMode emitMode = pickMode_;
        if (!selection_.selectedNodes.empty()) {
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        } else {
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        }
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(emitMode, static_cast<int>(ids.size()), ids);
    }
    // 部件模式：发射选中的部件索引列表，同步模型树
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi) {
            if (isPartFullySelected(pi))
                pickedParts.push_back(pi);
        }
        emit partsPicked(pickedParts);
    }
}


void GLWidget::deselectAtPoint(const QPoint& pos) {
    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 view = cam_.viewMatrix();
    glm::mat4 mvp = projection * view;

    int elemId = -1;
    if (pickFramebuffer_ && pickFramebuffer_->isValid() && !triToElem_.empty()) {
        renderPickBuffer(mvp);

        unsigned char pixel[4] = {0};
        int dpr = devicePixelRatio();
        int px = pos.x() * dpr;
        int py = (height() - pos.y()) * dpr;
        auto* glBackend = openGLBackend();
        glBackend->readFramebufferPixel(*pickFramebuffer_, px, py, pixel);
        elemId = colorToId(pixel[0], pixel[1], pixel[2]);
    }
    if (elemId < 0) {
        elemId = edgeElementAtPoint(pos, mvp, 8.0f);
    }

    if (pickMode_ == PickMode::Node) {
        int closestNode = closestNodeForElement(elemId, pos, mvp);
        if (closestNode >= 0) selection_.selectedNodes.erase(closestNode);

    } else if (pickMode_ == PickMode::Part) {
        if (elemId >= 0 && !elemToPart_.empty()) {
            auto it = elemToPart_.find(elemId);
            if (it != elemToPart_.end()) deselectPart(it->second);
        }

    } else {
        if (elemId >= 0) selection_.selectedElements.erase(elemId);
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        if (!selection_.selectedNodes.empty())
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    }
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }
}


void GLWidget::deselectInRect(const QRect& rect) {
    if (triToElem_.empty() && mesh_.elemEdgeToElement.empty()) return;

    float aspect = (height() > 0) ? static_cast<float>(width()) / height() : 1.0f;
    glm::mat4 projection = projectionMatrix(aspect);
    glm::mat4 mvp = projection * cam_.viewMatrix();

    float ndcL = (2.0f * rect.left() / width()) - 1.0f;
    float ndcR = (2.0f * rect.right() / width()) - 1.0f;
    float ndcT = 1.0f - (2.0f * rect.top() / height());
    float ndcB = 1.0f - (2.0f * rect.bottom() / height());
    if (ndcL > ndcR) std::swap(ndcL, ndcR);
    if (ndcB > ndcT) std::swap(ndcB, ndcT);

    auto pointInside = [&](const glm::vec3& p) {
        glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
        if (clip.w <= 0) return false;
        float sx = clip.x / clip.w;
        float sy = clip.y / clip.w;
        return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
    };

    if (pickMode_ == PickMode::Node) {
        std::unordered_set<int> removedNodes;
        auto pointInside = [&](const glm::vec3& p) {
            glm::vec4 clip = mvp * glm::vec4(p, 1.0f);
            if (clip.w <= 0) return false;
            float sx = clip.x / clip.w;
            float sy = clip.y / clip.w;
            return sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT;
        };
        const int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec3 p(mesh_.vertices[vi * 6],
                            mesh_.vertices[vi * 6 + 1],
                            mesh_.vertices[vi * 6 + 2]);
                if (pointInside(p)) {
                    int nodeId = (vi < static_cast<int>(vertexToNode_.size())) ? vertexToNode_[vi] : static_cast<int>(vi);
                    if (nodeId >= 0 && removedNodes.insert(nodeId).second)
                        selection_.selectedNodes.erase(nodeId);
                }
            }
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId) ||
                edge >= static_cast<int>(mesh_.elemEdgeNodeIds.size())) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
            if (node0 >= 0 && pointInside(p0) && removedNodes.insert(node0).second)
                selection_.selectedNodes.erase(node0);
            if (node1 >= 0 && pointInside(p1) && removedNodes.insert(node1).second)
                selection_.selectedNodes.erase(node1);
        }
    } else if (pickMode_ == PickMode::Part) {
        std::unordered_set<int> hitParts;
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) { anyInside = true; break; }
            }
            if (anyInside && t < static_cast<int>(triToPart_.size()) && triToPart_[t] >= 0)
                hitParts.insert(triToPart_[t]);
        }
        for (int p : hitParts) deselectPart(p);
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (!isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                const auto partIt = elemToPart_.find(elementId);
                if (partIt != elemToPart_.end()) {
                    deselectPart(partIt->second);
                }
            }
        }
    } else {
        int triCount = static_cast<int>(triToElem_.size());
        for (int t = 0; t < triCount; ++t) {
            if (!isTriangleVisible(t)) continue;
            int elemId = triToElem_[t];
            if (!selection_.isElementSelected(elemId)) continue;
            bool anyInside = false;
            for (int v = 0; v < 3; ++v) {
                unsigned int vi = mesh_.indices[t * 3 + v];
                glm::vec4 wp(mesh_.vertices[vi * 6], mesh_.vertices[vi * 6 + 1],
                             mesh_.vertices[vi * 6 + 2], 1.0f);
                glm::vec4 clip = mvp * wp;
                if (clip.w <= 0) continue;
                float sx = clip.x / clip.w;
                float sy = clip.y / clip.w;
                if (sx >= ndcL && sx <= ndcR && sy >= ndcB && sy <= ndcT) { anyInside = true; break; }
            }
            if (anyInside) selection_.selectedElements.erase(elemId);
        }
        const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                           static_cast<int>(mesh_.elemEdgeVertices.size() / 6));
        for (int edge = 0; edge < elemEdgeCount; ++edge) {
            const int elementId = mesh_.elemEdgeToElement[edge];
            if (elementId < 0 ||
                !selection_.isElementSelected(elementId) ||
                !isElementVisibleForSelection(elementId)) {
                continue;
            }
            const int base = edge * 6;
            const glm::vec3 p0(mesh_.elemEdgeVertices[base],
                               mesh_.elemEdgeVertices[base + 1],
                               mesh_.elemEdgeVertices[base + 2]);
            const glm::vec3 p1(mesh_.elemEdgeVertices[base + 3],
                               mesh_.elemEdgeVertices[base + 4],
                               mesh_.elemEdgeVertices[base + 5]);
            if (pointInside(p0) || pointInside(p1)) {
                selection_.selectedElements.erase(elementId);
            }
        }
    }

    selectionDirty_ = true;
    {
        std::vector<int> ids;
        if (!selection_.selectedNodes.empty())
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
    }
    if (pickMode_ == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }
}

// ============================================================
// 私有方法
// ============================================================


