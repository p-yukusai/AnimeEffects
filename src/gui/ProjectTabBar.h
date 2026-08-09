#ifndef GUI_PROJECTTABBAR_H
#define GUI_PROJECTTABBAR_H

#include <QVector>
#include <QTabBar>
#include <QMouseEvent>
#include "core/Project.h"

#include "gui/GUIResources.h"
#include "gui/TabCloseButton.h"

namespace gui {

class ProjectTabBar: public QTabBar {
public:
    ProjectTabBar(QWidget* aParent, GUIResources& aResources);
    void updateTabPosition(const QSize& aDisplaySize);
    bool pushProject(core::Project& aProject);
    void removeProject(core::Project& aProject);
    void removeAllProject();
    void updateTabNames();
    core::Project* currentProject() const;
    util::Signaler<void(core::Project&)> onCurrentChanged;

    util::Signaler<void(core::Project&)> onCloseTab;
    util::Signaler<void(core::Project&)> onCloseOtherTabs;
    util::Signaler<void()> onCloseAllTabs;
    util::Signaler<void(core::Project&)> onSaveTab;
    util::Signaler<void(core::Project&)> onSaveTabAs;
    util::Signaler<void(core::Project&)> onCopyPath;

    QString getTabName(const core::Project&) const;

private:
    void onTabChanged(int aIndex);
    TabCloseButton* createCloseButton(core::Project& aProject);
    void updateTabHover(int aIndex);
    void updateTabActiveState();
    void showContextMenu(const QPoint& aGlobalPos);
    void updateTabHeight();

    QVector<core::Project*> mProjects;
    QVector<TabCloseButton*> mCloseButtons;
    bool mSignal;
    int mContextMenuIndex;

    GUIResources& mGUIResources;
    void onThemeUpdated(theme::Theme&);

    // QObject
    virtual bool eventFilter(QObject* aObj, QEvent* aEvent) override;
    virtual void mousePressEvent(QMouseEvent* aEvent) override;
};


} // namespace gui

#endif // GUI_PROJECTTABBAR_H
