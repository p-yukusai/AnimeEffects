#ifndef GUI_QML_ANIMATIONSETTINGVIEWMODEL_H
#define GUI_QML_ANIMATIONSETTINGVIEWMODEL_H

#include <QObject>
#include <QVector>

namespace gui {

// View-model backing the Playback settings dialog scene. The QML scene binds
// to these properties and calls reject() on the close button; the shell relays
// each property change out via the dialog's own changed() signal and applies it
// to the model live (there is no OK/Cancel/exec).
//
// frameStart/frameEnd are kept in range by the setters (start <= end) so the
// QML side can do one-way bindings without re-validating. The reason the
// bounds use the full int range (and may be negative) is that pre-roll frames
// before 0 are a valid project extent; the range is a playback/export bound,
// not a constraint on key positions.
class AnimationSettingViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int frameStart READ frameStart WRITE setFrameStart NOTIFY frameStartChanged)
    Q_PROPERTY(int frameEnd READ frameEnd WRITE setFrameEnd NOTIFY frameEndChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY loopChanged)

public:
    // One snapshot of the whole settings state, used for the session undo/redo
    // history (a single popup session squashes to one model undo step on close).
    struct State {
        int start = 0;
        int end = 0;
        int fps = 0;
        bool loop = false;
        bool operator==(const State& aOther) const {
            return start == aOther.start && end == aOther.end
                && fps == aOther.fps && loop == aOther.loop;
        }
        bool operator!=(const State& aOther) const { return !(*this == aOther); }
    };

    AnimationSettingViewModel(int aStart, int aEnd, int aFps, bool aLoop, QObject* aParent = nullptr);

    int frameStart() const { return mFrameStart; }
    int frameEnd() const { return mFrameEnd; }
    int fps() const { return mFps; }
    bool loop() const { return mLoop; }

    void setFrameStart(int aValue);
    void setFrameEnd(int aValue);
    void setFps(int aValue);
    void setLoop(bool aValue);

    // Reset the working state and drop the session history; called on every open.
    void reset(int aStart, int aEnd, int aFps, bool aLoop);

    // ---- session undo/redo (one step per edit, one model undo step on close) ----
    // A continuous scrub is one gesture: beginGesture() snapshots the pre-drag
    // state and endGesture() folds the whole drag into a single history entry.
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    // Step the working state back/forward within the session (Ctrl+Z/Shift+Z).
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // The QML close button calls reject() to dismiss the popup; the shell
    // connects rejected() to hide. Commits are applied live (each property
    // change is read by the shell and applied immediately), not on an OK.
    Q_INVOKABLE void reject();

signals:
    void frameStartChanged();
    void frameEndChanged();
    void fpsChanged();
    void loopChanged();
    void rejected();

private:
    State state() const;
    // Set the working state from a snapshot without recording a history entry
    // (used by undo/redo).
    void applyState(const State& aState);
    // Record a snapshot to step back to (and discard the redo stack).
    void pushState(const State& aState);

    int mFrameStart;
    int mFrameEnd;
    int mFps;
    bool mLoop;

    QVector<State> mUndo;   // states to step back to
    QVector<State> mRedo;   // states to step forward to
    State mGestureStart;    // draft snapshot at drag start (coalesced in endGesture)
    bool mGestureActive = false;
};

} // namespace gui

#endif // GUI_QML_ANIMATIONSETTINGVIEWMODEL_H
