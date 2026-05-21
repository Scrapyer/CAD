/**
 * @file FEIsoSurface.cpp
 * @brief 等值面提取实现（Marching Tetrahedra）
 */

#include "FEIsoSurface.h"
#include <cmath>

// 六面体分解为 5 个四面体的节点索引（标准分解方案）
static const int hexToTet[5][4] = {
    {0, 1, 3, 4},
    {1, 2, 3, 6},
    {1, 4, 5, 6},
    {3, 4, 6, 7},
    {1, 3, 4, 6}
};

Mesh FEIsoSurface::extract(const FEModel& model,
                            const FEScalarField& field,
                            float isoValue) {
    Mesh output;

    if (field.location != FieldLocation::Node) return output;

    for (const auto& [elemId, elem] : model.elements) {
        if (elem.type == ElementType::TET4 && elem.nodeIds.size() == 4) {
            TetVertex tet[4];
            bool valid = true;
            for (int k = 0; k < 4; ++k) {
                int nid = elem.nodeIds[k];
                auto nit = model.nodes.find(nid);
                auto vit = field.values.find(nid);
                if (nit == model.nodes.end() || vit == field.values.end()) {
                    valid = false;
                    break;
                }
                tet[k].pos = nit->second.coords;
                tet[k].value = vit->second;
            }
            if (valid) marchTet(tet, isoValue, output);

        } else if (elem.type == ElementType::HEX8 && elem.nodeIds.size() == 8) {
            // 读取 8 个顶点
            TetVertex hexVerts[8];
            bool valid = true;
            for (int k = 0; k < 8; ++k) {
                int nid = elem.nodeIds[k];
                auto nit = model.nodes.find(nid);
                auto vit = field.values.find(nid);
                if (nit == model.nodes.end() || vit == field.values.end()) {
                    valid = false;
                    break;
                }
                hexVerts[k].pos = nit->second.coords;
                hexVerts[k].value = vit->second;
            }
            if (!valid) continue;

            // 分解为 5 个四面体
            for (int t = 0; t < 5; ++t) {
                TetVertex tet[4];
                for (int k = 0; k < 4; ++k)
                    tet[k] = hexVerts[hexToTet[t][k]];
                marchTet(tet, isoValue, output);
            }
        }
    }

    return output;
}

static glm::vec3 interpolate(const FEIsoSurface::TetVertex& a,
                              const FEIsoSurface::TetVertex& b,
                              float isoValue) {
    float denom = b.value - a.value;
    if (std::abs(denom) < 1e-10f)
        return (a.pos + b.pos) * 0.5f;
    float t = (isoValue - a.value) / denom;
    return a.pos + t * (b.pos - a.pos);
}

void FEIsoSurface::marchTet(const TetVertex tet[4], float isoValue, Mesh& output) {
    // 判断每个顶点在等值面哪一侧
    int mask = 0;
    for (int k = 0; k < 4; ++k) {
        if (tet[k].value >= isoValue)
            mask |= (1 << k);
    }

    // 全在一侧，无交面
    if (mask == 0 || mask == 0xF) return;

    // Marching Tetrahedra 的 16 种情况（利用对称性只需处理 8 种）
    // 边索引: (0,1)=0, (0,2)=1, (0,3)=2, (1,2)=3, (1,3)=4, (2,3)=5
    static const int edgeTable[6][2] = {
        {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}
    };

    // 每种 mask 对应的三角形（用边索引列出）
    // 参考标准 Marching Tetrahedra 查找表
    struct TriCase {
        int edges[6];  // 边索引列表，-1 结尾
    };

    glm::vec3 edgePoints[6];
    bool edgeValid[6] = {};

    // 计算所有需要的边交点
    for (int e = 0; e < 6; ++e) {
        int v0 = edgeTable[e][0], v1 = edgeTable[e][1];
        bool s0 = (mask & (1 << v0)) != 0;
        bool s1 = (mask & (1 << v1)) != 0;
        if (s0 != s1) {
            edgePoints[e] = interpolate(tet[v0], tet[v1], isoValue);
            edgeValid[e] = true;
        }
    }

    // 根据 mask 生成三角形
    // 1 个顶点在一侧：1 个三角形
    // 2 个顶点在一侧：2 个三角形（四边形分割）
    auto addTri = [&](int e0, int e1, int e2) {
        if (edgeValid[e0] && edgeValid[e1] && edgeValid[e2])
            output.addFlatTriangle(edgePoints[e0], edgePoints[e1], edgePoints[e2]);
    };

    switch (mask) {
        case 0x1: // 只有 v0 在内
            addTri(0, 1, 2);
            break;
        case 0xE: // v1,v2,v3 在内（v0 外）
            addTri(0, 2, 1);
            break;
        case 0x2: // 只有 v1 在内
            addTri(0, 4, 3);
            break;
        case 0xD: // v0,v2,v3 在内
            addTri(0, 3, 4);
            break;
        case 0x4: // 只有 v2 在内
            addTri(1, 3, 5);
            break;
        case 0xB: // v0,v1,v3 在内
            addTri(1, 5, 3);
            break;
        case 0x8: // 只有 v3 在内
            addTri(2, 5, 4);
            break;
        case 0x7: // v0,v1,v2 在内
            addTri(2, 4, 5);
            break;
        case 0x3: // v0,v1 在内
            addTri(1, 2, 4);
            addTri(1, 4, 3);
            break;
        case 0xC: // v2,v3 在内
            addTri(1, 4, 2);
            addTri(1, 3, 4);
            break;
        case 0x5: // v0,v2 在内
            addTri(0, 5, 2);
            addTri(0, 3, 5);
            break;
        case 0xA: // v1,v3 在内
            addTri(0, 2, 5);
            addTri(0, 5, 3);
            break;
        case 0x6: // v1,v2 在内
            addTri(0, 4, 5);
            addTri(0, 5, 1);
            break;
        case 0x9: // v0,v3 在内
            addTri(0, 5, 4);
            addTri(0, 1, 5);
            break;
        default:
            break;
    }
}
