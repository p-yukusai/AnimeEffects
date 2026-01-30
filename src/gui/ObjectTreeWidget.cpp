#include <QMenu>
#include <QPainter>
#include <memory>
#include <QScrollBar>
#include <QDragMoveEvent>
#include <QModelIndexList>
#include <QStyle>
#include <QProxyStyle>
#include "ctrl/TimeLineEditor.h"
#include "qmessagebox.h"
#include "util/TreeUtil.h"
#include "util/LinkPointer.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/CmndName.h"
#include "gui/ObjectTreeWidget.h"
#include "core/ObjectNodeUtil.h"
#include "ctrl/ImageFileLoader.h"
#include "gui/ResourceDialog.h"
#include "gui/ProjectHook.h"
#include "gui/obj/obj_MoveItem.h"
#include "gui/obj/obj_InsertItem.h"
#include "gui/obj/obj_RemoveItem.h"
#include "gui/obj/obj_Notifiers.h"
#include "gui/obj/obj_Util.h"
#include "ctrl/ffd/ffd_Target.h"
#include <QFormLayout>
#include <iostream>
#include <sstream>

namespace {
static const int kTopItemSize = 22;
static const int kItemSize = ctrl::TimeLineRow::kHeight;
static const int kItemSizeInc = ctrl::TimeLineRow::kIncrease;
} // namespace

namespace gui {

//-------------------------------------------------------------------------------------------------
ObjectTreeWidget::ObjectTreeWidget(ViaPoint& aViaPoint, GUIResources& aResources, QWidget* aParent):
    QTreeWidget(aParent),
    mViaPoint(aViaPoint),
    mGUIResources(aResources),
    mProject(),
    mTimeLineSlot(),
    mStoreInsert(false),
    mRemovedPositions(),
    mInsertedPositions(),
    mMacroScope(),
    mObjTreeNotifier(),
    mDragIndex(),
    mDropIndicatorPos(),
    mActionItem(),
    mSlimAction(),
    mRenameAction(),
    mObjectAction(),
    mFolderAction(),
    mDeleteAction() {
    this->setObjectName("objectTree");
    this->setFocusPolicy(Qt::NoFocus);
    this->setContextMenuPolicy(Qt::CustomContextMenu);
    this->setSelectionMode(ExtendedSelection);
    this->setHeaderHidden(true);
    this->setAnimated(true);
    this->setUniformRowHeights(false);
    this->setDragDropMode(InternalMove);
    this->setDefaultDropAction(Qt::TargetMoveAction);
    // this->setAlternatingRowColors(true);
    this->setVerticalScrollMode(ScrollPerItem);
    this->setHorizontalScrollMode(ScrollPerItem);
    // this->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // this->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    this->setColumnCount(kColumnCount);

    if (this->invisibleRootItem()) {
        this->invisibleRootItem()->setFlags(Qt::NoItemFlags);
    }
    this->connect(this, &QWidget::customContextMenuRequested, this, &ObjectTreeWidget::onContextMenuRequested);
    this->connect(this, &QTreeWidget::itemChanged, this, &ObjectTreeWidget::onItemChanged);
    this->connect(this, &QTreeWidget::itemClicked, this, &ObjectTreeWidget::onItemClicked);
    this->connect(this, &QTreeWidget::itemCollapsed, this, &ObjectTreeWidget::onItemCollapsed);
    this->connect(this, &QTreeWidget::itemExpanded, this, &ObjectTreeWidget::onItemExpanded);
    this->connect(this, &QTreeWidget::itemSelectionChanged, this, &ObjectTreeWidget::onItemSelectionChanged);

    mGUIResources.onThemeChanged.connect(this, &ObjectTreeWidget::onThemeUpdated);

    {
        mSlimAction = new QAction(tr("Contract"), this);
        mSlimAction->connect(mSlimAction, &QAction::triggered, this, &ObjectTreeWidget::onSlimActionTriggered);

        mReconstructAction = new QAction(tr("Add missing resources"), this);
        mReconstructAction->connect(mReconstructAction, &QAction::triggered, this, &ObjectTreeWidget::onObjectReconstructionTriggered);

        mAddTreeAction = new QAction(tr("Add new resources"), this);
        mAddTreeAction->connect(mAddTreeAction, &QAction::triggered, this, &ObjectTreeWidget::onAddTreeTriggered);

        mRenameAction = new QAction(tr("Rename"), this);
        mRenameAction->connect(mRenameAction, &QAction::triggered, this, &ObjectTreeWidget::onRenameActionTriggered);

        mPasteAction = new QAction(tr("Paste from clipboard"), this);
        mPasteAction->connect(mPasteAction, &QAction::triggered, this, &ObjectTreeWidget::onPasteActionTriggered);

        mObjectAction = new QAction(tr("Create layer"), this);
        mObjectAction->connect(mObjectAction, &QAction::triggered, this, &ObjectTreeWidget::onObjectActionTriggered);

        mObjectMirror = new QAction(tr("Duplicate node"), this);
        mObjectMirror->connect(mObjectMirror, &QAction::triggered, this, &ObjectTreeWidget::onObjectMirrorTriggered);

        mFolderAction = new QAction(tr("Create folder"), this);
        mFolderAction->connect(mFolderAction, &QAction::triggered, this, &ObjectTreeWidget::onFolderActionTriggered);

        mDeleteAction = new QAction(tr("Delete"), this);
        mDeleteAction->connect(mDeleteAction, &QAction::triggered, this, &ObjectTreeWidget::onDeleteActionTriggered);
    }

    // enable pixel scrolling
    setVerticalScrollMode(ScrollPerPixel);
    verticalScrollBar()->setSingleStep(24);
}

void ObjectTreeWidget::setProject(core::Project* aProject) {
    // finalize
    if (mProject) {
        mProject->onTimeLineModified.disconnect(mTimeLineSlot);

        auto treeCount = this->topLevelItemCount();
        if (treeCount > 0) {
            std::unique_ptr<QVector<QTreeWidgetItem*>> trees(new QVector<QTreeWidgetItem*>());
            for (int i = 0; i < treeCount; ++i) {
                trees->push_back(this->takeTopLevelItem(0));
            }
            // save
            auto hook = static_cast<ProjectHook*>(mProject->hook());
            hook->grabObjectTrees(trees.release());
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

    // setup
    if (mProject) {

        mTimeLineSlot = mProject->onTimeLineModified.connect(this, &ObjectTreeWidget::onTimeLineModified);

        auto hook = static_cast<ProjectHook*>(mProject->hook());
        // load trees
        if (hook && hook->hasObjectTrees()) {
            QScopedPointer<QVector<QTreeWidgetItem*>> trees(hook->releaseObjectTrees());
            for (auto tree : *trees) {
                this->addTopLevelItem(tree);
            }
            trees.reset();
        } else {
            createTree(&(mProject->objectTree()));
        }
    }
    notifyViewUpdated();
}

core::ObjectNode* ObjectTreeWidget::findSelectingRepresentNode() {
    QList<QTreeWidgetItem*> items = selectedItems();
    core::ObjectNode* node = nullptr;

    for (auto item : items) {
        obj::Item* objItem = obj::Item::cast(item);
        if (objItem && !objItem->isTopNode()) {
            if (node)
                return nullptr;
            node = &objItem->node();
        }
    }
    return node;
}

void ObjectTreeWidget::notifyViewUpdated() {
    onTreeViewUpdated(this->topLevelItem(0));
    onScrollUpdated(scrollHeight());
}

void ObjectTreeWidget::notifyRestructure() {
    onTreeViewUpdated(this->topLevelItem(0));
    onScrollUpdated(scrollHeight());
}

//-------------------------------------------------------------------------------------------------
void ObjectTreeWidget::createTree(core::ObjectTree* aTree) {
    this->clear();

    if (aTree) {
        core::ObjectNode* node = aTree->topNode();
        if (node) {
            obj::Item* item = new obj::Item(*this, *node);
            item->setSizeHint(kItemColumn, QSize(kTopItemSize, kTopItemSize));
            this->addTopLevelItem(item);
            addItemRecursive(item, node);
        }
    }
    notifyViewUpdated();
}

void ObjectTreeWidget::addItemRecursive(QTreeWidgetItem* aItem, core::ObjectNode* aNode) {
    const core::ObjectNode::Children& children = aNode->children();
    for (auto childNode : children) {
        if (childNode->canHoldChild()) {
            auto childItem = createFolderItem(*childNode);
            aItem->addChild(childItem);
            addItemRecursive(childItem, childNode);
        } else {
            aItem->addChild(createFileItem(*childNode));
        }
    }
}

int ObjectTreeWidget::itemHeight(const core::ObjectNode& aNode) const {
    return ctrl::TimeLineRow::calculateHeight(aNode);
}

obj::Item* ObjectTreeWidget::createFolderItem(core::ObjectNode& aNode) {

    obj::Item* item = new obj::Item(*this, aNode);
    item->setSizeHint(kItemColumn, QSize(kItemSize, itemHeight(aNode)));
    // item->setBackgroundColor(kItemColumn, QColor(235, 235, 235, 255));
    item->setIcon(kItemColumn, mGUIResources.icon("folder"));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(kItemColumn, aNode.isVisible() ? Qt::Checked : Qt::Unchecked);
    return item;
}

obj::Item* ObjectTreeWidget::createFileItem(core::ObjectNode& aNode) {
    auto* item = new obj::Item(*this, aNode);
    item->setSizeHint(kItemColumn, QSize(kItemSize, itemHeight(aNode)));
    item->setIcon(kItemColumn, mGUIResources.icon("filew"));
    item->setFlags(item->flags() & ~Qt::ItemIsDropEnabled);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(kItemColumn, aNode.isVisible() ? Qt::Checked : Qt::Unchecked);
    return item;
}

QModelIndex ObjectTreeWidget::cheatDragDropPos(QPoint& aPos) {
    static const int kMargin = 5;

    QModelIndex index = this->indexAt(aPos);
    if (index.isValid()) {
        QRect rect = this->visualRect(index);
        if (aPos.y() - rect.top() < kMargin) {
            aPos.setY(rect.top() + 1);
        }
        if (rect.bottom() - aPos.y() < kMargin) {
            aPos.setY(rect.bottom() - 1);
        }
    }
    return index;
}

QPoint ObjectTreeWidget::treeTopLeftPosition() const {
    if (topLevelItemCount()) {
        return visualItemRect(topLevelItem(0)).topLeft();
    }
    return QPoint();
}

void ObjectTreeWidget::endRenameEditor() {
    class NameChanger: public cmnd::Stable {
        core::ObjectNode& mNode;
        QTreeWidgetItem& mItem;
        QString mPrevName;
        QString mNextName;

    public:
        NameChanger(core::ObjectNode& aNode, QTreeWidgetItem& aItem, const QString& aName):
            mNode(aNode), mItem(aItem), mPrevName(), mNextName(aName) {}

        virtual QString name() const { return CmndName::tr("Rename object"); }

        virtual void exec() {
            mPrevName = mNode.name();
            redo();
        }

        virtual void redo() {
            mNode.setName(mNextName);
            mItem.setText(kItemColumn, mNextName);
        }

        virtual void undo() {
            mNode.setName(mPrevName);
            mItem.setText(kItemColumn, mPrevName);
        }
    };

    if (mActionItem) {
        this->closePersistentEditor(mActionItem, kItemColumn);

        obj::Item* objItem = obj::Item::cast(mActionItem);
        if (objItem) {
            core::ObjectNode& node = objItem->node();
            const QString newName = objItem->text(kItemColumn);
            if (node.name() != newName) {
                mProject->commandStack().push(new NameChanger(node, *mActionItem, newName));
            }
        }
        mActionItem = nullptr;
    }
}

bool ObjectTreeWidget::updateItemHeights(QTreeWidgetItem* aItem) {
    if (!aItem)
        return false;

    // cast to a objectnode's item
    obj::Item* objItem = obj::Item::cast(aItem);
    bool changed = false;

    if (objItem && !objItem->isTopNode()) {
        const int height = itemHeight(objItem->node());
        // update
        if (objItem->sizeHint(kItemColumn).height() != height) {
            objItem->setSizeHint(kItemColumn, QSize(kItemSize, height));
            changed = true;
        }
    }

    // recursive call
    const int childCount = aItem->childCount();
    for (int i = 0; i < childCount; ++i) {
        changed |= updateItemHeights(aItem->child(i));
    }
    return changed;
}

void ObjectTreeWidget::onThemeUpdated(theme::Theme& aTheme) {
    QFile stylesheet(aTheme.path() + "/stylesheet/standard.ssa");
    if (stylesheet.open(QIODevice::ReadOnly | QIODevice::Text)) {
        this->setStyleSheet(QTextStream(&stylesheet).readAll());
    }
}

//-------------------------------------------------------------------------------------------------
void ObjectTreeWidget::onTimeLineModified(core::TimeLineEvent& aEvent, bool) {
    auto type = aEvent.type();
    if (type != core::TimeLineEvent::Type_PushKey && type != core::TimeLineEvent::Type_RemoveKey) {
        return;
    }

    // update height of items in proportion to the key type count
    if (updateItemHeights(this->topLevelItem(0))) {
        notifyViewUpdated();
    }
}

void ObjectTreeWidget::onItemChanged(QTreeWidgetItem* aItem, int aColumn) {
#if 0
    if (aColumn == kItemColumn)
    {
        this->closePersistentEditor(aItem, aColumn);
    }
#else
    // is this ok?
    (void)aItem;
    (void)aColumn;
    endRenameEditor();
#endif
}

void ObjectTreeWidget::onItemClicked(QTreeWidgetItem* aItem, int aColumn) {
    endRenameEditor();

    obj::Item* objItem = obj::Item::cast(aItem);
    if (aColumn == kItemColumn && objItem && !objItem->isTopNode()) {
        if (mProject) {
            const bool isVisible = objItem->checkState(kItemColumn) == Qt::Checked;
            objItem->node().setVisibility(isVisible);
            onVisibilityUpdated();
        }
    }
}

void ObjectTreeWidget::onItemCollapsed(QTreeWidgetItem* aItem) {
    (void)aItem;
    endRenameEditor();

    notifyViewUpdated();
}

void ObjectTreeWidget::onItemExpanded(QTreeWidgetItem* aItem) {
    (void)aItem;
    endRenameEditor();

    notifyViewUpdated();
}

void ObjectTreeWidget::onItemSelectionChanged() {
    core::ObjectNode* representNode = findSelectingRepresentNode();
    onSelectionChanged(representNode);
}

void ObjectTreeWidget::onContextMenuRequested(const QPoint& aPos) {
    endRenameEditor();

    mActionItem = this->itemAt(aPos);
    if (mActionItem) {
        obj::Item* objItem = obj::Item::cast(mActionItem);
        QMenu menu(this);
        if (objItem && !objItem->isTopNode()) {
            mSlimAction->setText(objItem->node().isSlimmedDown() ? tr("Enlarge") : tr("Contract"));
            menu.addAction(mSlimAction);
            menu.addSeparator();
        }
        if (objItem && objItem->isTopNode()) {
            menu.addAction(mAddTreeAction);
            menu.addAction(mReconstructAction);
            menu.addSeparator();
        }

        menu.addAction(mRenameAction);
        menu.addAction(mObjectAction);
        menu.addAction(mFolderAction);

        if (objItem && objItem->node().parent()) {
            menu.addAction(mPasteAction);
            menu.addAction(mObjectMirror);
            menu.addSeparator();
            menu.addAction(mDeleteAction);
        }

        menu.exec(this->mapToGlobal(aPos));
    }
}

void ObjectTreeWidget::onSlimActionTriggered(bool) {
    if (mActionItem) {
        obj::Item* objItem = obj::Item::cast(mActionItem);
        if (objItem && !objItem->isTopNode()) {
            objItem->node().setSlimDown(!objItem->node().isSlimmedDown());
            if (updateItemHeights(this->topLevelItem(0))) {
                notifyViewUpdated();
            }
        }
    }
}

void getResChild(img::ResourceNode* curNode, QVector<ObjectTreeWidget::resource>* resources) {
    if (curNode) {
        resources->emplace_back();
        resources->last().name = curNode->data().identifier();
        resources->last().node = curNode;
        resources->last().isFolder = !curNode->data().isLayer();
        if (!curNode->data().isLayer()) {
            resources->last().childCount = static_cast<int>(curNode->children().size());
            for (const auto child : curNode->children()) {
                resources->last().children.append(child);
            }
            for (const auto child: resources->last().children) {
                getResChild(child, resources);
            }
        }
    }
}

void getTargetChild(core::ObjectNode* curNode, QVector<QString>* resources) {
    if (curNode) {
        resources->append(curNode->name());
        for (const auto child : curNode->children()) {
            getTargetChild(child, resources);
        }
    }
}

void ObjectTreeWidget::addItems(QStringList targets = QStringList()) {
    if (mActionItem) {
        obj::Item* objItem = obj::Item::cast(mActionItem);
        // get children for current nodes and the resource and then add the missing resources
        QVector<resource> resources;
        QVector<QString> currentResources;
        QVector<QString> parsedResources;
        if (objItem && objItem->isTopNode()) {
            for (const auto& tree: mProject->resourceHolder().imageTrees()) {
                if(QFileInfo(tree.filePath).baseName() == objItem->node().name() || targets.contains(QFileInfo(tree.filePath).baseName())) {
                    // This is what sleep deprivation does to someone
                    qDebug() << "Filetree " << QFileInfo(tree.filePath).baseName() << " found for the selected node.";
                    img::ResourceNode* topNode = tree.topNode;
                    if (topNode->children().empty()) {
                        getResChild(topNode, &resources);
                    }
                    for (const auto child: topNode->children()) {
                        getResChild(child, &resources);
                    }
                    for (const auto child: objItem->node().children()) {
                        getTargetChild(child, &currentResources);
                    }
                    // We do this to get a mostly accurate image depth on load
                    std::reverse(resources.begin(), resources.end());
                    for (const auto& target: resources) {
                        qDebug() << "Target: " << target.name;
                        const bool containsTarget = currentResources.contains(target.name);
                        qDebug() << "Object tree contains target: " << containsTarget;
                        if (!containsTarget) {
                            if (target.isFolder && !parsedResources.contains(target.name)) {
                                addFolder(
                                    mActionItem->treeWidget()->topLevelItem(0),
                                    mProject->objectTree().topNode(),
                                    false,
                                    -1,
                                    target.node,
                                    &parsedResources,
                                    &resources
                                );
                            }
                            else if (!parsedResources.contains(target.name)) {
                                addLayer(
                                    mActionItem->treeWidget()->topLevelItem(0),
                                    mProject->objectTree().topNode(),
                                    false,
                                    -1,
                                    target.node,
                                    &parsedResources,
                                    &resources
                                    );
                            }
                        }
                    }
                }
                else {
                    QMessageBox errorMsg;
                    errorMsg.setWindowTitle(tr("Filetree not found"));
                    errorMsg.setText("Filetree for " + QFileInfo(tree.filePath).baseName() + " was not found, tree has probably been renamed.");
                    errorMsg.exec();
                }
                qDebug("\nTree Identifier");
                qDebug() << QFileInfo(tree.filePath).baseName();
                qDebug("\nObjItem Identifier");
                qDebug() << objItem->node().name();
            }
        }
    }
}

void ObjectTreeWidget::onAddTreeTriggered(bool) {
    if (mActionItem) {
        const obj::Item* objItem = obj::Item::cast(mActionItem);
        // get children for current nodes and the resource and then add the missing resources
        QVector<resource> resources;
        QVector<QString> currentResources;
        QVector<QString> parsedResources;
        if (objItem && objItem->isTopNode()) {
            QStringList treeNames;
            for (const auto& tree : mProject->resourceHolder().imageTrees()) {
                treeNames.append(QFileInfo(tree.filePath).baseName());
            }
            QWidget* treeSelector = new QWidget();
            QGridLayout* gridLayout_2;
            QScrollArea* treeScroll;
            QWidget* treeScrollWidget;
            QPushButton* okButton;
            QPushButton* no_button;
            QFormLayout* formLayout;
            QVector<QCheckBox*> treeCheckBoxes;
            QVector<QLabel*> treeLabels;

            if (treeSelector->objectName().isEmpty())
                treeSelector->setObjectName("treeSelector");
            treeSelector->setMinimumSize(300, 200);
            gridLayout_2 = new QGridLayout(treeSelector);
            gridLayout_2->setObjectName("gridLayout_2");
            okButton = new QPushButton(treeSelector);
            okButton->setObjectName("pushButton");
            okButton->setText(tr("Add"));

            gridLayout_2->addWidget(okButton, 1, 0, 1, 1);

            no_button = new QPushButton(treeSelector);
            no_button->setObjectName("pushButton_2");
            no_button->setText(tr("Cancel"));

            gridLayout_2->addWidget(no_button, 1, 1, 1, 1);

            treeScroll = new QScrollArea(treeSelector);
            treeScroll->setObjectName("treeScroll");
            treeScroll->setWidgetResizable(true);
            treeScrollWidget = new QWidget();
            treeScrollWidget->setObjectName("treeScrollWidget");
            treeScrollWidget->setGeometry(treeSelector->rect());
            formLayout = new QFormLayout(treeScrollWidget);
            formLayout->setObjectName("formLayout");

            for (const auto& treeName : treeNames) {
                treeCheckBoxes.append(new QCheckBox(treeScrollWidget));
                QSizePolicy sizePolicy(QSizePolicy::Policy::MinimumExpanding, QSizePolicy::Policy::MinimumExpanding);
                sizePolicy.setHeightForWidth(treeCheckBoxes.last()->sizePolicy().hasHeightForWidth());
                treeCheckBoxes.last()->setChecked(false);
                treeCheckBoxes.last()->setText(treeName);
                treeCheckBoxes.last()->setSizePolicy(sizePolicy);
                treeCheckBoxes.last()->setCursor(QCursor(Qt::PointingHandCursor));
                formLayout->addWidget(treeCheckBoxes.last());
            }
            treeScroll->setWidget(treeScrollWidget);
            gridLayout_2->addWidget(treeScroll, 0, 0, 1, 2);

            okButton->connect(okButton,&QPushButton::clicked,
            [=] {
                QStringList selectedTrees;
                for (auto treeCheckBox : treeCheckBoxes) {
                    qDebug() << treeCheckBox->text();
                    if (treeCheckBox->isChecked()) {
                        selectedTrees.append(treeCheckBox->text());
                    }
                }
                if (selectedTrees.empty()) {
                    QMessageBox errorMsg;
                    errorMsg.setWindowTitle(tr("No trees selected"));
                    errorMsg.setText(tr("Please select at least one tree."));
                    errorMsg.exec();
                }
                else {
                    addItems(selectedTrees);
                    QMessageBox::information(treeSelector, tr("Done"), tr("Tree addition was attempted, layers with duplicate names have been suppressed."));
                    treeSelector->close();
                    treeSelector->deleteLater();
                }
            });
            no_button->connect(no_button, &QPushButton::clicked, [=] { treeSelector->close(); treeSelector->deleteLater();});

            treeSelector->show();
        }
    }
}

void ObjectTreeWidget::onObjectReconstructionTriggered(bool) {
    addItems();
}



void ObjectTreeWidget::onRenameActionTriggered(bool) {
    if (mActionItem) {
        if (obj::Item::cast(mActionItem)) {
            this->openPersistentEditor(mActionItem, kItemColumn);
            this->editItem(mActionItem, kItemColumn);
        }
    }
}

int extractIntFromStr(const QString& str) {
    return QRegularExpression(R"(-?\b\d+(?:\.\d+)?\b)").match(str).captured(0).toInt();
}

void ObjectTreeWidget::onPasteActionTriggered(bool) const {
    if (!mActionItem) {
        return;
    }
    if (!obj::Item::cast(mActionItem)) {
        return;
    }
    obj::Item* objItem = obj::Item::cast(mActionItem);
    bool objIsFolder = objItem->childCount() != 0;
    auto pasteReturnVal = mEditor->pasteCbKeys(objItem, mProject->pointee(), objIsFolder);
    // Result handler
    QStringList returnVal = pasteReturnVal.split("\n");
    int successNum = extractIntFromStr(returnVal.first());
    bool aKeyErrored = returnVal.size() > 1;
    QStringList errors = returnVal.filter("Error");
    QStringList nullLogs = returnVal.filter(QRegularExpression("^(?!.*[\\d:])[^\\n]*$"));
    auto keyTypeErrors = returnVal.filter("Key types errored");
    auto keys = mEditor->getTypesFromCb(mProject->pointee());

    QMessageBox box;
    if (successNum != 0) {
        if (!aKeyErrored) {
            box.setText(tr("Successfully pasted ") + QString::number(successNum) + tr(" keys."));
        } else {
            box.setText(
                tr("Successfully pasted ") + QString::number(successNum) + tr(" keys.\n") +
                QString::number(errors.size()) + tr(" error(s) have been detected.\nThe log is available below.")
            );
        }
    }
    else {
        box.setText(tr("Failed to paste key(s)"));
        if (!aKeyErrored) {
            box.setDetailedText(
                tr("Clipboard does not contain valid JSON information or timeline already has a key in the same frame.")
            );
        }
    }
    if (aKeyErrored && !errors.empty() && !nullLogs.empty() && !keyTypeErrors.empty()) {
        QString errorLog;
        errorLog.append("--- Error log ---\n");
        errorLog.append(errors.join(".\n"));
        errorLog.append("\n-----\n");
        errorLog.append(nullLogs.join(".\n"));
        errorLog.append("\n-----\n");
        errorLog.append(keyTypeErrors.join(".\n"));
        errorLog.append("\n-----\n");
        box.setDetailedText(errorLog);
    }
    if (successNum != 0) {
        // It doesn't work without this for some godforsaken reason.
        for (auto & x : keys) {
            if (objItem->node().timeLine()->hasTimeKey(core::TimeKeyType_Image, x->frame())) {
                auto key = ((const core::ImageKey*)&x);
                ctrl::TimeLineUtil::assignImageKeyCellSize(
                    *mProject, objItem->node(), x->frame(), key->data().gridMesh().cellSize()
                );
            }
        }
    }
    if (box.text().isNull()) {
        box.setText(tr("Failed to paste key(s)"));
    }
    if (box.detailedText().isNull() && successNum == 0) {
        box.setDetailedText(tr("Timeline has a key in the same frame."));
    }
    box.exec();
}

void ObjectTreeWidget::onObjectActionTriggered(bool) {
    if (mActionItem) {
        obj::Item* objItem = obj::Item::cast(mActionItem);

        core::ObjectNode* parent = nullptr;
        int index = -1;
        float depth = 0.0f;

        QTreeWidgetItem* parentItem = nullptr;
        int itemIndex = -1;

        // top node
        if (!objItem || objItem->isTopNode()) {
            parent = mProject->objectTree().topNode();
            XC_PTR_ASSERT(parent);

            index = static_cast<int>(parent->children().size());
            if (index > 0) {
                auto prevNode = parent->children().back();
                depth = prevNode->initialDepth() - 1.0f;
            }
            parentItem = mActionItem;
            itemIndex = parentItem->childCount();
        } else // sub node
        {
            auto prevNode = &(objItem->node());
            depth = prevNode->initialDepth() + 1.0f;

            parent = prevNode->parent();
            if (!parent)
                return;

            index = parent->children().indexOf(prevNode);
            if (index < 0)
                return;

            parentItem = mActionItem->parent();
            if (!parentItem)
                return;

            itemIndex = parentItem->indexOfChild(objItem);
            if (itemIndex < 0)
                return;
        }

        // show resource dialog
        QScopedPointer<ResourceDialog> dialog(new ResourceDialog(mViaPoint, true, this));
        dialog->setProject(mProject.get());
        dialog->exec();

        // create command
        if (dialog->hasValidNode()) {
            int node_count = dialog->nodeList().count();
            for (int i = 0; i < node_count; i++) {
                // get resource
                auto resNode = dialog->nodeList().at(i);
                if (!resNode)
                    return;
                XC_ASSERT(resNode->data().hasImage());

                // create node
                core::LayerNode* ptr =
                    new core::LayerNode(resNode->data().identifier(), mProject->objectTree().shaderHolder());
                ptr->setVisibility(true);
                ptr->setDefaultImage(resNode->handle());
                ptr->setDefaultPosture(QVector2D(mProject->objectTree().topNode()->initialRect().center()));
                ptr->setDefaultDepth(depth);
                ptr->setDefaultOpacity(1.0f); // @todo support default opacity


                cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Create a layer"));
                // notifier
                {
                    auto coreNotifier = new core::ObjectTreeNotifier(*mProject);
                    coreNotifier->event().setType(core::ObjectTreeEvent::Type_Add);
                    coreNotifier->event().pushTarget(parent, *ptr);
                    macro.grabListener(coreNotifier);
                }
                macro.grabListener(new obj::RestructureNotifier(*this));

                // create commands
                mProject->commandStack().push(new cmnd::GrabNewObject<core::LayerNode>(ptr));
                mProject->commandStack().push(new cmnd::InsertTree<core::ObjectNode>(&(parent->children()), index, ptr)
                );

                // create gui commands
                auto itemPtr = createFileItem(*ptr);
                mProject->commandStack().push(new cmnd::GrabNewObject<obj::Item>(itemPtr));
                mProject->commandStack().push(new obj::InsertItem(*parentItem, itemIndex, *itemPtr));
            }
        }
    }
}

template <typename tlKey>
void addKeyToTimeLine(core::TimeLine* tl, cmnd::Stack& stack, const tlKey& keys) {
    QHash<const core::TimeKey*, core::TimeKey*> parentMap;
    struct ChildInfo {
        core::TimeKey* key;
        core::TimeKey* parent;
    };
    QList<ChildInfo> childList;
    for(auto key: keys) {
        XC_PTR_ASSERT(tl);
        auto copiedKey = key;
        XC_PTR_ASSERT(copiedKey);
        auto parentKey = copiedKey->parent();
        core::TimeKey* newKey = copiedKey->createClone();
        auto newFrame = key->frame();
        newKey->setFrame(newFrame);
        stack.push(new cmnd::GrabNewObject(newKey));
        stack.push(tl->createPusher(key->type(), newFrame, newKey));
        if (newKey->canHoldChild()) {
            parentMap[copiedKey] = newKey;
        }
        if (parentKey) {
            ChildInfo info = {newKey, parentKey};
            childList.push_back(info);
        }
    }
    // connect to parents
    for (auto child : childList) {
        auto parent = child.parent;
        // if the parent was also copied, connect to a new parent key.
        auto it = parentMap.find(parent);
        if (it != parentMap.end())
            parent = it.value();
        stack.push(new cmnd::PushBackTree<core::TimeKey>(&parent->children(), child.key));
    }
}
void ObjectTreeWidget::addFolder(QTreeWidgetItem* curActionItem, core::ObjectNode* itemNode, const bool moveToFolder, // NOLINT(*-no-recursion)
    const int folderIndex, img::ResourceNode* resNode, QVector<QString>* parsedRes, QVector<resource>* res){
    obj::Item* objItem = obj::Item::cast(curActionItem);
    core::ObjectNode* parent;
    int index;
    QTreeWidgetItem* parentItem;
    int itemIndex;

    // top node
    if (objItem->isTopNode()) {
        parent = mProject->objectTree().topNode();
        XC_PTR_ASSERT(parent);
        index = static_cast<int>(parent->children().size());
        parentItem = curActionItem;
        itemIndex = parentItem->childCount();
    }
    else {
        const auto node = &objItem->node();
        parent = node->parent();
        if (!parent) { return; }
        index = parent->children().indexOf(node);
        if (index < 0) { return; }
        parentItem = curActionItem->parent();
        if (!parentItem) { return; }
        itemIndex = parentItem->indexOfChild(objItem);
        if (itemIndex < 0) { return; }
    }

    const auto isResNode = resNode && parsedRes && res;
    int resIdx = 0;
    if (isResNode) { for (auto x = 0; x < res->size(); x++) { if (res->at(x).node == resNode) { resIdx = x; } } }
    core::FolderNode* ptr = nullptr;

    if (isResNode) {
        ptr = new core::FolderNode(resNode->data().identifier());
        ptr->setClipped(false);
        ptr->setVisibility(true);
        ptr->setDefaultDepth(std::max(1.0f, static_cast<float>(resIdx)));
        ptr->setBlendMode(resNode->data().blendMode());
        ptr->setInitialRect(resNode->data().rect());
    }
    else {
        ptr = new core::FolderNode(itemNode->name() + " (Copy)");
        ptr->setClipped(itemNode->renderer()->isClipped());
        ptr->setVisibility(itemNode->isVisible());
        ptr->setDefaultDepth(itemNode->initialDepth());
        ptr->setBlendMode(itemNode->renderer()->blendMode());
        ptr->setInitialRect(itemNode->initialRect());
    }
    ptr->setDefaultOpacity(1.0f);
    // We get all keys now
    core::TimeLine* ptrTl = ptr->timeLine();
    ptrTl->current() = objItem->node().timeLine()->current();
    for (int tIndex = 0; tIndex != core::TimeKeyType_TERM; tIndex++) {
        addKeyToTimeLine(ptrTl, mProject->commandStack(), objItem->node().timeLine()->map(static_cast<core::TimeKeyType>(tIndex)));
    }
    for (const auto bone: ptrTl->map(core::TimeKeyType_Bone)) {
        dynamic_cast<core::BoneKey*>(bone)->resetCaches(*mProject, objItem->node());
    }
    ptr->timeLine()->current().clearCaches();
    ptr->timeLine()->current().clearMasterCache();

    const auto itemPtr = createFolderItem(*ptr);
    {
        cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Duplicate base folder"));
        // notifier
        {
            const auto coreNotifier = new core::ObjectTreeNotifier(*mProject);
            coreNotifier->event().setType(core::ObjectTreeEvent::Type_Add);
            coreNotifier->event().pushTarget(parent, *ptr);
            macro.grabListener(coreNotifier);
        }
        macro.grabListener(new obj::RestructureNotifier(*this));
        // push commands
        const bool validMove = moveToFolder && folderIndex != -1;
        mProject->commandStack().push(new cmnd::GrabNewObject(ptr));
        if (validMove) {
            mProject->commandStack().push(new cmnd::InsertTree<core::ObjectNode>(&objItem->node().children(),
                static_cast<int>(objItem->node().children().size()), ptr));
        }
        else {
            mProject->commandStack().push(new cmnd::InsertTree<core::ObjectNode>(&parent->children(), index, ptr));
        }
        // push gui item commands
        mProject->commandStack().push(new cmnd::GrabNewObject(itemPtr));
        if (validMove) {
            mProject->commandStack().push(new obj::InsertItem(*curActionItem, folderIndex, *itemPtr));
        }
        else {
            mProject->commandStack().push(new obj::InsertItem(*parentItem, itemIndex, *itemPtr));
        }
    }

    if (isResNode) {
        for (const auto child: res->at(resIdx).children){
            if(child->data().isLayer()) {
                addLayer(itemPtr, itemNode, true, static_cast<int>(itemPtr->node().children().size()), child, parsedRes, res);
            }
            else{
                addFolder(itemPtr, itemNode, true, static_cast<int>(itemPtr->node().children().size()), child, parsedRes, res);
            }
        }
    }

    else {
        for(core::ObjectNode* node: itemNode->children()){
            if(node->type() == core::ObjectType_Layer) {
                addLayer(itemPtr, node, true, static_cast<int>(itemPtr->node().children().size()));
            }
            else if(node->type() == core::ObjectType_Folder){
                addFolder(itemPtr, node, true, static_cast<int>(itemPtr->node().children().size()));
            }
        }
    }

    if (isResNode) { parsedRes->append(resNode->data().identifier()); }
}
void ObjectTreeWidget::addLayer(QTreeWidgetItem* curActionItem, core::ObjectNode* itemNode,
    const bool moveToFolder, const int folderIndex, img::ResourceNode* resNode, QVector<QString>* parsedRes,
    const QVector<resource>* res){
    obj::Item* objItem = obj::Item::cast(curActionItem);
    core::ObjectNode* parent;
    QTreeWidgetItem* parentItem;
    int index;
    int itemIndex;

    // top node
    if (objItem->isTopNode()) {
        parent = mProject->objectTree().topNode();
        XC_PTR_ASSERT(parent);
        index = static_cast<int>(parent->children().size());
        parentItem = curActionItem;
        itemIndex = parentItem->childCount();
    }
    else {
        const auto node = &objItem->node();
        parent = node->parent();
        if (!parent) { return; }
        index = parent->children().indexOf(node);
        if (index < 0) { return; }
        parentItem = curActionItem->parent();
        if (!parentItem) { return; }
        itemIndex = parentItem->indexOfChild(objItem);
        if (itemIndex < 0) { return; }
    }

    const auto isResNode = resNode && parsedRes && res;

    auto* itemTL = itemNode->timeLine();

    QString nodeName;
    if (isResNode) {
        nodeName = resNode->data().identifier();
    } else {
        nodeName = itemNode->name() + " (Copy)";
    }
    img::ResourceHandle resHandle;
    if (isResNode) {
        resHandle = resNode->handle();
    } else {
        resHandle = itemTL->current().areaImageKey()->data().resource();
    }
    // create node
    auto* ptr = new core::LayerNode(nodeName, mProject->objectTree().shaderHolder());

    int resIdx = 0;
    if (isResNode) { for (auto x = 0; x < res->size(); x++) { if (res->at(x).node == resNode) { resIdx = x; } } }

    ptr->setDefaultImage(resHandle);
    ptr->setDefaultPosture(resHandle->center());
    ptr->setBlendMode(resHandle->blendMode());
    ptr->setInitialRect(resHandle->rect());
    if (isResNode) {
        ptr->setVisibility(resHandle->isVisible());
        ptr->setDefaultDepth(std::max(1.0f, static_cast<float>(resIdx)));
        ptr->setDefaultOpacity(1.0f);
        ptr->setClipped(false);
    }
    else{
        ptr->setVisibility(itemNode->isVisible());
        ptr->timeLine()->current().setImageOffset(itemTL->current().imageOffset());
        ptr->setDefaultDepth(itemNode->initialDepth());
        ptr->setDefaultOpacity(itemTL->current().opa().opacity());
        ptr->setClipped(objItem->node().renderer()->isClipped());
        // We get all keys now
        core::TimeLine* ptrTl = ptr->timeLine();
        ptrTl->current() = itemTL->current();
        for (int tIndex = 0; tIndex != core::TimeKeyType_TERM; tIndex++) {
            addKeyToTimeLine(ptrTl, mProject->commandStack(), itemTL->map(static_cast<core::TimeKeyType>(tIndex)));
        }
        for (const auto bone: ptrTl->map(core::TimeKeyType_Bone)) {
            dynamic_cast<core::BoneKey*>(bone)->resetCaches(*mProject, objItem->node());
        }
    }

    {
        cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Duplicate layer"));
        // notifier
        {
            auto coreNotifier = new core::ObjectTreeNotifier(*mProject);
            coreNotifier->event().setType(core::ObjectTreeEvent::Type_Add);
            coreNotifier->event().pushTarget(parent, *ptr);
            macro.grabListener(coreNotifier);
        }
        macro.grabListener(new obj::RestructureNotifier(*this));
        // create commands
        const bool validMove = moveToFolder && folderIndex != -1;
        mProject->commandStack().push(new cmnd::GrabNewObject(ptr));
        if (validMove) {
            mProject->commandStack().push(new cmnd::InsertTree<core::ObjectNode>(&objItem->node().children(),
                static_cast<int>(objItem->node().children().size()), ptr));
        }
        else {
            mProject->commandStack().push(new cmnd::InsertTree<core::ObjectNode>(&parent->children(), index, ptr));
        }
        // create gui commands
        obj::Item* itemPtr = createFileItem(*ptr);
        mProject->commandStack().push(new cmnd::GrabNewObject(itemPtr));

        if (validMove) {
            mProject->commandStack().push(new obj::InsertItem(*curActionItem, folderIndex, *itemPtr));
        }
        else {
            mProject->commandStack().push(new obj::InsertItem(*parentItem, itemIndex, *itemPtr));
        }

    }

    if (isResNode) { parsedRes->append(resNode->data().identifier()); }
}
void ObjectTreeWidget::onObjectMirrorTriggered() {
    if(mActionItem) {
        obj::Item* objItem = obj::Item::cast(mActionItem);
        if (objItem->node().type() == core::ObjectType_Layer) { addLayer(mActionItem, &objItem->node()); }
        else if (objItem->node().type() == core::ObjectType_Folder) {
            addFolder(mActionItem, &objItem->node());
        }
    }
}

void ObjectTreeWidget::onFolderActionTriggered(bool) {
    if (mActionItem) {
        obj::Item* objItem = obj::Item::cast(mActionItem);

        core::ObjectNode* parent = nullptr;
        int index = -1;
        float depth = 0.0f;

        QTreeWidgetItem* parentItem = nullptr;
        int itemIndex = -1;

        // top node
        if (!objItem || objItem->isTopNode()) {
            parent = mProject->objectTree().topNode();
            XC_PTR_ASSERT(parent);

            index = static_cast<int>(parent->children().size());
            if (index > 0) {
                auto prevNode = parent->children().back();
                depth = prevNode->initialDepth() - 1.0f;
            }
            parentItem = mActionItem;
            itemIndex = parentItem->childCount();
        } else // sub node
        {
            auto prevNode = &(objItem->node());
            depth = prevNode->initialDepth() + 1.0f;

            parent = prevNode->parent();
            if (!parent)
                return;

            index = parent->children().indexOf(prevNode);
            if (index < 0)
                return;

            parentItem = mActionItem->parent();
            if (!parentItem)
                return;

            itemIndex = parentItem->indexOfChild(objItem);
            if (itemIndex < 0)
                return;
        }

        // create command
        {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("create a folder object"));

            // create node
            auto* ptr = new core::FolderNode("folder0");
            ptr->setDefaultPosture(QVector2D());
            ptr->setDefaultDepth(depth);
            ptr->setDefaultOpacity(1.0f); // @todo support default opacity
            // notifier
            {
                auto coreNotifier = new core::ObjectTreeNotifier(*mProject);
                coreNotifier->event().setType(core::ObjectTreeEvent::Type_Add);
                coreNotifier->event().pushTarget(parent, *ptr);
                macro.grabListener(coreNotifier);
            }
            macro.grabListener(new obj::RestructureNotifier(*this));

            // push commands
            mProject->commandStack().push(new cmnd::GrabNewObject<core::FolderNode>(ptr));
            mProject->commandStack().push(new cmnd::InsertTree<core::ObjectNode>(&(parent->children()), index, ptr));

            // push gui item commands
            auto itemPtr = createFolderItem(*ptr);
            mProject->commandStack().push(new cmnd::GrabNewObject<obj::Item>(itemPtr));
            mProject->commandStack().push(new obj::InsertItem(*parentItem, itemIndex, *itemPtr));
        }
    }
}

void ObjectTreeWidget::onDeleteActionTriggered(bool) {
    if (mActionItem) {
        obj::Item* objItem = obj::Item::cast(mActionItem);
        if (!objItem || objItem->isTopNode())
            return;

        core::ObjectNode& node = objItem->node();

        core::ObjectNode* parent = node.parent();
        if (!parent)
            return;

        QTreeWidgetItem* parentItem = mActionItem->parent();
        if (!parentItem)
            return;

        const int itemIndex = parentItem->indexOfChild(objItem);
        if (itemIndex < 0)
            return;

        // delete command
        {
            cmnd::ScopedMacro macro(mProject->commandStack(), CmndName::tr("Delete object"));
            {
                auto coreNotifier = new core::ObjectTreeNotifier(*mProject);
                coreNotifier->event().setType(core::ObjectTreeEvent::Type_Delete);
                coreNotifier->event().pushTarget(parent, node);
                macro.grabListener(coreNotifier);
            }
            macro.grabListener(new obj::RestructureNotifier(*this));

            // delete node
            mProject->commandStack().push(mProject->objectTree().createNodeDeleter(node));

            // delete item
            mProject->commandStack().push(new obj::RemoveItem(*parentItem, itemIndex, *objItem));
            mProject->commandStack().push(new cmnd::GrabDeleteObject<obj::Item>(objItem));
        }
    }
}

//-------------------------------------------------------------------------------------------------
void ObjectTreeWidget::paintEvent(QPaintEvent* aEvent) {
    QTreeWidget::paintEvent(aEvent);

    if (mDragIndex.isValid()) {
        QPainter painter(this->viewport());

        const QTreeWidgetItem* item = this->itemFromIndex(mDragIndex);
        QRect itemRect = this->visualItemRect(item);

        const QBrush kBrush(QColor(140, 140, 140, 80));
        const QBrush kPenBrush(QColor(100, 100, 100, 200));
        painter.setBrush(kBrush);
        painter.setPen(QPen(kPenBrush, 1));

        QPoint pos;
        if (mDropIndicatorPos == AboveItem) {
            pos = itemRect.topLeft();
            painter.drawLine(pos, pos + QPoint(itemRect.width(), 0));
        } else if (mDropIndicatorPos == OnItem) {
            pos = QPoint(itemRect.left(), itemRect.center().y());
            painter.drawRect(itemRect);
        } else if (mDropIndicatorPos == BelowItem) {
            pos = QPoint(itemRect.left(), itemRect.bottom() + 1);
            painter.drawLine(pos, pos + QPoint(itemRect.width(), 0));
        }
        QPolygon arrow;
        arrow.push_back(pos);
        arrow.push_back(pos + QPoint(-6, -4));
        arrow.push_back(pos + QPoint(-6, +4));
        painter.setRenderHint(QPainter::Antialiasing);
        painter.drawConvexPolygon(arrow);
        painter.end();
    }
}

void ObjectTreeWidget::showEvent(QShowEvent* aEvent) {
    QTreeWidget::showEvent(aEvent);

    if (this->horizontalScrollBar()) {
        this->setViewportMargins(0, 0, 0, this->horizontalScrollBar()->sizeHint().height());
    }
}

void ObjectTreeWidget::dragMoveEvent(QDragMoveEvent* aEvent) {
    QPoint cheatPos = aEvent->position().toPoint();
    mDragIndex = cheatDragDropPos(cheatPos);

    QDragMoveEvent dummyEvent(
        cheatPos,
        aEvent->dropAction(),
        aEvent->mimeData(),
        aEvent->buttons(),
        aEvent->modifiers(),
        aEvent->type()
    );
    QTreeWidget::dragMoveEvent(&dummyEvent);

    if (!dummyEvent.isAccepted()) {
        mDragIndex = QModelIndex();
        mDropIndicatorPos = DropIndicatorPosition();
        aEvent->ignore();
    } else {
        aEvent->accept();
        mDropIndicatorPos = this->dropIndicatorPosition();
    }
}

void ObjectTreeWidget::dropEvent(QDropEvent* aEvent) {
    mDragIndex = QModelIndex();
    QPoint cheatPos = aEvent->position().toPoint();
    cheatDragDropPos(cheatPos);
    QDropEvent dummyEvent(
        cheatPos,
        aEvent->dropAction(),
        aEvent->mimeData(),
        aEvent->buttons(),
        aEvent->modifiers(),
        aEvent->type()
    );
    QModelIndex cursorIndex = this->indexAt(aEvent->position().toPoint());

    if (this->visualRect(cursorIndex).contains(aEvent->position().toPoint())) {
        mRemovedPositions.clear();
        mInsertedPositions.clear();

        // begin move
        mStoreInsert = true;
        QTreeWidget::dropEvent(&dummyEvent);
        mStoreInsert = false;

        // finalize move command
        if (mMacroScope) {
            // item mover
            mProject->commandStack().push(new obj::MoveItems(*this, mRemovedPositions, mInsertedPositions));

            // node mover
            mProject->commandStack().push(mProject->objectTree().createNodesMover(mRemovedPositions, mInsertedPositions)
            );

            mMacroScope->grabListener(new obj::RestructureNotifier(*this));
            mMacroScope.destruct();
        }
    } else {
        aEvent->ignore();
    }
}

void ObjectTreeWidget::rowsAboutToBeRemoved(const QModelIndex& aParent, int aStart, int aEnd) {
    if (mStoreInsert) {
        XC_ASSERT(aStart == aEnd);
        QTreeWidgetItem* item = this->itemFromIndex(aParent.model()->index(aStart, kItemColumn, aParent));
        util::TreePos removePos(this->indexFromItem(item));
        XC_ASSERT(removePos.isValid());
        // qDebug() << "remove"; removePos.dump();

        mRemovedPositions.push_back(removePos);

        // firstly, create macro
        if (mProject && !mMacroScope) {
            mObjTreeNotifier = new core::ObjectTreeNotifier(*mProject);
            mObjTreeNotifier->event().setType(core::ObjectTreeEvent::Type_Move);

            mMacroScope.construct(mProject->commandStack(), CmndName::tr("Move object"));
            mMacroScope->grabListener(mObjTreeNotifier);
        }
        // record target
        if (mMacroScope) {
            auto objItem = obj::Item::cast(item);
            if (objItem) {
                core::ObjectNode& node = objItem->node();
                mObjTreeNotifier->event().pushTarget(node.parent(), node);
                // qDebug() << "node" << node.name() << (node.parent() ? node.parent()->name() : "");
            }
        }
    }
    QTreeWidget::rowsAboutToBeRemoved(aParent, aStart, aEnd);
}

void ObjectTreeWidget::rowsInserted(const QModelIndex& aParent, int aStart, int aEnd) {
    if (mStoreInsert) {
        XC_ASSERT(aStart == aEnd);
        QTreeWidgetItem* item = this->itemFromIndex(aParent.model()->index(aStart, kItemColumn, aParent));
        util::TreePos insertPos(this->indexFromItem(item));
        XC_ASSERT(insertPos.isValid());
        // qDebug() << "insert"; insertPos.dump();

        mInsertedPositions.push_back(insertPos);
    }
    QTreeWidget::rowsInserted(aParent, aStart, aEnd);
}

void ObjectTreeWidget::scrollContentsBy(int aDx, int aDy) {
    QTreeWidget::scrollContentsBy(aDx, aDy);
    onScrollUpdated(scrollHeight());
}

void ObjectTreeWidget::resizeEvent(QResizeEvent* aEvent) {
    QTreeWidget::resizeEvent(aEvent);
    onScrollUpdated(scrollHeight());
}

void ObjectTreeWidget::scrollTo(const QModelIndex& aIndex, ScrollHint aHint) {
    // Avoided qt bug that QTreeView scroll incorrectly (in scrollTo, EnsureVisible).
    if (aHint == EnsureVisible) {
        auto view = this->viewport()->rect();
        auto rect = this->visualRect(aIndex);
        if (view.top() <= rect.top() && rect.bottom() <= view.bottom()) {
            return; // nothing to do
        }
    }
    QTreeWidget::scrollTo(aIndex, aHint);
}

} // namespace gui
