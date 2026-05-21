/**
 * @file FEResultRepository.cpp
 * @brief 结果仓库实现
 */

#include "FEResultRepository.h"

void FEResultRepository::clear()
{
    frames_.clear();
}

void FEResultRepository::addFrame(const FEResultFrame& frame)
{
    frames_.push_back(frame);
}

int FEResultRepository::frameCount() const
{
    return static_cast<int>(frames_.size());
}

const FEResultFrame* FEResultRepository::frame(int index) const
{
    if (index < 0 || index >= static_cast<int>(frames_.size()))
        return nullptr;
    return &frames_[index];
}

std::vector<std::string> FEResultRepository::resultTypeNames(int frameIndex) const
{
    std::vector<std::string> names;
    const FEResultFrame* f = frame(frameIndex);
    if (!f) return names;

    names.reserve(f->resultTypes.size());
    for (const auto& rt : f->resultTypes) {
        names.push_back(rt.name);
    }
    return names;
}

bool FEResultRepository::empty() const
{
    return frames_.empty();
}

FEResultRepository FEResultRepository::fromResultData(const FEResultData& data)
{
    FEResultRepository repo;
    for (const auto& sc : data.subcases) {
        FEResultFrame frame;
        frame.info.subcaseId = sc.id;
        frame.info.stepIndex = 0;
        frame.info.frameIndex = 0;
        frame.info.value = 0.0;
        frame.info.valueLabel = sc.name;
        frame.info.domain = FEResultDomain::Static;
        frame.resultTypes = sc.resultTypes;
        repo.addFrame(frame);
    }
    return repo;
}
