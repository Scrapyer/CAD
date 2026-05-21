/**
 * @file FEMeshConverter.h
 * @brief 有限元模型 → 可渲染数据包的转换器
 *
 * 这是 FEM 数据层与渲染层之间的桥梁。
 * 负责将 FEModel 中的单元拓扑转换为 GLWidget 可以渲染的三角网格，
 * 同时生成反向映射表以支持拾取（Picking）功能。
 *
 * ┌──────────────────────────────────────────────────────────┐
 * │                   转换流程                              │
 * │                                                          │
 * │  FEModel (节点+单元)                                    │
 * │       ↓                                                  │
 * │  单元分类 (1D / 2D / 3D)                                │
 * │       ↓                                                  │
 * │  ┌─────────────┬──────────────┬───────────────┐         │
 * │  │ 1D: 生成    │ 2D: 直接     │ 3D: 提取外    │         │
 * │  │ 圆柱/线段   │ 三角化       │ 表面再三角化  │         │
 * │  └─────────────┴──────────────┴───────────────┘         │
 * │       ↓                                                  │
 * │  FERenderData                                            │
 * │  ├── Mesh (顶点 + 法线 [+ 颜色] + 索引)                │
 * │  ├── triangleToElement[] (三角形→单元映射)              │
 * │  ├── triangleToFace[]    (三角形→面索引映射)            │
 * │  └── vertexToNode[]      (顶点→节点映射)               │
 * │       ↓                                                  │
 * │  GLWidget 渲染 + 拾取                                   │
 * └──────────────────────────────────────────────────────────┘
 *
 * 关键算法：
 *
 * 1. 面提取（Surface Extraction）
 *    3D 实体单元的内部面不需要渲染，只渲染外表面。
 *    方法：遍历所有单元的所有面，找到只被一个单元引用的面
 *    （即外表面面）。使用面的节点 ID 排序后作为 key 去重。
 *
 * 2. 单元三角化（Tessellation）
 *    - TRI3 → 1 个三角形
 *    - QUAD4 → 2 个三角形 (0,1,2) + (0,2,3)
 *    - TET4 → 4 个三角面
 *    - HEX8 → 6 个四边形面 → 12 个三角形
 *    - 高阶单元：先用角节点做线性近似，后续可细分
 *
 * 3. 反向映射表生成
 *    在三角化的同时，记录每个渲染三角形/顶点与 FEM 实体的对应关系：
 *    - triangleToElement[i] = 第 i 个三角形来自哪个单元
 *    - triangleToFace[i]    = 该三角形属于单元的第几个面
 *    - vertexToNode[i]      = 第 i 个渲染顶点对应哪个 FEM 节点
 *    这些映射表在拾取时用于从渲染层反查 FEM 层信息。
 *
 * 4. 云图颜色映射
 *    如果提供了标量场，将每个顶点的标量值通过 ColorMap
 *    映射为 RGB 颜色，存入 Mesh 的顶点颜色属性。
 *
 * 设计说明：
 *   - 所有方法都是静态的，不持有状态（纯转换工具）
 *   - 主要接口返回 FERenderData（Mesh + 映射表），支持渲染与拾取
 *   - 提供 toMeshOnly 系列方法用于不需要拾取的场景（如线框、变形预览）
 *   - 后续可扩展：支持高阶单元细分、截面显示、爆炸视图等
 */

#pragma once

#include "FEModel.h"
#include "FERenderData.h"
#include "Geometry.h"
#include <functional>

#include "ferender_export.h"

using ProgressCallback = std::function<void(int percent)>;

class FERENDER_EXPORT FEMeshConverter {
public:
    // ════════════════════════════════════════════════════════════
    // 主要接口：返回 FERenderData（Mesh + 反向映射表）
    // 用于需要拾取功能的场景
    // ════════════════════════════════════════════════════════════

    /**
     * @brief 将整个 FEM 模型转换为可渲染数据包
     * @param model FEM 模型
     * @return FERenderData（三角网格 + 反向映射表）
     *
     * 自动处理：
     *   - 2D 单元直接三角化
     *   - 3D 单元提取外表面后三角化
     *   - 1D 单元暂时跳过（后续可生成管状几何）
     *
     * 同时填充 triangleToElement、triangleToFace、vertexToNode 映射表，
     * 使得拾取时可以从渲染三角形/顶点反查 FEM 单元/节点。
     */
    static FERenderData toRenderData(const FEModel& model, const ProgressCallback& progress = nullptr);

    /**
     * @brief 将指定的单元集转换为可渲染数据包
     * @param model FEM 模型
     * @param elementIds 要转换的单元 ID 列表
     * @param progress 进度回调（0-100）
     * @return FERenderData（三角网格 + 反向映射表）
     *
     * 用于：分部件显示、选中单元高亮等。
     * 映射表只包含指定单元的信息。
     */
    static FERenderData toRenderData(const FEModel& model, const std::vector<int>& elementIds, const ProgressCallback& progress = nullptr);

    /**
     * @brief 将 FEM 模型转换为带云图颜色的渲染数据包
     * @param model FEM 模型
     * @param field 要显示的标量场
     * @param colorMap 色谱映射器
     * @param minVal 色谱最小值（可手动指定范围）
     * @param maxVal 色谱最大值
     * @return FERenderData（带颜色的 Mesh + 反向映射表）
     *
     * 注意：此方法生成的 Mesh 顶点格式为 [pos(3) + normal(3) + color(3)]
     * 需要 GLWidget 支持额外的颜色属性（后续扩展）。
     */
    static FERenderData toColoredRenderData(const FEModel& model,
                                             const FEScalarField& field,
                                             const ColorMap& colorMap,
                                             float minVal, float maxVal);

    // ════════════════════════════════════════════════════════════
    // 辅助接口：仅返回 Mesh（无反向映射表）
    // 用于不需要拾取的场景，避免不必要的映射表开销
    // ════════════════════════════════════════════════════════════

    /**
     * @brief 生成变形后的网格（仅 Mesh，无映射表）
     * @param model FEM 模型（原始坐标）
     * @param displacement 位移矢量场
     * @param scale 变形放大系数（通常 > 1 以便观察微小变形）
     * @return 变形后的 Mesh
     *
     * 新坐标 = 原始坐标 + displacement × scale
     * 注意：变形后可对结果再调用 toRenderData 获取带映射表的版本。
     */
    static Mesh toDeformedMesh(const FEModel& model,
                                const FEVectorField& displacement,
                                float scale = 1.0f);

    /**
     * @brief 生成线框网格（仅 Mesh，用 GL_LINES 渲染）
     * @param model FEM 模型
     * @return 仅包含边的 Mesh
     *
     * 线框模式下不需要拾取三角形，所以不生成映射表。
     */
    static Mesh toWireframeMesh(const FEModel& model);

private:
    // ── 内部辅助方法 ──

    /**
     * @brief 将 2D 单元三角化并追加到渲染数据包
     * @param renderData 输出渲染数据包（Mesh + 映射表同时更新）
     * @param elem       单元
     * @param model      模型（用于查询节点坐标）
     *
     * 在三角化的同时填充：
     *   - triangleToElement: 每个生成的三角形 → elem.id
     *   - triangleToFace:    面索引为 0（2D 单元本身就是一个面）
     *   - vertexToNode:      每个顶点 → 对应的 FEM 节点 ID
     */
    static void tessellate2D(FERenderData& renderData, const FEElement& elem, const FEModel& model);

    /**
     * @brief 将 3D 单元的一个面三角化并追加到渲染数据包
     * @param renderData 输出渲染数据包
     * @param faceNodes  面的节点 ID 列表（3 或 4 个节点）
     * @param elemId     该面所属的单元 ID
     * @param faceIndex  该面在单元中的面序号
     * @param model      模型（用于查询节点坐标）
     *
     * 在三角化的同时填充映射表，记录该面产生的三角形属于哪个单元的哪个面。
     */
    static void tessellateFace(FERenderData& renderData,
                                const std::vector<int>& faceNodes,
                                int elemId, int faceIndex,
                                const FEModel& model);

    /**
     * @brief 提取 3D 单元的所有面
     * @param elem 单元
     * @return 面列表，每个面是一组节点 ID
     *
     * 例如 HEX8 返回 6 个面，每面 4 个节点 ID。
     */
    static std::vector<std::vector<int>> extractFaces(const FEElement& elem);

    /**
     * @brief 计算三角面法线
     * @param a, b, c 三角形三个顶点坐标
     * @return 归一化法线向量
     */
    static glm::vec3 computeNormal(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
};
