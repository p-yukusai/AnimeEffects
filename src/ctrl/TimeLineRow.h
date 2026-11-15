#ifndef CTRL_TIMELINEROW_H
#define CTRL_TIMELINEROW_H

#include <QRect>
#include "core/ObjectNode.h"

namespace ctrl {

class TimeLineRow {
public:
    enum { kHeight = 22, kIncrease = 18, kFolderSummaryHeight = 22 };

    core::ObjectNode* node;
    QRect rect;
    bool closedFolder;
    bool selecting;
    // A COLLAPSED folder with keys somewhere below it gets a dedicated summary
    // lane (kFolderSummaryHeight) under its own lanes. Computed once, because
    // both the lane model and the hit test read it every frame.
    bool summary;

    TimeLineRow();
    TimeLineRow(core::ObjectNode* aNode, const QRect& aRect, bool aClosedFolder, bool aSelecting);

    // The folder's own animation lanes (above the summary band, when there is one).
    QRect ownRect() const;
    // The summary band (empty when this row has no summary). A folder with no
    // own timeline types and a summary uses the WHOLE row for the summary: an
    // empty lane above it would only waste height.
    QRect summaryRect() const;
    int summaryHeight() const;
    float summaryCenterY() const;

    float keyHeight(int aIndex, int aValidCount) const;
    static int calculateHeight(const core::ObjectNode& aNode);
    // Any key in any descendant timeline (the folder's own keys do not count:
    // its row already draws them).
    static bool hasDescendantKeys(const core::ObjectNode& aNode);
};

} // namespace ctrl

#endif // CTRL_TIMELINEROW_H
