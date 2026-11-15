#include "gui/qml/ExportViewModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QRegularExpression>
#include <QVariantMap>

#include <algorithm>

#include "media/FFmpegHelpers.h"

namespace gui {

namespace {
// The four quality INTENTS, in the order the combo shows them. Index -> bucket is
// the mapping MainWindow has always used (and the one the quick-export menu reads
// back out of settings), so it stays the same mapping.
constexpr int kSmallerIndex = 0;
constexpr int kBalancedIndex = 1;
constexpr int kBestIndex = 2;
constexpr int kLosslessIndex = 3;
} // namespace

// ===========================================================================
// ExportFactsModel
// ===========================================================================
ExportFactsModel::ExportFactsModel(QObject* aParent) : QAbstractListModel(aParent) {}

int ExportFactsModel::rowCount(const QModelIndex& aParent) const {
    return aParent.isValid() ? 0 : mFacts.size();
}

QVariant ExportFactsModel::data(const QModelIndex& aIndex, int aRole) const {
    if (!aIndex.isValid() || aIndex.row() < 0 || aIndex.row() >= mFacts.size())
        return {};
    const ExportFact& fact = mFacts.at(aIndex.row());
    switch (aRole) {
    case Qt::UserRole + 1: return fact.id;
    case Qt::UserRole + 2: return fact.label;
    case Qt::UserRole + 3: return fact.value;
    case Qt::UserRole + 4: return fact.detail;
    case Qt::UserRole + 5: return fact.severityName();
    default: break;
    }
    return {};
}

QHash<int, QByteArray> ExportFactsModel::roleNames() const {
    return exportFactRoleNames();
}

int ExportFactsModel::noteCount() const {
    int notes = 0;
    for (const ExportFact& fact : mFacts) {
        if (fact.id == QLatin1String("note"))
            ++notes;
    }
    return notes;
}

void ExportFactsModel::setFacts(const QList<ExportFact>& aFacts) {
    beginResetModel();
    mFacts = aFacts;
    endResetModel();
    emit countChanged();
}

// ===========================================================================
// ExportRangeModel
// ===========================================================================
ExportRangeModel::ExportRangeModel(QObject* aParent) : QAbstractListModel(aParent) {
    mRanges.append(ctrl::FrameRange{0, 0});
}

int ExportRangeModel::rowCount(const QModelIndex& aParent) const {
    return aParent.isValid() ? 0 : mRanges.size();
}

QVariant ExportRangeModel::data(const QModelIndex& aIndex, int aRole) const {
    if (!aIndex.isValid() || aIndex.row() < 0 || aIndex.row() >= mRanges.size())
        return {};
    const ctrl::FrameRange& range = mRanges.at(aIndex.row());
    const int ordinal = aIndex.row() + 1;
    switch (aRole) {
    case FirstRole: return range.first;
    case LastRole: return range.last;
    // A column heading is a layout device, not a name: each row carries its own.
    case FirstNameRole:
        return tr("First frame %1").arg(ordinal);
    case LastNameRole:
        return tr("Last frame %1").arg(ordinal);
    case CanRemoveRole: return mRanges.size() > 1;
    default: break;
    }
    return {};
}

QHash<int, QByteArray> ExportRangeModel::roleNames() const {
    return {
        {FirstRole, QByteArrayLiteral("first")},
        {LastRole, QByteArrayLiteral("last")},
        {FirstNameRole, QByteArrayLiteral("firstName")},
        {LastNameRole, QByteArrayLiteral("lastName")},
        {CanRemoveRole, QByteArrayLiteral("canRemove")},
    };
}

void ExportRangeModel::setProjectExtent(int aFirst, int aLast) {
    mProjectFirst = aFirst;
    mProjectLast = aLast;
}

void ExportRangeModel::addRange(int aLastFrame) {
    ctrl::FrameRange range;
    // A new range starts where the previous one ended: the second range a user adds
    // is nearly always the next stretch of the animation.
    range.first = mRanges.isEmpty() ? mProjectFirst : mRanges.last().last;
    range.last = aLastFrame > 0 ? aLastFrame : mProjectLast;
    const int row = mRanges.size();
    beginInsertRows(QModelIndex(), row, row);
    mRanges.append(range);
    endInsertRows();
    emit countChanged();
    emit rangesChanged();
    // canRemove flips to true the moment a second row exists.
    if (mRanges.size() == 2)
        emit dataChanged(index(0, 0), index(0, 0), {CanRemoveRole});
}

void ExportRangeModel::removeRange(int aIndex) {
    // No crashing the app please: a row that is not there is a no-op, and the last
    // range cannot be removed (the trash is disabled, but a stale click can still
    // arrive).
    if (aIndex < 0 || aIndex >= mRanges.size() || mRanges.size() <= 1)
        return;
    beginRemoveRows(QModelIndex(), aIndex, aIndex);
    mRanges.removeAt(aIndex);
    endRemoveRows();
    emit countChanged();
    emit rangesChanged();
    if (mRanges.size() == 1)
        emit dataChanged(index(0, 0), index(0, 0), {CanRemoveRole});
    // The per-row accessible names carry the row's ordinal ("First frame 2"), so a
    // removal renumbers every row below it.
    if (aIndex < mRanges.size()) {
        emit dataChanged(index(aIndex, 0), index(mRanges.size() - 1, 0),
                         {FirstRole, LastRole, FirstNameRole, LastNameRole});
    }
}

void ExportRangeModel::setRangeFirst(int aIndex, int aValue) {
    if (aIndex < 0 || aIndex >= mRanges.size())
        return;
    if (mRanges.at(aIndex).first == aValue)
        return;
    mRanges[aIndex].first = aValue;
    emit dataChanged(index(aIndex, 0), index(aIndex, 0), {FirstRole, LastRole});
    emit rangesChanged();
}

void ExportRangeModel::setRangeLast(int aIndex, int aValue) {
    if (aIndex < 0 || aIndex >= mRanges.size())
        return;
    if (mRanges.at(aIndex).last == aValue)
        return;
    mRanges[aIndex].last = aValue;
    emit dataChanged(index(aIndex, 0), index(aIndex, 0), {FirstRole, LastRole});
    emit rangesChanged();
}

void ExportRangeModel::resetRanges(const QList<ctrl::FrameRange>& aRanges) {
    beginResetModel();
    mRanges = aRanges;
    if (mRanges.isEmpty())
        mRanges.append(ctrl::FrameRange{mProjectFirst, mProjectLast});
    endResetModel();
    emit countChanged();
    emit rangesChanged();
}

int ExportRangeModel::spanFirst() const {
    int first = 0;
    bool any = false;
    for (const ctrl::FrameRange& range : mRanges) {
        if (!any || range.first < first) {
            first = range.first;
            any = true;
        }
    }
    return any ? first : 0;
}

int ExportRangeModel::spanLast() const {
    int last = 0;
    bool any = false;
    for (const ctrl::FrameRange& range : mRanges) {
        if (!any || range.last > last) {
            last = range.last;
            any = true;
        }
    }
    return any ? last : 0;
}

// ===========================================================================
// ExportCapabilityRow
// ===========================================================================
void ExportCapabilityRow::setValue(bool aValue) {
    if (mValue == aValue) return;
    mValue = aValue;
    emit valueChanged();
}

void ExportCapabilityRow::setVisible(bool aVisible) {
    if (mVisible == aVisible) return;
    mVisible = aVisible;
    emit visibleChanged();
}

void ExportCapabilityRow::setEnabled(bool aEnabled) {
    if (mEnabled == aEnabled) return;
    mEnabled = aEnabled;
    emit enabledChanged();
}

void ExportCapabilityRow::setLabel(const QString& aLabel) {
    if (mLabel == aLabel) return;
    mLabel = aLabel;
    emit labelChanged();
}

// ===========================================================================
// ExportViewModel
// ===========================================================================
ExportViewModel::ExportViewModel(const Init& aInit, QObject* aParent)
    : QObject(aParent), mInit(aInit), mNativeSize(aInit.nativeSize), mNativeFps(aInit.nativeFps),
      mPlayhead(aInit.playhead) {
    mRanges.setProjectExtent(aInit.projectFirstFrame, aInit.projectLastFrame);
    // The ranges are the PROJECT's own until an initial one is given: an empty Init
    // range list means "export what the project says".
    if (aInit.ranges.isEmpty()) {
        mRanges.resetRanges({ctrl::FrameRange{aInit.projectFirstFrame, aInit.projectLastFrame}});
    } else {
        mRanges.resetRanges(aInit.ranges);
    }
    connect(&mRanges, &ExportRangeModel::rangesChanged, this, &ExportViewModel::rebuildFacts);
    connect(&mRanges, &ExportRangeModel::countChanged, this, &ExportViewModel::constraintsChanged);

    mWidth = aInit.width > 0 ? aInit.width : mNativeSize.width();
    mHeight = aInit.height > 0 ? aInit.height : mNativeSize.height();
    mUserSize = QSize(mWidth, mHeight);
    mUserSizeKnown = true;
    mFps = aInit.fps > 0 ? aInit.fps : mNativeFps;
    mKeepAspectRatio = aInit.keepAspectRatio;
    mTransparency.setValue(aInit.transparency);
    mLoop.setValue(aInit.loop);
    mAudio.setValue(aInit.audio);
    mPalette.setValue(aInit.customPalette);
    mPalettePath = aInit.palettePath;
    mQualityIndex = (aInit.qualityIndex >= 0 && aInit.qualityIndex <= 3) ? aInit.qualityIndex
                                                                        : kBalancedIndex;
    // The destination row is never empty, including in manual mode: it is the one
    // line that says what this form is about.
    updateDestinationLabel();
    populateFormats();
    retranslateUi();
    // No destination yet: state the manual facts so the summary is never empty, then
    // let the destination (when there is one) place the plan over them.
    rebuildFacts();
    refresh();
    if (!aInit.destinationId.isEmpty())
        setDestinationId(aInit.destinationId);
}

// ---------------------------------------------------------------- destination
void ExportViewModel::setDestinationId(const QString& aId) {
    if (mDestinationId == aId)
        return;
    mDestinationId = aId;
    // A destination switch re-syncs the codec row (I44): the user's pin belonged
    // to the row they left and does not survive into the next one.
    mCodecPinned = false;
    if (aId.isEmpty()) {
        // Manual mode is the escape hatch: from here the combo IS the answer.
        mArtifactFormatId.clear();
        mArtifactCodecId.clear();
        mArtifactIsStill = false;
        mArtifactFpsIgnored = false;
        mTarget = ctrl::ExportTarget();
        updateDestinationLabel();
        clearPlan();
        rebuildFacts();
        emit destinationChanged();
        return;
    }
    const ctrl::ExportTarget target = ctrl::ExportTargets::find(ctrl::ExportTargets::load(), aId);
    mTarget = target;
    mDestinationIconName = target.icon;
    mDestinationLabel = target.id.isEmpty() ? aId : target.label;
    emit destinationChanged();
    refresh();
}

void ExportViewModel::updateDestinationLabel() {
    if (mDestinationId.isEmpty()) {
        mDestinationLabel = tr("My platform isn't listed (I'll choose the format)");
        mDestinationIconName.clear();
        return;
    }
    mDestinationLabel = mTarget.id.isEmpty() ? mDestinationId : mTarget.label;
    mDestinationIconName = mTarget.icon;
}

// ---------------------------------------------------------------------- facts
void ExportViewModel::rebuildFacts() {
    if (manualMode()) {
        rebuildManualFacts();
    } else {
        mFacts.setFacts(exportFacts(mLastPlan, mTarget, mLastRequest));
    }
    emit factsChanged();
}

void ExportViewModel::rebuildManualFacts() {
    // No destination row to read the answers from, so the model states them itself. The
    // rows are the ones exportFacts() builds for a destination-driven export - same
    // labels, same roles, same wording for the same fact - because a form that words the
    // artifact differently depending on how the user reached it is two forms wearing one
    // dialog. The two notes are the widget form's manual ones.
    const bool isSequence = currentFormatIsSequence();
    const bool isGif = currentFormatIsGif();

    QList<ExportFact> facts;
    ExportFact size;
    size.id = QStringLiteral("size");
    size.label = tr("Output:");
    size.value = tr("%1 x %2, %3 fps").arg(width()).arg(height()).arg(fps());
    // No detail, deliberately: the rate has a row of its own immediately below this
    // one, and "1100 x 790, 24 fps · 24 fps" states the same fact twice in a single
    // line - which is the furniture the two-column row exists to remove. exportFacts()
    // reached the same conclusion for a destination-driven export.
    facts << size;

    ExportFact rate;
    rate.id = QStringLiteral("fps");
    rate.label = tr("Frame rate:");
    rate.value = tr("%1 fps").arg(fps());
    facts << rate;

    ExportFact format;
    format.id = QStringLiteral("format");
    format.label = tr("Format:");
    if (isSequence) {
        const int frames = frameCount();
        const QString suffix = formatId().mid(6).toUpper();
        // "61 numbered PNG files" says which format it is already, so there is no
        // detail to add.
        format.value = tr("%1 numbered %2 files")
                           .arg(frames)
                           .arg(suffix.isEmpty() ? formatLabel() : suffix);
    } else {
        // The shape exportFacts() gives a destination-driven export: the value is the
        // codec, the detail is the container it lands in, and the row reads
        // "VP9 · WebM". The widget form's manual branch instead printed "VP9 in WebM"
        // AND the container after it, so the same fact was worded differently depending
        // on how the user reached this form.
        //
        // A container with exactly one codec (GIF, APNG, WebP) has no codec label to
        // print and nothing to add to it: it states itself.
        const QString codecLabel = selectedCodecLabel();
        format.value = codecLabel.isEmpty() ? formatLabel() : codecLabel;
        format.detail = codecLabel.isEmpty() ? QString() : formatLabel();
    }
    facts << format;

    ExportFact audio;
    audio.id = QStringLiteral("audio");
    audio.label = tr("Audio:");
    audio.value = mAudio.value() ? tr("yes") : tr("no");
    // The detail is the one thing the row's own answer cannot say: whether the project
    // had sound to write (a ticked box over a silent project writes no audio track) or
    // sound to leave out. The widget form's pair printed "Audio: no · yes" - the
    // checkbox and the project contradicting each other in one two-column line.
    const bool hasAudio = mInit.exportableAudioTracks > 0;
    if (!hasAudio)
        audio.detail = tr("none in this project");
    else if (!mAudio.value())
        audio.detail = tr("off in this dialog");
    facts << audio;

    ExportFact transparency;
    transparency.id = QStringLiteral("transparency");
    transparency.label = tr("Transparency:");
    transparency.value = transparencyRowValue() ? tr("yes") : tr("no");
    facts << transparency;

    // One row per note. A ticked "Export with transparency" beside a codec that has
    // no alpha pixel format is the clearest case: the export will be opaque however
    // the box is set, and only words can say so.
    if (mTransparency.value() && !activeCodecCarriesAlpha()) {
        ExportFact row;
        row.id = QStringLiteral("note");
        row.value = tr("%1 cannot carry transparency, so this export will be opaque.")
                        .arg(selectedCodecLabel());
        row.severity = ExportFact::Severity::Warning;
        facts << row;
    }
    if (isGif) {
        ExportFact row;
        row.id = QStringLiteral("note");
        row.value = tr("A GIF loops forever, so the Loop animation box is fixed on.");
        facts << row;
    }
    mFacts.setFacts(facts);
}

// ------------------------------------------------------------------- artifact
void ExportViewModel::populateFormats() {
    // Filling a combo is not the user choosing from it, so no signal here: the
    // widget version recursed to a stack overflow by rebuilding the list from inside
    // its own change handler.
    mFormatChoices.clear();
    for (const media::ffmpeg::ContainerOption& c : media::ffmpeg::writableContainers()) {
        FormatOption option;
        option.label = c.label;
        option.id = c.name;
        mFormatChoices.append(option);
    }
    // A directory of numbered stills, written by Qt's own image writers - a
    // different mechanism from the libav* video path, hence the "image/" prefix that
    // marks it. These two are the ones anyone asks for.
    mFormatChoices.append({tr("PNG sequence"), QStringLiteral("image/png")});
    mFormatChoices.append({tr("JPEG sequence"), QStringLiteral("image/jpg")});
    if (mFormatIndex < 0 && !mFormatChoices.isEmpty())
        mFormatIndex = 0;
    emit formatOptionsChanged();
    refreshCodecs();
}

void ExportViewModel::refreshCodecs() {
    mCodecChoices.clear();
    const QString id = formatId();
    if (id.startsWith(QLatin1String("image/"))) {
        mCodecIndex = -1;
        emit codecOptionsChanged();
        applyArtifactConstraints();
        return;
    }
    for (const media::ffmpeg::CodecOption& c : media::ffmpeg::usableCodecsForContainer(id)) {
        mCodecChoices.append({c.label, c.encoder, c.alphaCapable});
    }
    // A previous choice survives a format change when the new container can still
    // carry it; otherwise the first (best) candidate is the answer.
    const QString previous = (mCodecIndex >= 0 && mCodecIndex < mCodecChoices.size())
                                 ? mCodecChoices.at(mCodecIndex).encoder
                                 : QString();
    mCodecIndex = 0;
    for (int i = 0; i < mCodecChoices.size(); ++i) {
        if (mCodecChoices.at(i).encoder == previous) {
            mCodecIndex = i;
            break;
        }
    }
    if (mCodecChoices.isEmpty())
        mCodecIndex = -1;
    emit codecOptionsChanged();
    emit codecChanged();
    applyArtifactConstraints();
}

QString ExportViewModel::currentFormatId() const {
    return mArtifactFormatId.isEmpty() ? formatId() : mArtifactFormatId;
}

bool ExportViewModel::currentFormatIsSequence() const {
    return currentFormatId().startsWith(QLatin1String("image/"));
}

bool ExportViewModel::currentFormatIsGif() const {
    return currentFormatId() == QLatin1String("gif");
}

bool ExportViewModel::activeCodecCarriesAlpha() const {
    const QString id = currentFormatId();
    if (id == QLatin1String("image/png"))
        return true;
    if (id == QLatin1String("image/jpg"))
        return false; // JPEG has no alpha channel at all
    if (id == QLatin1String("gif"))
        return true;
    // Asked of the probe, not of a table: libx265 lists yuva420p and refuses to open
    // with it, so a row that merely declares alpha is not an answer.
    const QString codec = selectedCodecKey();
    for (const media::ffmpeg::CodecOption& c : media::ffmpeg::usableCodecsForContainer(id)) {
        if (c.encoder == codec)
            return c.alphaCapable;
    }
    return false;
}

bool ExportViewModel::transparencyRowValue() const {
    return mTransparency.value();
}

ctrl::QualityControl ExportViewModel::qualityControl() const {
    return ctrl::ExportTargets::loadQualityControl().value(selectedCodecKey());
}

QString ExportViewModel::qualityKnob() const {
    return qualityControl().option;
}

int ExportViewModel::qualityOverrideAutoValue() const {
    const ctrl::QualityControl control = qualityControl();
    return control.option.isEmpty() ? -1 : control.autoValue();
}

void ExportViewModel::setQualityRangeFromCodec() {
    // The sentinel has to be INSIDE the range. QSpinBox::setRange clamped it away
    // against a span that started higher; a QML field does not clamp by itself, so
    // the model does the equivalent bookkeeping: a value left over from another
    // codec's span (WebP's 29 inside libx264's 18..34) is an ordinary number here
    // and would silently stop being Auto.
    //
    // The comparison is against the Auto value the value was LAST SET FOR, not against
    // this codec's own: this function runs after the codec changed, so asking the new
    // control what Auto is compares the old number with the wrong answer and keeps it.
    // Two encoders whose Auto values differ (VP9's CRF and libx264's) are enough - a
    // manual WebM export left its CRF behind as a fixed number on the H.264
    // destination chosen next.
    const ctrl::QualityControl control = qualityControl();
    const bool wasAuto = mQualityOverrideValue < 0
                         || mQualityOverrideValue == mQualityOverrideSentinel
                         || mQualityOverrideValue < control.autoValue()
                         || mQualityOverrideValue > std::max(control.best, control.worst);
    if (control.option.isEmpty()) {
        mQualityOverrideValue = -1;
        mQualityOverrideMaximum = 0;
        mQualityOverrideSentinel = -1;
    } else {
        mQualityOverrideMaximum = std::max(control.best, control.worst);
        mQualityOverrideValue = wasAuto ? control.autoValue() : mQualityOverrideValue;
        mQualityOverrideSentinel = control.autoValue();
    }
    emit qualityOverrideChanged();
}

// ------------------------------------------------------------- constraints
void ExportViewModel::applyArtifactConstraints() {
    const QString id = currentFormatId();
    const bool isGif = id == QLatin1String("gif");
    const bool isSequence = id.startsWith(QLatin1String("image/"));

    // The user's own values, kept the first time an artifact constrains them and
    // handed back as soon as it stops. A format must not silently rewrite a
    // preference it merely had no use for.
    if (!mPreArtifact.valid) {
        mPreArtifact.loop = mLoop.value();
        mPreArtifact.audio = mAudio.value();
        mPreArtifact.palette = mPalette.value();
        mPreArtifact.valid = true;
    }

    const bool isVideo = !isGif && !isSequence;

    // --- looping ----------------------------------------------------------
    // A GIF has an infinite-loop extension and no way to say otherwise, so the box is
    // frozen ON. A directory of stills does not loop at all, so it is frozen off.
    const bool loopForced = isGif || isSequence;
    mLoop.setValue(isGif ? true : (loopForced ? false : mPreArtifact.loop));
    mLoop.setVisible(isGif);
    mLoop.setEnabled(!loopForced);

    // --- audio ------------------------------------------------------------
    // GIF cannot carry a stream of any kind, and a sequence is a directory. For a
    // container it is a real choice exactly when we can mux audio into it - one list,
    // shared with the exporter, because this used to be two lists that disagreed
    // about AVI.
    const bool audioPossible = !isGif && !isSequence
                               && media::ffmpeg::containerCarriesAudio(id);
    mAudio.setValue(audioPossible ? mPreArtifact.audio : false);
    mAudio.setVisible(audioPossible);
    mAudio.setEnabled(audioPossible);

    // --- the palette ------------------------------------------------------
    // media::GifEncoder owns the palette; every other codec ignores it entirely. A
    // box left ticked while greyed still reads as "a custom palette is in use", which
    // for a WebM export is plainly untrue.
    mPalette.setVisible(isGif);
    mPalette.setEnabled(isGif);
    if (!isGif)
        mPalette.setValue(false);

    // --- transparency -----------------------------------------------------
    // A codec that cannot carry alpha must not offer to. The facts already say so in
    // words; a ticked box beside them reads as a promise the encoder has already been
    // proven unable to keep.
    mTransparency.setVisible(activeCodecCarriesAlpha());

    // --- what the artifact cannot use is HIDDEN, not greyed ----------------
    // Which frames and what size belong to the project, so these never go away -
    // except for a destination that writes ONE image, where the frame range and the
    // frame rate cannot affect the file at all.
    mFpsVisible = !mArtifactIsStill && !mArtifactFpsIgnored && !isSequence;
    mImageQualityVisible = isSequence;
    mImageQualityEnabled = true;

    // The codec's own value. Both halves have to hold: the codec must HAVE a knob,
    // and the export must go through the video encoder that honours it. GIF has one in
    // the table (its palette size) and does not honour it here: the palette is the FIT
    // loop's lever, not a thing a plain export sets.
    const QString knob = qualityKnob();
    const bool knobUsable = isVideo && !knob.isEmpty();
    mQualityOverrideVisible = knobUsable;
    setQualityRangeFromCodec();
    if (knobUsable) {
        // Named by the knob it sets, because the number means nothing without it: CRF
        // runs backwards from 0, WebP's quality runs forwards from 100.
        mQualityOverrideLabelOverride = knob == QLatin1String("crf") ? tr("CRF") : tr("Quality");
    }

    // --- bitrate ----------------------------------------------------------
    // What is left when there is no quality knob to turn (ProRes, FFV1, MJPEG).
    // Offering both is offering two answers to one question.
    mBitrateVisible = isVideo && knob.isEmpty();
    mBitrateEnabled = false; // a destination fixes its encoder's numbers; manual mode re-enables

    // --- the disclosure itself --------------------------------------------
    // An empty Advanced section is a dead end, so it is only offered when something
    // inside it applies to this artifact. Asked of the FLAGS above, never of a
    // child's visibility, or the section would hide itself in the two cases where
    // the palette, the loop flag and transparency are the only things in there.
    mAdvancedVisible = audioPossible || isGif || knobUsable
                       || (isVideo && knob.isEmpty()) || activeCodecCarriesAlpha();
    if (!mAdvancedVisible)
        mAdvancedOpen = false;

    emit constraintsChanged();
}

// ------------------------------------------------------------------ size
void ExportViewModel::writeSize(const QSize& aSize) {
    mWritingSize = true;
    setWidth(aSize.width());
    setHeight(aSize.height());
    mWritingSize = false;
    mUserSize = QSize(mWidth, mHeight);
    mUserSizeKnown = true;
}

void ExportViewModel::setWidth(int aValue) {
    if (aValue < 0)
        aValue = 0;
    if (aValue > mWidthMaximum)
        aValue = mWidthMaximum;
    if (mWidth != aValue) {
        mWidth = aValue;
        emit sizeChanged();
    }
    // The aspect link: editing Width moves Height, rounded UP to an even number so a
    // codec's chroma plane is not half a pixel. The formula is the widget form's, so
    // the numbers a user has seen for years do not change.
    //
    // The write-back is ONE WAY: the derived Height write must not derive a Width in
    // turn. The equality check above cannot stop that, because the derive block runs on
    // every call, and the rounding makes the pair unstable (680 -> 418 -> 680, forever).
    // So the guard is carried ACROSS the recursive call, which makes the derived write
    // terminal while leaving both numbers exactly as they were.
    if (!mWritingSize && mKeepAspectRatio && mNativeSize.width() > 0 && mNativeSize.height() > 0) {
        const int res = (mNativeSize.height() * mWidth) / mNativeSize.width();
        mWritingSize = true;
        setHeight(res % 2 ? res + 1 : res);
        mWritingSize = false;
    }
    if (!mWritingSize) {
        mUserSize = QSize(mWidth, mHeight);
        mUserSizeKnown = true;
    }
}

void ExportViewModel::setHeight(int aValue) {
    if (aValue < 0)
        aValue = 0;
    if (aValue > mHeightMaximum)
        aValue = mHeightMaximum;
    if (mHeight != aValue) {
        mHeight = aValue;
        emit sizeChanged();
    }
    // The mirror of setWidth, and ONE WAY for the same reason.
    if (!mWritingSize && mKeepAspectRatio && mNativeSize.height() > 0 && mNativeSize.width() > 0) {
        const int res = (mNativeSize.width() * mHeight) / mNativeSize.height();
        mWritingSize = true;
        setWidth(res % 2 ? res + 1 : res);
        mWritingSize = false;
    }
    if (!mWritingSize) {
        mUserSize = QSize(mWidth, mHeight);
        mUserSizeKnown = true;
    }
}

void ExportViewModel::setKeepAspectRatio(bool aOn) {
    if (mKeepAspectRatio == aOn)
        return;
    mKeepAspectRatio = aOn;
    emit keepAspectRatioChanged();
}

void ExportViewModel::setSizeToNative() {
    writeSize(mNativeSize);
}

void ExportViewModel::setFps(int aValue) {
    if (aValue < 1)
        aValue = 1;
    if (aValue > mFpsMaximum)
        aValue = mFpsMaximum;
    if (mFps == aValue)
        return;
    mFps = aValue;
    emit fpsChanged();
    rebuildFacts();
}

void ExportViewModel::setFpsToNative() {
    setFps(mNativeFps);
}

// ------------------------------------------------------------- capabilities
void ExportViewModel::setCapabilityDefault(ctrl::Capability aCapability, ExportCapabilityRow& aRow) {
    switch (aCapability) {
    case ctrl::Capability::Required:
        aRow.setValue(true);
        aRow.setEnabled(false);
        break;
    case ctrl::Capability::Forbidden:
        aRow.setValue(false);
        aRow.setEnabled(false);
        break;
    case ctrl::Capability::DefaultOn:
        aRow.setValue(true);
        aRow.setEnabled(true);
        break;
    case ctrl::Capability::DefaultOff:
        aRow.setValue(false);
        aRow.setEnabled(true);
        break;
    case ctrl::Capability::Choice:
        aRow.setEnabled(true);
        break;
    }
}

void ExportViewModel::freezeCapability(ctrl::Capability aCapability, ExportCapabilityRow& aRow,
                                       bool aResolved) {
    const bool decidedByDestination = aCapability == ctrl::Capability::Required
                                      || aCapability == ctrl::Capability::Forbidden;
    if (decidedByDestination) {
        aRow.setValue(aResolved);
        aRow.setEnabled(false);
        return;
    }
    // The plan has the last word: a destination may ask for alpha the chosen codec
    // cannot deliver, so whenever the plan says no the box must say no too - and stay
    // put, so the refusal cannot be re-asserted.
    if (aRow.value() && !aResolved) {
        aRow.setValue(false);
        aRow.setEnabled(false);
        return;
    }
    aRow.setEnabled(true);
}

void ExportViewModel::stashManualState() {
    if (mHasManualState)
        return;
    mManualState.transparency = mTransparency.value();
    // The artifact stash, when present, holds the user's real values: an artifact (a
    // GIF forcing the loop on) may have overwritten the boxes, and that forced state
    // is not the user's preference to hand back.
    mManualState.loop = mPreArtifact.valid ? mPreArtifact.loop : mLoop.value();
    mManualState.audio = mPreArtifact.valid ? mPreArtifact.audio : mAudio.value();
    mManualState.palette = mPreArtifact.valid ? mPreArtifact.palette : mPalette.value();
    mHasManualState = true;
}

void ExportViewModel::restoreManualState() {
    if (!mHasManualState)
        return;
    mTransparency.setValue(mManualState.transparency);
    mLoop.setValue(mManualState.loop);
    mAudio.setValue(mManualState.audio);
    mPalette.setValue(mManualState.palette);
    mHasManualState = false;
}

// ----------------------------------------------------------- quality intent
void ExportViewModel::setQualityIndex(int aIndex) {
    if (aIndex < 0 || aIndex > 3 || aIndex == mQualityIndex)
        return;
    mQualityIndex = aIndex;
    emit qualityIndexChanged();
    // Lossless is a real answer for a codec that can carry it, and the plan says so;
    // the destination is otherwise unchanged, so the facts are rebuilt rather than
    // the whole form re-planned.
    refresh();
}

QStringList ExportViewModel::qualityLabels() const {
    return {tr("Smaller file"), tr("Balanced"), tr("Best quality"), tr("Lossless")};
}

ctrl::QualityBucket ExportViewModel::qualityBucket() const {
    switch (mQualityIndex) {
    case kSmallerIndex: return ctrl::QualityBucket::Smaller;
    case kBestIndex: return ctrl::QualityBucket::Best;
    case kLosslessIndex: return ctrl::QualityBucket::Lossless;
    default: return ctrl::QualityBucket::Balanced;
    }
}

// ------------------------------------------------------- encoder's numbers
void ExportViewModel::setQualityOverrideValue(int aValue) {
    const ctrl::QualityControl control = qualityControl();
    if (control.option.isEmpty())
        return;
    const int low = control.autoValue();
    const int high = std::max(control.best, control.worst);
    if (aValue < low)
        aValue = low;
    if (aValue > high)
        aValue = high;
    if (mQualityOverrideValue == aValue)
        return;
    mQualityOverrideValue = aValue;
    emit qualityOverrideChanged();
    // Two controls for one number: while the user has named the value, the intent is
    // not in charge, and a combo still reading "Balanced" beside "CRF 18" is the form
    // contradicting itself.
    emit constraintsChanged();
}

void ExportViewModel::setQualityOverrideToAuto() {
    setQualityOverrideValue(qualityOverrideAutoValue());
}

bool ExportViewModel::qualityInCharge() const {
    if (!mQualityOverrideVisible)
        return true;
    return mQualityOverrideValue <= qualityOverrideAutoValue();
}

QString ExportViewModel::qualityOverrideAutoText() const {
    return tr("Auto");
}

QString ExportViewModel::qualityOverrideLabel() const {
    return mQualityOverrideLabelOverride.isEmpty() ? tr("Quality") : mQualityOverrideLabelOverride;
}

void ExportViewModel::setBitrate(const QString& aValue) {
    if (mBitrate == aValue)
        return;
    mBitrate = aValue;
    emit bitrateChanged();
}

int ExportViewModel::bitrateValue() const {
    const QString text = mBitrate.trimmed();
    // The parsing the widget form did, unchanged: empty means "let the encoder
    // decide", "auto"/"automatic" means the same, a positive decimal is the number,
    // and anything else is -1 (which isExportParamValid turns back into automatic
    // with a warning).
    if (text.isEmpty())
        return 0;
    if (text.contains(QRegularExpression(QStringLiteral("^(?i)(auto(matic)?)$"))))
        return 0;
    if (text.contains(QRegularExpression(QStringLiteral("^(?!0\\d+)\\d+$"))))
        return text.toInt();
    return -1;
}

void ExportViewModel::setImageQuality(int aValue) {
    if (aValue < -1)
        aValue = -1;
    if (aValue > 100)
        aValue = 100;
    if (mImageQuality == aValue)
        return;
    mImageQuality = aValue;
    emit imageQualityChanged();
}

void ExportViewModel::setImageQualityToAuto() {
    setImageQuality(-1);
}

void ExportViewModel::setSizeCap(double aValue) {
    if (aValue < sizeCapMinimum())
        aValue = sizeCapMinimum();
    if (aValue > sizeCapMaximum())
        aValue = sizeCapMaximum();
    if (qFuzzyCompare(mSizeCapValue, aValue))
        return;
    mSizeCapValue = aValue;
    emit sizeCapChanged();
}

void ExportViewModel::setSizeCapToAuto() {
    setSizeCap(0.0);
}

// ---------------------------------------------------------------- disclosure
void ExportViewModel::setAdvancedOpen(bool aOpen) {
    if (mAdvancedOpen == aOpen)
        return;
    mAdvancedOpen = aOpen;
    emit advancedOpenChanged();
}

// ------------------------------------------------------------------- palette
void ExportViewModel::setPalettePath(const QString& aPath) {
    if (mPalettePath == aPath)
        return;
    mPalettePath = aPath;
    emit palettePathChanged();
}

// ------------------------------------------------------------------- outcome
void ExportViewModel::accept() {
    mOperationCancelled = false;
    emit operationCancelledChanged();
    emit acceptRequested();
}

void ExportViewModel::cancel() {
    mOperationCancelled = true;
    emit operationCancelledChanged();
    emit cancelRequested();
}

// --------------------------------------------------------------- the phases
void ExportViewModel::applyDestinationDefaults(const ctrl::ExportTarget& aTarget) {
    stashManualState();
    // Ownership of the size. The boxes hold the USER's size exactly when the last
    // destination did not dictate one; while one did, they hold a number the
    // destination chose, and letting that number stand as the user's would silently
    // export the next destination at Twitch's 112 pixels. So: save what is the
    // user's, and hand it back before the plan reads the boxes.
    if (mLastSizeDictated) {
        writeSize(mUserSizeKnown ? mUserSize : mNativeSize);
    } else {
        mUserSize = QSize(mWidth, mHeight);
        mUserSizeKnown = true;
    }
    setCapabilityDefault(aTarget.alpha, mTransparency);
    setCapabilityDefault(aTarget.loop, mLoop);
    setCapabilityDefault(aTarget.audio, mAudio);
}

void ExportViewModel::applyPlan(const ctrl::ExportPlan& aPlan, const ctrl::ExportTarget& aTarget) {
    const bool planOk = aPlan.ok();
    // What this export will actually be, so every rule below answers about the
    // destination rather than about the hidden manual combos.
    if (aTarget.artifact == ctrl::ArtifactKind::Video) {
        mArtifactFormatId = aPlan.config.container.isEmpty() ? aTarget.container
                                                            : aPlan.config.container;
        mArtifactCodecId = aPlan.config.codec.isEmpty() ? aTarget.codec : aPlan.config.codec;
    } else {
        mArtifactFormatId = QStringLiteral("image/") + aPlan.imageSuffix;
        mArtifactCodecId.clear();
    }
    // A STILL is one image: the exporter writes the frame the timeline is showing,
    // not a range, so the range editor and the frame rate cannot affect the file and
    // are not shown. A SEQUENCE does take a range, so it keeps them.
    mArtifactIsStill = aTarget.artifact == ctrl::ArtifactKind::Still;
    mArtifactFpsIgnored = aTarget.fpsIgnored;

    // --- size: dictated, or the project's own -----------------------------
    const QSize beforeFit(mWidth, mHeight);
    if (planOk)
        writeSize(aPlan.size);
    // THE SIZE STAYS THE USER'S WHEREVER THE DESTINATION ONLY BOUNDS IT.
    //   Free  the destination imposes nothing: the project's own size
    //   Max   a CEILING ("no bigger than 1280x720"): the user may export smaller,
    //         and the boxes say so by carrying that as their maximum
    //   Exact / MaxExactSide / MaxMinSide   the size is pinned, so the boxes report
    //         it and stop accepting edits.
    const bool boundsOnly = aTarget.size.mode == ctrl::SizeMode::Max;
    const bool sizeIsUsers = aTarget.size.mode == ctrl::SizeMode::Free || boundsOnly;
    if (sizeIsUsers) {
        mUserSize = beforeFit;
        mUserSizeKnown = true;
    }
    mLastSizeDictated = !sizeIsUsers;
    mWidthMaximum = (boundsOnly && aTarget.size.w > 0) ? aTarget.size.w : INT32_MAX;
    mHeightMaximum = (boundsOnly && aTarget.size.h > 0) ? aTarget.size.h : INT32_MAX;
    mSizeEditable = sizeIsUsers;
    emit sizeLimitsChanged();

    // --- fps: bounded, not dictated ---------------------------------------
    // Specs cap the frame rate (Telegram 30, LINE 20); below the cap the user's own
    // rate stands. The bound lives on the control, which clamps the displayed value,
    // so the dialog cannot show a rate the plan has already reduced.
    mFpsMaximum = aTarget.caps.maxFps > 0 ? aTarget.caps.maxFps : 1000;
    emit fpsLimitsChanged();
    if (mFps > mFpsMaximum)
        setFps(mFpsMaximum);

    // --- capabilities -----------------------------------------------------
    freezeCapability(aTarget.alpha, mTransparency, planOk && aPlan.alphaEnabled);
    freezeCapability(aTarget.loop, mLoop, planOk && aPlan.gifPath);
    freezeCapability(aTarget.audio, mAudio, planOk && aPlan.audioEnabled);

    // --- the codec's own quality knob -------------------------------------
    // A destination resolves its codec and its quality from the bucket, so these
    // numbers are not inputs: leaving them editable would advertise a decision that
    // translateVideoExport() overwrites. "Auto" is the honest display for "the
    // destination decides".
    mBitrate = QStringLiteral("Auto");
    mBitrateEnabled = false;
    // Image quality is NOT a frozen fact when the artifact goes through
    // QImage::save(): it is the only quality control that file has, so it is left
    // enabled and starts at Auto, which is the writer's own default.
    mImageQuality = -1;
    // The artifact is the destination's, so the manual combos report it and stop
    // accepting edits.
    const bool imageArtifact = planOk && aPlan.artifact != ctrl::ArtifactKind::Video;
    const QString artifact =
        planOk ? (imageArtifact
                      ? QStringLiteral("image/") + aPlan.imageSuffix
                      : (aPlan.gifPath ? QStringLiteral("gif") : aPlan.config.container))
               : QString();
    const QString artifactCodec =
        planOk ? (imageArtifact || aPlan.gifPath ? QString() : aPlan.config.codec) : QString();
    if (!artifact.isEmpty())
        setArtifactFromPlan(artifact, artifactCodec);

    applyArtifactConstraints();
    setAdvancedOpen(false);

    // --- palette: only a GIF has one --------------------------------------
    const bool hasPalette = planOk && aPlan.gifPath;
    mPalette.setVisible(hasPalette);
    mPalette.setEnabled(hasPalette);
    if (!hasPalette)
        mPalette.setValue(false);

    emit constraintsChanged();
    rebuildFacts();
}

void ExportViewModel::setArtifactFromPlan(const QString& aFormatId, const QString& aCodecId) {
    // Blocked, conceptually: this is the plan REPORTING what it resolved, not the
    // user choosing, and letting the change handlers fire would re-enter the whole
    // plan computation from inside its own application. Note that refreshCodecs() is
    // deliberately NOT called: the artifact constraints it applies (loop, audio,
    // palette) belong to manual mode, where the artifact IS the decision.
    for (int i = 0; i < mFormatChoices.size(); ++i) {
        if (mFormatChoices.at(i).id == aFormatId) {
            mFormatIndex = i;
            break;
        }
    }
    mCodecChoices.clear();
    if (!aCodecId.isEmpty()) {
        if (!mTarget.id.isEmpty() && mTarget.artifact == ctrl::ArtifactKind::Video) {
            // I44: in destination mode the codec row offers the destination's
            // ACCEPTED codecs - its declared list ∩ what this build can produce,
            // one entry per FORMAT ("H.264", "H.265", "AV1") - not the
            // container's every encoder. A row that accepts one codec has one
            // entry, which is what keeps the row hidden for dictated ones.
            const QList<media::ffmpeg::CodecOption> containerCodecs =
                media::ffmpeg::usableCodecsForContainer(aFormatId);
            for (const ctrl::CodecChoice& c : ctrl::offeredCodecs(mTarget)) {
                bool alpha = false;
                for (const media::ffmpeg::CodecOption& o : containerCodecs) {
                    if (o.encoder == c.encoder) {
                        alpha = o.alphaCapable;
                        break;
                    }
                }
                mCodecChoices.append({c.label, c.encoder, alpha});
            }
        } else {
            for (const media::ffmpeg::CodecOption& c :
                 media::ffmpeg::usableCodecsForContainer(aFormatId)) {
                mCodecChoices.append({c.label, c.encoder, c.alphaCapable});
            }
        }
        mCodecIndex = 0;
        for (int i = 0; i < mCodecChoices.size(); ++i) {
            // The exact encoder first, then its FORMAT: the plan's own resolution
            // may name a hardware encoder where the offered entry names the
            // software one, and the row must still land on the right line.
            if (mCodecChoices.at(i).encoder == aCodecId
                || ctrl::formatKeyForEncoder(mCodecChoices.at(i).encoder)
                       == ctrl::formatKeyForEncoder(aCodecId)) {
                mCodecIndex = i;
                break;
            }
        }
    } else {
        mCodecIndex = -1;
    }
    emit formatChanged();
    emit codecOptionsChanged();
    emit codecChanged();
}

void ExportViewModel::clearPlan() {
    // Manual mode: no destination, so every axis is the user's again, at the values
    // they had before.
    writeSize(mUserSizeKnown ? mUserSize : mNativeSize);
    mLastSizeDictated = false;
    mSizeEditable = true;
    mWidthMaximum = INT32_MAX;
    mHeightMaximum = INT32_MAX;
    mFpsMaximum = 1000;
    mTransparency.setEnabled(true);
    mLoop.setEnabled(true);
    mAudio.setEnabled(true);
    mPalette.setEnabled(true);
    mImageQualityEnabled = true;
    mBitrateEnabled = true;
    restoreManualState();
    // Manual mode is exactly the mode where the artifact is the user's own decision,
    // so its constraints apply here and only here.
    refreshCodecs();
    emit sizeLimitsChanged();
    emit fpsLimitsChanged();
    emit constraintsChanged();
}

// ---------------------------------------------------------------- the plan
ctrl::ExportRequest ExportViewModel::buildRequest() const {
    ctrl::ExportRequest request;
    request.sourceSize = mNativeSize;
    request.requestedSize = QSize(mWidth, mHeight);
    request.fps = mFps;
    request.selection = ctrl::FrameSelection::fromRange(std::min(spanFirst(), spanLast()),
                                                        std::max(spanFirst(), spanLast()));
    request.exportableAudioTracks = mInit.exportableAudioTracks;
    request.mute = !mAudio.value();
    request.playhead = mPlayhead;
    // I44: the user's codec row choice travels on the request - but only once
    // they HAVE chosen. Unpinned, the request says nothing and the plan applies
    // the destination's own default (its declared one, else the class policy),
    // so the policy's own encoder pick (hardware first where the bucket is
    // time-sensitive) is never mistaken for a user override.
    request.codec = mCodecPinned ? codecValue() : QString();
    return request;
}

void ExportViewModel::refresh() {
    if (manualMode()) {
        clearPlan();
        rebuildFacts();
        return;
    }
    const ctrl::ExportTarget target = ctrl::ExportTargets::find(ctrl::ExportTargets::load(),
                                                               mDestinationId);
    if (target.id.isEmpty()) {
        rebuildFacts();
        return;
    }
    // One pass at a time. applyPlan() rebuilds the manual combos, which is the plan
    // REPORTING what it resolved; a handler wired to that would otherwise re-enter
    // this function from inside its own application (the recursion the widget form
    // blocked with QSignalBlocker).
    if (mRefreshing)
        return;
    mRefreshing = true;
    mTarget = target;
    // Phase one: the destination's own declarations go into the controls BEFORE the
    // plan is asked, because the plan is computed from them. A row that says "this
    // loops" has to have ticked the box first, or the answer would contradict the
    // declaration it came from.
    applyDestinationDefaults(target);
    const ctrl::ExportRequest request = buildRequest();
    const ctrl::ExportPlan plan = ctrl::planExport(target, request, qualityBucket());
    mLastRequest = request;
    mLastPlan = plan;
    // State the facts AND make them true in the controls the destination governs, so
    // the form cannot contradict itself.
    mFacts.setFacts(exportFacts(plan, target, request));
    applyPlan(plan, target); // rebuilds the facts once more, after the constraints
    mRefreshing = false;
}

// ------------------------------------------------------------- format/codec
QVariantList ExportViewModel::formatOptions() const {
    QVariantList out;
    for (const FormatOption& option : mFormatChoices) {
        QVariantMap map;
        map.insert(QStringLiteral("label"), option.label);
        map.insert(QStringLiteral("value"), option.id);
        out.append(map);
    }
    return out;
}

QString ExportViewModel::formatId() const {
    if (mFormatIndex < 0 || mFormatIndex >= mFormatChoices.size())
        return QString();
    return mFormatChoices.at(mFormatIndex).id;
}

QString ExportViewModel::formatLabel() const {
    if (mFormatIndex < 0 || mFormatIndex >= mFormatChoices.size())
        return QString();
    return mFormatChoices.at(mFormatIndex).label;
}

QString ExportViewModel::formatDisplayLabel() const {
    const QString id = currentFormatId();
    if (id.startsWith(QLatin1String("image/")))
        return tr("%1 sequence").arg(id.mid(6).toUpper());
    if (id == QLatin1String("gif"))
        return tr("animated GIF");
    return ctrl::containerDisplayName(id);
}

void ExportViewModel::setFormatIndex(int aIndex) {
    if (aIndex < 0 || aIndex >= mFormatChoices.size() || aIndex == mFormatIndex)
        return;
    mFormatIndex = aIndex;
    emit formatChanged();
    // The artifact changed, so the manual constraints move with it: this is the one
    // place a format is genuinely the user's choice.
    refreshCodecs();
    rebuildFacts();
}

QVariantList ExportViewModel::codecOptions() const {
    QVariantList out;
    for (const CodecOption& option : mCodecChoices) {
        QVariantMap map;
        map.insert(QStringLiteral("label"), option.label);
        map.insert(QStringLiteral("value"), option.encoder);
        out.append(map);
    }
    return out;
}

void ExportViewModel::setCodecIndex(int aIndex) {
    if (aIndex < 0 || aIndex >= mCodecChoices.size() || aIndex == mCodecIndex)
        return;
    mCodecIndex = aIndex;
    // A change through THIS path is the user choosing from the codec row (I44):
    // from here the export uses their codec, not the destination's default. The
    // plan-driven writes go through setArtifactFromPlan and leave the flag alone.
    mCodecPinned = true;
    emit codecChanged();
    // The codec's knob, its span and its alpha capability all just changed.
    applyArtifactConstraints();
    rebuildFacts();
}

QString ExportViewModel::selectedCodecKey() const {
    const QString id = currentFormatId();
    if (id.startsWith(QLatin1String("image/")) || id == QLatin1String("gif"))
        return QString();
    const QString chosen = mArtifactCodecId.isEmpty() ? codecValue() : mArtifactCodecId;
    return chosen.isEmpty() ? media::ffmpeg::defaultCodecForContainer(id) : chosen;
}

QString ExportViewModel::codecValue() const {
    if (mCodecIndex < 0 || mCodecIndex >= mCodecChoices.size())
        return QString();
    return mCodecChoices.at(mCodecIndex).encoder;
}

QString ExportViewModel::selectedCodecLabel() const {
    if (mCodecIndex < 0 || mCodecIndex >= mCodecChoices.size())
        return QString();
    return mCodecChoices.at(mCodecIndex).label;
}

// -------------------------------------------------------------------- frames
void ExportViewModel::addRange() {
    mRanges.addRange(mInit.projectLastFrame);
}

void ExportViewModel::removeRange(int aIndex) {
    mRanges.removeRange(aIndex);
}

int ExportViewModel::frameCount() const {
    return std::max(0, spanLast() - spanFirst() + 1);
}

// ------------------------------------------------------------------ language
void ExportViewModel::retranslateUi() {
    // What the model owns in words: the capability names (which double as the
    // accessible names of the checkboxes) and the two Auto labels. The scene's own
    // strings retranslate through qsTr() on a LanguageChange event.
    mTransparency.setLabel(tr("Export with transparency"));
    mLoop.setLabel(tr("Loop animation"));
    mAudio.setLabel(tr("Export with audio"));
    mPalette.setLabel(tr("Custom palette"));
    // The manual-mode destination row's words are the model's own, so a language
    // change has to restate them too.
    if (mDestinationId.isEmpty()) {
        updateDestinationLabel();
        emit destinationChanged();
    }
    emit languageChanged();
    emit qualityOverrideChanged();
    emit constraintsChanged();
}

// ------------------------------------------------------------------ read-back
ExportViewModel::ReadBack ExportViewModel::readBack() const {
    ReadBack out;
    out.destinationId = mDestinationId;
    out.qualityBucket = qualityBucket();
    out.width = mWidth;
    out.height = mHeight;
    out.fps = mFps;
    out.bitrate = bitrateValue();
    // The sentinel is the bottom of the control's own range, and -1 means "there is
    // no knob here at all".
    out.qualityOverride = (mQualityOverrideVisible && mQualityOverrideValue > qualityOverrideAutoValue())
                              ? mQualityOverrideValue
                              : -1;
    out.imageExportQuality = mImageQuality;
    // I43: the opt-in cap travels in bytes. 0 (the "Auto" sentinel) stays 0 —
    // the fit block runs only on a value the user chose.
    out.sizeCapBytes = (mSizeCapValue > 0.0) ? qint64(qRound64(mSizeCapValue * 1048576.0)) : 0;
    out.allowTransparency = mTransparency.value();
    out.loop = mLoop.value();
    out.exportWithAudio = mAudio.value();
    out.useCustomPalette = mPalette.value();
    out.keepAspectRatio = mKeepAspectRatio;
    out.palettePath = mPalettePath;
    out.ranges = mRanges.ranges();
    out.artifactId = currentFormatId();
    out.sequence = out.artifactId.startsWith(QLatin1String("image/"));
    out.artifactSuffix = out.sequence ? out.artifactId.mid(6)
                                      : (out.artifactId.startsWith(QLatin1String("image/"))
                                             ? out.artifactId.mid(6)
                                             : out.artifactId);
    // I44: the codec that goes into the export. The user's pin, when they set
    // one; otherwise the PLAN's exact resolution (possibly a hardware encoder of
    // the offered format) - the row shows its FORMAT either way, and the
    // passthrough must not silently downgrade what the plan picked.
    out.codec = out.sequence
                    ? QString()
                    : (manualMode() || mCodecPinned
                           ? codecValue()
                           : (mArtifactCodecId.isEmpty() ? codecValue() : mArtifactCodecId));
    return out;
}

} // namespace gui
