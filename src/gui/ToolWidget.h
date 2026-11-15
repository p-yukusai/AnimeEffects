#ifndef GUI_TOOLWIDGET_H
#define GUI_TOOLWIDGET_H

#include <QWidget>

#include "ctrl/Driver.h"
#include "gui/LocaleParam.h"
#include "gui/MainViewSetting.h"
#include "gui/ViaPoint.h"
#include "util/Signaler.h"

class QQuickWidget;

namespace gui {

class KeyCommandMap;
class QmlTheme;
class ToolViewModel;

// The dock's Tools page. The scene in qml/ToolsPanel.qml owns the layout and the ink;
// gui::ToolViewModel owns the tool parameters and every decision about which rows a tool
// shows. This class is only the HOST - the same split gui::PropertyWidget has on the
// other side of the dock:
//
//   * it keeps the scene alive and carries the theme, the font and the model in;
//   * it owns the DRIVER, so every request that needs one (push a param, switch tools,
//     clear a mask) is answered here and nowhere else;
//   * it owns the KEY MAP's side of the dock: the tool-selection bindings and the two
//     brush notches, which must keep working while the panel has the keyboard.
class ToolWidget: public QWidget {
    Q_OBJECT
public:
    ToolWidget(ViaPoint& aViaPoint,
               KeyCommandMap& aKeyCommandMap,
               const LocaleParam& aLocale,
               const QSize& aSizeHint,
               QWidget* aParent);
    ~ToolWidget();

    void setDriver(ctrl::Driver* aDriver);
    // The tree selection/frame changed: rebuild the mask-target chooser from the
    // document. Both are idempotent; the model only notifies when the result moves.
    void onSelectionChanged(core::ObjectNode* aNode);
    void onFrameUpdated();
    void refreshMaskTargets();
    // the temporary eyedropper's value, from the viewport through MainWindow
    void setMaskValue(float aValue);
    // one brush adjustment step (the [ ] keys and the Alt+right-drag gesture):
    // positive steps enlarge/harden, negative shrink/soften
    void adjustBrush(int aSizeSteps, int aHardnessSteps);
    // the Alt drag: CONTINUOUS steps of the same units, so a drag is not quantised to notches
    void nudgeBrush(double aSizeSteps, double aHardnessSteps);
    util::Signaler<void(double, double)> onBrushNudged;
    // the ACTIVE tool's brush, for the viewport's adjustment HUD (null when the tool has none)
    const ctrl::BrushParam* activeBrush() const;
    // A filter card asked to paint a mask: switch to the Mask tool with that lane and
    // channel already selected, so the artist paints immediately.
    void beginMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel);
    // the node's own matte: same tool, same brush, a target that is not a filter lane
    void beginLayerMaskPaint();

    void refreshTheme();

    QSize sizeHint() const override { return mSizeHint; }

    util::Signaler<void()> onVisualUpdated;
    // Ctrl+I with the Mask tool in hand: the properties dock owns the matte's edit
    util::Signaler<void()> onInvertMaskRequested;
    util::Signaler<void(ctrl::ToolType)> onToolChanged;
    // the Mask tool is painting THIS LAYER's own matte (see ToolViewModel::maskPaintingLayerMask)
    bool maskPaintingLayerMask() const;
    bool maskToolActive() const;
    core::TimeKeyType maskLane() const;
    core::FilterMaskChannel maskChannel() const;
    // the mask editor's TARGET changed (a filter card's Paint switches it without switching tools)
    util::Signaler<void()> onMaskTargetChanged;
    util::Signaler<void(ctrl::ToolType)> onFinalizeTool;
    // The FFD tool's automatic show/hide of the mesh writes the window's view
    // setting; the viewport consumes it through this signal, exactly as the header's
    // own ViewSettingsModel does for the user's clicks.
    util::Signaler<void(const MainViewSetting&)> onViewSettingChanged;

private:
    MainViewSetting& viewSetting() { return mViaPoint.mainViewSetting(); }
    const MainViewSetting& viewSetting() const { return mViaPoint.mainViewSetting(); }

    // The single route a tool changes by, whatever asked: the scene's toolbox, a filter
    // card's "paint mask", or a key binding.
    void onModelToolChanged(int aOldType, int aNewType);
    // Hand the active tool's param to the driver. Guarded by the tool the driver was
    // last given, because Driver::updateParam ASSERTS that the editor matches its own
    // tool and a mismatched call is a crash rather than a no-op.
    void pushParam();
    void onParamUpdated();
    // Re-read the Bone editor's selected-bone state into the panel model.
    void syncWeightsState();
    void onWeightsAction(int aAction);

    // "Show mesh" while the FFD tool is up (generalsettings/tools/autoshowmesh). The
    // flag is what keeps the tool from taking ownership of a mesh the USER turned on.
    void showHideMesh(bool aShow);

    ViaPoint& mViaPoint;
    const QSize mSizeHint;
    ctrl::Driver* mDriver;
    // The tool the driver's editor was created for; ctrl::ToolType_TERM means none.
    ctrl::ToolType mDriverTool;
    bool meshFromFunction;
    ToolViewModel* mModel;
    QmlTheme* mTheme;
    QQuickWidget* mQuick;
    util::SlotId mWeightsSlot;
};

} // namespace gui

#endif // GUI_TOOLWIDGET_H
