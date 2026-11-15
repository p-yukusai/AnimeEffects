#include "ctrl/time/time_Selection.h"

using namespace core;

namespace ctrl {
namespace time {

    //-------------------------------------------------------------------------------------------------
    void Selection::clear() {
        mItems.clear();
        rebuildFocus();
    }

    bool Selection::contains(const TimeLineEvent::Target& aTarget) const {
        for (const Item& item : mItems) {
            if (sameKey(item.target, aTarget)) {
                return true;
            }
        }
        return false;
    }

    bool Selection::sameKey(const TimeLineEvent::Target& aLhs, const TimeLineEvent::Target& aRhs) {
        // the map is unique per (line, type); address equality is exact
        return &aLhs.pos.map() == &aRhs.pos.map() && aLhs.pos.type() == aRhs.pos.type() &&
               aLhs.pos.index() == aRhs.pos.index();
    }

    void Selection::set(const TimeLineEvent& aEvent) {
        mItems.clear();
        for (const TimeLineEvent::Target& src : aEvent.targets()) {
            if (!contains(src)) {
                TimeLineEvent::Target t = src;
                mItems.push_back(Item(t, t.pos.key()));
            }
        }
        rebuildFocus();
    }

    void Selection::add(const TimeLineEvent& aEvent) {
        for (const TimeLineEvent::Target& src : aEvent.targets()) {
            if (!contains(src)) {
                TimeLineEvent::Target t = src;
                mItems.push_back(Item(t, t.pos.key()));
            }
        }
        rebuildFocus();
    }

    void Selection::subtract(const TimeLineEvent& aEvent) {
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
        if (changed) {
            rebuildFocus();
        }
    }

    void Selection::toggle(const TimeLineEvent::Target& aTarget) {
        for (auto itr = mItems.begin(); itr != mItems.end(); ++itr) {
            if (sameKey(itr->target, aTarget)) {
                mItems.erase(itr);
                rebuildFocus();
                return;
            }
        }
        TimeLineEvent::Target t = aTarget;
        mItems.push_back(Item(t, t.pos.key()));
        rebuildFocus();
    }

    void Selection::assign(TimeLineEvent& aOut) const {
        for (const Item& item : mItems) {
            aOut.pushTarget(*item.target.node, item.target.pos);
        }
    }

    void Selection::refresh() {
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
            next.push_back(moved);
        }
        mItems = next;
        rebuildFocus();
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
