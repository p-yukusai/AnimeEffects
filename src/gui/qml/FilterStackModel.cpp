#include "gui/qml/FilterStackModel.h"

#include "gui/qml/FilterNames.h"
#include "gui/qml/FuzzyMatch.h"

#include <QColor>
#include <QCoreApplication>
#include <QVariant>

#include "core/BlurKey.h"
#include "core/ChannelKey.h"
#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/Stack.h"
#include "core/Constant.h"
#include "core/FilterFrame.h"
#include "core/HsvKey.h"
#include "core/WaveKey.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeKeyPos.h"
#include "ctrl/TimeLineUtil.h"
#include "gui/qml/InspectorViewModel.h"
#include "gui/theme/Colors.h"

namespace gui {

namespace {
QString trFilter(const char* aText) {
    return QCoreApplication::translate("gui::prop::FilterStackPanel", aText);
}

bool isStackFilter(core::TimeKeyType aType) { return core::isFilterType(aType); }

// Same display rule as ScrubSpinBox's trimTrailingZeros: the summary must not
// show 0.0 when the field shows 0.
QString summaryNumber(double aValue, int aDecimals) {
    QString text = QString::number(aValue, 'f', aDecimals);
    if (text.contains('.')) {
        while (text.endsWith('0'))
            text.chop(1);
        if (text.endsWith('.'))
            text.chop(1);
    }
    return text == QStringLiteral("-0") ? QStringLiteral("0") : text;
}
} // namespace

FilterRowModel::FilterRowModel(core::TimeKeyType aType, InspectorViewModel& aOwner, bool aExpanded, QObject* aParent):
    QObject(aParent), mOwner(aOwner), mType(aType), mExpanded(aExpanded) {}

QString FilterRowModel::kind() const {
    if (isHSV()) return QStringLiteral("hsv");
    if (isBlur()) return QStringLiteral("blur");
    if (isWave()) return QStringLiteral("wave");
    return QString();
}

QString FilterRowModel::name() const { return filterDisplayName(mType); }

void FilterRowModel::refresh() {
    core::TimeLine* line = mOwner.timeLine();
    if (!line)
        return;

    const int frame = mOwner.frame();
    const bool enabled = line->filterEnabled(mType);
    QString keyState;
    bool hasKeys = false;
    bool keyedHere = false;
    if (core::TimeLine::usesChannelTracks(mType)) {
        const core::PropertyTrack* prop = line->property(mType);
        if (prop) {
            for (int c = 0; c < prop->channelCount(); ++c) {
                const core::ChannelTrack* channel = prop->channel(c);
                hasKeys = hasKeys || (channel && !channel->keys().isEmpty());
                keyedHere = keyedHere || line->hasChannelKey(mType, c, frame);
            }
        }
    } else {
        hasKeys = !line->map(mType).isEmpty();
        keyedHere = line->hasTimeKey(mType, frame);
    }
    keyState = !hasKeys ? QStringLiteral("static")
        : keyedHere       ? QStringLiteral("keyed")
                          : QStringLiteral("animated");

    const QColor keyColor = theme::keyTypeColor(theme::Colors::current(), mType);
    const QVariantList before{mEnabled, mKeyState, mKeyColor, mSummary, mSummarySegments, mHasAmountMask,
                              mHasTimeMask, mAmountPaintActive, mTimePaintActive, mHue,
                              mSaturation, mValue, mAbsolute, mAmount, mDirectional, mBlurX,
                              mBlurY, mBlurAngle,
                              mWave.strength(), mWave.wavelength(), mWave.angleDeg(), mWave.phase(),
                              mWave.mix(), mWave.speed(), mWave.crest(), mWave.dualAngleDeg(),
                              mWave.dualScale(), int(mWave.deform())};
    QVariantList summarySegments;
    const auto addSummary = [&summarySegments](const QString& aLabel, const QString& aValue) {
        QVariantMap segment;
        segment.insert(QStringLiteral("label"), aLabel);
        segment.insert(QStringLiteral("value"), aValue);
        summarySegments.append(segment);
    };

    mEnabled = enabled;
    mKeyState = keyState;
    mKeyColor = keyColor;

    const core::TimeKeyExpans& current = line->current();
    if (isHSV()) {
        const QList<int> values = current.hsv().hsv();
        mHue = values.size() > 0 ? values.at(0) : 0;
        mSaturation = values.size() > 1 ? values.at(1) : 100;
        mValue = values.size() > 2 ? values.at(2) : 100;
        mAbsolute = values.size() > 3 && values.at(3) != 0;
        mSummary = trFilter("H %1  S %2  V %3").arg(mHue).arg(mSaturation).arg(mValue);
        addSummary(QStringLiteral("H"), QString::number(mHue));
        addSummary(QStringLiteral("S"), QString::number(mSaturation));
        addSummary(QStringLiteral("V"), QString::number(mValue));
    } else if (isWave()) {
        // The played data at this frame, blended or from the explicit key; the
        // row edits a copy and writes the whole Data back.
        mWave = mOwner.accessor().waveData();
        const QString strength = summaryNumber(double(mWave.strength()), 1);
        const QString wavelength = summaryNumber(double(mWave.wavelength()), 0);
        const QString direction = summaryNumber(double(mWave.angleDeg()), 0);
        mSummary = trFilter("Strength %1 px  Wavelength %2 px  Direction %3°")
                       .arg(strength, wavelength, direction);
        addSummary(trFilter("Strength"), strength + QStringLiteral(" px"));
        addSummary(trFilter("Wavelength"), wavelength + QStringLiteral(" px"));
        addSummary(trFilter("Direction"), direction + QStringLiteral("°"));
    } else if (isBlur()) {
        const auto data = mOwner.accessor().blurData();
        // The mode flag is the property's state track; the accessor already
        // resolves the state at this frame (explicit, previous or base).
        mDirectional = data.isDirectional();
        mAmount = current.blurX();
        mBlurX = current.blurX();
        mBlurY = current.blurY();
        mBlurAngle = current.angleDeg();
        if (data.amount() != mAmount) // keep the accessor's exact read for a static default
            mAmount = data.amount();
        const QString blurX = summaryNumber(double(mBlurX), 1);
        const QString blurY = summaryNumber(double(mBlurY), 1);
        const QString blurAngle = summaryNumber(double(mBlurAngle), 1);
        const QString amount = summaryNumber(double(mAmount), 1);
        mSummary = mDirectional
            ? trFilter("X %1 px  Y %2 px  Angle %3°").arg(blurX, blurY, blurAngle)
            : trFilter("Amount %1 px").arg(amount);
        if (mDirectional) {
            addSummary(QStringLiteral("X"), blurX + QStringLiteral(" px"));
            addSummary(QStringLiteral("Y"), blurY + QStringLiteral(" px"));
            addSummary(trFilter("Angle"), blurAngle + QStringLiteral("°"));
        } else {
            addSummary(trFilter("Amount"), amount + QStringLiteral(" px"));
        }
    }
    mSummarySegments = summarySegments;

    // whether a field exists per channel (any filter kind can carry one; the time
    // offset channel is only consumed by the wave today)
    const bool hasTarget = mOwner.hasTarget();
    mHasAmountMask = hasTarget && ctrl::TimeLineUtil::hasFilterMask(
        *mOwner.target(), mType, core::FilterMaskChannel_Amount
    );
    mHasTimeMask = hasTarget && ctrl::TimeLineUtil::hasFilterMask(
        *mOwner.target(), mType, core::FilterMaskChannel_TimeOffset
    );
    mAmountPaintActive = mOwner.maskPaintIsFilter() && mOwner.maskPaintLane() == int(mType)
        && mOwner.maskPaintChannel() == int(core::FilterMaskChannel_Amount);
    mTimePaintActive = mOwner.maskPaintIsFilter() && mOwner.maskPaintLane() == int(mType)
        && mOwner.maskPaintChannel() == int(core::FilterMaskChannel_TimeOffset);

    const QVariantList after{mEnabled, mKeyState, mKeyColor, mSummary, mSummarySegments, mHasAmountMask,
                             mHasTimeMask, mAmountPaintActive, mTimePaintActive, mHue,
                             mSaturation, mValue, mAbsolute, mAmount, mDirectional, mBlurX,
                             mBlurY, mBlurAngle,
                             mWave.strength(), mWave.wavelength(), mWave.angleDeg(), mWave.phase(),
                             mWave.mix(), mWave.speed(), mWave.crest(), mWave.dualAngleDeg(),
                             mWave.dualScale(), int(mWave.deform())};
    if (before != after)
        emit changed();
}

void FilterRowModel::refreshMaskPaintState() {
    const bool amount = mOwner.maskPaintIsFilter() && mOwner.maskPaintLane() == int(mType)
        && mOwner.maskPaintChannel() == int(core::FilterMaskChannel_Amount);
    const bool time = mOwner.maskPaintIsFilter() && mOwner.maskPaintLane() == int(mType)
        && mOwner.maskPaintChannel() == int(core::FilterMaskChannel_TimeOffset);
    if (amount == mAmountPaintActive && time == mTimePaintActive)
        return;
    mAmountPaintActive = amount;
    mTimePaintActive = time;
    emit changed();
}

void FilterRowModel::refreshTheme() {
    const QColor color = theme::keyTypeColor(theme::Colors::current(), mType);
    if (color != mKeyColor) {
        mKeyColor = color;
        emit changed();
    }
}

void FilterRowModel::setExpanded(bool aExpanded) {
    if (mExpanded == aExpanded)
        return;
    mExpanded = aExpanded;
    emit changed();
}

void FilterRowModel::toggleExpanded() { mOwner.filters()->toggleExpanded(int(mType)); }

void FilterRowModel::toggleKey() {
    core::TimeLine* line = mOwner.timeLine();
    if (!line)
        return;
    if (line->hasTimeKey(mType, mOwner.frame()))
        mOwner.accessor().removeKey(mType);
    else if (isHSV())
        mOwner.accessor().knockNewHSV();
    else if (isBlur())
        mOwner.accessor().knockNewBlur();
    else if (isWave())
        mOwner.accessor().knockNewWave();
    mOwner.refreshKeys();
}

void FilterRowModel::toggleEnabled() {
    core::TimeLine* line = mOwner.timeLine();
    if (!line)
        return;
    mOwner.filters()->setFilterEnabled(int(mType), !line->filterEnabled(mType));
}

void FilterRowModel::remove() { mOwner.filters()->removeFilter(int(mType)); }

bool FilterRowModel::channelKeyExists(int aComponent) const {
    if (!core::TimeLine::usesChannelTracks(mType) || !mOwner.hasTarget()) {
        return false;
    }
    if (aComponent < 0 || core::propertyChannelCount(mType) <= aComponent) {
        return false;
    }
    return mOwner.timeLine()->hasChannelKey(mType, aComponent, mOwner.frame());
}

QString FilterRowModel::channelName(int aComponent) const {
    switch (mType) {
    case core::TimeKeyType_HSV:
        return aComponent == 0 ? QStringLiteral("Hue")
            : aComponent == 1 ? QStringLiteral("Saturation")
                              : QStringLiteral("Value");
    case core::TimeKeyType_Blur:
        return aComponent == 0 ? QStringLiteral("Radius X")
            : aComponent == 1 ? QStringLiteral("Radius Y")
                              : QStringLiteral("Angle");
    case core::TimeKeyType_Wave:
        switch (aComponent) {
        case 0: return QStringLiteral("Strength");
        case 1: return QStringLiteral("Wavelength");
        case 2: return QStringLiteral("Direction");
        case 3: return QStringLiteral("Phase");
        default: return QStringLiteral("Mix");
        }
    default:
        return QString();
    }
}

QString FilterRowModel::channelKeyAction(int aComponent) const {
    if (!core::TimeLine::usesChannelTracks(mType) || !mOwner.hasTarget()) {
        return QString();
    }
    if (aComponent < 0 || core::propertyChannelCount(mType) <= aComponent) {
        return QString();
    }
    return channelKeyExists(aComponent)
        ? QStringLiteral("Remove %1 Key").arg(channelName(aComponent))
        : QStringLiteral("Add %1 Key").arg(channelName(aComponent));
}

float FilterRowModel::channelValue(int aComponent) const {
    core::TimeLine* line = mOwner.timeLine();
    if (!line) {
        return 0.0f;
    }
    const core::TimeKeyExpans& current = line->current();
    switch (mType) {
    case core::TimeKeyType_HSV: {
        const QList<int> values = current.hsv().hsv();
        return float(values.value(aComponent, aComponent == 0 ? 0 : 100));
    }
    case core::TimeKeyType_Blur:
        if (aComponent == 0) return current.blurX();
        if (aComponent == 1) return current.blurY();
        return current.angleDeg();
    case core::TimeKeyType_Wave: {
        const core::WaveKey::Data& data = current.wave();
        switch (aComponent) {
        case 0: return data.strength();
        case 1: return data.wavelength();
        case 2: return data.angleDeg();
        case 3: return data.phase();
        default: return data.mix();
        }
    }
    default:
        return 0.0f;
    }
}

void FilterRowModel::toggleChannelKey(int aComponent) {
    if (!mOwner.hasTarget() || !mOwner.project() || !mOwner.timeLine()
        || !core::TimeLine::usesChannelTracks(mType)) {
        return;
    }
    if (aComponent < 0 || core::propertyChannelCount(mType) <= aComponent) {
        return;
    }
    core::TimeLine& line = *mOwner.timeLine();
    const int frame = mOwner.frame();
    cmnd::Stack& stack = mOwner.project()->commandStack();
    cmnd::ScopedMacro macro(stack, QStringLiteral("Toggle channel key"));
    auto* notifier = new ctrl::TimeLineUtil::Notifier(*mOwner.project());
    const bool exists = line.hasChannelKey(mType, aComponent, frame);
    notifier->event().setType(
        exists ? core::TimeLineEvent::Type_RemoveKey : core::TimeLineEvent::Type_PushKey
    );
    notifier->event().pushTarget(
        *mOwner.target(), core::TimeKeyPos(line, mType, frame, aComponent)
    );
    macro.grabListener(notifier);

    if (exists) {
        stack.push(line.createChannelRemover(mType, aComponent, frame, true));
    } else {
        auto* key = new core::ChannelKey(mType, aComponent);
        key->setFrame(frame);
        core::ChannelTrack* track = line.channelTrack(mType, aComponent);
        const float value = channelValue(aComponent);
        key->setValue(track ? track->clamp(track->quantize(value)) : value);
        key->easing() = util::Easing::defaultParam();
        stack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
        stack.push(line.createChannelPusher(mType, aComponent, frame, key));
    }
    refresh();
}

void FilterRowModel::setHue(int aValue) {
    if (!isHSV() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignHSV(aValue, QStringLiteral("hue"));
    mOwner.refreshKeys();
}

void FilterRowModel::setSaturation(int aValue) {
    if (!isHSV() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignHSV(aValue, QStringLiteral("sat"));
    mOwner.refreshKeys();
}

void FilterRowModel::setValue(int aValue) {
    if (!isHSV() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignHSV(aValue, QStringLiteral("val"));
    mOwner.refreshKeys();
}

void FilterRowModel::setAbsolute(bool aValue) {
    if (!isHSV() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignHSV(int(aValue), QStringLiteral("abs"));
    mOwner.refreshKeys();
}

void FilterRowModel::setAmount(double aValue) {
    if (!isBlur() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignBlur(float(aValue));
    mOwner.refreshKeys();
}

void FilterRowModel::setDirectional(bool aValue) {
    if (!isBlur() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignBlurDirectional(aValue);
    mOwner.refreshKeys();
}

void FilterRowModel::writeBlurEllipse(int aComponent) {
    if (!isBlur() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignBlurEllipse(
        float(mBlurX), float(mBlurY), float(mBlurAngle), aComponent
    );
    mOwner.refreshKeys();
}

void FilterRowModel::setBlurX(double aValue) {
    if (!isBlur()) return;
    mBlurX = aValue;
    writeBlurEllipse(0);
}

void FilterRowModel::setBlurY(double aValue) {
    if (!isBlur()) return;
    mBlurY = aValue;
    writeBlurEllipse(1);
}

void FilterRowModel::setBlurAngle(double aValue) {
    if (!isBlur()) return;
    mBlurAngle = aValue;
    writeBlurEllipse(2);
}

void FilterRowModel::setWaveStrength(double aValue) {
    if (!isWave()) return;
    mWave.setStrength(float(aValue));
    writeWave(0);
}

void FilterRowModel::setWaveWavelength(double aValue) {
    if (!isWave()) return;
    mWave.setWavelength(float(aValue));
    writeWave(1);
}

void FilterRowModel::setWaveDirection(double aValue) {
    if (!isWave()) return;
    mWave.setAngleDeg(float(aValue));
    writeWave(2);
}

void FilterRowModel::setWavePhase(double aValue) {
    if (!isWave()) return;
    mWave.setPhase(float(aValue));
    writeWave(3);
}

void FilterRowModel::setWaveMix(double aValue) {
    if (!isWave()) return;
    mWave.setMix(float(aValue));
    writeWave(4);
}

void FilterRowModel::setWaveSpeed(double aValue) {
    if (!isWave()) return;
    mWave.setSpeed(float(aValue));
    writeWave(-1);
}

void FilterRowModel::setWaveCrest(double aValue) {
    if (!isWave()) return;
    mWave.setCrest(float(aValue));
    writeWave(-1);
}

void FilterRowModel::setWaveDualAngle(double aValue) {
    if (!isWave()) return;
    mWave.setDualAngleDeg(float(aValue));
    writeWave(-1);
}

void FilterRowModel::setWaveDualScale(double aValue) {
    if (!isWave()) return;
    mWave.setDualScale(float(aValue));
    writeWave(-1);
}

void FilterRowModel::setWaveRipple(bool aValue) {
    if (!isWave()) return;
    mWave.setDeform(aValue ? core::WaveKey::Deform_Ripple : core::WaveKey::Deform_Sway);
    writeWave(-1);
}

void FilterRowModel::paintMask(int aChannel) {
    if (!mOwner.hasTarget())
        return;
    const auto channel = core::FilterMaskChannel(aChannel);
    if (channel < 0 || core::FilterMaskChannel_TERM <= channel)
        return;
    mOwner.requestMaskPaint(mType, channel);
}

void FilterRowModel::clearMask(int aChannel) {
    if (!mOwner.hasTarget())
        return;
    const auto channel = core::FilterMaskChannel(aChannel);
    if (channel < 0 || core::FilterMaskChannel_TERM <= channel)
        return;
    ctrl::TimeLineUtil::clearFilterMask(*mOwner.project(), *mOwner.target(), mType, channel);
    mOwner.refreshKeys();
}

void FilterRowModel::writeWave(int aComponent) {
    if (!isWave() || !mOwner.hasTarget()) return;
    mOwner.accessor().assignWave(mWave, aComponent);
    mOwner.refreshKeys();
}

//-------------------------------------------------------------------------------------------------
FilterStackModel::FilterStackModel(InspectorViewModel& aOwner, QObject* aParent):
    QObject(aParent), mOwner(aOwner) {}

QVariantList FilterStackModel::rows() const {
    QVariantList out;
    for (FilterRowModel* row : mRows)
        out.append(QVariant::fromValue(row));
    return out;
}

QVariantList FilterStackModel::catalog() const {
    const auto entry = [](const QString& aName, const QString& aDescription, core::TimeKeyType aType,
                          bool aAvailable) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), aName);
        map.insert(QStringLiteral("description"), aDescription);
        map.insert(QStringLiteral("type"), int(aType));
        map.insert(QStringLiteral("available"), aAvailable);
        return map;
    };
    return {
        entry(trFilter("HSV"), trFilter("Hue, saturation and value adjustment."),
              core::TimeKeyType_HSV, true),
        entry(trFilter("Blur"), trFilter("Directional Gaussian blur in the layer's content space."),
              core::TimeKeyType_Blur, true),
        entry(trFilter("Wave Deform"),
              trFilter("Traveling waves that distort the layer in its own content space."),
              core::TimeKeyType_Wave, true),
        entry(trFilter("Custom GLSL"), trFilter("A fragment shader with a guided, typed interface. Coming soon."),
              core::TimeKeyType_TERM, false),
    };
}

int FilterStackModel::catalogScore(int aIndex, const QString& aQuery) const {
    const QVariantList entries = catalog();
    if (aIndex < 0 || aIndex >= entries.size())
        return fuzzy::kNoMatch;
    const QVariantMap entry = entries.at(aIndex).toMap();
    // Name and description are two FIELDS of one haystack (kFieldSeparator), so the
    // matcher's phrase bonus and its joined-run rule stay inside each of them: "deform"
    // must not match by stitching the end of a name to the start of a description.
    const QString haystack = entry.value(QStringLiteral("name")).toString()
                           + fuzzy::kFieldSeparator
                           + entry.value(QStringLiteral("description")).toString();
    return fuzzy::queryScore(haystack, aQuery);
}

int FilterStackModel::catalogRowHeight(int aIndex) const {
    const QVariantList entries = catalog();
    if (aIndex < 0 || aIndex >= entries.size())
        return 0;
    return entries.at(aIndex).toMap().value(QStringLiteral("available")).toBool() ? 34 : 38;
}

QString FilterStackModel::noFiltersText() const { return trFilter("No filters"); }
QString FilterStackModel::title() const { return trFilter("Filters"); }
QString FilterStackModel::addLabel() const { return trFilter("Add filter"); }
QString FilterStackModel::searchLabel() const { return trFilter("Search filters"); }
QString FilterStackModel::noMatchLabel() const { return trFilter("No matching filters"); }
QString FilterStackModel::addedLabel() const { return trFilter("Added"); }
QString FilterStackModel::comingSoonLabel() const { return trFilter("Coming soon"); }

void FilterStackModel::setProject(core::Project* aProject) {
    mProject = aProject;
    // A different document has different filters; an open card from the old
    // one is not a state this stack can carry.
    mExpanded.clear();
    refresh();
}

void FilterStackModel::setTarget(core::ObjectNode* aTarget) {
    mTarget = aTarget;
    mExpanded.clear();
    refresh();
}

FilterRowModel* FilterStackModel::rowFor(core::TimeKeyType aType) const {
    for (FilterRowModel* row : mRows) {
        if (core::TimeKeyType(row->type()) == aType)
            return row;
    }
    return nullptr;
}

void FilterStackModel::rebuildRows(const QVector<core::FilterEntry>& aEntries) {
    for (FilterRowModel* row : mRows)
        row->deleteLater();
    mRows.clear();

    // This rebuild runs for add, remove AND reorder, so it destroys every row
    // and the expansion cannot live on the rows across it. Prune the set to the
    // types the lane still has first (a removed filter would otherwise leave
    // membership behind for a later add of the same type to inherit), then
    // build each row already expanded: the QML delegate reads `expanded` as
    // soon as rowsChanged lands, which is before any later setExpanded could
    // run, and a card that briefly existed collapsed would flash closed on
    // every drag.
    QVector<core::TimeKeyType> present;
    present.reserve(aEntries.size());
    for (const core::FilterEntry& entry : aEntries) {
        if (mExpanded.contains(entry.type))
            present.append(entry.type);
    }
    mExpanded = present;

    for (const core::FilterEntry& entry : aEntries) {
        auto* row = new FilterRowModel(entry.type, mOwner, mExpanded.contains(entry.type), this);
        mRows.append(row);
    }
    emit rowsChanged();
}

void FilterStackModel::refresh() {
    core::TimeLine* line = mOwner.timeLine();
    if (!line) {
        if (!mRows.isEmpty()) {
            for (FilterRowModel* row : mRows)
                row->deleteLater();
            mRows.clear();
            emit rowsChanged();
        }
        // No timeline means no filters, so nothing can be open; clearing here
        // and not only when rows existed keeps a stale type from surviving into
        // the next target and opening its card unasked.
        mExpanded.clear();
        return;
    }

    const QVector<core::FilterEntry> entries = line->filters();
    bool structureChanged = entries.size() != mRows.size();
    if (!structureChanged) {
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).type != core::TimeKeyType(mRows.at(i)->type())) {
                structureChanged = true;
                break;
            }
        }
    }
    if (structureChanged)
        rebuildRows(entries);

    // Expansion is a set of types, so every card the user opened stays open.
    // A card they collapsed stays collapsed too: this projects the set and
    // never picks a card on its own (the old first-entry fallback reopened a
    // card after every structural rebuild, including an undo).
    for (FilterRowModel* row : mRows) {
        row->refresh();
        row->setExpanded(mExpanded.contains(core::TimeKeyType(row->type())));
    }
}

void FilterStackModel::refreshMaskPaintState() {
    for (FilterRowModel* row : mRows)
        row->refreshMaskPaintState();
}

void FilterStackModel::refreshTheme() {
    for (FilterRowModel* row : mRows)
        row->refreshTheme();
}

bool FilterStackModel::hasFilter(int aType) const {
    core::TimeLine* line = mOwner.timeLine();
    return line && isStackFilter(core::TimeKeyType(aType)) && line->hasFilter(core::TimeKeyType(aType));
}

void FilterStackModel::addFilter(int aType) {
    const auto type = core::TimeKeyType(aType);
    if (!mProject || !mTarget || !isFilterType(type))
        return;
    ctrl::TimeLineUtil::addFilter(*mProject, *mTarget, type);
    // Adding a filter opens it - the documented first-add behaviour. As a set
    // this is additive: the second add opens that card as well instead of
    // collapsing the one the user already had open.
    if (!mExpanded.contains(type))
        mExpanded.append(type);
    refresh();
}

void FilterStackModel::removeFilter(int aType) {
    const auto type = core::TimeKeyType(aType);
    if (!mProject || !mTarget || !isFilterType(type))
        return;
    ctrl::TimeLineUtil::removeFilter(*mProject, *mTarget, type);
    // Only the removed type leaves the set; the cards the user left open stay
    // open across the rebuild that follows.
    mExpanded.removeAll(type);
    refresh();
}

void FilterStackModel::moveFilter(int aType, int aTargetIndex) {
    const auto type = core::TimeKeyType(aType);
    if (!mProject || !mTarget || !isFilterType(type))
        return;
    const int count = mRows.size();
    if (count < 2)
        return;
    ctrl::TimeLineUtil::moveFilter(*mProject, *mTarget, type, qBound(0, aTargetIndex, count - 1));
    refresh();
}

void FilterStackModel::setFilterEnabled(int aType, bool aEnabled) {
    const auto type = core::TimeKeyType(aType);
    if (!mProject || !mTarget || !isFilterType(type))
        return;
    ctrl::TimeLineUtil::setFilterEnabled(*mProject, *mTarget, type, aEnabled);
    refresh();
}

void FilterStackModel::toggleExpanded(int aType) {
    const auto type = core::TimeKeyType(aType);
    // The header click flips THIS type's membership and nothing else: several
    // filters can be open at once, which is what a one-slot `mSelected` made
    // impossible (opening a second card closed the first).
    if (mExpanded.contains(type))
        mExpanded.removeAll(type);
    else
        mExpanded.append(type);
    for (FilterRowModel* row : mRows)
        row->setExpanded(mExpanded.contains(core::TimeKeyType(row->type())));
}

} // namespace gui
