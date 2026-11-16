#include "ctrl/TimeLineRow.h"

namespace ctrl {

TimeLineRow::TimeLineRow():
    node(nullptr), rect(), closedFolder(), selecting(), summary() {}

TimeLineRow::TimeLineRow(core::ObjectNode* aNode, const QRect& aRect, bool aClosedFolder, bool aSelecting):
    node(aNode), rect(aRect), closedFolder(aClosedFolder), selecting(aSelecting),
    // THE BAND IS NOT A COLLAPSE AFFORDANCE. It used to exist only while the folder was closed,
    // so expanding one made its row 22px shorter and every lane under it jumped. It is a
    // property of the FOLDER (it has keyed descendants), which is also what keeps the tree row
    // and the lane block the same height in both states.
    summary(aNode && hasDescendantKeys(*aNode)) {}

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

int TimeLineRow::displayHeight(const core::ObjectNode& aNode, bool aExpanded) {
    // `aExpanded` no longer changes the height: the summary band is a permanent part of a folder
    // that has keyed descendants (see TimeLineRow's constructor), so expanding never shifts the
    // layout. Kept in the signature because every caller asks the same question and the answer
    // being expansion-independent is the point.
    (void)aExpanded;
    const int base = calculateHeight(aNode);
    if (!aNode.parent() || !hasDescendantKeys(aNode))
        return base;
    const bool hasOwnLanes = aNode.timeLine() && aNode.timeLine()->validChannelCount() > 0;
    return hasOwnLanes ? base + kFolderSummaryHeight : kFolderSummaryHeight;
}

int TimeLineRow::calculateHeight(const core::ObjectNode& aNode) {
    if (!aNode.timeLine())
        return kHeight;

    auto validCount = aNode.timeLine()->validChannelCount();
    if (validCount <= 1 || aNode.isSlimmedDown()) {
        return kHeight;
    } else {
        return kIncrease * validCount;
    }
}

} // namespace ctrl
