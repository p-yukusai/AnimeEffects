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
        // a small horizontal tolerance so near-miss clicks still select
        const int beginFrame = mScale.frame((aPoint.x() - 2) - mMargin);
        const int endFrame = mScale.frame((aPoint.x() + 2) - mMargin);
        // the key diamond is ~8px tall; clicks land on its visible shape, so
        // accept lane centers within half of that (the old Focuser used the
        // same radius)
        constexpr int kHitRadius = 5;

        Hit hit;
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

                    const float height = line.keyHeight(validIndex, validNum);
                    ++validIndex;
                    if (height < aPoint.y() - kHitRadius || aPoint.y() + kHitRadius < height)
                        continue;

                    auto itr = map.lowerBound(beginFrame);
                    if (itr != map.end() && itr.key() <= endFrame) {
                        hit.node = node;
                        hit.pos.setLine(&timeLine);
                        hit.pos.setType(type);
                        hit.pos.setIndex(itr.key());
                        return hit;
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
