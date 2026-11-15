#include "gui/ToolWidget.h"
#include "gui/KeyCommandMap.h"

namespace gui {

ToolWidget::ToolWidget(
    ViaPoint& aViaPoint,
    GUIResources& aResources,
    KeyCommandMap& aKeyCommandMap,
    const QSize& aSizeHint,
    QWidget* aParent
):
    QWidget(aParent),
    mViaPoint(aViaPoint),
    mResources(aResources),
    mKeyCommandMap(aKeyCommandMap),
    mSizeHint(aSizeHint),
    mViewPanel(),
    mModePanel(),
    mDriver(),
    mToolType(ctrl::ToolType_TERM),
    mSRTPanel(),
    mFFDPanel(),
    mBonePanel(),
    mPosePanel(),
    mMeshPanel(),
    mMaskPanel() {
    setObjectName("toolWidget"); // QSS id for the dock surface
    createViewPanel();

    createModePanel();
    setPanelActivity(false);

    // the dock width is the default and the max: narrower would cramp the
    // buttons, wider adds no value (the sizeHint width is the panel's design
    // width, divisible by 4)
    setMaximumWidth(mSizeHint.width());

    mSRTPanel = new tool::SRTPanel(this, mResources);
    mSRTPanel->onParamUpdated.connect(this, &ToolWidget::onParamUpdated);
    mSRTPanel->hide();

    mFFDPanel = new tool::FFDPanel(this, mResources);
    mFFDPanel->onParamUpdated.connect(this, &ToolWidget::onParamUpdated);
    mFFDPanel->hide();

    mBonePanel = new tool::BonePanel(this, mResources);
    mBonePanel->onParamUpdated.connect(this, &ToolWidget::onParamUpdated);
    mBonePanel->hide();

    mPosePanel = new tool::PosePanel(this, mResources);
    mPosePanel->onParamUpdated.connect(this, &ToolWidget::onParamUpdated);
    mPosePanel->hide();

    mMeshPanel = new tool::MeshPanel(this, mResources);
    mMeshPanel->onParamUpdated.connect(this, &ToolWidget::onParamUpdated);
    mMeshPanel->hide();

    mMaskPanel = new tool::MaskPanel(this, mResources);
    mMaskPanel->onParamUpdated.connect(this, &ToolWidget::onParamUpdated);
    mMaskPanel->onClearRequested.connect([this]() {
        if (mDriver)
            mDriver->clearMask();
    });
    mMaskPanel->hide();

    updateGeometry();

    // key binding
    if (mViaPoint.keyCommandMap()) {
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectCursor");
            if (key)
                key->addListener([=]() { this->mModePanel->pushButton(ctrl::ToolType_Cursor); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectSRT");
            if (key)
                key->addListener([=]() { this->mModePanel->pushButton(ctrl::ToolType_SRT); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectBone");
            if (key)
                key->addListener([=]() { this->mModePanel->pushButton(ctrl::ToolType_Bone); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectPose");
            if (key)
                key->addListener([=]() { this->mModePanel->pushButton(ctrl::ToolType_Pose); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectMesh");
            if (key)
                key->addListener([=]() { this->mModePanel->pushButton(ctrl::ToolType_Mesh); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("BrushSizeIncrease");
            if (key)
                key->addListener([=]() { this->adjustBrush(+1, 0); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("BrushSizeDecrease");
            if (key)
                key->addListener([=]() { this->adjustBrush(-1, 0); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("BrushHardnessIncrease");
            if (key)
                key->addListener([=]() { this->adjustBrush(0, +1); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("BrushHardnessDecrease");
            if (key)
                key->addListener([=]() { this->adjustBrush(0, -1); });
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectFFD");
            if (key)
                key->addListener([=]() { this->mModePanel->pushButton(ctrl::ToolType_FFD); });
        }
    }
}

void ToolWidget::setDriver(ctrl::Driver* aDriver) {
    mDriver = aDriver;

    if (mDriver) {
        mDriver->setTool(mToolType);
        setPanelActivity(true);
    } else {
        setPanelActivity(false);
    }
}

void ToolWidget::createViewPanel() {
    if (mViewPanel)
        delete mViewPanel;
    mViewPanel = new tool::ViewPanel(this, mResources, tr("View Settings"));

    mViewPanel->addButton("cube", true, tr("Show mesh"), [=](bool aChecked) {
        this->viewSetting().showLayerMesh = aChecked;
        this->onViewSettingChanged(this->viewSetting());
    });
    mViewPanel->addButton("image", true, tr("Do not draw outside the frame"), [=](bool aChecked) {
        this->viewSetting().cutImagesByTheFrame = aChecked;
        this->onViewSettingChanged(this->viewSetting());
    });

    QString _rotateViewAntiClockwiseKeyBindingText =
        this->mKeyCommandMap.get("RotateCanvas15AntiClockwise")->binding.text();
    mViewPanel->addButton(
        "arrow-counter-clockwise",
        false,
        tr("Rotate the canvas counterclockwise (%1)").arg(_rotateViewAntiClockwiseKeyBindingText),
        [=](bool) {
            this->viewSetting().rotateViewACW = true;
            this->onViewSettingChanged(this->viewSetting());
            this->viewSetting().rotateViewACW = false;
        }
    );

    QString _rotateResetKeyBindingText = this->mKeyCommandMap.get("ResetCanvasAngle")->binding.text();
    mViewPanel->addButton(
        "dot-outline",
        false,
        tr("Reset rotation of the canvas (%1)").arg(_rotateResetKeyBindingText),
        [=](bool) {
            this->viewSetting().resetRotateView = true;
            this->onViewSettingChanged(this->viewSetting());
            this->viewSetting().resetRotateView = false;
        }
    );

    QString _rotateViewClockwiseKeyBindingText = this->mKeyCommandMap.get("RotateCanvas15Clockwise")->binding.text();
    mViewPanel->addButton(
        "arrow-clockwise",
        false,
        tr("Rotate the canvas clockwise (%1)").arg(_rotateViewClockwiseKeyBindingText),
        [=](bool) {
            this->viewSetting().rotateViewCW = true;
            this->onViewSettingChanged(this->viewSetting());
            this->viewSetting().rotateViewCW = false;
        }
    );

    mViewPanel->addButton("flip-horizontal", true, tr("Flip canvas"), [=](bool aChecked) {
        this->viewSetting().flipCanvas = aChecked;
        this->onViewSettingChanged(this->viewSetting());
    });

    // THE GRAPH TOGGLE IS GONE. It showed/hid the panel this viewport used to draw
    // over the artwork (a Dear ImGui overlay); the panel is the TIMELINE DOCK's Graph
    // tab now (design section 5), and the tab is the control - a second switch for the
    // same thing would be a state the tab cannot show. ctrl::GraphEditor::setVisible is
    // therefore never true in the application (the Driver's cursor routing reads it as
    // "the viewport's overlay is up"), and the docked page draws whether or not it is.
    // The toggle's accessible name went with it; its coverage is the dock's probe
    // (scripts/gui/probe_graph_ink.sh).
}

void ToolWidget::createModePanel() {
    if (mModePanel)
        delete mModePanel;
    mModePanel = new tool::ModePanel(this, mResources, [=](ctrl::ToolType aType, bool aChecked) {
        this->onModePanelPushed(aType, aChecked);
    });
}

void ToolWidget::setPanelActivity(bool aIsActive) {
    if (mModePanel) {
        for (int i = 0; i < ctrl::ToolType_TERM; ++i) {
            setButtonActivity((ctrl::ToolType)i, aIsActive);
        }
    }
}

void ToolWidget::setButtonActivity(ctrl::ToolType aType, bool aIsActive) {
    if (mModePanel) {
        if (aIsActive) {
            mModePanel->button(aType)->setEnabled(true);
        } else {
            if (mModePanel->button(aType)->isChecked()) {
                mModePanel->button(aType)->setChecked(false);
            }
            mModePanel->button(aType)->setEnabled(false);
        }
    }
}

void ToolWidget::onModePanelPushed(ctrl::ToolType aType, bool aChecked) {
    if (mDriver && mToolType != ctrl::ToolType_TERM) {
        onFinalizeTool(mToolType);
    }

    mToolType = aChecked ? aType : ctrl::ToolType_TERM;

    if (mDriver) {
        mDriver->setTool(mToolType);

        onToolChanged(mToolType);

        mSRTPanel->hide();
        mFFDPanel->hide();
        mBonePanel->hide();
        mPosePanel->hide();
        mMeshPanel->hide();
        mMaskPanel->hide();

        QSettings settings;
        auto autoshowmesh = settings.value("generalsettings/tools/autoshowmesh");
        if (autoshowmesh.isValid() && autoshowmesh.toBool()) {
            if (mToolType != ctrl::ToolType_FFD) {
                if (meshFromFunction) {
                    meshFromFunction = false;
                    showHideMesh(false);
                }
            }
        }

        if (mToolType == ctrl::ToolType_SRT) {
            mSRTPanel->show();
        } else if (mToolType == ctrl::ToolType_Bone) {
            mBonePanel->show();
        } else if (mToolType == ctrl::ToolType_Pose) {
            mPosePanel->show();
        } else if (mToolType == ctrl::ToolType_FFD) {
            if (autoshowmesh.isValid() && autoshowmesh.toBool()) {
                showHideMesh(true);
            }
            mFFDPanel->show();
        } else if (mToolType == ctrl::ToolType_Mesh) {
            mMeshPanel->show();
        } else if (mToolType == ctrl::ToolType_Mask) {
            mMaskPanel->show();
        }
        onParamUpdated(false);

        onVisualUpdated();
    }
}

void ToolWidget::beginMaskPaint(core::TimeKeyType aLane, core::FilterMaskChannel aChannel) {
    if (!mMaskPanel)
        return;
    // the panel is the authority for lane and channel; select them, then press the
    // tool button so the whole switch goes through the normal route
    mMaskPanel->setLaneAndChannel(aLane, aChannel);
    mModePanel->pushButton(ctrl::ToolType_Mask);
    // ...and PUSH THE PARAM AGAIN: onParamUpdated only forwards while the Mask tool is
    // already current, so a panel filled in BEFORE the switch leaves the freshly created
    // editor on the default parameter - measured as a stroke that painted nothing at all
    // (the editor still thought it was writing the Wave filter's mask, which this layer
    // does not have).
    onParamUpdated(false);
}

void ToolWidget::beginLayerMaskPaint() {
    if (!mMaskPanel)
        return;
    mMaskPanel->setLayerMask();
    mModePanel->pushButton(ctrl::ToolType_Mask);
    onParamUpdated(false);   // see beginMaskPaint: the editor is created by the switch
}

void ToolWidget::adjustBrush(int aSizeSteps, int aHardnessSteps) {
    // the ACTIVE tool's brush: the same rows are shared, but each panel owns its own
    // BrushParam, so the adjustment has to go to the one on screen
    tool::BrushPanel* brush = nullptr;
    if (mToolType == ctrl::ToolType_FFD)
        brush = mFFDPanel->brushPanel();
    else if (mToolType == ctrl::ToolType_Bone)
        brush = mBonePanel->brushPanel();
    else if (mToolType == ctrl::ToolType_Pose)
        brush = mPosePanel->brushPanel();
    else if (mToolType == ctrl::ToolType_Mask)
        brush = mMaskPanel->brushPanel();
    if (!brush)
        return;

    brush->adjustSize(aSizeSteps);
    brush->adjustHardness(aHardnessSteps);
    onParamUpdated(false);
}

void ToolWidget::setMaskValue(float aValue) {
    if (!mMaskPanel || mToolType != ctrl::ToolType_Mask)
        return;
    mMaskPanel->setValue(aValue);
}

void ToolWidget::onParamUpdated(bool aLayoutChanged) {
    if (mDriver) {
        if (aLayoutChanged) {
            updateGeometry();
        }

        if (mToolType == ctrl::ToolType_SRT) {
            mDriver->updateParam(mSRTPanel->param());
            onVisualUpdated();
        } else if (mToolType == ctrl::ToolType_Bone) {
            mDriver->updateParam(mBonePanel->param());
            onVisualUpdated();
        } else if (mToolType == ctrl::ToolType_Pose) {
            mDriver->updateParam(mPosePanel->param());
            onVisualUpdated();
        } else if (mToolType == ctrl::ToolType_FFD) {
            mDriver->updateParam(mFFDPanel->param());
            onVisualUpdated();
        } else if (mToolType == ctrl::ToolType_Mesh) {
            mDriver->updateParam(mMeshPanel->param());
            onVisualUpdated();
        } else if (mToolType == ctrl::ToolType_Mask) {
            mDriver->updateParam(mMaskPanel->param());
            onVisualUpdated();
        }
    }
}

void ToolWidget::resizeEvent(QResizeEvent* aEvent) {
    QWidget::resizeEvent(aEvent);
    updateGeometry();
}

void ToolWidget::updateGeometry() {
    const int width = this->size().width();
    int height = 0;

    height = mViewPanel->updateGeometry(QPoint(3, height), width);

    height = mModePanel->updateGeometry(QPoint(3, height), width);

    mSRTPanel->updateGeometry(QPoint(3, height), width);
    mBonePanel->updateGeometry(QPoint(3, height), width);
    mPosePanel->updateGeometry(QPoint(3, height), width);
    mFFDPanel->updateGeometry(QPoint(3, height), width);
    mMeshPanel->updateGeometry(QPoint(3, height), width);
    mMaskPanel->updateGeometry(QPoint(3, height), width);
}

} // namespace gui
