#include <QSet>
#include "XC.h"
#include "cmnd/BasicCommands.h"
#include "cmnd/Scalable.h"
#include "core/TimeLine.h"
#include "core/TimeKeyExpans.h"
#include "core/DepthKey.h"
#include "core/ImageKey.h"
#include "core/OpacityKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/MoveKey.h"
#include "core/MoveStateKey.h"
#include "core/BlurKey.h"
#include "core/BlurStateKey.h"
#include "core/HSVStateKey.h"
#include "core/WaveStateKey.h"
#include "core/WaveKey.h"
#include "core/MaskKey.h"
#include "core/Project.h"

namespace {

constexpr int kMaxLengthOfTimeKeyName = 8;
constexpr std::array<const char*, core::TimeKeyType_TERM> kTimeKeyNames = {
    "Move", "Rotate", "Scale", "Depth", "Opacity", "Bone", "Pose", "Mesh", "FFD", "Image", "HSV", "Blur",
    "Wave", "Mask"};

core::TimeLine::MapType& emptyChannelMap() {
    static core::TimeLine::MapType map;
    return map;
}

constexpr std::array<core::TimeKeyType, core::TimeKeyType_TERM> kTimeKeyTypeInOrderOfOperations = {
    core::TimeKeyType_Image,
    core::TimeKeyType_Mesh,
    core::TimeKeyType_FFD,
    core::TimeKeyType_Bone,
    core::TimeKeyType_Pose,
    core::TimeKeyType_Move,
    core::TimeKeyType_Rotate,
    core::TimeKeyType_Scale,
    core::TimeKeyType_Depth,
    core::TimeKeyType_Opacity,
    core::TimeKeyType_HSV,
    core::TimeKeyType_Blur,
    core::TimeKeyType_Wave,
    // A mask multiplies the PRESENT alpha, i.e. it is the last thing applied to
    // a node, so it resolves after every filter.
    core::TimeKeyType_Mask};

} // namespace

namespace core {

//---------------------------------------------------------------------------------------
QString TimeLine::getTimeKeyName(const TimeKeyType aType) { return {kTimeKeyNames.at(aType)}; }

QString TimeLine::getTimeKeyDisplayName(const TimeKeyType aType) {
    if (aType == TimeKeyType_Move)
        return QStringLiteral("Position");
    return getTimeKeyName(aType);
}

TimeKeyType TimeLine::getTimeKeyType(const QString& aName) {
    // The retired compact token. It is a READER alias only: serialization
    // writes the property's name ("Opacity"), so a new file never contains
    // the abbreviation.
    if (aName == QLatin1String("Opa"))
        return TimeKeyType_Opacity;
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (getTimeKeyName(static_cast<TimeKeyType>(i)) == aName) {
            return static_cast<TimeKeyType>(i);
        }
    }
    return TimeKeyType_TERM;
}

TimeKeyType TimeLine::getTimeKeyTypeInOrderOfOperations(int aIndex) {
    return kTimeKeyTypeInOrderOfOperations.at(aIndex);
}

TimeLine::TimeLine(): mMap(), mProperties(), mCurrent(new TimeKeyExpans()), mWorking(new TimeKeyExpans()), mBaseValues() {
    initProperties();
}

void TimeLine::initProperties() {
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        const auto type = static_cast<TimeKeyType>(i);
        // Image keeps its resource/grid/cache/FFD payload in a StateKey, but its
        // two offsets are scalar channels owned here (0.25): they can be keyed
        // independently without turning the whole image state into a bundle.
        if (usesChannelTracks(type) || type == TimeKeyType_Image) {
            mProperties[i] = std::make_unique<PropertyTrack>(type);
        } else {
            mProperties[i].reset();
        }
    }
}

TimeLine::~TimeLine() { clear(); }

bool TimeLine::hasTimeKey(int aIndex) const {
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (hasTimeKey(static_cast<TimeKeyType>(i), aIndex))
            return true;
    }
    return false;
}

bool TimeLine::hasTimeKey(TimeKeyType aType, int aIndex) const {
    if (usesChannelTracks(aType)) {
        return hasAnyChannelKey(aType, aIndex);
    }
    if (aType == TimeKeyType_Image) {
        return map(aType).contains(aIndex);
    }
    return mMap.at(aType).contains(aIndex);
}

bool TimeLine::isEmpty() const {
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (!isEmpty(static_cast<TimeKeyType>(i)))
            return false;
    }
    return true;
}

bool TimeLine::isEmpty(TimeKeyType aType) const {
    if (usesChannelTracks(aType)) {
        const PropertyTrack* prop = property(aType);
        return !prop || prop->isEmpty();
    }
    if (aType == TimeKeyType_Image) {
        return map(aType).isEmpty() && baseValue(aType) == nullptr;
    }
    return mMap.at(aType).isEmpty();
}

int TimeLine::validTypeCount() const {
    int count = 0;
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (!isEmpty(static_cast<TimeKeyType>(i)))
            ++count;
    }
    return count;
}

const TimeLine::MapType& TimeLine::map(TimeKeyType aType) const {
    // A track-backed type has no bundle map. Callers that iterate key maps must
    // branch on usesChannelTracks() and ask channelMap()/property() instead.
    if (usesChannelTracks(aType)) {
        return emptyChannelMap();
    }
    // Image's state payload lives in its PropertyTrack's StateTrack; the legacy
    // ImageKey map stays empty. Expose the same map currency so the existing
    // ImageKey consumers keep working while the storage is a state track.
    if (aType == TimeKeyType_Image) {
        const PropertyTrack* prop = property(aType);
        return prop ? prop->state().keys() : emptyChannelMap();
    }
    return mMap.at(aType);
}

PropertyTrack* TimeLine::property(TimeKeyType aType) { return mProperties.at(aType).get(); }

const PropertyTrack* TimeLine::property(TimeKeyType aType) const { return mProperties.at(aType).get(); }

ChannelTrack* TimeLine::channelTrack(TimeKeyType aType, int aComponent) {
    PropertyTrack* prop = property(aType);
    return prop ? prop->channel(aComponent) : nullptr;
}

const ChannelTrack* TimeLine::channelTrack(TimeKeyType aType, int aComponent) const {
    const PropertyTrack* prop = property(aType);
    return prop ? prop->channel(aComponent) : nullptr;
}

TimeLine::MapType& TimeLine::channelMap(TimeKeyType aType, int aComponent) {
    ChannelTrack* track = channelTrack(aType, aComponent);
    return track ? track->keysRef() : emptyChannelMap();
}

const TimeLine::MapType& TimeLine::channelMap(TimeKeyType aType, int aComponent) const {
    const ChannelTrack* track = channelTrack(aType, aComponent);
    return track ? track->keys() : emptyChannelMap();
}

TimeLine::MapType& TimeLine::stateMap(TimeKeyType aType) {
    PropertyTrack* prop = property(aType);
    return prop ? prop->state().keysRef() : emptyChannelMap();
}

const TimeLine::MapType& TimeLine::stateMap(TimeKeyType aType) const {
    const PropertyTrack* prop = property(aType);
    return prop ? prop->state().keys() : emptyChannelMap();
}

ChannelKey* TimeLine::channelKey(TimeKeyType aType, int aComponent, int aFrame) {
    ChannelTrack* track = channelTrack(aType, aComponent);
    return track ? track->keyAt(aFrame) : nullptr;
}

const ChannelKey* TimeLine::channelKey(TimeKeyType aType, int aComponent, int aFrame) const {
    const ChannelTrack* track = channelTrack(aType, aComponent);
    return track ? track->keyAt(aFrame) : nullptr;
}

bool TimeLine::hasChannelKey(TimeKeyType aType, int aComponent, int aFrame) const {
    const ChannelTrack* track = channelTrack(aType, aComponent);
    return track && track->hasKey(aFrame);
}

bool TimeLine::hasChannelKeys(TimeKeyType aType, int aComponent) const {
    const ChannelTrack* track = channelTrack(aType, aComponent);
    return track && !track->isEmpty();
}

bool TimeLine::hasAnyChannelKey(TimeKeyType aType, int aFrame) const {
    const PropertyTrack* prop = property(aType);
    if (!prop) {
        return mMap.at(aType).contains(aFrame);
    }
    for (int c = 0; c < prop->channelCount(); ++c) {
        if (const ChannelTrack* track = prop->channel(c)) {
            if (track->hasKey(aFrame)) {
                return true;
            }
        }
    }
    return false;
}

float TimeLine::channelBaseValue(TimeKeyType aType, int aComponent) const {
    const ChannelTrack* track = channelTrack(aType, aComponent);
    return track ? track->baseValue() : 0.0f;
}

void TimeLine::setChannelBaseValue(TimeKeyType aType, int aComponent, float aValue) {
    if (ChannelTrack* track = channelTrack(aType, aComponent)) {
        track->setBaseValue(aValue);
    }
}

QVector<TimeLine::ChannelLane> TimeLine::channelLanes(TimeKeyType aType) const {
    QVector<ChannelLane> lanes;
    if (aType == TimeKeyType_Image) {
        // Image's state key owns resource/grid/cache/FFD; these lanes are only
        // its two step-held offsets.
        const PropertyTrack* prop = property(aType);
        if (!prop) {
            return lanes;
        }
        for (int c = 0; c < 2; ++c) {
            const ChannelTrack* track = prop->channel(c);
            if (track && !track->isEmpty()) {
                lanes.push_back({c, false});
            }
        }
        return lanes;
    }
    if (!usesChannelTracks(aType)) {
        return lanes;
    }
    const PropertyTrack* prop = property(aType);
    if (!prop) {
        return lanes;
    }
    if (channelsGrouped(aType)) {
        lanes.push_back({0, true});
        return lanes;
    }
    for (int c = 0; c < prop->channelCount(); ++c) {
        const ChannelTrack* track = prop->channel(c);
        // The centroid pair is a keyable channel but not a lane: the shared
        // property dot keys it, and no UI draws it on its own.
        if (track && track->spec().graphable && !track->isEmpty()) {
            lanes.push_back({c, false});
        }
    }
    // A state-only property still has something to show: one property lane for
    // its state markers (Blur's directional mode, Move's spline, ...). There is
    // no channel to label with, so the lane carries the property name.
    if (lanes.isEmpty() && prop->state().keyCount() > 0) {
        lanes.push_back({0, true});
    }
    return lanes;
}

int TimeLine::channelLaneCount(TimeKeyType aType) const {
    return channelLanes(aType).size();
}

bool TimeLine::isFrameLinked(TimeKeyType aType, int aFrame) const {
    if (!usesChannelTracks(aType)) {
        return false;
    }
    const PropertyTrack* prop = property(aType);
    if (!prop) {
        return false;
    }
    bool anyGraphable = false;
    for (int c = 0; c < prop->channelCount(); ++c) {
        const ChannelTrack* track = prop->channel(c);
        if (!track || !track->spec().graphable) {
            continue;
        }
        anyGraphable = true;
        const ChannelKey* key = track->keyAt(aFrame);
        if (!key || !key->linked()) {
            return false;
        }
    }
    return anyGraphable;
}

bool TimeLine::canLinkFrame(TimeKeyType aType, int aFrame) const {
    if (!usesChannelTracks(aType)) {
        return false;
    }
    const PropertyTrack* prop = property(aType);
    if (!prop) {
        return false;
    }
    int graphableCount = 0;
    for (int c = 0; c < prop->channelCount(); ++c) {
        const ChannelTrack* track = prop->channel(c);
        if (!track || !track->spec().graphable) {
            continue;
        }
        ++graphableCount;
        if (!track->hasKey(aFrame)) {
            return false;
        }
    }
    return graphableCount > 1;
}

bool TimeLine::hasSplitChannelKeys(TimeKeyType aType) const {
    if (!usesChannelTracks(aType)) {
        return false;
    }
    const PropertyTrack* prop = property(aType);
    if (!prop) {
        return false;
    }

    // The union of every graphable channel's frames: a frame with only some
    // channels keyed is partial, hence split. Any linked bit cleared also splits.
    QSet<int> frames;
    for (int c = 0; c < prop->channelCount(); ++c) {
        const ChannelTrack* track = prop->channel(c);
        if (!track || !track->spec().graphable) {
            continue;
        }
        for (auto itr = track->keys().constBegin(); itr != track->keys().constEnd(); ++itr) {
            frames.insert(itr.key());
        }
    }
    for (int frame : frames) {
        if (!isFrameLinked(aType, frame)) {
            return true;
        }
    }
    return false;
}

bool TimeLine::channelsGrouped(TimeKeyType aType) const {
    if (!usesChannelTracks(aType)) {
        return false;
    }
    const PropertyTrack* prop = property(aType);
    if (!prop) {
        return false;
    }
    bool any = false;
    for (int c = 0; c < prop->channelCount(); ++c) {
        const ChannelTrack* track = prop->channel(c);
        if (track && track->spec().graphable && !track->isEmpty()) {
            any = true;
            break;
        }
    }
    return any && !hasSplitChannelKeys(aType);
}

bool TimeLine::frameEditIsLinked(TimeKeyType aType, int aFrame) const {
    if (isFrameLinked(aType, aFrame)) {
        return true;
    }
    return !hasSplitChannelKeys(aType);
}

bool TimeLine::setFrameLinked(TimeKeyType aType, int aFrame, bool aLinked) {
    if (!usesChannelTracks(aType)) {
        return false;
    }
    if (aLinked && !canLinkFrame(aType, aFrame)) {
        return false;
    }
    PropertyTrack* prop = property(aType);
    if (!prop) {
        return false;
    }
    bool any = false;
    // Every channel key at the frame, including the hidden centroid pair: the
    // flag is per key, so a later Link/Separate must not leave a stale sibling
    // behind even though the centroid is not drawn as a lane.
    for (int c = 0; c < prop->channelCount(); ++c) {
        ChannelTrack* track = prop->channel(c);
        ChannelKey* key = track ? track->keyAt(aFrame) : nullptr;
        if (key) {
            key->setLinked(aLinked);
            any = true;
        }
    }
    return any;
}

bool TimeLine::imageOffsetsAuthored() const {
    const PropertyTrack* prop = property(TimeKeyType_Image);
    if (!prop) {
        return false;
    }
    for (int c = 0; c < 2; ++c) {
        const ChannelTrack* track = prop->channel(c);
        if (track && (!track->isEmpty() || track->baseValue() != 0.0f)) {
            return true;
        }
    }
    return false;
}

int TimeLine::validChannelCount() const {
    int count = 0;
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        const auto type = static_cast<TimeKeyType>(i);
        if (usesChannelTracks(type)) {
            count += channelLaneCount(type);
        } else if (type == TimeKeyType_Image) {
            // The Image state lane plus any offset channel lanes.
            if (!isEmpty(type)) {
                ++count;
            }
            count += channelLaneCount(type);
        } else if (!mMap[i].isEmpty()) {
            ++count;
        }
    }
    return count;
}

QVector<FilterEntry> TimeLine::filters() const {
    // Stored order first (including an entry added before its first key), then
    // every filter lane that exists but was never written to the stack. The
    // canonical tail is the renderer's historical order, so a legacy project
    // opens in the order it rendered before stacks existed.
    QVector<FilterEntry> out;
    QSet<TimeKeyType> seen;
    for (const FilterEntry& entry : mFilters) {
        if (!isFilterType(entry.type) || seen.contains(entry.type))
            continue;
        out.append(entry);
        seen.insert(entry.type);
    }
    for (const TimeKeyType type : { TimeKeyType_Blur, TimeKeyType_HSV, TimeKeyType_Wave }) {
        bool authored = false;
        if (usesChannelTracks(type)) {
            const PropertyTrack* prop = property(type);
            if (prop) {
                for (int c = 0; c < prop->channelCount(); ++c) {
                    const ChannelTrack* channel = prop->channel(c);
                    if (channel && !channel->keys().isEmpty()) {
                        authored = true;
                        break;
                    }
                }
            }
        } else {
            authored = !mMap.at(type).isEmpty();
        }
        if (authored && !seen.contains(type)) {
            out.append(FilterEntry(type, true));
            seen.insert(type);
        }
    }
    return out;
}

FilterMask* TimeLine::filterMask(TimeKeyType aType, FilterMaskChannel aChannel) {
    if (!isFilterType(aType) || aChannel < 0 || FilterMaskChannel_TERM <= aChannel)
        return nullptr;
    return mFilterMasks.at(aType).at(aChannel).get();
}

const FilterMask* TimeLine::filterMask(TimeKeyType aType, FilterMaskChannel aChannel) const {
    if (!isFilterType(aType) || aChannel < 0 || FilterMaskChannel_TERM <= aChannel)
        return nullptr;
    return mFilterMasks.at(aType).at(aChannel).get();
}

FilterMask& TimeLine::ensureFilterMask(TimeKeyType aType, FilterMaskChannel aChannel, const QRectF& aContentRect) {
    XC_ASSERT(isFilterType(aType));
    XC_ASSERT(0 <= aChannel && aChannel < FilterMaskChannel_TERM);
    auto& slot = mFilterMasks.at(aType).at(aChannel);
    if (!slot)
        slot.reset(new FilterMask());
    if (slot->isEmpty() || slot->contentRect() != aContentRect)
        slot->reset(aContentRect, filterMaskNeutral(aChannel));
    return *slot;
}

void TimeLine::removeFilterMask(TimeKeyType aType, FilterMaskChannel aChannel) {
    if (isFilterType(aType) && 0 <= aChannel && aChannel < FilterMaskChannel_TERM)
        mFilterMasks.at(aType).at(aChannel).reset();
}

QSize TimeLine::currentAreaTextureSize() const { return mCurrent->areaTextureSize(); }

bool TimeLine::filterEnabled(TimeKeyType aType) const {
    if (!isFilterType(aType))
        return false;
    for (const FilterEntry& entry : mFilters) {
        if (entry.type == aType)
            return entry.enabled;
    }
    // A lane that exists without a stored entry is a filter the user has not
    // touched from the stack yet. A channel BASE value alone is structural (it
    // survives undo), not a stack lane.
    if (usesChannelTracks(aType)) {
        const PropertyTrack* prop = property(aType);
        if (prop) {
            for (int c = 0; c < prop->channelCount(); ++c) {
                const ChannelTrack* channel = prop->channel(c);
                if (channel && !channel->keys().isEmpty()) {
                    return true;
                }
            }
        }
        return false;
    }
    return !mMap.at(aType).isEmpty();
}

bool TimeLine::hasFilter(TimeKeyType aType) const {
    for (const FilterEntry& entry : filters()) {
        if (entry.type == aType)
            return true;
    }
    return false;
}

void TimeLine::setFilterEntries(const QVector<FilterEntry>& aEntries) {
    QVector<FilterEntry> normalized;
    QSet<TimeKeyType> seen;
    for (const FilterEntry& entry : aEntries) {
        if (!isFilterType(entry.type) || seen.contains(entry.type))
            continue;
        normalized.append(entry);
        seen.insert(entry.type);
    }
    mFilters = normalized;
}

TimeKey* TimeLine::timeKey(TimeKeyType aType, int aIndex) {
    // A track-backed type's keys live in channel maps; ask channelKey() with the
    // component. The legacy bundle slot is empty for it by construction.
    if (usesChannelTracks(aType)) {
        return nullptr;
    }
    if (aType == TimeKeyType_Image) {
        return map(aType).value(aIndex);
    }
    if (mMap.at(aType).contains(aIndex)) {
        return mMap[aType][aIndex];
    }
    return NULL;
}

const TimeKey* TimeLine::timeKey(TimeKeyType aType, int aIndex) const {
    if (usesChannelTracks(aType)) {
        return nullptr;
    }
    if (aType == TimeKeyType_Image) {
        return map(aType).value(aIndex);
    }
    if (mMap.at(aType).contains(aIndex)) {
        return mMap[aType][aIndex];
    }
    return NULL;
}

void TimeLine::setBaseValue(TimeKeyType aType, TimeKey* aKey) {
    if (usesChannelTracks(aType)) {
        // The base value of a track-backed property is one value per channel.
        // Node construction still hands in the legacy typed key; convert what
        // that key means and release it here.
        if (aKey) {
            if (aType == TimeKeyType_Opacity) {
                if (auto* opacityKey = dynamic_cast<OpacityKey*>(aKey)) {
                    setChannelBaseValue(aType, 0, opacityKey->opacity());
                }
            } else if (aType == TimeKeyType_Rotate) {
                if (auto* rotate = dynamic_cast<RotateKey*>(aKey)) {
                    setChannelBaseValue(aType, 0, rotate->rotate());
                }
            } else if (aType == TimeKeyType_Depth) {
                if (auto* depth = dynamic_cast<DepthKey*>(aKey)) {
                    setChannelBaseValue(aType, 0, depth->depth());
                }
            } else if (aType == TimeKeyType_Scale) {
                if (auto* scale = dynamic_cast<ScaleKey*>(aKey)) {
                    setChannelBaseValue(aType, 0, scale->scale().x());
                    setChannelBaseValue(aType, 1, scale->scale().y());
                }
            } else if (aType == TimeKeyType_Move) {
                if (auto* move = dynamic_cast<MoveKey*>(aKey)) {
                    setChannelBaseValue(aType, 0, move->pos().x());
                    setChannelBaseValue(aType, 1, move->pos().y());
                    setChannelBaseValue(aType, 2, move->centroid().x());
                    setChannelBaseValue(aType, 3, move->centroid().y());
                    if (PropertyTrack* prop = property(aType)) {
                        auto state = std::make_unique<MoveStateKey>();
                        state->setSpline(move->data().spline());
                        prop->state().setBaseState(std::move(state));
                    }
                }
            } else if (aType == TimeKeyType_Blur) {
                if (auto* blur = dynamic_cast<BlurKey*>(aKey)) {
                    setChannelBaseValue(aType, 0, blur->blurX());
                    setChannelBaseValue(aType, 1, blur->blurY());
                    setChannelBaseValue(aType, 2, blur->angleDeg());
                    if (PropertyTrack* prop = property(aType)) {
                        auto state = std::make_unique<BlurStateKey>();
                        state->setDirectional(blur->isDirectional());
                        prop->state().setBaseState(std::move(state));
                    }
                }
            } else if (aType == TimeKeyType_HSV) {
                if (auto* hsv = dynamic_cast<HSVKey*>(aKey)) {
                    const QList<int>& values = hsv->hsv();
                    for (int c = 0; c < 3 && c < values.size(); ++c) {
                        setChannelBaseValue(aType, c, float(values[c]));
                    }
                    if (PropertyTrack* prop = property(aType)) {
                        auto state = std::make_unique<HSVStateKey>();
                        state->setAbsolute(values.size() > 3 && values[3] != 0);
                        prop->state().setBaseState(std::move(state));
                    }
                }
            } else if (aType == TimeKeyType_Wave) {
                if (auto* wave = dynamic_cast<WaveKey*>(aKey)) {
                    const WaveKey::Data& d = wave->data();
                    setChannelBaseValue(aType, 0, d.strength());
                    setChannelBaseValue(aType, 1, d.wavelength());
                    setChannelBaseValue(aType, 2, d.angleDeg());
                    setChannelBaseValue(aType, 3, d.phase());
                    setChannelBaseValue(aType, 4, d.mix());
                    if (PropertyTrack* prop = property(aType)) {
                        auto state = std::make_unique<WaveStateKey>();
                        state->setSpeed(d.speed());
                        state->setCrest(d.crest());
                        state->setDualAngleDeg(d.dualAngleDeg());
                        state->setDualScale(d.dualScale());
                        state->setTimeOffset(d.timeOffset());
                        state->setDeform(d.deform());
                        prop->state().setBaseState(std::move(state));
                    }
                }
            }
            delete aKey;
        }
        return;
    }
    if (aType == TimeKeyType_Image) {
        PropertyTrack* prop = property(aType);
        auto* state = dynamic_cast<StateKey*>(aKey);
        if (prop && state) {
            state->setFrame(kBaseValueIndex);
            prop->state().setBaseState(std::unique_ptr<StateKey>(state));
            setChannelBaseValue(aType, 0, static_cast<ImageKey*>(state)->data().imageOffset().x());
            setChannelBaseValue(aType, 1, static_cast<ImageKey*>(state)->data().imageOffset().y());
            return;
        }
        delete aKey;
        return;
    }
    if (aKey) {
        aKey->setFrame(kBaseValueIndex);
    }
    mBaseValues.at(aType).reset(aKey);
    // A newly constructed layer's base ImageKey supplies the offset channel
    // bases; 0.25 keeps the authoritative animated value in those tracks.
    if (aType == TimeKeyType_Image) {
        if (const auto* image = dynamic_cast<const ImageKey*>(aKey)) {
            setChannelBaseValue(aType, 0, image->data().imageOffset().x());
            setChannelBaseValue(aType, 1, image->data().imageOffset().y());
        }
    }
}

TimeKey* TimeLine::baseValue(TimeKeyType aType) {
    if (usesChannelTracks(aType)) {
        return nullptr;
    }
    if (aType == TimeKeyType_Image) {
        PropertyTrack* prop = property(aType);
        return prop ? prop->state().baseState() : nullptr;
    }
    return mBaseValues.at(aType).data();
}

const TimeKey* TimeLine::baseValue(TimeKeyType aType) const {
    if (usesChannelTracks(aType)) {
        return nullptr;
    }
    if (aType == TimeKeyType_Image) {
        const PropertyTrack* prop = property(aType);
        return prop ? prop->state().baseState() : nullptr;
    }
    return mBaseValues.at(aType).data();
}

void TimeLine::clear() {
    mFilters.clear();
    for (auto& lane : mFilterMasks) {
        for (auto& mask : lane) {
            mask.reset();
        }
    }
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        mBaseValues[i].reset();

        QList<TimeKey*> values = mMap[i].values();
        qDeleteAll(values.begin(), values.end());
        mMap[i].clear();
    }
    for (auto& prop : mProperties) {
        prop.reset();
    }
    initProperties();
    mCurrent.reset(new TimeKeyExpans());
}

bool TimeLine::move(TimeKeyType aType, int aFrom, int aTo) {
    if (aType == TimeKeyType_Image) {
        return moveStateKeys(aType, { qMakePair(aFrom, aTo) });
    }
    if (usesChannelTracks(aType)) {
        PropertyTrack* prop = property(aType);
        if (!prop || prop->channelCount() != 1) {
            return false;
        }
        ChannelTrack* track = prop->channel(0);
        return track && track->moveKey(aFrom, aTo);
    }

    TimeLine::MapType& map = mMap.at(aType);

    if (!map.contains(aFrom) || map.contains(aTo)) {
        return false;
    }

    TimeKey* key = map.value(aFrom);
    XC_PTR_ASSERT(key);
    map.remove(aFrom);
    map.insert(aTo, key);
    key->setFrame(aTo);
    return true;
}

bool TimeLine::moveChannelKeys(
    TimeKeyType aType, int aComponent, const QVector<QPair<int, int>>& aMoves
) {
    ChannelTrack* track = channelTrack(aType, aComponent);
    if (!track) {
        return false;
    }
    TimeLine::MapType& map = track->keysRef();
    QSet<int> sources;
    QSet<int> destinations;
    for (const auto& aMove : aMoves) {
        if (!map.contains(aMove.first)) {
            return false;
        }
        if (sources.contains(aMove.first) || destinations.contains(aMove.second)) {
            return false;
        }
        sources.insert(aMove.first);
        destinations.insert(aMove.second);
    }
    for (int destination : destinations) {
        if (map.contains(destination) && !sources.contains(destination)) {
            return false;
        }
    }
    QVector<TimeKey*> keys;
    keys.reserve(aMoves.size());
    for (const auto& aMove : aMoves) {
        TimeKey* key = map.take(aMove.first);
        XC_PTR_ASSERT(key);
        keys.push_back(key);
    }
    for (int i = 0; i < aMoves.size(); ++i) {
        TimeKey* key = keys.at(i);
        const int to = aMoves.at(i).second;
        map.insert(to, key);
        key->setFrame(to);
    }
    return true;
}

bool TimeLine::moveStateKeys(TimeKeyType aType, const QVector<QPair<int, int>>& aMoves) {
    PropertyTrack* prop = property(aType);
    if (!prop) {
        return false;
    }
    TimeLine::MapType& map = prop->state().keysRef();
    QSet<int> sources;
    QSet<int> destinations;
    for (const auto& aMove : aMoves) {
        if (!map.contains(aMove.first)) {
            return false;
        }
        if (sources.contains(aMove.first) || destinations.contains(aMove.second)) {
            return false;
        }
        sources.insert(aMove.first);
        destinations.insert(aMove.second);
    }
    for (int destination : destinations) {
        if (map.contains(destination) && !sources.contains(destination)) {
            return false;
        }
    }
    QVector<TimeKey*> keys;
    keys.reserve(aMoves.size());
    for (const auto& aMove : aMoves) {
        TimeKey* key = map.take(aMove.first);
        XC_PTR_ASSERT(key);
        keys.push_back(key);
    }
    for (int i = 0; i < aMoves.size(); ++i) {
        TimeKey* key = keys.at(i);
        const int to = aMoves.at(i).second;
        map.insert(to, key);
        key->setFrame(to);
    }
    return true;
}

bool TimeLine::moveKeys(TimeKeyType aType, const QVector<QPair<int, int>>& aMoves) {
    if (aType == TimeKeyType_Image) {
        return moveStateKeys(aType, aMoves);
    }
    PropertyTrack* prop = usesChannelTracks(aType) ? property(aType) : nullptr;
    if (prop && 1 < prop->channelCount()) {
        // A whole-key move of a multi-channel property moves every channel that
        // holds a key at the source frame. Validate the WHOLE batch across every
        // channel before detaching anything, so a refused move leaves the
        // document untouched.
        QSet<int> sources;
        QSet<int> destinations;
        for (const auto& aMove : aMoves) {
            bool any = false;
            for (int c = 0; c < prop->channelCount(); ++c) {
                const ChannelTrack* track = prop->channel(c);
                if (track && track->hasKey(aMove.first)) {
                    any = true;
                    break;
                }
            }
            if (!any) {
                return false;
            }
            if (sources.contains(aMove.first) || destinations.contains(aMove.second)) {
                return false;
            }
            sources.insert(aMove.first);
            destinations.insert(aMove.second);
        }
        for (int c = 0; c < prop->channelCount(); ++c) {
            const ChannelTrack* track = prop->channel(c);
            if (!track) {
                continue;
            }
            for (int destination : destinations) {
                if (track->hasKey(destination) && !sources.contains(destination)) {
                    return false;
                }
            }
        }

        // A property key includes its state key: a whole-key move carries it too.
        QVector<QPair<int, int>> stateMoves;
        for (const auto& aMove : aMoves) {
            if (prop->state().keys().contains(aMove.first)) {
                stateMoves.append(aMove);
            }
        }
        if (!stateMoves.isEmpty() && !moveStateKeys(aType, stateMoves)) {
            return false;
        }
        for (int c = 0; c < prop->channelCount(); ++c) {
            ChannelTrack* track = prop->channel(c);
            if (!track) {
                continue;
            }
            QVector<TimeKey*> keys;
            keys.reserve(aMoves.size());
            for (const auto& aMove : aMoves) {
                TimeKey* key = track->keysRef().take(aMove.first);
                keys.push_back(key);
            }
            for (int i = 0; i < aMoves.size(); ++i) {
                TimeKey* key = keys.at(i);
                if (!key) {
                    continue;
                }
                const int to = aMoves.at(i).second;
                track->keysRef().insert(to, key);
                key->setFrame(to);
            }
        }

        return true;
    }

    TimeLine::MapType* mapPtr = nullptr;
    if (usesChannelTracks(aType)) {
        if (!prop || prop->channelCount() != 1) {
            return false;
        }
        ChannelTrack* track = prop->channel(0);
        if (!track) {
            return false;
        }
        mapPtr = &track->keysRef();
    } else {
        mapPtr = &mMap.at(aType);
    }
    TimeLine::MapType& map = *mapPtr;

    // Validate the whole batch before touching the map: a refused batch must
    // leave every key where it was.
    QSet<int> sources;
    QSet<int> destinations;
    for (const auto& aMove : aMoves) {
        // every source must exist
        if (!map.contains(aMove.first)) {
            return false;
        }
        // a source or destination may appear only once
        if (sources.contains(aMove.first) || destinations.contains(aMove.second)) {
            return false;
        }
        sources.insert(aMove.first);
        destinations.insert(aMove.second);
    }
    for (int destination : destinations) {
        // a destination is free, or is vacated by this same batch
        if (map.contains(destination) && !sources.contains(destination)) {
            return false;
        }
    }

    // Detach every source first so frames vacated by this batch are free for
    // the incoming keys regardless of the order the moves are listed in.
    QVector<TimeKey*> keys;
    keys.reserve(aMoves.size());
    for (const auto& aMove : aMoves) {
        TimeKey* key = map.take(aMove.first);
        XC_PTR_ASSERT(key);
        keys.push_back(key);
    }
    for (int i = 0; i < aMoves.size(); ++i) {
        TimeKey* key = keys.at(i);
        const int to = aMoves.at(i).second;
        map.insert(to, key);
        key->setFrame(to);
    }
    return true;
}

bool TimeLine::overwriteMove(TimeKeyType aType, int aFrom, int aTo) {
    if (aType == TimeKeyType_Image) {
        return moveStateKeys(aType, { qMakePair(aFrom, aTo) });
    }
    TimeLine::MapType* mapPtr = nullptr;
    if (usesChannelTracks(aType)) {
        PropertyTrack* prop = property(aType);
        if (!prop || prop->channelCount() != 1) {
            return false;
        }
        ChannelTrack* track = prop->channel(0);
        if (!track) {
            return false;
        }
        mapPtr = &track->keysRef();
    } else {
        mapPtr = &mMap.at(aType);
    }
    TimeLine::MapType& map = *mapPtr;

    if(!map.contains(aFrom)) { return false; }
    if(map.contains(aTo)){
        map.remove(aTo);
    }

    TimeKey* key = map.value(aFrom);
    XC_PTR_ASSERT(key);
    map.remove(aFrom);
    map.insert(aTo, key);
    key->setFrame(aTo);
    return true;
}

bool TimeLine::adoptKey(TimeKeyType aType, int aFrame, TimeKey* aTimeKey) {
    if (!aTimeKey)
        return false;

    if (usesChannelTracks(aType)) {
        if (aFrame == kBaseValueIndex) {
            return false;
        }
        auto* channelKey = dynamic_cast<ChannelKey*>(aTimeKey);
        if (!channelKey) {
            return false;
        }
        ChannelTrack* track = channelTrack(aType, channelKey->component());
        return track && track->adoptKey(channelKey);
    }

    if (aType == TimeKeyType_Image) {
        PropertyTrack* prop = property(aType);
        auto* state = dynamic_cast<StateKey*>(aTimeKey);
        if (!prop || !state) {
            return false;
        }
        state->setFrame(aFrame);
        if (aFrame == kBaseValueIndex) {
            if (prop->state().baseState()) {
                return false;
            }
            prop->state().setBaseState(std::unique_ptr<StateKey>(state));
            return true;
        }
        return prop->state().insertKey(aFrame, std::unique_ptr<StateKey>(state)) != nullptr;
    }
    if (aFrame == kBaseValueIndex) {
        if (mBaseValues[aType])
            return false;
        aTimeKey->setFrame(aFrame);
        mBaseValues[aType].reset(aTimeKey);
        return true;
    }
    if (mMap[aType].contains(aFrame))
        return false;
    aTimeKey->setFrame(aFrame);
    mMap[aType].insert(aFrame, aTimeKey);
    return true;
}

cmnd::Base* TimeLine::createPusher(TimeKeyType aType, int aFrame, TimeKey* aTimeKey) {
    if (aType == TimeKeyType_Image) {
        auto* state = dynamic_cast<StateKey*>(aTimeKey);
        if (!state) {
            delete aTimeKey;
            return new cmnd::LambdaScalable([](cmnd::Vector&) {});
        }
        return createStatePusher(aType, aFrame, state);
    }
    return new cmnd::LambdaScalable([=](cmnd::Vector& aCommands) {
        TimeLine::MapType& map = this->mMap.at(aType);
        // remove old key
        if (map.contains(aFrame)) {
            pushRemoveCommands(aType, aFrame, aCommands);
        }
        aTimeKey->setFrame(aFrame);
        aCommands.push(new cmnd::InsertMap<int, TimeKey*>(map, aFrame, aTimeKey));
    });
}

cmnd::Base* TimeLine::createStatePusher(TimeKeyType aType, int aFrame, StateKey* aStateKey) {
    return new cmnd::LambdaScalable([=](cmnd::Vector& aCommands) {
        PropertyTrack* prop = property(aType);
        if (!prop || !aStateKey || aStateKey->type() != aType) {
            delete aStateKey;
            return;
        }
        TimeLine::MapType& map = prop->state().keysRef();
        if (map.contains(aFrame)) {
            pushRemoveStateCommands(*prop, aFrame, aCommands);
        }
        aStateKey->setFrame(aFrame);
        aCommands.push(new cmnd::InsertMap<int, TimeKey*>(map, aFrame, aStateKey));
    });
}

cmnd::Base* TimeLine::createStateRemover(TimeKeyType aType, int aFrame, bool aOptional) {
    return new cmnd::LambdaScalable([=](cmnd::Vector& aCommands) {
        PropertyTrack* prop = property(aType);
        if (!prop) {
            return;
        }
        TimeLine::MapType& map = prop->state().keysRef();
        XC_ASSERT(aOptional || map.contains(aFrame));
        if (map.contains(aFrame)) {
            pushRemoveStateCommands(*prop, aFrame, aCommands);
        }
    });
}

cmnd::Base* TimeLine::createRemover(TimeKeyType aType, int aFrame, bool aOptional) {
    if (aType == TimeKeyType_Image) {
        return createStateRemover(aType, aFrame, aOptional);
    }
    return new cmnd::LambdaScalable([=](cmnd::Vector& aCommands) {
        TimeLine::MapType& map = this->mMap.at(aType);
        XC_ASSERT(aOptional || map.contains(aFrame));
        (void)aOptional;

        if (map.contains(aFrame)) {
            // remove
            pushRemoveCommands(aType, aFrame, aCommands);
        }
    });
}

cmnd::Base* TimeLine::createChannelPusher(
    TimeKeyType aType, int aComponent, int aFrame, ChannelKey* aChannelKey
) {
    return new cmnd::LambdaScalable([=](cmnd::Vector& aCommands) {
        ChannelTrack* track = this->channelTrack(aType, aComponent);
        if (!track || !aChannelKey || aChannelKey->type() != aType
            || aChannelKey->component() != aComponent) {
            delete aChannelKey;
            return;
        }

        TimeLine::MapType& map = track->keysRef();
        if (map.contains(aFrame)) {
            TimeKey* old = map.value(aFrame);
            aCommands.push(new cmnd::RemoveMap<int, TimeKey*>(map, aFrame));
            aCommands.push(new cmnd::Sleep(old));
            aCommands.push(new cmnd::GrabDeleteObject<TimeKey>(old));
        }
        aChannelKey->setFrame(aFrame);
        aCommands.push(new cmnd::InsertMap<int, TimeKey*>(map, aFrame, aChannelKey));
    });
}

cmnd::Base* TimeLine::createChannelRemover(
    TimeKeyType aType, int aComponent, int aFrame, bool aOptional
) {
    return new cmnd::LambdaScalable([=](cmnd::Vector& aCommands) {
        ChannelTrack* track = this->channelTrack(aType, aComponent);
        if (!track) {
            return;
        }
        TimeLine::MapType& map = track->keysRef();
        XC_ASSERT(aOptional || map.contains(aFrame));
        (void)aOptional;
        if (map.contains(aFrame)) {
            TimeKey* old = map.value(aFrame);
            aCommands.push(new cmnd::RemoveMap<int, TimeKey*>(map, aFrame));
            aCommands.push(new cmnd::Sleep(old));
            aCommands.push(new cmnd::GrabDeleteObject<TimeKey>(old));
        }
    });
}

void TimeLine::pushRemoveCommands(TimeKeyType aType, int aFrame, cmnd::Vector& aCommands) {
    TimeLine::MapType& map = mMap.at(aType);
    TimeKey* key = map.value(aFrame);
    XC_ASSERT(key->frame() == aFrame);

    // remove children
    for (auto itr = key->children().rbegin(); itr != key->children().rend(); ++itr) {
        TimeKey* child = *itr;
        TimeLine::MapType& cmap = mMap.at(child->type());
        const int cframe = child->frame();
        XC_ASSERT(child == cmap.value(cframe));
        aCommands.push(new cmnd::PopBackTree<TimeKey>(&key->children()));
        aCommands.push(new cmnd::RemoveMap<int, TimeKey*>(cmap, cframe));
        aCommands.push(new cmnd::Sleep(child));
        aCommands.push(new cmnd::GrabDeleteObject<TimeKey>(child));
    }

    // remove from parent
    if (key->parent()) {
        aCommands.push(new cmnd::RemoveTreeByObj<TimeKey>(&key->parent()->children(), key));
    }

    // remove
    aCommands.push(new cmnd::RemoveMap<int, TimeKey*>(map, aFrame));
    aCommands.push(new cmnd::Sleep(key));
    aCommands.push(new cmnd::GrabDeleteObject<TimeKey>(key));
}

void TimeLine::pushRemoveStateCommands(
    PropertyTrack& aProp, int aFrame, cmnd::Vector& aCommands
) {
    TimeLine::MapType& map = aProp.state().keysRef();
    TimeKey* key = map.value(aFrame);
    if (!key) {
        return;
    }
    // An ImageStateKey may hold FFD children. Remove them from their own maps
    // exactly as a structural key's remove path does, so a state-key delete
    // cannot orphan a child in the FFD map.
    for (auto itr = key->children().rbegin(); itr != key->children().rend(); ++itr) {
        TimeKey* child = *itr;
        TimeLine::MapType& childMap = mMap.at(child->type());
        aCommands.push(new cmnd::PopBackTree<TimeKey>(&key->children()));
        if (childMap.value(child->frame()) == child) {
            aCommands.push(new cmnd::RemoveMap<int, TimeKey*>(childMap, child->frame()));
        }
        aCommands.push(new cmnd::Sleep(child));
        aCommands.push(new cmnd::GrabDeleteObject<TimeKey>(child));
    }
    if (key->parent()) {
        aCommands.push(new cmnd::RemoveTreeByObj<TimeKey>(&key->parent()->children(), key));
    }
    aCommands.push(new cmnd::RemoveMap<int, TimeKey*>(map, aFrame));
    aCommands.push(new cmnd::Sleep(key));
    aCommands.push(new cmnd::GrabDeleteObject<TimeKey>(key));
}

void TimeLine::migrateImageOffsets() {
    PropertyTrack* prop = property(TimeKeyType_Image);
    if (!prop) {
        return;
    }
    const ImageKey* base = static_cast<const ImageKey*>(baseValue(TimeKeyType_Image));
    if (base) {
        prop->channel(0)->setBaseValue(base->data().imageOffset().x());
        prop->channel(1)->setBaseValue(base->data().imageOffset().y());
    }
    const TimeLine::MapType& imageMap = map(TimeKeyType_Image);
    for (auto itr = imageMap.constBegin(); itr != imageMap.constEnd(); ++itr) {
        const auto* key = static_cast<const ImageKey*>(itr.value());
        if (!key) {
            continue;
        }
        prop->channel(0)->insertKey(itr.key(), key->data().imageOffset().x());
        prop->channel(1)->insertKey(itr.key(), key->data().imageOffset().y());
    }
}

int TimeLine::serializeTypeCount() const {
    int count = 0;
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        const auto type = static_cast<TimeKeyType>(i);
        if (usesChannelTracks(type)) {
            const PropertyTrack* prop = property(type);
            if (prop && !prop->isEmpty()) {
                ++count;
            }
            continue;
        }
        if (baseValue(type) || !map(type).isEmpty())
            ++count;
    }
    return count;
}

bool TimeLine::serialize(Serializer& aOut) const {
    static const std::array<uint8, 8> kSignature = {'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'};
    static const std::array<uint8, 8> kMapSignature = {'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'};

    // signature
    auto pos = aOut.beginBlock(kSignature);

    // non-empty type count (including base values)
    aOut.write(serializeTypeCount());

    // each map
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        const auto type = static_cast<TimeKeyType>(i);

        if (usesChannelTracks(type)) {
            const PropertyTrack* prop = property(type);
            if (!prop || prop->isEmpty()) {
                continue;
            }

            auto mapPos = aOut.beginBlock(kMapSignature);
            aOut.write(getTimeKeyName(type));
            // 0.23: the block is channel tracks, not bundle keys.
            aOut.write(true);
            aOut.write(prop->channelCount());
            for (int c = 0; c < prop->channelCount(); ++c) {
                const ChannelTrack* track = prop->channel(c);
                aOut.write(c);
                if (!track || !track->serialize(aOut)) {
                    return false;
                }
            }
            if (!prop->state().serialize(aOut)) {
                return false;
            }
            aOut.endBlock(mapPos);
            continue;
        }

        const MapType& map = this->map(type);
        const TimeKey* base = baseValue(type);

        if (!base && map.isEmpty())
            continue;

        // signature
        auto mapPos = aOut.beginBlock(kMapSignature);

        // type name
        aOut.write(getTimeKeyName(type));

        // 0.23 legacy marker: this block is still the bundle-key layout.
        aOut.write(false);

        // a base value exists
        aOut.write(static_cast<bool>(base));

        // defautltKey
        if (base) {
            if (!serializeTimeKey(aOut, *base)) {
                return false;
            }
        }

        // key count
        aOut.write(static_cast<int>(map.count()));

        for (auto itr = map.begin(); itr != map.end(); ++itr) {
            // timekey index
            aOut.write(itr.key());

            const TimeKey* timeKey = itr.value();
            XC_PTR_ASSERT(timeKey);

            // time key
            if (!serializeTimeKey(aOut, *timeKey)) {
                return false;
            }
        }

        aOut.endBlock(mapPos);
    }

    // filter stack (0.17): order + enabled flags. The filter VALUES are the key
    // maps above; this block is only the stack.
    const QVector<FilterEntry> stack = filters();
    aOut.write(static_cast<int>(stack.size()));
    for (const FilterEntry& entry : stack) {
        aOut.write(getTimeKeyName(entry.type));
        aOut.write(entry.enabled);
    }

    // paintable filter masks (0.19): one field per lane that has one
    {
        int maskCount = 0;
        for (const auto& lane : mFilterMasks) {
            for (const auto& mask : lane) {
                if (mask && !mask->isEmpty())
                    ++maskCount;
            }
        }
        aOut.write(maskCount);
        for (int i = 0; i < TimeKeyType_TERM; ++i) {
            for (int c = 0; c < FilterMaskChannel_TERM; ++c) {
                const auto& mask = mFilterMasks[i][c];
                if (!mask || mask->isEmpty())
                    continue;
                aOut.write(getTimeKeyName(static_cast<TimeKeyType>(i)));
                aOut.write(c);
                if (!mask->serialize(aOut)) {
                    return false;
                }
            }
        }
    }

    // Image offsets (0.25): two step-held channel tracks beside the Image state
    // key. The block is written only when an offset is authored; a 0.25 reader
    // treats its absence as unauthored, while a pre-0.25 reader migrates the
    // legacy key offset.
    if (imageOffsetsAuthored()) {
        static const std::array<uint8, 8> kImageOffsetsSignature = {
            'I', 'm', 'g', 'O', 'f', 'f', 's', '_'
        };
        auto offsetsPos = aOut.beginBlock(kImageOffsetsSignature);
        const PropertyTrack* prop = property(TimeKeyType_Image);
        aOut.write(2);
        for (int c = 0; c < 2; ++c) {
            const ChannelTrack* track = prop ? prop->channel(c) : nullptr;
            if (!track || !track->serialize(aOut)) {
                return false;
            }
        }
        aOut.endBlock(offsetsPos);
    }

    aOut.endBlock(pos);

    return aOut.checkStream();
}

bool TimeLine::serializeTimeKey(Serializer& aOut, const TimeKey& aTimeKey) const {
    // reference id
    aOut.writeID(&aTimeKey);

    // timekey value
    if (!aTimeKey.serialize(aOut)) {
        return false;
    }

    // stable id and, when proportional, its bound ids and per-axis weights
    aOut.write(aTimeKey.id());
    const TimeKey::Proportional* proportional = aTimeKey.proportionalState();
    aOut.write(static_cast<bool>(proportional));
    if (proportional) {
        aOut.write(proportional->leftId);
        aOut.write(proportional->rightId);
        aOut.write(proportional->timeValid);
        aOut.write(proportional->timeWeight);
        // The payload's length is a format fact: 0.16-0.17 wrote three values,
        // 0.18 writes kMaxProportionalComponents (a Wave key has five). A newer
        // file therefore cannot be read by an older build, which is what the
        // format version is for.
        for (int c = 0; c < TimeKey::kMaxProportionalComponents; ++c) {
            aOut.write(proportional->valueValid[c]);
            aOut.write(proportional->valueWeight[c]);
        }
    }

    // child count
    aOut.write((int)aTimeKey.children().size());

    // references to children
    for (const TimeKey* child : aTimeKey.children()) {
        aOut.writeID(child);
    }
    return true;
}

bool TimeLine::deserialize(Deserializer& aIn) {
    clear();

    // check block begin
    if (!aIn.beginBlock("TimeLine")) {
        return false;
    }
    aIn.pushLogScope("TimeLine");

    // non empty type count (including base values)
    int mapCount = 0;
    aIn.read(mapCount);

    // each map
    for (int i = 0; i < mapCount; ++i) {
        // check block begin
        if (!aIn.beginBlock("TimeMap_")) {
            return false;
        }
        aIn.pushLogScope("TimeKeyMap");

        // key type
        QString typeName;
        aIn.read(typeName, kMaxLengthOfTimeKeyName);
        auto typeIndex = getTimeKeyType(typeName);

        // pass only supported index
        if (typeIndex >= TimeKeyType_TERM) {
            if (!aIn.endBlock()) {
                return false;
            }
            aIn.popLogScope();
            continue;
        }

        const auto type = static_cast<TimeKeyType>(typeIndex);
        bool channelTracks = false;
        if (aIn.version().majorVersion() == 0 && aIn.version().minorVersion() >= 23) {
            aIn.read(channelTracks);
        }

        if (channelTracks) {
            if (!usesChannelTracks(type)) {
                return aIn.errored("channel tracks for an unsupported key type");
            }
            int channelCount = 0;
            aIn.read(channelCount);
            if (channelCount < 0 || PropertyTrack::kMaxChannels < channelCount) {
                return aIn.errored("invalid channel count");
            }
            PropertyTrack* prop = property(type);
            if (!prop) {
                return aIn.errored("missing property track");
            }
            for (int c = 0; c < channelCount; ++c) {
                int component = -1;
                aIn.read(component);
                ChannelTrack* track = channelTrack(type, component);
                if (!track) {
                    return aIn.errored("invalid channel component");
                }
                if (!track->deserialize(aIn)) {
                    return false;
                }
            }
            if (!prop->state().deserialize(aIn)) {
                return false;
            }
        } else {
            // a base value exists
            bool hasBaseValue = false;
            aIn.read(hasBaseValue);

            // base value
            if (hasBaseValue) {
                if (!deserializeTimeKey(aIn, type, TimeLine::kBaseValueIndex)) {
                    return false;
                }
            }

            // timekey count
            int keyCount = 0;
            aIn.read(keyCount);

            for (int k = 0; k < keyCount; ++k) {
                // timekey index
                int keyIndex = 0;
                aIn.read(keyIndex);

                // time key
                if (!deserializeTimeKey(aIn, type, keyIndex)) {
                    return false;
                }
            }
        }

        // check block end
        if (!aIn.endBlock()) {
            return false;
        }
        aIn.popLogScope();
    }

    // Old files stored Rotate as a bundle key map. Convert it once, after all
    // maps are read, so a 0.22 project opens into the same per-channel tracks a
    // newly saved 0.23 project holds.
    migrateLegacyKeyMapsToTracks();

    // filter stack (0.17). A legacy file has no block: its filters are the lanes
    // that already exist, in the order the renderer used (blur then HSV).
    mFilters.clear();
    if (aIn.version().majorVersion() == 0 && aIn.version().minorVersion() >= 17) {
        int filterCount = 0;
        aIn.read(filterCount);
        for (int i = 0; i < filterCount; ++i) {
            QString typeName;
            aIn.read(typeName, kMaxLengthOfTimeKeyName);
            const TimeKeyType type = getTimeKeyType(typeName);
            bool enabled = true;
            aIn.read(enabled);
            if (isFilterType(type))
                mFilters.append(FilterEntry(type, enabled));
        }
        setFilterEntries(mFilters);
    } else {
        for (const TimeKeyType type : { TimeKeyType_Blur, TimeKeyType_HSV, TimeKeyType_Wave }) {
            if (!mMap.at(type).isEmpty())
                mFilters.append(FilterEntry(type, true));
        }
    }

    // paintable filter masks (0.19). Old files have none: a filter lane without a
    // mask renders exactly as it did before masks existed.
    for (auto& lane : mFilterMasks) {
        for (auto& mask : lane) {
            mask.reset();
        }
    }
    if (aIn.version().majorVersion() == 0 && aIn.version().minorVersion() >= 19) {
        int maskCount = 0;
        aIn.read(maskCount);
        if (maskCount < 0 || TimeKeyType_TERM * FilterMaskChannel_TERM < maskCount) {
            return aIn.errored("invalid filter mask count");
        }
        for (int i = 0; i < maskCount; ++i) {
            QString typeName;
            aIn.read(typeName, kMaxLengthOfTimeKeyName);
            int channel = 0;
            aIn.read(channel);
            const TimeKeyType type = getTimeKeyType(typeName);
            auto mask = std::make_unique<FilterMask>();
            if (!mask->deserialize(aIn)) {
                return false;
            }
            if (isFilterType(type) && 0 <= channel && channel < FilterMaskChannel_TERM)
                mFilterMasks.at(type).at(channel) = std::move(mask);
        }
    }

    // Image offsets (0.25). Older files keep their offsets in ImageKey; migrate
    // them into the two step-held channel tracks. A 0.25 file with no block has
    // deliberately unauthored offsets: its ImageKey field is a compatibility
    // mirror, so migrating it would resurrect an offset the user removed.
    const bool hasOffsetChannels = aIn.version().majorVersion() == 0
        && aIn.version().minorVersion() >= 25;
    if (hasOffsetChannels) {
        if (aIn.hasBlock("ImgOffs_")) {
            if (!aIn.beginBlock("ImgOffs_")) {
                return false;
            }
            int count = 0;
            aIn.read(count);
            if (count < 0 || 2 < count) {
                return aIn.errored("invalid image offset count");
            }
            PropertyTrack* prop = property(TimeKeyType_Image);
            for (int c = 0; c < count; ++c) {
                ChannelTrack* track = prop ? prop->channel(c) : nullptr;
                if (!track || !track->deserialize(aIn)) {
                    return false;
                }
            }
            if (!aIn.endBlock()) {
                return false;
            }
        }
    } else {
        migrateImageOffsets();
    }

    // check block end
    if (!aIn.endBlock()) {
        return false;
    }
    aIn.popLogScope();

    return aIn.checkStream();
}

void TimeLine::migrateLegacyKeyMapsToTracks() {
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        const auto type = static_cast<TimeKeyType>(i);
        if (!usesChannelTracks(type)) {
            continue;
        }

        // Rotate is the first migrated type. Its legacy key is a bundle whose
        // only value is the angle; every other field is the per-component easing
        // and the proportional state, both of which a ChannelKey carries itself.
        PropertyTrack* prop = property(type);
        ChannelTrack* track = prop ? prop->channel(0) : nullptr;
        if (!track) {
            continue;
        }

        if (type == TimeKeyType_Rotate || type == TimeKeyType_Opacity || type == TimeKeyType_Depth) {
            for (auto itr = mMap[type].begin(); itr != mMap[type].end(); ++itr) {
                TimeKey* oldKey = itr.value();
                if (!oldKey) {
                    continue;
                }
                const float value = type == TimeKeyType_Rotate
                    ? static_cast<RotateKey*>(oldKey)->rotate()
                    : type == TimeKeyType_Depth
                        ? static_cast<DepthKey*>(oldKey)->depth()
                        : static_cast<OpacityKey*>(oldKey)->opacity();
                const util::Easing::Param& easing = type == TimeKeyType_Rotate
                    ? static_cast<RotateKey*>(oldKey)->data().easing(0)
                    : type == TimeKeyType_Depth
                        ? static_cast<DepthKey*>(oldKey)->data().easing(0)
                        : static_cast<OpacityKey*>(oldKey)->data().easing(0);

                auto* key = new ChannelKey(type, 0);
                key->setFrame(itr.key());
                key->setValue(value);
                key->easing() = easing;
                if (oldKey->isProportional()) {
                    key->setProportionalState(*oldKey->proportionalState());
                }
                key->setId(oldKey->id());
                if (!track->adoptKey(key)) {
                    delete key;
                }
                delete oldKey;
            }
            mMap[type].clear();

            TimeKey* base = mBaseValues[type].data();
            if (base) {
                const float value = type == TimeKeyType_Rotate
                    ? static_cast<RotateKey*>(base)->rotate()
                    : type == TimeKeyType_Depth
                        ? static_cast<DepthKey*>(base)->depth()
                        : static_cast<OpacityKey*>(base)->opacity();
                setChannelBaseValue(type, 0, value);
                mBaseValues[type].reset();
            }
        } else if (type == TimeKeyType_Scale) {
            // Two bundle components become two channel maps. The old key's one
            // id cannot name both, so each channel key gets a fresh id; the
            // proportional bounds are remapped per component afterwards.
            QHash<int, std::array<int, 2>> newIds;
            for (auto itr = mMap[type].begin(); itr != mMap[type].end(); ++itr) {
                auto* oldKey = static_cast<ScaleKey*>(itr.value());
                if (!oldKey) {
                    continue;
                }
                std::array<int, 2> ids {};
                for (int c = 0; c < 2; ++c) {
                    ChannelTrack* channel = prop->channel(c);
                    if (!channel) {
                        continue;
                    }
                    auto* key = new ChannelKey(type, c);
                    key->setFrame(itr.key());
                    key->setValue(c == 0 ? oldKey->scale().x() : oldKey->scale().y());
                    key->easing() = oldKey->data().easing(c);
                    if (oldKey->isProportional()) {
                        key->setProportionalState(*oldKey->proportionalState());
                    }
                    ids[c] = key->id();
                    if (!channel->adoptKey(key)) {
                        delete key;
                    }
                }
                newIds.insert(oldKey->id(), ids);
                delete oldKey;
            }
            mMap[type].clear();

            // Re-point each component's proportional bounds at that component's
            // own new key.
            for (int c = 0; c < 2; ++c) {
                ChannelTrack* channel = prop->channel(c);
                if (!channel) {
                    continue;
                }
                for (auto itr = channel->keys().begin(); itr != channel->keys().end(); ++itr) {
                    auto* key = static_cast<ChannelKey*>(itr.value());
                    if (!key || !key->isProportional()) {
                        continue;
                    }
                    TimeKey::Proportional state = *key->proportionalState();
                    const auto left = newIds.constFind(state.leftId);
                    const auto right = newIds.constFind(state.rightId);
                    if (left != newIds.constEnd()) {
                        state.leftId = (*left)[c];
                    }
                    if (right != newIds.constEnd()) {
                        state.rightId = (*right)[c];
                    }
                    key->setProportionalState(state);
                }
            }

            TimeKey* base = mBaseValues[type].data();
            if (base) {
                auto* scale = static_cast<ScaleKey*>(base);
                setChannelBaseValue(type, 0, scale->scale().x());
                setChannelBaseValue(type, 1, scale->scale().y());
                mBaseValues[type].reset();
            }
        } else if (type == TimeKeyType_Move) {
            QHash<int, std::array<int, 4>> newIds;
            for (auto itr = mMap[type].begin(); itr != mMap[type].end(); ++itr) {
                auto* oldKey = static_cast<MoveKey*>(itr.value());
                if (!oldKey) {
                    continue;
                }
                const QVector2D pos = oldKey->pos();
                const QVector2D centroid = oldKey->centroid();
                const float values[4] = { pos.x(), pos.y(), centroid.x(), centroid.y() };
                std::array<int, 4> ids {};
                for (int c = 0; c < 4; ++c) {
                    ChannelTrack* channel = prop->channel(c);
                    if (!channel) {
                        continue;
                    }
                    auto* key = new ChannelKey(type, c);
                    key->setFrame(itr.key());
                    key->setValue(values[c]);
                    key->easing() = oldKey->data().easing(c < 2 ? c : c - 2);
                    if (oldKey->isProportional()) {
                        key->setProportionalState(*oldKey->proportionalState());
                    }
                    ids[c] = key->id();
                    if (!channel->adoptKey(key)) {
                        delete key;
                    }
                }
                newIds.insert(oldKey->id(), ids);

                auto state = std::make_unique<MoveStateKey>();
                state->setSpline(oldKey->data().spline());
                prop->state().insertKey(itr.key(), std::move(state));
                delete oldKey;
            }
            mMap[type].clear();

            for (int c = 0; c < 4; ++c) {
                ChannelTrack* channel = prop->channel(c);
                if (!channel) {
                    continue;
                }
                for (auto itr = channel->keys().begin(); itr != channel->keys().end(); ++itr) {
                    auto* key = static_cast<ChannelKey*>(itr.value());
                    if (!key || !key->isProportional()) {
                        continue;
                    }
                    TimeKey::Proportional state = *key->proportionalState();
                    const auto left = newIds.constFind(state.leftId);
                    const auto right = newIds.constFind(state.rightId);
                    if (left != newIds.constEnd()) {
                        state.leftId = (*left)[c];
                    }
                    if (right != newIds.constEnd()) {
                        state.rightId = (*right)[c];
                    }
                    key->setProportionalState(state);
                }
            }

            TimeKey* base = mBaseValues[type].data();
            if (base) {
                auto* move = static_cast<MoveKey*>(base);
                setChannelBaseValue(type, 0, move->pos().x());
                setChannelBaseValue(type, 1, move->pos().y());
                setChannelBaseValue(type, 2, move->centroid().x());
                setChannelBaseValue(type, 3, move->centroid().y());
                auto state = std::make_unique<MoveStateKey>();
                state->setSpline(move->data().spline());
                prop->state().setBaseState(std::move(state));
                mBaseValues[type].reset();
            }
        } else if (type == TimeKeyType_Blur) {
            QHash<int, std::array<int, 3>> newIds;
            for (auto itr = mMap[type].begin(); itr != mMap[type].end(); ++itr) {
                auto* oldKey = static_cast<BlurKey*>(itr.value());
                if (!oldKey) {
                    continue;
                }
                const float values[3] = { oldKey->blurX(), oldKey->blurY(), oldKey->angleDeg() };
                std::array<int, 3> ids {};
                for (int c = 0; c < 3; ++c) {
                    ChannelTrack* channel = prop->channel(c);
                    if (!channel) {
                        continue;
                    }
                    auto* key = new ChannelKey(type, c);
                    key->setFrame(itr.key());
                    key->setValue(values[c]);
                    key->easing() = oldKey->data().easing(c);
                    if (oldKey->isProportional()) {
                        key->setProportionalState(*oldKey->proportionalState());
                    }
                    ids[c] = key->id();
                    if (!channel->adoptKey(key)) {
                        delete key;
                    }
                }
                newIds.insert(oldKey->id(), ids);
                auto state = std::make_unique<BlurStateKey>();
                state->setDirectional(oldKey->isDirectional());
                prop->state().insertKey(itr.key(), std::move(state));
                delete oldKey;
            }
            mMap[type].clear();

            for (int c = 0; c < 3; ++c) {
                ChannelTrack* channel = prop->channel(c);
                if (!channel) {
                    continue;
                }
                for (auto itr = channel->keys().begin(); itr != channel->keys().end(); ++itr) {
                    auto* key = static_cast<ChannelKey*>(itr.value());
                    if (!key || !key->isProportional()) {
                        continue;
                    }
                    TimeKey::Proportional state = *key->proportionalState();
                    const auto left = newIds.constFind(state.leftId);
                    const auto right = newIds.constFind(state.rightId);
                    if (left != newIds.constEnd()) {
                        state.leftId = (*left)[c];
                    }
                    if (right != newIds.constEnd()) {
                        state.rightId = (*right)[c];
                    }
                    key->setProportionalState(state);
                }
            }

            TimeKey* base = mBaseValues[type].data();
            if (base) {
                auto* blur = static_cast<BlurKey*>(base);
                setChannelBaseValue(type, 0, blur->blurX());
                setChannelBaseValue(type, 1, blur->blurY());
                setChannelBaseValue(type, 2, blur->angleDeg());
                auto state = std::make_unique<BlurStateKey>();
                state->setDirectional(blur->isDirectional());
                prop->state().setBaseState(std::move(state));
                mBaseValues[type].reset();
            }
        } else if (type == TimeKeyType_HSV) {
            QHash<int, std::array<int, 3>> newIds;
            for (auto itr = mMap[type].begin(); itr != mMap[type].end(); ++itr) {
                auto* oldKey = static_cast<HSVKey*>(itr.value());
                if (!oldKey) {
                    continue;
                }
                const QList<int>& values = oldKey->hsv();
                std::array<int, 3> ids {};
                for (int c = 0; c < 3; ++c) {
                    ChannelTrack* channel = prop->channel(c);
                    if (!channel) {
                        continue;
                    }
                    auto* key = new ChannelKey(type, c);
                    key->setFrame(itr.key());
                    key->setValue(c < values.size() ? float(values[c]) : 0.0f);
                    key->easing() = oldKey->data().easing(c);
                    if (oldKey->isProportional()) {
                        key->setProportionalState(*oldKey->proportionalState());
                    }
                    ids[c] = key->id();
                    if (!channel->adoptKey(key)) {
                        delete key;
                    }
                }
                newIds.insert(oldKey->id(), ids);
                auto state = std::make_unique<HSVStateKey>();
                state->setAbsolute(values.size() > 3 && values[3] != 0);
                prop->state().insertKey(itr.key(), std::move(state));
                delete oldKey;
            }
            mMap[type].clear();

            for (int c = 0; c < 3; ++c) {
                ChannelTrack* channel = prop->channel(c);
                if (!channel) {
                    continue;
                }
                for (auto itr = channel->keys().begin(); itr != channel->keys().end(); ++itr) {
                    auto* key = static_cast<ChannelKey*>(itr.value());
                    if (!key || !key->isProportional()) {
                        continue;
                    }
                    TimeKey::Proportional state = *key->proportionalState();
                    const auto left = newIds.constFind(state.leftId);
                    const auto right = newIds.constFind(state.rightId);
                    if (left != newIds.constEnd()) {
                        state.leftId = (*left)[c];
                    }
                    if (right != newIds.constEnd()) {
                        state.rightId = (*right)[c];
                    }
                    key->setProportionalState(state);
                }
            }

            TimeKey* base = mBaseValues[type].data();
            if (base) {
                auto* hsv = static_cast<HSVKey*>(base);
                const QList<int>& values = hsv->hsv();
                for (int c = 0; c < 3 && c < values.size(); ++c) {
                    setChannelBaseValue(type, c, float(values[c]));
                }
                auto state = std::make_unique<HSVStateKey>();
                state->setAbsolute(values.size() > 3 && values[3] != 0);
                prop->state().setBaseState(std::move(state));
                mBaseValues[type].reset();
            }
        } else if (type == TimeKeyType_Wave) {
            QHash<int, std::array<int, 5>> newIds;
            for (auto itr = mMap[type].begin(); itr != mMap[type].end(); ++itr) {
                auto* oldKey = static_cast<WaveKey*>(itr.value());
                if (!oldKey) {
                    continue;
                }
                const WaveKey::Data& d = oldKey->data();
                const float values[5] = {
                    d.strength(), d.wavelength(), d.angleDeg(), d.phase(), d.mix()
                };
                std::array<int, 5> ids {};
                for (int c = 0; c < 5; ++c) {
                    ChannelTrack* channel = prop->channel(c);
                    if (!channel) {
                        continue;
                    }
                    auto* key = new ChannelKey(type, c);
                    key->setFrame(itr.key());
                    key->setValue(values[c]);
                    key->easing() = d.easing(c);
                    if (oldKey->isProportional()) {
                        key->setProportionalState(*oldKey->proportionalState());
                    }
                    ids[c] = key->id();
                    if (!channel->adoptKey(key)) {
                        delete key;
                    }
                }
                newIds.insert(oldKey->id(), ids);
                auto state = std::make_unique<WaveStateKey>();
                state->setSpeed(d.speed());
                state->setCrest(d.crest());
                state->setDualAngleDeg(d.dualAngleDeg());
                state->setDualScale(d.dualScale());
                state->setTimeOffset(d.timeOffset());
                state->setDeform(d.deform());
                prop->state().insertKey(itr.key(), std::move(state));
                delete oldKey;
            }
            mMap[type].clear();

            for (int c = 0; c < 5; ++c) {
                ChannelTrack* channel = prop->channel(c);
                if (!channel) {
                    continue;
                }
                for (auto itr = channel->keys().begin(); itr != channel->keys().end(); ++itr) {
                    auto* key = static_cast<ChannelKey*>(itr.value());
                    if (!key || !key->isProportional()) {
                        continue;
                    }
                    TimeKey::Proportional state = *key->proportionalState();
                    const auto left = newIds.constFind(state.leftId);
                    const auto right = newIds.constFind(state.rightId);
                    if (left != newIds.constEnd()) {
                        state.leftId = (*left)[c];
                    }
                    if (right != newIds.constEnd()) {
                        state.rightId = (*right)[c];
                    }
                    key->setProportionalState(state);
                }
            }

            TimeKey* base = mBaseValues[type].data();
            if (base) {
                auto* wave = static_cast<WaveKey*>(base);
                const WaveKey::Data& d = wave->data();
                setChannelBaseValue(type, 0, d.strength());
                setChannelBaseValue(type, 1, d.wavelength());
                setChannelBaseValue(type, 2, d.angleDeg());
                setChannelBaseValue(type, 3, d.phase());
                setChannelBaseValue(type, 4, d.mix());
                auto state = std::make_unique<WaveStateKey>();
                state->setSpeed(d.speed());
                state->setCrest(d.crest());
                state->setDualAngleDeg(d.dualAngleDeg());
                state->setDualScale(d.dualScale());
                state->setTimeOffset(d.timeOffset());
                state->setDeform(d.deform());
                prop->state().setBaseState(std::move(state));
                mBaseValues[type].reset();
            }
        }
    }
}

bool TimeLine::deserializeTimeKey(Deserializer& aIn, TimeKeyType aType, int aIndex) {
    TimeKey* key = nullptr;
    if (aType == TimeKeyType_Move) {
        key = new MoveKey();
    } else if (aType == TimeKeyType_Rotate) {
        key = new RotateKey();
    } else if (aType == TimeKeyType_Scale) {
        key = new ScaleKey();
    } else if (aType == TimeKeyType_Depth) {
        key = new DepthKey();
    } else if (aType == TimeKeyType_Opacity) {
        key = new OpacityKey();
    } else if (aType == TimeKeyType_HSV) {
        key = new HSVKey();
    } else if (aType == TimeKeyType_Blur) {
        key = new BlurKey();
    } else if (aType == TimeKeyType_Wave) {
        key = new WaveKey();
    } else if (aType == TimeKeyType_Mask) {
        key = new MaskKey();
    } else if (aType == TimeKeyType_Bone) {
        key = new BoneKey();
    } else if (aType == TimeKeyType_Pose) {
        key = new PoseKey();
    } else if (aType == TimeKeyType_Mesh) {
        key = new MeshKey();
    } else if (aType == TimeKeyType_FFD) {
        key = new FFDKey();
    } else if (aType == TimeKeyType_Image) {
        key = new ImageKey();
    }

    // reference id
    if (!aIn.bindIDData(key)) {
        return aIn.errored("failed to bind reference id");
    }

    // set frame
    key->setFrame(aIndex);

    // timekey value
    if (!key->deserialize(aIn)) {
        delete key;
        return false;
    }

    // stable id and, when proportional, its bound ids and per-axis weights
    // (0.16; older files carry neither and keep the constructor's fresh id)
    if (aIn.version().majorVersion() == 0 && aIn.version().minorVersion() >= 16) {
        int id = 0;
        aIn.read(id);
        bool proportional = false;
        aIn.read(proportional);
        TimeKey::Proportional proportionalState;
        if (proportional) {
            aIn.read(proportionalState.leftId);
            aIn.read(proportionalState.rightId);
            aIn.read(proportionalState.timeValid);
            aIn.read(proportionalState.timeWeight);
            // 0.16-0.17 stored three component weights; 0.18 (the Wave filter)
            // stores kMaxProportionalComponents. Reading the wrong count would
            // silently shift the rest of the stream by two pairs.
            const int storedComponents = (aIn.version().minorVersion() >= 18)
                ? TimeKey::kMaxProportionalComponents : 3;
            for (int c = 0; c < storedComponents; ++c) {
                aIn.read(proportionalState.valueValid[c]);
                aIn.read(proportionalState.valueWeight[c]);
            }
        }
        if (id > 0) {
            TimeKey::reserveId(id);
            key->setId(id);
        }
        if (proportional) {
            key->setProportionalState(proportionalState);
        }
    }

    // Image is serialized in the legacy bundle block for format compatibility,
    // but it is a StateTrack in memory. Route it through adoptKey or a load
    // would put it in the empty legacy map and it would vanish from the property.
    if (aType == TimeKeyType_Image) {
        if (!adoptKey(aType, aIndex, key)) {
            delete key;
            return aIn.errored("failed to adopt image state key");
        }
    } else if (aIndex == TimeLine::kBaseValueIndex) {
        mBaseValues[aType].reset(key);
    } else {
        mMap[aType].insert(aIndex, key);
    }

    // child count
    int childCount = 0;
    aIn.read(childCount);

    // references to children
    for (int ci = 0; ci < childCount; ++ci) {
        auto solver = [=](void* aPtr) {
            TimeKey* child = static_cast<TimeKey*>(aPtr);
            key->children().pushBack(child);
        };
        if (key->type() == TimeKeyType_HSV || key->type() == TimeKeyType_Wave) {
            return true;
        }
        if (!aIn.orderIDData(solver)) {
            return aIn.errored("invalid child reference id");
        }
    }
    return true;
}

} // namespace core
