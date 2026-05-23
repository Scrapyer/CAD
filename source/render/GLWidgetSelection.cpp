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

namespace {
constexpr int kFullElementHighlightLimit = 2000;
constexpr float kFeatureAngleThreshold = 0.5f;  // cos(60°)
}

void GLWidget::selectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return;
    if (!isPartVisible(partIndex)) return;
    for (int eid : partElementIds_[partIndex]) {
        if (isElementVisibleForSelection(eid))
            selection_.selectedElements.insert(eid);
    }
}


void GLWidget::deselectPart(int partIndex) {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return;
    for (int eid : partElementIds_[partIndex]) {
        selection_.selectedElements.erase(eid);
    }
}


bool GLWidget::isPartFullySelected(int partIndex) const {
    if (partIndex < 0 || partIndex >= static_cast<int>(partElementIds_.size())) return false;
    const auto& elems = partElementIds_[partIndex];
    if (elems.empty()) return false;
    bool hasVisibleElement = false;
    for (int eid : elems) {
        if (!isElementVisibleForSelection(eid)) continue;
        hasVisibleElement = true;
        if (!selection_.isElementSelected(eid)) return false;
    }
    return hasVisibleElement;
}


void GLWidget::rebuildSelectionEdges() {
    int edgeCount = static_cast<int>(mesh_.elemEdgeToElement.size());
    std::vector<float> verts;
    selectionEdgeCacheUsesSilhouette_ = false;

    if (pickMode_ == PickMode::Part && !vertexToNode_.empty()) {
        // 部件模式：使用缓存机制，避免每帧重建 edgeMap
        if (!partEdgeCacheValid_) {
            buildPartEdgeCache();
        }
        selectionEdgeCacheUsesSilhouette_ = true;
        updateSilhouetteFromCache();
        return;  // VBO 已在 updateSilhouetteFromCache 中上传
    } else if (pickMode_ == PickMode::Element &&
               static_cast<int>(selection_.selectedElements.size()) > kFullElementHighlightLimit &&
               !vertexToNode_.empty() && !triToElem_.empty()) {
        // 大规模单元选中：只显示外边界/特征边/视角轮廓，避免绘制海量内部网格线
        buildElementEdgeCache();
        selectionEdgeCacheUsesSilhouette_ = true;
        updateSilhouetteFromCache();
        return;
    } else {
        // 单元模式：显示所有选中单元的全部边线
        for (int i = 0; i < edgeCount; ++i) {
            if (!selection_.isElementSelected(mesh_.elemEdgeToElement[i])) continue;

            int base = i * 6;
            for (int j = 0; j < 6; ++j)
                verts.push_back(mesh_.elemEdgeVertices[base + j]);
        }
    }

    selEdgeVertCount_ = static_cast<int>(verts.size() / 3);

    if (selectionEdgeResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadLineVertices(
            *selectionEdgeResource_,
            verts.data(),
            static_cast<int>(verts.size() * sizeof(float)));
    }
}


void GLWidget::buildEdgeAdjacency() {
    edgeAdjMap_.clear();
    edgeAdjDirty_ = false;

    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    if (triCount == 0) return;

    // 预分配（每个三角形 3 条边，约 50% 共享 → ~1.5x triCount 条边）
    edgeAdjMap_.reserve(triCount * 2);

    for (int t = 0; t < triCount; ++t) {
        for (int e = 0; e < 3; ++e) {
            unsigned int vi_a = mesh_.indices[t * 3 + e];
            unsigned int vi_b = mesh_.indices[t * 3 + (e + 1) % 3];
            int na = (vi_a < vertexToNode_.size()) ? vertexToNode_[vi_a] : static_cast<int>(vi_a);
            int nb = (vi_b < vertexToNode_.size()) ? vertexToNode_[vi_b] : static_cast<int>(vi_b);
            int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) | static_cast<uint32_t>(std::max(na, nb));

            auto& pe = edgeAdjMap_[key];
            if (pe.adjTris.empty()) { pe.va = vi_a; pe.vb = vi_b; }
            pe.adjTris.push_back(t);
        }
    }
}


void GLWidget::buildPartEdgeCache() {
    // 确保边邻接表已构建
    if (edgeAdjDirty_) buildEdgeAdjacency();

    cachedStaticEdgeVerts_.clear();
    cachedSilhouettes_.clear();

    // ── 1. 收集选中且可见的部件索引 ──
    std::unordered_set<int> selectedParts;
    int numParts = static_cast<int>(partElementIds_.size());
    for (int p = 0; p < numParts; ++p) {
        auto vit = partVisibility_.find(p);
        if (vit != partVisibility_.end() && !vit->second) continue;
        for (int eid : partElementIds_[p]) {
            if (selection_.isElementSelected(eid)) {
                selectedParts.insert(p);
                break;
            }
        }
    }

    if (selectedParts.empty()) {
        partEdgeCacheValid_ = true;
        return;
    }

    // ── 2. 只遍历选中部件的三角形，收集边并分类 ──
    auto triNormal = [&](int t) -> glm::vec3 {
        unsigned int i0 = mesh_.indices[t * 3];
        unsigned int i1 = mesh_.indices[t * 3 + 1];
        unsigned int i2 = mesh_.indices[t * 3 + 2];
        glm::vec3 p0(mesh_.vertices[i0 * 6], mesh_.vertices[i0 * 6 + 1], mesh_.vertices[i0 * 6 + 2]);
        glm::vec3 p1(mesh_.vertices[i1 * 6], mesh_.vertices[i1 * 6 + 1], mesh_.vertices[i1 * 6 + 2]);
        glm::vec3 p2(mesh_.vertices[i2 * 6], mesh_.vertices[i2 * 6 + 1], mesh_.vertices[i2 * 6 + 2]);
        glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
        float len = glm::length(cr);
        return (len > 1e-12f) ? cr / len : glm::vec3(0.0f);
    };

    auto pushEdgeVerts = [&](unsigned int a, unsigned int b, std::vector<float>& out) {
        out.push_back(mesh_.vertices[a * 6]);
        out.push_back(mesh_.vertices[a * 6 + 1]);
        out.push_back(mesh_.vertices[a * 6 + 2]);
        out.push_back(mesh_.vertices[b * 6]);
        out.push_back(mesh_.vertices[b * 6 + 1]);
        out.push_back(mesh_.vertices[b * 6 + 2]);
    };

    // 用 visited 集合确保每条边只处理一次
    std::unordered_set<int64_t> visitedEdges;

    // 预估容量（减少 rehash）
    int totalSelectedTris = 0;
    for (int p : selectedParts) totalSelectedTris += static_cast<int>(partTriangles_[p].size());
    visitedEdges.reserve(totalSelectedTris * 2);
    cachedStaticEdgeVerts_.reserve(totalSelectedTris * 6);

    for (int p : selectedParts) {
        for (int t : partTriangles_[p]) {
            for (int e = 0; e < 3; ++e) {
                unsigned int vi_a = mesh_.indices[t * 3 + e];
                unsigned int vi_b = mesh_.indices[t * 3 + (e + 1) % 3];
                int na = (vi_a < vertexToNode_.size()) ? vertexToNode_[vi_a] : static_cast<int>(vi_a);
                int nb = (vi_b < vertexToNode_.size()) ? vertexToNode_[vi_b] : static_cast<int>(vi_b);
                int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) | static_cast<uint32_t>(std::max(na, nb));

                if (!visitedEdges.insert(key).second) continue;  // 已处理

                auto it = edgeAdjMap_.find(key);
                if (it == edgeAdjMap_.end()) continue;

                const PreEdge& pe = it->second;

                // 分类邻接三角形
                int selectedTriCount = 0;
                int otherTriCount = 0;
                int selTri0 = -1, selTri1 = -1;

                for (int adjT : pe.adjTris) {
                    int adjPart = (adjT < static_cast<int>(triToPart_.size())) ? triToPart_[adjT] : -1;
                    if (adjPart >= 0 && selectedParts.count(adjPart)) {
                        if (selectedTriCount == 0) selTri0 = adjT;
                        else if (selectedTriCount == 1) selTri1 = adjT;
                        selectedTriCount++;
                    } else {
                        otherTriCount++;
                    }
                }

                // 边界边（与非选中部件共享）
                if (otherTriCount > 0) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 开放边（只有一个三角形）
                if (selectedTriCount == 1) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    continue;
                }
                // 特征边 or 轮廓边候选
                if (selectedTriCount >= 2 && selTri0 >= 0 && selTri1 >= 0) {
                    glm::vec3 n0 = triNormal(selTri0);
                    glm::vec3 n1 = triNormal(selTri1);
                    if (glm::dot(n0, n1) < kFeatureAngleThreshold) {
                        pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                    } else {
                        SilhouetteCandidate sc;
                        sc.ax = mesh_.vertices[pe.va * 6];
                        sc.ay = mesh_.vertices[pe.va * 6 + 1];
                        sc.az = mesh_.vertices[pe.va * 6 + 2];
                        sc.bx = mesh_.vertices[pe.vb * 6];
                        sc.by = mesh_.vertices[pe.vb * 6 + 1];
                        sc.bz = mesh_.vertices[pe.vb * 6 + 2];
                        sc.n0 = n0;
                        sc.n1 = n1;
                        cachedSilhouettes_.push_back(sc);
                    }
                }
            }
        }
    }

    partEdgeCacheValid_ = true;
}

void GLWidget::buildElementEdgeCache() {
    if (edgeAdjDirty_) buildEdgeAdjacency();

    cachedStaticEdgeVerts_.clear();
    cachedSilhouettes_.clear();

    auto triNormal = [&](int t) -> glm::vec3 {
        unsigned int i0 = mesh_.indices[t * 3];
        unsigned int i1 = mesh_.indices[t * 3 + 1];
        unsigned int i2 = mesh_.indices[t * 3 + 2];
        glm::vec3 p0(mesh_.vertices[i0 * 6], mesh_.vertices[i0 * 6 + 1], mesh_.vertices[i0 * 6 + 2]);
        glm::vec3 p1(mesh_.vertices[i1 * 6], mesh_.vertices[i1 * 6 + 1], mesh_.vertices[i1 * 6 + 2]);
        glm::vec3 p2(mesh_.vertices[i2 * 6], mesh_.vertices[i2 * 6 + 1], mesh_.vertices[i2 * 6 + 2]);
        glm::vec3 cr = glm::cross(p1 - p0, p2 - p0);
        float len = glm::length(cr);
        return (len > 1e-12f) ? cr / len : glm::vec3(0.0f);
    };

    auto pushEdgeVerts = [&](unsigned int a, unsigned int b, std::vector<float>& out) {
        out.push_back(mesh_.vertices[a * 6]);
        out.push_back(mesh_.vertices[a * 6 + 1]);
        out.push_back(mesh_.vertices[a * 6 + 2]);
        out.push_back(mesh_.vertices[b * 6]);
        out.push_back(mesh_.vertices[b * 6 + 1]);
        out.push_back(mesh_.vertices[b * 6 + 2]);
    };

    std::unordered_set<int64_t> visitedEdges;
    visitedEdges.reserve(std::min<size_t>(triToElem_.size() * 2,
                                          selection_.selectedElements.size() * 12));
    cachedStaticEdgeVerts_.reserve(selection_.selectedElements.size() * 12);

    const int triCount = std::min(static_cast<int>(triToElem_.size()),
                                  static_cast<int>(mesh_.indices.size() / 3));
    for (int t = 0; t < triCount; ++t) {
        const int elemId = triToElem_[t];
        if (!selection_.isElementSelected(elemId) || !isTriangleVisible(t)) {
            continue;
        }

        for (int e = 0; e < 3; ++e) {
            unsigned int vi_a = mesh_.indices[t * 3 + e];
            unsigned int vi_b = mesh_.indices[t * 3 + (e + 1) % 3];
            int na = (vi_a < vertexToNode_.size()) ? vertexToNode_[vi_a] : static_cast<int>(vi_a);
            int nb = (vi_b < vertexToNode_.size()) ? vertexToNode_[vi_b] : static_cast<int>(vi_b);
            int64_t key = (static_cast<int64_t>(std::min(na, nb)) << 32) |
                          static_cast<uint32_t>(std::max(na, nb));

            if (!visitedEdges.insert(key).second) continue;

            auto it = edgeAdjMap_.find(key);
            if (it == edgeAdjMap_.end()) continue;

            const PreEdge& pe = it->second;
            int selectedTriCount = 0;
            int otherTriCount = 0;
            int selTri0 = -1;
            int selTri1 = -1;

            for (int adjT : pe.adjTris) {
                const bool adjSelected =
                    adjT >= 0 &&
                    adjT < static_cast<int>(triToElem_.size()) &&
                    selection_.isElementSelected(triToElem_[adjT]) &&
                    isTriangleVisible(adjT);
                if (adjSelected) {
                    if (selectedTriCount == 0) selTri0 = adjT;
                    else if (selectedTriCount == 1) selTri1 = adjT;
                    selectedTriCount++;
                } else {
                    otherTriCount++;
                }
            }

            if (otherTriCount > 0 || selectedTriCount == 1) {
                pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                continue;
            }

            if (selectedTriCount >= 2 && selTri0 >= 0 && selTri1 >= 0) {
                glm::vec3 n0 = triNormal(selTri0);
                glm::vec3 n1 = triNormal(selTri1);
                if (glm::dot(n0, n1) < kFeatureAngleThreshold) {
                    pushEdgeVerts(pe.va, pe.vb, cachedStaticEdgeVerts_);
                } else {
                    SilhouetteCandidate sc;
                    sc.ax = mesh_.vertices[pe.va * 6];
                    sc.ay = mesh_.vertices[pe.va * 6 + 1];
                    sc.az = mesh_.vertices[pe.va * 6 + 2];
                    sc.bx = mesh_.vertices[pe.vb * 6];
                    sc.by = mesh_.vertices[pe.vb * 6 + 1];
                    sc.bz = mesh_.vertices[pe.vb * 6 + 2];
                    sc.n0 = n0;
                    sc.n1 = n1;
                    cachedSilhouettes_.push_back(sc);
                }
            }
        }
    }

    partEdgeCacheValid_ = true;
}


void GLWidget::updateSilhouetteFromCache() {
    // 预分配：静态边 + 最大可能的轮廓边
    size_t staticSize = cachedStaticEdgeVerts_.size();
    std::vector<float> verts;
    verts.reserve(staticSize + cachedSilhouettes_.size() * 6);

    // 复制静态边（边界/特征/开放）
    verts.insert(verts.end(), cachedStaticEdgeVerts_.begin(), cachedStaticEdgeVerts_.end());

    // 添加视角依赖的轮廓边
    glm::vec3 eyePos = cam_.eye();
    for (const auto& sc : cachedSilhouettes_) {
        glm::vec3 edgeMid((sc.ax + sc.bx) * 0.5f,
                          (sc.ay + sc.by) * 0.5f,
                          (sc.az + sc.bz) * 0.5f);
        glm::vec3 viewDir = eyePos - edgeMid;
        float d0 = glm::dot(sc.n0, viewDir);
        float d1 = glm::dot(sc.n1, viewDir);
        if (d0 * d1 <= 0.0f) {
            verts.push_back(sc.ax); verts.push_back(sc.ay); verts.push_back(sc.az);
            verts.push_back(sc.bx); verts.push_back(sc.by); verts.push_back(sc.bz);
        }
    }

    selEdgeVertCount_ = static_cast<int>(verts.size() / 3);

    if (selectionEdgeResource_) {
        auto* glBackend = openGLBackend();
        glBackend->uploadLineVertices(
            *selectionEdgeResource_,
            verts.data(),
            static_cast<int>(verts.size() * sizeof(float)));
    }
}


void GLWidget::setShowLabels(bool show) {
    if (showLabels_ != show) {
        showLabels_ = show;
        update();
    }
}


void GLWidget::selectByIds(PickMode mode, const std::vector<int>& ids) {
    pickMode_ = mode;
    selection_.clear();

    if (mode == PickMode::Node) {
        // 过滤：只保留网格中实际存在的节点 ID
        std::unordered_set<int> validNodes(vertexToNode_.begin(), vertexToNode_.end());
        for (int id : ids) {
            if (validNodes.count(id) && isNodeVisibleForSelection(id))
                selection_.selectedNodes.insert(id);
        }
    } else if (mode == PickMode::Part) {
        for (int pi : ids) selectPart(pi);
    } else {
        // 过滤：只保留渲染网格中存在的单元 ID（含三角面和 1D 边线）
        std::unordered_set<int> validElems(triToElem_.begin(), triToElem_.end());
        for (int eid : mesh_.elemEdgeToElement)
            validElems.insert(eid);
        for (int id : ids) {
            if (validElems.count(id) && isElementVisibleForSelection(id))
                selection_.selectedElements.insert(id);
        }
    }

    selectionDirty_ = true;

    // 发射选中变更信号（只包含实际匹配的 ID）
    std::vector<int> matchedIds;
    if (mode == PickMode::Node) {
        matchedIds.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
    } else if (mode == PickMode::Part) {
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) matchedIds.push_back(pi);
    } else {
        matchedIds.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
    }
    std::sort(matchedIds.begin(), matchedIds.end());
    emit selectionChanged(mode, static_cast<int>(matchedIds.size()), matchedIds);

    if (mode == PickMode::Part) {
        std::vector<int> pickedParts;
        for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
            if (isPartFullySelected(pi)) pickedParts.push_back(pi);
        emit partsPicked(pickedParts);
    }

    update();
}


void GLWidget::highlightParts(const std::vector<int>& partIndices) {
    // 清除当前选中
    selection_.selectedElements.clear();
    selection_.selectedNodes.clear();

    // 将指定部件的所有单元加入选中
    for (int pi : partIndices)
        selectPart(pi);

    // 触发高亮重建
    partEdgeCacheValid_ = false;
    selectionDirty_ = true;

    // 切换到部件拾取模式以使用轮廓边高亮
    pickMode_ = PickMode::Part;

    emit selectionChanged(pickMode_,
                          static_cast<int>(selection_.selectedElements.size()),
                          std::vector<int>(selection_.selectedElements.begin(),
                                           selection_.selectedElements.end()));
    update();
}
