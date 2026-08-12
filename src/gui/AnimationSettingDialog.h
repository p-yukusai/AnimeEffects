#ifndef GUI_ANIMATIONSETTINGDIALOG_H
#define GUI_ANIMATIONSETTINGDIALOG_H

#include <QCheckBox>
#include <QSpinBox>
#include "gui/EasyDialog.h"
#include "core/Project.h"

namespace gui {

// Dialog behind the playback bar's options button. Frames per second and the
// maximum frame count are project attributes (they affect export); the loop
// flag is the live playback loop.
class AnimationSettingDialog: public EasyDialog {
    Q_OBJECT
public:
    AnimationSettingDialog(core::Project& aProject, bool aCurLoop, QWidget* aParent);

    bool isLoopChecked() const { return mLoopBox->isChecked(); }
    int maxFrame() const { return mMaxFrameBox->value(); }
    int fps() const { return mFPSBox->value(); }

private:
    core::Project& mProject;
    QCheckBox* mLoopBox;
    QSpinBox* mMaxFrameBox;
    QSpinBox* mFPSBox;
};

} // namespace gui

#endif // GUI_ANIMATIONSETTINGDIALOG_H
