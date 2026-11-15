#ifndef CORE_TIMELINE_H
#define CORE_TIMELINE_H

#include <array>
#include <memory>
#include <QMap>
#include <QPair>
#include <QScopedPointer>
#include <QVector>
#include "cmnd/Vector.h"
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
    // The default key's frame marker; see TimeKey::kDefaultKeyIndex (moved
    // off -1 so negative frames are ordinary frames).
    enum { kDefaultKeyIndex = TimeKey::kDefaultKeyIndex };

    typedef QMap<int, TimeKey*> MapType;

    static QString getTimeKeyName(TimeKeyType aType);
    // The name a UI should print. Serialization keeps the compact token ("Opa");
    // a label reads "Opacity".
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

    void grabDefaultKey(TimeKeyType aType, TimeKey* aKey);
    TimeKey* defaultKey(TimeKeyType aType);
    const TimeKey* defaultKey(TimeKeyType aType) const;

    TimeKeyExpans& current() { return *mCurrent; }
    const TimeKeyExpans& current() const { return *mCurrent; }

    TimeKeyExpans& working() { return *mWorking; }
    const TimeKeyExpans& working() const { return *mWorking; }

    bool move(TimeKeyType aType, int aFrom, int aTo);
    // Moves several keys of one type in one step. aMoves are (fromFrame,toFrame).
    // Fails (and mutates nothing) when a source is missing, two moves share a
    // source or a destination, or a destination is occupied by a key that is
    // not itself moving. Used by the frame-projection command so selected and
    // projected keys can swap/chain without order-dependent move() failures.
    bool moveKeys(TimeKeyType aType, const QVector<QPair<int, int>>& aMoves);
    bool overwriteMove(TimeKeyType aType, int aFrom, int aTo);
    cmnd::Base* createPusher(TimeKeyType aType, int aFrame, TimeKey* aTimeKey);
    cmnd::Base* createRemover(TimeKeyType aType, int aFrame, bool aOptional = false);

    bool serialize(Serializer& aOut) const;
    bool deserialize(Deserializer& aIn);

private:
    void clear();
    int serializeTypeCount() const;
    void pushRemoveCommands(TimeKeyType aType, int aFrame, cmnd::Vector& aCommands);
    bool serializeTimeKey(Serializer& aOut, const TimeKey& aTimeKey) const;
    bool deserializeTimeKey(Deserializer& aIn, TimeKeyType aType, int aIndex);

    std::array<MapType, TimeKeyType_TERM> mMap;
    QVector<FilterEntry> mFilters;
    QScopedPointer<TimeKeyExpans> mCurrent;
    QScopedPointer<TimeKeyExpans> mWorking;
    std::array<QScopedPointer<TimeKey>, TimeKeyType_TERM> mDefaultKeys;
    std::array<std::array<std::unique_ptr<FilterMask>, FilterMaskChannel_TERM>, TimeKeyType_TERM> mFilterMasks;
};

} // namespace core

#endif // CORE_TIMELINE_H
