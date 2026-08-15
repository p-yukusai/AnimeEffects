#include "gui/ThinSplitter.h"
#include "gui/theme/Colors.h"

#include <algorithm>
#include <QChildEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>

namespace gui {

namespace {
constexpr int kHitBoxSize = 12; // invisible grab area (VS Code style)
} // namespace

// --------------------------------------------------------------------------
// Grab area overlaid on one boundary. Child of the splitter (so it moves and
// dies with it), but ThinSplitter::childEvent keeps QSplitter from adopting
// it as a pane. Draws only the 1px hairline on the boundary (brightened on
// hover/drag); the rest of the box stays transparent.
class ThinSplitter::HitBox: public QWidget {
public:
    HitBox(ThinSplitter* aSplitter, int aBoundaryIndex):
        // construct parentless, then setParent: parenting in the base-class
        // constructor would fire ChildAdded while the object is still a plain
        // QWidget, slipping past ThinSplitter::childEvent's HitBox check and
        // getting adopted as a pane
        QWidget(nullptr),
        mSplitter(aSplitter),
        // QSplitter handles are 1-based (handle 0 is the hidden dummy), so the
        // boundary after pane i is driven through handle i+1
        mHandleIndex(aBoundaryIndex + 1),
        mHovered(false),
        mDragging(false),
        mPressPos(0),
        mStartBoundary(0),
        mBoundaryPos(0) {
        setParent(aSplitter);
        // paint only the hairline: with a universal `QWidget { background }`
        // rule in the QSS, the style sheet engine fills even a paintEvent-less
        // widget unless it is marked translucent
        setAttribute(Qt::WA_TranslucentBackground);
        const bool horizontal = mSplitter->orientation() == Qt::Horizontal;
        setCursor(horizontal ? Qt::SplitHCursor : Qt::SplitVCursor);
        setMouseTracking(true);
    }

    void setBoundary(int aBoundaryPos) {
        // splitter coordinates: the hitbox straddles the boundary, offset
        // toward the second pane so a first-pane edge scrollbar stays clear
        mBoundaryPos = aBoundaryPos;
        if (mSplitter->orientation() == Qt::Horizontal)
            setGeometry(aBoundaryPos - kHitBoxSize / 2 + mSplitter->mHitBoxOffset, 0,
                        kHitBoxSize, mSplitter->height());
        else
            setGeometry(0, aBoundaryPos - kHitBoxSize / 2 + mSplitter->mHitBoxOffset,
                        mSplitter->width(), kHitBoxSize);
        raise();
        show();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        // draw only the 1px hairline centered on the boundary; never call the
        // base paintEvent, so the universal QWidget QSS background cannot
        // fill the hitbox
        const theme::Colors c = theme::Colors::current();
        QPainter p(this);
        p.fillRect(lineRect(), mDragging ? c.hover : (mHovered ? c.hairlineHover : c.hairline));
    }

    void enterEvent(QEnterEvent*) override {
        mHovered = true;
        update();
    }

    void leaveEvent(QEvent*) override {
        mHovered = false;
        update();
    }

    void mousePressEvent(QMouseEvent* aEvent) override {
        if (aEvent->button() != Qt::LeftButton)
            return;
        mDragging = true;
        // the widget keeps an implicit mouse grab until release, so the drag
        // survives the cursor leaving the hitbox
        const QPointF global = aEvent->globalPosition();
        mPressPos = (mSplitter->orientation() == Qt::Horizontal) ? global.x() : global.y();
        // the drag reference is the boundary itself, not the (offset) hitbox
        // center, so the splitter tracks the cursor exactly
        mStartBoundary = mBoundaryPos;
        update();
    }

    void mouseMoveEvent(QMouseEvent* aEvent) override {
        if (!mDragging)
            return;
        const QPointF global = aEvent->globalPosition();
        const int pos = (mSplitter->orientation() == Qt::Horizontal) ? global.x() : global.y();
        // drive the resize through the stock logic: the splitter clamps to
        // the panel minimums and redistributes proportionally
        mSplitter->moveTo(mStartBoundary + (pos - mPressPos), mHandleIndex);
    }

    void mouseReleaseEvent(QMouseEvent* aEvent) override {
        if (aEvent->button() == Qt::LeftButton) {
            mDragging = false;
            update();
        }
    }

private:
    QRect lineRect() const {
        // the hairline stays exactly on the boundary, wherever the hitbox
        // sits: kHitBoxSize/2 - offset relative to the hitbox
        if (mSplitter->orientation() == Qt::Horizontal)
            return QRect(kHitBoxSize / 2 - mSplitter->mHitBoxOffset, 0, 1, height());
        return QRect(0, kHitBoxSize / 2 - mSplitter->mHitBoxOffset, width(), 1);
    }

    ThinSplitter* mSplitter;
    int mHandleIndex;
    bool mHovered;
    bool mDragging;
    int mPressPos;
    int mStartBoundary;
    int mBoundaryPos;
};

// --------------------------------------------------------------------------
ThinSplitter::ThinSplitter(Qt::Orientation aOrientation, QWidget* aParent):
    QSplitter(aOrientation, aParent), mHitBoxOffset(0) {
    // the handle takes no layout space: panels tile edge-to-edge and the
    // invisible hitboxes overlay the boundaries
    setHandleWidth(0);
    // dragging resizes the panes but not the splitter, so reposition the
    // hitboxes explicitly after every move
    connect(this, &QSplitter::splitterMoved, this, [this](int, int) { relayoutHitBoxes(); });
}

void ThinSplitter::setHitBoxOffset(int aOffset) {
    // keep the hairline inside the hitbox: the offset must stay within half
    // the hitbox size of the center
    mHitBoxOffset = std::max(-kHitBoxSize / 2 + 1, std::min(kHitBoxSize / 2 - 1, aOffset));
    relayoutHitBoxes();
}

void ThinSplitter::childEvent(QChildEvent* aEvent) {
    // never let QSplitter adopt a hitbox as a pane (it auto-inserts any
    // QWidget child)
    if (dynamic_cast<HitBox*>(aEvent->child()))
        return;
    QSplitter::childEvent(aEvent);
    if (aEvent->type() == QEvent::ChildAdded) {
        // the new pane has no geometry yet; relayout once the event loop
        // settles
        QTimer::singleShot(0, this, [this]() { relayoutHitBoxes(); });
    }
}

ThinSplitter::HitBox* ThinSplitter::hitBoxForBoundary(int aBoundaryIndex) {
    // one hitbox per boundary, created lazily once the splitter has panes
    while (mHitBoxes.size() <= aBoundaryIndex)
        mHitBoxes.push_back(new HitBox(this, mHitBoxes.size()));
    return mHitBoxes[aBoundaryIndex];
}

void ThinSplitter::relayoutHitBoxes() {
    // one boundary between each pair of visible panes. Compute it from the
    // PANES (their shared edge), not from QSplitter::handle positions: with a
    // zero handle width the splitter's internal handles sit in odd places
    const int paneCount = count();
    for (int i = 0; i + 1 < paneCount; ++i) {
        QWidget* pane = widget(i);
        QWidget* next = widget(i + 1);
        if (!pane || !next)
            continue;
        HitBox* hitBox = hitBoxForBoundary(i);
        if (pane->isHidden() || next->isHidden()) {
            hitBox->hide();
            continue;
        }
        const int boundary =
            (orientation() == Qt::Horizontal) ? pane->x() + pane->width() : pane->y() + pane->height();
        hitBox->setBoundary(boundary);
    }
}

void ThinSplitter::resizeEvent(QResizeEvent* aEvent) {
    QSplitter::resizeEvent(aEvent);
    relayoutHitBoxes();
}

void ThinSplitter::showEvent(QShowEvent* aEvent) {
    QSplitter::showEvent(aEvent);
    relayoutHitBoxes();
}

} // namespace gui
