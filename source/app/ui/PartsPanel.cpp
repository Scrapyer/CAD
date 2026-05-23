/**
 * @file PartsPanel.cpp
 * @brief 部件模型树面板实现
 */

#include "PartsPanel.h"
#include "Theme.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QToolButton>
#include <QVBoxLayout>
#include <set>

namespace {
constexpr int kNameColumn = 0;
constexpr int kVisibilityColumn = 1;
constexpr int kPartRole = Qt::UserRole;
constexpr int kVisibleRole = Qt::UserRole + 1;
} // namespace

PartsPanel::PartsPanel(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(220);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* header = new QFrame;
    header->setObjectName(QStringLiteral("itemsHeader"));
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(10, 5, 8, 5);
    headerLayout->setSpacing(6);

    auto* title = new QLabel(QStringLiteral("All Items"));
    title->setObjectName(QStringLiteral("itemsHeaderTitle"));
    headerLayout->addWidget(title, 1);

    auto* headerButton = new QToolButton;
    headerButton->setObjectName(QStringLiteral("itemsHeaderButton"));
    headerButton->setText(QStringLiteral("..."));
    headerButton->setToolTip(QStringLiteral("更多"));
    headerLayout->addWidget(headerButton);
    layout->addWidget(header);

    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(2);
    tree_->setIndentation(16);
    tree_->setAnimated(true);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setRootIsDecorated(true);
    tree_->setItemsExpandable(true);
    tree_->setExpandsOnDoubleClick(true);
    tree_->setAllColumnsShowFocus(false);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(kVisibilityColumn, QHeaderView::Fixed);
    tree_->setColumnWidth(kVisibilityColumn, 28);
    layout->addWidget(tree_);

    connect(tree_, &QTreeWidget::itemClicked, this, &PartsPanel::onItemClicked);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &PartsPanel::onSelectionChanged);

    auto* footer = new QFrame;
    footer->setObjectName(QStringLiteral("itemsFooter"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->setSpacing(6);
    auto* addButton = new QToolButton;
    addButton->setObjectName(QStringLiteral("itemsFooterButton"));
    addButton->setText(QStringLiteral("+"));
    addButton->setToolTip(QStringLiteral("添加入口预留"));
    auto* moreButton = new QToolButton;
    moreButton->setObjectName(QStringLiteral("itemsFooterButton"));
    moreButton->setText(QStringLiteral("..."));
    moreButton->setToolTip(QStringLiteral("更多"));
    footerLayout->addWidget(addButton);
    footerLayout->addStretch();
    footerLayout->addWidget(moreButton);
    layout->addWidget(footer);

    // 默认主题在 MainWindow 中统一调用 applyTheme() 设置
}

void PartsPanel::applyTheme(const Theme& t) {
    iconColor_ = QColor(t.overlay2);
    QString style = QStringLiteral(
        "QWidget { background: @base@; color: @text@; }"
        "QFrame#itemsHeader {"
        "  background: @mantle@; border: 1px solid @surface0@; border-radius: 6px; }"
        "QLabel#itemsHeaderTitle {"
        "  color: @text@; font-weight: 600; font-size: 12px; }"
        "QToolButton#itemsHeaderButton, QToolButton#itemsFooterButton {"
        "  background: transparent; color: @surface2@; border: none;"
        "  min-width: 24px; min-height: 24px; border-radius: 4px; }"
        "QToolButton#itemsHeaderButton:hover, QToolButton#itemsFooterButton:hover {"
        "  background: @surface0@; color: @text@; }"
        "QTreeWidget {"
        "  background: transparent; border: none;"
        "  outline: none; padding: 0; }"
        "QTreeWidget::item {"
        "  min-height: 25px; padding: 2px 4px;"
        "  border-radius: 4px; margin: 1px 0; }"
        "QTreeWidget::item:hover { background: @surface0@; }"
        "QTreeWidget::item:selected {"
        "  background: @surface1@; color: @text@; }"
        "QTreeWidget::branch { background: transparent; }"
        "QTreeWidget::branch:closed:has-children {"
        "  image: none; border-image: none; }"
        "QTreeWidget::branch:open:has-children {"
        "  image: none; border-image: none; }"
        "QFrame#itemsFooter { background: transparent; }"
        "QScrollBar:vertical {"
        "  background: transparent; width: 8px; margin: 4px 0; }"
        "QScrollBar::handle:vertical {"
        "  background: @surface0@; border-radius: 4px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: @surface2@; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"
    );
    style.replace(QStringLiteral("@base@"), t.base);
    style.replace(QStringLiteral("@text@"), t.text);
    style.replace(QStringLiteral("@mantle@"), t.mantle);
    style.replace(QStringLiteral("@surface0@"), t.surface0);
    style.replace(QStringLiteral("@surface1@"), t.surface1);
    style.replace(QStringLiteral("@surface2@"), t.surface2);
    setStyleSheet(style);

    if (rootItem_) {
        rootItem_->setIcon(kNameColumn, makeRootIcon(iconColor_));
        updateRootVisibilityIcon();
        for (int i = 0; i < rootItem_->childCount(); ++i) {
            QTreeWidgetItem* child = rootItem_->child(i);
            const bool visible = child->data(kNameColumn, kVisibleRole).toBool();
            child->setIcon(kVisibilityColumn, makeVisibilityIcon(visible, iconColor_));
        }
    }
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

QIcon PartsPanel::makeVisibilityIcon(bool visible, const QColor& color) const
{
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);

    QColor c = color;
    c.setAlpha(visible ? 230 : 90);
    QPen pen(c, 1.5);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    QPainterPath eye;
    eye.moveTo(2.5, 9.0);
    eye.cubicTo(5.0, 5.4, 13.0, 5.4, 15.5, 9.0);
    eye.cubicTo(13.0, 12.6, 5.0, 12.6, 2.5, 9.0);
    p.drawPath(eye);
    if (visible) {
        p.setBrush(c);
        p.drawEllipse(QPointF(9.0, 9.0), 2.2, 2.2);
    } else {
        p.drawLine(QPointF(4.0, 14.0), QPointF(14.0, 4.0));
    }
    return QIcon(pm);
}

QIcon PartsPanel::makePartIcon(const glm::vec3& color) const
{
    return QIcon(makeColorSwatch(color, 14));
}

QIcon PartsPanel::makeRootIcon(const QColor& color) const
{
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QColor c = color;
    c.setAlpha(220);
    QPen pen(c, 1.4);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    QPolygonF top;
    top << QPointF(9, 2.5) << QPointF(14.5, 5.5) << QPointF(9, 8.5) << QPointF(3.5, 5.5);
    QPolygonF left;
    left << QPointF(3.5, 5.5) << QPointF(9, 8.5) << QPointF(9, 15.5) << QPointF(3.5, 12.5);
    QPolygonF right;
    right << QPointF(14.5, 5.5) << QPointF(9, 8.5) << QPointF(9, 15.5) << QPointF(14.5, 12.5);
    p.drawPolygon(top);
    p.drawPolygon(left);
    p.drawPolygon(right);
    return QIcon(pm);
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

    QString rootName = modelName.isEmpty() ? "模型" : modelName;
    rootItem_ = new QTreeWidgetItem(tree_);
    rootItem_->setText(kNameColumn, rootName);
    rootItem_->setIcon(kNameColumn, makeRootIcon(iconColor_));
    rootItem_->setIcon(kVisibilityColumn, makeVisibilityIcon(true, iconColor_));
    rootItem_->setData(kNameColumn, kVisibleRole, true);
    rootItem_->setFlags((rootItem_->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable)
        & ~Qt::ItemIsUserCheckable);
    QFont rootFont = rootItem_->font(0);
    rootFont.setBold(true);
    rootItem_->setFont(0, rootFont);

    for (int i = 0; i < static_cast<int>(parts.size()); ++i) {
        const FEPart& part = parts[i];

        QString label = QString::fromStdString(part.name);
        if (!part.elementIds.empty())
            label += QString("  (%1)").arg(part.elementIds.size());

        auto* item = new QTreeWidgetItem(rootItem_);
        item->setText(kNameColumn, label);
        item->setFlags((item->flags() | Qt::ItemIsEnabled | Qt::ItemIsSelectable)
            & ~Qt::ItemIsUserCheckable);
        item->setData(kNameColumn, kPartRole, i);
        item->setData(kNameColumn, kVisibleRole, part.visible);
        item->setIcon(kVisibilityColumn, makeVisibilityIcon(part.visible, iconColor_));
        if (i < static_cast<int>(partColors.size())) {
            item->setIcon(kNameColumn, makePartIcon(partColors[i]));
        }
    }

    tree_->expandAll();
    updating_ = false;
}

void PartsPanel::onItemClicked(QTreeWidgetItem* item, int column)
{
    if (updating_ || !item || column != kVisibilityColumn) {
        return;
    }

    if (item == rootItem_) {
        const bool anyVisible = rootItem_->data(kNameColumn, kVisibleRole).toBool();
        const bool visible = !anyVisible;
        updating_ = true;
        for (int i = 0; i < rootItem_->childCount(); ++i) {
            setPartVisible(rootItem_->child(i), visible, true);
        }
        updating_ = false;
        updateRootVisibilityIcon();
        return;
    }

    const bool visible = !item->data(kNameColumn, kVisibleRole).toBool();
    setPartVisible(item, visible, true);
    updateRootVisibilityIcon();
}

void PartsPanel::onSelectionChanged() {
    if (updating_) return;
    std::vector<int> selected;
    for (auto* item : tree_->selectedItems()) {
        QVariant v = item->data(kNameColumn, kPartRole);
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
        int partIndex = child->data(kNameColumn, kPartRole).toInt();
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

void PartsPanel::setPartVisible(QTreeWidgetItem* item, bool visible, bool notify)
{
    if (!item || item == rootItem_) {
        return;
    }
    item->setData(kNameColumn, kVisibleRole, visible);
    item->setIcon(kVisibilityColumn, makeVisibilityIcon(visible, iconColor_));
    if (notify) {
        const int partIndex = item->data(kNameColumn, kPartRole).toInt();
        emit partVisibilityChanged(partIndex, visible);
    }
}

void PartsPanel::updateRootVisibilityIcon()
{
    if (!rootItem_) {
        return;
    }
    bool anyVisible = false;
    bool allVisible = rootItem_->childCount() > 0;
    for (int i = 0; i < rootItem_->childCount(); ++i) {
        const bool visible = rootItem_->child(i)->data(kNameColumn, kVisibleRole).toBool();
        anyVisible = anyVisible || visible;
        allVisible = allVisible && visible;
    }
    rootItem_->setData(kNameColumn, kVisibleRole, anyVisible);
    QColor color = iconColor_;
    if (anyVisible && !allVisible) {
        color = QColor(137, 180, 250);
    }
    rootItem_->setIcon(kVisibilityColumn, makeVisibilityIcon(anyVisible, color));
}
