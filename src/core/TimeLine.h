#ifndef CORE_TIMELINE_H
#define CORE_TIMELINE_H

#include <array>
#include <memory>
#include <QMap>
#include <QPair>
#include <QSize>
#include <QScopedPointer>
#include <QVector>
#include "cmnd/Vector.h"
#include "core/ChannelKey.h"
#include "core/ChannelTrack.h"
#include "core/PropertyTrack.h"
#include "core/TimeKey.h"
#include "core/TimeKeyType.h"
#include "core/FilterMask.h"
#include "core/Serializer.h"
#include "core/Deserializer.h"
namespace core {
class Project;
}
namespace core {
class ObjectNode;
}
namespace core {
class TimeKeyExpans;
}


namespace core {

class TimeLine {
public:
    // The base value's frame marker; see TimeKey::kBaseValueIndex (moved
    // off -1 so negative frames are ordinary frames).
    enum { kBaseValueIndex = TimeKey::kBaseValueIndex };

    typedef QMap<int, TimeKey*> MapType;

    // A property is being migrated from the legacy bundle key map to per-channel
    // tracks one type at a time. The switch is explicit, so generic code can ask
    // which storage a type uses instead of guessing from an empty map.
    enum class StorageModel { LegacyKeys, ChannelTracks };
    static constexpr StorageModel storageModel(TimeKeyType aType) {
        return (aType == TimeKeyType_Opacity || aType == TimeKeyType_Depth || aType == TimeKeyType_Rotate || aType == TimeKeyType_Scale || aType == TimeKeyType_Move || aType == TimeKeyType_Blur || aType == TimeKeyType_HSV || aType == TimeKeyType_Wave) ? StorageModel::ChannelTracks : StorageModel::LegacyKeys;
    }
    static constexpr bool usesChannelTracks(TimeKeyType aType) {
        return storageModel(aType) == StorageModel::ChannelTracks;
    }

    static QString getTimeKeyName(TimeKeyType aType);
    // The name a UI should print. Serialization uses the property's own name
    // ("Opacity"), with getTimeKeyType also reading the retired token "Opa".
    static QString getTimeKeyDisplayName(TimeKeyType aType);
    static TimeKeyType getTimeKeyType(const QString& aName);
    static TimeKeyType getTimeKeyTypeInOrderOfOperations(int aIndex);

    TimeLine();
    ~TimeLine();

    bool hasTimeKey(int aIndex) const;
    bool hasTimeKey(TimeKeyType aType, int aIndex) const;
    bool isEmpty() const;
    bool isEmpty(TimeKeyType aType) const;
    int validTypeCount() const;

    const MapType& map(TimeKeyType aType) const;

    // --- per-channel tracks (the migrated storage) ---
    //
    // A property's channel map is the SAME MapType the legacy path uses, so a
    // ChannelKey is addressed, selected, focused and moved by the existing key
    // machinery. The map() overload is the legacy bundle map; channelMap() is the
    // per-channel one. For a legacy type the channel API is empty/null.
    PropertyTrack* property(TimeKeyType aType);
    const PropertyTrack* property(TimeKeyType aType) const;
    ChannelTrack* channelTrack(TimeKeyType aType, int aComponent);
    const ChannelTrack* channelTrack(TimeKeyType aType, int aComponent) const;
    MapType& channelMap(TimeKeyType aType, int aComponent);
    const MapType& channelMap(TimeKeyType aType, int aComponent) const;
    // The property's discrete state keys, in the same map currency.
    MapType& stateMap(TimeKeyType aType);
    const MapType& stateMap(TimeKeyType aType) const;

    ChannelKey* channelKey(TimeKeyType aType, int aComponent, int aFrame);
    const ChannelKey* channelKey(TimeKeyType aType, int aComponent, int aFrame) const;
    bool hasChannelKey(TimeKeyType aType, int aComponent, int aFrame) const;
    bool hasChannelKeys(TimeKeyType aType, int aComponent) const;
    float channelBaseValue(TimeKeyType aType, int aComponent) const;
    void setChannelBaseValue(TimeKeyType aType, int aComponent, float aValue);
    // True when any channel of the property has a key at aFrame.
    bool hasAnyChannelKey(TimeKeyType aType, int aFrame) const;
    // --- property-key grouping (AUTHORED, 0.24) ------------------------------
    //
    // Linked is the default. A frame is a LINKED property key when every
    // graphable channel has a key there and every one of those keys is linked.
    // Any other keyframe is split: it belongs to one channel (or only some
    // channels have keys), and edits to it must not write its siblings. The bit
    // lives on each ChannelKey because it moves with the key and is serialized;
    // there is still no property-key object. "Separate Channels"/"Link Channels"
    // set the bits at one frame in one undo step.
    struct ChannelLane {
        int component = 0;
        bool grouped = false;
    };
    QVector<ChannelLane> channelLanes(TimeKeyType aType) const;
    int channelLaneCount(TimeKeyType aType) const;
    // Every graphable channel has a key at aFrame, all linked.
    bool isFrameLinked(TimeKeyType aType, int aFrame) const;
    // Every graphable channel has a key at aFrame (so a property key can form).
    bool canLinkFrame(TimeKeyType aType, int aFrame) const;
    // Any keyframe that is not a linked property key (split or partial).
    bool hasSplitChannelKeys(TimeKeyType aType) const;
    // Every keyframe is a linked property key. This is the one-lane view.
    bool channelsGrouped(TimeKeyType aType) const;
    // What a field/value edit at aFrame must write: the whole property while the
    // frame is linked, or while the property has no split frame yet (the linked
    // default). Once any frame is split, an unlinked frame edits one channel.
    bool frameEditIsLinked(TimeKeyType aType, int aFrame) const;
    // Sets/clears the linked bit on every channel key at aFrame. Linking requires
    // canLinkFrame(); separating only requires a key at the frame. The caller owns
    // the undo command and notification.
    bool setFrameLinked(TimeKeyType aType, int aFrame, bool aLinked);

    // True when the Image offset block needs to be written (any key, or a
    // non-zero base). A file with no authored offsets stays compatible with a
    // reader that did not know the block.
    bool imageOffsetsAuthored() const;

    // The number of non-empty key lanes: one per keyed channel for a track-backed
    // property, one per non-empty legacy map otherwise. This is what the
    // timeline's row height and lane pass read.
    int validChannelCount() const;

    // Insert a key with no command, because the CALLER is building one and owns the undo
    // record itself (a node duplicate, a paste into a fresh timeline). Deserialization
    // inserts the same way. Ownership passes to this timeline on success; on refusal
    // (null, or the frame is already taken) the caller keeps the key and must delete it,
    // so a caller bug cannot silently replace a key it does not own.
    bool adoptKey(TimeKeyType aType, int aFrame, TimeKey* aTimeKey);

    // --- the filter stack ---
    //
    // ORDER and ENABLED are stored per timeline; the filter VALUES remain the
    // existing HSV/Blur key maps, so animation, the graph and the timeline are
    // untouched. A stored entry may exist before its first key (adding a filter is
    // a stack edit, not a key edit); a filter whose keys exist but has no stored
    // entry is appended in canonical order, which is what makes a legacy project
    // and a key added from the timeline appear in the stack.
    const QVector<FilterEntry>& storedFilters() const { return mFilters; }
    QVector<FilterEntry> filters() const;

    // PAINTED FILTER MASKS (0.19): one field per filter LANE. They live here and
    // not in FilterEntry because the stack is value-replayed by its commands -
    // megabytes of pixels must never travel through one.
    FilterMask* filterMask(TimeKeyType aType, FilterMaskChannel aChannel);
    const FilterMask* filterMask(TimeKeyType aType, FilterMaskChannel aChannel) const;
    // allocates the field for aContentRect on first use (an existing field is
    // returned untouched, so a repaint cannot wipe a stroke)
    FilterMask& ensureFilterMask(TimeKeyType aType, FilterMaskChannel aChannel, const QRectF& aContentRect);
    void removeFilterMask(TimeKeyType aType, FilterMaskChannel aChannel);
    bool filterEnabled(TimeKeyType aType) const;
    bool hasFilter(TimeKeyType aType) const;
    // The raw stored vector, for cmnd::ModifiableAssign's value target. Callers
    // must write a NORMALIZED vector (filters() plus their edit) - the stack
    // command owns the value, not the normalization.
    QVector<FilterEntry>& filterEntriesRef() { return mFilters; }
    void setFilterEntries(const QVector<FilterEntry>& aEntries);

    TimeKey* timeKey(TimeKeyType aType, int aIndex);
    const TimeKey* timeKey(TimeKeyType aType, int aIndex) const;

    // The unkeyed value for a type: a TimeKey-typed data record whose frame is
    // kBaseValueIndex. It is not an authored position and never appears in a lane.
    void setBaseValue(TimeKeyType aType, TimeKey* aKey);
    TimeKey* baseValue(TimeKeyType aType);
    const TimeKey* baseValue(TimeKeyType aType) const;

    TimeKeyExpans& current() { return *mCurrent; }
    const TimeKeyExpans& current() const { return *mCurrent; }
    // The current area texture size, exposed without making every caller include
    // TimeKeyExpans (a header that drags Project.h and the image-IO headers with it).
    QSize currentAreaTextureSize() const;

    TimeKeyExpans& working() { return *mWorking; }
    const TimeKeyExpans& working() const { return *mWorking; }

    bool move(TimeKeyType aType, int aFrom, int aTo);
    // Moves several keys of one type in one step. aMoves are (fromFrame,toFrame).
    // Fails (and mutates nothing) when a source is missing, two moves share a
    // source or a destination, or a destination is occupied by a key that is
    // not itself moving. Used by the frame-projection command so selected and
    // projected keys can swap/chain without order-dependent move() failures.
    bool moveKeys(TimeKeyType aType, const QVector<QPair<int, int>>& aMoves);
    // One component's channel map (Image's offset channels are not a track-backed
    // property, so moveKeys() addresses the structural map).
    bool moveChannelKeys(TimeKeyType aType, int aComponent, const QVector<QPair<int, int>>& aMoves);
    // State keys have one map per property, like a channel.
    bool moveStateKeys(TimeKeyType aType, const QVector<QPair<int, int>>& aMoves);
    bool overwriteMove(TimeKeyType aType, int aFrom, int aTo);
    cmnd::Base* createPusher(TimeKeyType aType, int aFrame, TimeKey* aTimeKey);
    cmnd::Base* createRemover(TimeKeyType aType, int aFrame, bool aOptional = false);

    // The channel-map equivalents. Ownership of aChannelKey passes to the
    // command on success; the caller must already have pushed a
    // GrabNewObject for it (the same contract createPusher has).
    cmnd::Base* createChannelPusher(TimeKeyType aType, int aComponent, int aFrame, ChannelKey* aChannelKey);
    cmnd::Base* createChannelRemover(TimeKeyType aType, int aComponent, int aFrame, bool aOptional = false);
    cmnd::Base* createStateRemover(TimeKeyType aType, int aFrame, bool aOptional = false);
    cmnd::Base* createStatePusher(TimeKeyType aType, int aFrame, StateKey* aStateKey);

    bool serialize(Serializer& aOut) const;
    bool deserialize(Deserializer& aIn);

private:
    void clear();
    void initProperties();
    void migrateImageOffsets();
    void migrateLegacyKeyMapsToTracks();
    int serializeTypeCount() const;
    void pushRemoveCommands(TimeKeyType aType, int aFrame, cmnd::Vector& aCommands);
    void pushRemoveStateCommands(PropertyTrack& aProp, int aFrame, cmnd::Vector& aCommands);
    bool serializeTimeKey(Serializer& aOut, const TimeKey& aTimeKey) const;
    bool deserializeTimeKey(Deserializer& aIn, TimeKeyType aType, int aIndex);

    std::array<MapType, TimeKeyType_TERM> mMap;
    std::array<std::unique_ptr<PropertyTrack>, TimeKeyType_TERM> mProperties;
    QVector<FilterEntry> mFilters;
    QScopedPointer<TimeKeyExpans> mCurrent;
    QScopedPointer<TimeKeyExpans> mWorking;
    std::array<QScopedPointer<TimeKey>, TimeKeyType_TERM> mBaseValues;
    std::array<std::array<std::unique_ptr<FilterMask>, FilterMaskChannel_TERM>, TimeKeyType_TERM> mFilterMasks;
};

} // namespace core

#endif // CORE_TIMELINE_H
