/**
 * @file FEParser.h
 * @brief 有限元模型文件解析器声明
 *
 * 无状态静态工具类，负责解析各种有限元文件格式：
 *   - ABAQUS INP（含 INCLUDE 展开）
 *   - Nastran BDF/FEM（固定/自由格式，CORD2R 坐标系变换）
 *   - Nastran OP2 二进制（几何 + 结果）
 *   - UNV（Dataset 2414/55 结果数据）
 */

#pragma once

#include <QString>
#include <functional>

#include "ferender_export.h"
#include "FEModel.h"
#include "FEResultData.h"
#include "FEResultRepository.h"

class FERENDER_EXPORT FEParser {
public:
    /** @brief 解析 ABAQUS INP 文件 */
    static bool parseAbaqusInp(const QString& filePath, FEModel& model,
                                const std::function<void(int)>& progress = nullptr);

    /** @brief 解析 Nastran BDF/FEM 文件 */
    static bool parseNastranBdf(const QString& filePath, FEModel& model,
                                 const std::function<void(int)>& progress = nullptr);

    /** @brief 解析 Nastran OP2 几何数据 */
    static bool parseNastranOp2(const QString& filePath, FEModel& model,
                                 const std::function<void(int)>& progress = nullptr);

    /** @brief 解析 Nastran OP2 结果数据（位移/应力） */
    static bool parseNastranOp2Results(const QString& filePath, FEResultData& results);

    /** @brief 解析 UNV 结果数据（Dataset 2414/55） */
    static bool parseUnvResults(const QString& filePath, FEResultData& results);

    /** @brief 解析 OP2 结果到 FEResultRepository（兼容包装） */
    static bool parseNastranOp2Results(const QString& filePath, FEResultRepository& repo);

    /** @brief 解析 UNV 结果到 FEResultRepository（保留帧域信息） */
    static bool parseUnvResults(const QString& filePath, FEResultRepository& repo);
};
