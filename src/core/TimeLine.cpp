#include <QSet>
#include "XC.h"
#include "cmnd/BasicCommands.h"
#include "cmnd/Scalable.h"
#include "core/TimeLine.h"
#include "core/TimeKeyExpans.h"
#include "core/DepthKey.h"
#include "core/BlurKey.h"
#include "core/WaveKey.h"
#include "core/MaskKey.h"
#include "core/Project.h"

namespace {

constexpr int kMaxLengthOfTimeKeyName = 8;
constexpr std::array<const char*, core::TimeKeyType_TERM> kTimeKeyNames = {
    "Move", "Rotate", "Scale", "Depth", "Opa", "Bone", "Pose", "Mesh", "FFD", "Image", "HSV", "Blur",
    "Wave", "Mask"};

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
    core::TimeKeyType_Opa,
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
    if (aType == TimeKeyType_Opa)
        return QStringLiteral("Opacity");
    if (aType == TimeKeyType_Move)
        return QStringLiteral("Position");
    return getTimeKeyName(aType);
}

TimeKeyType TimeLine::getTimeKeyType(const QString& aName) {
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

TimeLine::TimeLine(): mMap(), mCurrent(new TimeKeyExpans()), mWorking(new TimeKeyExpans()), mDefaultKeys() {}

TimeLine::~TimeLine() { clear(); }

bool TimeLine::hasTimeKey(int aIndex) const {
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (mMap[i].contains(aIndex))
            return true;
    }
    return false;
}

bool TimeLine::hasTimeKey(TimeKeyType aType, int aIndex) const { return mMap.at(aType).contains(aIndex); }

bool TimeLine::isEmpty() const {
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (!mMap[i].isEmpty())
            return false;
    }
    return true;
}

bool TimeLine::isEmpty(TimeKeyType aType) const { return mMap.at(aType).isEmpty(); }

int TimeLine::validTypeCount() const {
    int count = 0;
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (!mMap[i].isEmpty())
            ++count;
    }
    return count;
}

const TimeLine::MapType& TimeLine::map(TimeKeyType aType) const { return mMap.at(aType); }

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
        if (!mMap.at(type).isEmpty() && !seen.contains(type)) {
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

bool TimeLine::filterEnabled(TimeKeyType aType) const {
    if (!isFilterType(aType))
        return false;
    for (const FilterEntry& entry : mFilters) {
        if (entry.type == aType)
            return entry.enabled;
    }
    // A lane that exists without a stored entry is a filter the user has not
    // touched from the stack yet; it is enabled.
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
    if (mMap.at(aType).contains(aIndex)) {
        return mMap[aType][aIndex];
    }
    return NULL;
}

const TimeKey* TimeLine::timeKey(TimeKeyType aType, int aIndex) const {
    if (mMap.at(aType).contains(aIndex)) {
        return mMap[aType][aIndex];
    }
    return NULL;
}

void TimeLine::grabDefaultKey(TimeKeyType aType, TimeKey* aKey) {
    if (aKey) {
        aKey->setFrame(kDefaultKeyIndex);
    }
    mDefaultKeys.at(aType).reset(aKey);
}

TimeKey* TimeLine::defaultKey(TimeKeyType aType) { return mDefaultKeys.at(aType).data(); }

const TimeKey* TimeLine::defaultKey(TimeKeyType aType) const { return mDefaultKeys.at(aType).data(); }

void TimeLine::clear() {
    mFilters.clear();
    for (auto& lane : mFilterMasks) {
        for (auto& mask : lane) {
            mask.reset();
        }
    }
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        mDefaultKeys[i].reset();

        QList<TimeKey*> values = mMap[i].values();
        qDeleteAll(values.begin(), values.end());
        mMap[i].clear();
    }
    mCurrent.reset(new TimeKeyExpans());
}

bool TimeLine::move(TimeKeyType aType, int aFrom, int aTo) {
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

bool TimeLine::moveKeys(TimeKeyType aType, const QVector<QPair<int, int>>& aMoves) {
    TimeLine::MapType& map = mMap.at(aType);

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
    TimeLine::MapType& map = mMap.at(aType);

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
    if (aFrame == kDefaultKeyIndex) {
        if (mDefaultKeys[aType])
            return false;
        aTimeKey->setFrame(aFrame);
        mDefaultKeys[aType].reset(aTimeKey);
        return true;
    }
    if (mMap[aType].contains(aFrame))
        return false;
    aTimeKey->setFrame(aFrame);
    mMap[aType].insert(aFrame, aTimeKey);
    return true;
}

cmnd::Base* TimeLine::createPusher(TimeKeyType aType, int aFrame, TimeKey* aTimeKey) {
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

cmnd::Base* TimeLine::createRemover(TimeKeyType aType, int aFrame, bool aOptional) {
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

int TimeLine::serializeTypeCount() const {
    int count = 0;
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        if (mDefaultKeys[i] || !mMap[i].isEmpty())
            ++count;
    }
    return count;
}

bool TimeLine::serialize(Serializer& aOut) const {
    static const std::array<uint8, 8> kSignature = {'T', 'i', 'm', 'e', 'L', 'i', 'n', 'e'};
    static const std::array<uint8, 8> kMapSignature = {'T', 'i', 'm', 'e', 'M', 'a', 'p', '_'};

    // signature
    auto pos = aOut.beginBlock(kSignature);

    // non-empty type count (including default keys)
    aOut.write(serializeTypeCount());

    // each map
    for (int i = 0; i < TimeKeyType_TERM; ++i) {
        const MapType& map = mMap[i];
        auto defaultKey = mDefaultKeys[i].data();

        if (!defaultKey && map.isEmpty())
            continue;

        // signature
        auto mapPos = aOut.beginBlock(kMapSignature);

        // type name
        aOut.write(getTimeKeyName(static_cast<TimeKeyType>(i)));

        // default key is exists
        aOut.write(static_cast<bool>(defaultKey));

        // defautltKey
        if (defaultKey) {
            if (!serializeTimeKey(aOut, *defaultKey)) {
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

    // non empty type count (including default keys)
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
        if (typeIndex >= TimeKeyType_TERM)
            continue;

        // default key is exists
        bool hasDefaultKey = false;
        aIn.read(hasDefaultKey);

        // default key
        if (hasDefaultKey) {
            if (!deserializeTimeKey(aIn, (TimeKeyType)typeIndex, TimeLine::kDefaultKeyIndex)) {
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
            if (!deserializeTimeKey(aIn, (TimeKeyType)typeIndex, keyIndex)) {
                return false;
            }
        }

        // check block end
        if (!aIn.endBlock()) {
            return false;
        }
        aIn.popLogScope();
    }

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

    // check block end
    if (!aIn.endBlock()) {
        return false;
    }
    aIn.popLogScope();

    return aIn.checkStream();
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
    } else if (aType == TimeKeyType_Opa) {
        key = new OpaKey();
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

    if (aIndex == TimeLine::kDefaultKeyIndex) {
        mDefaultKeys[aType].reset(key);
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
