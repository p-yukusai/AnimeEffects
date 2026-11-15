#include "gui/GeneralSettingDialog.h"

#include "MainWindow.h"
#include "XCAssert.h"
#include "core/TimeFormat.h"
#include "gui/GUIResources.h"
#include "gui/MouseSetting.h"
#include "gui/ViaPoint.h"
#include "gui/qml/GeneralSettingViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "util/Easing.h"

#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QScreen>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariantMap>
#include <algorithm>

namespace {

// Timeline format picker order. Frames_From1 is dropped from the UI (with frames
// natively 1-indexed it is identical to Frames_From0), but the enum value is kept so
// previously saved settings still load - they map to the "Frame number" entry. The
// scene's option list (gui::GeneralSettingViewModel::timeFormatOptions) is written in
// this order and with this many entries.
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

// The canonical (untranslated) names the settings file stores, indexed the way the
// scene's language/range option lists are: same order, same count. These are the
// load/save halves only - the LABELS the user reads come from the view model, which
// carries the widget's existing translation entries.
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

QString indexToLanguage(int aIndex) {
    switch (aIndex) {
    case 0: return {"Auto"};
    case 1: return {"English"};
    case 2: return {"Japanese"};
    case 3: return {"Chinese"};
    case 4: return {"Spanish"};
    case 5: return {"Russian"};
    case 6: return {"Korean"};
    default: return {"Auto"};
    }
}

int rangeToIndex(const QString& aRange) {
    if (aRange == "In")
        return 0;
    if (aRange == "Out")
        return 1;
    return 2;
    // "All" IS InOut - the widget stored the third entry under that name.
}

QString indexToRange(int aIndex) {
    switch (aIndex) {
    case 0: return {"In"};
    case 1: return {"Out"};
    default: return {"All"};
    }
}

} // namespace

namespace gui {

GeneralSettingDialog::GeneralSettingDialog(GUIResources& aGUIResources, ViaPoint& aViaPoint, QWidget* aParent)
    : QDialog(aParent),
      mGUIResources(aGUIResources),
      mViaPoint(aViaPoint) {
    setObjectName(QStringLiteral("generalSettingsWidget"));
    setWindowTitle(tr("Settings"));
    // A native framed window, not a frameless popup: normal title bar, movable and
    // resizable by the window manager, and modal over the main window.
    setModal(true);
    setSizeGripEnabled(true);

    mModel = new GeneralSettingViewModel(this);

    // Read the saved settings, the way the widget form did, and hand the resulting
    // values over in one piece: the model's "before" and "now" sides both start here,
    // so nothing reads as changed until the user moves something.
    {
        GeneralSettingViewModel::Init init;
        QSettings settings;

        auto language = settings.value("generalsettings/language");
        if (language.isValid())
            init.languageIndex = languageToIndex(language.toString());

        // ONE resolver for both stored forms (the enum ordinal of the shared-table
        // refactor and the older canonical NAME): a stored "Auto" or the retired
        // ordinal 14 meant the automatic (CLAMPED) handle pair, which the option
        // list carries as its own entry (GeneralSettingViewModel::optionForSetting).
        init.easingIndex =
            GeneralSettingViewModel::optionForSetting(settings.value("generalsettings/easing"));

        auto range = settings.value("generalsettings/range");
        if (range.isValid())
            init.rangeIndex = rangeToIndex(range.toString());

        auto timeScale = settings.value("generalsettings/ui/timeformat");
        if (timeScale.isValid()) {
            // Stored as the enum value; map it (the legacy Frames_From1 included) to a
            // picker index.
            init.timeFormatIndex =
                timeFormatComboIndexOf(static_cast<core::TimeFormatType>(timeScale.toInt()));
        }

        auto savedTheme = settings.value("generalsettings/ui/theme");
        if (savedTheme.isValid())
            init.themeKey = savedTheme.toString();

        const QString accentName = settings.value("generalsettings/ui/accent", QString()).toString();
        init.accentIndex = int(theme::accentFromName(accentName));

        auto donationAllowed = settings.value("generalsettings/ui/donationAllowed");
        init.donationAllowed = donationAllowed.isValid() ? donationAllowed.toBool() : true;

        auto forceSolverLoad = settings.value("forceSolverLoad", false);
        init.forceSolverLoad = forceSolverLoad.toBool();

        auto ignoreWarnings = settings.value("export_ignore_warnings");
        init.ignoreWarnings = ignoreWarnings.isValid() ? ignoreWarnings.toBool() : false;

        auto isAutoSave = settings.value("generalsettings/projects/enableAutosave");
        init.autoSave = isAutoSave.isValid() ? isAutoSave.toBool() : true;

        auto isAutoSaveDelay = settings.value("generalsettings/projects/autosaveDelay");
        init.autoSaveDelayMinutes = isAutoSaveDelay.isValid() ? std::max(1, isAutoSaveDelay.toInt()) : 5;

        // Kept byte-for-byte as the widget had it: the box opened UNTICKED whatever
        // the file said (`!isAutoCbCopy.isValid() ? isAutoCbCopy.toBool() : false`
        // evaluates to false on both branches). Faithfully ported rather than
        // silently fixed; flagged in the port's report.
        auto isAutoCbCopy = settings.value("generalsettings/keys/autocb");
        init.autoCbCopy = !isAutoCbCopy.isValid() ? isAutoCbCopy.toBool() : false;

        auto isAutoShowMesh = settings.value("generalsettings/tools/autoshowmesh");
        init.autoShowMesh = isAutoShowMesh.isValid() ? isAutoShowMesh.toBool() : false;

        auto isResIDCheck = settings.value("res_id_check");
        init.resIDCheck = isResIDCheck.isValid() ? isResIDCheck.toBool() : true;

        auto isKeyDelay = settings.value("generalsettings/keybindings/keyDelay");
        init.keyDelayMs = isKeyDelay.isValid() ? isKeyDelay.toInt() : 125;

        MouseSetting mouse;
        mouse.load();
        init.invertZoom = mouse.invertZoom;
        init.middleMouseMoveCanvas = mouse.middleMouseMoveCanvas;

        mModel->seed(init);
    }

    // The two option lists the model cannot build itself: the theme ids come from
    // gui::GUIResources::themeList() (the widget built its buttons from the same
    // list) and the accent previews are the theme's own hues. The easing names are
    // the model's own now - it reads them from util::Easing's display table, so the
    // combo, the timeline's menu and the picker's tiles cannot disagree - and the
    // assert below is the count that ordering implies: the table's non-Custom
    // entries plus the Auto Clamped MODE option appended after them.
    // Everything else is model-built.
    XC_ASSERT(mModel->easingOptions().size() == GeneralSettingViewModel::autoClampedOptionIndex() + 1);
    {
        const QStringList themeIds = mGUIResources.themeList();
        const QStringList themeLabels{tr("System"), tr("Light"), tr("Dark")};
        QVariantList themes;
        for (int i = 0; i < themeIds.size(); ++i) {
            QVariantMap map;
            map.insert(QStringLiteral("value"), themeIds.at(i));
            // The widget indexed a fixed 3-label list and would have thrown on a
            // fourth theme; the id is the fallback here instead.
            map.insert(
                QStringLiteral("label"), i < themeLabels.size() ? themeLabels.at(i) : themeIds.at(i)
            );
            themes.append(map);
        }
        mModel->setThemeOptions(themes);
    }
    {
        const theme::Colors c = theme::Colors::current();
        QVariantList accents;
        for (int i = 0; i < theme::kAccentCount; ++i) {
            const theme::AccentColor acc = (theme::AccentColor)i;
            QVariantMap map;
            map.insert(QStringLiteral("value"), i);
            // Preview the accent hue at a mid lightness (accentSwatch): the fill token
            // itself is theme-tuned (pale in light, deep in dark) and reads poorly as a
            // bare swatch on the dialog background.
            map.insert(
                QStringLiteral("swatch"),
                (c.isDark ? theme::Colors::dark(acc) : theme::Colors::light(acc)).accentSwatch.name()
            );
            accents.append(map);
        }
        mModel->setAccentOptions(accents);
    }

    mTheme = new QmlTheme(this);

    mQuick = new QQuickWidget(this);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // The theme and the model are context properties, exactly as the export shell
    // does it: the scene reads `theme.base` and `generalSettingModel.languageIndex`
    // with no C++ type registration. NOT "model": every Repeater and ListView has a
    // `model` property of its own, so a scene binding would resolve the right-hand
    // side to that shadowing property.
    mQuick->rootContext()->setContextProperty(QStringLiteral("generalSettingModel"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // The scene's backdrop: the theme's own window colour, so a resize does not flash
    // white.
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/GeneralSettingDialog.qml")));

    // Size to the scene, now that it exists (see resizeToScene).
    resizeToScene();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mQuick);

    // The scene's buttons do not know about QDialog, and they should not: the model
    // carries the outcome, and the shell turns the request into the dialog result and
    // writes the settings. Saving happens BEFORE accept() so the call site's
    // post-exec() read of the *HasChanged() predicates sees the same model state the
    // settings were written from.
    connect(mModel, &GeneralSettingViewModel::acceptRequested, this, [this]() {
        saveSettings();
        accept();
    });
    connect(mModel, &GeneralSettingViewModel::cancelRequested, this, &QDialog::reject);
    // Each tab is a different set of rows, so the window re-hugs the scene when the
    // user switches (the same size-to-scene rule as on open).
    connect(mModel, &GeneralSettingViewModel::currentTabChanged, this, &GeneralSettingDialog::resizeToScene);
    // The reset also raises the app's info popup, which is a WIDGET surface: the scene
    // asks, the shell answers.
    connect(mModel, &GeneralSettingViewModel::resetRecentFilesRequested, this, []() {
        QSettings().remove(QStringLiteral("projectloader/recents"));
        MainWindow::showInfoPopup(tr("Success"), tr("All entries have been successfully removed"), "Info");
    });
    // "Show mesh when selecting FFD" is not part of the accepted batch: the widget
    // wrote its key the moment the box was clicked, and the port keeps that.
    connect(mModel, &GeneralSettingViewModel::autoShowMeshChanged, this, [this]() {
        QSettings().setValue(QStringLiteral("generalsettings/tools/autoshowmesh"), mModel->autoShowMesh());
    });
}

GeneralSettingDialog::~GeneralSettingDialog() {
    // Tear the scene down before the model/theme context objects die, so the scene's
    // binding teardown never re-reads a destroyed context property (the same rule the
    // export and new-project shells follow).
    delete mQuick;
    mQuick = nullptr;
}

void GeneralSettingDialog::resizeToScene() {
    // The window HUGS the scene: the QML root's implicitHeight is the natural height
    // of the current tab's rows plus the tab strip and the footer band, and the shell
    // sizes the dialog to it. Both dimensions yield to the screen, and the scene's own
    // scroll host covers a screen shorter than the rows.
    constexpr int kDefaultWidth = 520;
    constexpr int kMinimumWidth = 420;
    constexpr int kMinimumHeight = 260;
    const QRect avail = screen() ? screen()->availableGeometry() : QRect();
    const int maxW = avail.width() > 0 ? avail.width() : kDefaultWidth;
    const int maxH = avail.height() > 0 ? avail.height() : kMinimumHeight;
    int wantedH = kMinimumHeight;
    if (auto* scene = mQuick->rootObject()) {
        const int natural = qRound(scene->implicitHeight());
        if (natural > 0)
            wantedH = natural;
    }
    setMinimumSize(qMin(kMinimumWidth, maxW), qMin(kMinimumHeight, maxH));
    resize(qMin(kDefaultWidth, maxW), qMin(wantedH, maxH));
}

void GeneralSettingDialog::selectTab(int aIndex) { mModel->setCurrentTab(aIndex); }

bool GeneralSettingDialog::easingHasChanged() { return mModel->easingHasChanged(); }

bool GeneralSettingDialog::rangeHasChanged() { return mModel->rangeHasChanged(); }

bool GeneralSettingDialog::languageHasChanged() { return mModel->languageHasChanged(); }

bool GeneralSettingDialog::timeFormatHasChanged() { return mModel->timeFormatHasChanged(); }

bool GeneralSettingDialog::autoSaveHasChanged() { return mModel->autoSaveHasChanged(); }

bool GeneralSettingDialog::autoSaveDelayHasChanged() { return mModel->autoSaveDelayHasChanged(); }

bool GeneralSettingDialog::resIDCheckHasChanged() { return mModel->resIDCheckHasChanged(); }

bool GeneralSettingDialog::themeHasChanged() { return mModel->themeHasChanged(); }

bool GeneralSettingDialog::donationHasChanged() { return mModel->donationHasChanged(); }

bool GeneralSettingDialog::forceSolverLoadHasChanged() { return mModel->forceSolverLoadHasChanged(); }

bool GeneralSettingDialog::ignoreWarningsHasChanged() { return mModel->ignoreWarningsHasChanged(); }

bool GeneralSettingDialog::cbCopyHasChanged() { return mModel->cbCopyHasChanged(); }

bool GeneralSettingDialog::keyDelayHasChanged() { return mModel->keyDelayHasChanged(); }

bool GeneralSettingDialog::mouseHasChanged() { return mModel->mouseHasChanged(); }

QString GeneralSettingDialog::theme() { return mModel->themeKey(); }

theme::AccentColor GeneralSettingDialog::accent() { return (theme::AccentColor)mModel->accentIndex(); }

bool GeneralSettingDialog::accentHasChanged() { return mModel->accentHasChanged(); }

void GeneralSettingDialog::saveSettings() {
    QSettings settings;
    if (languageHasChanged())
        settings.setValue("generalsettings/language", indexToLanguage(mModel->languageIndex()));

    if (easingHasChanged()) {
        // The option's stored value: an easing type's enum ordinal, or the retired
        // 14 when the option is the automatic pair (util::Easing::defaultParam maps
        // that back to Custom + kDefaultAutomaticHandleMode).
        settings.setValue(
            "generalsettings/easing", GeneralSettingViewModel::storedValueForOption(mModel->easingIndex())
        );
    }
    if (rangeHasChanged()) {
        settings.setValue("generalsettings/range", indexToRange(mModel->rangeIndex()));
    }
    if (timeFormatHasChanged()) {
        // Store the enum value (not the picker index) so a saved format is stable even
        // if the picker order changes; load maps it back.
        settings.setValue(
            "generalsettings/ui/timeformat", int(timeFormatTypeAt(mModel->timeFormatIndex()))
        );
    }
    if (themeHasChanged()) {
        settings.setValue("generalsettings/ui/theme", mModel->themeKey());
    }
    if (donationHasChanged()) {
        settings.setValue("generalsettings/ui/donationAllowed", mModel->donationAllowed());
    }
    // TODO: Bandaid fix, needs solving
    if (forceSolverLoadHasChanged()) {
        settings.setValue("forceSolverLoad", mModel->forceSolverLoad());
    }
    if (ignoreWarningsHasChanged()) {
        settings.setValue("export_ignore_warnings", mModel->ignoreWarnings());
    }
    if (autoSaveHasChanged()) {
        settings.setValue("generalsettings/projects/enableAutosave", mModel->autoSave());
    }
    if (autoSaveDelayHasChanged()) {
        settings.setValue("generalsettings/projects/autosaveDelay", mModel->autoSaveDelayMinutes());
    }
    if (resIDCheckHasChanged()) {
        settings.setValue("res_id_check", mModel->resIDCheck());
    }
    if (cbCopyHasChanged()) {
        settings.setValue("generalsettings/keys/autocb", mModel->autoCbCopy());
    }
    if (keyDelayHasChanged()) {
        settings.setValue("generalsettings/keybindings/keyDelay", mModel->keyDelayMs());
    }
    if (mouseHasChanged()) {
        MouseSetting newValues;
        newValues.invertZoom = mModel->invertZoom();
        newValues.middleMouseMoveCanvas = mModel->middleMouseMoveCanvas();
        newValues.save();
        mViaPoint.mouseSetting() = newValues;
    }
}

} // namespace gui
