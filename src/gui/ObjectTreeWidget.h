#ifndef GUI_OBJECTTREEWIDGET_H
#define GUI_OBJECTTREEWIDGET_H

#include <QWidget>
#include <QVector>

#include "core/ObjectNode.h"
#include "core/TimeLineEvent.h"
#include "gui/AnimationRow.h"
#include "gui/GUIResources.h"
#include "gui/LocaleParam.h"
#include "gui/ViaPoint.h"
#include "util/LinkPointer.h"
#include "util/Signaler.h"

class QQuickWidget;

namespace gui {

class ObjectTreeModel;
class QmlTheme;
class KeyForwardingQuickWidget;

// THE ANIMATION DOCK'S TREE COLUMN.
//
// It is a Qt Quick scene now (qml/ObjectTreePanel.qml), hosted the same way the
// Properties and Tools docks are: a QQuickWidget with the model and the theme as context
// properties. What is left in C++ is the HOST's own job - the widgets an action needs
// (the asset chooser, the relink walker's tree selector, message boxes, the context menu),
// the project hand-over, and the two outward signals the dock wires.
//
// EVERYTHING ABOUT THE TREE ITSELF lives in gui::ObjectTreeModel: the rows (which the
// timeline draws too), the selection and its anchor/cursor rules, expansion, drag and drop,
// rename, and the actions - each one undo step, through ctrl/TreeActions.h. This class
// neither lays out a row nor decides what a click means.
//
// THE ROWS ARE THE TIMELINE'S ROWS. onRowsUpdated says "the row list is now this", and the
// dock hands rows() to TimeLineWidget::setRows: the tree's row heights and the lane blocks
// beside it come from ONE definition (ctrl::TimeLineRow::displayHeight), where the widget
// used to have its items' geometry read back by the timeline.
class ObjectTreeWidget: public QWidget {
    Q_OBJECT
public:
    ObjectTreeWidget(
        ViaPoint& aViaPoint, GUIResources& aResources, QWidget* aParent, const LocaleParam& aLocale
    );
    ~ObjectTreeWidget() override;

    void setProject(core::Project* aProject);

    // The one selected layer, or null when nothing (or a whole group) is selected: the
    // authority for the lane tint, the row rail and the inspector's target.
    core::ObjectNode* findSelectingRepresentNode();

    // The selected nodes in row order - the graph panel draws every one of them.
    QVector<core::ObjectNode*> selectedNodes() const;

    // The row list the timeline draws (see gui/AnimationRow.h).
    const AnimationRows& rows() const;

    // A vertical scroll request from the timeline (a pan, a key): the scene applies it and
    // reports the value it actually reached through onScrollUpdated.
    void setScrollOffset(int aOffset);

    // The dock forwards the timeline's frame notification: a mask chip previews the matte
    // at the CURRENT frame, so an animated one has to be re-rendered.
    void frameChanged();

    void refreshTheme();

    // The dock's column: wide enough to name a layer and narrow enough to leave the lanes
    // theirs. The widget sized itself to its widest row (a column that jumped as names
    // changed); the splitter starts here and the user's drag is what moves it.
    QSize sizeHint() const override { return QSize(240, 240); }

    // signals (the dock's wiring, unchanged in name and meaning)
    util::Signaler<void()> onVisibilityUpdated;
    util::Signaler<void(core::ObjectNode*)> onSelectionChanged;
    // the row list changed: push rows() into the timeline
    util::Signaler<void()> onRowsUpdated;
    // the tree's vertical scroll offset in pixels (positive = scrolled down)
    util::Signaler<void(int)> onScrollUpdated;

private:
    // ---- the widgets an action needs -------------------------------------
    void showMenu(int aRow, const QPoint& aScenePos);
    void pasteKeys(core::ObjectNode& aNode);
    void createLayerDialog(core::ObjectNode* aParent, int aIndex);

    ViaPoint& mViaPoint;
    GUIResources& mGUIResources;
    util::LinkPointer<core::Project> mProject;
    ObjectTreeModel* mModel;
    QmlTheme* mTheme;
    KeyForwardingQuickWidget* mQuick;
    // the withheld name of the last scroll this host pushed, so a scene-side report that
    // repeats it does not echo back into the timeline
    int mScrollOffset;
};

} // namespace gui

#endif // GUI_OBJECTTREEWIDGET_H
