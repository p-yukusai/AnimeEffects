#include "gui/AnimationSettingDialog.h"
#include "core/ObjectNodeUtil.h"
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>

namespace gui {

AnimationSettingDialog::AnimationSettingDialog(core::Project& aProject, bool aCurLoop, QWidget* aParent):
    EasyDialog(tr("Animation settings"), aParent),
    mProject(aProject),
    mLoopBox(nullptr),
    mMaxFrameBox(nullptr),
    mFPSBox(nullptr) {
    const int curMaxFrame = aProject.attribute().maxFrame();
    const int curFPS = aProject.attribute().fps();

    auto form = new QFormLayout();
    form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignRight);

    {
        auto layout = new QHBoxLayout();
        mLoopBox = new QCheckBox();
        mLoopBox->setChecked(aCurLoop);
        layout->addWidget(mLoopBox);
        form->addRow(tr("Loop animation :"), layout);
    }
    {
        auto layout = new QHBoxLayout();
        mMaxFrameBox = new QSpinBox();
        mMaxFrameBox->setRange(1, 0x7fffffff);
        mMaxFrameBox->setValue(curMaxFrame);
        layout->addWidget(mMaxFrameBox);
        form->addRow(tr("Maximum frame count :"), layout);
    }
    {
        auto layout = new QHBoxLayout();
        mFPSBox = new QSpinBox();
        mFPSBox->setRange(1, 0x7fffffff);
        mFPSBox->setValue(curFPS);
        layout->addWidget(mFPSBox);
        form->addRow(tr("Frames per second :"), layout);
    }

    auto group = new QGroupBox(tr("Parameters"));
    group->setLayout(form);
    this->setMainWidget(group);

    this->setOkCancel([=](int aIndex) -> bool {
        if (aIndex == 0) {
            const int newMaxFrame = this->mMaxFrameBox->value();
            if (newMaxFrame < mProject.attribute().maxFrame() &&
                core::ObjectNodeUtil::thereAreSomeKeysExceedingFrame(mProject.objectTree().topNode(), newMaxFrame)) {
                QMessageBox::warning(nullptr, tr("Operation Error"),
                    tr("Frame value cannot be set.") + "\n" +
                    tr("One or more keys exceed the specified frame value."));
                return false;
            }
        }
        return true;
    });
    this->fixSize();
}

} // namespace gui
