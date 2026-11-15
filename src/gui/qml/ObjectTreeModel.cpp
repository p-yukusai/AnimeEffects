#include "gui/qml/ObjectTreeModel.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <algorithm>
#include <functional>

#include "ctrl/TimeLineEditor.h"
#include "cmnd/ScopedMacro.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/TreeActions.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "core/ObjectTree.h"
#include "core/TimeLineEvent.h"
#include "core/ObjectTreeEvent.h"
#include "core/ImageKey.h"
#include "core/MaskKey.h"
#include "core/ResourceEvent.h"
#include "img/ResourceNode.h"
#include "core/TimeLine.h"
#include <QFileInfo>
#include <QFontMetrics>
#include <QVariantList>
#include <QVariantMap>

namespace gui {
namespace {

// The document row: the tree's own header (a document is not a layer). 22px, the row
// height every other row starts from.
constexpr int kDocumentRowHeight = ctrl::TimeLineRow::kHeight;
// The thumbnail cell's logical box (the row's 22px icon slot, 1px of air on each side).
constexpr int kThumbBox = ctrl::TimeLineRow::kHeight - 2;
// The matte chip, in logical pixels: the same 14px the widget drew.
constexpr int kMatteChip = 14;


// One resource's image, when the node has one: the Image default key carries it.
const img::ResourceData* layerResource(const core::ObjectNode& aNode) {
    const core::TimeLine* line = aNode.timeLine();
    if (!line)
        return nullptr;
    const core::TimeKey* key = line->defaultKey(core::TimeKeyType_Image);
    if (!key || key->type() != core::TimeKeyType_Image)
        return nullptr;
    const auto* image = static_cast<const core::ImageKey*>(key);
    return image->data().resource().get();
}

QString urlFor(const char* aScheme, int aRow, quint64 aKey) {
    return QStringLiteral("image://%1/%2?%3")
        .arg(QString::fromLatin1(aScheme))
        .arg(aRow)
        .arg(aKey, 0, 16);
}

} // namespace

//-------------------------------------------------------------------------------------------------
ObjectTreeModel::ObjectTreeModel(QObject* aParent):
    QAbstractListModel(aParent),
    mProject(),
    mTimeLineSlot(),
    mTreeSlot(),
    mNodeAttrSlot(),
    mResourceSlot(),
    mRows(),
    mContract(),
    mExpanded(),
    mSelection(),
    mAnchor(nullptr),
    mCurrent(nullptr),
    mCollapseNode(nullptr),
    mRenamingRow(-1),
    mFocused(false),
    mScrollOffset(0),
    mViewportHeight(0),
    mDrag(),
    mDragDwell(),
    mDragClock(),
    mThumbnails(),
    mPixelRatio(1.0),
    mMatteRevision(0),
    mMatteCache() {}

ObjectTreeModel::~ObjectTreeModel() { setProject(nullptr); }

//-------------------------------------------------------------------------------------------------
// the document
//-------------------------------------------------------------------------------------------------
void ObjectTreeModel::setProject(core::Project* aProject) {
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mTimeLineSlot);
        mProject->onTreeRestructured.disconnect(mTreeSlot);
        mProject->onNodeAttributeModified.disconnect(mNodeAttrSlot);
        mProject->onResourceModified.disconnect(mResourceSlot);
    }
    if (aProject)
        mProject = aProject->pointee();
    else
        mProject.reset();

    mExpanded.clear();
    mSelection.clear();
    mAnchor = nullptr;
    mCurrent = nullptr;
    mCollapseNode = nullptr;
    mRenamingRow = -1;
    mDrag = DragState();
    mDragDwell = ctrl::autoscroll::EdgeDwell();
    mMatteCache.clear();
    mThumbnails.clear();

    if (mProject) {
        // A freshly opened document shows its layers: the ROOT starts expanded. The widget
        // started every folder collapsed, so the timeline's lanes were empty until the user
        // found the document row's branch indicator -- a dock whose point is the lanes,
        // opening blank. Nested folders keep the collapsed default: their summary band
        // already shows that keys live below them.
        if (core::ObjectNode* top = mProject->objectTree().topNode())
            mExpanded.insert(top);

        mTimeLineSlot = mProject->onTimeLineModified.connect([this](core::TimeLineEvent& aEvent, bool) {
            onTimeLineEvent(aEvent, false);
        });
        mTreeSlot = mProject->onTreeRestructured.connect([this](core::ObjectTreeEvent& aEvent, bool) {
            onTreeEvent(aEvent, false);
        });
        mNodeAttrSlot = mProject->onNodeAttributeModified.connect([this](core::ObjectNode& aNode, bool) {
            refreshRowOf(aNode);
        });
        mResourceSlot = mProject->onResourceModified.connect([this](core::ResourceEvent&, bool) {
            // a reimport hands a layer a NEW buffer: the thumbnail's key changes with it,
            // and the provider is cached per row+key, so the rows just need re-issuing
            mThumbnails.clear();
            if (!mRows.isEmpty())
                emit dataChanged(index(0), index(int(mRows.size()) - 1), {ThumbUrlRole});
        });
    }

    rebuild();
    emitSelectionChanged();
    emit scrollOffsetChanged();
}

void ObjectTreeModel::onTimeLineEvent(core::TimeLineEvent& aEvent, bool) {
    switch (aEvent.type()) {
    case core::TimeLineEvent::Type_PushKey:
    case core::TimeLineEvent::Type_RemoveKey: {
        // A key changes its row's HEIGHT (a channel with a key becomes a lane band) and a
        // collapsed folder's summary may appear or vanish, so the rows are rebuilt. The
        // heights are equal for a summary-only row gaining its first key, which is why
        // this cannot be a "did any height change" test.
        rebuild();
        break;
    }
    case core::TimeLineEvent::Type_ChangeKeyValue: {
        for (const auto& target : aEvent.targets()) {
            if (target.pos.type() == core::TimeKeyType_Mask) {
                invalidateMatte();
                return;
            }
        }
        break;
    }
    default:
        break;
    }
}

void ObjectTreeModel::onTreeEvent(core::ObjectTreeEvent&, bool) {
    // A restructure: the row list is the document's shape, so it is rebuilt (and the
    // selection is pruned to what still exists). Expansion and scroll are kept: they name
    // NODES and a pixel offset, not row indices.
    rebuild();
}

void ObjectTreeModel::frameChanged() {
    // only an ANIMATED matte changes with the frame: a mask lane with one key (or none) is
    // the same picture on every frame, and re-rendering every chip 60 times a second is the
    // one thing this preview must not do
    bool animated = false;
    for (const Row& row : mRows) {
        if (!row.hasMatte || !row.geometry.node)
            continue;
        const core::TimeLine* line = row.geometry.node->timeLine();
        if (line && line->map(core::TimeKeyType_Mask).size() > 1) {
            animated = true;
            break;
        }
    }
    if (animated)
        invalidateMatte();
}

// A ROW HAS A MATTE WHEN ITS NODE HAS A MASK, painted or not. Requiring a stroke meant the chip
// only appeared once the lane had a key - so a freshly added mask showed nothing in the tree at
// all (reported). The neutral field is a picture too (fully visible), which is what the chip shows.
const core::MaskKey::Data* ObjectTreeModel::maskDataOf(const core::ObjectNode& aNode) const {
    // THE DOCUMENT says whether a node has a mask, not the blended frame. `current()` is whatever
    // the last blend produced, so right after the mask is added (and before anything re-blends) it
    // is still empty - which is why the chip appeared only once the lane ALSO had an explicit key
    // (reported). Creating a mask pushes its DEFAULT key, so that is what to look at first.
    const core::TimeLine* line = aNode.timeLine();
    if (!line)
        return nullptr;
    if (const core::MaskKey::Data& blended = line->current().mask(); blended.isValid())
        return &blended;
    if (const core::TimeKey* key = line->defaultKey(core::TimeKeyType_Mask))
        return &static_cast<const core::MaskKey*>(key)->data();
    return nullptr;
}

bool ObjectTreeModel::rowHasMatte(const Row& aRow) const {
    if (!aRow.geometry.node)
        return false;
    return maskDataOf(*aRow.geometry.node) != nullptr;
}

QString ObjectTreeModel::matteUrlFor(const Row& aRow, int aRowIndex) const {
    if (!rowHasMatte(aRow))
        return QString();
    return urlFor("treematte", aRowIndex, quint64(mMatteRevision));
}

void ObjectTreeModel::invalidateMatte() {
    mMatteCache.clear();
    ++mMatteRevision;
    emit matteChanged();
    // BOTH roles: a row that GAINS a matte was built with `hasMatte == false`, and emitting only the
    // url left the chip invisible until the next rebuild (the reported "no mask thumbnail until it
    // has a key").
    for (int i = 0; i < int(mRows.size()); ++i)
        mRows[i].matteUrl = matteUrlFor(mRows[i], i);
    if (!mRows.isEmpty())
        emit dataChanged(index(0), index(int(mRows.size()) - 1), {HasMatteRole, MatteUrlRole});
}

//-------------------------------------------------------------------------------------------------
// the row list
//-------------------------------------------------------------------------------------------------
int ObjectTreeModel::rowCount(const QModelIndex& aParent) const {
    return aParent.isValid() ? 0 : int(mRows.size());
}

int ObjectTreeModel::contentHeight() const {
    return mRows.isEmpty() ? 0 : mRows.last().geometry.top + mRows.last().geometry.height;
}

bool ObjectTreeModel::isEmpty() const { return mRows.isEmpty(); }

int ObjectTreeModel::contentWidth() const { return mContentWidth; }

void ObjectTreeModel::setContentFont(const QFont& aFont) {
    mContentFont = aFont;
    // the rows are laid out by the scene, but the HORIZONTAL RANGE is this model's: re-measure
    // so a locale with a wider face scrolls as far as it needs to
    if (!mRows.isEmpty()) {
        beginResetModel();
        measureRows();
        endResetModel();
    }
}

// The two facts a row cannot know on its own: which indentation trunks continue past it, and
// how wide the widest row is. Both are ONE backwards pass over the finished list.
void ObjectTreeModel::measureRows() {
    const QFontMetrics metrics(mContentFont);
    // the widget's own indent step, caret box and icon slot (see TreeRow.qml's k* constants)
    constexpr int kIndent = 18;
    constexpr int kCaret = 14;
    constexpr int kIcon = 22;
    constexpr int kNameGap = 4;
    constexpr int kTail = 10;

    // guides: ONE LINE PER ANCESTOR LEVEL. A row at depth D sits inside D containers, so it draws
    // the trunk of every level below D - which is what a subtree looks like: the folder it is in
    // has its line through all of its children, and the line ends where the subtree ends,
    // because no later row is at a deeper level. (The first version skipped a level whose node
    // had no FOLLOWING sibling, so the last child of an expanded folder lost its folder's line -
    // reported as "one line fewer than there should be".)
    for (Row& row : mRows) {
        const int depth = std::max(0, row.geometry.depth);
        row.guideMask = depth >= 31 ? 0x7fffffff : ((1 << depth) - 1);
    }

    // width: the deepest indent plus a name as wide as its font makes it
    mNameWidths.resize(mRows.size());
    mContentWidth = 0;
    for (int i = 0; i < int(mRows.size()); ++i) {
        Row& row = mRows[i];
        row.nameWidth = metrics.horizontalAdvance(row.name);
        mNameWidths[i] = row.nameWidth;
        const int width = row.geometry.isDocument
                              ? row.nameWidth + kTail
                              : row.geometry.depth * kIndent + kCaret + 2 + kIcon + kNameGap
                                    + row.nameWidth + kTail;
        mContentWidth = std::max(mContentWidth, width);
    }
    emit contentWidthChanged();
}

QVariant ObjectTreeModel::data(const QModelIndex& aIndex, int aRole) const {
    const Row* row = rowAt(aIndex.row());
    if (!row)
        return QVariant();
    const core::ObjectNode* node = row->geometry.node;

    switch (aRole) {
    case NameRole: return row->name;
    case DepthRole: return row->geometry.depth;
    case RowHeightRole: return row->geometry.height;
    case IsDocumentRole: return row->geometry.isDocument;
    case IsFolderRole: return row->isFolder;
    case HasChildrenRole: return row->hasChildren;
    case ExpandedRole: return row->expanded;
    case SelectedRole: return isSelected(node);
    case CurrentRole: return mFocused && node == mCurrent;
    case VisibleRole: return node ? node->isVisible() : true;
    case SlimmedRole: return node ? node->isSlimmedDown() : false;
    case GlyphRole: return row->isFolder ? QStringLiteral("folder") : QStringLiteral("file");
    case ThumbUrlRole: return row->thumbUrl;
    case ThumbWidthRole: return row->thumbWidth;
    case ThumbHeightRole: return row->thumbHeight;
    case HasMatteRole: return rowHasMatte(*row);
    case MatteUrlRole: return row->matteUrl;
    case RenamingRole: return aIndex.row() == mRenamingRow;
    case GuideMaskRole: return row->guideMask;
    case DropZoneRole: {
        if (!mDrag.active || aIndex.row() != mDrag.targetRow)
            return int(DropNone);
        return int(mDrag.zone);
    }
    default: return QVariant();
    }
}

QHash<int, QByteArray> ObjectTreeModel::roleNames() const {
    return QHash<int, QByteArray>{
        {NameRole, "name"},
        {DepthRole, "depth"},
        {RowHeightRole, "rowHeight"},
        {IsDocumentRole, "isDocument"},
        {IsFolderRole, "isFolder"},
        {HasChildrenRole, "hasChildren"},
        {ExpandedRole, "expanded"},
        {SelectedRole, "selected"},
        {CurrentRole, "current"},
        {VisibleRole, "nodeVisible"},
        {SlimmedRole, "slimmed"},
        {GlyphRole, "glyph"},
        {ThumbUrlRole, "thumbUrl"},
        {ThumbWidthRole, "thumbWidth"},
        {ThumbHeightRole, "thumbHeight"},
        {HasMatteRole, "hasMatte"},
        {MatteUrlRole, "matteUrl"},
        {RenamingRole, "renaming"},
        {DropZoneRole, "dropZone"},
        {GuideMaskRole, "guideMask"},
    };
}

const ObjectTreeModel::Row* ObjectTreeModel::rowAt(int aIndex) const {
    if (aIndex < 0 || aIndex >= int(mRows.size()))
        return nullptr;
    return &mRows[aIndex];
}

bool ObjectTreeModel::isSelectableRow(int aIndex) const {
    const Row* row = rowAt(aIndex);
    // the document row is the document, not a layer: it expands and it owns the asset
    // actions, but it can never be "the selected layer" (selecting it used to empty the
    // inspector, which read as "nothing is loaded")
    return row && row->geometry.node && !row->geometry.isDocument;
}

int ObjectTreeModel::rowOf(const core::ObjectNode* aNode) const {
    if (!aNode)
        return -1;
    for (int i = 0; i < int(mRows.size()); ++i) {
        if (mRows[i].geometry.node == aNode)
            return i;
    }
    return -1;
}

void ObjectTreeModel::rebuild() {
    // The rows are derived, so a rebuild is a reset. Selection, expansion and the scroll
    // offset are keyed by NODE and by pixels, so they survive it - and the offset is re-announced
    // below, because the views themselves cannot keep it across a reset.
    const int keepOffset = mScrollOffset;
    mRebuilding = true;
    beginResetModel();
    mRows.clear();
    mContract.clear();

    if (mProject) {
        if (core::ObjectNode* top = mProject->objectTree().topNode()) {
            int topOffset = 0;
            appendRow(*top, 0, true, topOffset);
        }
    }

    measureRows();

    mContract.reserve(mRows.size());
    for (const Row& row : mRows)
        mContract.append(row.geometry);

    // Drop the selection entries whose node left the document - but KEEP one that a COLLAPSED
    // folder now stands for: its keys still draw in that folder's summary band, and the timeline
    // keeps it too (ctrl::TimeLineEditor::retainSelectionInRows has the same rule). A collapse is
    // a view change, not a deselect.
    QSet<const core::ObjectNode*> represented;
    for (const Row& row : mRows) {
        if (row.geometry.node) {
            represented.insert(row.geometry.node);
            if (row.geometry.closedFolder) {
                for (core::ObjectNode::Iterator itr(const_cast<core::ObjectNode*>(row.geometry.node));
                     itr.hasNext();) {
                    if (core::ObjectNode* below = itr.next())
                        represented.insert(below);
                }
            }
        }
    }
    QVector<core::ObjectNode*> kept;
    for (core::ObjectNode* node : mSelection) {
        if (represented.contains(node))
            kept.append(node);
    }
    const bool selectionLost = kept.size() != mSelection.size();
    mSelection = kept;
    if (mAnchor && rowOf(mAnchor) < 0)
        mAnchor = nullptr;
    if (mCurrent && rowOf(mCurrent) < 0)
        mCurrent = nullptr;
    if (mCollapseNode && rowOf(mCollapseNode) < 0)
        mCollapseNode = nullptr;
    if (!isRenaming() || mRenamingRow >= int(mRows.size()))
        mRenamingRow = -1;

    endResetModel();   // the views drop their contentY here; nothing is recorded (mRebuilding)

    mRebuilding = false;
    // The offset survived the reset, only its RANGE may have moved (a key add makes a row taller).
    mScrollOffset = std::max(0, std::min(keepOffset, maxScrollOffset()));

    emit rowsChanged();
    // re-announced LAST, so the panel can put contentY back and the timeline never sees a zero
    emit scrollOffsetChanged();
    if (selectionLost)
        emitSelectionChanged();
}

void ObjectTreeModel::appendRow(core::ObjectNode& aNode, int aDepth, bool aIsDocument, int& aTop) {
    Row row;
    row.geometry.node = &aNode;
    row.geometry.top = aTop;
    row.geometry.depth = aDepth;
    row.geometry.isDocument = aIsDocument;
    row.name = aNode.name();
    row.isFolder = aNode.canHoldChild();
    row.hasChildren = !aNode.children().empty();
    // the root's expansion is the document's own; every other row's is this model's state
    row.expanded = mExpanded.contains(&aNode);
    // a closed container "represents" its descendants (see rebuild), and the document row is a
    // container like any other for that purpose - the TIMELINE never asks about it
    row.geometry.closedFolder = row.hasChildren && !row.expanded;
    row.geometry.height = aIsDocument
                              ? kDocumentRowHeight
                              : ctrl::TimeLineRow::displayHeight(aNode, row.expanded);

    // the thumbnail: the cell's box is the ART's aspect, so its size is part of the row
    if (!aIsDocument) {
        if (const img::ResourceData* resource = layerResource(aNode)) {
            if (resource->hasImage()) {
                const QSize fitted = ThumbnailCache::fittedSize(resource->image(), kThumbBox);
                row.thumbWidth = fitted.width();
                row.thumbHeight = fitted.height();
                const quint64 key = ThumbnailCache::key(resource->image(), kThumbBox, mPixelRatio);
                row.thumbUrl = urlFor("treethumb", int(mRows.size()), key);
            }
        }
        row.matteUrl = matteUrlFor(row, int(mRows.size()));
    }

    aTop += row.geometry.height;
    mRows.append(row);

    // ONLY the expansion state decides - `aIsDocument` used to force the document row's children
    // in, so collapsing it did nothing (measured: the caret toggled the flag and the rows stayed)
    if (row.expanded) {
        for (core::ObjectNode* child : aNode.children())
            appendRow(*child, aDepth + 1, false, aTop);
    }
}

void ObjectTreeModel::refreshRowOf(core::ObjectNode& aNode) {
    const int row = rowOf(&aNode);
    if (row < 0)
        return;
    Row& target = mRows[row];
    const int oldHeight = target.geometry.height;

    const bool nameChanged = target.name != aNode.name();
    target.name = aNode.name();
    target.isFolder = aNode.canHoldChild();
    target.hasChildren = !aNode.children().empty();
    target.expanded = mExpanded.contains(&aNode);
    target.geometry.closedFolder = !target.geometry.isDocument && target.hasChildren && !target.expanded;
    target.geometry.height = target.geometry.isDocument
                                 ? kDocumentRowHeight
                                 : ctrl::TimeLineRow::displayHeight(aNode, target.expanded);

    if (target.geometry.height != oldHeight) {
        // a slimmed/unslimmed row (or a folder that gained its first key below it) changes
        // the CONTENT OFFSETS of every row under it: rebuild, which is exact, instead of
        // patching tops by hand
        rebuild();
        return;
    }
    if (nameChanged)
        measureRows();
    emit dataChanged(
        index(row), index(row),
        {NameRole, IsFolderRole, HasChildrenRole, ExpandedRole, VisibleRole, SlimmedRole, GlyphRole}
    );
}

void ObjectTreeModel::emitRowChanged(int aRow, const QVector<int>& aRoles) {
    if (aRow < 0 || aRow >= int(mRows.size()))
        return;
    emit dataChanged(index(aRow), index(aRow), aRoles);
}

//-------------------------------------------------------------------------------------------------
// properties
//-------------------------------------------------------------------------------------------------
void ObjectTreeModel::setFocused(bool aFocused) {
    if (mFocused == aFocused)
        return;
    mFocused = aFocused;
    emit focusChanged();
    const int row = rowOf(mCurrent);
    if (row >= 0)
        emitRowChanged(row, {CurrentRole});
}

int ObjectTreeModel::maxScrollOffset() const {
    return std::max(0, contentHeight() - mViewportHeight);
}

void ObjectTreeModel::setScrollOffset(int aOffset) {
    // A REBUILD IS NOT A SCROLL. `beginResetModel` makes the ListView drop its contentY to 0 (the
    // delegates it was positioning no longer exist), and that zero arrives here as a scroll report
    // - which then travelled to the timeline through scrollOffsetChanged, so ADDING A KEY SCROLLED
    // BOTH VIEWS TO THE TOP (reported). The offset is pixels keyed to nodes, not to row indices, so
    // it survives a rebuild; the report during one is ignored and the value is re-announced after.
    if (mRebuilding)
        return;
    // THE MODEL IS THE ONE PLACE the offset passes through in BOTH directions, so it is where the
    // range has to be enforced: a pan asked for 5000px, the list stopped at its end, and the lanes
    // kept going (measured: "I can pan way below the bottom lane and it goes out of sync").
    aOffset = std::max(0, std::min(aOffset, maxScrollOffset()));
    if (mScrollOffset == aOffset)
        return;
    mScrollOffset = aOffset;
    emit scrollOffsetChanged();
}

void ObjectTreeModel::requestScroll(int aOffset) {
    emit scrollToRequested(std::max(0, std::min(aOffset, maxScrollOffset())));
}

int ObjectTreeModel::currentRow() const { return rowOf(mCurrent); }

void ObjectTreeModel::setViewportHeight(int aHeight) {
    const int next = std::max(0, aHeight);
    if (mViewportHeight == next)
        return;
    mViewportHeight = next;
    // a shorter viewport means more travel: re-clamp what we are showing, and tell the dock
    const int clamped = std::min(mScrollOffset, maxScrollOffset());
    if (clamped != mScrollOffset) {
        mScrollOffset = clamped;
        emit scrollOffsetChanged();
    }
    emit viewportHeightChanged();
}

void ObjectTreeModel::setPixelRatio(qreal aRatio) {
    const qreal ratio = aRatio > 0.0 ? aRatio : 1.0;
    if (qFuzzyCompare(mPixelRatio, ratio))
        return;
    mPixelRatio = ratio;
    mThumbnails.clear();
    rebuild();
}

int ObjectTreeModel::selectionCount() const { return int(mSelection.size()); }

bool ObjectTreeModel::hasSelection() const { return !mSelection.isEmpty(); }

//-------------------------------------------------------------------------------------------------
// selection
//-------------------------------------------------------------------------------------------------
bool ObjectTreeModel::isSelected(const core::ObjectNode* aNode) const {
    return aNode && mSelection.contains(const_cast<core::ObjectNode*>(aNode));
}

void ObjectTreeModel::emitSelectionChanged() {
    emit selectionChanged();

    // the two roles are painted per row, so the rows that changed are the ones that were or
    // are selected -- cheap enough to re-emit for the whole list at the sizes this dock has
    if (!mRows.isEmpty())
        emit dataChanged(index(0), index(int(mRows.size()) - 1), {SelectedRole, CurrentRole});
}

void ObjectTreeModel::setSelection(const QVector<core::ObjectNode*>& aNodes, core::ObjectNode* aAnchor) {
    QVector<core::ObjectNode*> next;
    for (core::ObjectNode* node : aNodes) {
        if (node && rowOf(node) >= 0 && !next.contains(node))
            next.append(node);
    }
    const bool same = next == mSelection;
    mSelection = next;
    if (aAnchor)
        mAnchor = aAnchor;
    else if (!next.contains(mAnchor))
        mAnchor = next.isEmpty() ? nullptr : next.first();
    if (mCurrent == nullptr || !next.contains(mCurrent))
        mCurrent = next.isEmpty() ? nullptr : next.last();
    if (!same)
        emitSelectionChanged();
    else
        emitRowChanged(rowOf(mCurrent), {CurrentRole});
}

void ObjectTreeModel::addToSelection(core::ObjectNode& aNode) {
    if (isSelected(&aNode))
        return;
    mSelection.append(&aNode);
    mAnchor = &aNode;
    mCurrent = &aNode;
    emitSelectionChanged();
}

void ObjectTreeModel::removeFromSelection(core::ObjectNode& aNode) {
    if (!isSelected(&aNode))
        return;
    mSelection.removeAll(&aNode);
    if (mAnchor == &aNode)
        mAnchor = mSelection.isEmpty() ? nullptr : mSelection.last();
    if (mCurrent == &aNode)
        mCurrent = mSelection.isEmpty() ? nullptr : mSelection.last();
    emitSelectionChanged();
}

void ObjectTreeModel::pressRow(int aRow, int aModifiers) {
    const Row* row = rowAt(aRow);
    if (!row)
        return;
    // the scene focuses the tree on a press; a press on the document row is still a click
    // on the COLUMN (it selects nothing), so the node may be the document
    // CLICKING AWAY COMMITS. A rename is an edit in flight; a press on another row (or on the
    // document row) commits it, which is what every list view does and what the field's own
    // focus loss cannot be relied on for (it keeps focus when the window is busy).
    commitRenaming();
    core::ObjectNode* node = row->geometry.node;
    const auto mods = Qt::KeyboardModifiers(aModifiers);
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl = mods & Qt::ControlModifier;

    if (row->geometry.isDocument) {
        // the document row is not a layer and cannot be selected; clicking it is what a header
        // click means in every outliner - open or close it
        mCollapseNode = nullptr;
        toggleExpanded(aRow);
        return;
    }

    if (shift) {
        // range from the anchor (or from this row when there is no anchor)
        const int anchorRow = rowOf(mAnchor);
        const int from = anchorRow >= 0 ? std::min(anchorRow, aRow) : aRow;
        const int to = anchorRow >= 0 ? std::max(anchorRow, aRow) : aRow;
        QVector<core::ObjectNode*> range;
        for (int i = from; i <= to; ++i) {
            if (isSelectableRow(i))
                range.append(mRows[i].geometry.node);
        }
        setSelection(range, mAnchor ? mAnchor : node);
        mCurrent = node;
        mCollapseNode = nullptr;
        emitRowChanged(rowOf(mCurrent), {CurrentRole});
        return;
    }
    if (ctrl) {
        if (isSelected(node))
            removeFromSelection(*node);
        else
            addToSelection(*node);
        mCollapseNode = nullptr;
        return;
    }

    // PLAIN PRESS. A row already inside a multi-selection is NOT collapsed here: that
    // press may be the start of a drag of the whole selection, and collapsing first is what
    // made a group drag impossible (the same defect the graph panel's marker press had).
    // The RELEASE collapses it when no drag happened.
    mCollapseNode = nullptr;
    if (isSelected(node) && mSelection.size() > 1) {
        mCollapseNode = node;
        mCurrent = node;
        if (mAnchor == nullptr)
            mAnchor = node;
        emitRowChanged(rowOf(node), {CurrentRole});
        return;
    }
    setSelection({node}, node);
}

void ObjectTreeModel::releaseRow(int aRow, int aModifiers) {
    (void)aModifiers;
    const Row* row = rowAt(aRow);
    core::ObjectNode* node = row ? row->geometry.node : nullptr;
    core::ObjectNode* collapse = mCollapseNode;
    mCollapseNode = nullptr;
    if (!collapse || node != collapse)
        return;
    setSelection({node}, node);
}

void ObjectTreeModel::clearSelection() {
    mCollapseNode = nullptr;
    setSelection({}, nullptr);
    emitSelectionChanged();
}

void ObjectTreeModel::selectAll() {
    QVector<core::ObjectNode*> all;
    for (int i = 0; i < int(mRows.size()); ++i) {
        if (isSelectableRow(i))
            all.append(mRows[i].geometry.node);
    }
    setSelection(all, all.isEmpty() ? nullptr : all.first());
}



QVector<core::ObjectNode*> ObjectTreeModel::selectionInRowOrder() const {
    // the order the ROWS show, which is what delete/duplicate/paste act in - not the order the
    // user happened to click
    QVector<core::ObjectNode*> ordered;
    for (const Row& row : mRows) {
        if (isSelected(row.geometry.node))
            ordered.append(row.geometry.node);
    }
    return ordered;
}

QVector<core::ObjectNode*> ObjectTreeModel::selectedNodes() const { return selectionInRowOrder(); }

core::ObjectNode* ObjectTreeModel::representNode() const {
    // the row the lanes tint: exactly one selected row ON SCREEN (a selection kept inside a
    // collapsed folder has no row, so nothing is tinted until the folder opens again)
    // The timeline tints ONE lane and marks one row: exactly one selected, non-document row.
    QVector<core::ObjectNode*> nodes = selectionInRowOrder();
    return nodes.size() == 1 ? nodes.first() : nullptr;
}


bool ObjectTreeModel::anySelectedIsSlimmed() const {
    for (core::ObjectNode* node : mSelection) {
        if (node && node->isSlimmedDown())
            return true;
    }
    return false;
}

core::ObjectNode* ObjectTreeModel::pasteTargetNode(int aRow) const {
    // the row the menu was opened on, when it is a layer; otherwise the one selected layer
    const Row* row = rowAt(aRow);
    if (row && row->geometry.node && !row->geometry.isDocument)
        return row->geometry.node;
    const QVector<core::ObjectNode*> nodes = selectionInRowOrder();
    return nodes.size() == 1 ? nodes.first() : nullptr;
}

//-------------------------------------------------------------------------------------------------
// rows' own controls
//-------------------------------------------------------------------------------------------------
bool ObjectTreeModel::isExpanded(int aRow) const {
    const Row* row = rowAt(aRow);
    return row && row->expanded;
}

void ObjectTreeModel::toggleExpanded(int aRow) {
    const Row* row = rowAt(aRow);
    if (!row || !row->geometry.node || !row->hasChildren)
        return;
    core::ObjectNode* node = row->geometry.node;
    if (mExpanded.contains(node))
        mExpanded.remove(node);
    else
        mExpanded.insert(node);
    // the rows below move by the height of the subtree: one reset, which is exact
    rebuild();
}

void ObjectTreeModel::toggleVisibility(int aRow) {
    const Row* row = rowAt(aRow);
    if (!row || !row->geometry.node)
        return;
    setRowVisible(aRow, !row->geometry.node->isVisible());
}

void ObjectTreeModel::setRowVisible(int aRow, bool aVisible) {
    const Row* row = rowAt(aRow);
    if (!row || !row->geometry.node || !mProject)
        return;
    core::ObjectNode* node = row->geometry.node;
    if (node->isVisible() == aVisible)
        return;
    ctrl::tree::setVisibility(*mProject, *node, aVisible);
    emit visibilityChanged();
}

//-------------------------------------------------------------------------------------------------
// rename
//-------------------------------------------------------------------------------------------------
void ObjectTreeModel::beginRename(int aRow) {
    const Row* row = rowAt(aRow);
    if (!row || !row->geometry.node || row->geometry.isDocument)
        return;
    mRenamingRow = aRow;
    mRenamingText = row->name;
    emit renameChanged();
    emitRowChanged(aRow, {RenamingRole});
}

void ObjectTreeModel::setRenamingText(const QString& aText) { mRenamingText = aText; }

void ObjectTreeModel::commitRenaming() {
    if (!isRenaming())
        return;
    const QString text = mRenamingText;
    commitRename(text);
}

void ObjectTreeModel::commitRename(const QString& aName) {
    const int row = mRenamingRow;
    mRenamingRow = -1;
    mRenamingText.clear();
    const Row* target = rowAt(row);
    if (target && target->geometry.node && mProject) {
        const QString name = aName.trimmed();
        if (!name.isEmpty() && name != target->geometry.node->name())
            ctrl::tree::rename(*mProject, *target->geometry.node, name);
    }
    emit renameChanged();
    if (row >= 0)
        emitRowChanged(row, {RenamingRole});
}

void ObjectTreeModel::cancelRename() {
    const int row = mRenamingRow;
    mRenamingRow = -1;
    mRenamingText.clear();
    emit renameChanged();
    if (row >= 0)
        emitRowChanged(row, {RenamingRole});
}

//-------------------------------------------------------------------------------------------------
// drag and drop
//-------------------------------------------------------------------------------------------------
void ObjectTreeModel::beginDrag(int aRow) {
    const Row* row = rowAt(aRow);
    if (!row || !row->geometry.node || row->geometry.isDocument)
        return;
    core::ObjectNode* node = row->geometry.node;

    mDrag = DragState();
    mDrag.active = true;
    mDrag.row = aRow;
    mDrag.pressNode = node;
    // THE SELECTION DRAGS, the pressed row when it is not part of it: a press inside a
    // selection keeps the group (see pressRow), and a drag of the one row under the pointer
    // that is not selected carries just that row, as every list view does
    if (isSelected(node) && mSelection.size() > 1)
        mDrag.nodes = selectionInRowOrder();
    else
        mDrag.nodes = {node};

    mDragDwell.reset();
    mDragClock.restart();
    emit dragChanged();
}

bool ObjectTreeModel::dropIntoWouldCycle(core::ObjectNode& aParent) const {
    for (const core::ObjectNode* node : mDrag.nodes) {
        if (!node)
            continue;
        if (node == &aParent)
            return true;
        for (const core::ObjectNode* p = aParent.parent(); p; p = p->parent()) {
            if (p == node)
                return true;
        }
    }
    return false;
}

bool ObjectTreeModel::computeDropTarget(
    qreal aContentY, core::ObjectNode** aParent, int* aIndex, int* aRow, DropZone* aZone
) {
    if (mRows.isEmpty())
        return false;

    const qreal y = aContentY;
    // the row under the pointer: its own top/bottom decide the zone
    int hit = -1;
    for (int i = 0; i < int(mRows.size()); ++i) {
        const AnimationRow& geometry = mRows[i].geometry;
        if (y >= geometry.top && y < geometry.top + geometry.height) {
            hit = i;
            break;
        }
    }

    core::ObjectNode* parent = nullptr;
    int index = 0;
    int rowForIndicator = -1;
    DropZone zone = DropNone;

    if (hit < 0) {
        // above the first row or below the last one: the ends of the document
        core::ObjectNode* root = mProject ? mProject->objectTree().topNode() : nullptr;
        if (!root || y < 0)
            return false;
        parent = root;
        index = int(root->children().size());
        rowForIndicator = int(mRows.size()) - 1;
        zone = DropBelow;
        if (rowForIndicator < 0)
            return false;
    } else {
        const Row& row = mRows[hit];
        core::ObjectNode* node = row.geometry.node;
        if (!node)
            return false;
        const AnimationRow& geometry = row.geometry;
        const qreal offset = (y - geometry.top) / std::max(1, geometry.height);
        rowForIndicator = hit;

        if (row.geometry.isDocument) {
            // the document row: everything lands INSIDE it, at the top
            parent = node;
            index = 0;
            zone = DropInto;
        } else if (row.isFolder) {
            // a folder: above / into / below, the middle half being "inside"
            if (offset < 0.25) {
                parent = node->parent();
                index = parent ? parent->children().indexOf(node) : 0;
                zone = DropAbove;
            } else if (offset > 0.75) {
                parent = node->parent();
                index = parent ? parent->children().indexOf(node) + 1 : 0;
                zone = DropBelow;
            } else {
                parent = node;
                index = int(node->children().size());
                zone = DropInto;
            }
        } else {
            // a layer: its own row splits above/below, since there is nothing inside it
            if (offset < 0.5) {
                parent = node->parent();
                index = parent ? parent->children().indexOf(node) : 0;
                zone = DropAbove;
            } else {
                parent = node->parent();
                index = parent ? parent->children().indexOf(node) + 1 : 0;
                zone = DropBelow;
            }
        }
    }

    if (!parent)
        return false;
    if (dropIntoWouldCycle(*parent))
        return false;
    // dropping a node into the position it already occupies changes nothing
    if (mDrag.nodes.size() == 1 && mDrag.nodes.first() && mDrag.nodes.first()->parent() == parent) {
        const int current = parent->children().indexOf(mDrag.nodes.first());
        if (current == index || current + 1 == index) {
            *aParent = nullptr;
            return false;
        }
    }

    *aParent = parent;
    *aIndex = index;
    *aRow = rowForIndicator;
    *aZone = zone;
    return true;
}

void ObjectTreeModel::clearDragIndicators() {
    if (mDrag.targetRow >= 0)
        emitRowChanged(mDrag.targetRow, {DropZoneRole});
    mDrag.targetRow = -1;
    mDrag.zone = DropNone;
    mDrag.targetParent = nullptr;
    mDrag.targetIndex = 0;
}

void ObjectTreeModel::applyDragIndicator() {
    core::ObjectNode* parent = nullptr;
    int index = 0;
    int row = -1;
    DropZone zone = DropNone;
    const qreal contentY = mDrag.scrollOffsetCache + mDrag.pointerY;
    const int oldRow = mDrag.targetRow;

    if (computeDropTarget(contentY, &parent, &index, &row, &zone)) {
        mDrag.targetParent = parent;
        mDrag.targetIndex = index;
        mDrag.targetRow = row;
        mDrag.zone = zone;
    } else {
        mDrag.targetParent = nullptr;
        mDrag.targetRow = -1;
        mDrag.zone = DropNone;
    }
    if (oldRow != mDrag.targetRow) {
        emitRowChanged(oldRow, {DropZoneRole});
        emitRowChanged(mDrag.targetRow, {DropZoneRole});
    }
}

void ObjectTreeModel::updateDrag(qreal aViewportY, qreal aContentY) {
    if (!mDrag.active)
        return;
    mDrag.pointerY = aViewportY;
    mDrag.scrollOffsetCache = aContentY;
    const QRect viewport(0, 0, 1, std::max(1, mViewportHeight));
    mDragDwell.observe(QPoint(0, int(aViewportY)), viewport, mDragClock.elapsed());
    applyDragIndicator();
}

int ObjectTreeModel::dragAutoScrollStep(qreal aViewportY) {
    if (!mDrag.active)
        return 0;
    const QRect viewport(0, 0, 1, std::max(1, mViewportHeight));
    const QPoint pointer(0, int(aViewportY));
    // the dwell arms on MOVES (updateDrag observes) and ticks here, exactly like the
    // timeline's and the graph panel's marquee: the shared ctrl::autoscroll rule
    const ctrl::autoscroll::Step step = mDragDwell.update(pointer, viewport, mDragClock.elapsed());
    return step.y;
}

void ObjectTreeModel::endDrag(bool aCommit) {
    if (!mDrag.active)
        return;
    const bool hadTarget = mDrag.targetParent != nullptr;
    const QVector<core::ObjectNode*> moved = mDrag.nodes;
    if (aCommit && hadTarget) {
        commitDrop();
        // what you dropped is what is selected, as every list view does: the rows are still
        // the same NODES, so the selection survives the rebuild the move triggers
        if (!moved.isEmpty())
            setSelection(moved, moved.first());
    }
    else {
        clearDragIndicators();
    }
    mDrag = DragState();
    emit dragChanged();
}

QVector<util::TreePos> ObjectTreeModel::removalPositions() const {
    // Reverse document order: a removal renumbers the siblings AFTER it, so removing the
    // last one first keeps every recorded position valid against the original tree. The
    // mover replays them in exactly this order.
    QVector<core::ObjectNode*> nodes = mDrag.nodes;
    std::sort(nodes.begin(), nodes.end(), [this](core::ObjectNode* aLhs, core::ObjectNode* aRhs) {
        const int lhs = rowOf(aLhs);
        const int rhs = rowOf(aRhs);
        return lhs > rhs;
    });
    QVector<util::TreePos> positions;
    positions.reserve(nodes.size());
    for (core::ObjectNode* node : nodes)
        positions.append(pathOf(*node, false));
    return positions;
}

util::TreePos ObjectTreeModel::pathOf(const core::ObjectNode& aNode, bool aAfterRemovals) const {
    // The path the document's own query reads: row 0 is the document row (the invisible
    // root's only item), then one child index per level down.
    QVector<const core::ObjectNode*> chain;
    for (const core::ObjectNode* node = &aNode; node && node->parent(); node = node->parent())
        chain.prepend(node);

    util::TreePos pos;
    pos.setValidity(true);
    pos.pushRow(0);
    for (const core::ObjectNode* node : chain) {
        const core::ObjectNode* parent = node->parent();
        int index = parent->children().indexOf(node);
        if (aAfterRemovals) {
            // the index this node takes once every dragged node of the same parent, that
            // sits before it, is gone
            for (core::ObjectNode* dragged : mDrag.nodes) {
                if (dragged->parent() != parent)
                    continue;
                const int draggedIndex = parent->children().indexOf(dragged);
                if (draggedIndex >= 0 && draggedIndex < parent->children().indexOf(node))
                    --index;
            }
        }
        pos.pushRow(index);
    }
    return pos;
}

void ObjectTreeModel::commitDrop() {
    if (!mProject || !mDrag.targetParent || mDrag.nodes.isEmpty())
        return;

    core::ObjectNode* parent = mDrag.targetParent;
    // the target index counted in the list AFTER the removals
    int targetIndex = mDrag.targetIndex;
    for (core::ObjectNode* dragged : mDrag.nodes) {
        if (dragged->parent() != parent)
            continue;
        const int draggedIndex = parent->children().indexOf(dragged);
        if (draggedIndex >= 0 && draggedIndex < mDrag.targetIndex)
            --targetIndex;
    }

    const QVector<util::TreePos> removed = removalPositions();

    // insertions in DOCUMENT order, at consecutive indices from the adjusted target
    QVector<core::ObjectNode*> ordered = mDrag.nodes;
    std::sort(ordered.begin(), ordered.end(), [this](core::ObjectNode* aLhs, core::ObjectNode* aRhs) {
        return rowOf(aLhs) < rowOf(aRhs);
    });
    util::TreePos parentPath = pathOf(*parent, true);
    QVector<util::TreePos> inserted;
    inserted.reserve(ordered.size());
    for (int i = 0; i < ordered.size(); ++i) {
        util::TreePos pos = parentPath;
        pos.pushRow(targetIndex + i);
        inserted.append(pos);
    }

    ctrl::tree::moveNodes(*mProject, removed, inserted);
}

//-------------------------------------------------------------------------------------------------
// keyboard
//-------------------------------------------------------------------------------------------------
bool ObjectTreeModel::handleKey(int aKey, int aModifiers) {
    const auto mods = Qt::KeyboardModifiers(aModifiers);
    const bool shift = mods & Qt::ShiftModifier;
    const bool ctrl = mods & Qt::ControlModifier;

    // a rename in flight owns Return/Escape; everything else belongs to the field
    if (isRenaming())
        return false;

    const int current = rowOf(mCurrent);

    const auto moveTo = [&](int aRow, bool aExtend) {
        if (aRow < 0 || aRow >= int(mRows.size()))
            return false;
        if (!isSelectableRow(aRow)) {
            // skip the document row: step past it in the direction of travel
            const int step = aRow > current ? 1 : -1;
            aRow += step;
            if (aRow < 0 || aRow >= int(mRows.size()) || !isSelectableRow(aRow))
                return false;
        }
        if (aExtend) {
            const int anchorRow = rowOf(mAnchor);
            const int from = anchorRow >= 0 ? std::min(anchorRow, aRow) : aRow;
            const int to = anchorRow >= 0 ? std::max(anchorRow, aRow) : aRow;
            QVector<core::ObjectNode*> range;
            for (int i = from; i <= to; ++i) {
                if (isSelectableRow(i))
                    range.append(mRows[i].geometry.node);
            }
            setSelection(range, mAnchor ? mAnchor : mRows[aRow].geometry.node);
            mCurrent = mRows[aRow].geometry.node;
            emitRowChanged(aRow, {CurrentRole});
        } else {
            setSelection({mRows[aRow].geometry.node}, mRows[aRow].geometry.node);
        }
        return true;
    };

    switch (aKey) {
    case Qt::Key_Down: return moveTo(current < 0 ? 0 : current + 1, shift);
    case Qt::Key_Up: return moveTo(current < 0 ? 0 : current - 1, shift);
    case Qt::Key_Home: return moveTo(0, shift);
    case Qt::Key_End: return moveTo(int(mRows.size()) - 1, shift);
    case Qt::Key_Left: {
        // a collapsed folder expands, an expanded one collapses, a leaf jumps to its parent
        const Row* row = rowAt(current);
        if (!row || !row->geometry.node)
            return false;
        if (row->hasChildren && row->expanded) {
            toggleExpanded(current);
            return true;
        }
        const int parent = rowOf(row->geometry.node->parent());
        if (parent >= 0)
            return moveTo(parent, shift);
        return false;
    }
    case Qt::Key_Right: {
        const Row* row = rowAt(current);
        if (!row || !row->geometry.node)
            return false;
        if (row->hasChildren && !row->expanded) {
            toggleExpanded(current);
            return true;
        }
        if (row->expanded && current + 1 < int(mRows.size()))
            return moveTo(current + 1, shift);
        return false;
    }
    case Qt::Key_A: {
        if (!ctrl)
            return false;
        if (shift)
            clearSelection();
        else
            selectAll();
        return true;
    }
    case Qt::Key_Escape: {
        if (mDrag.active) {
            endDrag(false);
            return true;
        }
        if (!hasSelection())
            return false;
        clearSelection();
        return true;
    }
    case Qt::Key_F2: {
        if (!isSelectableRow(current))
            return false;
        beginRename(current);
        return true;
    }
    case Qt::Key_Delete: {
        if (!hasSelection())
            return false;
        deleteSelection();
        return true;
    }
    default:
        return false;
    }
}

//-------------------------------------------------------------------------------------------------
// actions
//-------------------------------------------------------------------------------------------------
void ObjectTreeModel::deleteSelection() {
    if (!mProject)
        return;
    const QVector<core::ObjectNode*> nodes = selectionInRowOrder();
    if (nodes.isEmpty())
        return;
    const int firstRow = rowOf(nodes.first());
    ctrl::tree::deleteNodes(*mProject, nodes);
    // the selection is pruned by the rebuild the tree event triggers; move the cursor to
    // the row the first deleted node sat on, so the keyboard keeps working
    if (firstRow >= 0 && firstRow < int(mRows.size()))
        mCurrent = mRows[firstRow].geometry.node;
}

void ObjectTreeModel::duplicateSelection() {
    if (!mProject)
        return;
    const QVector<core::ObjectNode*> nodes = selectionInRowOrder();
    if (nodes.isEmpty())
        return;
    QVector<core::ObjectNode*> copies;
    for (core::ObjectNode* node : nodes) {
        if (core::ObjectNode* copy = ctrl::tree::duplicateNode(*mProject, *node))
            copies.append(copy);
    }
    if (!copies.isEmpty())
        setSelection(copies, copies.first());
}

void ObjectTreeModel::addDefaultKeysToSelection() {
    if (!mProject)
        return;
    const QVector<core::ObjectNode*> nodes = selectionInRowOrder();
    if (nodes.isEmpty())
        return;
    // per node its own one-step action: adding defaults to three layers is three steps,
    // which is what an undo of "I added keys to three layers" has to unwind anyway
    for (core::ObjectNode* node : nodes)
        ctrl::tree::addDefaultKeys(*mProject, *node);
}

void ObjectTreeModel::createFolder(int aRow) {
    if (!mProject)
        return;
    const Row* row = rowAt(aRow);
    core::ObjectNode* parent = mProject->objectTree().topNode();
    int index = parent ? int(parent->children().size()) : 0;
    float depth = 0.0f;
    if (row && row->geometry.node) {
        if (row->isFolder && row->expanded) {
            parent = row->geometry.node;
            index = int(parent->children().size());
        } else if (row->geometry.node->parent()) {
            parent = row->geometry.node->parent();
            index = parent->children().indexOf(row->geometry.node) + 1;
        }
        if (parent && !parent->children().empty())
            depth = parent->children().back()->initialDepth() - 1.0f;
    }
    if (!parent)
        return;
    if (core::ObjectNode* folder = ctrl::tree::createFolder(*mProject, *parent, index, depth)) {
        mExpanded.insert(folder);
        setSelection({folder}, folder);
    }
}

void ObjectTreeModel::setSelectionSlimmed(bool aSlimmed) {
    if (!mProject)
        return;
    for (core::ObjectNode* node : selectionInRowOrder())
        ctrl::tree::setSlimmed(*mProject, *node, aSlimmed);
}

void ObjectTreeModel::pasteKeys(int aRow) {
    if (!mProject)
        return;
    if (core::ObjectNode* node = pasteTargetNode(aRow))
        emit pasteRequested(node);
}

void ObjectTreeModel::showContextMenu(int aRow, qreal aGlobalX, qreal aGlobalY) {
    // the menu is a widget (QMenu): the host builds it, at the row the press named
    emit contextMenuRequested(aRow, QPoint(qRound(aGlobalX), qRound(aGlobalY)));
}

void ObjectTreeModel::requestCreateLayer(int aRow) {
    const Row* row = rowAt(aRow);
    core::ObjectNode* parent = mProject ? mProject->objectTree().topNode() : nullptr;
    int index = parent ? int(parent->children().size()) : 0;
    if (row && row->geometry.node) {
        if (row->isFolder) {
            parent = row->geometry.node;
            index = int(parent->children().size());
        } else if (row->geometry.node->parent()) {
            parent = row->geometry.node->parent();
            index = parent->children().indexOf(row->geometry.node) + 1;
        }
    }
    if (parent)
        emit createLayerRequested(parent, index);
}



//-------------------------------------------------------------------------------------------------
// the asset relink walker ("Add new assets" / "Add missing assets")
//-------------------------------------------------------------------------------------------------
namespace {
struct ResourceEntry {
    img::ResourceNode* node = nullptr;
};

void collectResources(img::ResourceNode& aNode, QVector<ResourceEntry>& aEntries) {
    aEntries.append({&aNode});
    if (aNode.data().isLayer())
        return;
    for (img::ResourceNode* child : aNode.children())
        collectResources(*child, aEntries);
}

void collectNames(core::ObjectNode& aNode, QSet<QString>& aNames) {
    aNames.insert(aNode.name());
    for (core::ObjectNode* child : aNode.children())
        collectNames(*child, aNames);
}
} // namespace

void ObjectTreeModel::addResourceNode(
    core::ObjectNode& aParent, img::ResourceNode& aResource, int aResIndex, QSet<QString>& aKnown,
    QSet<QString>& aAdded
) {
    core::Project* project = this->project();
    if (!project)
        return;
    const QString name = aResource.data().identifier();
    core::ObjectNode* created = nullptr;
    if (!aKnown.contains(name) && !aAdded.contains(name)) {
        if (aResource.data().isLayer()) {
            created = ctrl::tree::createLayerFromResource(
                *project, aParent, int(aParent.children().size()), aResource, aResIndex
            );
        } else {
            created = ctrl::tree::createFolderFromResource(
                *project, aParent, int(aParent.children().size()), aResource, aResIndex
            );
        }
        if (created)
            aAdded.insert(name);
    }
    if (aResource.data().isLayer())
        return;
    core::ObjectNode& childParent = created ? *created : aParent;
    int index = 0;
    for (img::ResourceNode* child : aResource.children()) {
        if (child)
            addResourceNode(childParent, *child, index++, aKnown, aAdded);
    }
}

void ObjectTreeModel::addTreeContents(core::ObjectNode& aTop, img::ResourceNode& aResourceTop) {
    // the resource tree's entries are walked in REVERSE, which is how the importer gets a
    // mostly accurate depth: the last resource is drawn furthest back
    QVector<ResourceEntry> entries;
    collectResources(aResourceTop, entries);
    std::reverse(entries.begin(), entries.end());

    QSet<QString> known;
    collectNames(aTop, known);
    QSet<QString> added;
    int resIndex = 0;
    for (const ResourceEntry& entry : entries) {
        if (entry.node)
            addResourceNode(aTop, *entry.node, resIndex++, known, added);
    }
}

QVariantList ObjectTreeModel::assetTrees() const {
    QVariantList list;
    for (const auto& tree : mAssetTrees) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), tree.first);
        entry.insert(QStringLiteral("checked"), tree.second);
        list.append(entry);
    }
    return list;
}

void ObjectTreeModel::openAddAssets() {
    core::Project* project = this->project();
    if (!project)
        return;
    mAssetTrees.clear();
    for (const auto& tree : project->resourceHolder().imageTrees())
        mAssetTrees.append(qMakePair(QFileInfo(tree.filePath).baseName(), true));
    if (mAssetTrees.isEmpty())
        return; // nothing to choose from: the control that opened this does not exist either
    mAddAssetsOpen = true;
    emit assetTreesChanged();
    emit addAssetsOpenChanged();
}

void ObjectTreeModel::closeAddAssets() {
    if (!mAddAssetsOpen)
        return;
    mAddAssetsOpen = false;
    emit addAssetsOpenChanged();
}

void ObjectTreeModel::setAssetTreeChecked(int aIndex, bool aChecked) {
    if (aIndex < 0 || aIndex >= int(mAssetTrees.size()))
        return;
    if (mAssetTrees[aIndex].second == aChecked)
        return;
    mAssetTrees[aIndex].second = aChecked;
    emit assetTreesChanged();
}

void ObjectTreeModel::addMissingAssets() {
    core::Project* project = this->project();
    core::ObjectNode* top = project ? project->objectTree().topNode() : nullptr;
    if (!project || !top)
        return;

    // the resource trees that belong to THIS document, by file base name (the importer's own
    // rule: a PSD's resource tree is named after the document)
    const QString documentName = top->name();
    for (const auto& tree : project->resourceHolder().imageTrees()) {
        if (!tree.topNode || QFileInfo(tree.filePath).baseName() != documentName)
            continue;
        if (tree.topNode->children().empty()) {
            addTreeContents(*top, *tree.topNode);
            continue;
        }
        for (img::ResourceNode* child : tree.topNode->children()) {
            if (child)
                addTreeContents(*top, *child);
        }
    }
    rebuild();
    selectAll();
}

void ObjectTreeModel::addCheckedAssetTrees() {
    core::Project* project = this->project();
    core::ObjectNode* top = project ? project->objectTree().topNode() : nullptr;
    if (!project || !top) {
        closeAddAssets();
        return;
    }
    QSet<QString> checked;
    for (const auto& tree : mAssetTrees) {
        if (tree.second)
            checked.insert(tree.first);
    }
    for (const auto& tree : project->resourceHolder().imageTrees()) {
        if (!tree.topNode || !checked.contains(QFileInfo(tree.filePath).baseName()))
            continue;
        if (tree.topNode->children().empty()) {
            addTreeContents(*top, *tree.topNode);
            continue;
        }
        for (img::ResourceNode* child : tree.topNode->children()) {
            if (child)
                addTreeContents(*top, *child);
        }
    }
    closeAddAssets();
    rebuild();
    selectAll();
}

//-------------------------------------------------------------------------------------------------
// thumbnails and the matte
//-------------------------------------------------------------------------------------------------
QImage ObjectTreeModel::thumbnailImage(int aRow) {
    const Row* row = rowAt(aRow);
    if (!row || !row->geometry.node)
        return QImage();
    const img::ResourceData* resource = layerResource(*row->geometry.node);
    if (!resource || !resource->hasImage())
        return QImage();
    return mThumbnails.layerThumbnail(resource->image(), kThumbBox, mPixelRatio).toImage();
}

QImage ObjectTreeModel::matteImage(int aRow) {
    const Row* row = rowAt(aRow);
    if (!row || !row->geometry.node || !rowHasMatte(*row))
        return QImage();
    const auto found = mMatteCache.constFind(row->geometry.node);
    if (found != mMatteCache.constEnd())
        return found.value();

    // AN UNPAINTED MATTE IS A PICTURE TOO: its neutral field is fully visible, which is exactly
    // what the chip should show for a mask that has not been touched yet.
    const core::MaskKey::Data* foundMask = maskDataOf(*row->geometry.node);
    if (!foundMask || foundMask->field().isNull())
        return QImage();
    const core::MaskKey::Data& mask = *foundMask;
    // THE MATTE ITSELF, in grayscale: white = visible, black = hidden, the reading an alpha
    // channel has everywhere (the inspector's card shows the same picture larger)
    // KEEP THE MATTE'S OWN SHAPE: the chip is a 14px CELL, not a 14x14 picture, so a 2:1 field
    // must letterbox inside it rather than be squashed (reported: "the mask thumbnail doesn't use
    // the correct aspect ratio"). The QML Image preserves the aspect too, so the two agree.
    const QImage preview =
        mask.field().scaled(kMatteChip, kMatteChip, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    mMatteCache.insert(row->geometry.node, preview);
    return preview;
}



} // namespace gui
