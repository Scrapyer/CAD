/**
 * @file PostState.h
 * @brief 后处理显示状态（变形 / 云图 / 过滤·裁剪·切片·等值面）
 *
 * 纯值类型，供 MainWindow 集中管理当前渲染状态，
 * 替代散落在各处的 bool 标志和临时变量。
 */

#pragma once

#include <QString>
#include <glm/glm.hpp>
#include "FERenderData.h"
#include "FEField.h"
#include "FEModel.h"

// ════════════════════════════════════════════════════════════
// 变形状态
// ════════════════════════════════════════════════════════════

struct DeformState {
    bool active = false;
    float scale = 1.0f;
    bool overlay = false;
    FERenderData renderData;
    FEModel model;

    void clear() {
        active = false;
        scale = 1.0f;
        overlay = false;
        renderData.clear();
        model.clear();
    }
};

// ════════════════════════════════════════════════════════════
// 云图状态
// ════════════════════════════════════════════════════════════

struct ContourState {
    bool active = false;
    FEScalarField field;
    QString title;

    void clear() {
        active = false;
        field = {};
        title.clear();
    }
};

// ════════════════════════════════════════════════════════════
// 后处理操作模式与参数
// ════════════════════════════════════════════════════════════

enum class PostEffectMode {
    None,
    Threshold,
    ClipPlane,
    Slice,
    IsoSurface
};

struct ThresholdParams {
    QString fieldName;
    float minVal = 0;
    float maxVal = 0;
};

struct ClipPlaneParams {
    int axis = 0;
    float offset = 0;
    glm::vec3 origin{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    bool keepPositive = true;
};

struct SlicePlaneParams {
    int axis = 0;
    float offset = 0;
    glm::vec3 origin{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
};

struct IsoSurfaceParams {
    QString fieldName;
    float isoValue = 0;
};

// ════════════════════════════════════════════════════════════
// 后处理操作状态
// ════════════════════════════════════════════════════════════

struct PostEffectState {
    PostEffectMode mode = PostEffectMode::None;
    bool applied = false;

    ThresholdParams threshold;
    ClipPlaneParams clipPlane;
    SlicePlaneParams slice;
    IsoSurfaceParams iso;

    FERenderData filteredRD;

    bool isActive() const { return applied && mode != PostEffectMode::None; }

    bool isMeshReplacement() const {
        return mode == PostEffectMode::Threshold
            || mode == PostEffectMode::ClipPlane;
    }

    bool isOverlay() const {
        return mode == PostEffectMode::Slice
            || mode == PostEffectMode::IsoSurface;
    }

    void clear() {
        mode = PostEffectMode::None;
        applied = false;
        threshold = {};
        clipPlane = {};
        slice = {};
        iso = {};
        filteredRD.clear();
    }
};
