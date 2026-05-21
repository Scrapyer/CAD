/**
 * @file FEProbe.cpp
 * @brief 结果场探针实现
 */

#include "FEProbe.h"

#include <algorithm>
#include <fstream>

FEProbeValue FEProbe::valueAtEntity(const FEScalarField& field, int entityId)
{
    FEProbeValue result;
    auto it = field.values.find(entityId);
    if (it == field.values.end())
        return result;

    result.valid = true;
    result.entityId = entityId;
    result.location = field.location;
    result.value = it->second;
    return result;
}

std::vector<FEProbeValue> FEProbe::topHotspots(const FEScalarField& field,
                                                int count,
                                                bool descending)
{
    if (count <= 0 || field.values.empty())
        return {};

    std::vector<std::pair<int, float>> entries(field.values.begin(), field.values.end());

    if (descending) {
        std::partial_sort(entries.begin(),
                          entries.begin() + std::min(count, static_cast<int>(entries.size())),
                          entries.end(),
                          [](const auto& a, const auto& b) { return a.second > b.second; });
    } else {
        std::partial_sort(entries.begin(),
                          entries.begin() + std::min(count, static_cast<int>(entries.size())),
                          entries.end(),
                          [](const auto& a, const auto& b) { return a.second < b.second; });
    }

    int n = std::min(count, static_cast<int>(entries.size()));
    std::vector<FEProbeValue> result;
    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        FEProbeValue pv;
        pv.valid = true;
        pv.entityId = entries[i].first;
        pv.location = field.location;
        pv.value = entries[i].second;
        result.push_back(pv);
    }
    return result;
}

std::vector<FEPathSample> FEProbe::sampleNodePath(const FEModel& model,
                                                    const FEScalarField& field,
                                                    const std::vector<int>& nodeIds)
{
    if (nodeIds.empty())
        return {};

    std::vector<FEPathSample> samples;
    samples.reserve(nodeIds.size());
    float cumDist = 0.0f;
    glm::vec3 prevPos{0.0f};

    for (size_t i = 0; i < nodeIds.size(); ++i) {
        int nid = nodeIds[i];
        auto nit = model.nodes.find(nid);
        if (nit == model.nodes.end())
            continue;

        glm::vec3 pos = nit->second.coords;
        if (!samples.empty())
            cumDist += glm::length(pos - prevPos);
        prevPos = pos;

        FEPathSample s;
        s.distance = cumDist;
        s.position = pos;
        s.value = valueAtEntity(field, nid);
        samples.push_back(s);
    }
    return samples;
}

bool FEProbe::writePathSamplesCsv(const std::string& filePath,
                                   const std::vector<FEPathSample>& samples)
{
    std::ofstream ofs(filePath);
    if (!ofs.is_open())
        return false;

    ofs << "Distance,X,Y,Z,NodeID,Value,Valid\n";
    for (const auto& s : samples) {
        ofs << s.distance << ","
            << s.position.x << "," << s.position.y << "," << s.position.z << ","
            << s.value.entityId << "," << s.value.value << ","
            << (s.value.valid ? 1 : 0) << "\n";
    }
    return ofs.good();
}

bool FEProbe::writeScalarFieldCsv(const std::string& filePath,
                                   const FEScalarField& field)
{
    std::ofstream ofs(filePath);
    if (!ofs.is_open())
        return false;

    std::string idHeader = (field.location == FieldLocation::Node) ? "NodeID" : "ElementID";
    ofs << idHeader << ",Value\n";

    std::vector<std::pair<int, float>> sorted(field.values.begin(), field.values.end());
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [id, val] : sorted)
        ofs << id << "," << val << "\n";

    return ofs.good();
}
