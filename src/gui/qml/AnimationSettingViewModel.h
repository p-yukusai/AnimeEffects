#ifndef GUI_QML_ANIMATIONSETTINGVIEWMODEL_H
#define GUI_QML_ANIMATIONSETTINGVIEWMODEL_H

#include <QObject>

namespace gui {

// View-model backing the Animation settings dialog scene. The QML scene binds
// to these properties and calls reject() on the close button; the shell relays
// each property change out via the dialog's own changed() signal and applies it
// live (there is no OK/Cancel/exec).
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
    AnimationSettingViewModel(int aStart, int aEnd, int aFps, bool aLoop, QObject* aParent = nullptr);

    int frameStart() const { return mFrameStart; }
    int frameEnd() const { return mFrameEnd; }
    int fps() const { return mFps; }
    bool loop() const { return mLoop; }

    void setFrameStart(int aValue);
    void setFrameEnd(int aValue);
    void setFps(int aValue);
    void setLoop(bool aValue);

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
    int mFrameStart;
    int mFrameEnd;
    int mFps;
    bool mLoop;
};

} // namespace gui

#endif // GUI_QML_ANIMATIONSETTINGVIEWMODEL_H
