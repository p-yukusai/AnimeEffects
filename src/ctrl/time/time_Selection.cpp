#include "ctrl/time/time_Selection.h"

#include <algorithm>

using namespace core;

namespace ctrl {
namespace time {

    //-------------------------------------------------------------------------------------------------
    bool Selection::clear() {
        // Nothing to clear is not a change: the set answers so, and does not
        // announce one.
        if (mItems.isEmpty())
            return false;
        mItems.clear();
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::contains(const TimeLineEvent::Target& aTarget) const {
        for (const Item& item : mItems) {
            if (sameKey(item.target, aTarget)) {
                return true;
            }
        }
        return false;
    }

    bool Selection::namesMarker(const Item& aItem, const TimeLineEvent::Target& aTarget, int aComponent) {
        if (!sameKey(aItem.target, aTarget))
            return false;
        // A key-level entry IS the whole key's mark set: every component of its key
        // is selected, so it covers this marker too.
        return aItem.component == kKeyLevel || aItem.component == aComponent;
    }

    bool Selection::containsMarker(const TimeLineEvent::Target& aTarget, int aComponent) const {
        for (const Item& item : mItems) {
            if (namesMarker(item, aTarget, aComponent)) {
                return true;
            }
        }
        return false;
    }

    QVector<int> Selection::componentsOf(const TimeLineEvent::Target& aTarget, int aStoredCount) const {
        QVector<int> result;
        if (aStoredCount <= 0)
            return result;
        const auto add = [&result](int aComponent) {
            if (!result.contains(aComponent))
                result.push_back(aComponent);
        };
        for (const Item& item : mItems) {
            if (!sameKey(item.target, aTarget))
                continue;
            if (item.component == kKeyLevel) {
                for (int component = 0; component < aStoredCount; ++component)
                    add(component);
            } else if (0 <= item.component && item.component < aStoredCount) {
                add(item.component);
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    bool Selection::sameKey(const TimeLineEvent::Target& aLhs, const TimeLineEvent::Target& aRhs) {
        if (aLhs.pos.type() != aRhs.pos.type() || aLhs.pos.index() != aRhs.pos.index())
            return false;
        if (aLhs.pos.type() != core::TimeKeyType_TERM
            && core::TimeLine::usesChannelTracks(aLhs.pos.type())) {
            // A track-backed key spans one maps per channel; the KEY is
            // (line, type, frame), not the channel map the marker names.
            return aLhs.pos.line() == aRhs.pos.line();
        }
        // the bundle map is unique per (line, type); address equality is exact
        return &aLhs.pos.map() == &aRhs.pos.map();
    }

    int Selection::entryComponent(const TimeLineEvent::Target& aTarget) {
        const TimeKeyType type = aTarget.pos.type();
        if (aTarget.pos.component() == TimeKeyPos::kStateComponent) {
            return TimeKeyPos::kStateComponent;
        }
        const TimeLine* line = aTarget.pos.line();
        if (line && aTarget.pos.component() >= 0) {
            if (type == TimeKeyType_Image) {
                return aTarget.pos.component();
            }
            if (TimeLine::usesChannelTracks(type)
                && !line->isFrameLinked(type, aTarget.pos.index())) {
                return aTarget.pos.component();
            }
        }
        return kKeyLevel;
    }

    bool Selection::sameEntries(const QVector<Item>& aLhs, const QVector<Item>& aRhs) {
        if (aLhs.size() != aRhs.size())
            return false;
        for (int i = 0; i < aLhs.size(); ++i) {
            const Item& lhs = aLhs[i];
            const Item& rhs = aRhs[i];
            // The stored key pointer is only compared, never dereferenced, exactly
            // like every other use of it here.
            if (lhs.component != rhs.component || lhs.key != rhs.key || !sameKey(lhs.target, rhs.target))
                return false;
        }
        return true;
    }

    bool Selection::retain(const std::function<bool(const Item&)>& aKeep) {
        QVector<Item> next;
        next.reserve(mItems.size());
        for (const Item& item : mItems) {
            if (aKeep(item))
                next.push_back(item);
        }
        if (sameEntries(mItems, next))
            return false;
        mItems = next;
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::set(const TimeLineEvent& aEvent) {
        // Built BESIDE the live set, so "did this change anything" is answerable
        // before the set is replaced: a re-selection of what is already selected is
        // not a change, and is not announced as one.
        QVector<Item> next;
        for (const TimeLineEvent::Target& src : aEvent.targets()) {
            const int component = entryComponent(src);
            bool duplicate = false;
            for (const Item& item : next) {
                if (item.component == component && sameKey(item.target, src)) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate)
                continue;
            TimeLineEvent::Target t = src;
            next.push_back(Item(t, t.pos.key(), component));
        }
        if (sameEntries(mItems, next))
            return false;
        mItems = next;
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::add(const TimeLineEvent& aEvent) {
        bool changed = false;
        for (const TimeLineEvent::Target& src : aEvent.targets()) {
            const int component = entryComponent(src);
            bool covered = false;
            for (const Item& item : mItems) {
                if (item.component == component && sameKey(item.target, src)) {
                    covered = true;
                    break;
                }
            }
            if (!covered) {
                TimeLineEvent::Target t = src;
                mItems.push_back(Item(t, t.pos.key(), component));
                changed = true;
            }
        }
        if (!changed)
            return false;
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::subtract(const TimeLineEvent& aEvent) {
        bool changed = false;
        for (const TimeLineEvent::Target& t : aEvent.targets()) {
            for (auto itr = mItems.begin(); itr != mItems.end();) {
                if (sameKey(itr->target, t)) {
                    itr = mItems.erase(itr);
                    changed = true;
                } else {
                    ++itr;
                }
            }
        }
        if (!changed)
            return false;
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::toggle(const TimeLineEvent::Target& aTarget) {
        // The timeline's toggle is a KEY's: it takes the key out - every entry that
        // names it, a marker entry included, since a key the user sees highlighted
        // is one thing there - or puts it in whole.
        bool removed = false;
        for (auto itr = mItems.begin(); itr != mItems.end();) {
            if (sameKey(itr->target, aTarget)) {
                itr = mItems.erase(itr);
                removed = true;
            } else {
                ++itr;
            }
        }
        if (!removed) {
            TimeLineEvent::Target t = aTarget;
            mItems.push_back(Item(t, t.pos.key()));
        }
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::setMarker(const TimeLineEvent::Target& aTarget, int aComponent) {
        TimeLineEvent::Target t = aTarget;
        QVector<Item> next;
        next.push_back(Item(t, t.pos.key(), aComponent == kKeyLevel ? kKeyLevel : aComponent));
        if (sameEntries(mItems, next))
            return false;
        mItems = next;
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::addMarker(const TimeLineEvent::Target& aTarget, int aComponent) {
        // The component a caller may hand in is kKeyLevel itself, and then the
        // question is "does anything name this key", not "does anything name this
        // component": a key already selected must not gain a second entry.
        const bool covered = (aComponent == kKeyLevel) ? contains(aTarget)
                                                       : containsMarker(aTarget, aComponent);
        if (covered)
            return false;
        TimeLineEvent::Target t = aTarget;
        mItems.push_back(Item(t, t.pos.key(), aComponent == kKeyLevel ? kKeyLevel : aComponent));
        rebuildFocus();
        notifyChanged();
        return true;
    }

    bool Selection::subtractMarker(const TimeLineEvent::Target& aTarget, int aComponent) {
        bool removed = false;
        for (auto itr = mItems.begin(); itr != mItems.end();) {
            const bool names = sameKey(itr->target, aTarget)
                               && (aComponent == kKeyLevel || itr->component == kKeyLevel
                                   || itr->component == aComponent);
            if (names) {
                itr = mItems.erase(itr);
                removed = true;
            } else {
                ++itr;
            }
        }
        if (!removed)
            return false;
        rebuildFocus();
        notifyChanged();
        return true;
    }

    void Selection::assign(TimeLineEvent& aOut) const {
        // ONE TARGET PER KEY. The event is the timeline's currency and it names a
        // key, while this set can hold one entry per component of one key: the
        // second entry for a key is not a second key, so it is not a second target -
        // a consumer that walked both would remove, copy or re-ease the same key
        // twice.
        for (int i = 0; i < mItems.size(); ++i) {
            bool alreadyNamed = false;
            for (int j = 0; j < i; ++j) {
                if (sameKey(mItems[j].target, mItems[i].target)) {
                    alreadyNamed = true;
                    break;
                }
            }
            if (alreadyNamed)
                continue;
            aOut.pushTarget(*mItems[i].target.node, mItems[i].target.pos);
        }
    }

    bool Selection::refresh() {
        QVector<Item> next;
        next.reserve(mItems.size());
        for (const Item& item : mItems) {
            const TimeLine::MapType& map = item.target.pos.map();
            // fast path: the recorded slot still holds the recorded key —
            // data-only edits (easing, HSV, ...) leave every entry valid
            auto slot = map.find(item.target.pos.index());
            if (slot != map.end() && slot.value() == item.key) {
                next.push_back(item);
                continue;
            }
            // stale index: the key moved since it was recorded. Follow it by
            // pointer identity through its lane map; the stored pointer is
            // only compared, never dereferenced, so removed or recreated
            // keys are safe here.
            bool found = false;
            int frame = 0;
            for (auto itr = map.begin(); itr != map.end(); ++itr) {
                if (itr.value() == item.key) {
                    frame = itr.key();
                    found = true;
                    break;
                }
            }
            if (!found)
                continue; // the key is gone from its lane: drop the entry

            Item moved;
            moved.target = TimeLineEvent::Target(*item.target.node, item.target.pos, frame);
            moved.target.pos.setIndex(frame);
            moved.key = item.key;
            // The marker an entry names is the user's choice, not the document's:
            // a key that moved to a new frame is the same key, so it is still
            // selected in the same component.
            moved.component = item.component;
            next.push_back(moved);
        }

        if (sameEntries(mItems, next))
            return false; // a data-only edit: every entry is still exactly what it was
        mItems = next;
        rebuildFocus();
        notifyChanged();
        return true;
    }

    void Selection::rebuildFocus() {
        mFocusLink.clear();
        for (Item& item : mItems) {
            // yields null and is skipped, so a stale item can never deref a
            // dangling TimeKey. Only refresh() touches the stored pointer,
            // and only by comparison against map entries -- never by
            // dereference.
            TimeKey* key = item.target.pos.isExist() ? item.target.pos.key() : nullptr;
            if (key) {
                key->setFocus(mFocusLink);
            }
        }
    }

} // namespace time
} // namespace ctrl
