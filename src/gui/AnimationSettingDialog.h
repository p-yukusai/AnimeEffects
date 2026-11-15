#ifndef GUI_ANIMATIONSETTINGDIALOG_H
#define GUI_ANIMATIONSETTINGDIALOG_H

#include <QWidget>
#include <QMouseEvent>
#include "util/Range.h"

class QQuickWidget;

namespace gui {

class AnimationSettingViewModel;
class QmlTheme;

// Animation settings, rendered as a full-bleed Qt Quick scene (see
// qml/AnimationSettingDialog.qml) inside this widget, shown as a Qt::Popup
// anchored to the playback bar's options button — the same idiom as
// EasingPickWidget. Dismisses on an outside click.
//
// This is a pure VIEW: it holds no reference to the project. The current
// frame-range / fps / loop state is passed in to showAt(), so the same instance
// is created once and reused for every open (the popup simply shows/hides, and
// its QML scene is never destroyed and recreated — which is why it does not
// leak "Cannot read property of null" teardown errors on repeated opens).
//
// APPLY-LIVE: there is no OK/Cancel. Each committed change (a scrub drag ends,
// a value is typed and confirmed, or the loop checkbox toggles) updates the
// view-model and the shell emits changed(); the caller reads the current state
// (fps()/frameRange()/isLoopChecked()) and applies it immediately. The model is
// reset from the values passed to showAt() on every open.
class AnimationSettingDialog : public QWidget {
    Q_OBJECT
public:
    explicit AnimationSettingDialog(QWidget* aParent);
    ~AnimationSettingDialog() override;

    bool isLoopChecked() const;
    util::Range frameRange() const;
    int fps() const;

    // Reset the model to aRange/aFps/aLoop, position the popup just below
    // aAnchor (clamped to the screen) and show it.
    void showAt(const QPoint& aAnchor, const util::Range& aRange, int aFps, bool aLoop);

    // The QML scrub fields call these to mark a scrub drag as active/inactive.
    // (No grabMouse: that cannot keep the drag alive — Qt's popup path
    // re-targets an outside move with buttons()=NoButton, so the QML scene
    // stops the scrub. Instead the dialog forwards those moves to the
    // QQuickWidget with the button restored; see mouseMoveEvent/mouseReleaseEvent.)
    Q_INVOKABLE void beginScrub();
    Q_INVOKABLE void endScrub();

signals:
    void changed();

protected:
    // Forward the popup's re-targeted (outside) mouse events to the QQuickWidget
    // with the held LeftButton restored, so a QML scrub keeps tracking when the
    // cursor leaves the popup — the Qt::Popup path drops buttons() to NoButton
    // on those moves. Mirrors EasingPickWidget::forwardBlendCursorEvent.
    void mouseMoveEvent(QMouseEvent* aEvent) override;
    void mouseReleaseEvent(QMouseEvent* aEvent) override;
    void hideEvent(QHideEvent* aEvent) override;

private:
    AnimationSettingViewModel* mModel;
    QmlTheme* mTheme;
    QQuickWidget* mQuick;
    bool mScrubActive = false;
};

} // namespace gui

#endif // GUI_ANIMATIONSETTINGDIALOG_H
