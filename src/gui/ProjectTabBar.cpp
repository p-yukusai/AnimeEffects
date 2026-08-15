#include <QFile>
#include <QFileInfo>
#include <QMenu>
#include <QMouseEvent>
#include <QHoverEvent>
#include "gui/ProjectTabBar.h"

namespace gui {

static const int kBarHeight = 18;

//-------------------------------------------------------------------------------------------------
ProjectTabBar::ProjectTabBar(QWidget* aParent, GUIResources& aResources):
    QTabBar(aParent), mProjects(), mCloseButtons(), mSignal(true), mContextMenuIndex(-1), mGUIResources(aResources) {
    this->setUsesScrollButtons(false);
    this->setAutoFillBackground(true);
    this->setExpanding(false);
    this->setElideMode(Qt::ElideRight);
    this->setDrawBase(false);
    this->setMouseTracking(true);
    this->setAttribute(Qt::WA_Hover, true);

    this->installEventFilter(this);

    this->setGeometry(0, 0, aParent->geometry().width(), kBarHeight);

    this->connect(this, &QTabBar::currentChanged, this, &ProjectTabBar::onTabChanged);
    mGUIResources.onThemeChanged.connect(this, &ProjectTabBar::onThemeUpdated);
    this->onThemeUpdated(mGUIResources.mTheme);
}

void ProjectTabBar::updateTabPosition(const QSize& aDisplaySize) {
    this->setGeometry(0, 0, aDisplaySize.width(), kBarHeight);
}

QString ProjectTabBar::getTabName(const core::Project& aProject) const {
    QString name = aProject.fileName();
    return name.isEmpty() ? QString("New Project") : QFileInfo(name).fileName();
}

void ProjectTabBar::onThemeUpdated(theme::Theme& aTheme) {
    this->setStyleSheet(aTheme.loadStylesheet("modetabbar.ssa"));
    this->update();
    for (auto* button : mCloseButtons) {
        if (button) {
            button->update();
        }
    }
}

bool ProjectTabBar::pushProject(core::Project& aProject) {
    if (!mProjects.contains(&aProject)) {
        mSignal = false;
        mProjects.push_back(&aProject);
        const int index = this->addTab(getTabName(aProject));
        mCloseButtons.push_back(createCloseButton(aProject));
        this->setTabButton(index, QTabBar::RightSide, mCloseButtons.last());
        this->setCurrentIndex(index);
        this->updateTabActiveState();
        mSignal = true;

        aProject.commandStack().setOnEditStatusChanged([=](bool) { this->updateTabNames(); });
        return true;
    }
    return false;
}

TabCloseButton* ProjectTabBar::createCloseButton(core::Project& aProject) {
    auto* button = new TabCloseButton(this);
    button->setDirty(aProject.commandStack().isEdited());
    button->setToolTip(tr("Close tab"));
    this->connect(button, &QAbstractButton::clicked, this, [this, &aProject]() { onCloseTab(aProject); });
    return button;
}

void ProjectTabBar::removeProject(core::Project& aProject) {
    const int index = mProjects.indexOf(&aProject);

    if (0 <= index && index < mProjects.count()) {
        mSignal = false;

        mProjects.removeAt(index);
        mCloseButtons.removeAt(index);
        // removeTab already reselects when the current tab is removed; do not
        // force a selection here, or closing an inactive tab would jump the
        // current project to the closed tab's slot.
        this->removeTab(index);
        this->updateTabActiveState();
        mSignal = true;

        aProject.commandStack().setOnEditStatusChanged(nullptr);
    }
}

void ProjectTabBar::removeAllProject() {
    mSignal = false;
    mCloseButtons.clear();
    for (int i = 0; i < mProjects.count(); ++i) {
        this->removeTab(0);
    }
    mProjects.clear();
    this->updateTabActiveState();
    mSignal = true;
}

void ProjectTabBar::updateTabNames() {
    for (int i = 0; i < mProjects.count(); ++i) {
        this->setTabText(i, getTabName(*mProjects[i]));
        if (i < mCloseButtons.count() && mCloseButtons[i]) {
            mCloseButtons[i]->setDirty(mProjects[i]->commandStack().isEdited());
        }
    }
}

void ProjectTabBar::updateTabHover(int aIndex) {
    for (int i = 0; i < mCloseButtons.count(); ++i) {
        if (mCloseButtons[i]) {
            mCloseButtons[i]->setTabHovered(i == aIndex);
        }
    }
}

void ProjectTabBar::updateTabActiveState() {
    const int current = this->currentIndex();
    for (int i = 0; i < mCloseButtons.count(); ++i) {
        if (mCloseButtons[i]) {
            mCloseButtons[i]->setActive(i == current);
        }
    }
}

void ProjectTabBar::showContextMenu(const QPoint& aGlobalPos) {
    if (mContextMenuIndex < 0 || mContextMenuIndex >= mProjects.count()) {
        return;
    }

    core::Project& project = *mProjects[mContextMenuIndex];

    QMenu menu(this);
    QAction* saveAction = menu.addAction(tr("Save"));
    QAction* saveAsAction = menu.addAction(tr("Save As..."));
    menu.addSeparator();
    QAction* closeAction = menu.addAction(tr("Close"));
    QAction* closeOthersAction = menu.addAction(tr("Close Others"));
    QAction* closeAllAction = menu.addAction(tr("Close All"));
    menu.addSeparator();
    QAction* copyPathAction = menu.addAction(tr("Copy Full Path"));
    copyPathAction->setEnabled(!project.fileName().isEmpty());

    const QAction* selected = menu.exec(aGlobalPos);
    if (selected == saveAction) {
        onSaveTab(project);
    } else if (selected == saveAsAction) {
        onSaveTabAs(project);
    } else if (selected == closeAction) {
        onCloseTab(project);
    } else if (selected == closeOthersAction) {
        onCloseOtherTabs(project);
    } else if (selected == closeAllAction) {
        onCloseAllTabs();
    } else if (selected == copyPathAction) {
        onCopyPath(project);
    }
}

core::Project* ProjectTabBar::currentProject() const {
    const int index = this->currentIndex();
    if (0 <= index && index < mProjects.count()) {
        return mProjects[index];
    }
    return nullptr;
}

void ProjectTabBar::onTabChanged(int aIndex) {
    if (mSignal) {
        if (0 <= aIndex && aIndex < mProjects.count()) {
            onCurrentChanged(*mProjects[aIndex]);
        }
    }
    updateTabActiveState();
}

bool ProjectTabBar::eventFilter(QObject* aObj, QEvent* aEvent) {
    if (aObj == this) {
        switch (aEvent->type()) {
        case QEvent::HoverMove: {
            const int index = this->tabAt(static_cast<QHoverEvent*>(aEvent)->position().toPoint());
            updateTabHover(index);
            break;
        }
        case QEvent::HoverLeave:
        case QEvent::Leave:
            updateTabHover(-1);
            break;
        default:
            break;
        }
    }
    return QTabBar::eventFilter(aObj, aEvent);
}

void ProjectTabBar::mousePressEvent(QMouseEvent* aEvent) {
    if (aEvent->button() == Qt::RightButton) {
        const int index = this->tabAt(aEvent->pos());
        if (0 <= index && index < mProjects.count()) {
            mContextMenuIndex = index;
            showContextMenu(aEvent->globalPosition().toPoint());
            aEvent->accept();
            return;
        }
    }
    QTabBar::mousePressEvent(aEvent);
}

} // namespace gui
