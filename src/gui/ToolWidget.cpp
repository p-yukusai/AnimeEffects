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
    mMeshPanel() {
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

    updateGeometry();

    // key binding
    if (mViaPoint.keyCommandMap()) {
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectCursor");
            if (key)
                key->invoker = [=]() { this->mModePanel->pushButton(ctrl::ToolType_Cursor); };
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectSRT");
            if (key)
                key->invoker = [=]() { this->mModePanel->pushButton(ctrl::ToolType_SRT); };
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectBone");
            if (key)
                key->invoker = [=]() { this->mModePanel->pushButton(ctrl::ToolType_Bone); };
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectPose");
            if (key)
                key->invoker = [=]() { this->mModePanel->pushButton(ctrl::ToolType_Pose); };
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectMesh");
            if (key)
                key->invoker = [=]() { this->mModePanel->pushButton(ctrl::ToolType_Mesh); };
        }
        {
            auto key = mViaPoint.keyCommandMap()->get("SelectFFD");
            if (key)
                key->invoker = [=]() { this->mModePanel->pushButton(ctrl::ToolType_FFD); };
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
        }
        onParamUpdated(false);

        onVisualUpdated();
    }
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
}

} // namespace gui
