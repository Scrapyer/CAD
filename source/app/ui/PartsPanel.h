/**
 * @file PartsPanel.h
 * @brief 部件模型树面板声明
 *
 * 以树形结构显示 FEM 模型的组成：
 *   - 根节点：模型名称
 *   - 子节点：每个部件（带颜色色块 + 可见性图标控制显隐）
 */

#pragma once

#include <QIcon>
#include <QPoint>
#include <QWidget>
#include <QTreeWidget>
#include <utility>
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

    /** @brief 返回当前部件显隐状态（partIndex, visible） */
    std::vector<std::pair<int, bool>> partVisibilityStates() const;

public slots:
    /** @brief 程序化选中指定部件（由 GLWidget 部件拾取触发） */
    void selectParts(const std::vector<int>& partIndices);

    /** @brief 程序化设置单个部件显隐，并可选择是否通知视口 */
    void setPartVisibleByIndex(int partIndex, bool visible, bool notify = true);

    /** @brief 程序化设置全部部件显隐，并可选择是否通知视口 */
    void setAllPartsVisible(bool visible, bool notify = true);

    /** @brief 仅显示指定部件，其余部件隐藏 */
    void isolateParts(const std::vector<int>& partIndices, bool notify = true);

signals:
    /** @brief 某个部件的可见性被用户切换 */
    void partVisibilityChanged(int partIndex, bool visible);

    /** @brief 模型树中选中的部件发生变化（多选） */
    void partSelectionChanged(const std::vector<int>& selectedParts);

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onSelectionChanged();
    void onContextMenuRequested(const QPoint& pos);

private:
    QPixmap makeColorSwatch(const glm::vec3& color, int size = 12) const;
    QIcon makeVisibilityIcon(bool visible, const QColor& color) const;
    QIcon makePartIcon(const glm::vec3& color) const;
    QIcon makeRootIcon(const QColor& color) const;
    QTreeWidgetItem* itemForPartIndex(int partIndex) const;
    std::vector<int> selectedPartIndices() const;
    bool allPartsVisible() const;
    bool anyPartsVisible() const;
    void setPartVisible(QTreeWidgetItem* item, bool visible, bool notify);
    void updateRootVisibilityIcon();

    QTreeWidget*  tree_       = nullptr;
    QTreeWidgetItem* rootItem_ = nullptr;
    bool          updating_   = false;   // 防止信号递归
    QColor        iconColor_{160, 166, 190};
};
