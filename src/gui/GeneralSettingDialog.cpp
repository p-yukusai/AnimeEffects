#include <QApplication>
#include <qstandardpaths.h>
#include "MainWindow.h"
#include "gui/GeneralSettingDialog.h"
#include "util/NetworkUtil.h"
#include <QComboBox>
#include <QHBoxLayout>
#include "gui/theme/Colors.h"

namespace {

constexpr int kLanguageTypeCount = 7;
constexpr int kTimeFormatTypeCount = 6;
constexpr int kEasingTypeCount = 13;
constexpr int kRangeTypeCount = 3;

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

int easingToIndex(const QString& aEasing) {
    if (aEasing == "None")
        return 0;
    if (aEasing == "Linear")
        return 1;
    if (aEasing == "Sine")
        return 2;
    if (aEasing == "Quad")
        return 3;
    if (aEasing == "Cubic")
        return 4;
    if (aEasing == "Quart")
        return 5;
    if (aEasing == "Quint")
        return 6;
    if (aEasing == "Expo")
        return 7;
    if (aEasing == "Circ")
        return 8;
    if (aEasing == "Back")
        return 9;
    if (aEasing == "Elastic")
        return 10;
    if (aEasing == "Bounce")
        return 11;
    if (aEasing == "Custom")
        return 12;
    return 1;
    // Default easing is Linear
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

QString indexToEasing(int aIndex, bool translated = true) {
    if (translated) {
        switch (aIndex) {
        case 0:
            return QCoreApplication::translate("GeneralSettingsDialog", "None");
        case 1:
            return QCoreApplication::translate("GeneralSettingsDialog", "Linear");
        case 2:
            return QCoreApplication::translate("GeneralSettingsDialog", "Sine");
        case 3:
            return QCoreApplication::translate("GeneralSettingsDialog", "Quad");
        case 4:
            return QCoreApplication::translate("GeneralSettingsDialog", "Cubic");
        case 5:
            return QCoreApplication::translate("GeneralSettingsDialog", "Quart");
        case 6:
            return QCoreApplication::translate("GeneralSettingsDialog", "Quint");
        case 7:
            return QCoreApplication::translate("GeneralSettingsDialog", "Expo");
        case 8:
            return QCoreApplication::translate("GeneralSettingsDialog", "Circ");
        case 9:
            return QCoreApplication::translate("GeneralSettingsDialog", "Back");
        case 10:
            return QCoreApplication::translate("GeneralSettingsDialog", "Elastic");
        case 11:
            return QCoreApplication::translate("GeneralSettingsDialog", "Bounce");
        case 12:
            return QCoreApplication::translate("GeneralSettingsDialog", "Custom");
        default:
            return QCoreApplication::translate("GeneralSettingsDialog", "Linear");
        }
    }
    switch (aIndex) {
    case 0:
        return QString("None");
    case 1:
        return QString("Linear");
    case 2:
        return QString("Sine");
    case 3:
        return QString("Quad");
    case 4:
        return QString("Cubic");
    case 5:
        return QString("Quart");
    case 6:
        return QString("Quint");
    case 7:
        return QString("Expo");
    case 8:
        return QString("Circ");
    case 9:
        return QString("Back");
    case 10:
        return QString("Elastic");
    case 11:
        return QString("Bounce");
    case 12:
        return QString("Custom");
    default:
        return QString("Linear");
    }
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

static QString indexToTimeFormat(const int aIndex) {
    switch (aIndex) {
    case core::TimeFormatType::TimeFormatType_Frames_From0:
        return QCoreApplication::translate("GeneralSettingsDialog", "Frame number (from 0)");
    case core::TimeFormatType::TimeFormatType_Frames_From1:
        return QCoreApplication::translate("GeneralSettingsDialog", "Frame number (from 1)");
    case core::TimeFormatType::TimeFormatType_Relative_FPS:
        return QCoreApplication::translate("GeneralSettingsDialog", "Relative to FPS (1.0 = 60.0)");
    case core::TimeFormatType::TimeFormatType_Seconds_Frames:
        return QCoreApplication::translate("GeneralSettingsDialog", "Seconds : Frame");
    case core::TimeFormatType::TimeFormatType_Timecode_SMPTE:
        return QCoreApplication::translate("GeneralSettingsDialog", "Timecode (SMPTE) (HH:MM:SS:FF)");
    case core::TimeFormatType::TimeFormatType_Timecode_HHMMSSmmm:
        return QCoreApplication::translate("GeneralSettingsDialog", "Timecode (HH:MM:SS:mmm)");
    default:
        return "";
    }
}


namespace gui {

GeneralSettingDialog::GeneralSettingDialog(GUIResources& aGUIResources, QWidget* aParent):
    EasyDialog(tr("General Settings"), aParent),
    mTabs(new QTabWidget(this)),
    mInitialLanguageIndex(),
    mLanguageBox(),
    mInitialTimeFormatIndex(),
    mTimeFormatBox(),
    mInitialAccent(theme::kDefaultAccent),
    mAccentGroup(),
    mInitialThemeKey("dark"),
    mThemeGroup(),
    mGUIResources(aGUIResources) {
    // read current settings
    {
        QSettings settings;
        auto language = settings.value("generalsettings/language");

        if (language.isValid()) {
            mInitialLanguageIndex = languageToIndex(language.toString());
        }

        auto easing = settings.value("generalsettings/easing");
        mInitialEasingIndex = easing.isValid() ? easingToIndex(easing.toString()) : easingToIndex("Linear");

        auto range = settings.value("generalsettings/range");
        mInitialRangeIndex = range.isValid() ? rangeToIndex(range.toString()) : rangeToIndex("All");

        auto timeScale = settings.value("generalsettings/ui/timeformat");
        if (timeScale.isValid()) {
            mInitialTimeFormatIndex = timeScale.toInt();
        }

        auto uiScale = settings.value("generalsettings/ui/uiScale", 1.0);
        mUiScale = uiScale.toDouble();

        auto font = settings.value("generalsettings/ui/font", qApp->font().family());
        if (font.isValid()) {
            mFont = font.toString();
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
        mAutoSaveDelay = isAutoSaveDelay.isValid() ? isAutoSaveDelay.toInt() : 5;

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

        mFontFamilyBox = new QLineEdit();
        mFontFamilyBox->setText(mFont);
        form->addRow(tr("Font"), mFontFamilyBox);

        auto resetFontButton = new QPushButton(tr("Reset font"));
        connect(resetFontButton, &QPushButton::clicked, [=]() {
            QSettings settings;
            settings.remove("generalsettings/ui/font");
            settings.setValue("generalsettings/ui/customFont", false);
            QMessageBox::information(this, tr("Font reset"), tr("The font has been reset."));
        });
        form->addRow(resetFontButton);

        mUiScaleBox = new QDoubleSpinBox();
        mUiScaleBox->setValue(mUiScale);
        mUiScaleBox->setSingleStep(0.1);
        mUiScaleBox->setMinimum(0.5);
        mUiScaleBox->setMaximum(3.0);
        form->addRow(tr("UI scale"), mUiScaleBox);

        auto resetUIScaleButton = new QPushButton(tr("Reset UI scale"));
        connect(resetUIScaleButton, &QPushButton::clicked, [=]() {
            QSettings settings;
            settings.setValue("generalsettings/ui/uiScale", 1.0);
            settings.setValue("temp/scaleReset", true);
            QMessageBox::information(this, tr("UI Scale reset"), tr("The UI scaling has been reset."));
        });
        form->addRow(resetUIScaleButton);

        mThemeGroup = new QButtonGroup(this);
        mThemeGroup->setExclusive(true);
        {
            const theme::Colors c = theme::Colors::current();
            auto* themeRow = new QWidget();
            themeRow->setAttribute(Qt::WA_StyledBackground, true);
            auto* themeLayout = new QHBoxLayout(themeRow);
            themeLayout->setContentsMargins(2, 2, 2, 2);
            themeLayout->setSpacing(0);
            const QStringList themeList = gui::GUIResources::themeList();
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

        mAccentGroup = new QButtonGroup(this);
        mAccentGroup->setExclusive(true);
        {
            const theme::Colors c = theme::Colors::current();
            auto* accentRow = new QWidget();
            auto* accentLayout = new QHBoxLayout(accentRow);
            accentLayout->setContentsMargins(0, 0, 0, 0);
            accentLayout->setSpacing(6);
            for (int i = 0; i < theme::kAccentCount; ++i) {
                const auto acc = static_cast<theme::AccentColor>(i);
                auto* b = new QPushButton();
                b->setCheckable(true);
                b->setProperty("accent", i);
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

    auto project = new QFormLayout();
    {
        mAutoSave = new QCheckBox();
        mAutoSave->setChecked(bAutoSave);
        project->addRow(tr("Automatically save your project"), mAutoSave);

        mAutoSaveDelayBox = new QSpinBox();
        mAutoSaveDelayBox->setValue(mAutoSaveDelay);
        project->addRow(tr("Time (in minutes) between autosaves"), mAutoSaveDelayBox);

        mForceSolverLoad = new QCheckBox();
        mForceSolverLoad->setChecked(bForceSolverLoad);
        project->addRow(tr("Force project to load"), mForceSolverLoad);

        mAutoFFmpegBox = new QCheckBox();
        mAutoFFmpegBox->setChecked(mAutoFFmpegCheck);
        project->addRow(tr("Always check for FFmpeg on export"), mAutoFFmpegBox);

        mIgnoreWarnings = new QCheckBox();
        mIgnoreWarnings->setChecked(bIgnoreWarnings);
        project->addRow(tr("Ignore export warnings"), mIgnoreWarnings);
    }

    auto qualityOfLife = new QFormLayout();
    {
        mEasingBox = new QComboBox();
        for (int i = 0; i < kEasingTypeCount; ++i) {
            mEasingBox->addItem(indexToEasing(i));
        }
        mEasingBox->setCurrentIndex(mInitialEasingIndex);
        qualityOfLife->addRow(tr("Default keyframe easing"), mEasingBox);

        mRangeBox = new QComboBox();
        for (int i = 0; i < kRangeTypeCount; ++i) {
            mRangeBox->addItem(indexToRange(i));
        }
        mRangeBox->setCurrentIndex(mInitialRangeIndex);
        qualityOfLife->addRow(tr("Default keyframe range"), mRangeBox);

        mTimeFormatBox = new QComboBox();
        for (int i = 0; i < kTimeFormatTypeCount; ++i) {
            mTimeFormatBox->addItem(indexToTimeFormat(i));
        }
        mTimeFormatBox->setCurrentIndex(mInitialTimeFormatIndex);
        qualityOfLife->addRow(tr("Timeline format (needs restart)"), mTimeFormatBox);

        mAutoShowMesh = new QCheckBox();
        mAutoShowMesh->setChecked(bAutoShowMesh);
        connect(mAutoShowMesh, &QPushButton::clicked, [=]() {
            QSettings settings;
            settings.setValue("generalsettings/tools/autoshowmesh", mAutoShowMesh->isChecked());
        });
        qualityOfLife->addRow(tr("Show mesh when selecting FFD"), mAutoShowMesh);

        mAutoCbCopy = new QCheckBox();
        mAutoCbCopy->setChecked(bAutoCbCopy);
        qualityOfLife->addRow(tr("On copy send keys to the clipboard"), mAutoCbCopy);

        bResIDBox = new QCheckBox();
        bResIDBox->setChecked(bResIDCheck);
        qualityOfLife->addRow(tr("Enforce ID check on asset download"), bResIDBox);

        mDonationAllowed = new QCheckBox();
        mDonationAllowed->setChecked(bDonationAllowed);
        qualityOfLife->addRow(tr("Allow donation menu"), mDonationAllowed);

        mResetButton = new QPushButton(tr("Reset recent files list"));
        mResetButton->setToolTip(tr("Deletes all project entries from your recents"));
        connect(mResetButton, &QPushButton::clicked, [=]() {
            QSettings settings;
            settings.remove("projectloader/recents");
            MainWindow::showInfoPopup(tr("Success"), tr("All entries have been successfully removed"), "Info");
            auto mmb = qobject_cast<MainMenuBar*>(this->parent());
            mmb->onRecentsUpdate();
        });
        qualityOfLife->addRow(mResetButton);


    }

    auto keybindingSettings = new QFormLayout();
    {
        mKeyDelayBox = new QSpinBox();
        mKeyDelayBox->setRange(0, 10000);
        mKeyDelayBox->setValue(mKeyDelay);
        keybindingSettings->addRow(tr("Global keybind delay (ms)"), mKeyDelayBox);

        mResetKeybindsButton = new QPushButton(tr("Reset keybinds"));
        mResetKeybindsButton->setToolTip(tr("Reset all keybinds, a restart is required."));
        connect(mResetKeybindsButton, &QPushButton::clicked, [=]() {
            QSettings settings;
            settings.setValue("keybindReset",
                !settings.value("keybindReset").isValid() || !settings.value("keybindReset").toBool()
            );
            MainWindow::showInfoPopup(
                tr("Keybinds reset status"),
                settings.value("keybindReset").toBool() ? tr("The keybinds will be reset when you restart.")
                                                        : tr("The keybinds will not be reset when you restart."),
                "Info"
            );
        });
        keybindingSettings->addRow(mResetKeybindsButton);
    }

    auto ffmpegSettings = new QFormLayout();
    {
        ffmpegTroubleshoot = new QPushButton(tr("Troubleshoot FFmpeg"));
        connect(ffmpegTroubleshoot, &QPushButton::clicked, [=]() {
            QMessageBox ffmpegNotif;

            QString ffmpeg = getFFmpeg();
            bool ffmpegWorks = ffmpegCheck(ffmpeg, this);
            if (!ffmpegWorks) {
                ffmpegCheckFailed(this);
                return;
            }
            ffmpegNotif.setWindowTitle(tr("FFmpeg test success"));
            ffmpegNotif.setText(tr("All tests have passed, FFmpeg is working correctly."));
            ffmpegNotif.setDetailedText(
                tr("FFmpeg at: ") + ffmpeg + "\n" +
                tr("Check FFmpeg response ✓\n"
                "Check FFmpeg exporting ✓\n"
                "Check FFmpeg palette generation ✓")
            );
            ffmpegNotif.addButton(QMessageBox::Ok);
            ffmpegNotif.exec();
        });

        selectFromExe = new QPushButton(tr("Select executable and automatically set up"));
        selectFromExe->setToolTip(
            tr("This will remove previous instances of FFmpeg from your tools directory"
               " and replace them with your custom executable, please make sure this is a valid FFmpeg executable.")
        );
        connect(selectFromExe, &QPushButton::clicked, [=]() {
            #ifdef Q_OS_LINUX
            auto dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
            dir = QDir(dir.absolutePath() + "/AnimeEffects");
            #else
            QDir dir = QDir("./tools");
            #endif
            if (!dir.exists()) {}
            QString file = util::NetworkUtil::os() == "win" ? "/ffmpeg.exe" : "/ffmpeg";

            if (QFileInfo::exists(dir.absolutePath() + file)) {
                QFile(dir.absolutePath() + file).moveToTrash();
            }

            QFileInfo ffmpeg = QFileInfo(QFileDialog::getOpenFileName(nullptr, "Select FFmpeg executable"));
            qDebug() << "Selected exe: " << ffmpeg.fileName() << " | " << ffmpeg.absoluteFilePath();
            QFile(ffmpeg.absoluteFilePath()).copy(dir.absolutePath() + file);

            QMessageBox success;
            success.setText(tr("Operation successful"));
            success.setWindowTitle(tr("Success"));
            success.addButton(QMessageBox::Ok);
            success.exec();
        });


        autoSetup = new QPushButton(tr("Download and automatically set up"));
        connect(autoSetup, &QPushButton::clicked, [=]() {
            #ifdef Q_OS_LINUX
            auto dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
            dir = QDir(dir.absolutePath() + "/AnimeEffects/");
            #else
            QDir dir = QDir("./tools");
            #endif
            if (!dir.exists()) {
                if (dir.mkpath(dir.absolutePath())) {
                    qDebug() << "Directory created : " << dir.absolutePath();
                } else {
                    qDebug() << "Directory creation failed, assumed protected : " << dir.absolutePath();
                    QMessageBox error;
                    error.setWindowTitle(tr("Unable to create directory"));
                    error.setText(
                        tr("An error has occurred while creating the tools directory in ") + dir.absolutePath() +
                        tr(" the application will use a fallback but if this doesn't work please run the app as "
                           "administrator or use a folder that isn't write protected.")
                    );
                    error.addButton(QMessageBox::Ok);
                    error.exec();
                }
            }
            const QString file = util::NetworkUtil::os() == "win" ? "/ffmpeg.exe" : "/ffmpeg";
            if (QFileInfo::exists(dir.absolutePath() + file)) {
                QFile(dir.absolutePath() + file).moveToTrash();
            }
            const QString os = util::NetworkUtil::os();
            QString gitFile;
            // ID checking is probably overkill, but security is important!
            int id;
            if (os == "win") {
                gitFile = "ffmpeg-win64.exe";
                id = 222521524;
            } else if (os == "linux") {
                gitFile = "ffmpeg-linux64";
                id = 222521700;
            } else {
                gitFile = "ffmpeg-macos";
                id = 222521413;
            }

            const QFileInfo ffmpeg = util::NetworkUtil::downloadGithubFile(
                "https://api.github.com/repos/AnimeEffectsDevs/ffmpeg-bin/releases/latest", gitFile, id, this
                );
            qDebug() << "Download name : " << ffmpeg.fileName() << "\nDownload is executable : " << ffmpeg.isExecutable();
            bool pathSetAttempted = false;
            bool ffmpegMoveAttempted = false;
            if (ffmpeg.isExecutable()) {
                if(!dir.exists() || !dir.isReadable() || !QFileInfo(dir.absolutePath()).isWritable()){
                    // We'll attempt a global installation as a fallback in cases where the folder is write protected
                    auto appdata = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
                    auto anieFolder = QDir(appdata.absolutePath() + "/AnimeEffects");
                    if (!anieFolder.exists() || !QFileInfo(anieFolder.absolutePath()).isWritable()) {
                        if (!appdata.mkdir(appdata.absolutePath() + "/AnimeEffects")) {
                            qDebug(
                                "AnimeEffects folder creation failed in AppData, attempting to locate in Documents..."
                            );
                            appdata = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
                            anieFolder = QDir(appdata.absolutePath() + "/AnimeEffects");
                            if (!anieFolder.exists() || !QFileInfo(anieFolder.absolutePath()).isWritable()) {
                                if (!appdata.mkdir(appdata.absolutePath() + "/AnimeEffects")) {
                                    qDebug("AnimeEffects folder creation failed, skipping...");
                                }
                            } else {
                                appdata = anieFolder;
                            }
                        } else {
                            appdata = anieFolder;
                        }
                    }
                    else {
                        appdata = anieFolder;
                    }
                    const QString fileInAppDataLoc = appdata.absolutePath() + file;
                    QFile(ffmpeg.absoluteFilePath()).rename(fileInAppDataLoc);
                    qDebug() << "FFmpeg moved : " << QFile(fileInAppDataLoc).exists();
                    if (QFile(fileInAppDataLoc).exists()) {
                        QProcess pathSet;
                        QString console;
                        QStringList instruct;
                        if(os == "linux" || os == "mac"){
                            console = "sh";
                            instruct.append("export");
                            instruct.append("PATH=$PATH:" + fileInAppDataLoc);
                        }
                        else{
                            console = "cmd";
                            instruct.append("set");
                            instruct.append("PATH=%PATH%;" + fileInAppDataLoc);
                        }
                        pathSet.start(console, instruct);
                        pathSet.waitForFinished(2*1000);
                        if (pathSet.exitCode() == 0) {
                            QMessageBox success;
                            success.setWindowTitle(tr("Success"));
                            success.setText(tr("FFmpeg was successfully set up. Please restart AnimeEffects"));
                            success.exec();
                            return;
                        }
                        pathSetAttempted = true;
                        qDebug("FFmpeg path setup errored, continuing...");
                    }
                    else {
                        ffmpegMoveAttempted = true;
                        qDebug("FFmpeg move errored, continuing...");
                    }
                }
                QFile(ffmpeg.absoluteFilePath()).rename(dir.absolutePath() + file);
                qDebug() << "FFmpeg moved : " << QFile(dir.absolutePath() + file).exists();
                #ifdef Q_OS_LINUX
                QMessageBox info;
                info.setWindowTitle(tr("FFmpeg installed"));
                info.setText(tr("FFmpeg was installed but it requires to be set as executable, please right click "
                                "\"ffmpeg\" go to properties and allow it to be executable as a program."));
                info.exec();
                QDesktopServices::openUrl(QUrl::fromLocalFile(dir.absolutePath()));
                #endif
                if (QFile(dir.absolutePath() + file).exists()) {
                    QMessageBox success;
                    success.setWindowTitle(tr("Success"));
                    success.setText(tr("FFmpeg was successfully set up."));
                    success.exec();
                    return;
                }
            }
            QMessageBox error;
            error.setWindowTitle(tr("Error"));
            error.setText(tr("While setting up FFmpeg an unexpected error has occurred, please send the below information to our devs."));
            QString platform = QSysInfo::productType(); platform[0] = platform[0].toUpper();
            error.setDetailedText(
                QString("Operating system: ") + platform + " " + QSysInfo::productVersion() +
                "\nCPU: " + QSysInfo::currentCpuArchitecture() +
                "\nFile requested: " + gitFile + " - Hardcoded file ID: " + QString::number(id) +
                "\nFile received: " + ffmpeg.absoluteFilePath() +
                "\nFile is executable: " + (ffmpeg.isExecutable()? "True" : "False") +
                "\nFile is readable: " + (ffmpeg.isReadable()? "True": "False") +
                "\nFile is writable: " + (ffmpeg.isWritable()? "True": "False") +
                "\nRequested base folders : " + dir.absolutePath() + " | " + QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + " | " + QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                "\nFallback path set errored: " + (pathSetAttempted? "True" : "False") +
                "\nFallback FFmpeg move errored: " + (ffmpegMoveAttempted? "True" : "False")
            );
            error.exec();
        });

        ffmpegSettings->addRow(ffmpegTroubleshoot);
        ffmpegSettings->addRow(selectFromExe);
        ffmpegSettings->addRow(autoSetup);
    }

    createTab(tr("General"), form);
    createTab(tr("Project"), project);
    createTab(tr("QoL"), qualityOfLife);
    createTab(tr("FFmpeg"), ffmpegSettings);
    createTab(tr("Keybindings"), keybindingSettings);


    this->setMainWidget(mTabs, false);

    this->setOkCancel([=](const int aResult) -> bool {
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

bool GeneralSettingDialog::fontHasChanged() { return mFont != mFontFamilyBox->text(); }

bool GeneralSettingDialog::uiScaleHasChanged() {
    QSettings settings;
    if (settings.value("temp/scaleReset", false).toBool()) {
        settings.remove("temp/scaleReset");
        forceThemeReload = true;
        return false;
    }
    return mUiScale != mUiScaleBox->value();
}

bool GeneralSettingDialog::themeHasChanged() { return mInitialThemeKey != theme(); }

bool GeneralSettingDialog::donationHasChanged() {return bDonationAllowed != mDonationAllowed->isChecked(); }

bool GeneralSettingDialog::forceSolverLoadHasChanged() {return bForceSolverLoad != mForceSolverLoad->isChecked(); }

bool GeneralSettingDialog::ignoreWarningsHasChanged() {return bIgnoreWarnings != mIgnoreWarnings->isChecked(); }

bool GeneralSettingDialog::autoSaveHasChanged() { return bAutoSave != mAutoSave->isChecked(); }

bool GeneralSettingDialog::autoSaveDelayHasChanged() { return mAutoSaveDelay != mAutoSaveDelayBox->value(); }

bool GeneralSettingDialog::autoFFmpegHasChanged() { return mAutoFFmpegCheck != mAutoFFmpegBox->isChecked(); }

bool GeneralSettingDialog::resIDCheckHasChanged() { return bResIDCheck != bResIDBox->isChecked(); }

bool GeneralSettingDialog::cbCopyHasChanged() { return bAutoCbCopy != mAutoCbCopy->isChecked(); }

bool GeneralSettingDialog::keyDelayHasChanged() { return mKeyDelay != mKeyDelayBox->value(); }

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


QString GeneralSettingDialog::getFFmpeg() {
    QFileInfo ffmpeg_file;
    QString ffmpeg;
    if (util::NetworkUtil::os() == "win") { ffmpeg_file = QFileInfo("./tools/ffmpeg.exe"); }
    else { ffmpeg_file = QFileInfo("./tools/ffmpeg"); }
    const auto file = util::NetworkUtil::os() == "win" ? "ffmpeg.exe" : "ffmpeg";
    auto appdata = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    auto anieFolder = QDir(appdata.absolutePath() + "/AnimeEffects");
    if (!ffmpeg_file.exists() || !ffmpeg_file.isExecutable()) {
        qDebug() << "FFmpeg not found, attempting backup...";
        ffmpeg_file = QFileInfo(anieFolder.absolutePath() + "/" + file);
        ffmpeg = ffmpeg_file.absoluteFilePath();
        if (!anieFolder.exists() || !QFileInfo(anieFolder.absolutePath()).isReadable()) {
            qDebug() << "AnimeEffects not found in appdata, attempting backup...";
            appdata = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
            anieFolder = QDir(appdata.absolutePath() + "/AnimeEffects");
            ffmpeg_file = QFileInfo(anieFolder.absolutePath() + "/" + file);
            ffmpeg = ffmpeg_file.absoluteFilePath();
            if (!anieFolder.exists() || !QFileInfo(anieFolder.absolutePath()).isReadable()) {
                qDebug() << "AnimeEffects not found in documents, attempting back up...";
                appdata = QApplication::applicationDirPath();
                ffmpeg_file = QFileInfo(appdata.absolutePath() + "/" + file);
                ffmpeg = ffmpeg_file.absoluteFilePath();
                if (!appdata.exists() || !ffmpeg_file.isReadable()) {
                    qDebug() << "AnimeEffects not found in appdir, stopping...";
                    ffmpeg = "ffmpeg";
                }
                else { qDebug("FFmpeg found in appdir, initializing..."); }
            }
            else{ qDebug("FFmpeg found in documents, initializing..."); }
        } else { qDebug("FFmpeg found in appdata, initializing..."); }
    }
    else {
        qDebug() << "FFmpeg found in tools, initializing...";
        ffmpeg = ffmpeg_file.absoluteFilePath();
    }
    qDebug() << "FFmpeg at: " << ffmpeg;
    return ffmpeg;
}
void GeneralSettingDialog::ffmpegCheckFailed(GeneralSettingDialog* aDialog) {
    QMessageBox message;
    message.setIcon(QMessageBox::Warning);
    message.setText(tr("FFmpeg was not found."));
    auto infoText =
        tr("Exporting video requires FFmpeg to be installed on your computer, "
           "FFmpeg is a free tool that AnimeEffects uses to create video files.\n"
           "In the following screen you can instruct AnimeEffects to download and install it automatically for "
           "you, "
           "or you can download it by yourself and tell AnimeEffects where it is.");
    message.setInformativeText(infoText);
    message.setStandardButtons(QMessageBox::Ok);
    message.setDefaultButton(QMessageBox::Ok);
    message.exec();
    aDialog->selectTab(2);
    aDialog->exec();
}

bool GeneralSettingDialog::ffmpegCheck(const QString& ffmpeg, GeneralSettingDialog* generalSettingsDialog) {
    QSettings settings;
    auto ffCheck = settings.value("ffmpeg_check");
    if (ffCheck.isValid() && !ffCheck.toBool()) {
        return true;
    }
    const bool libExists = util::NetworkUtil::libExists(ffmpeg, "-version");
    const bool pathExists = util::NetworkUtil::libExists("ffmpeg");

    if (!libExists && !pathExists) {
        return false;
    }
    // Test FFmpeg functionality
    QMessageBox ffmpegNotif;
    ffmpegNotif.setIcon(QMessageBox::Critical);

    QTemporaryDir tempDir;
    QDir ffmpegOutputDir = tempDir.isValid() ? tempDir.path() : QDir::temp();

    // Noise to GIF conversion test
    QProcess gif;
    QString gifLoc = ffmpegOutputDir.absoluteFilePath("gif.gif");
    qDebug() << "Gif loc: " << gifLoc;
    gif.start(ffmpeg, {"-f", "bin", "-i", "pipe:", "-y", gifLoc}, QProcess::ReadWrite);
    std::string testData = "Test";
    for (auto x = 0; x < 2; x++) {
        gif.write(testData.c_str(), testData.size());
        gif.waitForBytesWritten();
    }
    gif.closeWriteChannel();
    gif.waitForFinished();
    qDebug() << gif.readAllStandardOutput();
    bool exportSuccess = gif.exitStatus() == 0 && QFileInfo::exists(gifLoc);
    qDebug() << "Gif exists: " << QFileInfo::exists(gifLoc) << "| Gif remove: " << QFile(gifLoc).remove();
    gif.deleteLater();
    if (!exportSuccess) {
        ffmpegNotif.setWindowTitle(tr("FFmpeg doesn't export"));
        ffmpegNotif.setText(tr("FFmpeg was unable to export, please troubleshoot."));
        ffmpegNotif.setDetailedText(
            "File exists: " + QString(QFileInfo::exists("./data/icons/file.png")? "True" : "False") +
            "\nFile readable: " + QString(QFileInfo("./data/icons/file.png").isReadable()? "True" : "False") +
            "\nFile writeable: " + QString(QFileInfo("./data/icons/file.png").isWritable()? "True" : "False") +
            "\nFolder writeable: " + QString(QDir("./data/icons/").exists() ? "True" : "False") +
            "\nFolder readable: " + QString(QDir("./data/icons/").isReadable() ? "True" : "False")
            );
        ffmpegNotif.addButton(QMessageBox::Ok);
        ffmpegNotif.exec();
        generalSettingsDialog->selectTab(2);
        generalSettingsDialog->exec();
        ffCheck.setValue(true);
        return false;
    }
    // Palettegen test
    QProcess palettegen;
    QString palettegenLoc = ffmpegOutputDir.absoluteFilePath("palette.gif");
    qDebug() << "Palette loc: " << palettegenLoc;
    palettegen.start(ffmpeg, {"-f", "bin", "-i", "pipe:", "-y", "-vf", "palettegen", palettegenLoc}, QProcess::ReadWrite);
    for (auto x = 0; x < 2; x++) {
        palettegen.write(testData.c_str(), testData.size());
        palettegen.waitForBytesWritten();
    }
    palettegen.closeWriteChannel();
    palettegen.waitForFinished();
    qDebug() << palettegen.readAllStandardOutput();
    bool pGenSuccess = palettegen.exitStatus() == 0 && QFileInfo::exists(palettegenLoc);
    if (!pGenSuccess) {
        ffmpegNotif.setWindowTitle(tr("FFmpeg doesn't generate palettes"));
        ffmpegNotif.setText(tr("FFmpeg was unable to generate palettes, please troubleshoot."));
        ffmpegNotif.addButton(QMessageBox::Ok);
        ffmpegNotif.exec();
        generalSettingsDialog->selectTab(2);
        generalSettingsDialog->exec();
        ffCheck.setValue(true);
        return false;
    }
    qDebug() << "Palette exists: " << QFileInfo::exists(palettegenLoc)
             << "| Palette remove: " << QFile(palettegenLoc).remove();
    palettegen.deleteLater();
    return true;
}

void GeneralSettingDialog::saveSettings() {
    QSettings settings;
    if (languageHasChanged())
        settings.setValue("generalsettings/language", indexToLanguage(mLanguageBox->currentIndex(), false));

    if (easingHasChanged()) {
        settings.setValue("generalsettings/easing", indexToEasing(mEasingBox->currentIndex(), false));
    }
    if (rangeHasChanged()) {
        settings.setValue("generalsettings/range", indexToRange(mRangeBox->currentIndex(), false));
    }
    if (timeFormatHasChanged()) {
        settings.setValue("generalsettings/ui/timeformat", mTimeFormatBox->currentIndex());
    }
    if (fontHasChanged()) {
        settings.setValue("generalsettings/ui/customFont", true);
        if (mFontFamilyBox->text().contains("Comic Sans")) {

            QMessageBox message;
            message.setIcon(QMessageBox::Critical);
            message.setWindowTitle("SIGILL");
            message.setText(tr("I'm sorry Dave, I'm afraid I can't do that..."));
            message.setInformativeText("I think you know what the problem is just as well as I do.");
            message.setStandardButtons(QMessageBox::Ok);
            message.setDefaultButton(QMessageBox::Ok);
            message.setWindowModality(Qt::ApplicationModal);
            message.exec();
            bHal = true;
        }
        settings.setValue("generalsettings/ui/font", mFontFamilyBox->text());
    }
    if (uiScaleHasChanged()) {
        settings.setValue("generalsettings/ui/uiScale", mUiScaleBox->value());
    }
    if (themeHasChanged()) {
        settings.setValue("generalsettings/ui/theme", theme());
    }
    if (donationHasChanged()){
        settings.setValue("generalsettings/ui/donationAllowed", mDonationAllowed->isChecked());
    }
    // TODO: Bandaid fix, maybe solved, requires investigation
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
}
} // namespace gui