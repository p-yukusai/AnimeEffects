#ifndef GUI_DOCKSASH_H
#define GUI_DOCKSASH_H

#include <QMainWindow>
#include <QVector>

namespace gui {

// The VS Code sash pattern for QMainWindow dock edges, mirroring ThinSplitter:
//   - the native QMainWindow separators are styled to 0px / invisible via QSS,
//     so docks tile edge-to-edge with NO layout gap,
//   - an invisible 12px hitbox overlays each dock boundary facing the central
//     widget, draws the 1px hairline (with hover/active feedback) and drives
//     the resize through the public QMainWindow::resizeDocks API (which clamps
//     to each dock's minimum/maximum sizes).
// Attach once to a QMainWindow after its docks exist; the object parents
// itself to the window and tracks dock moves/floats/hides via an event filter.
class DockSash: public QObject {
    Q_OBJECT
public:
    explicit DockSash(QMainWindow* aWindow);

protected:
    bool eventFilter(QObject* aWatched, QEvent* aEvent) override;

private:
    class HitBox;
    void relayout();
    void scheduleRelayout();
    void watchDocks();

    QMainWindow* mWindow;
    QVector<HitBox*> mHitBoxes;
};

} // namespace gui

#endif // GUI_DOCKSASH_H
