#ifndef CTRL_TIME_SELECTION_H
#define CTRL_TIME_SELECTION_H

#include <functional>
#include <QVector>
#include "util/LifeLink.h"
#include "util/Signaler.h"
#include "core/TimeKey.h"
#include "core/TimeLineEvent.h"

namespace ctrl {
namespace time {

    // The set of selected timeline keys - and, since the animation graph panel
    // shares it, of the MARKERS those keys are drawn as.
    //
    // ONE SET, TWO LEVELS. The timeline selects KEYS: its own gestures, its
    // copy/delete/easing actions and its renderer's highlight all speak of a key,
    // and it keeps doing exactly that. The graph panel selects MARKERS: one
    // component of one key is a thing a user can select on its own (a Scale key's
    // X without its Y), so an entry names either
    //   * the WHOLE key - component == kKeyLevel, which is what every timeline
    //     gesture and the panel's own bulk gestures (a marquee, ctrl+A) write, and
    //     which covers EVERY marker its key owns - or
    //   * ONE component of it - component >= 0, what a press on one curve writes.
    // One container, one identity (line, type, frame) plus the component, rather
    // than two sets that would have to agree.
    //
    // Every mutator is the set's own rule, so nobody repeats it outside: an entry
    // already in the set is not added twice (add), one that is not there is not
    // removed (subtract), a marker the set already covers is not added (addMarker),
    // and a component with no entry for it is not invented.
    //
    // Every mutator also ANSWERS WHETHER IT CHANGED THE SET and fires onChanged
    // exactly when it did, so a consumer can follow the selection without polling
    // it. onChanged is what the timeline's header chip reads instead of a reconcile
    // timer: the set is the one thing both views write, so it is the one place a
    // change can be noticed whichever view made it.
    //
    // One LifeLink drives TimeKey::setFocus for every member key, so the renderer's
    // highlight state always matches this set exactly - a marker entry focuses the
    // KEY it belongs to, which is why selecting one component of a key highlights
    // the whole key in the timeline. Members are TimeLineEvent::Targets - the same
    // currency the existing copy/delete/move commands consume. Each item also keeps
    // the TimeKey pointer so the set can follow keys that moved to new frames.
    class Selection {
    public:
        // The component of an entry that names the WHOLE key - every component it
        // owns - rather than one of its markers. Negative, so no component index a
        // key type can report collides with it.
        static constexpr int kKeyLevel = -1;

        struct Item {
            Item(): target(), key(), component(kKeyLevel) {}
            Item(const core::TimeLineEvent::Target& aTarget, core::TimeKey* aKey, int aComponent = kKeyLevel):
                target(aTarget), key(aKey), component(aComponent) {}

            core::TimeLineEvent::Target target;
            core::TimeKey* key;
            // kKeyLevel for the whole key, else one component index of its type.
            int component;
        };

        // Fired after every change to the set (see the class comment). No argument:
        // the set the consumer holds IS the answer.
        util::Signaler<void()> onChanged;

        Selection() {}

        // --- the set's own content ---
        // True when ANY entry names that key: a marker entry included, because the
        // key is the unit the timeline's currency is.
        bool empty() const { return mItems.isEmpty(); }
        // The number of ENTRIES (a whole key is one, each selected marker of a key
        // is one). The number of KEYS is a different question - see
        // ctrl::GraphEditor::selectionCount.
        int count() const { return mItems.size(); }

        // True when ANY entry names that key, a marker entry included.
        bool contains(const core::TimeLineEvent::Target& aTarget) const;
        // True when the selection names that MARKER: an entry for that exact
        // component, or a key-level entry, which covers every component its key
        // owns.
        bool containsMarker(const core::TimeLineEvent::Target& aTarget, int aComponent) const;
        // EVERY component of that key the set names, ascending and without
        // duplicates: a key-level entry names all aStoredCount of them, a marker
        // entry its own. Empty when no entry names the key (or when aStoredCount
        // is 0). This is what an editor reads when it has to OPEN on one
        // component: value(0) is the one to show, and size() > 1 says the
        // selection spans several (so a reader must not claim the others' values
        // are included in what it shows).
        QVector<int> componentsOf(const core::TimeLineEvent::Target& aTarget, int aStoredCount) const;

        // --- key level (the timeline's currency) ---
        bool clear(); // empty the set
        bool set(const core::TimeLineEvent& aEvent); // replace with the event's targets
        bool add(const core::TimeLineEvent& aEvent); // union
        bool subtract(const core::TimeLineEvent& aEvent); // remove the event's targets
        bool toggle(const core::TimeLineEvent::Target& aTarget); // the whole KEY in/out
        void assign(core::TimeLineEvent& aOut) const; // copy targets into aOut (one per KEY)

        // --- marker level (the graph panel's currency) ---
        // Replaces the whole set with this one marker. aComponent is the entry's
        // component, kKeyLevel included (a caller naming a whole key).
        bool setMarker(const core::TimeLineEvent::Target& aTarget, int aComponent);
        // Unions one marker in: a marker the set already covers is left alone.
        bool addMarker(const core::TimeLineEvent::Target& aTarget, int aComponent);
        // Removes what covers that marker - the entry for that exact component, and
        // a key-level entry, which names it among every other component. A marker
        // nothing covers, and a key no entry names, change nothing.
        bool subtractMarker(const core::TimeLineEvent::Target& aTarget, int aComponent);

        // Reconcile the set with the timeline after a modification: follow
        // keys that moved to their current frames, keeping each entry's component,
        // and drop entries whose key no longer sits in its lane map (removed or
        // recreated). Safe against dangling keys: a stored key pointer is only
        // compared against map entries, never dereferenced.
        bool refresh();

        const QVector<Item>& items() const { return mItems; }

        // Keep only the entries aKeep accepts. Used when the view stops showing a
        // layer (an expand/collapse rebuild): the rest of the set is untouched, and
        // entries keep their identity, so no key is re-resolved from an unstable id.
        // Fires onChanged exactly when something was dropped.
        bool retain(const std::function<bool(const Item&)>& aKeep);

    private:
        // Identity of a target is (line, type, frame); the map is unique per
        // line+type, so comparing map addresses is the exact and cheap check.
        static bool sameKey(const core::TimeLineEvent::Target& aLhs, const core::TimeLineEvent::Target& aRhs);
        // True when two lists name the same entries, in the same order.
        static bool sameEntries(const QVector<Item>& aLhs, const QVector<Item>& aRhs);
        // True when anItem names aMarker (see containsMarker's rule).
        static bool namesMarker(const Item& aItem, const core::TimeLineEvent::Target& aTarget, int aComponent);

        // Re-applies every member key's focus (a marker entry focuses its KEY, so
        // the timeline's highlight follows the panel's selection), and fires
        // onChanged. The two always travel together: a mutator that changed the set
        // reaches both, one that did not has already returned false.
        void rebuildFocus();
        void notifyChanged() { onChanged(); }

        QVector<Item> mItems;
        util::LifeLink mFocusLink;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_SELECTION_H
