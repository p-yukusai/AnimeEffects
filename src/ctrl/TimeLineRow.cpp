#include "ctrl/TimeLineRow.h"

namespace ctrl {

TimeLineRow::TimeLineRow():
    node(nullptr), rect(), closedFolder(), selecting(), summary() {}

TimeLineRow::TimeLineRow(core::ObjectNode* aNode, const QRect& aRect, bool aClosedFolder, bool aSelecting):
    node(aNode), rect(aRect), closedFolder(aClosedFolder), selecting(aSelecting),
    summary(aClosedFolder && aNode && hasDescendantKeys(*aNode)) {}

bool TimeLineRow::hasDescendantKeys(const core::ObjectNode& aNode) {
    for (auto child : aNode.children()) {
        core::ObjectNode::ConstIterator treeItr(child);
        while (treeItr.hasNext()) {
            const core::ObjectNode* node = treeItr.next();
            if (node && node->timeLine() && !node->timeLine()->isEmpty()) {
                return true;
            }
        }
    }
    return false;
}

int TimeLineRow::summaryHeight() const {
    return summary ? kFolderSummaryHeight : 0;
}

QRect TimeLineRow::ownRect() const {
    return QRect(rect.left(), rect.top(), rect.width(), rect.height() - summaryHeight());
}

QRect TimeLineRow::summaryRect() const {
    const int height = summaryHeight();
    if (height == 0) {
        return QRect();
    }
    return QRect(rect.left(), rect.bottom() - height + 1, rect.width(), height);
}

float TimeLineRow::summaryCenterY() const {
    const QRect area = summaryRect();
    return area.top() + 0.5f * area.height();
}

float TimeLineRow::keyHeight(int aIndex, int aValidCount) const {
    const QRect lane = ownRect();
    if (aValidCount <= 1 || (node && node->isSlimmedDown())) {
        return lane.top() + 0.5f * lane.height();
    } else {
        return lane.top() + 0.5f * kIncrease + aIndex * (float)lane.height() / aValidCount;
    }
}

int TimeLineRow::calculateHeight(const core::ObjectNode& aNode) {
    if (!aNode.timeLine())
        return kHeight;

    auto validCount = aNode.timeLine()->validTypeCount();
    if (validCount <= 1 || aNode.isSlimmedDown()) {
        return kHeight;
    } else {
        return kIncrease * validCount;
    }
}

} // namespace ctrl
