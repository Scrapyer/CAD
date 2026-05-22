/**
 * @file PartsPanel.cpp
 * @brief 部件模型树面板实现
 */

#include "PartsPanel.h"
#include "Theme.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QPixmap>
#include <QPainter>
#include <QIcon>
#include <set>

PartsPanel::PartsPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(140);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(0);

    tree_ = new QTreeWidget;
    tree_->setHeaderLabel("模型树");
    tree_->setColumnCount(1);
    tree_->setIndentation(16);
    tree_->setAnimated(true);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    layout->addWidget(tree_);

    connect(tree_, &QTreeWidget::itemChanged, this, &PartsPanel::onItemChanged);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &PartsPanel::onSelectionChanged);

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置
}

void PartsPanel::applyTheme(const Theme& t) {
    setStyleSheet(QString(
        "QWidget { background: %1; color: %2; }"
        "QTreeWidget {"
        "  background: %3; border: 1px solid %4;"
        "  border-radius: 8px; outline: none; padding: 2px; }"
        "QTreeWidget::item {"
        "  padding: 4px 4px; border-radius: 4px; margin: 1px 0; }"
        "QTreeWidget::item:hover { background: %4; }"
        "QTreeWidget::item:selected { background: %5; }"
        "QHeaderView::section {"
        "  background: %3; border: none; border-bottom: 1px solid %4;"
        "  padding: 6px 10px; font-weight: bold; font-size: 12px; color: %6; }"
        "QTreeWidget::indicator {"
        "  width: 16px; height: 16px; border-radius: 4px;"
        "  border: 2px solid %7; background: %4; }"
        "QTreeWidget::indicator:checked { background: %6; border-color: %6; }"
        "QTreeWidget::indicator:indeterminate { background: %5; border-color: %6; }"
        "QScrollBar:vertical {"
        "  background: transparent; width: 10px; border-radius: 5px; margin: 4px 0; }"
        "QScrollBar::handle:vertical {"
        "  background: %5; border-radius: 5px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: %7; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    ).arg(t.base, t.text, t.mantle, t.surface0, t.surface1, t.blue, t.surface2));
}

QPixmap PartsPanel::makeColorSwatch(const glm::vec3& color, int size) const {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c(static_cast<int>(color.x * 255),
              static_cast<int>(color.y * 255),
              static_cast<int>(color.z * 255));
    p.setBrush(c);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(1, 1, size - 2, size - 2, 3, 3);
    return pm;
}

void PartsPanel::setParts(const QString& modelName,
                          const std::vector<FEPart>& parts,
                          const std::vector<glm::vec3>& partColors) {
    updating_ = true;
    tree_->clear();
    rootItem_ = nullptr;

    // 无部件时不创建根节点
    if (parts.empty()) {
        updating_ = false;
        return;
    }

    // 根节点 = 模型名称
    QString rootName = modelName.isEmpty() ? "模型" : modelName;
    rootItem_ = new QTreeWidgetItem(tree_, {rootName});
    rootItem_->setCheckState(0, Qt::Checked);
    rootItem_->setFlags(rootItem_->flags()
        | Qt::ItemIsUserCheckable
        | Qt::ItemIsEnabled
        | Qt::ItemIsAutoTristate);
    QFont rootFont = rootItem_->font(0);
    rootFont.setBold(true);
    rootFont.setPointSize(rootFont.pointSize() + 1);
    rootItem_->setFont(0, rootFont);

    for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
        const FEPart& part = parts[i];

        QString label = QString::fromStdString(part.name);
        if (!part.elementIds.empty())
            label += QString("  (%1)").arg(part.elementIds.size());

        auto* item = new QTreeWidgetItem(rootItem_, {label});
        item->setCheckState(0, part.visible ? Qt::Checked : Qt::Unchecked);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        item->setData(0, Qt::UserRole, i);   // store part index

        // 颜色色块图标
        if (i < static_cast<int>(partColors.size())) {
            item->setIcon(0, QIcon(makeColorSwatch(partColors[i])));
        }
    }

    tree_->expandAll();
    updating_ = false;
}

void PartsPanel::onItemChanged(QTreeWidgetItem* item, int /*column*/) {
    if (updating_) return;
    updating_ = true;

    if (item == rootItem_) {
        // 根节点切换 → 同步所有子节点
        // 忽略 PartiallyChecked（由子节点变化自动触发，不应同步子节点）
        Qt::CheckState state = rootItem_->checkState(0);
        if (state == Qt::PartiallyChecked) {
            updating_ = false;
            return;
        }
        bool visible = (state == Qt::Checked);
        for (int i = 0; i < rootItem_->childCount(); ++i) {
            QTreeWidgetItem* child = rootItem_->child(i);
            child->setCheckState(0, state);
            int partIndex = child->data(0, Qt::UserRole).toInt();
            emit partVisibilityChanged(partIndex, visible);
        }
    } else {
        // 子节点切换 → 通知 GLWidget
        int partIndex = item->data(0, Qt::UserRole).toInt();
        bool visible = (item->checkState(0) == Qt::Checked);
        emit partVisibilityChanged(partIndex, visible);

        // 更新根节点的三态复选框
        if (rootItem_) {
            int total = rootItem_->childCount();
            int checkedCount = 0;
            for (int i = 0; i < total; ++i)
                if (rootItem_->child(i)->checkState(0) == Qt::Checked)
                    ++checkedCount;
            if (checkedCount == 0)
                rootItem_->setCheckState(0, Qt::Unchecked);
            else if (checkedCount == total)
                rootItem_->setCheckState(0, Qt::Checked);
            else
                rootItem_->setCheckState(0, Qt::PartiallyChecked);
        }
    }

    updating_ = false;
}

void PartsPanel::onSelectionChanged() {
    if (updating_) return;
    std::vector<int> selected;
    for (auto* item : tree_->selectedItems()) {
        QVariant v = item->data(0, Qt::UserRole);
        if (v.isValid())
            selected.push_back(v.toInt());
    }
    emit partSelectionChanged(selected);
}

void PartsPanel::selectParts(const std::vector<int>& partIndices) {
    if (!rootItem_) return;
    updating_ = true;

    // 构建快速查找集合
    std::set<int> indexSet(partIndices.begin(), partIndices.end());

    tree_->clearSelection();
    for (int i = 0; i < rootItem_->childCount(); ++i) {
        QTreeWidgetItem* child = rootItem_->child(i);
        int partIndex = child->data(0, Qt::UserRole).toInt();
        child->setSelected(indexSet.count(partIndex) > 0);
    }

    // 确保选中项可见
    if (!partIndices.empty()) {
        for (int i = 0; i < rootItem_->childCount(); ++i) {
            QTreeWidgetItem* child = rootItem_->child(i);
            if (child->isSelected()) {
                tree_->scrollToItem(child);
                break;
            }
        }
    }

    updating_ = false;
}
