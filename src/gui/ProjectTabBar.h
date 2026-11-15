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
    // tr() in this class resolves through QTabBar without a metaobject of its own, so
    // the catalog's gui::ProjectTabBar entries are read by nobody.
    Q_OBJECT
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
    /// Qt moves a tab's data but not the widgets set on its indices, so the model
    /// behind the bar (mProjects and mCloseButtons run parallel to the tabs) is
    /// reordered here and the close buttons are re-applied positionally.
    void onTabMoved(int aFrom, int aTo);
    /// Sizes each close button to the tab it sits in. The button is a widget
    /// rather than the style's close-button subcontrol - it shows the dirty state
    /// and a hover ring - so nothing else keeps the two sizes in step.
    void syncCloseButtonSizes();

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
