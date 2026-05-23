#include "ViewportContextMenu.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QString>
#include <QTimer>
#include <QToolButton>
#include <QWidget>

#include <vector>

namespace {
struct StandardViewSpec {
    QString text;
    StandardView view;
};

struct DisplayModeSpec {
    QString text;
    ModelDisplayMode mode;
};

const std::vector<StandardViewSpec>& standardViews()
{
    static const std::vector<StandardViewSpec> views = {
        {QStringLiteral("前视图"), StandardView::Front},
        {QStringLiteral("后视图"), StandardView::Back},
        {QStringLiteral("左视图"), StandardView::Left},
        {QStringLiteral("右视图"), StandardView::Right},
        {QStringLiteral("俯视图"), StandardView::Top},
        {QStringLiteral("仰视图"), StandardView::Bottom}
    };
    return views;
}

const std::vector<DisplayModeSpec>& displayModes()
{
    static const std::vector<DisplayModeSpec> modes = {
        {QStringLiteral("实体"), ModelDisplayMode::Solid},
        {QStringLiteral("线框"), ModelDisplayMode::Wireframe},
        {QStringLiteral("实体 + 线框"), ModelDisplayMode::SolidWireframe},
        {QStringLiteral("点"), ModelDisplayMode::Points}
    };
    return modes;
}

QToolButton* toolButtonFromWidget(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (auto* button = qobject_cast<QToolButton*>(current)) {
            return button;
        }
        if (current->isWindow()) {
            break;
        }
    }
    return nullptr;
}
} // namespace

ViewportContextMenu::ViewportContextMenu(QObject* parent)
    : QObject(parent)
{
}

void ViewportContextMenu::setDisplayMode(ModelDisplayMode mode)
{
    displayMode_ = mode;
}

void ViewportContextMenu::setGridVisible(bool visible)
{
    gridVisible_ = visible;
}

void ViewportContextMenu::setModelVisibilityState(bool hasModel,
                                                  bool hasSelection,
                                                  bool selectionHasVisibleParts,
                                                  bool selectionHasHiddenParts,
                                                  bool hasHiddenParts)
{
    hasModel_ = hasModel;
    hasSelection_ = hasSelection;
    selectionHasVisibleParts_ = selectionHasVisibleParts;
    selectionHasHiddenParts_ = selectionHasHiddenParts;
    hasHiddenParts_ = hasHiddenParts;
}

void ViewportContextMenu::popup(QWidget* parent, const QPoint& globalPos)
{
    if (!parent) {
        return;
    }

    if (activeMenu_) {
        activeMenu_->close();
    }

    auto* menu = new QMenu(parent);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    activeMenu_ = menu;
    activeParent_ = parent;
    qApp->installEventFilter(this);

    connect(menu, &QObject::destroyed, this, [this, menu]() {
        if (activeMenu_ == menu) {
            activeMenu_ = nullptr;
            activeParent_ = nullptr;
            qApp->removeEventFilter(this);
        }
    });

    QAction* fitAction = menu->addAction(QStringLiteral("适配窗口"));
    connect(fitAction, &QAction::triggered, this, &ViewportContextMenu::fitRequested);

    auto* viewMenu = menu->addMenu(QStringLiteral("标准视图"));
    for (const auto& spec : standardViews()) {
        QAction* action = viewMenu->addAction(spec.text);
        connect(action, &QAction::triggered, this, [this, view = spec.view]() {
            emit standardViewRequested(view);
        });
    }

    auto* displayMenu = menu->addMenu(QStringLiteral("显示模式"));
    auto* displayGroup = new QActionGroup(menu);
    displayGroup->setExclusive(true);
    for (const auto& spec : displayModes()) {
        QAction* action = displayMenu->addAction(spec.text);
        action->setCheckable(true);
        action->setChecked(spec.mode == displayMode_);
        action->setData(static_cast<int>(spec.mode));
        displayGroup->addAction(action);
    }
    connect(displayGroup, &QActionGroup::triggered, this, [this](QAction* action) {
        const auto mode = static_cast<ModelDisplayMode>(action->data().toInt());
        displayMode_ = mode;
        emit displayModeRequested(mode);
    });

    menu->addSeparator();
    QAction* modelInfoAction = menu->addAction(QStringLiteral("模型信息..."));
    modelInfoAction->setEnabled(hasModel_);
    connect(modelInfoAction, &QAction::triggered,
            this, &ViewportContextMenu::modelInfoRequested);

    menu->addSeparator();
    QAction* hideSelectedAction = menu->addAction(QStringLiteral("隐藏选中"));
    hideSelectedAction->setEnabled(hasModel_ && hasSelection_ && selectionHasVisibleParts_);
    connect(hideSelectedAction, &QAction::triggered,
            this, &ViewportContextMenu::hideSelectedRequested);

    QAction* showSelectedAction = menu->addAction(QStringLiteral("显示选中"));
    showSelectedAction->setEnabled(hasModel_ && hasSelection_ && selectionHasHiddenParts_);
    connect(showSelectedAction, &QAction::triggered,
            this, &ViewportContextMenu::showSelectedRequested);

    QAction* isolateSelectedAction = menu->addAction(QStringLiteral("仅显示选中"));
    isolateSelectedAction->setEnabled(hasModel_ && hasSelection_);
    connect(isolateSelectedAction, &QAction::triggered,
            this, &ViewportContextMenu::isolateSelectedRequested);

    QAction* showAllAction = menu->addAction(QStringLiteral("显示全部"));
    showAllAction->setEnabled(hasModel_ && hasHiddenParts_);
    connect(showAllAction, &QAction::triggered,
            this, &ViewportContextMenu::showAllRequested);

    menu->addSeparator();
    QAction* gridAction = menu->addAction(QStringLiteral("网格"));
    gridAction->setCheckable(true);
    gridAction->setChecked(gridVisible_);
    connect(gridAction, &QAction::toggled, this, [this](bool visible) {
        gridVisible_ = visible;
        emit gridVisibleChanged(visible);
    });

    QAction* backgroundAction = menu->addAction(QStringLiteral("背景色设置..."));
    connect(backgroundAction, &QAction::triggered,
            this, &ViewportContextMenu::backgroundSettingsRequested);

    menu->popup(globalPos);
}

bool ViewportContextMenu::eventFilter(QObject* watched, QEvent* event)
{
    if (activeMenu_ && activeMenu_->isVisible() && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        const bool insideMenu = activeMenu_->rect().contains(activeMenu_->mapFromGlobal(globalPos));

        if (!insideMenu && mouseEvent->button() == Qt::LeftButton) {
            if (auto* button = toolButtonFromWidget(QApplication::widgetAt(globalPos))) {
                if (button->isEnabled()) {
                    QPointer<QToolButton> target = button;
                    activeMenu_->close();
                    QTimer::singleShot(0, this, [target]() {
                        if (target) {
                            target->click();
                        }
                    });
                    return true;
                }
            }
        }

        if (mouseEvent->button() == Qt::RightButton && !insideMenu && activeParent_) {
            const bool insideViewport =
                activeParent_->rect().contains(activeParent_->mapFromGlobal(globalPos));
            if (insideViewport) {
                QPointer<QWidget> parent = activeParent_;
                activeMenu_->close();
                QTimer::singleShot(0, this, [this, parent, globalPos]() {
                    if (parent) {
                        popup(parent, globalPos);
                    }
                });
                return true;
            }
        }
    }
    return QObject::eventFilter(watched, event);
}
