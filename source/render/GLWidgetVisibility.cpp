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
// ── 部件颜色调色板（Catppuccin Mocha）──
static const glm::vec3 kPartPalette[] = {
    {0.61f, 0.86f, 0.63f},  // green   #a6e3a1
    {0.54f, 0.71f, 0.98f},  // blue    #89b4fa
    {0.98f, 0.70f, 0.53f},  // peach   #fab387
    {0.82f, 0.62f, 0.98f},  // mauve   #cba6f7
    {0.58f, 0.89f, 0.83f},  // teal    #94e2d5
    {0.98f, 0.89f, 0.69f},  // yellow  #f9e2af
    {0.94f, 0.56f, 0.66f},  // red     #eba0ac
    {0.71f, 0.71f, 0.98f},  // lavender #b4befe
};
static const int kPartPaletteSize = static_cast<int>(sizeof(kPartPalette) / sizeof(kPartPalette[0]));

template <typename T>
void alignSize(std::vector<T>& arr, int targetSize, const T& fillValue) {
    if (targetSize < 0) {
        targetSize = 0;
    }
    if (static_cast<int>(arr.size()) > targetSize) {
        arr.resize(static_cast<size_t>(targetSize));
    } else if (static_cast<int>(arr.size()) < targetSize) {
        arr.resize(static_cast<size_t>(targetSize), fillValue);
    }
}
}

void GLWidget::rebuildPartLookup()
{
    int numParts = 0;
    for (int part : triToPart_) {
        if (part >= 0) numParts = std::max(numParts, part + 1);
    }
    for (int part : edgeToPart_) {
        if (part >= 0) numParts = std::max(numParts, part + 1);
    }

    partColors_.resize(numParts);
    for (int i = 0; i < numParts; ++i)
        partColors_[i] = kPartPalette[i % kPartPaletteSize];

    partTriangles_.clear();
    partTriangles_.resize(numParts);
    partElementIds_.clear();
    partElementIds_.resize(numParts);
    elemToPart_.clear();

    std::vector<std::unordered_set<int>> partElementSets(numParts);

    const int triCount = std::min(static_cast<int>(triToPart_.size()),
                                  static_cast<int>(triToElem_.size()));
    for (int t = 0; t < triCount; ++t) {
        const int part = triToPart_[t];
        const int element = triToElem_[t];
        if (part < 0 || part >= numParts || element < 0) continue;
        partTriangles_[part].push_back(t);
        partElementSets[part].insert(element);
    }

    const int edgeCount = std::min(static_cast<int>(edgeToPart_.size()),
                                   static_cast<int>(mesh_.edgeToElement.size()));
    for (int edge = 0; edge < edgeCount; ++edge) {
        const int part = edgeToPart_[edge];
        const int element = mesh_.edgeToElement[edge];
        if (part < 0 || part >= numParts || element < 0) continue;
        partElementSets[part].insert(element);
    }

    for (int part = 0; part < numParts; ++part) {
        auto& ids = partElementIds_[part];
        ids.assign(partElementSets[part].begin(), partElementSets[part].end());
        std::sort(ids.begin(), ids.end());
        for (int element : ids) {
            elemToPart_[element] = part;
        }
    }
}


bool GLWidget::isPartVisible(int partIndex) const
{
    if (partIndex < 0) {
        return true;
    }
    auto it = partVisibility_.find(partIndex);
    return it == partVisibility_.end() || it->second;
}


bool GLWidget::isTriangleVisible(int triangleIndex) const
{
    if (triangleIndex < 0 || triangleIndex >= static_cast<int>(triToElem_.size())) {
        return false;
    }
    const int elementId = triToElem_[triangleIndex];
    if (elementId >= 0 && hiddenElementIds_.count(elementId) > 0) {
        return false;
    }
    const int partIndex = triangleIndex < static_cast<int>(triToPart_.size())
        ? triToPart_[triangleIndex]
        : -1;
    return isPartVisible(partIndex);
}


bool GLWidget::isElementVisibleForSelection(int elementId) const
{
    if (elementId >= 0 && hiddenElementIds_.count(elementId) > 0) {
        return false;
    }
    auto it = elemToPart_.find(elementId);
    if (it == elemToPart_.end()) {
        return true;
    }
    return isPartVisible(it->second);
}


bool GLWidget::isNodeVisibleForSelection(int nodeId) const
{
    const int triCount = static_cast<int>(triToElem_.size());
    for (int t = 0; t < triCount; ++t) {
        if (!isTriangleVisible(t)) continue;
        for (int corner = 0; corner < 3; ++corner) {
            const unsigned int vertexIndex = mesh_.indices[t * 3 + corner];
            const int mappedNode = vertexIndex < vertexToNode_.size()
                ? vertexToNode_[vertexIndex]
                : static_cast<int>(vertexIndex);
            if (mappedNode == nodeId) {
                return true;
            }
        }
    }
    const int elemEdgeCount = std::min(static_cast<int>(mesh_.elemEdgeToElement.size()),
                                       static_cast<int>(mesh_.elemEdgeNodeIds.size()));
    for (int edge = 0; edge < elemEdgeCount; ++edge) {
        const int elementId = mesh_.elemEdgeToElement[edge];
        if (!isElementVisibleForSelection(elementId)) continue;
        const auto [node0, node1] = mesh_.elemEdgeNodeIds[edge];
        if (node0 == nodeId || node1 == nodeId) {
            return true;
        }
    }
    return false;
}


void GLWidget::setTriangleToPartMap(const std::vector<int>& map) {
    triToPart_ = map;
    int triCount = static_cast<int>(mesh_.indices.size() / 3);
    alignSize(triToPart_, triCount, -1);
    rebuildPartLookup();

    // 上传 triToPart 到 texture buffer（供片段着色器用 gl_PrimitiveID 查表）
    triPartDirty_ = true;
    needsColorUpload_ = true;
    update();
}


void GLWidget::setEdgeToPartMap(const std::vector<int>& map) {
    edgeToPart_ = map;
    int edgeCount = static_cast<int>(mesh_.edgeIndices.size() / 2);
    alignSize(edgeToPart_, edgeCount, -1);
    rebuildPartLookup();
    edgeVisibilityDirty_ = true;
    update();
}


void GLWidget::setPartVisibility(int partIndex, bool visible) {
    partVisibility_[partIndex] = visible;
    partVisibilityDirty_ = true;
    edgeVisibilityDirty_ = true;
    bool selectionChangedNow = false;
    for (auto it = selection_.selectedElements.begin(); it != selection_.selectedElements.end();) {
        if (!isElementVisibleForSelection(*it)) {
            it = selection_.selectedElements.erase(it);
            selectionChangedNow = true;
        } else {
            ++it;
        }
    }
    for (auto it = selection_.selectedNodes.begin(); it != selection_.selectedNodes.end();) {
        if (!isNodeVisibleForSelection(*it)) {
            it = selection_.selectedNodes.erase(it);
            selectionChangedNow = true;
        } else {
            ++it;
        }
    }
    // 可见性变化影响选中高亮（隐藏部件不显示高亮）
    if (selection_.hasSelection() || selectionChangedNow) {
        partEdgeCacheValid_ = false;
        selectionDirty_ = true;
    }
    if (selectionChangedNow) {
        std::vector<int> ids;
        if (pickMode_ == PickMode::Node)
            ids.assign(selection_.selectedNodes.begin(), selection_.selectedNodes.end());
        else
            ids.assign(selection_.selectedElements.begin(), selection_.selectedElements.end());
        std::sort(ids.begin(), ids.end());
        emit selectionChanged(pickMode_, static_cast<int>(ids.size()), ids);
        if (pickMode_ == PickMode::Part) {
            std::vector<int> pickedParts;
            for (int pi = 0; pi < static_cast<int>(partElementIds_.size()); ++pi)
                if (isPartFullySelected(pi)) pickedParts.push_back(pi);
            emit partsPicked(pickedParts);
        }
    }
    update();
}


void GLWidget::setElementVisibility(int elementId, bool visible)
{
    if (elementId < 0) {
        return;
    }

    if (visible) {
        hiddenElementIds_.erase(elementId);
    } else {
        hiddenElementIds_.insert(elementId);
    }
    partVisibilityDirty_ = true;
    edgeVisibilityDirty_ = true;
    partEdgeCacheValid_ = false;
    selectionDirty_ = true;
    update();
}


void GLWidget::setElementsVisibility(const std::vector<int>& elementIds, bool visible)
{
    bool changed = false;
    for (int elementId : elementIds) {
        if (elementId < 0) {
            continue;
        }
        if (visible) {
            changed = hiddenElementIds_.erase(elementId) > 0 || changed;
        } else {
            changed = hiddenElementIds_.insert(elementId).second || changed;
        }
    }
    if (!changed) {
        return;
    }

    partVisibilityDirty_ = true;
    edgeVisibilityDirty_ = true;
    partEdgeCacheValid_ = false;
    selectionDirty_ = true;
    update();
}


void GLWidget::setAllElementsVisible()
{
    if (hiddenElementIds_.empty()) {
        return;
    }
    hiddenElementIds_.clear();
    partVisibilityDirty_ = true;
    edgeVisibilityDirty_ = true;
    partEdgeCacheValid_ = false;
    selectionDirty_ = true;
    update();
}


