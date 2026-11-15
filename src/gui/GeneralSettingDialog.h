#ifndef GUI_GENERALSETTINGDIALOG_H
#define GUI_GENERALSETTINGDIALOG_H

#include "gui/GUIResources.h"
#include <QCheckBox>
#include <QFormLayout>
#include "gui/EasyDialog.h"
#include "gui/MouseSetting.h"
#include <QSpinBox>
#include <QTabWidget>
#include <QComboBox>
#include <QButtonGroup>

namespace gui {

class ViaPoint;

class GeneralSettingDialog: public EasyDialog {
    Q_OBJECT
public:
    GeneralSettingDialog(GUIResources& aGUIResources, ViaPoint& aViaPoint, QWidget* aParent);
    QFormLayout* createTab(const QString& aTitle, QFormLayout* aForm);
    void selectTab(int aIndex) {
        // General - 0 ; QoL - 1 ; FFmpeg - 2 ; Keyboard - 3 ; Mouse - 4
        mTabs->setCurrentIndex(aIndex);
    }
    bool easingHasChanged();
    bool rangeHasChanged();
    bool languageHasChanged();
    bool timeFormatHasChanged();
    bool autoSaveHasChanged();
    bool autoSaveDelayHasChanged();
    bool autoFFmpegHasChanged();
    bool resIDCheckHasChanged();
    bool themeHasChanged();
    bool donationHasChanged();
    bool forceSolverLoadHasChanged();
    bool ignoreWarningsHasChanged();
    bool cbCopyHasChanged();
    bool keyDelayHasChanged();
    bool mouseHasChanged();
    QString theme();
    theme::AccentColor accent();
    bool accentHasChanged();
private:
    void saveSettings();

    QTabWidget* mTabs;

    int mInitialLanguageIndex;
    QComboBox* mLanguageBox;

    void mResetRecents();
    QPushButton* mResetButton;

    int mInitialEasingIndex;
    QComboBox* mEasingBox;

    int mInitialRangeIndex;
    QComboBox* mRangeBox;

    bool bDonationAllowed;
    QCheckBox* mDonationAllowed;

    bool bForceSolverLoad;
    QCheckBox* mForceSolverLoad;

    bool bIgnoreWarnings;
    QCheckBox* mIgnoreWarnings;

    bool bAutoSave;
    QCheckBox* mAutoSave;

    int mAutoSaveDelay;
    QSpinBox* mAutoSaveDelayBox;

    bool mAutoFFmpegCheck;
    QCheckBox* mAutoFFmpegBox;

    bool bResIDCheck;
    QCheckBox* bResIDBox;

    bool bAutoCbCopy;
    QCheckBox* mAutoCbCopy;

    int mInitialTimeFormatIndex;
    QComboBox* mTimeFormatBox;

    theme::AccentColor mInitialAccent;
    QButtonGroup* mAccentGroup;
    QString mInitialThemeKey;
    QButtonGroup* mThemeGroup;

    int mKeyDelay;
    QSpinBox* mKeyDelayBox;

    bool bAutoShowMesh;
    QCheckBox* mAutoShowMesh;

    MouseSetting mInitialMouseValues;
    QCheckBox* mInvertMainViewScalingBox;
    QCheckBox* mInvertTimeLineScalingBox;
    QCheckBox* mMiddleMouseMoveCanvas;

    GUIResources& mGUIResources;
    ViaPoint& mViaPoint;
};
} // namespace gui

#endif // GUI_GENERALSETTINGDIALOG_H
