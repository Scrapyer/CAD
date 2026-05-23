/**
 * @file FEModelPanel.h
 * @brief 有限元模型加载与数据分发组件声明
 *
 * 保留模型文件加载、结果解析和渲染数据分发逻辑，不再提供属性面板 UI。
 */

#pragma once

#include <QWidget>
#include <QString>

#include "FEModel.h"
#include "FERenderData.h"
#include "FEGroup.h"
#include "FEResultData.h"

struct Theme;

class FEModelPanel : public QWidget {
    Q_OBJECT

public:
    explicit FEModelPanel(QWidget* parent = nullptr);

    /** @brief 打开文件对话框并加载 FEM 模型（供工具栏调用） */
    void loadModelFromFile();

    /** @brief 从指定路径加载 FEM 模型（供底部面板调用） */
    void loadModelFromPath(const QString& path);

    /** @brief 清空当前模型 */
    void clearModel();

    /** @brief 从 OP2 文件解析结果数据（位移/应力），委托给 FEParser */
    static bool parseNastranOp2Results(const QString& filePath, FEResultData& results);

    /** @brief 从 UNV 文件解析结果数据（Dataset 2414/55），委托给 FEParser */
    static bool parseUnvResults(const QString& filePath, FEResultData& results);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

    /** @brief 获取当前模型 */
    const FEModel& currentModel() const { return currentModel_; }

    /** @brief 获取当前渲染数据 */
    const FERenderData& currentRenderData() const { return currentRenderData_; }

signals:
    void meshGenerated(const Mesh& mesh, const glm::vec3& center, float size,
                       const std::vector<int>& triToElem,
                       const std::vector<int>& vertexToNode);

    void partsChanged(const QString& modelName, const std::vector<FEPart>& parts,
                      const std::vector<int>& triToPart, const std::vector<int>& edgeToPart);

    /** @brief 加载进度更新 (0-100, 描述文字) */
    void loadProgress(int percent, const QString& text);

    /** @brief 加载完成 (成功/失败, 消息) */
    void loadFinished(bool success, const QString& message);

    /** @brief 结果数据加载完成 */
    void resultsLoaded(const FEResultData& results);

private:
    // 解析逻辑已移至 FEParser 静态工具类

    // ── 数据 ──
    FEModel currentModel_;
    FERenderData currentRenderData_;
};
