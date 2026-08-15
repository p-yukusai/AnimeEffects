#ifndef CTRL_TIME_SELECTION_H
#define CTRL_TIME_SELECTION_H

#include <QVector>
#include "util/LifeLink.h"
#include "core/TimeKey.h"
#include "core/TimeLineEvent.h"

namespace ctrl {
namespace time {

    // The set of selected timeline keys. One LifeLink drives TimeKey::setFocus
    // for every member, so the renderer's highlight state always matches this
    // set exactly. Members are TimeLineEvent::Targets — the same currency the
    // existing copy/delete/move commands consume. Each item also keeps the
    // TimeKey pointer so the set can follow keys that moved to new frames.
    class Selection {
    public:
        struct Item {
            Item(): key() {}
            Item(const core::TimeLineEvent::Target& aTarget, core::TimeKey* aKey): target(aTarget), key(aKey) {}

            core::TimeLineEvent::Target target;
            core::TimeKey* key;
        };

        Selection() {}

        void clear();
        bool empty() const { return mItems.isEmpty(); }
        int count() const { return mItems.size(); }

        bool contains(const core::TimeLineEvent::Target& aTarget) const;

        void set(const core::TimeLineEvent& aEvent); // replace with the event's targets
        void add(const core::TimeLineEvent& aEvent); // union
        void subtract(const core::TimeLineEvent& aEvent); // remove the event's targets
        void toggle(const core::TimeLineEvent::Target& aTarget);
        void assign(core::TimeLineEvent& aOut) const; // copy targets into aOut

        // Re-resolve each target to its key's current frame (after a move).
        void reindex();

        const QVector<Item>& items() const { return mItems; }

    private:
        // Identity of a target is (line, type, frame); the map is unique per
        // line+type, so comparing map addresses is the exact and cheap check.
        static bool sameKey(const core::TimeLineEvent::Target& aLhs, const core::TimeLineEvent::Target& aRhs);
        void rebuildFocus();

        QVector<Item> mItems;
        util::LifeLink mFocusLink;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_SELECTION_H
