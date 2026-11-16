#ifndef GUI_QML_OBJECTTREEMODEL_H
#define GUI_QML_OBJECTTREEMODEL_H

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QSet>
#include <QVector>

#include "core/MaskKey.h"
#include "ctrl/AutoScroll.h"
#include "gui/AnimationRow.h"
#include "gui/ThumbnailCache.h"
#include "util/LinkPointer.h"
#include "util/TreePos.h"
#include "util/Signaler.h"
#include "util/SlotId.h"

namespace img {
class ResourceNode;
}

namespace core {
class Project;
class ObjectNode;
class TimeLineEvent;
class ObjectTreeEvent;
}

namespace gui {

// THE OBJECT TREE'S CONTENT AND ITS GESTURES, as one model.
//
// The dock's tree column is a flat list of ROWS (the document row, then every visible
// node), which is the same list the timeline's lanes are built from -- see
// gui/AnimationRow.h. Rows are DERIVED from the document plus this model's expansion
// state: nothing about a row is stored in the scene, so a restructure, an undo or a
// project change is one reset and the view is correct again.
//
// WHAT LIVES HERE, and why it is not in QML:
//
//  * the ROW LIST (order, depth, heights, closed-folder summaries) -- ctrl::TimeLineRow's
//    rule is the only height definition, so the tree and the lanes cannot disagree;
//  * the SELECTION, with real list-view semantics: anchor and cursor, ranges, toggles, the
//    "press inside a selection does not collapse it until the release" rule that makes a
//    multi-row drag possible, and keyboard verbs;
//  * DRAG AND DROP as a computed DROP TARGET (parent + child index + which row shows the
//    indicator), translated into the document's mover positions with the index arithmetic
//    a remove-then-insert replay needs;
//  * every ACTION the tree can perform, through ctrl/TreeActions.h, so each is one undo
//    step and the menu acts on the WHOLE selection (the widget's menu acted on the row it
//    was opened on, which made a multi-selection pointless).
//
// The scene owns ink, hit rects and the pointer; the model owns what the tree MEANS. The
// host (gui::ObjectTreeWidget) owns the widgets a tree action needs - the asset chooser and
// the message boxes - which arrive here as requests.
class ObjectTreeModel: public QAbstractListModel {
    Q_OBJECT
public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        DepthRole,
        RowHeightRole,
        IsDocumentRole,
        IsFolderRole,
        HasChildrenRole,
        ExpandedRole,
        SelectedRole,
        CurrentRole,
        VisibleRole,
        SlimmedRole,
        GlyphRole,
        ThumbUrlRole,
        ThumbWidthRole,
        ThumbHeightRole,
        HasMatteRole,
        MatteUrlRole,
        RenamingRole,
        DropZoneRole,
        // Which indentation guides this row draws, as a BIT PER DEPTH LEVEL: bit d set means
        // the vertical trunk of level d continues through this row (there is another node at
        // that level further down). The model computes it because it is a fact about the ROW
        // LIST, not about the ink - and the scene draws nothing but what it is told.
        GuideMaskRole,
        CountRole
    };
    Q_ENUM(Role)

    // what a row's drop indicator says
    enum DropZone { DropNone = 0, DropAbove, DropBelow, DropInto, DropZoneCount };
    Q_ENUM(DropZone)

    explicit ObjectTreeModel(QObject* aParent = nullptr);
    ~ObjectTreeModel() override;

    // ---- the document ---------------------------------------------------------
    void setProject(core::Project* aProject);
    core::Project* project() const { return mProject.get(); }

    // ---- QAbstractListModel ---------------------------------------------------
    int rowCount(const QModelIndex& aParent = QModelIndex()) const override;
    QVariant data(const QModelIndex& aIndex, int aRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // ---- the one row list (also what the timeline draws) ----------------------
    const AnimationRows& rows() const { return mContract; }
    // Re-derive every row height/offset and announce the result. Used by a view
    // state that changes lane count without touching the document (the timeline's
    // Link/Separate Channels); the ordinary document paths call rebuild() from
    // their own handlers.
    void refreshRows();
    int contentHeight() const;
    int contentWidth() const;
    // how far the rows can travel: contentHeight - the viewport the scene reports
    int maxScrollOffset() const;

    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY rowsChanged)
    // the widest row's natural width (indent + caret + icon + name): what the horizontal
    // scroll has to reach, so a long name or a deep nesting stays readable and clickable
    Q_PROPERTY(int contentWidth READ contentWidth NOTIFY contentWidthChanged)
    Q_PROPERTY(bool empty READ isEmpty NOTIFY rowsChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(bool renaming READ isRenaming NOTIFY renameChanged)
    Q_PROPERTY(int renamingRow READ renamingRow NOTIFY renameChanged)
    Q_PROPERTY(bool dragging READ isDragging NOTIFY dragChanged)
    Q_PROPERTY(int dragCount READ dragCount NOTIFY dragChanged)
    Q_PROPERTY(int dragRow READ dragRow NOTIFY dragChanged)
    Q_PROPERTY(bool focused READ isFocused WRITE setFocused NOTIFY focusChanged)
    Q_PROPERTY(int scrollOffset READ scrollOffset WRITE setScrollOffset NOTIFY scrollOffsetChanged)
    Q_PROPERTY(int viewportHeight READ viewportHeight WRITE setViewportHeight NOTIFY viewportHeightChanged)
    Q_PROPERTY(int matteRevision READ matteRevision NOTIFY matteChanged)
    // "Add new assets": the project's resource trees and which of them are ticked. The
    // chooser used to be a hand-built widget dialog; the LIST is the model's and the scene
    // draws it (the dock has one dialog convention, and it is QML).
    Q_PROPERTY(bool addAssetsOpen READ isAddAssetsOpen NOTIFY addAssetsOpenChanged)
    Q_PROPERTY(QVariantList assetTrees READ assetTrees NOTIFY assetTreesChanged)

    bool isEmpty() const;
    int selectionCount() const;
    bool hasSelection() const;
    bool isRenaming() const { return mRenamingRow >= 0; }
    int renamingRow() const { return mRenamingRow; }
    bool isDragging() const { return mDrag.active; }
    int dragCount() const { return mDrag.nodes.size(); }
    int dragRow() const { return mDrag.row; }
    bool isFocused() const { return mFocused; }
    void setFocused(bool aFocused);
    int scrollOffset() const { return mScrollOffset; }
    // the scene reporting where it is
    void setScrollOffset(int aOffset);
    // the dock asking it to move (a timeline pan or a key press): the scene applies it and
    // reports the value it actually reached through setScrollOffset
    void requestScroll(int aOffset);
    int viewportHeight() const { return mViewportHeight; }
    void setViewportHeight(int aHeight);
    int matteRevision() const { return mMatteRevision; }
    bool isAddAssetsOpen() const { return mAddAssetsOpen; }
    QVariantList assetTrees() const;

    // ---- selection ------------------------------------------------------------
    // The press: `aModifiers` is a Qt::KeyboardModifiers. A plain press on a row that is
    // already inside the selection does NOT collapse it (the release does) so a drag can
    // carry the group.
    Q_INVOKABLE void pressRow(int aRow, int aModifiers);
    // The release: collapses a plain press that did not turn into a drag.
    Q_INVOKABLE void releaseRow(int aRow, int aModifiers);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void selectAll();
    // the cursor's row index (-1 when there is none): the scene scrolls it into view after a
    // key the model consumed
    Q_INVOKABLE int currentRow() const;

    QVector<core::ObjectNode*> selectedNodes() const;
    core::ObjectNode* representNode() const;
    core::ObjectNode* currentNode() const;

    // ---- rows' own controls ---------------------------------------------------
    Q_INVOKABLE void toggleExpanded(int aRow);
    Q_INVOKABLE bool isExpanded(int aRow) const;
    Q_INVOKABLE void toggleVisibility(int aRow);
    Q_INVOKABLE void setRowVisible(int aRow, bool aVisible);

    // ---- rename ---------------------------------------------------------------
    Q_INVOKABLE void beginRename(int aRow);
    Q_INVOKABLE void commitRename(const QString& aName);
    // the field's live text, pushed on every keystroke: a press anywhere else commits it, and a
    // focus loss commits it - neither can read the editor's own property
    Q_INVOKABLE void setRenamingText(const QString& aText);
    Q_INVOKABLE void commitRenaming();
    Q_INVOKABLE void cancelRename();

    // ---- drag and drop --------------------------------------------------------
    // beginDrag on the row under the press; updateDrag with the pointer in VIEWPORT
    // coordinates and the scene's content offset; endDrag(true) commits, false cancels.
    Q_INVOKABLE void beginDrag(int aRow);
    Q_INVOKABLE void updateDrag(qreal aViewportY, qreal aContentY);
    Q_INVOKABLE void endDrag(bool aCommit);
    // The autoscroll step the scene must apply to its own contentY while a drag holds near
    // an edge: positive scrolls DOWN (later rows). Zero when nothing should move.
    Q_INVOKABLE int dragAutoScrollStep(qreal aViewportY);

    // ---- keyboard -------------------------------------------------------------
    // Returns true when the tree consumed the key. The scene calls this first, so a key the
    // tree handles never reaches the application's key map (which owns Delete for the
    // TIMELINE's keys).
    Q_INVOKABLE bool handleKey(int aKey, int aModifiers);

    // ---- actions (each one undo step; they act on the whole selection) --------
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicateSelection();
    Q_INVOKABLE void addKeysAtPlayheadToSelection();
    Q_INVOKABLE void createFolder(int aRow);
    Q_INVOKABLE void setSelectionSlimmed(bool aSlimmed);
    Q_INVOKABLE void pasteKeys(int aRow);
    Q_INVOKABLE void showContextMenu(int aRow, qreal aGlobalX, qreal aGlobalY);
    // ask the host for the widgets an action needs
    Q_INVOKABLE void requestCreateLayer(int aRow);
    // the chooser
    Q_INVOKABLE void openAddAssets();
    Q_INVOKABLE void closeAddAssets();
    Q_INVOKABLE void setAssetTreeChecked(int aIndex, bool aChecked);
    // add the contents of every ticked resource tree to the document (one undo step per
    // node, which is what the relink walker has always been)
    Q_INVOKABLE void addCheckedAssetTrees();
    // "Add missing assets": the resource trees named after the document, adding only the
    // nodes whose names the object tree does not already hold
    Q_INVOKABLE void addMissingAssets();

    // What a menu needs to know before it is built.
    core::ObjectNode* pasteTargetNode(int aRow) const;
    bool anySelectedIsSlimmed() const;

    // ---- thumbnails and the matte chip ---------------------------------------
    // The scene sets this from Screen.devicePixelRatio; the thumbnails are rendered for it.
    Q_INVOKABLE void setPixelRatio(qreal aRatio);
    // The dock's font, so the model can measure a name the way the scene will draw it (the
    // horizontal range must cover the widest row's TEXT, and only the font knows how wide that is)
    void setContentFont(const QFont& aFont);
    // Providers' entry points (GUI thread: QQuickWidget renders on it).
    QImage thumbnailImage(int aRow);
    QImage matteImage(int aRow);

public:
    // A frame moved (the dock forwards the timeline's own notification): only an ANIMATED
    // matte changes with it, so the chip's images are re-issued only when one exists.
    void frameChanged();

signals:
    // The row list changed shape or heights: the timeline must rebuild its lanes.
    void rowsChanged();
    // the widest row changed (a rename, a font change): the horizontal RANGE moved, and only
    // this property's consumers need to hear it (a rename does not change any row's geometry)
    void contentWidthChanged();
    // The SELECTION changed: the dock hands it to the inspector, the viewport driver, the
    // timeline and the graph panel.
    void selectionChanged();
    // A node's visibility changed: the canvas is stale.
    void visibilityChanged();
    // The host's jobs (they need widgets: a chooser, a message box, a menu).
    void createLayerRequested(core::ObjectNode* aParent, int aIndex);
    void addAssetsOpenChanged();
    void assetTreesChanged();
    // the host runs the paste (it shows the outcome in a message box)
    void pasteRequested(core::ObjectNode* aNode);
    void contextMenuRequested(int aRow, const QPoint& aGlobalPos);

    void renameChanged();
    void dragChanged();
    void focusChanged();
    void scrollOffsetChanged();
    void viewportHeightChanged();
    void matteChanged();
    void scrollToRequested(int aOffset);

private:
    struct Row {
        AnimationRow geometry;
        QString name;
        bool isFolder = false;
        bool hasChildren = false;
        bool expanded = false;
        QString thumbUrl;
        qreal thumbWidth = 0;
        qreal thumbHeight = 0;
        bool hasMatte = false;
        QString matteUrl;
        // which indentation trunks continue through this row (a bit per depth level) and the
        // name's advance in the dock's font (for contentWidth)
        int guideMask = 0;
        int nameWidth = 0;
    };
    struct DragState {
        bool active = false;
        int row = -1;
        core::ObjectNode* pressNode = nullptr;
        QVector<core::ObjectNode*> nodes; // the moved set, document order
        int targetRow = -1;               // the row the indicator marks
        DropZone zone = DropNone;
        core::ObjectNode* targetParent = nullptr;
        int targetIndex = 0;
        qreal pointerY = 0;               // viewport coordinates
        qreal scrollOffsetCache = 0;      // the scene's contentY when the pointer last moved
    };

    // from the model's own notifications
    void onTimeLineEvent(core::TimeLineEvent& aEvent, bool aUndo);
    void onTreeEvent(core::ObjectTreeEvent& aEvent, bool aUndo);
    void onNodeAttribute();
    void onFrameChanged();

    void rebuild();
    // a row has a matte when its node HAS a mask (painted or not), and the chip's url follows
    bool rowHasMatte(const Row& aRow) const;
    // the node's mask data: the blended field, else the lane's BASE VALUE (a fresh mask has one)
    const core::MaskKey::Data* maskDataOf(const core::ObjectNode& aNode) const;
    QString matteUrlFor(const Row& aRow, int aRowIndex) const;
    void appendRow(core::ObjectNode& aNode, int aDepth, bool aIsDocument, int& aTop);
    // the guide mask + the content width, from the finished row list
    void measureRows();
    void refreshRowOf(core::ObjectNode& aNode);
    int rowOf(const core::ObjectNode* aNode) const;
    const Row* rowAt(int aIndex) const;
    bool isSelectableRow(int aIndex) const;


    void setSelection(const QVector<core::ObjectNode*>& aNodes, core::ObjectNode* aAnchor);
    void addToSelection(core::ObjectNode& aNode);
    void removeFromSelection(core::ObjectNode& aNode);
    bool isSelected(const core::ObjectNode* aNode) const;
    void emitSelectionChanged();
    void emitRowChanged(int aRow, const QVector<int>& aRoles);

    QVector<core::ObjectNode*> selectionInRowOrder() const;

    // drag
    void clearDragIndicators();
    void applyDragIndicator();
    bool computeDropTarget(qreal aContentY, core::ObjectNode** aParent, int* aIndex, int* aRow, DropZone* aZone);
    bool dropIntoWouldCycle(core::ObjectNode& aParent) const;
    void commitDrop();

    QVector<util::TreePos> removalPositions() const;
    util::TreePos pathOf(const core::ObjectNode& aNode, bool aAfterRemovals) const;

    // the asset relink walker: every node of one resource tree, in the importer's own order
    void addTreeContents(core::ObjectNode& aTop, img::ResourceNode& aResourceTop);
    void addResourceNode(
        core::ObjectNode& aParent, img::ResourceNode& aResource, int aResIndex, QSet<QString>& aKnown,
        QSet<QString>& aAdded
    );

    // thumbnails / matte
    void invalidateMatte();

    util::LinkPointer<core::Project> mProject;
    util::SlotId mTimeLineSlot;
    util::SlotId mTreeSlot;
    util::SlotId mNodeAttrSlot;
    util::SlotId mResourceSlot;

    QVector<Row> mRows;
    AnimationRows mContract;
    QSet<core::ObjectNode*> mExpanded;
    // Selection as an ORDERED list (row order is derived, but the order of "last added"
    // matters for the anchor rule); membership is what the view paints.
    QVector<core::ObjectNode*> mSelection;
    core::ObjectNode* mAnchor = nullptr;
    core::ObjectNode* mCurrent = nullptr;
    // a plain press inside the selection remembers the row it hit, and the RELEASE
    // collapses to it when the press did not become a drag
    core::ObjectNode* mCollapseNode = nullptr;
    int mRenamingRow = -1;
    QString mRenamingText;
    bool mFocused = false;
    int mScrollOffset = 0;
    int mViewportHeight = 0;
    // true while beginResetModel/endResetModel are in flight: the views' own contentY drop is
    // not a scroll (see setScrollOffset)
    bool mRebuilding = false;
    DragState mDrag;
    ctrl::autoscroll::EdgeDwell mDragDwell;
    QElapsedTimer mDragClock;
    ThumbnailCache mThumbnails;
    qreal mPixelRatio = 1.0;
    QFont mContentFont;
    int mContentWidth = 0;
    // per row: the name's advance in that font, so contentWidth is one pass over the rows
    QVector<int> mNameWidths;
    int mMatteRevision = 0;
    bool mAddAssetsOpen = false;
    // the resource trees the chooser lists, by base name, with their ticked state
    QVector<QPair<QString, bool>> mAssetTrees;
    // the matte previews: 14px, per node, dropped whenever the document or the frame moves
    QHash<const core::ObjectNode*, QImage> mMatteCache;
};

} // namespace gui

#endif // GUI_QML_OBJECTTREEMODEL_H
