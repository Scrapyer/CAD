/**
 * @file FEResultMapper.cpp
 * @brief 结果场到渲染顶点数据的映射工具实现
 */

#include "FEResultMapper.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <limits>

FEMappedScalars FEResultMapper::mapScalarToVertices(const FEScalarField& field,
                                                     const FERenderData& renderData,
                                                     const FEModel& model) {
    FEMappedScalars mapped;
    mapped.location = field.location;

    const int vertCount = renderData.vertexCount();
    mapped.scalars.assign(vertCount, 0.0f);

    if (field.values.empty() || vertCount <= 0) {
        return mapped;
    }

    if (field.location == FieldLocation::Element) {
        // 只在已渲染单元中计算极值（排除无几何的弹簧/连接单元）
        std::unordered_set<int> renderedElems(renderData.triangleToElement.begin(),
                                               renderData.triangleToElement.end());
        // 也包含只有边线的 1D 单元
        for (int eid : renderData.mesh.elemEdgeToElement)
            renderedElems.insert(eid);

        mapped.minValue =  std::numeric_limits<float>::max();
        mapped.maxValue = -std::numeric_limits<float>::max();
        mapped.minId = -1;
        mapped.maxId = -1;
        for (const auto& [id, val] : field.values) {
            if (!renderedElems.count(id)) continue;
            if (val < mapped.minValue) { mapped.minValue = val; mapped.minId = id; }
            if (val > mapped.maxValue) { mapped.maxValue = val; mapped.maxId = id; }
        }
        if (mapped.minId == -1) {
            mapped.minValue = 0.0f;
            mapped.maxValue = 0.0f;
        }

        const int triCount = static_cast<int>(renderData.triangleToElement.size());
        for (int t = 0; t < triCount; ++t) {
            int elemId = renderData.triangleToElement[t];
            auto valueIt = field.values.find(elemId);
            if (valueIt == field.values.end()) {
                continue;
            }

            for (int k = 0; k < 3; ++k) {
                int indexOffset = t * 3 + k;
                if (indexOffset >= static_cast<int>(renderData.mesh.indices.size())) {
                    continue;
                }

                unsigned int vertexIndex = renderData.mesh.indices[indexOffset];
                if (vertexIndex < mapped.scalars.size()) {
                    mapped.scalars[vertexIndex] = valueIt->second;
                }
            }
        }
        return mapped;
    }

    field.computeRangeWithIds(mapped.minValue, mapped.maxValue, mapped.minId, mapped.maxId);

    int directHits = 0;
    for (const auto& [nodeId, node] : model.nodes) {
        (void)node;
        if (field.values.count(nodeId) > 0) {
            ++directHits;
        }
    }

    const bool useDirect = model.nodes.empty()
        || directHits > static_cast<int>(model.nodes.size()) / 2;

    std::unordered_map<int, float> nodeValueMap;
    if (useDirect) {
        nodeValueMap = field.values;
    } else {
        std::vector<int> modelNodeIds;
        std::vector<int> resultNodeIds;
        modelNodeIds.reserve(model.nodes.size());
        resultNodeIds.reserve(field.values.size());

        for (const auto& [nodeId, node] : model.nodes) {
            (void)node;
            modelNodeIds.push_back(nodeId);
        }
        for (const auto& [nodeId, value] : field.values) {
            (void)value;
            resultNodeIds.push_back(nodeId);
        }

        std::sort(modelNodeIds.begin(), modelNodeIds.end());
        std::sort(resultNodeIds.begin(), resultNodeIds.end());

        int mappedCount = std::min(static_cast<int>(modelNodeIds.size()),
                                   static_cast<int>(resultNodeIds.size()));
        for (int i = 0; i < mappedCount; ++i) {
            auto valueIt = field.values.find(resultNodeIds[i]);
            if (valueIt != field.values.end()) {
                nodeValueMap[modelNodeIds[i]] = valueIt->second;
            }
        }

        auto remapId = [&](int resultId) {
            auto resultIt = std::lower_bound(resultNodeIds.begin(), resultNodeIds.end(), resultId);
            if (resultIt == resultNodeIds.end() || *resultIt != resultId) {
                return resultId;
            }

            int index = static_cast<int>(std::distance(resultNodeIds.begin(), resultIt));
            if (index >= 0 && index < static_cast<int>(modelNodeIds.size())) {
                return modelNodeIds[index];
            }
            return resultId;
        };

        mapped.minId = remapId(mapped.minId);
        mapped.maxId = remapId(mapped.maxId);
    }

    const int mappedVertexCount = std::min(vertCount, static_cast<int>(renderData.vertexToNode.size()));
    for (int i = 0; i < mappedVertexCount; ++i) {
        int nodeId = renderData.vertexToNode[i];
        if (nodeId < 0) {
            continue;
        }

        auto valueIt = nodeValueMap.find(nodeId);
        if (valueIt != nodeValueMap.end()) {
            mapped.scalars[i] = valueIt->second;
        }
    }

    return mapped;
}
