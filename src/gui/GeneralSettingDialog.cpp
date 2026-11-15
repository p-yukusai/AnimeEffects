#include <QApplication>
#include <qstandardpaths.h>
#include <algorithm>
#include "MainWindow.h"
#include "gui/GeneralSettingDialog.h"
#include "gui/ViaPoint.h"
#include "util/NetworkUtil.h"
#include "util/Easing.h"
#include <QComboBox>
#include <QHBoxLayout>
#include <QPixmap>
#include "gui/theme/Colors.h"

namespace {

constexpr int kLanguageTypeCount = 7;
// Timeline format picker order. Frames_From1 is dropped from the UI (with
// frames natively 1-indexed it is identical to Frames_From0), but the enum
// value is kept so previously saved settings still load — they map to the
// "Frame number" entry.
const core::TimeFormatType kTimeFormatOrder[] = {
    core::TimeFormatType_Frames_From0,       // Frame number
    core::TimeFormatType_Relative_FPS,       // Seconds (frame/fps)
    core::TimeFormatType_Seconds_Frames,     // Seconds : Frame
    core::TimeFormatType_Timecode_SMPTE,     // HH:MM:SS+FF
    core::TimeFormatType_Timecode_HHMMSSmmm, // HH:MM:SS:mmm
};
const int kTimeFormatOrderCount = int(sizeof(kTimeFormatOrder) / sizeof(kTimeFormatOrder[0]));

core::TimeFormatType timeFormatTypeAt(int aComboIndex) {
    if (0 <= aComboIndex && aComboIndex < kTimeFormatOrderCount)
        return kTimeFormatOrder[aComboIndex];
    return core::TimeFormatType_Frames_From0;
}

int timeFormatComboIndexOf(core::TimeFormatType aType) {
    if (aType == core::TimeFormatType_Frames_From1) // alias of Frames_From0
        return timeFormatComboIndexOf(core::TimeFormatType_Frames_From0);
    for (int i = 0; i < kTimeFormatOrderCount; ++i) {
        if (kTimeFormatOrder[i] == aType)
            return i;
    }
    return 0;
}
// The default-easing combo shows every DisplayOrder entry except Type_Custom
// (a global default carries no bezier control points); derived from the
// shared table so it can never drift from the picker order.
const int kEasingTypeCount = util::Easing::displayCount() - 1;
const int kRangeTypeCount = 3;

int languageToIndex(const QString& aLanguage) {
    if (aLanguage == "Auto")
        return 0;
    if (aLanguage == "English")
        return 1;
    if (aLanguage == "Japanese")
        return 2;
    if (aLanguage == "Chinese")
        return 3;
    if (aLanguage == "Spanish")
        return 4;
    if (aLanguage == "Russian")
        return 5;
    if (aLanguage == "Korean")
        return 6;
    return 0;
}

int rangeToIndex(const QString& aRange) {
    if (aRange == "In")
        return 0;
    if (aRange == "Out")
        return 1;
    if (aRange == "All")
        return 2;
    return 2;
    // Default range is InOut, it is referenced as "All" by Hidefuku
}

QString indexToEasing(int aIndex) {
    // The combo lists the shared display order minus Custom (a default easing
    // carries no bezier control points); an out-of-range index falls back to
    // Linear. The label comes from getTypeName (one translation source).
    util::Easing::Type type = util::Easing::typeAtDisplayIndex(aIndex);
    if (type == util::Easing::Type_Custom)
        type = util::Easing::Type_Linear;
    return util::Easing::getTypeName(type);
}

QString indexToRange(int aRange, bool translated = true) {
    if (translated) {
        switch (aRange) {
        case 0:
            return QCoreApplication::translate("GeneralSettingsDialog", "In");
        case 1:
            return QCoreApplication::translate("GeneralSettingsDialog", "Out");
        case 3:
            return QCoreApplication::translate("GeneralSettingsDialog", "All");
        default:
            return QCoreApplication::translate("GeneralSettingsDialog", "All");
        }
    }
    switch (aRange) {
    case 0:
        return QString("In");
    case 1:
        return QString("Out");
    case 3:
        return QString("All");
    default:
        return QString("All");
    }
}

QString indexToLanguage(int aIndex, bool translated = true) {
    if (translated) {
        switch (aIndex) {
        case 0:
            return QCoreApplication::translate("GeneralSettingsDialog", "Auto");
        case 1:
            return QCoreApplication::translate("GeneralSettingsDialog", "English");
        case 2:
            return QCoreApplication::translate("GeneralSettingsDialog", "Japanese");
        case 3:
            return QCoreApplication::translate("GeneralSettingsDialog", "Chinese");
        case 4:
            return QCoreApplication::translate("GeneralSettingsDialog", "Spanish");
        case 5:
            return QCoreApplication::translate("GeneralSettingsDialog", "Russian");
        case 6:
            return QCoreApplication::translate("GeneralSettingsDialog", "Korean");
        default:
            return QCoreApplication::translate("GeneralSettingsDialog", "Auto");
        }
    }
    switch (aIndex) {
    case 0:
        return {"Auto"};
    case 1:
        return {"English"};
    case 2:
        return {"Japanese"};
    case 3:
        return {"Chinese"};
    case 4:
        return {"Spanish"};
    case 5:
        return {"Russian"};
    case 6:
        return {"Korean"};
    default:
        return {"Auto"};
    }
}
} // namespace

QString indexToTimeFormat(int aComboIndex) {
    switch (timeFormatTypeAt(aComboIndex)) {
    case core::TimeFormatType_Frames_From0:
        return QCoreApplication::translate("GeneralSettingsDialog", "Frame number");
    case core::TimeFormatType_Relative_FPS:
        // Shows the frame's exact time in seconds (frame / fps), fractional.
        return QCoreApplication::translate("GeneralSettingsDialog", "Seconds");
    case core::TimeFormatType_Seconds_Frames:
        // SS+FF: whole seconds + frame-within-second, at the project fps.
        return QCoreApplication::translate("GeneralSettingsDialog", "Seconds + Frame (SS+FF)");
    case core::TimeFormatType_Timecode_SMPTE:
        return QCoreApplication::translate("GeneralSettingsDialog", "Timecode (SMPTE) (HH:MM:SS+FF)");
    case core::TimeFormatType_Timecode_HHMMSSmmm:
        return QCoreApplication::translate("GeneralSettingsDialog", "Timecode (HH:MM:SS:mmm)");
    default:
        return "";
    }
}


namespace gui {

GeneralSettingDialog::GeneralSettingDialog(GUIResources& aGUIResources, ViaPoint& aViaPoint, QWidget* aParent):
    EasyDialog(tr("Settings"), aParent),
    mTabs(new QTabWidget(this)),
    mInitialLanguageIndex(),
    mLanguageBox(),
    mInitialTimeFormatIndex(),
    mTimeFormatBox(),
    mInitialAccent(theme::kDefaultAccent),
    mAccentGroup(),
    mInitialThemeKey("dark"),
    mThemeGroup(),
    mInitialMouseValues(),
    mInvertMainViewScalingBox(),
    mInvertTimeLineScalingBox(),
    mMiddleMouseMoveCanvas(),
    mGUIResources(aGUIResources),
    mViaPoint(aViaPoint) {
    // read current settings
    {
        QSettings settings;
        auto language = settings.value("generalsettings/language");

        if (language.isValid()) {
            mInitialLanguageIndex = languageToIndex(language.toString());
        }

        auto easing = settings.value("generalsettings/easing");
        if (easing.isValid()) {
            // Saved as the enum value since the shared-table refactor; older
            // settings stored the canonical name — accept both.
            util::Easing::Type type = (easing.metaType().id() == QMetaType::QString)
                ? util::Easing::easingToEnum(easing.toString())
                : static_cast<util::Easing::Type>(easing.toInt());
            mInitialEasingIndex = util::Easing::displayIndexOf(type);
            if (type == util::Easing::Type_Custom || mInitialEasingIndex < 0 ||
                mInitialEasingIndex >= kEasingTypeCount) {
                mInitialEasingIndex = util::Easing::displayIndexOf(util::Easing::Type_Linear);
            }
        } else {
            mInitialEasingIndex = util::Easing::displayIndexOf(util::Easing::Type_Linear);
        }

        auto range = settings.value("generalsettings/range");
        mInitialRangeIndex = range.isValid() ? rangeToIndex(range.toString()) : rangeToIndex("All");

        auto timeScale = settings.value("generalsettings/ui/timeformat");
        if (timeScale.isValid()) {
            // Stored as the enum value; map it (the legacy Frames_From1
            // included) to a picker index.
            mInitialTimeFormatIndex =
                timeFormatComboIndexOf(static_cast<core::TimeFormatType>(timeScale.toInt()));
        }

        auto theme = settings.value("generalsettings/ui/theme");
        if (theme.isValid()) {
            mInitialThemeKey = theme.toString();
        }

        const QString accentName = settings.value("generalsettings/ui/accent", QString()).toString();
        mInitialAccent = theme::accentFromName(accentName);

        auto donationAllowed = settings.value("generalsettings/ui/donationAllowed");
        bDonationAllowed = donationAllowed.isValid()? donationAllowed.toBool() : true;

        auto forceSolverLoad = settings.value("forceSolverLoad", false);
        bForceSolverLoad = forceSolverLoad.toBool();

        auto ignoreWarnings = settings.value("export_ignore_warnings");
        bIgnoreWarnings = ignoreWarnings.isValid()? ignoreWarnings.toBool(): false;

        auto isAutoSave = settings.value("generalsettings/projects/enableAutosave");
        bAutoSave = isAutoSave.isValid()? isAutoSave.toBool() : true;

        auto isAutoSaveDelay = settings.value("generalsettings/projects/autosaveDelay");
        mAutoSaveDelay = isAutoSaveDelay.isValid() ? std::max(1, isAutoSaveDelay.toInt()) : 5;

        auto isAutoCbCopy = settings.value("generalsettings/keys/autocb");
        bAutoCbCopy = !isAutoCbCopy.isValid()? isAutoCbCopy.toBool() : false;

        auto isAutoFFmpegCheck = settings.value("ffmpeg_check");

        mAutoFFmpegCheck = isAutoFFmpegCheck.isValid()? isAutoFFmpegCheck.toBool() : true;

        auto isResIDCheck = settings.value("res_id_check");
        bResIDCheck = isResIDCheck.isValid()?  isResIDCheck.toBool() : true;

        auto isKeyDelay = settings.value("generalsettings/keybindings/keyDelay");
        mKeyDelay = isKeyDelay.isValid() ? isKeyDelay.toInt() : 125;

        auto isAutoShowMesh = settings.value("generalsettings/tools/autoshowmesh");
        bAutoShowMesh = isAutoShowMesh.isValid()? isAutoShowMesh.toBool() : false;

        mInitialMouseValues.load();
    }

    auto form = new QFormLayout();

    // create inner widgets
    {
        mLanguageBox = new QComboBox();
        for (int i = 0; i < kLanguageTypeCount; ++i) {
            mLanguageBox->addItem(indexToLanguage(i));
        }
        mLanguageBox->setCurrentIndex(mInitialLanguageIndex);
        form->addRow(tr("Language (needs restart)"), mLanguageBox);

        mEasingBox = new QComboBox();
        for (int i = 0; i < kEasingTypeCount; ++i) {
            mEasingBox->addItem(indexToEasing(i));
        }
        mEasingBox->setCurrentIndex(mInitialEasingIndex);
        form->addRow(tr("Default keyframe easing"), mEasingBox);

        mRangeBox = new QComboBox();
        for (int i = 0; i < kRangeTypeCount; ++i) {
            mRangeBox->addItem(indexToRange(i));
        }
        mRangeBox->setCurrentIndex(mInitialRangeIndex);
        form->addRow(tr("Default keyframe range"), mRangeBox);

        mTimeFormatBox = new QComboBox();
        for (int i = 0; i < kTimeFormatOrderCount; ++i) {
            mTimeFormatBox->addItem(indexToTimeFormat(i));
        }
        mTimeFormatBox->setCurrentIndex(mInitialTimeFormatIndex);
        form->addRow(tr("Timeline format"), mTimeFormatBox);


        // Theme: a segmented control on a recessed track; the active choice
        // is an accent-filled pill (no per-segment borders — those read as
        // "connected buttons", which is the dated look).
        mThemeGroup = new QButtonGroup(this);
        mThemeGroup->setExclusive(true);
        {
            const theme::Colors c = theme::Colors::current();
            auto* themeRow = new QWidget();
            themeRow->setAttribute(Qt::WA_StyledBackground, true);
            auto* themeLayout = new QHBoxLayout(themeRow);
            themeLayout->setContentsMargins(2, 2, 2, 2);
            themeLayout->setSpacing(0);
            const QStringList themeList = mGUIResources.themeList();
            const QStringList themeLabels{tr("System"), tr("Light"), tr("Dark")};
            for (int i = 0; i < themeList.size(); ++i) {
                auto* b = new QPushButton(themeLabels.at(i));
                b->setCheckable(true);
                b->setProperty("themeId", themeList.at(i));
                if (themeList.at(i) == mInitialThemeKey) b->setChecked(true);
                mThemeGroup->addButton(b);
                themeLayout->addWidget(b, 1);
            }
            themeRow->setStyleSheet(
                QString("QWidget { background-color: %1; border-radius: 0.5em; }"
                        "QPushButton { background-color: transparent; color: %2; border: none; border-radius: 0.375em; padding: 0.25em 0.875em; }"
                        "QPushButton:hover { background-color: %3; }"
                        "QPushButton:checked { background-color: %4; color: %5; }")
                    .arg(c.recessed.name(), c.icon.name(), c.raised.name(), c.accent.name(), c.accentText.name()));
            form->addRow(tr("Theme"), themeRow);
        }

        // Accent color: a row of square colored boxes. Border on hover, a
        // contrast ring on the active choice. No hue numbers — the user
        // picks the color, not a degree value.
        mAccentGroup = new QButtonGroup(this);
        mAccentGroup->setExclusive(true);
        {
            const theme::Colors c = theme::Colors::current();
            auto* accentRow = new QWidget();
            auto* accentLayout = new QHBoxLayout(accentRow);
            accentLayout->setContentsMargins(0, 0, 0, 0);
            accentLayout->setSpacing(6);
            for (int i = 0; i < theme::kAccentCount; ++i) {
                const theme::AccentColor acc = (theme::AccentColor)i;
                auto* b = new QPushButton();
                b->setCheckable(true);
                b->setProperty("accent", i);
                // Preview the accent hue at a mid lightness (accentSwatch):
                // the fill token itself is theme-tuned (pale in light, deep
                // in dark) and reads poorly as a bare swatch on the dialog
                // background; the swatch shows the recognizable hue.
                const QColor accent = (c.isDark ? theme::Colors::dark(acc) : theme::Colors::light(acc)).accentSwatch;
                b->setStyleSheet(
                    QString("QPushButton { width: 20px; height: 20px; background-color: %1; border: 2px solid transparent; padding: 0; min-height: 0; min-width: 0; }"
                            "QPushButton:hover { border-color: %2; }"
                            "QPushButton:checked { border-color: %3; }")
                        .arg(accent.name(), c.outline.name(), c.text.name()));
                if (acc == mInitialAccent) b->setChecked(true);
                mAccentGroup->addButton(b);
                accentLayout->addWidget(b);
            }
            accentLayout->addStretch();
            form->addRow(tr("Accent color"), accentRow);
        }
    }

    auto projectSaving = new QFormLayout();
    {
        mAutoSave = new QCheckBox();
        mAutoSave->setChecked(bAutoSave);
        projectSaving->addRow(tr("Automatically save your project"), mAutoSave);

        mAutoSaveDelayBox = new QSpinBox();
        mAutoSaveDelayBox->setRange(1, 999); // 0 would make the timer fire continuously
        mAutoSaveDelayBox->setValue(mAutoSaveDelay);
        projectSaving->addRow(tr("Time (in minutes) between autosaves"), mAutoSaveDelayBox);

        mAutoShowMesh = new QCheckBox();
        mAutoShowMesh->setChecked(bAutoShowMesh);
        connect(mAutoShowMesh, &QPushButton::clicked, [=]() {
            QSettings settings;
            settings.setValue("generalsettings/tools/autoshowmesh", mAutoShowMesh->isChecked());
        });
        projectSaving->addRow(tr("Show mesh when selecting FFD"), mAutoShowMesh);

        mAutoCbCopy = new QCheckBox();
        mAutoCbCopy->setChecked(bAutoCbCopy);
        projectSaving->addRow(tr("On copy send keys to the clipboard"), mAutoCbCopy);

        mAutoFFmpegBox = new QCheckBox();
        mAutoFFmpegBox->setChecked(mAutoFFmpegCheck);
        projectSaving->addRow(tr("Always check for FFmpeg on export"), mAutoFFmpegBox);

        bResIDBox = new QCheckBox();
        bResIDBox->setChecked(bResIDCheck);
        projectSaving->addRow(tr("Enforce ID check on asset download"), bResIDBox);

        mIgnoreWarnings = new QCheckBox();
        mIgnoreWarnings->setChecked(bIgnoreWarnings);
        projectSaving->addRow(tr("Ignore export warnings"), mIgnoreWarnings);

        mDonationAllowed = new QCheckBox();
        mDonationAllowed->setChecked(bDonationAllowed);
        projectSaving->addRow(tr("Allow donation menu"), mDonationAllowed);

        mForceSolverLoad = new QCheckBox();
        mForceSolverLoad->setChecked(bForceSolverLoad);
        projectSaving->addRow(tr("Force project to load"), mForceSolverLoad);

        mResetButton = new QPushButton(tr("Reset recent files list"));
        mResetButton->setToolTip(tr("Deletes all project entries from your recents"));
        connect(mResetButton, &QPushButton::clicked, [=]() {
            QSettings settings;
            settings.remove("projectloader/recents");
            MainWindow::showInfoPopup(tr("Success"), tr("All entries have been successfully removed"), "Info");
        });
        projectSaving->addRow(mResetButton);
    }

    auto keybindingSettings = new QFormLayout();
    {
        mKeyDelayBox = new QSpinBox();
        mKeyDelayBox->setRange(0, 10000);
        mKeyDelayBox->setValue(mKeyDelay);
        keybindingSettings->addRow(tr("Global keybind delay (ms)"), mKeyDelayBox);
    }

    auto mouseSettings = new QFormLayout();
    {
        mInvertMainViewScalingBox = new QCheckBox();
        mInvertMainViewScalingBox->setChecked(mInitialMouseValues.invertMainViewScaling);
        mouseSettings->addRow(tr("Inverse canvas scrolling"), mInvertMainViewScalingBox);

        mInvertTimeLineScalingBox = new QCheckBox();
        mInvertTimeLineScalingBox->setChecked(mInitialMouseValues.invertTimeLineScaling);
        mouseSettings->addRow(tr("Inverse timeline scrolling"), mInvertTimeLineScalingBox);

        mMiddleMouseMoveCanvas = new QCheckBox();
        mMiddleMouseMoveCanvas->setChecked(mInitialMouseValues.middleMouseMoveCanvas);
        mouseSettings->addRow(tr("Middle mouse moves canvas"), mMiddleMouseMoveCanvas);
    }

    createTab(tr("General"), form);
    createTab(tr("QoL"), projectSaving);
    createTab(tr("Keyboard"), keybindingSettings);
    createTab(tr("Mouse"), mouseSettings);


    this->setMainWidget(mTabs, false);

    this->setOkCancel([=](int aResult) -> bool {
        if (aResult == 0) {
            this->saveSettings();
        }
        return true;
    });
}

QFormLayout* GeneralSettingDialog::createTab(const QString& aTitle, QFormLayout* aForm) {
    auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);

    auto frame = new QFrame();
    scroll->setWidget(frame);

    auto form = new QFormLayout();
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight);
    frame->setLayout(aForm);
    frame->adjustSize();
    mTabs->addTab(scroll, aTitle);

    return form;
}

bool GeneralSettingDialog::languageHasChanged() { return mInitialLanguageIndex != mLanguageBox->currentIndex(); }

bool GeneralSettingDialog::easingHasChanged() { return mInitialEasingIndex != mEasingBox->currentIndex(); }

bool GeneralSettingDialog::rangeHasChanged() { return mInitialRangeIndex != mRangeBox->currentIndex(); }

bool GeneralSettingDialog::timeFormatHasChanged() { return mInitialTimeFormatIndex != mTimeFormatBox->currentIndex(); }

bool GeneralSettingDialog::themeHasChanged() { return mInitialThemeKey != theme(); }

bool GeneralSettingDialog::donationHasChanged() {return bDonationAllowed != mDonationAllowed->isChecked(); }

bool GeneralSettingDialog::forceSolverLoadHasChanged() {return bForceSolverLoad != mForceSolverLoad->isChecked(); }

bool GeneralSettingDialog::ignoreWarningsHasChanged() {return bIgnoreWarnings != mIgnoreWarnings->isChecked(); }

bool GeneralSettingDialog::autoSaveHasChanged() { return bAutoSave != mAutoSave->isChecked(); }

bool GeneralSettingDialog::autoSaveDelayHasChanged() { return mAutoSaveDelay != mAutoSaveDelayBox->value(); }

bool GeneralSettingDialog::autoFFmpegHasChanged() { return mAutoFFmpegCheck != mAutoFFmpegBox->isChecked(); }

bool GeneralSettingDialog::resIDCheckHasChanged() { return bResIDCheck != bResIDBox->isChecked(); }

bool GeneralSettingDialog::cbCopyHasChanged() { return bAutoCbCopy != mAutoCbCopy->isChecked(); }

QString GeneralSettingDialog::theme() {
    return mThemeGroup->checkedButton() ? mThemeGroup->checkedButton()->property("themeId").toString() : mInitialThemeKey;
}

theme::AccentColor GeneralSettingDialog::accent() {
    return mAccentGroup->checkedButton()
        ? (theme::AccentColor)mAccentGroup->checkedButton()->property("accent").toInt()
        : mInitialAccent;
}

bool GeneralSettingDialog::accentHasChanged() {
    return mInitialAccent != accent();
}


bool GeneralSettingDialog::keyDelayHasChanged() { return mKeyDelay != mKeyDelayBox->value(); }

bool GeneralSettingDialog::mouseHasChanged() {
    MouseSetting currentValues;
    currentValues.invertMainViewScaling = mInvertMainViewScalingBox->isChecked();
    currentValues.invertTimeLineScaling = mInvertTimeLineScalingBox->isChecked();
    currentValues.middleMouseMoveCanvas = mMiddleMouseMoveCanvas->isChecked();
    return mInitialMouseValues != currentValues;
}

void GeneralSettingDialog::saveSettings() {
    QSettings settings;
    if (languageHasChanged())
        settings.setValue("generalsettings/language", indexToLanguage(mLanguageBox->currentIndex(), false));

    if (easingHasChanged()) {
        // Store the enum value (locale-independent, and backward-compatible:
        // older name strings still load via easingToEnum).
        settings.setValue(
            "generalsettings/easing", int(util::Easing::typeAtDisplayIndex(mEasingBox->currentIndex()))
        );
    }
    if (rangeHasChanged()) {
        settings.setValue("generalsettings/range", indexToRange(mRangeBox->currentIndex(), false));
    }
    if (timeFormatHasChanged()) {
        // Store the enum value (not the picker index) so a saved format is
        // stable even if the picker order changes; load maps it back.
        settings.setValue("generalsettings/ui/timeformat", int(timeFormatTypeAt(mTimeFormatBox->currentIndex())));
    }
    if (themeHasChanged()) {
        settings.setValue("generalsettings/ui/theme", theme());
    }
    if (donationHasChanged()){
        settings.setValue("generalsettings/ui/donationAllowed", mDonationAllowed->isChecked());
    }
    // TODO: Bandaid fix, needs solving
    if (forceSolverLoadHasChanged()) {
        settings.setValue("forceSolverLoad", mForceSolverLoad->isChecked());
    }
    if (ignoreWarningsHasChanged()) {
        settings.setValue("export_ignore_warnings", mIgnoreWarnings->isChecked());
    }
    if (autoSaveHasChanged()) {
        settings.setValue("generalsettings/projects/enableAutosave", mAutoSave->isChecked());
    }
    if (autoSaveDelayHasChanged()) {
        settings.setValue("generalsettings/projects/autosaveDelay", mAutoSaveDelayBox->value());
    }
    if (autoFFmpegHasChanged()){
        settings.setValue("ffmpeg_check", mAutoFFmpegBox->isChecked());
    }
    if (resIDCheckHasChanged()) {
        settings.setValue("res_id_check", bResIDBox->isChecked());
    }
    if (cbCopyHasChanged()) {
        settings.setValue("generalsettings/keys/autocb", mAutoCbCopy->isChecked());
    }
    if (keyDelayHasChanged()) {
        settings.setValue("generalsettings/keybindings/keyDelay", mKeyDelayBox->value());
    }
    if (mouseHasChanged()) {
        MouseSetting newValues;
        newValues.invertMainViewScaling = mInvertMainViewScalingBox->isChecked();
        newValues.invertTimeLineScaling = mInvertTimeLineScalingBox->isChecked();
        newValues.middleMouseMoveCanvas = mMiddleMouseMoveCanvas->isChecked();
        newValues.save();
        mViaPoint.mouseSetting() = newValues;
    }
}
} // namespace gui
