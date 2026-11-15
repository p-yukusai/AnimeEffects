#ifndef GUI_TOOLWIDGET_H
#define GUI_TOOLWIDGET_H

#include <QWidget>
#include <QResizeEvent>
#include "util/Signaler.h"
#include "ctrl/Driver.h"
#include "ctrl/FFDParam.h"
#include "gui/GUIResources.h"
#include "gui/MainDisplayMode.h"
#include "gui/MainViewSetting.h"
#include "gui/ViaPoint.h"
#include "gui/tool/tool_ViewPanel.h"
#include "gui/tool/tool_ModePanel.h"
#include "gui/tool/tool_SRTPanel.h"
#include "gui/tool/tool_FFDPanel.h"
#include "gui/tool/tool_BonePanel.h"
#include "gui/tool/tool_PosePanel.h"
#include "gui/tool/tool_MeshPanel.h"
#include "gui/tool/tool_MaskPanel.h"

namespace gui {

class ToolWidget: public QWidget {
    Q_OBJECT
public:
    ToolWidget(
        ViaPoint& aViaPoint,
        GUIResources& aResources,
        KeyCommandMap& aKeyCommandMap,
        const QSize& aSizeHint,
        QWidget* aParent
    );

    void setDriver(ctrl::Driver* aDriver);
    // the temporary eyedropper's value, from the viewport through MainWindow
    void setMaskValue(float aValue);
    // one brush adjustment step (the [ ] keys and the Alt+right-drag gesture):
    // positive steps enlarge/harden, negative shrink/soften
    void adjustBrush(int aSizeSteps, int aHardnessSteps);
    // A filter card asked to paint a mask: switch to the Mask tool with that lane and
    // channel already selected, so the artist paints immediately.
    void beginMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel);
    // the node's own matte: same tool, same brush, a target that is not a filter lane
    void beginLayerMaskPaint();

    util::Signaler<void()> onVisualUpdated;
    util::Signaler<void(ctrl::ToolType)> onToolChanged;
    util::Signaler<void(ctrl::ToolType)> onFinalizeTool;
    util::Signaler<void(const MainViewSetting&)> onViewSettingChanged;

private:
    virtual QSize sizeHint() const { return mSizeHint; }
    virtual void resizeEvent(QResizeEvent* aEvent);

    MainViewSetting& viewSetting() { return mViaPoint.mainViewSetting(); }
    const MainViewSetting& viewSetting() const { return mViaPoint.mainViewSetting(); }
    void createViewPanel();
    void createModePanel();
    void setPanelActivity(bool aIsActive);
    void setButtonActivity(ctrl::ToolType aType, bool aIsActive);
    void updateGeometry();
    bool meshFromFunction = false;
    void showHideMesh(bool aShow) {
        if (this->viewSetting().showLayerMesh && aShow && !meshFromFunction) {
            return;
        };
        if (aShow) {
            meshFromFunction = true;
        }
        mViewPanel->button(0)->setChecked(aShow);
        this->viewSetting().showLayerMesh = aShow;
        this->onViewSettingChanged(this->viewSetting());
    }
    void onModePanelPushed(ctrl::ToolType, bool);
    void onParamUpdated(bool aLayoutChanged);

    ViaPoint& mViaPoint;
    GUIResources& mResources;
    KeyCommandMap& mKeyCommandMap;
    const QSize mSizeHint;
    tool::ViewPanel* mViewPanel;
    tool::ModePanel* mModePanel;
    ctrl::Driver* mDriver;
    ctrl::ToolType mToolType;

    tool::SRTPanel* mSRTPanel;
    tool::FFDPanel* mFFDPanel;
    tool::BonePanel* mBonePanel;
    tool::PosePanel* mPosePanel;
    tool::MeshPanel* mMeshPanel;
    tool::MaskPanel* mMaskPanel;
};

} // namespace gui

#endif // GUI_TOOLWIDGET_H
