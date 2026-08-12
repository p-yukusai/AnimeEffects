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

    void Selection::reindex() {
        for (Item& item : mItems) {
            if (item.key) {
                item.target.pos.setIndex(item.key->frame());
            }
        }
    }

    void Selection::rebuildFocus() {
        mFocusLink.clear();
        for (Item& item : mItems) {
            // resolve through the map: a vanished key (index no longer valid)
            // yields null and is skipped, so a stale item can never deref a
            // dangling TimeKey. Only reindex() uses the stored pointer, on the
            // one path where the key is guaranteed alive (post-move).
            TimeKey* key = item.target.pos.isExist() ? item.target.pos.key() : nullptr;
            if (key) {
                key->setFocus(mFocusLink);
            }
        }
    }

} // namespace time
} // namespace ctrl
