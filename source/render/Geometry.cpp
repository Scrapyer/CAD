/**
 * @file Geometry.cpp
 * @brief 网格数据结构方法 & 基础几何体生成器实现
 */

#include "Geometry.h"

#include <cmath>
#include <glm/gtc/constants.hpp>  // glm::pi

// ============================================================
// Mesh 方法实现
// ============================================================

void Mesh::addVertex(glm::vec3 p, glm::vec3 n) {
    // 将位置和法线分量依次追加到顶点数组
    // 存储格式：[px, py, pz, nx, ny, nz]
    vertices.insert(vertices.end(), {p.x, p.y, p.z, n.x, n.y, n.z});
}

void Mesh::addTriangle(unsigned int a, unsigned int b, unsigned int c) {
    // 追加三个顶点索引，定义一个三角面
    indices.insert(indices.end(), {a, b, c});
}

void Mesh::addFlatTriangle(glm::vec3 a, glm::vec3 b, glm::vec3 c) {
    // 计算面法线：两条边的叉积，然后归一化
    // cross(b-a, c-a) 得到垂直于三角面的向量，方向由右手定则决定
    glm::vec3 n = glm::normalize(glm::cross(b - a, c - a));

    // 当前顶点数量 / 6 = 已有顶点个数（每顶点 6 个 float）
    unsigned int base = static_cast<unsigned int>(vertices.size() / 6);

    // 三个顶点使用相同的面法线（flat shading）
    addVertex(a, n);
    addVertex(b, n);
    addVertex(c, n);
    addTriangle(base, base + 1, base + 2);
}

void Mesh::addFlatQuad(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d) {
    // 四边形拆分为两个三角形：
    //   三角形1: a → b → c
    //   三角形2: a → c → d
    addFlatTriangle(a, b, c);
    addFlatTriangle(a, c, d);
}

// ============================================================
// 几何体生成器
// ============================================================

namespace Geometry {

// ── 正方体 ──
// 8 个顶点，6 个面（每面 2 个三角形 = 12 个三角形）
Mesh cube() {
    Mesh m;

    // 定义正方体的 8 个顶点坐标（边长 1.0，中心在原点）
    //
    //     3───2          Y
    //    /|  /|          ↑
    //   0───1 |          │
    //   | 7─|─6          └──→ X
    //   |/  |/          /
    //   4───5          Z
    glm::vec3 v[8] = {
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},  // v0, v1（前面下边）
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},  // v2, v3（前面上边）
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},  // v4, v5（后面下边）
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},  // v6, v7（后面上边）
    };

    // 6 个面，每个面按逆时针顺序指定 4 个顶点
    m.addFlatQuad(v[0], v[1], v[2], v[3]); // 前面 (+Z)
    m.addFlatQuad(v[5], v[4], v[7], v[6]); // 后面 (-Z)
    m.addFlatQuad(v[1], v[5], v[6], v[2]); // 右面 (+X)
    m.addFlatQuad(v[4], v[0], v[3], v[7]); // 左面 (-X)
    m.addFlatQuad(v[3], v[2], v[6], v[7]); // 顶面 (+Y)
    m.addFlatQuad(v[4], v[5], v[1], v[0]); // 底面 (-Y)
    return m;
}

// ── 三棱锥（正四面体）──
// 4 个顶点，4 个三角面
Mesh tetrahedron() {
    Mesh m;

    // 正四面体的 4 个顶点（对称地分布在正方体的对角顶点上）
    // 缩放因子 s 控制大小
    float s = 0.6f;
    glm::vec3 v[4] = {
        { 1,  1,  1}, { 1, -1, -1},
        {-1,  1, -1}, {-1, -1,  1},
    };
    for (auto& p : v) p *= s;  // 缩放到合适大小

    // 4 个三角面
    m.addFlatTriangle(v[0], v[1], v[2]);
    m.addFlatTriangle(v[0], v[2], v[3]);
    m.addFlatTriangle(v[0], v[3], v[1]);
    m.addFlatTriangle(v[1], v[3], v[2]);
    return m;
}

// ── 三棱柱 ──
// 等边三角形截面，沿 Y 轴拉伸
// 6 个顶点，2 个三角面（上下底）+ 3 个四边形（侧面）= 8 个三角形
Mesh triangularPrism() {
    Mesh m;
    float h = 0.5f;  // 半高

    // XZ 平面上的等边三角形顶点（重心在原点附近）
    glm::vec3 t0(0.0f, 0.0f, 0.5f);
    glm::vec3 t1(0.433f, 0.0f, -0.25f);    // ≈ sqrt(3)/4
    glm::vec3 t2(-0.433f, 0.0f, -0.25f);

    // 下底面顶点（Y = -h）和上底面顶点（Y = +h）
    glm::vec3 b0 = t0 + glm::vec3(0, -h, 0);
    glm::vec3 b1 = t1 + glm::vec3(0, -h, 0);
    glm::vec3 b2 = t2 + glm::vec3(0, -h, 0);
    glm::vec3 u0 = t0 + glm::vec3(0,  h, 0);
    glm::vec3 u1 = t1 + glm::vec3(0,  h, 0);
    glm::vec3 u2 = t2 + glm::vec3(0,  h, 0);

    // 上下三角面（注意法线朝向：逆时针为正面）
    m.addFlatTriangle(u0, u1, u2);  // 上底面（法线朝上）
    m.addFlatTriangle(b2, b1, b0);  // 下底面（法线朝下，顶点顺序反转）

    // 三个侧面（四边形）
    m.addFlatQuad(b0, b1, u1, u0);
    m.addFlatQuad(b1, b2, u2, u1);
    m.addFlatQuad(b2, b0, u0, u2);
    return m;
}

// ── 圆柱 ──
// 参数化生成：将圆周等分为 segments 段
// 侧面由 segments 个四边形组成，上下各 segments 个三角形封盖
Mesh cylinder(int segments) {
    Mesh m;
    float h = 0.5f;   // 半高
    float r = 0.4f;    // 半径

    // ── 侧面 ──
    // 沿圆周遍历，每两个相邻角度之间构建一个四边形
    for (int i = 0; i < segments; ++i) {
        float a0 = glm::radians(360.0f * i / segments);        // 当前角度
        float a1 = glm::radians(360.0f * (i + 1) / segments);  // 下一个角度

        // 四边形的四个顶点：下边两个 + 上边两个
        glm::vec3 p0(r * cos(a0), -h, r * sin(a0));  // 下-左
        glm::vec3 p1(r * cos(a1), -h, r * sin(a1));  // 下-右
        glm::vec3 p2(r * cos(a1),  h, r * sin(a1));  // 上-右
        glm::vec3 p3(r * cos(a0),  h, r * sin(a0));  // 上-左
        m.addFlatQuad(p0, p1, p2, p3);
    }

    // ── 上下盖 ──
    // 以中心点为扇形顶点，连接圆周上相邻两点构成三角形
    glm::vec3 topCenter(0, h, 0);
    glm::vec3 botCenter(0, -h, 0);
    for (int i = 0; i < segments; ++i) {
        float a0 = glm::radians(360.0f * i / segments);
        float a1 = glm::radians(360.0f * (i + 1) / segments);

        // 上盖（法线朝上）
        m.addFlatTriangle(topCenter,
                          {r * cos(a0), h, r * sin(a0)},
                          {r * cos(a1), h, r * sin(a1)});
        // 下盖（法线朝下，顶点顺序反转）
        m.addFlatTriangle(botCenter,
                          {r * cos(a1), -h, r * sin(a1)},
                          {r * cos(a0), -h, r * sin(a0)});
    }
    return m;
}

// ── 圆锥 ──
// 顶点在上方 (0, h, 0)，底面圆在 Y = -h 平面
Mesh cone(int segments) {
    Mesh m;
    float h = 0.5f;   // 半高
    float r = 0.4f;    // 底面半径

    glm::vec3 apex(0, h, 0);        // 锥尖
    glm::vec3 botCenter(0, -h, 0);  // 底面中心

    for (int i = 0; i < segments; ++i) {
        float a0 = glm::radians(360.0f * i / segments);
        float a1 = glm::radians(360.0f * (i + 1) / segments);
        glm::vec3 p0(r * cos(a0), -h, r * sin(a0));
        glm::vec3 p1(r * cos(a1), -h, r * sin(a1));

        // 侧面三角形：锥尖 → 底面两点
        m.addFlatTriangle(apex, p1, p0);
        // 底面三角形：中心 → 底面两点（反向以使法线朝下）
        m.addFlatTriangle(botCenter, p0, p1);
    }
    return m;
}

// ── 球体 ──
// 使用 UV 球体参数化：纬线(rings) × 经线(sectors)
// 采用 smooth shading（法线 = 归一化顶点位置），表面光滑
Mesh sphere(int rings, int sectors) {
    Mesh m;
    float r = 0.5f;  // 半径

    // ── 生成顶点网格 ──
    // phi:   纬度角，从北极(0) 到南极(PI)
    // theta: 经度角，绕 Y 轴一周(0 ~ 2PI)
    for (int i = 0; i <= rings; ++i) {
        float phi = glm::pi<float>() * i / rings;
        for (int j = 0; j <= sectors; ++j) {
            float theta = 2.0f * glm::pi<float>() * j / sectors;

            // 球坐标 → 笛卡尔坐标（同时作为法线方向）
            glm::vec3 n(sin(phi) * cos(theta),   // X
                        cos(phi),                  // Y（北极朝上）
                        sin(phi) * sin(theta));    // Z
            m.addVertex(n * r, n);  // 位置 = 法线 × 半径
        }
    }

    // ── 生成索引 ──
    // 每个网格单元由两个三角形组成（四边形分割）
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < sectors; ++j) {
            unsigned int cur  = i * (sectors + 1) + j;        // 当前行当前列
            unsigned int next = cur + sectors + 1;             // 下一行同列

            // 上三角形和下三角形
            m.addTriangle(cur, next, cur + 1);
            m.addTriangle(cur + 1, next, next + 1);
        }
    }
    return m;
}

// ── 圆环（Torus）──
// 参数化曲面：大圆半径 R，管截面半径 rr
// ringSegs: 大圆方向分段数  tubeSegs: 管截面方向分段数
Mesh torus(int ringSegs, int tubeSegs) {
    Mesh m;
    float R = 0.35f;   // 大圆半径（圆环中心线到原点的距离）
    float rr = 0.15f;  // 管截面半径

    // ── 生成顶点 ──
    // u: 大圆方向角度 (0 ~ 2PI)
    // v: 管截面方向角度 (0 ~ 2PI)
    for (int i = 0; i <= ringSegs; ++i) {
        float u = 2.0f * glm::pi<float>() * i / ringSegs;
        for (int j = 0; j <= tubeSegs; ++j) {
            float v = 2.0f * glm::pi<float>() * j / tubeSegs;

            // 大圆上对应点的位置（管截面的中心）
            glm::vec3 center(R * cos(u), 0, R * sin(u));

            // 顶点位置：在中心点基础上，沿管截面方向偏移 rr
            glm::vec3 p(
                (R + rr * cos(v)) * cos(u),   // X
                rr * sin(v),                    // Y（管截面上下方向）
                (R + rr * cos(v)) * sin(u)     // Z
            );

            // 法线 = 顶点位置 - 管截面中心（指向管表面外侧）
            glm::vec3 n = glm::normalize(p - center);
            m.addVertex(p, n);
        }
    }

    // ── 生成索引 ──
    for (int i = 0; i < ringSegs; ++i) {
        for (int j = 0; j < tubeSegs; ++j) {
            unsigned int cur  = i * (tubeSegs + 1) + j;
            unsigned int next = cur + tubeSegs + 1;
            m.addTriangle(cur, next, cur + 1);
            m.addTriangle(cur + 1, next, next + 1);
        }
    }
    return m;
}

} // namespace Geometry
