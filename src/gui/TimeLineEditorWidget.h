#ifndef GUI_TIMELINEINNERWIDGET_H
#define GUI_TIMELINEINNERWIDGET_H

#include <QWidget>
#include <QCanvasPainterWidget>
class QCanvasPainter;
#include <QAction>
#include <functional>
#include "core/TimeLineEvent.h"
#include "gui/AnimationRow.h"
#include "ctrl/TimeLineEditor.h"
#include "gui/ViaPoint.h"
#include "gui/GUIResources.h"
#include "gui/TimeRuler.h"
#include "KeyCommandMap.h"

class QMenu;

// The live easing edit's command (cmnd/BasicCommands.h): declared, not included,
// so this header does not pull the command templates into every timeline user.
namespace cmnd { template<typename tTarget, typename tValue> class ModifiableAssign; }

namespace gui {

namespace prop { class EasingItem; } // the header's easing control (prop_Items.h)

// THE PLAYHEAD'S LINE, AND NOTHING ELSE. The 14px frame-number pill above it is the
// ruler's model (gui/FrameRuler.h's Model::playhead) and the ruler paints it, so the
// pill has exactly one spelling; what is left here is the 1px rule that crosses the
// lanes, which cannot be the ruler's ink (it starts under the strip and runs to the
// bottom of the surface) and must not be the lanes' either (a frame step would repaint
// every key). So it is a ONE-COLUMN widget: 1px wide, from the strip's line row down,
// painted opaquely - no transparency, no compositing, and a repaint the size of the
// thing that changed.
class TimeCursor: public QWidget {
    Q_OBJECT

    Q_PROPERTY(QColor edgeColor READ edgeColor WRITE setEdgeColor DESIGNABLE true)
public:
    TimeCursor(QWidget* aParent);
    // aLineTopLeft is the line's own column and top row (the surface maps the playhead).
    void setCursorPos(const QPoint& aLineTopLeft, int aHeight);
    void paintEvent(QPaintEvent* aEvent) override;

    QColor edgeColor() const;
    void setEdgeColor(const QColor& edgeColor);

private:
    QColor mEdgeColor;
};

// THE TIMELINE'S SURFACE IS A HOST. It paints ctrl::time::BodyScene - the lanes, the frame
// range's guides, the key passes and the marquee - with QPainter by default, and with Canvas
// Painter (QRhi, GPU) when AE_CANVAS_PAINTER is on. Nothing else changes: the children
// (TimeRuler, TimeCursor), the input and the hit-testing are untouched, because a
// QCanvasPainterWidget is a QWidget.
class TimeLineEditorWidget:
    public QCanvasPainterWidget
{
    Q_OBJECT

    // Properties introduced to let users theme the custom rendered parts of the widget.
    // Qt's stylesheet support for custom rendering is applied per widget via properties.
    Q_PROPERTY(QColor headerContentColor READ headerContentColor WRITE setHeaderContentColor DESIGNABLE true)
    Q_PROPERTY(QColor headerBackgroundColor READ headerBackgroundColor WRITE setHeaderBackgroundColor DESIGNABLE true)
    Q_PROPERTY(QColor rulerLineColor READ rulerLineColor WRITE setRulerLineColor DESIGNABLE true)


    Q_PROPERTY(QColor trackColor READ trackColor WRITE setTrackColor DESIGNABLE true)
    Q_PROPERTY(QColor trackEdgeColor READ trackEdgeColor WRITE setTrackEdgeColor DESIGNABLE true)
    Q_PROPERTY(QColor trackTextColor READ trackTextColor WRITE setTrackTextColor DESIGNABLE true)
    Q_PROPERTY(QColor trackSelectColor READ trackSelectColor WRITE setTrackSelectColor DESIGNABLE true)
    Q_PROPERTY(QColor trackSeperatorColor READ trackSeperatorColor WRITE setTrackSeperatorColor DESIGNABLE true)

public:
    TimeLineEditorWidget(ViaPoint& aViaPoint, QWidget* aParent);

    // Ruler gestures (seek / playback-window drag) move the current frame;
    // TimeLineWidget forwards this to its onFrameUpdated signal.
    Q_SIGNAL void rulerFrameChanged();
    // The temporary playback window changed (a shift-drag on the ruler set, moved or
    // collapsed it, or the context menu cleared it). The window is the timeline's own
    // state, and the animation graph panel's ruler overlays it too, so the GUI needs a
    // notification when the gesture lands - the frame window's onViewRangeChanged is
    // not emitted by a window drag that moves no frame.
    Q_SIGNAL void playWindowChanged();
    // THE SELECTION'S PROPORTIONAL FLAGS CHANGED (the key menu, the header
    // control, or any other caller of setSelectionProportional). The flags live
    // on the keys, not on the selection, so the selection's own onChanged does
    // not fire for this edit; the header's button re-reads through this signal.
    Q_SIGNAL void keyProportionalChanged();

    void setProject(core::Project* aProject);
    void setFrame(core::Frame aFrame);

    void updateCamera(const core::CameraInfo& aCamera);
    // The row list, from the dock's ONE row source (gui::ObjectTreeModel). Rows are content
    // pixels: this widget does not read any other view's geometry.
    void setRows(const gui::AnimationRows& aRows);
    void updateLineSelection(core::ObjectNode* aRepresent);
    bool updateCursor(const core::AbstractCursor& aCursor, Qt::KeyboardModifiers aModifiers);
    bool updateWheel(QWheelEvent* aEvent, int aMouseX, int& aPixelAfter);
    void updateProjectAttribute();
    void updateTheme(theme::Theme&);

    // The frame range's extent on the content axis (px): the left margin
    // gutter plus the scale position of the range's start/end frames.
    int frameStartPixel() const;
    int frameEndPixel() const;

    QSize getEditorSize() const;

    core::Frame currentFrame() const;
    // The controller the surface renders; TimeRuler drives it through the
    // same updateCursor entry point as the track input path.
    ctrl::TimeLineEditor* editor() const { return mEditor.data(); }
    // Opens the application's own KEY MENU at aGlobalPos, for the key aPressedKey
    // names under the caller's pointer: the menu a right-click in the timeline
    // itself builds (see buildKeyMenu), so both views offer the same items through
    // the same code and there is no second menu to keep in step.
    //
    // THE SET THE ITEMS ACT ON is the shared live selection, read here with the
    // timeline's own right-click rule (the one tests/rightclick_transient_selection.cpp
    // pins): a pressed key the selection already holds keeps the WHOLE selection, so
    // a menu opened on one key of a multi-key selection still names every selected
    // key; any other pressed key is lent a temporary selection for the menu's
    // lifetime, which is dropped when the menu closes (the previous selection is NOT
    // restored). This is the entry point for callers whose pointer is in THEIR own
    // space - the animation graph panel resolves the key with its OWN pick
    // (ctrl::GraphEditor::contextTargetAt, the press rule in panel pixels) - so the
    // rule is applied here, on the shared selection, by key identity:
    // ctrl::TimeLineEditor::selectKeysAt resolves a click in the timeline's own
    // pixels and cannot be reached from another view's geometry.
    //
    // The menu's paste entry anchors at aPressedKey's frame. False when there is no
    // menu to open: no editor, or aPressedKey is not a key the document holds.
    bool openKeyMenu(const core::TimeLineEvent::Target& aPressedKey, const QPoint& aGlobalPos);
    // The easing control the header hosts (prop::EasingItem). This widget is the
    // one place the picker is opened and the one place a committed edit lands, so
    // the control itself holds no model; the header sets it once, and clears it
    // when it goes away (null is a valid, inert state). The chip's state is
    // PUSHED to it (see pushEasingChipState), not asked for: this surface is where
    // the shared key selection is re-read, and the header has no other way to learn
    // that another view changed it.
    void setEasingItemHost(prop::EasingItem* aHost);
    // Opens THAT picker for the keys aTargets names, anchored at aGlobalAnchor -
    // which is the header's chip (gui::TimeLineHeader::onEasingChipClicked, the one
    // caller): the picker is a popover of the header's easing control, and the key
    // menus carry no easing entry, so this is not reached from a key at all. The
    // targets are held for the open's lifetime, so every edit the picker commits
    // applies to exactly the keys it was opened for, until the popover closes.
    //
    // False when nothing opened (no host, no targets, or no key of a type that
    // carries an easing).
    bool openEasingPicker(const core::TimeLineEvent& aTargets, const QPoint& aGlobalAnchor);
    // THE HEADER'S PROPORTIONAL CONTROL, in the timeline's own currency: make
    // every key the live selection names proportional (aEnabled) or absolute, as
    // ONE undo step. This is the same edit the key menu's "Proportional" item
    // makes; the header owns the control, so it lives here beside the other
    // selection actions rather than duplicating the toggle in the bar.
    //
    // FALSE when there is nothing to act on: no project/editor, an empty
    // selection, or only default keys (which have no authored position).
    bool setSelectionProportional(bool aEnabled);

    // THE BACKGROUND MENU, for a caller whose pointer is in ANOTHER view's space: the
    // animation graph panel's right-click on the plot's background. It offers exactly
    // what the timeline's own empty-space menu offers - buildKeyMenu over the SHARED
    // SELECTION, with the paste anchor mapped from aGlobalPos - and NOTHING ELSE: the
    // point is deliberately NOT hit-tested against this surface's lanes, because the
    // caller's pixels are the panel's and reading them as timeline model space named a
    // key of whatever row they happened to land in (a menu whose Delete then edited a
    // key the user was not looking at).
    //
    // aExtraText/aExtraAction append ONE view-specific entry after a separator, for a
    // caller that owns an action this surface does not (the graph's "Fit graph", which
    // runs ctrl::GraphEditor::fitAll). Empty text adds nothing. False when there is no
    // editor to build a menu from.
    bool openBackgroundMenu(
        const QPoint& aGlobalPos, const QString& aExtraText = QString(),
        const std::function<void()>& aExtraAction = std::function<void()>()
    );
    // THE HEADER'S QUICK-EASING BUTTONS, in the timeline's own currency: write
    // aEasing (or the Auto preset's mode pair) to every component the live
    // selection names, as ONE undo step. They live here rather than on the
    // animation graph's editor because the selection is the TIMELINE's: a Pose or
    // FFD key has one shared easing but no graph channel, and the graph's action
    // refuses it - the quick easings must not. FALSE when nothing was written
    // (no selection, only bone/mesh keys, or an edit that changes nothing).
    //
    // THE AUTO PRESET IS NOT A TYPE EDIT: it makes the segment AFTER each selected
    // key automatic - that key's type and outgoing side plus the next key's
    // incoming side (see ctrl::TimeLineEditor::assignSelectionAutoHandles). It is
    // one undo step per batch and can write up to two params per selected key; the
    // previous key's param and the key's own incoming side are NOT touched (they
    // belong to the segment arriving at the key). A LAST key (or the only key)
    // has no segment leaving it YET, so its OWN pair is written instead - type
    // Custom and both sides automatic - and the segments that appear when a
    // neighbour is added derive on both sides.
    bool applyEasingToSelection(util::Easing::Type aType);
    bool applyAutoHandlesToSelection();
    // LIVE EASING EDIT — the seam a CONTINUOUS easing gesture uses (the picker's
    // bezier handle drag and a typed spin value today; the animation graph's
    // handle drags are the same shape).
    //
    // begin() resolves the LIVE SELECTION's easing params - every component a
    // whole-key entry names, only the named component of a marker entry, Pose/FFD
    // their one shared param - and arms a session. step() writes aDraft to EVERY
    // param through ONE rewriteable command, pushed on the first step that actually
    // changes something (a press that never moves records nothing); every later
    // step rewrites that command and re-announces the keys, so the document and
    // every view update per step while the whole gesture stays ONE undo entry.
    // end() closes the session; a gesture that was never stepped leaves no history.
    //
    // This is the app's ordinary drag rule: keep the command you pushed and rewrite
    // it while the stack still allows (cmnd::ModifiableAssign). An undo/redo drops
    // that link; the next step then opens a fresh command instead of corrupting the
    // history. The seam takes a SELECTION and a draft, not a picker state, so the
    // graph's own gestures are the same shape; the session spans the picker's open,
    // which is what makes one popup one entry (the blend slider's close-time commit
    // follows the same rule).
    bool beginEasingEdit(const QString& aName);
    bool stepEasingEdit(const util::Easing::Param& aDraft);
    void endEasingEdit();
    // The MoveCanvas key (space by default) is held: the ruler lets a
    // left-press propagate to the timeline's pan instead of seeking.
    void setPanKeyHeld(bool aHeld) {
        if (mTimeRuler)
            mTimeRuler->setPanKeyHeld(aHeld);
    }
    // The effective playback range (project range or the temporary window
    // override); TimeLineWidget's playback tick and the playhead clamp use it.
    util::Range playbackRange() const { return mEditor->playbackRange(); }
    // The temporary playback window (see ctrl::TimeLineEditor::PlayWindow);
    // the renderer draws its band and brackets.
    const ctrl::TimeLineEditor::PlayWindow& playWindow() const { return mEditor->playWindow(); }
    // THE WINDOW GESTURE FROM THE OTHER RULER: ctrl::GraphEditor's band delegates
    // its Shift-press/drag/release here (via gui::TimeLineWidget), one step at a
    // time, on this editor's own ctrl::TimeLineEditor::PlayWindow. Emits
    // playWindowChanged on a value change - the same notification the timeline's
    // own Shift-drag produces, so the graph's overlay is re-pushed with it, and the
    // panel's copy is never a step behind the timeline's window - then repaints both
    // rulers (the surface's track and the separate ruler strip). Returns true when
    // the window changed.
    //
    // Defined here so the gesture stays one expression of the editor's own three
    // steps; it touches only state this class already owns (mEditor, mTimeRuler, the
    // signal).
    bool updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep aStep, core::Frame aFrame) {
        const bool changed = mEditor->updatePlayWindow(aStep, aFrame);
        if (changed) {
            emit playWindowChanged();
            this->update();
            if (mTimeRuler)
                mTimeRuler->update();
        }
        return changed;
    }

    QColor headerContentColor() const;
    void setHeaderContentColor(const QColor& headerContentColor);

    QColor headerBackgroundColor() const;
    void setHeaderBackgroundColor(const QColor& headerBackgroundColor);

    QColor rulerLineColor() const;
    void setRulerLineColor(const QColor& rulerLineColor);


    QColor trackColor() const;
    void setTrackColor(const QColor& trackColor);

    QColor trackEdgeColor() const;
    void setTrackEdgeColor(const QColor& trackEdgeColor);

    QColor trackTextColor() const;
    void setTrackTextColor(const QColor& trackTextColor);

    QColor trackSelectColor() const;
    void setTrackSelectColor(const QColor& trackSelectColor);

    QColor trackSeperatorColor() const;
    void setTrackSeperatorColor(const QColor& trackSeperatorColor);


private:
    void updateTimeCursorPos();
    void updateSize();
protected:
    void paint(QCanvasPainter* aPainter) override;

private:
    void resizeEvent(QResizeEvent* aEvent) override;
    // THE SURFACE'S KEYBOARD. The surface takes focus on a click (it is the widget
    // the pointer lands on, and it has Qt::ClickFocus: a click here hands it the
    // keyboard without adding a Tab stop beside the timeline's own, and the press
    // itself still propagates to TimeLineWidget's gestures), and it owns exactly one
    // key:
    //   * Ctrl+A - ctrl::TimeLineEditor::selectAllKeys(), every key of every row the
    //     timeline shows, then a repaint.
    // Every other key is IGNORED, so it propagates to TimeLineWidget (the arrow-key
    // panning) and from there to the application's key map, exactly as before this
    // widget could hold focus.
    void keyPressEvent(QKeyEvent* aEvent) override;
    void onTimeLineModified(core::TimeLineEvent& aEvent, bool aUndo);
    void onTreeRestructured(core::ObjectTreeEvent& aEvent, bool aUndo);
    void onProjectAttributeModified(core::ProjectEvent& aEvent, bool aUndo);
    void onContextMenuRequested(const QPoint& aPos);
    // THE KEY MENU ITSELF, built once for both routes: the timeline's own right-click
    // (onContextMenuRequested) and the animation graph panel's (openKeyMenu). The
    // items act on mTargets - the whole selection, one target per key - so a
    // multi-key selection is one edit per item, not one key's; the paste entry is
    // enabled by what has been copied (mCopyTargets), and anchors at mPastePos,
    // which each caller sets to the frame the menu was opened on. An EMPTY mTargets
    // is the paste-only menu the timeline shows when a right-click names no key and
    // nothing is selected.
    void buildKeyMenu(QMenu& aMenu);
    // The one path both the menu and the header control use: push the
    // proportional edit for aTargets and report it (see keyProportionalChanged).
    void applyProportionalForTargets(const core::TimeLineEvent& aTargets, bool aEnabled);
    void onCopyKeyTriggered(bool);
    void onCopyCBTriggered(bool);
    void onPasteKeyTriggered(bool);
    void onDeleteKeyTriggered(bool);
    void onSelectSpacingTriggered();
    // Hands the header's chip what it names: one easing per selected KEY, in
    // selection order, read from the FIRST marker the selection names for it (so a
    // Y-only selection shows Y's curve, not the key's first component - the first
    // component the selection names, the SAME answer openEasingPicker uses), plus
    // the Auto context of that component, so its glyph is the curve that component
    // plays rather than a shape no key has. A whole-key entry names every component,
    // so it reads the first. When the shown key's selection spans SEVERAL components
    // the chip is handed their names as a scope note (its tooltip), because its one
    // painted value is then one of several and the picker edits them all.
    // Keys of a type that carries no easing (bone, mesh) contribute nothing, and so
    // does a selection entry whose key the document no longer holds - the key is
    // resolved from the document, as ctrl::time::Selection's contract requires (see
    // the definition). IDEMPOTENT: prop::EasingChip ignores a state equal to the one
    // it holds, so an extra call costs one compare.
    //
    // Called from the paint (see paintEvent), when a host is attached or the picker
    // opens, on a document modification (see onTimeLineModified), and - the source
    // that closes the last gap - by the selection's own change notification
    // (ctrl::time::Selection::onChanged), which fires whichever view made the
    // selection and needs nothing to be visible or painted.
    void pushEasingChipState();

    // THE PICKER'S SHOWN STATE, re-read from the document: the param and the Auto
    // context of the FIRST key the open (mTargets) names that CARRIES an easing -
    // preferring one the live selection still names, so a selection change among the
    // keys the open edits moves what the popup shows. False when nothing resolves (the
    // open's key was deleted). ONE function for the open (openEasingPicker) and the
    // live re-read (refreshOpenPicker), so the two cannot open on different curves.
    bool pickerShownState(util::Easing::Param& aShown, util::Easing::AutoContext& aAutoContext) const;
    // KEEPS THE OPEN PICKER IN STEP WITH THE DOCUMENT AND THE SELECTION: called from
    // the selection's own change notification and from onTimeLineModified, it pushes
    // the state pickerShownState reads to the control, which adopts it unless a drag is
    // in progress IN THE POPUP (there the popup's value wins until the gesture ends -
    // the picker's own rule, see EasingPickWidget::setShownValue). A no-op while the
    // picker is closed or no host is attached.
    void refreshOpenPicker();

    ViaPoint& mViaPoint;
    util::LinkPointer<core::Project> mProject;
    util::SlotId mTimeLineSlot;
    util::SlotId mTreeRestructSlot;
    util::SlotId mProjectAttrSlot;
    QScopedPointer<ctrl::TimeLineEditor> mEditor;
    const core::CameraInfo* mCamera;
    TimeCursor mTimeCursor;
    // The ruler strip: a real widget with its own paint and input, layered
    // under the playhead (TimeCursor) and over the track surface.
    QScopedPointer<TimeRuler> mTimeRuler;

    //@todo Implement deserializer to put timeevent data into a bytebuffer or plain text

    KeyCommandMap& mKeyCommandMap;
    QAction* mCopyKey;
    QAction* mCopyToClipboard;
    QAction* mSetSpacing;
    QAction* mPasteKey;
    QAction* mDeleteKey;
    QAction* mSelectSpacing;
    // The header's easing control, if a header hosts one: it owns the single
    // picker popover this widget opens (see openEasingPicker).
    prop::EasingItem* mEasingHost;
    core::TimeLineEvent mTargets;
    core::TimeLineEvent mCopyTargets;
    QPoint mPastePos;
    bool mOnPasting;

    // ONE live easing edit (see beginEasingEdit): the params the selection named
    // when it began, each with the rewriteable command the session pushed for it
    // (null until the first step that changed something), plus the frame of the
    // key each param belongs to - the notification is per KEY.
    struct EasingEditTarget {
        util::Easing::Param* param = nullptr;
        core::ObjectNode* node = nullptr;
        core::TimeKeyType type = core::TimeKeyType_TERM;
        core::Frame frame{0};
        cmnd::ModifiableAssign<util::Easing::Param, util::Easing::Param>* command = nullptr;
    };
    QVector<EasingEditTarget> mEasingEdit;
    QString mEasingEditName;
    bool mEasingEditOpen = false;
    // The session's later steps: rewrites the commands above and announces the
    // keys they changed as ONE operation (ctrl::TimeLineUtil::RewriteAnnounce).
    // The session never calls onTimeLineModified by hand.
    ctrl::TimeLineUtil::RewriteAnnounce mWrite;

    theme::TimeLine mTimelineTheme;
};

} // namespace gui

#endif // GUI_TIMELINEINNERWIDGET_H
