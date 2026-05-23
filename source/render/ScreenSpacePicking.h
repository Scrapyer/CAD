#pragma once

#include "Geometry.h"

#include <QPointF>

#include <algorithm>
#include <array>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace ScreenSpacePicking {

inline float distanceSquaredToSegment(const QPointF& point,
                                      const QPointF& start,
                                      const QPointF& end)
{
    const QPointF ab = end - start;
    const QPointF ap = point - start;
    const float len2 = static_cast<float>(ab.x() * ab.x() + ab.y() * ab.y());
    if (len2 <= 1.0e-6f) {
        const QPointF d = point - start;
        return static_cast<float>(d.x() * d.x() + d.y() * d.y());
    }

    float t = static_cast<float>((ap.x() * ab.x() + ap.y() * ab.y()) / len2);
    t = std::clamp(t, 0.0f, 1.0f);
    const QPointF closest(start.x() + ab.x() * t, start.y() + ab.y() * t);
    const QPointF d = point - closest;
    return static_cast<float>(d.x() * d.x() + d.y() * d.y());
}

inline bool projectToScreen(const glm::vec3& point,
                            const glm::mat4& mvp,
                            float width,
                            float height,
                            bool invertNdcY,
                            QPointF& screen)
{
    const glm::vec4 clip = mvp * glm::vec4(point, 1.0f);
    if (clip.w <= 0.0f || width <= 0.0f || height <= 0.0f) {
        return false;
    }

    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float y01 = ndcY * 0.5f + 0.5f;
    screen = QPointF((ndcX * 0.5f + 0.5f) * width,
                     (invertNdcY ? (1.0f - y01) : y01) * height);
    return true;
}

template <typename IsElementVisible>
int edgeElementAtPoint(const Mesh& mesh,
                       const QPointF& position,
                       const glm::mat4& mvp,
                       float width,
                       float height,
                       bool invertNdcY,
                       float thresholdPx,
                       IsElementVisible isElementVisible)
{
    const size_t edgeCount = std::min(mesh.edgeToElement.size(), mesh.edgeIndices.size() / 2);
    if (edgeCount == 0 || thresholdPx <= 0.0f) {
        return -1;
    }

    auto edgePoint = [&mesh, &mvp, width, height, invertNdcY](unsigned int edgeVertexIndex,
                                                             QPointF& out) {
        const size_t base = static_cast<size_t>(edgeVertexIndex) * 3;
        if (base + 2 >= mesh.edgeVertices.size()) {
            return false;
        }
        return projectToScreen(glm::vec3(mesh.edgeVertices[base],
                                         mesh.edgeVertices[base + 1],
                                         mesh.edgeVertices[base + 2]),
                               mvp,
                               width,
                               height,
                               invertNdcY,
                               out);
    };

    float bestDist2 = thresholdPx * thresholdPx;
    int bestElement = -1;
    for (size_t edge = 0; edge < edgeCount; ++edge) {
        const int elementId = mesh.edgeToElement[edge];
        if (elementId < 0 || !isElementVisible(elementId)) {
            continue;
        }

        QPointF start;
        QPointF end;
        if (!edgePoint(mesh.edgeIndices[edge * 2], start) ||
            !edgePoint(mesh.edgeIndices[edge * 2 + 1], end)) {
            continue;
        }

        const float dist2 = distanceSquaredToSegment(position, start, end);
        if (dist2 < bestDist2) {
            bestDist2 = dist2;
            bestElement = elementId;
        }
    }
    return bestElement;
}

inline int closestNodeForElement(const Mesh& mesh,
                                 const std::vector<int>& triangleToElement,
                                 const std::vector<int>& vertexToNode,
                                 int elementId,
                                 const QPointF& position,
                                 const glm::mat4& mvp,
                                 float width,
                                 float height,
                                 bool invertNdcY)
{
    if (elementId < 0) {
        return -1;
    }

    float bestDist2 = 1.0e30f;
    int closestNode = -1;
    const size_t triCount = std::min(triangleToElement.size(), mesh.indices.size() / 3);
    for (size_t tri = 0; tri < triCount; ++tri) {
        if (triangleToElement[tri] != elementId) {
            continue;
        }
        for (int corner = 0; corner < 3; ++corner) {
            const unsigned int vertexIndex = mesh.indices[tri * 3 + static_cast<size_t>(corner)];
            const size_t base = static_cast<size_t>(vertexIndex) * 6;
            if (base + 2 >= mesh.vertices.size()) {
                continue;
            }

            QPointF screen;
            if (!projectToScreen(glm::vec3(mesh.vertices[base],
                                           mesh.vertices[base + 1],
                                           mesh.vertices[base + 2]),
                                 mvp,
                                 width,
                                 height,
                                 invertNdcY,
                                 screen)) {
                continue;
            }

            const QPointF d = position - screen;
            const float dist2 = static_cast<float>(d.x() * d.x() + d.y() * d.y());
            if (dist2 < bestDist2) {
                bestDist2 = dist2;
                closestNode = vertexIndex < vertexToNode.size()
                    ? vertexToNode[vertexIndex]
                    : static_cast<int>(vertexIndex);
            }
        }
    }

    const size_t elemEdgeCount = std::min(mesh.elemEdgeToElement.size(),
                                          mesh.elemEdgeVertices.size() / 6);
    for (size_t edge = 0; edge < elemEdgeCount; ++edge) {
        if (mesh.elemEdgeToElement[edge] != elementId ||
            edge >= mesh.elemEdgeNodeIds.size()) {
            continue;
        }

        const size_t base = edge * 6;
        const std::array<glm::vec3, 2> points = {
            glm::vec3(mesh.elemEdgeVertices[base],
                      mesh.elemEdgeVertices[base + 1],
                      mesh.elemEdgeVertices[base + 2]),
            glm::vec3(mesh.elemEdgeVertices[base + 3],
                      mesh.elemEdgeVertices[base + 4],
                      mesh.elemEdgeVertices[base + 5])
        };
        const std::array<int, 2> nodes = {
            mesh.elemEdgeNodeIds[edge].first,
            mesh.elemEdgeNodeIds[edge].second
        };
        for (size_t i = 0; i < points.size(); ++i) {
            if (nodes[i] < 0) {
                continue;
            }

            QPointF screen;
            if (!projectToScreen(points[i], mvp, width, height, invertNdcY, screen)) {
                continue;
            }

            const QPointF d = position - screen;
            const float dist2 = static_cast<float>(d.x() * d.x() + d.y() * d.y());
            if (dist2 < bestDist2) {
                bestDist2 = dist2;
                closestNode = nodes[i];
            }
        }
    }

    return closestNode;
}

} // namespace ScreenSpacePicking
