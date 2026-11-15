#ifndef GUI_TIMELINEHEADER_H
#define GUI_TIMELINEHEADER_H

#include <QWidget>
#include <QString>
#include <QScopedPointer>
#include <QPointer>
#include <vector>
#include "util/SlotId.h"
#include "util/Easing.h"
#ifdef AE_ENABLE_IMGUI
// The channel rows the Header's channels button lists (gui::PanelChannelRow): the
// header hands them to gui::MainDisplayWidget, which owns the popup.
#include "gui/GraphPanelModel.h"
#endif

class QAbstractButton;
class QHBoxLayout;
#ifdef AE_ENABLE_IMGUI
class QTabBar;
#endif

namespace theme {
class Theme;
}

namespace gui {

class GUIResources;
class IconButton;
class PlayBackWidget;
class TimeLineWidget;
class TimeLineEditorWidget;
class ProportionalButton;
#ifdef AE_ENABLE_IMGUI
class GraphPanelWidget;
#endif

namespace prop {
class EasingItem;
} // the easing chip + its picker (gui/prop/prop_Items.h)

// The timeline dock's header bar (design section 1): the 40px row above the
// surface, replacing the fixed-width PlayBackWidget column that used to sit
// beside the lanes. Left to right it holds
//
//   tabs | transport | quick easings | chip | stretch | proportional | channels
//
// with a 1x16 rule after the tabs and after the transport (the mock's .tl-hairline). The TABS are the dock's two views (design section 5, "one
// dock, two tabs"): Timeline is the lanes' surface (this bar's own surface) and Graph
// is the animation graph panel drawn on the same dock floor, with the object tree
// still beside it - the bar keeps ONE transport and ONE chip for both,
// and the tab is what says which view the dock's body is showing. In a build without
// the graph panel (AE_ENABLE_IMGUI off) the strip, the channels button and their rules
// do not exist at all: the bar keeps the transport, the chip, the quick easings and the
// view. The transport is the existing eight-button PlayBackWidget group, moved
// out of its column and laid out horizontally; the easing chip is the picker's only
// home (design section 2) and names the timeline's key selection; the four QUICK
// EASINGS to its left apply one easing TYPE to that same selection in one click (a
// shortcut for the picker's four common tiles, disabled while nothing is selected and
// lit on the one the selection agrees on, drawn with the picker's own curve glyphs);
// the PROPORTIONAL toggle at the right is NOT part of the easing group: its glyph is
// a relative key in context (two absolute anchors joined to a ring in the middle),
// and it changes the selection's relative flag through the same edit the key menu's
// item makes (a mixed selection reads as a dash through the ring);
// the CHANNELS button (Graph tab only) opens the panel's channel list, which the
// design keeps out of the panel's own chrome because it is a view action. There is NO
// zoom or fit group here: the timeline's wheel, its scroll bar and the graph panel's
// wheel own that, so a row of buttons for the same ladder was redundant (the bar's
// right end is the channels trigger on the Graph tab).
//
// The frame readout that used to sit between the transport and the chip is GONE: the
// current frame is in the playhead pill and the end frame is in the ruler, so a third
// copy in the bar said nothing the surface did not already say.
//
// The header's own section separator - the 1px hairline on its bottom edge - is
// painted, not styled: QSS box rules do not render on plain widget surfaces.
class TimeLineHeader: public QWidget {
    Q_OBJECT
public:
    // The dock's views. Timeline is the lanes' surface and, when the graph panel is
    // part of the build, Graph is the animation graph panel; the dock's body shows
    // the one this names, and the tree is the one part that is on screen in both.
    // Without the panel only Timeline exists: the header has no tab bar at all, so
    // no graph value can be reported.
    enum class Surface {
        Timeline,
#ifdef AE_ENABLE_IMGUI
        Graph,
#endif
    };

    TimeLineHeader(GUIResources& aResources, PlayBackWidget* aTransport, QWidget* aParent = nullptr);
    ~TimeLineHeader() override;

    // The timeline the quick easings apply to and the easing chip names the selection
    // of. Null (or a timeline with no project behind it) leaves the quick easings
    // disabled and the chip disabled rather than acting on a stale surface.
    void setTimeLineWidget(TimeLineWidget* aTimeLine);

    // The 196px area between the quick easings and the stretch, which the easing chip
    // fills. Fixed on purpose: the chip's content changes with the key selection, and a
    // control that resizes would re-flow the bar beside it.
    QWidget* easingChipSlot() const { return mEaseSlot; }

    // RE-READS THE BAR'S TIMELINE-DERIVED STATE: the quick easings' availability and
    // which of them is active, plus (in a build with the panel) the channels button's
    // own words. Public because a state the bar shows arrives through neither of the
    // signals it follows - the dock calls it when the project changed and when the
    // animation-settings popup edits the frame range.
    void refreshFrame();

signals:
    // The easing chip was activated (a left-click on it). The chip opens the
    // picker itself; this is the bar's own report that its focal control was used.
    void easingChipActivated();

    // The dock's view changed (a click on a tab). The dock's body follows this: it
    // shows the Timeline tab's lanes or the Graph tab's panel, and the tree stays
    // on screen for both. Never emitted with a graph value in a build without the
    // graph panel: the header has no tabs then, and Timeline is the only surface.
    void surfaceChanged(Surface aSurface);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void addHairline(QHBoxLayout& aLayout);
#ifdef AE_ENABLE_IMGUI
    // The tab strip's own ink: the mock's .tl-tabs numbers (11px, 3px/8px padding,
    // a 4px radius) on the application's tokens - muted text at rest, the ACCENT
    // fill for the selected tab, `raised` on hover - re-applied on every theme
    // change. Built in code because the header has no stylesheet asset of its own
    // (PlayBackWidget's playbackwidget.ssa styles the buttons, not a QTabBar) and
    // this bar must not add one: the theme's tokens are the same ones the app's
    // own tab bar (ProjectTabBar's modetabbar.ssa) is written against.
    void refreshTabStyle();
#endif
    // The chip's click: the picker opens for the timeline's key selection, under
    // the chip (the chip's state is pushed to it by the surface, see
    // TimeLineEditorWidget::setEasingItemHost).
    void onEasingChipClicked();
    // The four quick easings, in the bar's own vocabulary: one click writes that
    // easing to every key the timeline's live selection names. Three are a TYPE;
    // the fourth is the AUTO PRESET, which makes the SEGMENT AFTER each selected
    // key automatic (automatic handles are per-side modes now, not a type): the
    // key's own type and outgoing side plus the next key's incoming side. The
    // arriving segment is left alone, and a last key (no segment after it) writes
    // nothing. The write goes through the application's own selection action (see
    // the definition), in one undo step.
    void onQuickEasingClicked(util::Easing::Type aType, bool aAutoPreset);
    void refreshQuickEasings();
    // The proportional toggle's state: all selected keys proportional (bright glyph),
    // all absolute (dim glyph) or a mix (medium ink plus a dash), disabled with no key
    // to act on. Re-read on every selection change and after its own edit.
    void refreshProportional();
    void onProportionalClicked();
    // Attaches the state the quick easings read: the one picker lives in the timeline's
    // surface, so the host is bound (setEasingItemHost) and the picker's own commit is
    // composed with this bar's re-read - the one easing write that does NOT move the
    // selection.
    void bindEasingHost();
#ifdef AE_ENABLE_IMGUI
    // QTabBar::currentChanged, in the header's own currency.
    void onTabChanged(int aIndex);
    // The channels button: it opens the panel's channel list (gui::GraphChannelPopup,
    // through gui::MainDisplayWidget) for the row list the dock's Graph page publishes,
    // anchored under the button itself.
    void onChannelsClicked();
    // The button's own words and availability, from the rows it would open: the
    // model's own rule (the total while everything is shown, "shown of total" once a
    // channel is hidden), disabled while there is no row at all (a control that would
    // open an empty list).
    void updateChannelsButton(const QVector<PanelChannelRow>& aRows);
    // The dock's Graph page, when the window has one (the header carries no reference
    // to the dock's body, so the page is asked for through the window - the same route
    // gui::GraphPanelWidget uses for the application's menus).
    GraphPanelWidget* graphPanelWidget() const;
    // Subscribe to the page's own "the shown channels changed" - once, the first time the
    // page can be found (it is a sibling page that may not exist when this bar is built).
    void connectChannels();
#endif
    void onThemeUpdated(theme::Theme& aTheme);

    GUIResources& mGUIResources;
    PlayBackWidget* mTransport;
#ifdef AE_ENABLE_IMGUI
    // The dock's views, as tabs at the bar's left end (the mock's .tl-tabs). The
    // control is a real QTabBar: it is the application's own tab control
    // (gui::ProjectTabBar is a QTabBar too), so keyboard left/right, the hover and
    // the selected states all come from Qt rather than from a hand-rolled pair of
    // buttons. Not built without the graph panel (see the constructor).
    QTabBar* mTabs;
#endif
    TimeLineWidget* mTimeLine;
    // The timeline's surface widget, found inside mTimeLine: the ONE easing picker
    // and the chip's state both live there, and the timeline itself hands out its
    // editor, not its surface.
    //
    // QPointer, NOT A BARE POINTER. The surface is a CHILD of the timeline, so the
    // dock's child sweep frees it in creation order - before this bar, which the dock
    // creates last. A raw pointer here reads as non-null on freed memory, and the
    // destructor's `mInner->...` then walks a freed signaller (measured: exit 139 on
    // every quit, stack in util::Signaler::disconnect). The self-nulling handle makes
    // every use below null-guarded by construction instead of by an ordering promise.
    QPointer<TimeLineEditorWidget> mInner;
    // The live key selection's own change notification (ctrl::time::Selection::
    // onChanged, off the timeline's editor), which is what re-reads the quick easings
    // whichever view moved the selection - the same source the chip is pushed from.
    util::SlotId mSelectionSlot;
    // The KEY FLAGS' own notification (the menu's and this bar's proportional
    // edit): the selection does not move when a key becomes proportional, so its
    // onChanged cannot carry this state.
    QMetaObject::Connection mProportionalConnection;
    // The proportional toggle: a single glyph on the bar's right - two absolute key
    // bodies joined to a ring (the relative key) - with the toggle state in its ink
    // and a dash through the ring for a mixed selection. Disabled while nothing is
    // selected.
    ProportionalButton* mProportionalButton;
    QWidget* mEaseSlot;
    // The easing control: the chip in the slot above and the single picker
    // popover it opens (see prop::EasingItem).
    QScopedPointer<prop::EasingItem> mEasing;
    // The four quick easings to the left of the chip: (button, the easing it
    // writes, whether it is the Auto preset). Glyph buttons that draw the same
    // curve as the picker's tiles (Auto with its derived handles); lit in the
    // panel's text ink while the selection agrees on that easing and disabled
    // while nothing is selected. Each button's box is fixed, so its state never
    // changes the bar's width.
    struct QuickEase {
        QAbstractButton* button;
        util::Easing::Type type;
        bool autoPreset;
    };
    std::vector<QuickEase> mQuickEases;
#ifdef AE_ENABLE_IMGUI
    // The channels trigger: the mock's .tl-chans, hosted in the dock's header (the
    // panel's one control is a view action, so it sits with the view controls) and
    // shown only while the Graph tab is the dock's view. Its words are the panel's
    // own count ("4 of 6"), read from the page's rows. The trigger's BOX is fixed
    // (the widest three-digit label) and its size policy retains it while hidden, so
    // the header's minimum width - and with it the dock's - is the same on both tabs.
    QAbstractButton* mChannelsButton;
    // The rows this control names come from the panel, which is a SIBLING page that may
    // not exist yet when this bar is built, so the subscription is made the first time the
    // page can be found (see connectChannels). Without it the words are re-read only when
    // the TAB changes, and a layer picked while the Graph tab is already up leaves the
    // count stale (measured: "0 channel(s)" over three drawn curves).
    bool mChannelsConnected = false;
#endif
};

} // namespace gui

#endif // GUI_TIMELINEHEADER_H
