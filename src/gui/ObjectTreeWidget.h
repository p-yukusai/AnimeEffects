#ifndef GUI_OBJECTTREEWIDGET_H
#define GUI_OBJECTTREEWIDGET_H

#include <QTreeWidget>
#include <QHeaderView>
#include <QAction>
#include "ctrl/TimeLineEditor.h"
#include "util/PlacePointer.h"
#include "util/LinkPointer.h"
#include "util/TreePos.h"
#include "util/Signaler.h"
#include "cmnd/ScopedMacro.h"
#include "core/Project.h"
#include "core/ObjectTree.h"
#include "core/ObjectTreeNotifier.h"
#include "gui/GUIResources.h"
#include "gui/ViaPoint.h"
#include "gui/obj/obj_Item.h"

namespace gui {

//-------------------------------------------------------------------------------------------------
class ObjectTreeWidget: public QTreeWidget {
    Q_OBJECT
    obj::Item* createFileItem(core::ObjectNode& aNode);
    void updateItemVisibilityAppearance(QTreeWidgetItem* aItem, bool aVisible);

public:
    enum { kItemColumn = 0 };
    enum { kColumnCount = 1 };

    ObjectTreeWidget(ViaPoint& aViaPoint, GUIResources& aResources, QWidget* aParent);

    void setProject(core::Project* aProject);
    core::ObjectNode* findSelectingRepresentNode();

    // signals
    util::Signaler<void()> onVisibilityUpdated;
    util::Signaler<void(QTreeWidgetItem*)> onTreeViewUpdated;
    util::Signaler<void(core::ObjectNode*)> onSelectionChanged;
    util::Signaler<void(int)> onScrollUpdated;

    // for Notifiers
    void notifyViewUpdated();
    void notifyRestructure();

    struct resource {
        QString name{};
        img::ResourceNode* node{};
        bool isFolder = false;
        int childCount = 0;
        QVector<img::ResourceNode*> children;
    };

    static core::MeshKey *getMeshKey(const core::ObjectNode &aNode, int frame);

    static core::ImageKey *getImageKey(const core::ObjectNode &aNode, int frame);

    static std::pair<core::TimeKey *, core::LayerMesh *> getAreaMeshImpl(core::ObjectNode &aNode, int frame);

private:
    struct ItemInfo {
        ItemInfo(): ptr(nullptr), pos() {}
        ItemInfo(QTreeWidgetItem* aPtr, const util::TreePos& aPos): ptr(aPtr), pos(aPos) {}
        QTreeWidgetItem* ptr;
        util::TreePos pos;
    };

    // from QTreeWidget
    virtual void paintEvent(QPaintEvent* aEvent);
    virtual void showEvent(QShowEvent* aEvent);
    virtual void dragMoveEvent(QDragMoveEvent* aEvent);
    virtual void dropEvent(QDropEvent* aEvent);
    virtual void rowsAboutToBeRemoved(const QModelIndex& aParent, int aStart, int aEnd);
    virtual void rowsInserted(const QModelIndex& aParent, int aStart, int aEnd);
    virtual void scrollContentsBy(int aDx, int aDy);
    virtual void resizeEvent(QResizeEvent* aEvent);
    virtual void scrollTo(const QModelIndex& aIndex, ScrollHint aHint);

    void createTree(core::ObjectTree* aTree);
    void addItemRecursive(QTreeWidgetItem* aItem, core::ObjectNode* aNode);
    obj::Item* createFolderItem(core::ObjectNode& aNode);
    QModelIndex cheatDragDropPos(QPoint& aPos);
    QPoint treeTopLeftPosition() const;
    int scrollHeight() const { return -treeTopLeftPosition().y(); }
    void endRenameEditor();
    int itemHeight(const core::ObjectNode& aNode) const;
    bool updateItemHeights(QTreeWidgetItem* aItem);

    void onTimeLineModified(core::TimeLineEvent&, bool);
    void onItemChanged(QTreeWidgetItem* aItem, int aColumn);
    void onItemClicked(QTreeWidgetItem* aItem, int aColumn);
    void onItemCollapsed(QTreeWidgetItem* aItem);
    void onItemExpanded(QTreeWidgetItem* aItem);
    void onItemSelectionChanged();
    void onContextMenuRequested(const QPoint& aPos);
    void onSlimActionTriggered(bool aIsTriggered);
    void onRenameActionTriggered(bool aIsTriggered);
    void onPasteActionTriggered(bool aIsTriggered) const;
    void onObjectActionTriggered(bool aIsTriggered);
    void onObjectMirrorTriggered();
    void onAddDefaultsTriggered() const;
    void onFolderActionTriggered(bool aIsTriggered);
    void onDeleteActionTriggered(bool aIsTriggered);
    void onAddTreeTriggered(bool);
    void addItems(QStringList targets);
    void onObjectReconstructionTriggered(bool aIsTriggered);
    void onThemeUpdated(theme::Theme&);

    ViaPoint& mViaPoint;
    GUIResources& mGUIResources;
    util::LinkPointer<core::Project> mProject;
    util::SlotId mTimeLineSlot;

    bool mStoreInsert;
    QVector<util::TreePos> mRemovedPositions;
    QVector<util::TreePos> mInsertedPositions;
    util::PlacePointer<cmnd::ScopedMacro> mMacroScope;
    QScopedPointer<ctrl::TimeLineEditor> mEditor;
    core::ObjectTreeNotifier* mObjTreeNotifier;
    QModelIndex mDragIndex;
    QAbstractItemView::DropIndicatorPosition mDropIndicatorPos;

    QTreeWidgetItem* mActionItem;
    QAction* mSlimAction;
    QAction* mReconstructAction;
    QAction* mAddTreeAction;
    QAction* mRenameAction;
    QAction* mPasteAction;
    QAction* mObjectAction;
    QAction* mObjectMirror;
    QAction* mAddDefaults;
    QAction* mFolderAction;
    QAction* mDeleteAction;

    void addLayer(
        QTreeWidgetItem* curActionItem,
        core::ObjectNode* itemNode,
        bool moveToFolder = false,
        int folderIndex = -1,
        img::ResourceNode* resNode = nullptr,
        QVector<QString>* parsedRes = nullptr,
        const QVector<resource>* res = nullptr
    );
    void addFolder(
        QTreeWidgetItem* curActionItem,
        core::ObjectNode* itemNode,
        bool moveToFolder = false,
        int folderIndex = -1,
        img::ResourceNode* resNode = nullptr,
        QVector<QString>* parsedRes = nullptr,
        QVector<resource>* res = nullptr
    );
};

} // namespace gui

#endif // GUI_OBJECTTREEWIDGET_H
