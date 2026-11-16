#include <QMenu>
#include <QFileDialog>
#include "qfilesystemwatcher.h"
#include "qmessagebox.h"
#include "util/TreeUtil.h"
#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "ctrl/CmndName.h"
#include "gui/ProjectHook.h"
#include "gui/ResourceTreeWidget.h"
#include "gui/res/res_Item.h"
#include "gui/res/res_ResourceUpdater.h"
#include "gui/res/res_Notifier.h"
#include "MainWindow.h"

namespace gui {

ResourceTreeWidget::ResourceTreeWidget(ViaPoint& aViaPoint, bool aUseCustomContext, QWidget* aParent):
    QTreeWidget(aParent),
    mViaPoint(aViaPoint),
    mProject(),
    mHolder(),
    mActionItem(),
    mChangePathAction(),
    mRenameAction(),
    mReloadAction(),
    mFileWatch(),
    mWatchRemove(),
    mDeleteAction(),
    mRenaming() {
    // this->setSelectionMode(QAbstractItemView::ExtendedSelection);
    this->setObjectName("resourceTree");
    this->setHeaderHidden(true);
    this->setAnimated(true);
    // this->setDragDropMode(DragDropMode::InternalMove);
    // this->setDefaultDropAction(Qt::TargetMoveAction);
    this->setAlternatingRowColors(true);
    this->setVerticalScrollMode(QAbstractItemView::ScrollPerItem);
    this->setHorizontalScrollMode(QAbstractItemView::ScrollPerItem);
    this->setColumnCount(kColumnCount);
    this->setFocusPolicy(Qt::NoFocus);

    this->connect(this, &QTreeWidget::itemSelectionChanged, this, &ResourceTreeWidget::onItemSelectionChanged);

    this->connect(this, &QTreeWidget::itemChanged, [=](bool) { this->endRenameEditor(); });
    this->connect(this, &QTreeWidget::itemClicked, [=](bool) { this->endRenameEditor(); });
    this->connect(this, &QTreeWidget::itemCollapsed, [=](bool) { this->endRenameEditor(); });
    this->connect(this, &QTreeWidget::itemExpanded, [=](bool) { this->endRenameEditor(); });

    // custom context
    if (aUseCustomContext) {
        this->setContextMenuPolicy(Qt::CustomContextMenu);

        this->connect(this, &QWidget::customContextMenuRequested, this, &ResourceTreeWidget::onContextMenuRequested);

        mChangePathAction = new QAction(tr("Change file path"), this);
        mChangePathAction->connect(
            mChangePathAction, &QAction::triggered, this, &ResourceTreeWidget::onChangePathActionTriggered
        );

        mRenameAction = new QAction(tr("Rename"), this);
        mRenameAction->connect(mRenameAction, &QAction::triggered, this, &ResourceTreeWidget::onRenameActionTriggered);

        mReloadAction = new QAction(tr("Reload images from file"), this);
        mReloadAction->connect(mReloadAction, &QAction::triggered, this, &ResourceTreeWidget::onReloadActionTriggered);

        mFileWatch = new QAction(tr("Monitor for changes"), this);
        mFileWatch->connect(mFileWatch, &QAction::triggered, this, &ResourceTreeWidget::onWatchTriggered);

        mWatchRemove = new QAction(tr("Stop monitoring for changes"), this);
        mWatchRemove->connect(mWatchRemove, &QAction::triggered, this, &ResourceTreeWidget::onWatchRemoveTriggered);

        mDeleteAction = new QAction(tr("Delete"), this);
        mDeleteAction->connect(mDeleteAction, &QAction::triggered, this, &ResourceTreeWidget::onDeleteActionTriggered);
    }
}

void ResourceTreeWidget::setProject(core::Project* aProject) {
    // finalize tree
    if (mProject) {
        auto treeCount = this->topLevelItemCount();
        if (treeCount > 0) {
            // create vector
            std::unique_ptr<QVector<QTreeWidgetItem*>> trees(new QVector<QTreeWidgetItem*>());
            for (int i = 0; i < treeCount; ++i) {
                trees->push_back(this->takeTopLevelItem(0));
            }
            // save
            auto hook = (ProjectHook*)mProject->hook();
            hook->grabResourceTrees(trees.release());
        }
    }
    XC_ASSERT(this->topLevelItemCount() == 0);
    this->clear(); // fail-safe code

    // update reference
    if (aProject) {
        mProject = aProject->pointee();
    } else {
        mProject.reset();
    }

    // setup tree
    if (mProject) {
        auto hook = (ProjectHook*)mProject->hook();
        // load trees
        if (hook && hook->hasResourceTrees()) {
            QScopedPointer<QVector<QTreeWidgetItem*>> trees(hook->releaseResourceTrees());
            for (auto tree : *trees) {
                this->addTopLevelItem(tree);
            }
            trees.reset();
        } else {
            // create trees
            resetTreeView(mProject->resourceHolder());
        }
    }
}

void ResourceTreeWidget::load(const QString& aFileName) {
    res::ResourceUpdater updater(mViaPoint, *mProject);
    updater.load(*this, aFileName);
}

void ResourceTreeWidget::resetTreeView(core::ResourceHolder& aHolder) {
    this->clear();
    mHolder = &aHolder;

    for (auto data : aHolder.imageTrees()) {
        auto item = res::ResourceUpdater::createGUITree(*data.topNode, data.filePath);
        XC_PTR_ASSERT(item);
        this->addTopLevelItem(item);
    }
}

QTreeWidgetItem* ResourceTreeWidget::findItem(const util::TreePos& aPos) {
    if (!aPos.isValid() || aPos.depth() < 1)
        return nullptr;
    QTreeWidgetItem* item = this->topLevelItem(aPos.row(0));

    for (int i = 1; i < aPos.depth(); ++i) {
        if (!item)
            return nullptr;
        item = item->child(aPos.row(i));
    }
    return item;
}

void ResourceTreeWidget::updateTreeRootName(core::ResourceHolder& aHolder) {
    if (mHolder != &aHolder)
        return;

    for (int i = 0; i < this->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = this->topLevelItem(i);
        XC_PTR_ASSERT(item);
        res::Item* resItem = res::Item::cast(item);
        XC_PTR_ASSERT(resItem);

        auto nodename = mHolder->findRelativeFilePath(resItem->node());
        item->setText(kItemColumn, nodename);
    }
}

QList<img::ResourceNode*> ResourceTreeWidget::findSelectingNodes() const {
    QList<QTreeWidgetItem*> items = this->selectedItems();
    QList<img::ResourceNode*> nodes;

    for (auto item : items) {
        res::Item* resItem = res::Item::cast(item);
        if (resItem) {
            nodes.push_back(&resItem->node());
        }
    }
    return nodes;
}

void ResourceTreeWidget::onItemSelectionChanged() {
    auto nodes = findSelectingNodes();
    onNodeSelectionChanged(nodes);
}

void ResourceTreeWidget::onContextMenuRequested(const QPoint& aPos) {
    endRenameEditor();

    mActionItem = this->itemAt(aPos);

    if (mActionItem) {
        QMenu menu(this);

        res::Item* item = res::Item::cast(mActionItem);
        if (item && item->isTopNode()) {
            menu.addAction(mChangePathAction);
        } else {
            menu.addAction(mRenameAction);
        }
        menu.addSeparator();

        menu.addAction(mReloadAction);

        if (item && item->isTopNode()) {
            menu.addSeparator();
            menu.addAction(mFileWatch);
            menu.addAction(mWatchRemove);
            menu.addSeparator();
            menu.addAction(mDeleteAction);
        }

        menu.exec(this->mapToGlobal(aPos));
    }
}

void ResourceTreeWidget::onChangePathActionTriggered(bool) {
    res::Item* item = res::Item::cast(mActionItem);
    if (item && item->isTopNode()) {
        const QString fileName = QFileDialog::getOpenFileName(
            this, tr("Open File"), "", "ImageFile (*.psd *.ora *.jpg *.jpeg *.png *.gif *.tiff *.tif *.webp)"
        );
        if (fileName.isEmpty())
            return;

        if (mHolder) {
            auto& stack = mProject->commandStack();
            cmnd::ScopedMacro macro(stack, CmndName::tr("Update asset file path"));

            // notifier
            auto notifier = new res::ChangeFilePathNotifier(mViaPoint);
            macro.grabListener(notifier);

            img::ResourceNode* resNode = &(item->node());
            auto absFilePath = QFileInfo(fileName).absoluteFilePath();
            auto prevFilePath = mHolder->findAbsoluteFilePath(item->node());

            auto command = new cmnd::Delegatable(
                [=]() {
                    mHolder->changeAbsoluteFilePath(*resNode, absFilePath);
                    item->setText(kItemColumn, mHolder->relativeFilePath(absFilePath));
                },
                [=]() {
                    mHolder->changeAbsoluteFilePath(*resNode, prevFilePath);
                    item->setText(kItemColumn, mHolder->relativeFilePath(prevFilePath));
                }
            );

            stack.push(command);
        }
    }
}

void ResourceTreeWidget::onRenameActionTriggered(bool) {
    if (mActionItem) {
        this->openPersistentEditor(mActionItem, kItemColumn);
        this->editItem(mActionItem, kItemColumn);
        mRenaming = true;
    }
}

void ResourceTreeWidget::endRenameEditor() {
    auto actionItem = mActionItem;
    mActionItem = nullptr;

    if (!mProject)
        return;

    if (actionItem && mRenaming) {
        res::Item* item = res::Item::cast(actionItem);
        if (!item)
            return;

        mRenaming = false;
        this->closePersistentEditor(actionItem, kItemColumn);
        auto nodePtr = &(item->node());
        auto curName = nodePtr->data().identifier();
        auto newName = item->text(kItemColumn);

        if (curName != newName) {
            auto& stack = mProject->commandStack();
            cmnd::ScopedMacro macro(stack, CmndName::tr("Rename asset"));

            // notifier
            auto notifier = new res::RenameNotifier(*mProject, item->treePos());
            notifier->event().setSingleTarget(*nodePtr);
            macro.grabListener(notifier);

            stack.push(new cmnd::Delegatable(
                [=]() {
                    item->setText(kItemColumn, newName);
                    nodePtr->data().setIdentifier(newName);
                },
                [=]() {
                    item->setText(kItemColumn, curName);
                    nodePtr->data().setIdentifier(curName);
                }
            ));
        }
    }
}

void ResourceTreeWidget::onReloadActionTriggered(bool) {
    if (!mProject)
        return;

    res::Item* item = res::Item::cast(mActionItem);
    if (!item)
        return;

    res::ResourceUpdater updater(mViaPoint, *mProject);
    updater.reload(*item);
}

void ResourceTreeWidget::onWatchTriggered(bool) {
    if (!mProject)
        return;
    // The context menu always sets the action item; fall back on the selection
    // so a trigger that did not come from one (a shortcut, a programmatic call)
    // still watches a known asset rather than nothing. res::Item::cast reads the
    // item it is given, so an absent one is checked rather than cast.
    QTreeWidgetItem* actionItem = mActionItem;
    if (!actionItem) {
        actionItem = this->currentItem();
    }
    res::Item* item = actionItem ? res::Item::cast(actionItem) : nullptr;
    if (!item)
        return;
    if (!QFile(mProject->resourceHolder().findAbsoluteFilePath(item->node())).exists()) {
        MainWindow::showInfoPopup(
            tr("File not found"),
            tr("The file couldn't be found, please change "
               "its file path using the button above."),
            "Warn"
        );
        return;
    }
    auto fileWatcher = MainWindow::getWatcher();

    qDebug() << "files : " << fileWatcher->files();
    const QString absPath = mProject->resourceHolder().findAbsoluteFilePath(item->node());
    if (fileWatcher->files().contains(absPath)) {
        MainWindow::showInfoPopup(tr("Invalid Selection"), tr("Already watching for changes in this file"), "Warn");
        return;
    }
    addWatch(absPath);
}

void ResourceTreeWidget::addWatch(const QString& aAbsPath) {
    // The bare action the user-facing trigger resolves to, with none of the
    // checks that trigger owes its caller: the path is added to the global
    // watcher and its reload handler installed. Watch-all-assets reaches this
    // for every asset at once, so anything a caller can pre-check it must.
    auto fileWatcher = MainWindow::getWatcher();

    fileWatcher->addPath(aAbsPath);
    {
        // One handler per watched path, owned by a QObject named for the path:
        //   - the connection's context object is that owner, so it dies with the
        //     tree (the tab's widget), not with the item it reloaded -- a change
        //     that arrives after the project or the tab is gone reloads nothing;
        //   - a watch/stop cycle deletes the owner on the way out (see
        //     onWatchRemoveTriggered), so repeated cycles cannot accumulate
        //     handlers, and a re-watch finds exactly the owner it already had;
        //   - the handler reloads only the item whose file changed. The signal
        //     names the path, and a tree watching a different file ignores it --
        //     with two watched PSDs a save of either used to fire both.
        // The handler also does not sleep: a 500ms sleep on the GUI thread froze
        // the whole window for every watched save, and the file is already
        // complete when the watcher reports its change.
        auto* owner = new QObject(this);
        owner->setObjectName(aAbsPath);
        connect(fileWatcher, &QFileSystemWatcher::fileChanged, owner,
                [this, aAbsPath](const QString& aChangedPath) {
                    if (aChangedPath != aAbsPath) {
                        return; // a different watched file changed
                    }
                    if (!mProject) {
                        return; // the project this tree belonged to is gone
                    }
                    // Look the item up again by path: the one that was selected
                    // when the watch started may have since been renamed or
                    // removed, and a reload belongs to whatever the file's tree
                    // is now.
                    res::Item* watched = nullptr;
                    for (int i = 0; i < this->topLevelItemCount(); ++i) {
                        if (auto* topItem = res::Item::cast(this->topLevelItem(i))) {
                            if (mProject->resourceHolder().findAbsoluteFilePath(topItem->node()) == aAbsPath) {
                                watched = topItem;
                                break;
                            }
                        }
                    }
                    if (!watched) {
                        return; // the tree for this file is gone
                    }
                    res::ResourceUpdater updater(mViaPoint, *mProject);
                    updater.reload(*watched);
                });
        MainWindow::showInfoPopup(tr("Success"), tr("This file will be monitored for changes."), "Info");
        qDebug() << "Watching path: " << aAbsPath;
    }
}

void ResourceTreeWidget::watchAllTopItems() {
    if (!mProject)
        return;

    auto fileWatcher = MainWindow::getWatcher();

    // Watch every asset this tree holds that is not watched yet. The failures the
    // context-menu trigger would report on are skipped silently here: linking a
    // new project must not raise a popup for an asset that is missing on disk or
    // already watched, it just does not add a second watch for it.
    for (int i = 0; i < this->topLevelItemCount(); ++i) {
        res::Item* item = res::Item::cast(this->topLevelItem(i));
        if (!item)
            continue;

        const QString absPath = mProject->resourceHolder().findAbsoluteFilePath(item->node());
        if (!QFile(absPath).exists())
            continue;
        if (fileWatcher->files().contains(absPath))
            continue;

        addWatch(absPath);
    }
}

void ResourceTreeWidget::onWatchRemoveTriggered(bool) {
    if (!mProject)
        return;
    QTreeWidgetItem* actionItem = mActionItem;
    if (!actionItem) {
        actionItem = this->currentItem();
    }
    res::Item* item = actionItem ? res::Item::cast(actionItem) : nullptr;
    if (!item)
        return;

    auto fileWatcher = MainWindow::getWatcher();

    qDebug() << "files : " << fileWatcher->files();
    const QString absPath = mProject->resourceHolder().findAbsoluteFilePath(item->node());
    if (fileWatcher->files().contains(absPath)) {
        fileWatcher->removePath(absPath);
        // The path's handler owner goes with it: a watch/stop cycle must not
        // leave a handler behind for a file that is no longer watched.
        if (auto* owner = this->findChild<QObject*>(absPath)) {
            delete owner;
        }
        MainWindow::showInfoPopup(tr("Success"), tr("The file will no longer be monitored"), "Info");
        return;
    } else {
        MainWindow::showInfoPopup(tr("File not monitored"), tr("The file is not being currently monitored"), "Warn");
    }
}

int ResourceTreeWidget::removeUnusedAssets() {
    if (!mProject)
        return 0;

    // Collect before removing: each removal takes a top-level item out of the
    // tree, and every item after it shifts down.
    QList<res::Item*> unused;
    for (int i = 0; i < this->topLevelItemCount(); ++i) {
        res::Item* item = res::Item::cast(this->topLevelItem(i));
        if (!item)
            continue;

        img::ResourceNode& topNode = util::TreeUtil::getTreeRoot(item->node());
        if (!mProject->objectTree().findNodesUsingResource(topNode).isEmpty())
            continue; // a layer renders from it

        // A reference the object tree does not show still keeps the asset (an
        // image key held while a paste is in flight, say). Skip it rather than
        // let remove() answer with a warning box for every such asset.
        bool keeped = false;
        for (img::ResourceNode::ConstIterator itr(&topNode); itr.hasNext();) {
            if (itr.next()->isKeeped()) {
                keeped = true;
                break;
            }
        }
        if (keeped)
            continue;

        unused.push_back(item);
    }

    res::ResourceUpdater updater(mViaPoint, *mProject);
    int removed = 0;
    for (auto* item : unused) {
        if (updater.remove(*this, *item)) {
            ++removed;
        }
    }
    return removed;
}

void ResourceTreeWidget::onDeleteActionTriggered(bool) {
    if (!mProject)
        return;

    res::Item* item = res::Item::cast(mActionItem);
    if (!item)
        return;

    // The layers that render from this asset. The asset cannot go while they
    // still reference it, so when there are any, tell the user and offer to take
    // them with it rather than making them hunt the layers down by hand.
    img::ResourceNode& topNode = util::TreeUtil::getTreeRoot(item->node());
    QVector<core::ObjectNode*> references = mProject->objectTree().findNodesUsingResource(topNode);

    if (!references.isEmpty()) {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Warning);
        box.setWindowTitle(tr("Delete asset"));
        box.setText(tr("Some layers use this asset."));
        box.setInformativeText(
            references.size() == 1 ? tr("Delete the layer that uses it as well?")
                                   : tr("Delete the %1 layers that use it as well?").arg(references.size())
        );
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return;
    }

    res::ResourceUpdater updater(mViaPoint, *mProject);
    updater.remove(*this, *item, references);
}

} // namespace gui
