#ifndef CTRL_ASSIGNPIXELS_H
#define CTRL_ASSIGNPIXELS_H

#include <QImage>
#include <QRect>
#include "cmnd/Stable.h"
#include "core/FilterMask.h"

namespace ctrl {

// ONE mask stroke as ONE undo step, without copying the field per stamp.
//
// The command keeps the WHOLE field as it was when the stroke began (one copy per
// stroke: at most 1 MiB, because core::FilterMask caps its long side at 1024) and
// the pixels the stroke has written since, in the union box of the stamps. Redo
// re-applies that box; undo restores the whole "before" field, which is exactly
// what the stroke started from. Copying the field per STAMP would be a megabyte
// per cursor event; copying it once per stroke is the cost a paint stroke should
// have.
//
// Value-replayable, like every command in this tree: the pixels it carries are the
// document state, not a replay of the brush that produced them.
//
// It holds the MASK, not just its image: undoing pixels the renderer has already
// uploaded must also mark the field dirty, or the texture keeps the painted state
// until something else happens to touch it (measured: an undo restored the field
// and left the wave masked on screen).
class AssignPixels: public cmnd::Stable {
public:
    explicit AssignPixels(core::FilterMask& aMask):
        mMask(aMask), mTarget(aMask.image()), mBefore(), mAfter(), mAfterBox(), mDone(false) {}

    // call once, before the first write of the stroke
    void captureBefore() { mBefore = mTarget.copy(); }

    // call after each write: the box that stamp touched, plus the field to read it
    // from. The union box grows as the stroke travels.
    void addAfter(const QRect& aBox) {
        const QRect box = aBox.intersected(mTarget.rect());
        if (box.isEmpty())
            return;
        const QRect next = mAfterBox.isNull() ? box : mAfterBox.united(box);
        if (next != mAfterBox || mAfter.isNull()) {
            // re-cut the accumulated pixels for the new box: the old content is
            // copied into its place, so earlier stamps are not lost
            QImage grown(next.size(), mTarget.format());
            grown.fill(0);
            if (!mAfter.isNull()) {
                const QRect overlap = mAfterBox.intersected(next);
                for (int y = overlap.top(); y <= overlap.bottom(); ++y) {
                    const int rows = y - overlap.top();
                    memcpy(
                        grown.scanLine(y - next.top()) + (overlap.left() - next.left()) * bytesPerTexel(),
                        mAfter.constScanLine(y - mAfterBox.top()) + (overlap.left() - mAfterBox.left()) * bytesPerTexel(),
                        size_t(overlap.width()) * bytesPerTexel()
                    );
                    (void)rows;
                }
            }
            mAfterBox = next;
            mAfter = grown;
        }
        // copy the freshly painted box out of the field
        const QRect fresh = box;
        for (int y = fresh.top(); y <= fresh.bottom(); ++y) {
            memcpy(
                mAfter.scanLine(y - mAfterBox.top()) + (fresh.left() - mAfterBox.left()) * bytesPerTexel(),
                mTarget.constScanLine(y) + fresh.left() * bytesPerTexel(), size_t(fresh.width()) * bytesPerTexel()
            );
        }
        if (mDone)
            redo();
        else
            mDone = true;
    }

    bool hasPixels() const { return !mAfterBox.isEmpty() && !mAfter.isNull(); }

    QString name() const override { return QStringLiteral("Paint filter mask"); }
    bool isUseless() const override { return !hasPixels(); }

    virtual void exec() override { redo(); }

    virtual void undo() override {
        if (mBefore.isNull())
            return;
        mTarget = mBefore.copy();
        mMask.markDirtyAll(); // the whole field was restored
    }

    virtual void redo() override {
        if (!hasPixels())
            return;
        for (int y = 0; y <= mAfterBox.height() - 1; ++y) {
            memcpy(
                mTarget.scanLine(mAfterBox.top() + y) + mAfterBox.left() * bytesPerTexel(),
                mAfter.constScanLine(y), size_t(mAfterBox.width()) * bytesPerTexel()
            );
        }
        mMask.markDirty(mAfterBox);
    }

private:
    int bytesPerTexel() const { return mTarget.depth() / 8; }

    core::FilterMask& mMask;
    QImage& mTarget;
    QImage mBefore;
    QImage mAfter;
    QRect mAfterBox;
    bool mDone;
};

} // namespace ctrl

#endif // CTRL_ASSIGNPIXELS_H
