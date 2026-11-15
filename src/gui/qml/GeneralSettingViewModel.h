#ifndef GUI_QML_GENERALSETTINGVIEWMODEL_H
#define GUI_QML_GENERALSETTINGVIEWMODEL_H

#include <QObject>
#include <QString>
#include <QVariantList>

namespace gui {

// Everything the settings dialog decides, with no widgets, no GL and no scene.
//
// This is the port of the form that used to be built inside
// gui/GeneralSettingDialog.cpp out of a QTabWidget, four QFormLayouts, a handful of
// QComboBoxes/QSpinBoxes/QCheckBoxes and two QButtonGroups. The values, their
// defaults and the settings keys they come from are unchanged; what moved is WHERE
// they live. The widget held the "before" value in a member and compared it against
// a control on every *HasChanged() call; the model holds both the "before" and the
// "now" side, so the same predicates can be asked of it directly - which is what
// lets tests/general_setting_form.cpp exercise them with no window at all.
//
// The model is deliberately widget-free and Qt6::Core only. It must NOT pull in
// gui/GUIResources.h, gui/ViaPoint.h or core/Project.h: any of those drags the
// widget stack (or the theme's QColor palette) into anything that links this file,
// which would make the headless test that constructs this class link the GUI. The
// shell reads QSettings/GUIResources/MouseSetting and seeds plain values here; the
// model never names a widget, a theme::AccentColor or a MouseSetting.
//
// It is set as the QML context property "generalSettingModel", NOT "model": a
// Repeater (and a ListView) has a `model` property of its own, so a scene binding
// `something: model.languageIndex` would resolve the right-hand side to that
// shadowing property.
class GeneralSettingViewModel : public QObject {
    Q_OBJECT

    // Which of the four tabs the scene shows (General 0, QoL 1, Keyboard 2,
    // Mouse 3). selectTab() on the shell writes this; the scene's tab strip both
    // reads and writes it.
    Q_PROPERTY(int currentTab READ currentTab WRITE setCurrentTab NOTIFY currentTabChanged)

    // ---- General ----------------------------------------------------------
    Q_PROPERTY(int languageIndex READ languageIndex WRITE setLanguageIndex NOTIFY languageIndexChanged)
    Q_PROPERTY(int easingIndex READ easingIndex WRITE setEasingIndex NOTIFY easingIndexChanged)
    Q_PROPERTY(int rangeIndex READ rangeIndex WRITE setRangeIndex NOTIFY rangeIndexChanged)
    // The theme's settings id ("system"/"light"/"dark"), the value
    // "generalsettings/ui/theme" carries - not a display index.
    Q_PROPERTY(QString themeKey READ themeKey WRITE setThemeKey NOTIFY themeKeyChanged)
    // The theme::AccentColor ordinal, the value "generalsettings/ui/accent" is
    // derived from. The shell converts to/from theme::AccentColor; the model only
    // ever sees the number.
    Q_PROPERTY(int accentIndex READ accentIndex WRITE setAccentIndex NOTIFY accentIndexChanged)
    Q_PROPERTY(bool donationAllowed READ donationAllowed WRITE setDonationAllowed NOTIFY donationAllowedChanged)

    // ---- QoL --------------------------------------------------------------
    Q_PROPERTY(bool autoSave READ autoSave WRITE setAutoSave NOTIFY autoSaveChanged)
    // Minutes between autosaves. The floor is 1 (0 would arm a continuously-firing
    // timer), the same clamp the widget QSpinBox and MainWindow::updateAutosaveState
    // both apply.
    Q_PROPERTY(int autoSaveDelayMinutes READ autoSaveDelayMinutes WRITE setAutoSaveDelayMinutes NOTIFY autoSaveDelayMinutesChanged)
    // Not part of the dialog's changed/saved batch: the widget wrote this key the
    // moment its box was clicked (see the shell's live-save connection), and the
    // port keeps that.
    Q_PROPERTY(bool autoShowMesh READ autoShowMesh WRITE setAutoShowMesh NOTIFY autoShowMeshChanged)
    Q_PROPERTY(bool autoCbCopy READ autoCbCopy WRITE setAutoCbCopy NOTIFY autoCbCopyChanged)
    Q_PROPERTY(bool resIDCheck READ resIDCheck WRITE setResIDCheck NOTIFY resIDCheckChanged)
    Q_PROPERTY(bool ignoreWarnings READ ignoreWarnings WRITE setIgnoreWarnings NOTIFY ignoreWarningsChanged)
    Q_PROPERTY(bool forceSolverLoad READ forceSolverLoad WRITE setForceSolverLoad NOTIFY forceSolverLoadChanged)
    Q_PROPERTY(int timeFormatIndex READ timeFormatIndex WRITE setTimeFormatIndex NOTIFY timeFormatIndexChanged)

    // ---- Keyboard ---------------------------------------------------------
    // Milliseconds the global key command waits before firing.
    Q_PROPERTY(int keyDelayMs READ keyDelayMs WRITE setKeyDelayMs NOTIFY keyDelayMsChanged)

    // ---- Mouse ------------------------------------------------------------
    Q_PROPERTY(bool invertMainViewScaling READ invertMainViewScaling WRITE setInvertMainViewScaling NOTIFY invertMainViewScalingChanged)
    Q_PROPERTY(bool invertTimeLineScaling READ invertTimeLineScaling WRITE setInvertTimeLineScaling NOTIFY invertTimeLineScalingChanged)
    Q_PROPERTY(bool middleMouseMoveCanvas READ middleMouseMoveCanvas WRITE setMiddleMouseMoveCanvas NOTIFY middleMouseMoveCanvasChanged)

    // ---- the lists the scene repeats over ---------------------------------
    // [{label, value}] with value = the combo index. The labels are the EXISTING
    // C++ translation entries of the widget form (contexts "GeneralSettingsDialog"
    // and "util::EasingName"), NOT new qsTr() strings: the .ts catalogs already
    // carry them and re-spelling them in QML under the scene's context would be a
    // silent translation regression.
    Q_PROPERTY(QVariantList languageOptions READ languageOptions CONSTANT)
    Q_PROPERTY(QVariantList easingOptions READ easingOptions CONSTANT)
    Q_PROPERTY(QVariantList rangeOptions READ rangeOptions CONSTANT)
    Q_PROPERTY(QVariantList timeFormatOptions READ timeFormatOptions CONSTANT)
    // [{label, value}] with value = the theme's settings id. The SHELL seeds it from
    // gui::GUIResources::themeList() (mirroring the widget, which built its buttons
    // from that list), so like accentOptions it can change after construction.
    Q_PROPERTY(QVariantList themeOptions READ themeOptions WRITE setThemeOptions NOTIFY themeOptionsChanged)
    // [{value, swatch}] with value = the theme::AccentColor ordinal and swatch a
    // "#rrggbb" preview. The SHELL seeds it (the swatch hue is the theme's own -
    // see the widget's note on accentSwatch).
    Q_PROPERTY(QVariantList accentOptions READ accentOptions WRITE setAccentOptions NOTIFY accentOptionsChanged)

public:
    // The values the shell reads (QSettings + GUIResources + MouseSetting) and
    // hands over in one piece. The member initializers ARE the dialog's documented
    // defaults when no setting is present; seed() installs the same set as both the
    // "before" and the "now" side, so nothing reads as changed on open.
    struct Init {
        int languageIndex = 0;              // "Auto"
        // Display index of util::Easing::Type_Linear in util/Easing.cpp's
        // kEasingDisplayOrder (Floor, Round, Linear, ...). The shell always seeds
        // the real value via util::Easing::displayIndexOf; this is only the fallback
        // for a bare model.
        int easingIndex = 2;
        int rangeIndex = 2;                 // "All"
        int timeFormatIndex = 0;            // Frame number
        QString themeKey = QStringLiteral("dark");
        int accentIndex = 6;                // theme::kDefaultAccent (kAccentViolet)
        bool donationAllowed = true;
        bool autoSave = true;
        int autoSaveDelayMinutes = 5;
        bool autoShowMesh = false;
        bool autoCbCopy = false;
        bool resIDCheck = true;
        bool ignoreWarnings = false;
        bool forceSolverLoad = false;
        int keyDelayMs = 125;
        bool invertMainViewScaling = false;
        bool invertTimeLineScaling = false;
        bool middleMouseMoveCanvas = true;
    };

    explicit GeneralSettingViewModel(QObject* aParent = nullptr);

    // Install aInit as BOTH the initial and the current value of every field, so
    // the *HasChanged() predicates all read false immediately afterwards. Called
    // once by the shell, before the scene is built.
    void seed(const Init& aInit);

    int currentTab() const { return mCurrentTab; }
    int languageIndex() const { return mLanguageIndex; }
    int easingIndex() const { return mEasingIndex; }
    int rangeIndex() const { return mRangeIndex; }
    QString themeKey() const { return mThemeKey; }
    int accentIndex() const { return mAccentIndex; }
    bool donationAllowed() const { return mDonationAllowed; }
    bool autoSave() const { return mAutoSave; }
    int autoSaveDelayMinutes() const { return mAutoSaveDelayMinutes; }
    bool autoShowMesh() const { return mAutoShowMesh; }
    bool autoCbCopy() const { return mAutoCbCopy; }
    bool resIDCheck() const { return mResIDCheck; }
    bool ignoreWarnings() const { return mIgnoreWarnings; }
    bool forceSolverLoad() const { return mForceSolverLoad; }
    int timeFormatIndex() const { return mTimeFormatIndex; }
    int keyDelayMs() const { return mKeyDelayMs; }
    bool invertMainViewScaling() const { return mInvertMainViewScaling; }
    bool invertTimeLineScaling() const { return mInvertTimeLineScaling; }
    bool middleMouseMoveCanvas() const { return mMiddleMouseMoveCanvas; }

    QVariantList languageOptions() const;
    QVariantList easingOptions() const;
    QVariantList rangeOptions() const;
    QVariantList timeFormatOptions() const;
    QVariantList themeOptions() const { return mThemeOptions; }
    QVariantList accentOptions() const { return mAccentOptions; }

    void setCurrentTab(int aTab);
    void setLanguageIndex(int aIndex);
    void setEasingIndex(int aIndex);
    void setRangeIndex(int aIndex);
    void setThemeKey(const QString& aKey);
    void setAccentIndex(int aIndex);
    void setDonationAllowed(bool aAllowed);
    void setAutoSave(bool aAutoSave);
    // Clamps to >= 1 (0 would arm a continuously-firing timer) - the same floor the
    // widget's QSpinBox and MainWindow::updateAutosaveState apply.
    void setAutoSaveDelayMinutes(int aMinutes);
    void setAutoShowMesh(bool aAutoShowMesh);
    void setAutoCbCopy(bool aAutoCbCopy);
    void setResIDCheck(bool aCheck);
    void setIgnoreWarnings(bool aIgnore);
    void setForceSolverLoad(bool aForce);
    void setTimeFormatIndex(int aIndex);
    // Clamps to >= 0 (the widget QSpinBox's floor).
    void setKeyDelayMs(int aMs);
    void setInvertMainViewScaling(bool aInvert);
    void setInvertTimeLineScaling(bool aInvert);
    void setMiddleMouseMoveCanvas(bool aMove);
    void setAccentOptions(const QVariantList& aOptions);
    void setThemeOptions(const QVariantList& aOptions);

    // The "before vs now" predicates the dialog's public API is built from. Each
    // compares the seeded value with the current one; the widget's had the same
    // bodies (mInitialX != control->value()).
    Q_INVOKABLE bool languageHasChanged() const;
    Q_INVOKABLE bool easingHasChanged() const;
    Q_INVOKABLE bool rangeHasChanged() const;
    Q_INVOKABLE bool timeFormatHasChanged() const;
    Q_INVOKABLE bool themeHasChanged() const;
    Q_INVOKABLE bool accentHasChanged() const;
    Q_INVOKABLE bool donationHasChanged() const;
    Q_INVOKABLE bool autoSaveHasChanged() const;
    Q_INVOKABLE bool autoSaveDelayHasChanged() const;
    Q_INVOKABLE bool resIDCheckHasChanged() const;
    Q_INVOKABLE bool cbCopyHasChanged() const;
    Q_INVOKABLE bool forceSolverLoadHasChanged() const;
    Q_INVOKABLE bool ignoreWarningsHasChanged() const;
    Q_INVOKABLE bool keyDelayHasChanged() const;
    Q_INVOKABLE bool mouseHasChanged() const;

    // The scene's verbs. The reset is not a plain QSettings write: it also raises
    // the app's info popup, and both belong to the shell, so the model only asks
    // for it. accept/cancel carry the outcome the shell turns into the dialog
    // result.
    Q_INVOKABLE void requestResetRecentFiles();
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();

signals:
    void currentTabChanged();
    void languageIndexChanged();
    void easingIndexChanged();
    void rangeIndexChanged();
    void themeKeyChanged();
    void accentIndexChanged();
    void donationAllowedChanged();
    void autoSaveChanged();
    void autoSaveDelayMinutesChanged();
    void autoShowMeshChanged();
    void autoCbCopyChanged();
    void resIDCheckChanged();
    void ignoreWarningsChanged();
    void forceSolverLoadChanged();
    void timeFormatIndexChanged();
    void keyDelayMsChanged();
    void invertMainViewScalingChanged();
    void invertTimeLineScalingChanged();
    void middleMouseMoveCanvasChanged();
    void accentOptionsChanged();
    void themeOptionsChanged();

    void resetRecentFilesRequested();
    void acceptRequested();
    void cancelRequested();

private:
    int mCurrentTab = 0;

    // The "before" side, written only by seed().
    int mInitialLanguageIndex = 0;
    int mInitialEasingIndex = 2;
    int mInitialRangeIndex = 2;
    int mInitialTimeFormatIndex = 0;
    QString mInitialThemeKey = QStringLiteral("dark");
    int mInitialAccentIndex = 6;
    bool mInitialDonationAllowed = true;
    bool mInitialAutoSave = true;
    int mInitialAutoSaveDelayMinutes = 5;
    bool mInitialAutoCbCopy = false;
    bool mInitialResIDCheck = true;
    bool mInitialIgnoreWarnings = false;
    bool mInitialForceSolverLoad = false;
    int mInitialKeyDelayMs = 125;
    bool mInitialInvertMainViewScaling = false;
    bool mInitialInvertTimeLineScaling = false;
    bool mInitialMiddleMouseMoveCanvas = true;

    // The "now" side, written by the setters (and by seed()).
    int mLanguageIndex = 0;
    int mEasingIndex = 2;
    int mRangeIndex = 2;
    int mTimeFormatIndex = 0;
    QString mThemeKey = QStringLiteral("dark");
    int mAccentIndex = 6;
    bool mDonationAllowed = true;
    bool mAutoSave = true;
    int mAutoSaveDelayMinutes = 5;
    bool mAutoShowMesh = false;
    bool mAutoCbCopy = false;
    bool mResIDCheck = true;
    bool mIgnoreWarnings = false;
    bool mForceSolverLoad = false;
    int mKeyDelayMs = 125;
    bool mInvertMainViewScaling = false;
    bool mInvertTimeLineScaling = false;
    bool mMiddleMouseMoveCanvas = true;

    QVariantList mAccentOptions;
    QVariantList mThemeOptions;
};

} // namespace gui

#endif // GUI_QML_GENERALSETTINGVIEWMODEL_H
