#ifndef GUI_MAINMENUBAR_H
#define GUI_MAINMENUBAR_H

#include <QCheckBox>
#include <QSpinBox>
#include "core/Project.h"
#include "ctrl/VideoFormat.h"
#include "gui/EasyDialog.h"
#include "gui/GUIResources.h"
#include "qprocess.h"

namespace gui {
class MainWindow;
}
namespace gui {
class ViaPoint;
}
namespace gui {

//-------------------------------------------------------------------------------------------------
class MainMenuBar: public QMenuBar {
    Q_OBJECT
public:
    MainMenuBar(MainWindow& aMainWindow, ViaPoint& aViaPoint, GUIResources& aGUIResources, QWidget* aParent);
    void setProject(core::Project* aProject);
    void setShowResourceWindow(bool aShow) const;
    QStringList recentfiles;

public:
    // signals
    util::Signaler<void()> onVisualUpdated;
    util::Signaler<void()> onProjectAttributeUpdated;
    util::Signaler<void()> onTimeFormatChanged;
    QString system_info = "Unknown";

private:
    QScopedPointer<QProcess> mProcess;
    void loadVideoFormats();
    void onProjectSettingsTriggered();

    ViaPoint& mViaPoint;
    core::Project* mProject;
    QVector<QAction*> mProjectActions;
    QList<ctrl::VideoFormat> mVideoFormats;
    GUIResources& mGUIResources;
};

//-------------------------------------------------------------------------------------------------
class ProjectSettingDialog: public EasyDialog {
    Q_OBJECT
public:
    ProjectSettingDialog(ViaPoint& aViaPoint, core::Project& aProject, QWidget* aParent);
    QSize canvasSize() const { return {mWidthBox->value(), mHeightBox->value()}; }
    int maxFrame() const { return mMaxFrameBox->value(); }
    bool loop() const { return mLoopBox->isChecked(); }
    int fps() const { return mFPSBox->value(); }

private:
    bool confirmMaxFrameUpdating(int aNewMaxFrame) const;
    ViaPoint& mViaPoint;
    core::Project& mProject;
    QSpinBox* mWidthBox;
    QSpinBox* mHeightBox;
    QSpinBox* mMaxFrameBox;
    QCheckBox* mLoopBox;
    QSpinBox* mFPSBox;
};


} // namespace gui

#endif // GUI_MAINMENUBAR_H
