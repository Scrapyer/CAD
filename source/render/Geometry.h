/**
 * @file Geometry.h
 * @brief 网格数据结构与基础几何体生成器声明
 *
 * Mesh 结构体存储顶点数据和索引数据，每个顶点包含位置(3) + 法线(3)。
 * Geometry 命名空间提供 7 种基础几何体的生成函数。
 */

#pragma once

#include <vector>
#include <glm/glm.hpp>

/**
 * @struct Mesh
 * @brief 三角网格数据结构
 *
 * 顶点数据以交错格式存储：[px, py, pz, nx, ny, nz, ...]
 * 每 6 个 float 为一个顶点（位置 + 法线）。
 * 索引数据以三角形为单位：每 3 个索引构成一个三角面。
 */
#include "ferender_export.h"

struct FERENDER_EXPORT Mesh {
    std::vector<float> vertices;         // 顶点数据（位置 + 法线，交错存储）
    std::vector<unsigned int> indices;   // 索引数据（每 3 个索引一个三角形）
    std::vector<float> edgeVertices;     // 边线顶点数据（仅位置，用于 GL_LINES）
    std::vector<unsigned int> edgeIndices; // 边线索引数据（每 2 个索引一条线段）

    // ── 单元完整边线（用于选中高亮，包含内部边）──
    std::vector<float> elemEdgeVertices;   // 每条边 2 顶点 × 3 float
    std::vector<int> elemEdgeToElement;    // 每条边对应的单元 ID
    std::vector<std::pair<int,int>> elemEdgeNodeIds;  // 每条边的 FEM 节点 ID 对（已排序，min,max）

    /** @brief 添加一个顶点（位置 + 法线） */
    void addVertex(glm::vec3 p, glm::vec3 n);

    /** @brief 添加一个三角形（通过三个顶点索引） */
    void addTriangle(unsigned int a, unsigned int b, unsigned int c);

    /**
     * @brief 添加一个 flat-shading 三角形
     *
     * 自动计算面法线（通过两条边的叉积），三个顶点共享同一法线，
     * 实现平面着色效果（每个面颜色均匀，棱角分明）。
     */
    void addFlatTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c);

    /**
     * @brief 添加一个 flat-shading 四边形
     *
     * 将四边形分割为两个三角形 (a,b,c) 和 (a,c,d)。
     */
    void addFlatQuad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d);
};

/**
 * @namespace Geometry
 * @brief 基础几何体网格生成器
 *
 * 所有函数返回以原点为中心的标准大小网格。
 */
namespace Geometry {
    FERENDER_EXPORT Mesh cube();                                          // 正方体
    FERENDER_EXPORT Mesh tetrahedron();                                   // 三棱锥（正四面体）
    FERENDER_EXPORT Mesh triangularPrism();                               // 三棱柱
    FERENDER_EXPORT Mesh cylinder(int segments = 36);                     // 圆柱（可指定侧面分段数）
    FERENDER_EXPORT Mesh cone(int segments = 36);                         // 圆锥（可指定侧面分段数）
    FERENDER_EXPORT Mesh sphere(int rings = 24, int sectors = 36);        // 球体（可指定纬线/经线数）
    FERENDER_EXPORT Mesh torus(int ringSegs = 36, int tubeSegs = 24);     // 圆环（可指定环/管分段数）
}
