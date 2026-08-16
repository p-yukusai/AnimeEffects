#include "ctrl/time/time_Marquee.h"

using namespace core;

namespace ctrl {
namespace time {

    //-------------------------------------------------------------------------------------------------
    Marquee::Marquee(const QVector<TimeLineRow>& aRows, const Scaler& aScale, int aMargin):
        mRows(aRows),
        mScale(aScale),
        mMargin(aMargin),
        mAnchor(),
        mCurrent(),
        mActive(false) {}

    util::Range Marquee::frameRange() const {
        const QRect box = rect();
        const int frame0 = mScale.frame(box.left() - mMargin);
        const int frame1 = mScale.frame(box.right() - mMargin);
        return util::Range(std::min(frame0, frame1), std::max(frame0, frame1));
    }

    void Marquee::begin(const QPoint& aPoint) {
        mAnchor = aPoint;
        mCurrent = aPoint;
        mActive = true;
    }

    void Marquee::update(const QPoint& aPoint) {
        if (!mActive)
            return;
        mCurrent = aPoint;
    }

    void Marquee::clear() {
        mAnchor = QPoint();
        mCurrent = QPoint();
        mActive = false;
    }

    Hit Marquee::hitTest(const QPoint& aPoint) const {
        // Fixed screen-space grab radius (px), independent of zoom. The key
        // markers are ~6px across (the diamond's flat-to-flat and the ellipse's
        // diameter); 12px doubles that, so a whole marker plus a generous margin
        // is clickable. Erring large is deliberate: a false hit on the gate
        // (accidentally grabbing a key when starting a marquee/deselect) is
        // rarer than a false miss, since those gestures start well away from
        // keys. The gate is a plain distance test: a key is selectable iff it
        // is within the radius, and the nearest such key wins. Frame conversion
        // below is only a pruning bound for the sorted key maps, never the
        // tolerance itself.
        constexpr int kHitRadius = 12;
        const float radiusSq = static_cast<float>(kHitRadius * kHitRadius);

        // Frame window that is a superset of every key within the radius in X.
        // frame() rounds to nearest, so widen by one frame per side: a round at
        // either edge could otherwise drop a boundary key.
        const int beginFrame = mScale.frame((aPoint.x() - kHitRadius) - mMargin) - 1;
        const int endFrame = mScale.frame((aPoint.x() + kHitRadius) - mMargin) + 1;

        Hit hit;
        float bestDistSq = radiusSq; // "no hit yet" sentinel == the gate radius

        for (const TimeLineRow& line : mRows) {
            ObjectNode::Iterator nodeItr(line.node);
            while (nodeItr.hasNext()) {
                ObjectNode* node = nodeItr.next();
                XC_PTR_ASSERT(node->timeLine());
                TimeLine& timeLine = *(node->timeLine());
                const int validNum = timeLine.validTypeCount();
                int validIndex = 0;

                for (int i = 0; i < TimeKeyType_TERM; ++i) {
                    auto type = TimeLine::getTimeKeyTypeInOrderOfOperations(i);
                    const TimeLine::MapType& map = timeLine.map(type);
                    if (map.isEmpty())
                        continue;

                    const float keyY = line.keyHeight(validIndex, validNum);
                    ++validIndex;

                    // a lane whose vertical gap alone already reaches the best
                    // hit can never contain a nearer key (dx^2 >= 0)
                    const float dy = keyY - aPoint.y();
                    if (dy * dy >= bestDistSq)
                        continue;

                    auto itr = map.lowerBound(beginFrame);
                    while (itr != map.end() && itr.key() <= endFrame) {
                        const int frame = itr.key();
                        const float dx = (mMargin + mScale.pixelWidth(frame)) - aPoint.x();
                        const float distSq = dx * dx + dy * dy;
                        // strict < resolves ties to iteration order (deterministic)
                        if (distSq < bestDistSq) {
                            bestDistSq = distSq;
                            hit.node = node;
                            hit.pos.setLine(&timeLine);
                            hit.pos.setType(type);
                            hit.pos.setIndex(frame);
                        }
                        ++itr;
                    }
                }
                if (!line.closedFolder)
                    break;
            }
        }
        return hit;
    }

    void Marquee::gather(core::TimeLineEvent& aEvent) const {
        const QRect box = rect();
        const util::Range frames = frameRange();

        for (const TimeLineRow& line : mRows) {
            if (!line.rect.intersects(box))
                continue;

            ObjectNode::Iterator nodeItr(line.node);
            while (nodeItr.hasNext()) {
                ObjectNode* node = nodeItr.next();
                XC_PTR_ASSERT(node->timeLine());
                const TimeLine& timeLine = *(node->timeLine());
                const int validNum = timeLine.validTypeCount();
                int validIndex = 0;

                for (int i = 0; i < TimeKeyType_TERM; ++i) {
                    auto type = TimeLine::getTimeKeyTypeInOrderOfOperations(i);
                    const TimeLine::MapType& map = timeLine.map(type);
                    if (map.isEmpty())
                        continue;

                    const float height = line.keyHeight(validIndex, validNum);
                    ++validIndex;
                    if (height < box.top() || box.bottom() < height)
                        continue;

                    auto itr = map.lowerBound(frames.min());
                    while (itr != map.end() && itr.key() <= frames.max()) {
                        if (itr.value()) {
                            aEvent.pushTarget(*node, type, itr.key());
                        }
                        ++itr;
                    }
                }
                if (!line.closedFolder)
                    break;
            }
        }
    }

} // namespace time
} // namespace ctrl
