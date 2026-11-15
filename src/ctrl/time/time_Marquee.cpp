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

    Hit Marquee::hitTest(const QPoint& aPoint, const float scaleFactor) const {
        // Hit radius now takes a scale factor dependent on the key renderer
        const float kHitRadius = 12 * scaleFactor;
        const float radiusSq = kHitRadius * kHitRadius;

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
                const int validNum = timeLine.validChannelCount();
                int validIndex = 0;

                // A closed folder's descendants are drawn in its summary lane, not
                // in the folder's own lanes: the hit must use that band's centre.
                const bool summaryChild = line.closedFolder && line.summaryHeight() > 0 && node != line.node;

                for (int i = 0; i < TimeKeyType_TERM; ++i) {
                    auto type = TimeLine::getTimeKeyTypeInOrderOfOperations(i);

                    // A state key is part of the property, drawn at its centre.
                    // It is independently selectable when it is not already
                    // represented by a linked property marker.
                    const PropertyTrack* stateProp = timeLine.property(type);
                    if (stateProp && type != core::TimeKeyType_Image
                        && stateProp->state().keyCount() > 0 && !summaryChild) {
                        const QRect own = line.ownRect();
                        const float stateY = own.top() + 0.5f * own.height();
                        const float dy = stateY - aPoint.y();
                        if (dy * dy < bestDistSq) {
                            for (const TimeKey* stateKey : stateProp->state().keys()) {
                                const int frame = stateKey->frame();
                                if (frame < beginFrame || endFrame < frame
                                    || timeLine.isFrameLinked(type, frame)) {
                                    continue;
                                }
                                const float dx = (mMargin + mScale.pixelWidth(frame)) - aPoint.x();
                                const float distSq = dx * dx + dy * dy;
                                if (distSq < bestDistSq) {
                                    bestDistSq = distSq;
                                    hit.node = node;
                                    hit.pos.setLine(&timeLine);
                                    hit.pos.setType(type);
                                    hit.pos.setIndex(frame);
                                    hit.pos.setComponent(TimeKeyPos::kStateComponent);
                                }
                            }
                        }
                    }

                    if (type == core::TimeKeyType_Image) {
                        const TimeLine::MapType& imageMap = timeLine.map(type);
                        if (!imageMap.isEmpty()) {
                            const float keyY = summaryChild ? line.summaryCenterY()
                                                            : line.keyHeight(validIndex, validNum);
                            ++validIndex;
                            const float dy = keyY - aPoint.y();
                            if (dy * dy < bestDistSq) {
                                auto itr = imageMap.lowerBound(beginFrame);
                                while (itr != imageMap.end() && itr.key() <= endFrame) {
                                    const int frame = itr.key();
                                    const float dx = (mMargin + mScale.pixelWidth(frame)) - aPoint.x();
                                    const float distSq = dx * dx + dy * dy;
                                    if (distSq < bestDistSq) {
                                        bestDistSq = distSq;
                                        hit.node = node;
                                        hit.pos.setLine(&timeLine);
                                        hit.pos.setType(type);
                                        hit.pos.setIndex(frame);
                                        hit.pos.setComponent(-1);
                                    }
                                    ++itr;
                                }
                            }
                        }
                        for (const TimeLine::ChannelLane& channelLane : timeLine.channelLanes(type)) {
                            const float keyY = summaryChild ? line.summaryCenterY()
                                                            : line.keyHeight(validIndex, validNum);
                            ++validIndex;
                            const float dy = keyY - aPoint.y();
                            if (dy * dy >= bestDistSq) {
                                continue;
                            }
                            const ChannelTrack* track = timeLine.channelTrack(type, channelLane.component);
                            if (!track) {
                                continue;
                            }
                            auto itr = track->keys().lowerBound(beginFrame);
                            while (itr != track->keys().end() && itr.key() <= endFrame) {
                                const int frame = itr.key();
                                const float dx = (mMargin + mScale.pixelWidth(frame)) - aPoint.x();
                                const float distSq = dx * dx + dy * dy;
                                if (distSq < bestDistSq) {
                                    bestDistSq = distSq;
                                    hit.node = node;
                                    hit.pos.setLine(&timeLine);
                                    hit.pos.setType(type);
                                    hit.pos.setIndex(frame);
                                    hit.pos.setComponent(channelLane.component);
                                }
                                ++itr;
                            }
                        }
                        continue;
                    }

                    if (TimeLine::usesChannelTracks(type)) {
                        const PropertyTrack* prop = timeLine.property(type);
                        if (!prop) {
                            continue;
                        }
                        const QVector<TimeLine::ChannelLane> lanes = timeLine.channelLanes(type);
                        const bool grouped = timeLine.channelsGrouped(type);
                        if (!grouped && !summaryChild && !lanes.isEmpty()) {
                            const ChannelTrack* first = nullptr;
                            for (int c = 0; c < prop->channelCount() && !first; ++c) {
                                const ChannelTrack* candidate = prop->channel(c);
                                if (candidate && candidate->spec().graphable
                                    && !candidate->isEmpty()) {
                                    first = candidate;
                                }
                            }
                            const QRect own = line.ownRect();
                            const float centreY = own.top() + 0.5f * own.height();
                            const float dy = centreY - aPoint.y();
                            if (first && dy * dy < bestDistSq) {
                                auto trackItr = first->keys().lowerBound(beginFrame);
                                while (trackItr != first->keys().end() && trackItr.key() <= endFrame) {
                                    if (timeLine.isFrameLinked(type, trackItr.key())) {
                                        const int frame = trackItr.key();
                                        const float dx =
                                            (mMargin + mScale.pixelWidth(frame)) - aPoint.x();
                                        const float distSq = dx * dx + dy * dy;
                                        if (distSq < bestDistSq) {
                                            bestDistSq = distSq;
                                            hit.node = node;
                                            hit.pos.setLine(&timeLine);
                                            hit.pos.setType(type);
                                            hit.pos.setIndex(frame);
                                            hit.pos.setComponent(0);
                                        }
                                    }
                                    ++trackItr;
                                }
                            }
                        }
                        for (const TimeLine::ChannelLane& channelLane : lanes) {
                            const ChannelTrack* track = nullptr;
                            if (channelLane.grouped) {
                                for (int c = 0; c < prop->channelCount() && !track; ++c) {
                                    const ChannelTrack* candidate = prop->channel(c);
                                    if (candidate && candidate->spec().graphable
                                        && !candidate->isEmpty()) {
                                        track = candidate;
                                    }
                                }
                            } else {
                                track = prop->channel(channelLane.component);
                            }
                            if (!track || track->isEmpty()) {
                                continue;
                            }
                            const float keyY = summaryChild ? line.summaryCenterY()
                                                            : line.keyHeight(validIndex, validNum);
                            ++validIndex;
                            const float dy = keyY - aPoint.y();
                            if (dy * dy >= bestDistSq) {
                                continue;
                            }
                            auto trackItr = track->keys().lowerBound(beginFrame);
                            while (trackItr != track->keys().end() && trackItr.key() <= endFrame) {
                                const int frame = trackItr.key();
                                if (!grouped && !summaryChild
                                    && timeLine.isFrameLinked(type, frame)) {
                                    ++trackItr;
                                    continue;
                                }
                                const float dx = (mMargin + mScale.pixelWidth(frame)) - aPoint.x();
                                const float distSq = dx * dx + dy * dy;
                                if (distSq < bestDistSq) {
                                    bestDistSq = distSq;
                                    hit.node = node;
                                    hit.pos.setLine(&timeLine);
                                    hit.pos.setType(type);
                                    hit.pos.setIndex(frame);
                                    hit.pos.setComponent(
                                        channelLane.grouped ? 0 : channelLane.component
                                    );
                                }
                                ++trackItr;
                            }
                        }
                        continue;
                    }

                    const TimeLine::MapType& map = timeLine.map(type);
                    if (map.isEmpty())
                        continue;

                    const float keyY = summaryChild ? line.summaryCenterY()
                                                    : line.keyHeight(validIndex, validNum);
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
                            hit.pos.setComponent(-1);
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
            // The box gathers rows by their vertical band; the horizontal
            // extent of the box is never bounded by the frame range (the
            // frame window below handles key culling), so out-of-range keys
            // select normally.
            if (box.bottom() < line.rect.top() || line.rect.bottom() < box.top())
                continue;

            ObjectNode::Iterator nodeItr(line.node);
            while (nodeItr.hasNext()) {
                ObjectNode* node = nodeItr.next();
                XC_PTR_ASSERT(node->timeLine());
                TimeLine& timeLine = *(node->timeLine());
                const int validNum = timeLine.validChannelCount();
                int validIndex = 0;

                const bool summaryChild = line.closedFolder && line.summaryHeight() > 0 && node != line.node;

                for (int i = 0; i < TimeKeyType_TERM; ++i) {
                    auto type = TimeLine::getTimeKeyTypeInOrderOfOperations(i);

                    const PropertyTrack* stateProp = timeLine.property(type);
                    if (stateProp && type != core::TimeKeyType_Image
                        && stateProp->state().keyCount() > 0 && !summaryChild) {
                        const QRect own = line.ownRect();
                        const float stateY = own.top() + 0.5f * own.height();
                        if (box.top() <= stateY && stateY <= box.bottom()) {
                            for (const TimeKey* stateKey : stateProp->state().keys()) {
                                const int frame = stateKey->frame();
                                if (frame < frames.min() || frames.max() < frame
                                    || timeLine.isFrameLinked(type, frame)) {
                                    continue;
                                }
                                aEvent.pushTarget(
                                    *node,
                                    TimeKeyPos(timeLine, type, frame, TimeKeyPos::kStateComponent)
                                );
                            }
                        }
                    }

                    if (type == core::TimeKeyType_Image) {
                        const TimeLine::MapType& imageMap = timeLine.map(type);
                        if (!imageMap.isEmpty()) {
                            const float height = summaryChild ? line.summaryCenterY()
                                                              : line.keyHeight(validIndex, validNum);
                            ++validIndex;
                            if (box.top() <= height && height <= box.bottom()) {
                                auto itr = imageMap.lowerBound(frames.min());
                                while (itr != imageMap.end() && itr.key() <= frames.max()) {
                                    aEvent.pushTarget(*node, type, itr.key());
                                    ++itr;
                                }
                            }
                        }
                        for (const TimeLine::ChannelLane& channelLane : timeLine.channelLanes(type)) {
                            const float height = summaryChild ? line.summaryCenterY()
                                                              : line.keyHeight(validIndex, validNum);
                            ++validIndex;
                            if (height < box.top() || box.bottom() < height) {
                                continue;
                            }
                            const ChannelTrack* track = timeLine.channelTrack(type, channelLane.component);
                            if (!track) {
                                continue;
                            }
                            auto itr = track->keys().lowerBound(frames.min());
                            while (itr != track->keys().end() && itr.key() <= frames.max()) {
                                aEvent.pushTarget(
                                    *node,
                                    TimeKeyPos(timeLine, type, itr.key(), channelLane.component)
                                );
                                ++itr;
                            }
                        }
                        continue;
                    }

                    if (TimeLine::usesChannelTracks(type)) {
                        const PropertyTrack* prop = timeLine.property(type);
                        if (!prop) {
                            continue;
                        }
                        const QVector<TimeLine::ChannelLane> lanes = timeLine.channelLanes(type);
                        const bool grouped = timeLine.channelsGrouped(type);
                        if (!grouped && !summaryChild && !lanes.isEmpty()) {
                            const ChannelTrack* first = nullptr;
                            for (int c = 0; c < prop->channelCount() && !first; ++c) {
                                const ChannelTrack* candidate = prop->channel(c);
                                if (candidate && candidate->spec().graphable
                                    && !candidate->isEmpty()) {
                                    first = candidate;
                                }
                            }
                            const QRect own = line.ownRect();
                            const float centreY = own.top() + 0.5f * own.height();
                            if (first && box.top() <= centreY && centreY <= box.bottom()) {
                                auto trackItr = first->keys().lowerBound(frames.min());
                                while (trackItr != first->keys().end()
                                       && trackItr.key() <= frames.max()) {
                                    if (timeLine.isFrameLinked(type, trackItr.key())) {
                                        aEvent.pushTarget(
                                            *node,
                                            TimeKeyPos(timeLine, type, trackItr.key(), 0)
                                        );
                                    }
                                    ++trackItr;
                                }
                            }
                        }
                        for (const TimeLine::ChannelLane& channelLane : lanes) {
                            const ChannelTrack* track = nullptr;
                            if (channelLane.grouped) {
                                for (int c = 0; c < prop->channelCount() && !track; ++c) {
                                    const ChannelTrack* candidate = prop->channel(c);
                                    if (candidate && candidate->spec().graphable
                                        && !candidate->isEmpty()) {
                                        track = candidate;
                                    }
                                }
                            } else {
                                track = prop->channel(channelLane.component);
                            }
                            if (!track || track->isEmpty()) {
                                continue;
                            }
                            const float height = summaryChild ? line.summaryCenterY()
                                                              : line.keyHeight(validIndex, validNum);
                            ++validIndex;
                            if (height < box.top() || box.bottom() < height) {
                                continue;
                            }
                            auto trackItr = track->keys().lowerBound(frames.min());
                            while (trackItr != track->keys().end() && trackItr.key() <= frames.max()) {
                                if (!grouped && !summaryChild
                                    && timeLine.isFrameLinked(type, trackItr.key())) {
                                    ++trackItr;
                                    continue;
                                }
                                aEvent.pushTarget(
                                    *node,
                                    TimeKeyPos(
                                        timeLine, type, trackItr.key(),
                                        channelLane.grouped ? 0 : channelLane.component
                                    )
                                );
                                ++trackItr;
                            }
                        }
                        continue;
                    }

                    const TimeLine::MapType& map = timeLine.map(type);
                    if (map.isEmpty())
                        continue;

                    const float height = summaryChild ? line.summaryCenterY()
                                                      : line.keyHeight(validIndex, validNum);
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
