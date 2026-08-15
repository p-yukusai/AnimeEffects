#ifndef GUI_THINSPLITTER_H
#define GUI_THINSPLITTER_H

#include <QSplitter>

namespace gui {

// QSplitter with the VS Code sash pattern:
//   - the handle width is 0, so the panels tile edge-to-edge and the
//     splitter does NOT affect the layout (no hidden gap), and draws no
//     visuals of its own,
//   - an invisible 12px hitbox overlays each boundary, extending over the
//     neighbouring panels without affecting the layout; it provides the grab
//     area (cursor + drag), draws the 1px hairline on the boundary (with
//     hover/active feedback), and drives the resize through the stock
//     QSplitter::moveSplitter, so all the proven splitter logic (minimums,
//     proportional resize, keyboard) stays in QSplitter.
// The hitbox is a child of the splitter itself; childEvent blocks QSplitter
// from auto-adopting it as a pane, and WA_TranslucentBackground keeps the
// universal QWidget QSS background from filling it.
class ThinSplitter: public QSplitter {
    Q_OBJECT
public:
    ThinSplitter(Qt::Orientation aOrientation, QWidget* aParent = nullptr);

    // Shift the hitbox grab area from its centered boundary position, in px
    // (positive = toward the second pane). The hairline always stays exactly
    // on the boundary; use this to keep the grab area off a first pane's edge
    // scrollbar (e.g. +4 -> 2px over the first pane, 10px over the second).
    void setHitBoxOffset(int aOffset);

    // moveSplitter is protected in QSplitter; the hitboxes drive the stock
    // resize logic through it
    void moveTo(int aPos, int aHandleIndex) { moveSplitter(aPos, aHandleIndex); }

protected:
    void childEvent(QChildEvent* aEvent) override;
    void resizeEvent(QResizeEvent* aEvent) override;
    void showEvent(QShowEvent* aEvent) override;

private:
    class HitBox;
    HitBox* hitBoxForBoundary(int aBoundaryIndex);
    void relayoutHitBoxes();

    QVector<HitBox*> mHitBoxes;
    int mHitBoxOffset;
};

} // namespace gui

#endif // GUI_THINSPLITTER_H
