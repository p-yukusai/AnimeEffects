#include "gui/qml/GeneralSettingViewModel.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace {

// One {label, value} entry per row, value = the combo index.
QVariantList makeIndexedOptions(const char* aContext, const char* const* aLabels, int aCount) {
    QVariantList out;
    for (int i = 0; i < aCount; ++i) {
        QVariantMap map;
        map.insert(QStringLiteral("label"), QCoreApplication::translate(aContext, aLabels[i]));
        map.insert(QStringLiteral("value"), i);
        out.append(map);
    }
    return out;
}

} // namespace

namespace gui {

GeneralSettingViewModel::GeneralSettingViewModel(QObject* aParent) : QObject(aParent) {}

QVariantList GeneralSettingViewModel::languageOptions() const {
    // Order and count are the widget's (kLanguageTypeCount = 7), and the labels are
    // its existing translation entries.
    static const char* const kLabels[] = {
        "Auto", "English", "Japanese", "Chinese", "Spanish", "Russian", "Korean",
    };
    return makeIndexedOptions("GeneralSettingsDialog", kLabels, 7);
}

QVariantList GeneralSettingViewModel::easingOptions() const {
    // util::Easing::displayCount() - 1 entries in util/Easing.cpp's
    // kEasingDisplayOrder, with Type_Custom dropped (a global default carries no
    // bezier control points). The shell asserts this count against
    // util::Easing::displayCount() - 1, so the two cannot silently drift apart;
    // the ORDER here must match kEasingDisplayOrder, which is what
    // util::Easing::typeAtDisplayIndex() (the shell's save path) reads.
    static const char* const kLabels[] = {
        "Floor", "Round", "Linear", "Sine", "Quad", "Cubic", "Quart",
        "Quint", "Expo", "Circ", "Back", "Elastic", "Bounce",
    };
    return makeIndexedOptions("util::EasingName", kLabels, 13);
}

QVariantList GeneralSettingViewModel::rangeOptions() const {
    // "All" IS InOut - the widget's indexToRange() note says so.
    static const char* const kLabels[] = {"In", "Out", "All"};
    return makeIndexedOptions("GeneralSettingsDialog", kLabels, 3);
}

QVariantList GeneralSettingViewModel::timeFormatOptions() const {
    // The picker order of the widget's kTimeFormatOrder. Frames_From1 is absent
    // (frames are natively 1-indexed, so it is identical to Frames_From0) but the
    // shell's load path still maps the old saved value onto this list.
    static const char* const kLabels[] = {
        "Frame number",
        "Seconds",
        "Seconds + Frame (SS+FF)",
        "Timecode (SMPTE) (HH:MM:SS+FF)",
        "Timecode (HH:MM:SS:mmm)",
    };
    return makeIndexedOptions("GeneralSettingsDialog", kLabels, 5);
}

void GeneralSettingViewModel::setThemeOptions(const QVariantList& aOptions) {
    if (mThemeOptions == aOptions)
        return;
    mThemeOptions = aOptions;
    emit themeOptionsChanged();
}

void GeneralSettingViewModel::seed(const Init& aInit) {
    mInitialLanguageIndex = aInit.languageIndex;
    mInitialEasingIndex = aInit.easingIndex;
    mInitialRangeIndex = aInit.rangeIndex;
    mInitialTimeFormatIndex = aInit.timeFormatIndex;
    mInitialThemeKey = aInit.themeKey;
    mInitialAccentIndex = aInit.accentIndex;
    mInitialDonationAllowed = aInit.donationAllowed;
    mInitialAutoSave = aInit.autoSave;
    mInitialAutoSaveDelayMinutes = aInit.autoSaveDelayMinutes;
    mInitialAutoCbCopy = aInit.autoCbCopy;
    mInitialResIDCheck = aInit.resIDCheck;
    mInitialIgnoreWarnings = aInit.ignoreWarnings;
    mInitialForceSolverLoad = aInit.forceSolverLoad;
    mInitialKeyDelayMs = aInit.keyDelayMs;
    mInitialInvertMainViewScaling = aInit.invertMainViewScaling;
    mInitialInvertTimeLineScaling = aInit.invertTimeLineScaling;
    mInitialMiddleMouseMoveCanvas = aInit.middleMouseMoveCanvas;

    // Through the setters, so the "now" side moves and every listener sees it.
    setLanguageIndex(aInit.languageIndex);
    setEasingIndex(aInit.easingIndex);
    setRangeIndex(aInit.rangeIndex);
    setTimeFormatIndex(aInit.timeFormatIndex);
    setThemeKey(aInit.themeKey);
    setAccentIndex(aInit.accentIndex);
    setDonationAllowed(aInit.donationAllowed);
    setAutoSave(aInit.autoSave);
    setAutoSaveDelayMinutes(aInit.autoSaveDelayMinutes);
    setAutoShowMesh(aInit.autoShowMesh);
    setAutoCbCopy(aInit.autoCbCopy);
    setResIDCheck(aInit.resIDCheck);
    setIgnoreWarnings(aInit.ignoreWarnings);
    setForceSolverLoad(aInit.forceSolverLoad);
    setKeyDelayMs(aInit.keyDelayMs);
    setInvertMainViewScaling(aInit.invertMainViewScaling);
    setInvertTimeLineScaling(aInit.invertTimeLineScaling);
    setMiddleMouseMoveCanvas(aInit.middleMouseMoveCanvas);
}

void GeneralSettingViewModel::setCurrentTab(int aTab) {
    if (mCurrentTab == aTab)
        return;
    mCurrentTab = aTab;
    emit currentTabChanged();
}

void GeneralSettingViewModel::setLanguageIndex(int aIndex) {
    if (mLanguageIndex == aIndex)
        return;
    mLanguageIndex = aIndex;
    emit languageIndexChanged();
}

void GeneralSettingViewModel::setEasingIndex(int aIndex) {
    if (mEasingIndex == aIndex)
        return;
    mEasingIndex = aIndex;
    emit easingIndexChanged();
}

void GeneralSettingViewModel::setRangeIndex(int aIndex) {
    if (mRangeIndex == aIndex)
        return;
    mRangeIndex = aIndex;
    emit rangeIndexChanged();
}

void GeneralSettingViewModel::setThemeKey(const QString& aKey) {
    if (mThemeKey == aKey)
        return;
    mThemeKey = aKey;
    emit themeKeyChanged();
}

void GeneralSettingViewModel::setAccentIndex(int aIndex) {
    if (mAccentIndex == aIndex)
        return;
    mAccentIndex = aIndex;
    emit accentIndexChanged();
}

void GeneralSettingViewModel::setDonationAllowed(bool aAllowed) {
    if (mDonationAllowed == aAllowed)
        return;
    mDonationAllowed = aAllowed;
    emit donationAllowedChanged();
}

void GeneralSettingViewModel::setAutoSave(bool aAutoSave) {
    if (mAutoSave == aAutoSave)
        return;
    mAutoSave = aAutoSave;
    emit autoSaveChanged();
}

void GeneralSettingViewModel::setAutoSaveDelayMinutes(int aMinutes) {
    // Clamp at the model, not at the control: 0 would arm a timer that fires
    // continuously, and any writer (a scrub, a typed edit, a test) has to obey the
    // same floor the widget QSpinBox had.
    const int clamped = qMax(aMinutes, 1);
    if (mAutoSaveDelayMinutes == clamped)
        return;
    mAutoSaveDelayMinutes = clamped;
    emit autoSaveDelayMinutesChanged();
}

void GeneralSettingViewModel::setAutoShowMesh(bool aAutoShowMesh) {
    if (mAutoShowMesh == aAutoShowMesh)
        return;
    mAutoShowMesh = aAutoShowMesh;
    emit autoShowMeshChanged();
}

void GeneralSettingViewModel::setAutoCbCopy(bool aAutoCbCopy) {
    if (mAutoCbCopy == aAutoCbCopy)
        return;
    mAutoCbCopy = aAutoCbCopy;
    emit autoCbCopyChanged();
}

void GeneralSettingViewModel::setResIDCheck(bool aCheck) {
    if (mResIDCheck == aCheck)
        return;
    mResIDCheck = aCheck;
    emit resIDCheckChanged();
}

void GeneralSettingViewModel::setIgnoreWarnings(bool aIgnore) {
    if (mIgnoreWarnings == aIgnore)
        return;
    mIgnoreWarnings = aIgnore;
    emit ignoreWarningsChanged();
}

void GeneralSettingViewModel::setForceSolverLoad(bool aForce) {
    if (mForceSolverLoad == aForce)
        return;
    mForceSolverLoad = aForce;
    emit forceSolverLoadChanged();
}

void GeneralSettingViewModel::setTimeFormatIndex(int aIndex) {
    if (mTimeFormatIndex == aIndex)
        return;
    mTimeFormatIndex = aIndex;
    emit timeFormatIndexChanged();
}

void GeneralSettingViewModel::setKeyDelayMs(int aMs) {
    const int clamped = qMax(aMs, 0);
    if (mKeyDelayMs == clamped)
        return;
    mKeyDelayMs = clamped;
    emit keyDelayMsChanged();
}

void GeneralSettingViewModel::setInvertMainViewScaling(bool aInvert) {
    if (mInvertMainViewScaling == aInvert)
        return;
    mInvertMainViewScaling = aInvert;
    emit invertMainViewScalingChanged();
}

void GeneralSettingViewModel::setInvertTimeLineScaling(bool aInvert) {
    if (mInvertTimeLineScaling == aInvert)
        return;
    mInvertTimeLineScaling = aInvert;
    emit invertTimeLineScalingChanged();
}

void GeneralSettingViewModel::setMiddleMouseMoveCanvas(bool aMove) {
    if (mMiddleMouseMoveCanvas == aMove)
        return;
    mMiddleMouseMoveCanvas = aMove;
    emit middleMouseMoveCanvasChanged();
}

void GeneralSettingViewModel::setAccentOptions(const QVariantList& aOptions) {
    if (mAccentOptions == aOptions)
        return;
    mAccentOptions = aOptions;
    emit accentOptionsChanged();
}

bool GeneralSettingViewModel::languageHasChanged() const { return mInitialLanguageIndex != mLanguageIndex; }

bool GeneralSettingViewModel::easingHasChanged() const { return mInitialEasingIndex != mEasingIndex; }

bool GeneralSettingViewModel::rangeHasChanged() const { return mInitialRangeIndex != mRangeIndex; }

bool GeneralSettingViewModel::timeFormatHasChanged() const { return mInitialTimeFormatIndex != mTimeFormatIndex; }

bool GeneralSettingViewModel::themeHasChanged() const { return mInitialThemeKey != mThemeKey; }

bool GeneralSettingViewModel::accentHasChanged() const { return mInitialAccentIndex != mAccentIndex; }

bool GeneralSettingViewModel::donationHasChanged() const { return mInitialDonationAllowed != mDonationAllowed; }

bool GeneralSettingViewModel::autoSaveHasChanged() const { return mInitialAutoSave != mAutoSave; }

bool GeneralSettingViewModel::autoSaveDelayHasChanged() const {
    return mInitialAutoSaveDelayMinutes != mAutoSaveDelayMinutes;
}

bool GeneralSettingViewModel::resIDCheckHasChanged() const { return mInitialResIDCheck != mResIDCheck; }

bool GeneralSettingViewModel::cbCopyHasChanged() const { return mInitialAutoCbCopy != mAutoCbCopy; }

bool GeneralSettingViewModel::forceSolverLoadHasChanged() const {
    return mInitialForceSolverLoad != mForceSolverLoad;
}

bool GeneralSettingViewModel::ignoreWarningsHasChanged() const {
    return mInitialIgnoreWarnings != mIgnoreWarnings;
}

bool GeneralSettingViewModel::keyDelayHasChanged() const { return mInitialKeyDelayMs != mKeyDelayMs; }

bool GeneralSettingViewModel::mouseHasChanged() const {
    return mInitialInvertMainViewScaling != mInvertMainViewScaling ||
        mInitialInvertTimeLineScaling != mInvertTimeLineScaling ||
        mInitialMiddleMouseMoveCanvas != mMiddleMouseMoveCanvas;
}

void GeneralSettingViewModel::requestResetRecentFiles() { emit resetRecentFilesRequested(); }

void GeneralSettingViewModel::accept() { emit acceptRequested(); }

void GeneralSettingViewModel::cancel() { emit cancelRequested(); }

} // namespace gui
