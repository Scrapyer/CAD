/**
 * @file FEProbe.h
 * @brief 结果场探针：点值查询、热点排序、路径采样
 */

#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>

#include "FEField.h"
#include "FEModel.h"
#include "ferender_export.h"

struct FERENDER_EXPORT FEProbeValue {
    bool valid = false;
    int entityId = -1;
    FieldLocation location = FieldLocation::Node;
    float value = 0.0f;
};

struct FERENDER_EXPORT FEPathSample {
    float distance = 0.0f;
    glm::vec3 position{0.0f};
    FEProbeValue value;
};

class FERENDER_EXPORT FEProbe {
public:
    /** @brief 查询指定节点/单元 ID 处的标量值 */
    static FEProbeValue valueAtEntity(const FEScalarField& field, int entityId);

    /** @brief 返回 field 中最大或最小的 N 个热点 */
    static std::vector<FEProbeValue> topHotspots(const FEScalarField& field,
                                                  int count,
                                                  bool descending = true);

    /** @brief 沿节点路径采样标量值和累计弧长 */
    static std::vector<FEPathSample> sampleNodePath(const FEModel& model,
                                                     const FEScalarField& field,
                                                     const std::vector<int>& nodeIds);

    /** @brief 将路径采样数据导出为 CSV */
    static bool writePathSamplesCsv(const std::string& filePath,
                                     const std::vector<FEPathSample>& samples);

    /** @brief 将标量场导出为 CSV（ID, Value） */
    static bool writeScalarFieldCsv(const std::string& filePath,
                                     const FEScalarField& field);
};
