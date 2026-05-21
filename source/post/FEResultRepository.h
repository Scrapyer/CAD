/**
 * @file FEResultRepository.h
 * @brief 结果仓库与帧模型
 *
 * 支持 step/frame/time/frequency/mode 结构的后处理结果管理。
 * 在旧 FEResultData (subcase/type/component) 基础上增加帧时间轴，
 * 为动画、比较和结果曲线打底。
 *
 * 层级：FEResultRepository → FEResultFrame → FEResultType → FEResultComponent
 */

#pragma once

#include "FEResultData.h"
#include "ferender_export.h"

#include <string>
#include <vector>

/**
 * @enum FEResultDomain
 * @brief 结果帧所属的分析域
 */
enum class FERENDER_EXPORT FEResultDomain {
    Static,       // 静力分析
    Time,         // 瞬态/时间域
    Frequency,    // 频率域
    Mode          // 模态分析
};

/**
 * @struct FEResultFrameInfo
 * @brief 帧元数据：标识一个结果帧在时间/模态轴上的位置
 */
struct FERENDER_EXPORT FEResultFrameInfo {
    int subcaseId = 0;                              // 原始工况 ID
    int stepIndex = 0;                              // 步序号（一个工况可含多步）
    int frameIndex = 0;                             // 帧序号（一步内的时间/频率序号）
    double value = 0.0;                             // 轴值（时间/频率/模态特征值）
    std::string valueLabel;                         // 轴值标签（如 "t=0.5s", "100 Hz"）
    FEResultDomain domain = FEResultDomain::Static; // 分析域
};

/**
 * @struct FEResultFrame
 * @brief 一个结果帧，包含帧元数据和多种结果类型
 */
struct FERENDER_EXPORT FEResultFrame {
    FEResultFrameInfo info;                  // 帧元数据
    std::vector<FEResultType> resultTypes;   // 该帧包含的结果类型列表
};

/**
 * @class FEResultRepository
 * @brief 结果仓库：管理多帧结果数据
 *
 * 使用示例：
 * @code
 * FEResultRepository repo;
 * FEResultFrame frame;
 * frame.info.subcaseId = 1;
 * frame.info.domain = FEResultDomain::Static;
 * frame.resultTypes = subcaseData.resultTypes;
 * repo.addFrame(frame);
 *
 * // 查询
 * int n = repo.frameCount();
 * const FEResultFrame* f = repo.frame(0);
 * auto names = repo.resultTypeNames(0);
 * @endcode
 */
class FERENDER_EXPORT FEResultRepository {
public:
    /** @brief 清空所有帧数据 */
    void clear();

    /** @brief 添加一帧结果 */
    void addFrame(const FEResultFrame& frame);

    /** @brief 帧总数 */
    int frameCount() const;

    /** @brief 按索引获取帧（越界返回 nullptr） */
    const FEResultFrame* frame(int index) const;

    /** @brief 获取指定帧的结果类型名称列表（越界返回空） */
    std::vector<std::string> resultTypeNames(int frameIndex) const;

    /** @brief 是否为空 */
    bool empty() const;

    /**
     * @brief 从旧 FEResultData 构建 repository
     *
     * 每个 FESubcase 转为一个 FEResultFrame（domain=Static, value=0）。
     * 用于兼容现有 OP2/UNV 解析器输出。
     */
    static FEResultRepository fromResultData(const FEResultData& data);

private:
    std::vector<FEResultFrame> frames_;
};
