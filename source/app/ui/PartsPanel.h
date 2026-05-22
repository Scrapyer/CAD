/**
 * @file PartsPanel.h
 * @brief 部件模型树面板声明
 *
 * 以树形结构显示 FEM 模型的组成：
 *   - 根节点：模型名称
 *   - 子节点：每个部件（带颜色色块 + 复选框控制显隐）
 */

#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <vector>
#include <glm/glm.hpp>

#include "FEGroup.h"

struct Theme;

class PartsPanel : public QWidget {
    Q_OBJECT

public:
    explicit PartsPanel(QWidget* parent = nullptr);

    /** @brief 应用主题 */
    void applyTheme(const Theme& theme);

    /** @brief 用新的模型名称、部件列表和颜色更新树 */
    void setParts(const QString& modelName,
                  const std::vector<FEPart>& parts,
                  const std::vector<glm::vec3>& partColors);

public slots:
    /** @brief 程序化选中指定部件（由 GLWidget 部件拾取触发） */
    void selectParts(const std::vector<int>& partIndices);

signals:
    /** @brief 某个部件的可见性被用户切换 */
    void partVisibilityChanged(int partIndex, bool visible);

    /** @brief 模型树中选中的部件发生变化（多选） */
    void partSelectionChanged(const std::vector<int>& selectedParts);

private slots:
    void onItemChanged(QTreeWidgetItem* item, int column);
    void onSelectionChanged();

private:
    QPixmap makeColorSwatch(const glm::vec3& color, int size = 12) const;

    QTreeWidget*  tree_       = nullptr;
    QTreeWidgetItem* rootItem_ = nullptr;
    bool          updating_   = false;   // 防止信号递归
};
