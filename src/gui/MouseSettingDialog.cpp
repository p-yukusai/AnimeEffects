#include <QGroupBox>
#include <QFormLayout>
#include "gui/MouseSettingDialog.h"

namespace gui {

//-------------------------------------------------------------------------------------------------
MouseSettingDialog::MouseSettingDialog(ViaPoint& aViaPoint, QWidget* aParent):
    EasyDialog(tr("Mouse Settings"), aParent),
    mViaPoint(aViaPoint),
    mInitialValues(),
    mInvertMainViewScalingBox(),
    mInvertTimeLineScalingBox(),
    mMiddleMouseMoveCanvas() {
    // read current settings
    mInitialValues.load();

    auto form = new QFormLayout();
    form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignRight);

    // create inner widgets
    {
        mInvertMainViewScalingBox = new QCheckBox();
        mInvertMainViewScalingBox->setChecked(mInitialValues.invertMainViewScaling);
        form->addRow(tr("Inverse canvas scrolling"), mInvertMainViewScalingBox);

        mInvertTimeLineScalingBox = new QCheckBox();
        mInvertTimeLineScalingBox->setChecked(mInitialValues.invertTimeLineScaling);
        form->addRow(tr("Inverse timeline scrolling"), mInvertTimeLineScalingBox);

        mMiddleMouseMoveCanvas = new QCheckBox();
        mMiddleMouseMoveCanvas->setChecked(mInitialValues.middleMouseMoveCanvas);
        form->addRow(tr("Middle mouse moves canvas"), mMiddleMouseMoveCanvas);
    }

    this->setMainLayout(form);

    this->setOkCancel([=](int aResult) -> bool {
        if (aResult == 0) {
            this->saveSettings();
        }
        return true;
    });
}

void MouseSettingDialog::saveSettings() {
    MouseSetting newValues;
    newValues.invertMainViewScaling = mInvertMainViewScalingBox->isChecked();
    newValues.invertTimeLineScaling = mInvertTimeLineScalingBox->isChecked();
    newValues.middleMouseMoveCanvas = mMiddleMouseMoveCanvas->isChecked();

    if (mInitialValues != newValues) {
        newValues.save();
        mViaPoint.mouseSetting() = newValues;
    }
}

} // namespace gui
