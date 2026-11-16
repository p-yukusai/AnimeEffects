#ifndef GUI_ANIMATIONROW_H
#define GUI_ANIMATIONROW_H

#include <QVector>

namespace core {
class ObjectNode;
}

namespace gui {

// ONE ROW OF THE ANIMATION DOCK, as BOTH columns see it.
//
// The tree column and the lane column are one row list: a row's height is the height of
// the lane block beside it (a layer with five animated channels is 90px in both), which is
// why the height has exactly one definition in the code -- ctrl::TimeLineRow::displayHeight.
//
// WHAT CHANGED, AND WHY IT MATTERS. The lanes used to be pushed from the WIDGET's geometry
// (gui::ObjectTreeWidget's items, via TimeLineEditorWidget::updateLinesRecursive reading
// obj::Item::visualRect()), so the timeline's row model was a projection of a QTreeWidget's
// layout - the view WAS the document as far as the lanes were concerned. The rows are now
// produced by gui::ObjectTreeModel from the document (node, expansion) and consumed by that
// model's QML scene and by the timeline's controller from the same value. Top/bottom are
// CONTENT pixels: y = 0 is the top of the first row, and the timeline's camera translates
// them, so nothing here depends on either view's scroll position.
struct AnimationRow {
    core::ObjectNode* node = nullptr;
    int top = 0;
    int height = 0;
    // a folder with children, collapsed: its descendants draw in one summary band
    bool closedFolder = false;
    // the document root: drawn in the tree, NOT a timeline row (a document is not a layer)
    bool isDocument = false;
    // nesting depth in the tree, 0 for the document row
    int depth = 0;

    int bottom() const { return top + height - 1; }
};

using AnimationRows = QVector<AnimationRow>;

} // namespace gui

#endif // GUI_ANIMATIONROW_H
