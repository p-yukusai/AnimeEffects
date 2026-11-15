#include "gui/qml/GeneralSettingViewModel.h"

#include <QCoreApplication>
#include <QVariantMap>

#include "util/Easing.h"

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
    // Built from util::Easing's own display table (util/Easing.cpp's
    // kEasingDisplayOrder), with Type_Custom dropped - a global default carries no
    // bezier control points - so the list IS the order the shell's save path reads
    // (typeAtDisplayIndex) and it can never drift from the timeline's menu or the
    // picker's tiles. The names are the SAME strings those two show
    // (Easing::getTypeName, context "EasingName"), which is what makes them
    // translate: a hand-written label table here was invisible to lupdate (the
    // literal is an argument to a helper, not to a tr() call) AND asked for a
    // context no catalog carries ("util::EasingName"), so every language showed
    // these names in English while the same names were translated in the menu.
    QVariantList out;
    const int count = util::Easing::displayCount();
    for (int i = 0; i < count; ++i) {
        const util::Easing::Type type = util::Easing::typeAtDisplayIndex(i);
        if (type == util::Easing::Type_Custom)
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("label"), util::Easing::getTypeName(type));
        map.insert(QStringLiteral("value"), out.size());
        out.append(map);
    }
    // The MODE pair a default can be, as its own option (see the header).
    QVariantMap automatic;
    automatic.insert(
        QStringLiteral("label"),
        util::Easing::getHandleModeName(util::Easing::kDefaultAutomaticHandleMode));
    automatic.insert(QStringLiteral("value"), autoClampedOptionIndex());
    out.append(automatic);
    return out;
}

int GeneralSettingViewModel::autoClampedOptionIndex() {
    // The non-Custom entries, in display order; the mode option follows them.
    int count = 0;
    for (int i = 0; i < util::Easing::displayCount(); ++i) {
        if (util::Easing::typeAtDisplayIndex(i) != util::Easing::Type_Custom)
            ++count;
    }
    return count;
}

int GeneralSettingViewModel::optionForSetting(const QVariant& aStored) {
    const int linear = util::Easing::displayIndexOf(util::Easing::Type_Linear);
    if (!aStored.isValid())
        return linear;

    bool isOrdinal = false;
    const int ordinal = aStored.toInt(&isOrdinal);
    if (isOrdinal) {
        if (ordinal == int(util::Easing::Type_TERM))
            return autoClampedOptionIndex(); // the retired Auto id: the clamped pair
        const util::Easing::Type type = static_cast<util::Easing::Type>(ordinal);
        const int index = util::Easing::displayIndexOf(type);
        return (type == util::Easing::Type_Custom || index < 0 || index >= autoClampedOptionIndex())
                   ? linear
                   : index;
    }

    const QString name = aStored.toString();
    if (name == QStringLiteral("Auto"))
        return autoClampedOptionIndex(); // the retired NAME: the clamped pair
    const util::Easing::Type type = util::Easing::easingToEnum(name);
    const int index = util::Easing::displayIndexOf(type);
    return (type == util::Easing::Type_Custom || index < 0 || index >= autoClampedOptionIndex())
               ? linear
               : index;
}

int GeneralSettingViewModel::storedValueForOption(const int aOptionIndex) {
    if (aOptionIndex == autoClampedOptionIndex())
        // 14 is Type_TERM, deliberately not reused: defaultParam reads it as
        // Custom + kDefaultAutomaticHandleMode.
        return int(util::Easing::Type_TERM);
    return int(util::Easing::typeAtDisplayIndex(aOptionIndex));
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
    mInitialInvertZoom = aInit.invertZoom;
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
    setInvertZoom(aInit.invertZoom);
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

void GeneralSettingViewModel::setInvertZoom(bool aInvert) {
    if (mInvertZoom == aInvert)
        return;
    mInvertZoom = aInvert;
    emit invertZoomChanged();
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
    return mInitialInvertZoom != mInvertZoom ||
        mInitialMiddleMouseMoveCanvas != mMiddleMouseMoveCanvas;
}

void GeneralSettingViewModel::requestResetRecentFiles() { emit resetRecentFilesRequested(); }

void GeneralSettingViewModel::accept() { emit acceptRequested(); }

void GeneralSettingViewModel::cancel() { emit cancelRequested(); }

} // namespace gui
