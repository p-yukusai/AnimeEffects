#ifndef GUI_RESOURCETREEWIDGET_H
#define GUI_RESOURCETREEWIDGET_H

#include <QTreeWidget>
#include <QAction>
#include "util/Signaler.h"
#include "util/LinkPointer.h"
#include "cmnd/Stack.h"
#include "img/PSDFormat.h"
#include "core/ResourceHolder.h"
#include "core/Project.h"
#include "gui/ViaPoint.h"
#include "gui/res/res_Notifier.h"

namespace gui {

class ResourceTreeWidget: public QTreeWidget {
    Q_OBJECT
public:
    enum { kItemColumn = 0 };
    enum { kColumnCount = 1 };
    typedef QList<img::ResourceNode*> NodeList;

    ResourceTreeWidget(ViaPoint& aViaPoint, bool aUseCustomContext, QWidget* aParent);
    void setProject(core::Project* aProject);

    void load(const QString& aFileName);
    void updateTreeRootName(core::ResourceHolder& aHolder);
    void onReloadActionTriggered(bool aIsTriggered);
    void onWatchTriggered(bool aIsTriggered);
    void onWatchRemoveTriggered(bool aIsTriggered);
    // Watch one asset by absolute path: add it to the global watcher and install
    // that path's reload handler. The user-facing trigger resolves to this; it is
    // public so a newly created project can be watched without a per-asset popup.
    void addWatch(const QString& aAbsPath);
    // Watch every top-level asset of this tree that exists on disk and is not
    // already watched. Everything else is skipped silently.
    void watchAllTopItems();
    // Remove every top-level asset that no layer renders from, and report how
    // many went. An asset whose subtree still holds a live reference is left
    // alone -- the object tree is the authority on "unused", but a reference it
    // cannot see (a key in flight, say) must not be yanked either. Each removal
    // is one undo step, exactly as the context menu's Delete is; the engine's
    // macro cannot be nested, so they are not folded into one.
    int removeUnusedAssets();

    util::Signaler<void(const NodeList&)> onNodeSelectionChanged;

private:
    void resetTreeView(core::ResourceHolder& aHolder);
    QTreeWidgetItem* findItem(const util::TreePos& aPos);
    QList<img::ResourceNode*> findSelectingNodes() const;
    void endRenameEditor();
    void onItemSelectionChanged();
    void onContextMenuRequested(const QPoint& aPos);
    void onChangePathActionTriggered(bool aIsTriggered);
    void onRenameActionTriggered(bool aIsTriggered);
    void onDeleteActionTriggered(bool aIsTriggered);

    ViaPoint& mViaPoint;
    util::LinkPointer<core::Project> mProject;
    core::ResourceHolder* mHolder;
    QTreeWidgetItem* mActionItem;
    QAction* mChangePathAction;
    QAction* mRenameAction;
    QAction* mReloadAction;
    QAction* mFileWatch;
    QAction* mWatchRemove;
    QAction* mDeleteAction;
    bool mRenaming;
};

} // namespace gui

#endif // GUI_RESOURCETREEWIDGET_H
