#ifndef GUI_PROPERTYWIDGET_H
#define GUI_PROPERTYWIDGET_H

#include <QFont>
#include <QWidget>

#include "util/SlotId.h"
#include "util/LinkPointer.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/TimeLineEvent.h"
#include "gui/ViaPoint.h"
#include "gui/LocaleParam.h"

class QQuickWidget;

namespace gui {

class GUIResources;
class InspectorViewModel;
class QmlTheme;

// The dock's Properties page. The scene in qml/PropertiesPanel.qml owns the
// layout and ink; InspectorViewModel owns every document decision. This class is
// only the host: it keeps the scene alive, carries the project notifications in,
// and answers the one request that needs a widget (the asset chooser).
class PropertyWidget: public QWidget {
public:
    PropertyWidget(ViaPoint& aViaPoint, QWidget* aParent, GUIResources* aGUIResources, const LocaleParam& aLocale);
    ~PropertyWidget();

    void setProject(core::Project* aProject);

    // boostlike signals
    void onSelectionChanged(core::ObjectNode* aRepresentNode);
    void onAttributeUpdated(core::ObjectNode& aNode, bool aUndo);
    void onKeyUpdated(core::TimeLineEvent& aEvent, bool aUndo);
    void onFrameUpdated();
    void onPlayBackStateChanged(bool aIsActive);
    // The tool's mask paint state, with the node it is aimed at. The cards disable
    // Paint only when the tool is painting THEIR field on THIS node.
    void setMaskPaintState(core::ObjectNode* aNode, bool aLayerMask, bool aFilterInput,
                           core::TimeKeyType aLane, core::FilterMaskChannel aChannel);
    void refreshTheme();
    // the matte's invert (Ctrl+I with the Mask tool in hand): one door onto the model's own edit
    void invertMask();

    util::Signaler<void()> onVisualUpdated;
    // a filter row asked to paint its mask (see InspectorViewModel)
    util::Signaler<void(core::TimeKeyType, core::FilterMaskChannel)> onMaskPaintRequested;
    // the card's Paint: the node's own matte, which has no lane or channel
    util::Signaler<void()> onLayerMaskPaintRequested;
    // the Mesh spacing field's transient overlay: node, and whether to show it
    util::Signaler<void(core::ObjectNode*, bool)> onMeshPreviewRequested;
    // a filter/key edit may have changed which mask fields the current node can
    // paint; the Tools panel rebuilds its derived chooser from this
    util::Signaler<void()> onMaskTargetsChanged;

private:
    void unlinkProject();
    void updateAllProperties();

    util::LinkPointer<core::Project> mProject;
    util::SlotId mTimeLineSlot;
    util::SlotId mNodeAttrSlot;
    util::SlotId mResModifiedSlot;
    util::SlotId mTreeRestructSlot;
    util::SlotId mProjAttrSlot;
    InspectorViewModel* mModel;
    QmlTheme* mTheme;
    QFont mUiFont;
    QQuickWidget* mQuick;
};

} // namespace gui

#endif // GUI_PROPERTYWIDGET_H
