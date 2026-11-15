#ifndef GUI_QML_EXPORTVIEWMODEL_H
#define GUI_QML_EXPORTVIEWMODEL_H

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include "ctrl/ExportTarget.h"
#include "gui/ExportFacts.h"

class QDir;

namespace gui {

// The rows of the key-facts summary, as a model the scene can repeat over.
//
// A plain QList property would work for the first paint and then go stale: the
// summary is rebuilt on every destination change, format change and edit, and the
// scene has to see that. This is the smallest thing that supports that (a reset
// per rebuild; no partial updates, because the whole list is recomputed).
class ExportFactsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // How many of the rows are the destination's own NOTES: what the scene puts
    // behind the "Details" disclosure rather than in the summary.
    Q_PROPERTY(int noteCount READ noteCount NOTIFY countChanged)
public:
    explicit ExportFactsModel(QObject* aParent = nullptr);

    int rowCount(const QModelIndex& aParent = QModelIndex()) const override;
    QVariant data(const QModelIndex& aIndex, int aRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return mFacts.size(); }
    int noteCount() const;
    const QList<ExportFact>& facts() const { return mFacts; }
    void setFacts(const QList<ExportFact>& aFacts);

signals:
    void countChanged();

private:
    QList<ExportFact> mFacts;
};

// The frame selection: each range is a row of its own, so the trash button on row
// N removes row N. The widget form kept two parallel QVectors and a QGridLayout
// and removed the LAST pair whatever button was pressed.
class ExportRangeModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY countChanged)
public:
    enum Role {
        FirstRole = Qt::UserRole + 1,
        LastRole,
        // The words a screen reader (and a probe) addresses the row by:
        // "First frame 2" / "Last frame 2".
        FirstNameRole,
        LastNameRole,
        // Whether this row may be removed at all (false while it is the only one).
        // A ROLE, so the delegate binds a value instead of reaching back through
        // `model`, which is ambiguous inside a delegate.
        CanRemoveRole,
    };

    explicit ExportRangeModel(QObject* aParent = nullptr);

    int rowCount(const QModelIndex& aParent = QModelIndex()) const override;
    QVariant data(const QModelIndex& aIndex, int aRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return mRanges.size(); }
    // Whether removing a range is a thing the user may do at all. The trash button
    // is disabled (not hidden) while one range remains, so the row's shape does not
    // change under the pointer.
    bool canRemove() const { return mRanges.size() > 1; }
    const QList<ctrl::FrameRange>& ranges() const { return mRanges; }

    // The project's extent, used as the default for a new range and to keep a
    // remembered range inside the animation it belongs to.
    void setProjectExtent(int aFirst, int aLast);

    void addRange(int aLastFrame);
    void removeRange(int aIndex);
    Q_INVOKABLE void setRangeFirst(int aIndex, int aValue);
    Q_INVOKABLE void setRangeLast(int aIndex, int aValue);
    // Replace every range with `count` copies of [first..last] - used to restore
    // the per-project remembered inputs.
    void resetRanges(const QList<ctrl::FrameRange>& aRanges);

    // [min(first) .. max(last)] across every row: the single span the plan and the
    // manual-mode facts are computed from.
    int spanFirst() const;
    int spanLast() const;

signals:
    void countChanged();
    void rangesChanged();

private:
    QList<ctrl::FrameRange> mRanges;
    int mProjectFirst = 0;
    int mProjectLast = 0;
};

// A capability row (transparency, loop, audio, palette): a checkbox whose whole
// state the destination may own. Value/visible/enabled are separate because the
// three mean different things - a Required capability is visible and ticked but NOT
// editable, and an inapplicable one is not shown at all.
class ExportCapabilityRow : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
public:
    explicit ExportCapabilityRow(QObject* aParent = nullptr) : QObject(aParent) {}

    bool value() const { return mValue; }
    bool visible() const { return mVisible; }
    bool enabled() const { return mEnabled; }
    QString label() const { return mLabel; }

    void setValue(bool aValue);
    void setVisible(bool aVisible);
    void setEnabled(bool aEnabled);
    void setLabel(const QString& aLabel);

signals:
    void valueChanged();
    void visibleChanged();
    void enabledChanged();
    void labelChanged();

private:
    bool mValue = false;
    bool mVisible = true;
    bool mEnabled = true;
    QString mLabel;
};

// Everything the export dialog decides, with no widgets, no GL and no scene.
//
// This is the port of src/gui/exportdiag.h (the widget form) plus the
// refreshFacts lambda that used to live inside MainWindow::openExportDialog().
// The rules it holds are the ones that made the old form honest, and they are
// unchanged:
//
//   * applyDestinationDefaults() then applyPlan() - two phases, because the plan
//     is computed FROM the controls;
//   * applyArtifactConstraints() - what the selected artifact cannot use is
//     hidden, not greyed;
//   * size ownership (mUserSize / mLastSizeDictated / bounds-only), the fps cap,
//     the codec's quality knob and its sentinel, and the manual-state stash.
//
// The public surface is properties the scene binds to and the test drives, and a
// buildExportParam()/read-back block so MainWindow never touches a widget.
class ExportViewModel : public QObject {
    Q_OBJECT

    // ---- where it is going ------------------------------------------------
    Q_PROPERTY(QString destinationId READ destinationId WRITE setDestinationId NOTIFY destinationChanged)
    Q_PROPERTY(QString destinationLabel READ destinationLabel NOTIFY destinationChanged)
    Q_PROPERTY(QString destinationIconName READ destinationIconName NOTIFY destinationChanged)
    Q_PROPERTY(bool manualMode READ manualMode NOTIFY destinationChanged)

    // ---- the stated facts --------------------------------------------------
    // The CONCRETE model type, not QObject*: a Repeater needs to see a list model.
    // Declared as QObject* the scene receives a single opaque object, which renders
    // as one nameless delegate (and, with required roles, as none at all).
    Q_PROPERTY(gui::ExportFactsModel* facts READ facts CONSTANT)
    Q_PROPERTY(QString factsText READ factsText NOTIFY factsChanged)

    // ---- the quality intent -----------------------------------------------
    Q_PROPERTY(int qualityIndex READ qualityIndex WRITE setQualityIndex NOTIFY qualityIndexChanged)
    Q_PROPERTY(QStringList qualityLabels READ qualityLabels NOTIFY languageChanged)

    // ---- the artifact (manual mode only) ----------------------------------
    Q_PROPERTY(QVariantList formatOptions READ formatOptions NOTIFY formatOptionsChanged)
    Q_PROPERTY(int formatIndex READ formatIndex WRITE setFormatIndex NOTIFY formatChanged)
    Q_PROPERTY(bool formatVisible READ formatVisible NOTIFY constraintsChanged)
    Q_PROPERTY(QVariantList codecOptions READ codecOptions NOTIFY codecOptionsChanged)
    Q_PROPERTY(int codecIndex READ codecIndex WRITE setCodecIndex NOTIFY codecChanged)
    Q_PROPERTY(bool codecVisible READ codecVisible NOTIFY constraintsChanged)
    Q_PROPERTY(bool codecEnabled READ codecEnabled NOTIFY constraintsChanged)

    // ---- frames ------------------------------------------------------------
    Q_PROPERTY(gui::ExportRangeModel* ranges READ ranges CONSTANT)
    Q_PROPERTY(bool rangeVisible READ rangeVisible NOTIFY constraintsChanged)
    Q_PROPERTY(bool canRemoveRange READ canRemoveRange NOTIFY constraintsChanged)

    // ---- size --------------------------------------------------------------
    Q_PROPERTY(int width READ width WRITE setWidth NOTIFY sizeChanged)
    Q_PROPERTY(int height READ height WRITE setHeight NOTIFY sizeChanged)
    Q_PROPERTY(int widthMaximum READ widthMaximum NOTIFY sizeLimitsChanged)
    Q_PROPERTY(int heightMaximum READ heightMaximum NOTIFY sizeLimitsChanged)
    Q_PROPERTY(bool sizeEditable READ sizeEditable NOTIFY constraintsChanged)
    Q_PROPERTY(bool keepAspectRatio READ keepAspectRatio WRITE setKeepAspectRatio NOTIFY keepAspectRatioChanged)
    Q_PROPERTY(int nativeWidth READ nativeWidth NOTIFY nativeChanged)
    Q_PROPERTY(int nativeHeight READ nativeHeight NOTIFY nativeChanged)

    // ---- rate --------------------------------------------------------------
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(int fpsMaximum READ fpsMaximum NOTIFY fpsLimitsChanged)
    Q_PROPERTY(bool fpsVisible READ fpsVisible NOTIFY constraintsChanged)
    Q_PROPERTY(int nativeFps READ nativeFps NOTIFY nativeChanged)

    // ---- capabilities ------------------------------------------------------
    Q_PROPERTY(gui::ExportCapabilityRow* transparency READ transparency CONSTANT)
    Q_PROPERTY(gui::ExportCapabilityRow* loop READ loop CONSTANT)
    Q_PROPERTY(gui::ExportCapabilityRow* audio READ audio CONSTANT)
    Q_PROPERTY(gui::ExportCapabilityRow* palette READ palette CONSTANT)

    // ---- the encoder's own numbers ----------------------------------------
    Q_PROPERTY(int qualityOverrideValue READ qualityOverrideValue WRITE setQualityOverrideValue NOTIFY qualityOverrideChanged)
    Q_PROPERTY(int qualityOverrideMinimum READ qualityOverrideMinimum NOTIFY qualityOverrideChanged)
    Q_PROPERTY(int qualityOverrideMaximum READ qualityOverrideMaximum NOTIFY qualityOverrideChanged)
    // The value that means "let the Quality setting decide". It is the BOTTOM of the
    // control's own range (one below the codec's span), which is why it moves when
    // the codec does: a leftover from another codec's range is an ordinary number
    // inside this one and would silently stop being Auto.
    Q_PROPERTY(int qualityOverrideAutoValue READ qualityOverrideAutoValue NOTIFY qualityOverrideChanged)
    Q_PROPERTY(QString qualityOverrideAutoText READ qualityOverrideAutoText NOTIFY languageChanged)
    Q_PROPERTY(QString qualityOverrideLabel READ qualityOverrideLabel NOTIFY qualityOverrideChanged)
    Q_PROPERTY(bool qualityOverrideVisible READ qualityOverrideVisible NOTIFY constraintsChanged)
    // False while the user has named the codec's own value: the intent is then not in
    // charge, and a combo still reading "Balanced" beside "CRF 18" is the form
    // contradicting itself.
    Q_PROPERTY(bool qualityInCharge READ qualityInCharge NOTIFY qualityOverrideChanged)
    Q_PROPERTY(QString bitrate READ bitrate WRITE setBitrate NOTIFY bitrateChanged)
    Q_PROPERTY(bool bitrateVisible READ bitrateVisible NOTIFY constraintsChanged)
    Q_PROPERTY(bool bitrateEnabled READ bitrateEnabled NOTIFY constraintsChanged)
    Q_PROPERTY(int imageQuality READ imageQuality WRITE setImageQuality NOTIFY imageQualityChanged)
    Q_PROPERTY(bool imageQualityVisible READ imageQualityVisible NOTIFY constraintsChanged)
    // K46 (ruled 2026-10-01): the Quality row belongs to the encoder that writes the
    // file, so it is exactly the complement of the image-quality row - one predicate,
    // negated, and never both or neither. mImageQualityVisible is true whenever Qt's
    // image writers own the artifact (an image/* format, or a destination whose
    // artifact is a still or a sequence).
    Q_PROPERTY(bool qualityVisible READ qualityVisible NOTIFY constraintsChanged)
    Q_PROPERTY(bool imageQualityEnabled READ imageQualityEnabled NOTIFY constraintsChanged)

    // I43 (decided 2026-09-30): the size cap, in MB. The value 0 is the
    // sentinel = "Auto" = NO cap enforced (the destination's own cap stays a
    // stated fact). Opt-in and settable; GeneralParams::sizeCapBytes is the
    // bytes side of the same setting.
    Q_PROPERTY(double sizeCapValue READ sizeCapValue WRITE setSizeCap NOTIFY sizeCapChanged)
    Q_PROPERTY(double sizeCapMinimum READ sizeCapMinimum CONSTANT)
    Q_PROPERTY(double sizeCapMaximum READ sizeCapMaximum CONSTANT)
    Q_PROPERTY(double sizeCapAutoValue READ sizeCapAutoValue CONSTANT)
    Q_PROPERTY(bool sizeCapVisible READ sizeCapVisible NOTIFY constraintsChanged)

    // ---- the disclosure ----------------------------------------------------
    Q_PROPERTY(bool advancedVisible READ advancedVisible NOTIFY constraintsChanged)
    Q_PROPERTY(bool advancedOpen READ advancedOpen WRITE setAdvancedOpen NOTIFY advancedOpenChanged)

    // ---- the palette path (GIF), for the file dialog ------------------------
    Q_PROPERTY(QString palettePath READ palettePath NOTIFY palettePathChanged)

    // ---- the outcome -------------------------------------------------------
    Q_PROPERTY(bool operationCancelled READ operationCancelled NOTIFY operationCancelledChanged)

public:
    // What the dialog needs to know about the project and the saved preferences.
    // Passing this instead of reaching for a project keeps the model headless and
    // linkable into a test.
    struct Init {
        QSize nativeSize;
        int nativeFps = 24;
        int projectFirstFrame = 0;
        int projectLastFrame = 0;
        int playhead = 0;
        // The frames the project's last export used, if any. Empty means "the
        // project's own range".
        QList<ctrl::FrameRange> ranges;
        int exportableAudioTracks = 0;
        QString destinationId;
        int qualityIndex = 1;
        bool keepAspectRatio = true;
        bool transparency = true;
        bool loop = false;
        bool audio = false;
        bool customPalette = false;
        QString palettePath;
        int width = 0;  // 0 means "the project's own size"
        int height = 0;
        int fps = 0;
    };

    explicit ExportViewModel(const Init& aInit, QObject* aParent = nullptr);

    // ---- destination -------------------------------------------------------
    QString destinationId() const { return mDestinationId; }
    void setDestinationId(const QString& aId);
    QString destinationLabel() const { return mDestinationLabel; }
    QString destinationIconName() const { return mDestinationIconName; }
    bool manualMode() const { return mDestinationId.isEmpty(); }
    // The current target row (empty id in manual mode).
    const ctrl::ExportTarget& target() const { return mTarget; }

    // ---- facts -------------------------------------------------------------
    ExportFactsModel* facts() const { return const_cast<ExportFactsModel*>(&mFacts); }
    QObject* factsObject() { return &mFacts; }
    QString factsText() const { return exportFactsText(mFacts.facts()).join(QLatin1Char('\n')); }

    // ---- quality intent ----------------------------------------------------
    int qualityIndex() const { return mQualityIndex; }
    void setQualityIndex(int aIndex);
    QStringList qualityLabels() const;
    ctrl::QualityBucket qualityBucket() const;

    // ---- the artifact (manual mode) ---------------------------------------
    QVariantList formatOptions() const;
    int formatIndex() const { return mFormatIndex; }
    void setFormatIndex(int aIndex);
    bool formatVisible() const { return manualMode(); }
    QString formatId() const;
    QString formatLabel() const;
    // What the file dialog calls this artifact in its filter ("MP4", "PNG
    // SEQUENCE"): the container's own display name, from one table, so the filter
    // and the facts above it cannot name the same file differently.
    QString formatDisplayLabel() const;
    QVariantList codecOptions() const;
    int codecIndex() const { return mCodecIndex; }
    void setCodecIndex(int aIndex);
    // I44: the row appears exactly when there is a CHOICE - more than one codec
    // the destination accepts and this build can produce. A destination that
    // dictates one codec states it in the facts instead of offering it
    // (contract item 42), and a manual form always offers whatever the
    // container can carry.
    bool codecVisible() const { return !currentFormatIsSequence() && mCodecChoices.size() > 1; }
    bool codecEnabled() const { return codecVisible(); }
    // The encoder that would actually be used: the combo's choice, or the
    // container's probed default when the user left it alone.
    QString selectedCodecKey() const;
    QString selectedCodecLabel() const;
    // The chosen encoder's raw name ("libx264"), empty when there is no choice.
    QString codecValue() const;

    // ---- frames ------------------------------------------------------------
    ExportRangeModel* ranges() const { return const_cast<ExportRangeModel*>(&mRanges); }
    QObject* rangesObject() { return &mRanges; }
    bool rangeVisible() const { return !mArtifactIsStill; }
    bool canRemoveRange() const { return mRanges.canRemove(); }
    Q_INVOKABLE void addRange();
    Q_INVOKABLE void removeRange(int aIndex);
    int spanFirst() const { return mRanges.spanFirst(); }
    int spanLast() const { return mRanges.spanLast(); }
    int frameCount() const;

    // ---- size --------------------------------------------------------------
    int width() const { return mWidth; }
    int height() const { return mHeight; }
    void setWidth(int aValue);
    void setHeight(int aValue);
    int widthMaximum() const { return mWidthMaximum; }
    int heightMaximum() const { return mHeightMaximum; }
    bool sizeEditable() const { return mSizeEditable; }
    bool keepAspectRatio() const { return mKeepAspectRatio; }
    void setKeepAspectRatio(bool aOn);
    int nativeWidth() const { return mNativeSize.width(); }
    int nativeHeight() const { return mNativeSize.height(); }
    Q_INVOKABLE void setSizeToNative();

    // ---- rate --------------------------------------------------------------
    int fps() const { return mFps; }
    void setFps(int aValue);
    int fpsMaximum() const { return mFpsMaximum; }
    bool fpsVisible() const { return mFpsVisible; }
    int nativeFps() const { return mNativeFps; }
    Q_INVOKABLE void setFpsToNative();

    // ---- capabilities ------------------------------------------------------
    ExportCapabilityRow* transparency() { return &mTransparency; }
    ExportCapabilityRow* loop() { return &mLoop; }
    ExportCapabilityRow* audio() { return &mAudio; }
    ExportCapabilityRow* palette() { return &mPalette; }

    // ---- the encoder's own numbers ----------------------------------------
    int qualityOverrideValue() const { return mQualityOverrideValue; }
    void setQualityOverrideValue(int aValue);
    int qualityOverrideMinimum() const { return qualityOverrideAutoValue(); }
    int qualityOverrideMaximum() const { return mQualityOverrideMaximum; }
    int qualityOverrideAutoValue() const;
    QString qualityOverrideAutoText() const;
    QString qualityOverrideLabel() const;
    bool qualityOverrideVisible() const { return mQualityOverrideVisible; }
    Q_INVOKABLE void setQualityOverrideToAuto();
    bool qualityInCharge() const;
    QString bitrate() const { return mBitrate; }
    void setBitrate(const QString& aValue);
    bool bitrateVisible() const { return mBitrateVisible; }
    bool bitrateEnabled() const { return mBitrateEnabled; }
    int bitrateValue() const;
    int imageQuality() const { return mImageQuality; }
    void setImageQuality(int aValue);
    bool imageQualityVisible() const { return mImageQualityVisible; }
    // The writer that owns the file gets its own number, and only it: see the
    // Q_PROPERTY above.
    bool qualityVisible() const { return !mImageQualityVisible; }
    bool imageQualityEnabled() const { return mImageQualityEnabled; }
    Q_INVOKABLE void setImageQualityToAuto();
    double sizeCapValue() const { return mSizeCapValue; }
    void setSizeCap(double aValue);
    double sizeCapMinimum() const { return 0.0; }
    // Above every guidance tier the facts state (500 MB Nitro is the largest).
    double sizeCapMaximum() const { return 1024.0; }
    double sizeCapAutoValue() const { return 0.0; }
    // A byte cap needs ONE file to fit: hidden only for a directory of numbered
    // stills. Stills keep the row (a single image has one size — Weibo's 20 MB
    // fact is exactly such a cap); video and GIF keep it too. (The image-quality
    // row's own visibility is still-or-sequence, which is NOT this test.)
    bool sizeCapVisible() const { return !(mImageQualityVisible && !mArtifactIsStill); }
    Q_INVOKABLE void setSizeCapToAuto();

    // ---- the disclosure ----------------------------------------------------
    bool advancedVisible() const { return mAdvancedVisible; }
    bool advancedOpen() const { return mAdvancedOpen; }
    void setAdvancedOpen(bool aOpen);

    // ---- palette -----------------------------------------------------------
    QString palettePath() const { return mPalettePath; }
    // Called by the shell after the (widget) file dialog returns a path.
    Q_INVOKABLE void setPalettePath(const QString& aPath);

    // ---- outcome -----------------------------------------------------------
    bool operationCancelled() const { return mOperationCancelled; }
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();

    // ---- the two phases of applying a destination --------------------------
    // Phase one: the destination's own declarations, written into the controls
    // BEFORE the plan is asked, because the plan is computed FROM them.
    void applyDestinationDefaults(const ctrl::ExportTarget& aTarget);
    // Phase two: the plan has the last word.
    void applyPlan(const ctrl::ExportPlan& aPlan, const ctrl::ExportTarget& aTarget);
    // No destination: every axis is the user's again, at the values they had.
    void clearPlan();

    // Recompute the facts and re-apply the constraints. The scene calls this after
    // an edit; a destination change calls it itself.
    Q_INVOKABLE void refresh();

    // The request the plan is computed from, for the callers that need the same
    // inputs (the shell's own refresh and the test). NOTE: the requested size is
    // whatever the dialog currently holds, which is the whole point - the plan is
    // computed FROM the controls, not from a caller's idea of them.
    ctrl::ExportRequest buildRequest() const;

    // What the form last resolved and last stated. Exposed so a test can assert the
    // facts against the SAME plan the facts were built from: re-deriving a plan from
    // the model's post-apply state is a different question (the dialog's size has
    // moved on), and answering it would be asserting a number no user ever sees.
    const ctrl::ExportPlan& lastPlan() const { return mLastPlan; }
    const ctrl::ExportRequest& lastRequest() const { return mLastRequest; }

    // ---- read-back for MainWindow's post-exec assembly ---------------------
    // Everything the export parameters need, without a widget in sight.
    struct ReadBack {
        QString destinationId;
        ctrl::QualityBucket qualityBucket = ctrl::QualityBucket::Balanced;
        int width = 0;
        int height = 0;
        int fps = 0;
        int bitrate = 0;          // parsed the same way the widget form parsed it
        int qualityOverride = -1; // -1 = the quality intent decides
        int imageExportQuality = -1;
        qint64 sizeCapBytes = 0; // 0 = "Auto" = no cap enforced (I43)
        bool allowTransparency = true;
        bool loop = true;
        bool exportWithAudio = false;
        bool useCustomPalette = false;
        bool keepAspectRatio = true;
        QString palettePath;
        QList<ctrl::FrameRange> ranges;
        // The artifact the dialog settled on: "mp4", "gif", "image/png", ...
        QString artifactId;
        QString artifactSuffix;  // "mp4", "gif", "png", ...
        bool sequence = false;   // a directory of numbered stills
        QString codec;           // the video encoder, or empty for the probed default
    };
    ReadBack readBack() const;

signals:
    void destinationChanged();
    // The scene asked for the picker: the shell opens gui::DestinationPicker (the
    // widget dialog, whose API is unchanged) and calls setDestinationId() with the
    // answer. The model cannot open it itself - that would make it link widgets.
    void changeDestinationRequested();
    void factsChanged();
    void qualityIndexChanged();
    void formatOptionsChanged();
    void formatChanged();
    void codecOptionsChanged();
    void codecChanged();
    void sizeChanged();
    void sizeLimitsChanged();
    void keepAspectRatioChanged();
    void nativeChanged();
    void fpsChanged();
    void fpsLimitsChanged();
    void constraintsChanged();
    void qualityOverrideChanged();
    void bitrateChanged();
    void imageQualityChanged();
    void sizeCapChanged();
    void advancedOpenChanged();
    void palettePathChanged();
    void operationCancelledChanged();
    void languageChanged();
    // What the scene's Export/Cancel buttons and its Escape shortcut raise: the
    // shell turns these into QDialog::accept()/reject(). Distinct signals so the
    // scene never has to read a state to know WHICH button was pressed.
    void acceptRequested();
    void cancelRequested();
    // The palette is the platform's own file chooser - a WIDGET dialog - so the
    // scene asks the shell for it rather than pretending to own it.
    void paletteRequested();

public slots:
    // Retranslate the C++-owned labels (capability names, quality labels, the
    // Auto text). A LanguageChange event on the dialog calls this.
    void retranslateUi();

private:
    // The destination row's label and icon: the target's, or the manual-mode
    // fallback. One place, so the row can never be blank.
    void updateDestinationLabel();
    void rebuildFacts();
    void refreshCodecs();
    void applyArtifactConstraints();
    void populateFormats();

    QString currentFormatId() const;
    bool currentFormatIsSequence() const;
    bool currentFormatIsGif() const;
    bool activeCodecCarriesAlpha() const;
    // The transparency row's current state, for the manual-mode facts.
    bool transparencyRowValue() const;
    ctrl::QualityControl qualityControl() const;
    QString qualityKnob() const;

    void writeSize(const QSize& aSize);
    void setSizeEditable(bool aOn);
    void stashManualState();
    void restoreManualState();
    void setCapabilityDefault(ctrl::Capability aCapability, ExportCapabilityRow& aRow);
    void freezeCapability(ctrl::Capability aCapability, ExportCapabilityRow& aRow, bool aResolved);
    // Write the artifact the plan resolved into the manual combos, WITHOUT firing
    // the change signals (this is the plan REPORTING, not the user choosing).
    void setArtifactFromPlan(const QString& aFormatId, const QString& aCodecId);
    void setQualityRangeFromCodec();
    // The facts of a manual-mode export: no destination row to read them from, so
    // the model states them itself.
    void rebuildManualFacts();

    Init mInit;
    QSize mNativeSize;
    int mNativeFps = 24;
    int mPlayhead = 0;

    ExportFactsModel mFacts;
    ExportRangeModel mRanges;

    QString mDestinationId;
    QString mDestinationLabel;
    QString mDestinationIconName;
    ctrl::ExportTarget mTarget;

    // The last plan and request, kept so the facts can be re-stated (a range edit,
    // a language change) without recomputing the plan.
    ctrl::ExportPlan mLastPlan;
    ctrl::ExportRequest mLastRequest;

    int mQualityIndex = 1;

    struct FormatOption {
        QString label;
        QString id;
    };
    QList<FormatOption> mFormatChoices;
    struct CodecOption {
        QString label;
        QString encoder;
        bool alphaCapable = false;
    };
    QList<CodecOption> mCodecChoices;
    int mFormatIndex = -1;
    int mCodecIndex = -1;
    // I44: whether the current codec index is the USER's choice (set only by
    // setCodecIndex, i.e. by the codec row itself). Unpinned, the destination's
    // own default decides and the plan's exact encoder (hardware included)
    // passes through untouched. Reset on a destination switch.
    bool mCodecPinned = false;

    // What the form is about, as opposed to what the hidden manual combos hold.
    QString mArtifactFormatId;
    QString mArtifactCodecId;
    bool mArtifactIsStill = false;
    bool mArtifactFpsIgnored = false;

    // Size ownership: the user's own resolution, and whether the boxes currently
    // report one the destination forced.
    QSize mUserSize;
    bool mUserSizeKnown = false;
    bool mLastSizeDictated = false;
    bool mKeepAspectRatio = true;
    int mWidth = 0;
    int mHeight = 0;
    int mWidthMaximum = INT32_MAX;
    int mHeightMaximum = INT32_MAX;
    bool mSizeEditable = true;
    // Guard: a write through writeSize() must not run the aspect link.
    bool mWritingSize = false;
    // Guard: the plan pipeline rebuilds the combos, which is not the user choosing
    // from them. Without this, refresh() re-enters itself from its own application -
    // the same recursion the widget form prevented with QSignalBlocker.
    bool mRefreshing = false;

    int mFps = 24;
    int mFpsMaximum = 1000;
    bool mFpsVisible = true;

    ExportCapabilityRow mTransparency;
    ExportCapabilityRow mLoop;
    ExportCapabilityRow mAudio;
    ExportCapabilityRow mPalette;

    // The values the user had before any destination was chosen. Held so manual mode
    // gives back what it took.
    struct ManualState {
        bool transparency = false;
        bool loop = false;
        bool audio = false;
        bool palette = false;
    };
    ManualState mManualState;
    bool mHasManualState = false;

    // The looping / audio / palette preferences as they were before an artifact
    // froze them (a GIF loops and cannot carry audio; a sequence does neither).
    struct ArtifactState {
        bool loop = false;
        bool audio = false;
        bool palette = false;
        bool valid = false;
    };
    ArtifactState mPreArtifact;

    int mQualityOverrideValue = -1;
    int mQualityOverrideMaximum = 0;
    // The Auto value of the codec mQualityOverrideValue belongs to. The field's own
    // MINIMUM is the current codec's Auto, so any value above it is indistinguishable
    // from a value the user named - unless the value is remembered as "this encoder's
    // Auto", which is how a CRF left behind by a manual WebM export must not become a
    // fixed number for the H.264 destination the user picked next.
    int mQualityOverrideSentinel = -1;
    bool mQualityOverrideVisible = false;
    QString mQualityOverrideLabelOverride;
    QString mBitrate = QStringLiteral("Auto");
    bool mBitrateVisible = false;
    bool mBitrateEnabled = false;
    int mImageQuality = -1;
    bool mImageQualityVisible = false;
    bool mImageQualityEnabled = true;
    double mSizeCapValue = 0.0; // MB; 0 = the "Auto" sentinel = no cap

    bool mAdvancedVisible = false;
    bool mAdvancedOpen = false;

    QString mPalettePath;
    bool mOperationCancelled = true;
};

} // namespace gui

#endif // GUI_QML_EXPORTVIEWMODEL_H
