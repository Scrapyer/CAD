/**
 * @file FEMeshConverter.cpp
 * @brief FEMeshConverter 的方法实现
 *
 * 将 FEM 单元拓扑转换为可渲染三角网格，同时生成反向映射表用于拾取。
 *
 * 核心流程：
 *   1. 遍历所有单元，按维度分类处理
 *   2. 2D 单元：直接三角化（TRI3→1 三角形, QUAD4→2 三角形）
 *   3. 3D 单元：先提取外表面，再三角化每个面
 *   4. 每次生成三角形时，同步记录 三角形→单元、三角形→面、顶点→节点 映射
 */

#include "FEMeshConverter.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

// ════════════════════════════════════════════════════════════
// 公有接口：返回 FERenderData（Mesh + 反向映射表）
// ════════════════════════════════════════════════════════════

/**
 * @brief 将整个 FEM 模型转换为可渲染数据包
 *
 * 处理流程：
 *   1. 收集所有 3D 单元的面，做外表面提取（去除内部共享面）
 *   2. 对外表面的每个面进行三角化
 *   3. 对所有 2D 单元直接三角化
 *   4. 1D 单元暂时跳过
 *   5. 每步都同步更新映射表
 */
FERenderData FEMeshConverter::toRenderData(const FEModel& model, const ProgressCallback& progress) {
    // 收集所有单元 ID
    std::vector<int> allIds;
    allIds.reserve(model.elements.size());
    for (const auto& [id, elem] : model.elements) {
        allIds.push_back(id);
    }
    return toRenderData(model, allIds, progress);
}

/**
 * @brief 将指定的单元集转换为可渲染数据包
 *
 * 外表面提取算法：
 *   - 遍历指定单元集的所有 3D 单元，提取每个单元的每个面
 *   - 将面的节点 ID 排序后作为 key
 *   - 统计每个 key 出现的次数：
 *     - 出现 1 次 → 外表面（需要渲染）
 *     - 出现 2 次 → 内部面（两个单元共享，不需要渲染）
 *   - 只对外表面进行三角化
 */
FERenderData FEMeshConverter::toRenderData(const FEModel& model, const std::vector<int>& elementIds, const ProgressCallback& progress) {
    FERenderData result;
    const int totalElems = static_cast<int>(elementIds.size());
    int processed = 0;
    // 进度报告间隔：至少每 1% 或每 200 个单元报告一次
    const int reportInterval = std::max(1, totalElems / 100);

    // ── 第一遍：收集所有面（3D 外表面 + 2D 壳面），统一去重 ──（占 0-40%）

    // FaceKey: 面的节点 ID 排序后作为唯一标识
    // FaceInfo: 记录面所属的单元 ID、面序号、原始节点顺序、是否 2D
    struct FaceInfo {
        int elemId;                    // 面所属的单元 ID
        int faceIndex;                 // 面在单元中的序号
        std::vector<int> faceNodes;    // 面的节点 ID（保持原始顺序，用于确定法线方向）
        bool is2D;                     // 是否来自 2D 壳单元
    };

    // key = 排序后的节点 ID 列表, value = 该面的所有出现记录
    std::map<std::vector<int>, std::vector<FaceInfo>> faceMap;

    processed = 0;
    for (int elemId : elementIds) {
        auto it = model.elements.find(elemId);
        if (it == model.elements.end()) continue;
        const FEElement& elem = it->second;
        int dim = elementDimension(elem.type);

        if (dim == 3) {
            // 提取该 3D 单元的所有面
            auto faces = extractFaces(elem);
            for (int fi = 0; fi < static_cast<int>(faces.size()); ++fi) {
                std::vector<int> sortedKey = faces[fi];
                std::sort(sortedKey.begin(), sortedKey.end());

                FaceInfo info;
                info.elemId = elemId;
                info.faceIndex = fi;
                info.faceNodes = faces[fi];
                info.is2D = false;
                faceMap[sortedKey].push_back(info);
            }
        } else if (dim == 2) {
            // 2D 壳单元也加入 faceMap，用角节点做 key 进行去重
            int cc = elementCornerNodeCount(elem.type);
            std::vector<int> faceNodes(cc);
            for (int i = 0; i < cc; ++i) faceNodes[i] = elem.nodeIds[i];

            std::vector<int> sortedKey = faceNodes;
            std::sort(sortedKey.begin(), sortedKey.end());

            FaceInfo info;
            info.elemId = elemId;
            info.faceIndex = 0;
            info.faceNodes = faceNodes;
            info.is2D = true;
            faceMap[sortedKey].push_back(info);
        }

        if (progress && (++processed % reportInterval == 0)) {
            progress(processed * 40 / totalElems);  // 0-40%
        }
    }
    if (progress) progress(40);

    // ── 第二遍：共享顶点 + 面积加权平滑法线 ──（占 40-70%）

    // Step A: 收集需要渲染的面，建立共享顶点表
    struct RenderFace {
        std::vector<int> faceNodes;  // 面的节点 ID（原始顺序）
        int elemId;
        int faceIndex;
    };
    std::vector<RenderFace> renderFaces;
    renderFaces.reserve(faceMap.size());

    for (const auto& [key, infos] : faceMap) {
        if (infos.size() == 1) {
            const FaceInfo& info = infos[0];
            renderFaces.push_back({info.faceNodes, info.elemId, info.faceIndex});
        } else if (infos.size() >= 2) {
            bool anyIs2D = false;
            for (const auto& info : infos) {
                if (info.is2D) { anyIs2D = true; break; }
            }
            if (anyIs2D) {
                for (const auto& info : infos) {
                    if (info.is2D) {
                        renderFaces.push_back({info.faceNodes, info.elemId, info.faceIndex});
                        break;
                    }
                }
            }
        }
    }

    // 收集所有需要渲染的节点，建立 nodeToVertex 映射
    std::unordered_map<int, unsigned int> nodeToVertex;
    for (const auto& rf : renderFaces) {
        for (int nid : rf.faceNodes) {
            if (nodeToVertex.find(nid) == nodeToVertex.end()) {
                unsigned int vi = static_cast<unsigned int>(nodeToVertex.size());
                nodeToVertex[nid] = vi;
            }
        }
    }

    // 创建顶点数据：每个节点一个渲染顶点，pos(3) + normal(3)，法线初始化为零
    int vertCount = static_cast<int>(nodeToVertex.size());
    result.mesh.vertices.resize(vertCount * 6, 0.0f);
    result.vertexToNode.resize(vertCount, -1);

    // 填充位置和 vertexToNode
    for (const auto& [nid, vi] : nodeToVertex) {
        const glm::vec3* p = model.nodeCoords(nid);
        if (p) {
            result.mesh.vertices[vi * 6 + 0] = p->x;
            result.mesh.vertices[vi * 6 + 1] = p->y;
            result.mesh.vertices[vi * 6 + 2] = p->z;
        }
        result.vertexToNode[vi] = nid;
    }

    if (progress) progress(50);

    // Step B: 三角化 + 累加面积加权法线
    int totalRenderFaces = static_cast<int>(renderFaces.size());
    int faceProcessed = 0;
    int faceReportInterval = std::max(1, totalRenderFaces / 20);

    for (const auto& rf : renderFaces) {
        int n = static_cast<int>(rf.faceNodes.size());
        if (n < 3) continue;

        // Fan 三角化：以第 0 个节点为扇心
        unsigned int vi0 = nodeToVertex[rf.faceNodes[0]];
        for (int i = 1; i < n - 1; ++i) {
            unsigned int vi1 = nodeToVertex[rf.faceNodes[i]];
            unsigned int vi2 = nodeToVertex[rf.faceNodes[i + 1]];

            // 三角形索引（共享顶点）
            result.mesh.indices.push_back(vi0);
            result.mesh.indices.push_back(vi1);
            result.mesh.indices.push_back(vi2);

            // 映射表
            result.triangleToElement.push_back(rf.elemId);
            result.triangleToFace.push_back(rf.faceIndex);

            // 面积加权法线（不归一化的叉积 = 面积加权）
            glm::vec3 p0(result.mesh.vertices[vi0 * 6], result.mesh.vertices[vi0 * 6 + 1], result.mesh.vertices[vi0 * 6 + 2]);
            glm::vec3 p1(result.mesh.vertices[vi1 * 6], result.mesh.vertices[vi1 * 6 + 1], result.mesh.vertices[vi1 * 6 + 2]);
            glm::vec3 p2(result.mesh.vertices[vi2 * 6], result.mesh.vertices[vi2 * 6 + 1], result.mesh.vertices[vi2 * 6 + 2]);
            glm::vec3 faceNormal = glm::cross(p1 - p0, p2 - p0);  // 不归一化 = 面积加权

            // 累加到三个顶点的法线上
            for (unsigned int vi : {vi0, vi1, vi2}) {
                result.mesh.vertices[vi * 6 + 3] += faceNormal.x;
                result.mesh.vertices[vi * 6 + 4] += faceNormal.y;
                result.mesh.vertices[vi * 6 + 5] += faceNormal.z;
            }
        }

        if (progress && (++faceProcessed % faceReportInterval == 0)) {
            progress(50 + faceProcessed * 15 / totalRenderFaces);  // 50-65%
        }
    }

    // Step C: 归一化所有法线
    for (int v = 0; v < vertCount; ++v) {
        float nx = result.mesh.vertices[v * 6 + 3];
        float ny = result.mesh.vertices[v * 6 + 4];
        float nz = result.mesh.vertices[v * 6 + 5];
        float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1e-8f) {
            result.mesh.vertices[v * 6 + 3] = nx / len;
            result.mesh.vertices[v * 6 + 4] = ny / len;
            result.mesh.vertices[v * 6 + 5] = nz / len;
        } else {
            result.mesh.vertices[v * 6 + 3] = 0.0f;
            result.mesh.vertices[v * 6 + 4] = 0.0f;
            result.mesh.vertices[v * 6 + 5] = 1.0f;
        }
    }
    if (progress) progress(70);

    // ── 第四遍：生成外表面边线数据（去重，用于普通线框渲染）──（占 70-85%）
    std::vector<int> edgeElemIds;   // 每条边归属的单元 ID（与 edgeIndices 对应，每条边一项）
    {
        // 使用 map 代替 set，同时记录每条边来自哪个单元（用于 edgeToPart 填充）
        std::map<std::pair<int, int>, int> edgeToElemMap;

        for (const auto& [key, infos] : faceMap) {
            if (infos.size() == 1) {
                const auto& faceNodes = infos[0].faceNodes;
                int elemId = infos[0].elemId;
                int n = static_cast<int>(faceNodes.size());
                for (int i = 0; i < n; ++i) {
                    int a = faceNodes[i];
                    int b = faceNodes[(i + 1) % n];
                    edgeToElemMap.emplace(std::make_pair(std::min(a, b), std::max(a, b)), elemId);
                }
            }
        }

        for (int elemId : elementIds) {
            auto it = model.elements.find(elemId);
            if (it == model.elements.end()) continue;
            const FEElement& elem = it->second;
            int dim = elementDimension(elem.type);
            if (dim == 2) {
                int cc = elementCornerNodeCount(elem.type);
                for (int i = 0; i < cc; ++i) {
                    int a = elem.nodeIds[i];
                    int b = elem.nodeIds[(i + 1) % cc];
                    edgeToElemMap.emplace(std::make_pair(std::min(a, b), std::max(a, b)), elemId);
                }
            } else if (dim == 1 && elem.nodeIds.size() >= 2) {
                int a = elem.nodeIds[0], b = elem.nodeIds[1];
                edgeToElemMap.emplace(std::make_pair(std::min(a, b), std::max(a, b)), elemId);
            }
        }

        for (const auto& [edge, elemId] : edgeToElemMap) {
            auto [a, b] = edge;
            const glm::vec3* pa = model.nodeCoords(a);
            const glm::vec3* pb = model.nodeCoords(b);
            if (!pa || !pb) continue;

            unsigned int idx = static_cast<unsigned int>(result.mesh.edgeVertices.size() / 3);
            result.mesh.edgeVertices.push_back(pa->x);
            result.mesh.edgeVertices.push_back(pa->y);
            result.mesh.edgeVertices.push_back(pa->z);
            result.mesh.edgeVertices.push_back(pb->x);
            result.mesh.edgeVertices.push_back(pb->y);
            result.mesh.edgeVertices.push_back(pb->z);
            result.mesh.edgeIndices.push_back(idx);
            result.mesh.edgeIndices.push_back(idx + 1);
            edgeElemIds.push_back(elemId);
        }
    }
    if (progress) progress(85);

    // ── 第五遍：生成完整单元边线数据（不去重，带单元归属，用于选中高亮）──（占 85-100%）
    {
        // 辅助：添加一条单元边
        auto addElemEdge = [&](int a, int b, int elemId) {
            const glm::vec3* pa = model.nodeCoords(a);
            const glm::vec3* pb = model.nodeCoords(b);
            if (!pa || !pb) return;

            result.mesh.elemEdgeVertices.push_back(pa->x);
            result.mesh.elemEdgeVertices.push_back(pa->y);
            result.mesh.elemEdgeVertices.push_back(pa->z);
            result.mesh.elemEdgeVertices.push_back(pb->x);
            result.mesh.elemEdgeVertices.push_back(pb->y);
            result.mesh.elemEdgeVertices.push_back(pb->z);
            result.mesh.elemEdgeToElement.push_back(elemId);
            result.mesh.elemEdgeNodeIds.push_back({std::min(a, b), std::max(a, b)});
        };

        processed = 0;
        for (int elemId : elementIds) {
            auto it = model.elements.find(elemId);
            if (it == model.elements.end()) continue;
            const FEElement& elem = it->second;
            int dim = elementDimension(elem.type);

            // 用 set 去重同一单元内的边（不同面可能共享边）
            std::set<std::pair<int, int>> elemEdgeSet;

            if (dim == 3) {
                auto faces = extractFaces(elem);
                for (const auto& face : faces) {
                    int n = static_cast<int>(face.size());
                    for (int i = 0; i < n; ++i) {
                        int a = face[i];
                        int b = face[(i + 1) % n];
                        elemEdgeSet.insert({std::min(a, b), std::max(a, b)});
                    }
                }
            } else if (dim == 2) {
                int cc = elementCornerNodeCount(elem.type);
                for (int i = 0; i < cc; ++i) {
                    int a = elem.nodeIds[i];
                    int b = elem.nodeIds[(i + 1) % cc];
                    elemEdgeSet.insert({std::min(a, b), std::max(a, b)});
                }
            } else if (dim == 1 && elem.nodeIds.size() >= 2) {
                elemEdgeSet.insert({std::min(elem.nodeIds[0], elem.nodeIds[1]),
                                    std::max(elem.nodeIds[0], elem.nodeIds[1])});
            }

            for (const auto& [a, b] : elemEdgeSet) {
                addElemEdge(a, b, elemId);
            }

            if (progress && (++processed % reportInterval == 0)) {
                progress(85 + processed * 15 / totalElems);  // 85-100%
            }
        }
    }
    if (progress) progress(100);

    // ── 填充 triangleToPart 和 edgeToPart（三角形/边 → 部件索引）──
    if (!model.parts.empty()) {
        std::unordered_map<int, int> elemToPart;
        for (int i = 0; i < static_cast<int>(model.parts.size()); ++i) {
            for (int eid : model.parts[i].elementIds)
                elemToPart[eid] = i;
        }
        int triCount = static_cast<int>(result.triangleToElement.size());
        result.triangleToPart.resize(triCount, -1);
        for (int t = 0; t < triCount; ++t) {
            auto it = elemToPart.find(result.triangleToElement[t]);
            if (it != elemToPart.end())
                result.triangleToPart[t] = it->second;
        }
        int edgeCount = static_cast<int>(edgeElemIds.size());
        result.edgeToPart.resize(edgeCount, -1);
        for (int e = 0; e < edgeCount; ++e) {
            auto it = elemToPart.find(edgeElemIds[e]);
            if (it != elemToPart.end())
                result.edgeToPart[e] = it->second;
        }
    }

    return result;
}

/**
 * @brief 将 FEM 模型转换为带云图颜色的渲染数据包
 *
 * 在 toRenderData 的基础上，为每个顶点添加颜色属性：
 *   1. 先正常转换得到 FERenderData
 *   2. 遍历每个渲染顶点，通过 vertexToNode 查找对应的 FEM 节点 ID
 *   3. 从标量场中取该节点的值
 *   4. 通过 ColorMap 映射为 RGB 颜色
 *   5. 将颜色追加到顶点数据中（顶点格式变为 [pos(3) + normal(3) + color(3)]）
 */
FERenderData FEMeshConverter::toColoredRenderData(const FEModel& model,
                                                   const FEScalarField& field,
                                                   const ColorMap& colorMap,
                                                   float minVal, float maxVal) {
    // 先做标准转换（得到 pos + normal 格式的 Mesh + 映射表）
    FERenderData result = toRenderData(model);

    // 重建顶点数据：从 [pos(3) + normal(3)] 扩展为 [pos(3) + normal(3) + color(3)]
    int vertCount = result.vertexCount();
    int triCount = result.triangleCount();
    std::vector<float> coloredVertices;
    coloredVertices.reserve(vertCount * 9);  // 9 floats per vertex

    if (field.location == FieldLocation::Element) {
        // ── 单元场：通过 triangleToElement 映射，三角形三顶点同色 ──
        // 先为每个顶点找到其所属三角形的单元 ID
        std::vector<int> vertexElemId(vertCount, -1);
        for (int t = 0; t < triCount; ++t) {
            int elemId = result.triangleToElement[t];
            for (int k = 0; k < 3; ++k) {
                unsigned int vi = result.mesh.indices[t * 3 + k];
                if (static_cast<int>(vi) < vertCount)
                    vertexElemId[vi] = elemId;
            }
        }

        for (int i = 0; i < vertCount; ++i) {
            int base = i * 6;
            for (int j = 0; j < 6; ++j)
                coloredVertices.push_back(result.mesh.vertices[base + j]);

            int elemId = vertexElemId[i];
            glm::vec3 color(0.5f);
            if (elemId >= 0) {
                auto valIt = field.values.find(elemId);
                if (valIt != field.values.end())
                    color = colorMap.map(valIt->second, minVal, maxVal);
            }
            coloredVertices.push_back(color.r);
            coloredVertices.push_back(color.g);
            coloredVertices.push_back(color.b);
        }
    } else {
        // ── 节点场：通过 vertexToNode 映射查值 ──
        for (int i = 0; i < vertCount; ++i) {
            int base = i * 6;
            for (int j = 0; j < 6; ++j)
                coloredVertices.push_back(result.mesh.vertices[base + j]);

            int nodeId = result.vertexToNode[i];
            glm::vec3 color(0.5f);
            if (nodeId >= 0) {
                auto valIt = field.values.find(nodeId);
                if (valIt != field.values.end())
                    color = colorMap.map(valIt->second, minVal, maxVal);
            }
            coloredVertices.push_back(color.r);
            coloredVertices.push_back(color.g);
            coloredVertices.push_back(color.b);
        }
    }

    result.mesh.vertices = std::move(coloredVertices);
    return result;
}

// ════════════════════════════════════════════════════════════
// 辅助接口：仅返回 Mesh
// ════════════════════════════════════════════════════════════

/**
 * @brief 生成变形后的网格
 *
 * 将原始坐标加上位移矢量（乘以放大系数）：
 *   新坐标 = 原始坐标 + displacement[nodeId] × scale
 *
 * 实现方式：先做标准转换，再遍历顶点修改坐标。
 */
Mesh FEMeshConverter::toDeformedMesh(const FEModel& model,
                                      const FEVectorField& displacement,
                                      float scale) {
    // 先获取带映射表的数据（需要 vertexToNode 来查位移）
    FERenderData renderData = toRenderData(model);

    // 修改每个顶点的坐标：加上位移 × 放大系数
    int vertCount = renderData.vertexCount();
    for (int i = 0; i < vertCount; ++i) {
        int nodeId = renderData.vertexToNode[i];
        if (nodeId >= 0) {
            auto dispIt = displacement.values.find(nodeId);
            if (dispIt != displacement.values.end()) {
                int base = i * 6;  // 每顶点 6 float: pos(3) + normal(3)
                renderData.mesh.vertices[base + 0] += dispIt->second.x * scale;
                renderData.mesh.vertices[base + 1] += dispIt->second.y * scale;
                renderData.mesh.vertices[base + 2] += dispIt->second.z * scale;
                // 注意：变形后法线也应该更新（严格来说需要重新计算）
                // 但对于小变形预览，保持原法线近似即可
            }
        }
    }

    return renderData.mesh;
}

/**
 * @brief 生成线框网格（仅包含边）
 *
 * 遍历所有单元，提取边并去重。
 * 每条边生成两个顶点（用 GL_LINES 渲染）。
 *
 * 去重方法：将边的两个节点 ID 排序后作为 key 存入 set。
 */
Mesh FEMeshConverter::toWireframeMesh(const FEModel& model) {
    Mesh mesh;

    // 用 set 去重：每条边只保留一次
    // key = (较小节点 ID, 较大节点 ID)
    std::set<std::pair<int, int>> edgeSet;

    for (const auto& [elemId, elem] : model.elements) {
        int dim = elementDimension(elem.type);
        int cornerCount = elementCornerNodeCount(elem.type);

        if (dim == 1) {
            // 1D 单元：一条边（首尾两个角节点）
            if (cornerCount >= 2) {
                int a = elem.nodeIds[0], b = elem.nodeIds[1];
                edgeSet.insert({std::min(a, b), std::max(a, b)});
            }
        } else if (dim == 2) {
            // 2D 单元：角节点顺序连接形成边
            for (int i = 0; i < cornerCount; ++i) {
                int a = elem.nodeIds[i];
                int b = elem.nodeIds[(i + 1) % cornerCount];
                edgeSet.insert({std::min(a, b), std::max(a, b)});
            }
        } else {
            // 3D 单元：提取所有面，每个面的边
            auto faces = extractFaces(elem);
            for (const auto& face : faces) {
                int n = static_cast<int>(face.size());
                for (int i = 0; i < n; ++i) {
                    int a = face[i];
                    int b = face[(i + 1) % n];
                    edgeSet.insert({std::min(a, b), std::max(a, b)});
                }
            }
        }
    }

    // 将去重后的边转换为 Mesh 顶点（每条边 2 个顶点）
    // 线框不需要法线，但为了与现有顶点格式一致，法线填 (0,0,0)
    for (const auto& [a, b] : edgeSet) {
        const glm::vec3* pa = model.nodeCoords(a);
        const glm::vec3* pb = model.nodeCoords(b);
        if (!pa || !pb) continue;

        unsigned int idx = static_cast<unsigned int>(mesh.vertices.size() / 6);

        // 顶点 A: pos + 空法线
        mesh.vertices.push_back(pa->x);
        mesh.vertices.push_back(pa->y);
        mesh.vertices.push_back(pa->z);
        mesh.vertices.push_back(0.0f);
        mesh.vertices.push_back(0.0f);
        mesh.vertices.push_back(0.0f);

        // 顶点 B: pos + 空法线
        mesh.vertices.push_back(pb->x);
        mesh.vertices.push_back(pb->y);
        mesh.vertices.push_back(pb->z);
        mesh.vertices.push_back(0.0f);
        mesh.vertices.push_back(0.0f);
        mesh.vertices.push_back(0.0f);

        // 线段索引
        mesh.indices.push_back(idx);
        mesh.indices.push_back(idx + 1);
    }

    return mesh;
}

// ════════════════════════════════════════════════════════════
// 私有辅助方法
// ════════════════════════════════════════════════════════════

/**
 * @brief 将 2D 单元三角化并追加到渲染数据包
 *
 * 使用 flat shading：每个三角形的三个顶点共享同一法线，
 * 因此同一个 FEM 节点如果被多个三角形使用，会生成多个渲染顶点
 * （法线不同，所以不能共享顶点）。
 *
 * 三角化规则：
 *   - TRI3/TRI6:  → 1 个三角形 (0,1,2)
 *   - QUAD4/QUAD8: → 2 个三角形 (0,1,2) + (0,2,3)
 *
 * 高阶单元只取角节点做线性近似。
 */
void FEMeshConverter::tessellate2D(FERenderData& renderData, const FEElement& elem, const FEModel& model) {
    int cornerCount = elementCornerNodeCount(elem.type);

    // 收集角节点的坐标
    std::vector<glm::vec3> coords(cornerCount);
    for (int i = 0; i < cornerCount; ++i) {
        const glm::vec3* p = model.nodeCoords(elem.nodeIds[i]);
        coords[i] = p ? *p : glm::vec3(0.0f);
    }

    // 三角化：TRI → 1 三角形, QUAD → 2 三角形
    // 使用 fan 三角化：以第 0 个顶点为扇心
    for (int i = 1; i < cornerCount - 1; ++i) {
        // 三角形顶点索引：0, i, i+1
        glm::vec3 a = coords[0];
        glm::vec3 b = coords[i];
        glm::vec3 c = coords[i + 1];

        // 计算面法线
        glm::vec3 normal = computeNormal(a, b, c);

        // 当前渲染顶点的起始索引
        unsigned int baseIdx = static_cast<unsigned int>(renderData.mesh.vertices.size() / 6);

        // 添加三个顶点（flat shading，法线相同）
        // 顶点 0
        renderData.mesh.vertices.push_back(a.x);
        renderData.mesh.vertices.push_back(a.y);
        renderData.mesh.vertices.push_back(a.z);
        renderData.mesh.vertices.push_back(normal.x);
        renderData.mesh.vertices.push_back(normal.y);
        renderData.mesh.vertices.push_back(normal.z);

        // 顶点 i
        renderData.mesh.vertices.push_back(b.x);
        renderData.mesh.vertices.push_back(b.y);
        renderData.mesh.vertices.push_back(b.z);
        renderData.mesh.vertices.push_back(normal.x);
        renderData.mesh.vertices.push_back(normal.y);
        renderData.mesh.vertices.push_back(normal.z);

        // 顶点 i+1
        renderData.mesh.vertices.push_back(c.x);
        renderData.mesh.vertices.push_back(c.y);
        renderData.mesh.vertices.push_back(c.z);
        renderData.mesh.vertices.push_back(normal.x);
        renderData.mesh.vertices.push_back(normal.y);
        renderData.mesh.vertices.push_back(normal.z);

        // 三角形索引
        renderData.mesh.indices.push_back(baseIdx);
        renderData.mesh.indices.push_back(baseIdx + 1);
        renderData.mesh.indices.push_back(baseIdx + 2);

        // ── 填充反向映射表 ──
        // 三角形 → 单元映射（2D 单元面索引始终为 0）
        renderData.triangleToElement.push_back(elem.id);
        renderData.triangleToFace.push_back(0);

        // 顶点 → 节点映射
        renderData.vertexToNode.push_back(elem.nodeIds[0]);      // 顶点 0 → FEM 节点 0
        renderData.vertexToNode.push_back(elem.nodeIds[i]);      // 顶点 i
        renderData.vertexToNode.push_back(elem.nodeIds[i + 1]);  // 顶点 i+1
    }
}

/**
 * @brief 将 3D 单元的一个面三角化并追加到渲染数据包
 *
 * 面可以是三角形（3 节点）或四边形（4 节点）。
 * 四边形使用 fan 三角化：(0,1,2) + (0,2,3)。
 *
 * @param renderData 输出渲染数据包
 * @param faceNodes  面的节点 ID 列表
 * @param elemId     面所属的单元 ID
 * @param faceIndex  面在单元中的序号
 * @param model      模型（查询节点坐标）
 */
void FEMeshConverter::tessellateFace(FERenderData& renderData,
                                      const std::vector<int>& faceNodes,
                                      int elemId, int faceIndex,
                                      const FEModel& model) {
    int n = static_cast<int>(faceNodes.size());
    if (n < 3) return;

    // 收集面的节点坐标
    std::vector<glm::vec3> coords(n);
    for (int i = 0; i < n; ++i) {
        const glm::vec3* p = model.nodeCoords(faceNodes[i]);
        coords[i] = p ? *p : glm::vec3(0.0f);
    }

    // Fan 三角化
    for (int i = 1; i < n - 1; ++i) {
        glm::vec3 a = coords[0];
        glm::vec3 b = coords[i];
        glm::vec3 c = coords[i + 1];

        glm::vec3 normal = computeNormal(a, b, c);

        unsigned int baseIdx = static_cast<unsigned int>(renderData.mesh.vertices.size() / 6);

        // 添加三个顶点
        renderData.mesh.vertices.push_back(a.x);
        renderData.mesh.vertices.push_back(a.y);
        renderData.mesh.vertices.push_back(a.z);
        renderData.mesh.vertices.push_back(normal.x);
        renderData.mesh.vertices.push_back(normal.y);
        renderData.mesh.vertices.push_back(normal.z);

        renderData.mesh.vertices.push_back(b.x);
        renderData.mesh.vertices.push_back(b.y);
        renderData.mesh.vertices.push_back(b.z);
        renderData.mesh.vertices.push_back(normal.x);
        renderData.mesh.vertices.push_back(normal.y);
        renderData.mesh.vertices.push_back(normal.z);

        renderData.mesh.vertices.push_back(c.x);
        renderData.mesh.vertices.push_back(c.y);
        renderData.mesh.vertices.push_back(c.z);
        renderData.mesh.vertices.push_back(normal.x);
        renderData.mesh.vertices.push_back(normal.y);
        renderData.mesh.vertices.push_back(normal.z);

        // 三角形索引
        renderData.mesh.indices.push_back(baseIdx);
        renderData.mesh.indices.push_back(baseIdx + 1);
        renderData.mesh.indices.push_back(baseIdx + 2);

        // ── 反向映射表 ──
        renderData.triangleToElement.push_back(elemId);
        renderData.triangleToFace.push_back(faceIndex);

        renderData.vertexToNode.push_back(faceNodes[0]);
        renderData.vertexToNode.push_back(faceNodes[i]);
        renderData.vertexToNode.push_back(faceNodes[i + 1]);
    }
}

/**
 * @brief 提取 3D 单元的所有面
 *
 * 按照标准 FEM 约定，返回每个面的节点 ID 列表。
 * 节点顺序决定法线朝向（右手定则，法线朝外）。
 *
 * 支持的单元类型及面定义：
 *   - TET4:    4 个三角面
 *   - TET10:   4 个三角面（只取角节点）
 *   - HEX8:    6 个四边形面
 *   - HEX20:   6 个四边形面（只取角节点）
 *   - WEDGE6:  2 个三角面 + 3 个四边形面 = 5 个面
 *   - PYRAMID5: 1 个四边形底面 + 4 个三角侧面 = 5 个面
 */
std::vector<std::vector<int>> FEMeshConverter::extractFaces(const FEElement& elem) {
    std::vector<std::vector<int>> faces;
    const auto& n = elem.nodeIds;

    switch (elem.type) {
        case ElementType::TET4:
        case ElementType::TET10: {
            // 四面体的 4 个三角面
            // 节点编号约定: 0,1,2,3
            //   面 0: 0-1-2（底面）
            //   面 1: 0-1-3（侧面）
            //   面 2: 1-2-3（侧面）
            //   面 3: 0-2-3（侧面）— 注意顺序保证法线朝外
            faces.push_back({n[0], n[2], n[1]});  // 底面（法线朝下）
            faces.push_back({n[0], n[1], n[3]});
            faces.push_back({n[1], n[2], n[3]});
            faces.push_back({n[0], n[3], n[2]});
            break;
        }

        case ElementType::HEX8:
        case ElementType::HEX20: {
            // 六面体的 6 个四边形面
            // 节点编号约定 (Abaqus/Nastran):
            //   底面: 0-1-2-3    顶面: 4-5-6-7
            //   底面节点与顶面节点一一对应: 0↔4, 1↔5, 2↔6, 3↔7
            //
            //       7 ─────── 6
            //      /|        /|
            //     4 ─────── 5 |
            //     | 3 ──────| 2
            //     |/        |/
            //     0 ─────── 1
            //
            faces.push_back({n[0], n[3], n[2], n[1]});  // 底面 (z-)
            faces.push_back({n[4], n[5], n[6], n[7]});  // 顶面 (z+)
            faces.push_back({n[0], n[1], n[5], n[4]});  // 前面 (y-)
            faces.push_back({n[2], n[3], n[7], n[6]});  // 后面 (y+)
            faces.push_back({n[0], n[4], n[7], n[3]});  // 左面 (x-)
            faces.push_back({n[1], n[2], n[6], n[5]});  // 右面 (x+)
            break;
        }

        case ElementType::WEDGE6: {
            // 三棱柱（楔形体）的 5 个面
            // 底面三角: 0-1-2    顶面三角: 3-4-5
            //
            //       5
            //      /|\
            //     3 ─ 4
            //     | 2 |
            //     |/ \|
            //     0 ─ 1
            //
            faces.push_back({n[0], n[2], n[1]});          // 底面三角
            faces.push_back({n[3], n[4], n[5]});          // 顶面三角
            faces.push_back({n[0], n[1], n[4], n[3]});    // 侧面 1
            faces.push_back({n[1], n[2], n[5], n[4]});    // 侧面 2
            faces.push_back({n[0], n[3], n[5], n[2]});    // 侧面 3
            break;
        }

        case ElementType::PYRAMID5: {
            // 四棱锥的 5 个面
            // 底面: 0-1-2-3    顶点: 4
            //
            //         4
            //        /|\
            //       / | \
            //      3 ─┼─ 2
            //     /   |  /
            //    0 ─── 1
            //
            faces.push_back({n[0], n[3], n[2], n[1]});  // 底面四边形
            faces.push_back({n[0], n[1], n[4]});        // 侧面三角 1
            faces.push_back({n[1], n[2], n[4]});        // 侧面三角 2
            faces.push_back({n[2], n[3], n[4]});        // 侧面三角 3
            faces.push_back({n[3], n[0], n[4]});        // 侧面三角 4
            break;
        }

        default:
            // 不支持的 3D 类型（1D/2D 不应调用此方法）
            break;
    }

    return faces;
}

/**
 * @brief 计算三角面法线（归一化）
 *
 * 使用叉乘：normal = normalize((b - a) × (c - a))
 * 法线方向由右手定则决定（a→b→c 逆时针时法线朝上）。
 *
 * 退化三角形（面积为 0）返回 (0, 0, 1) 作为默认法线。
 */
glm::vec3 FEMeshConverter::computeNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    glm::vec3 edge1 = b - a;
    glm::vec3 edge2 = c - a;
    glm::vec3 n = glm::cross(edge1, edge2);

    float len = glm::length(n);
    if (len > 1e-8f) {
        return n / len;  // 归一化
    }

    // 退化三角形，返回默认法线
    return glm::vec3(0.0f, 0.0f, 1.0f);
}
