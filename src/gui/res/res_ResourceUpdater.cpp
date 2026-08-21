#include <utility>
#include <iostream>
#include <QFileInfo>
#include <QMessageBox>
#include "util/TextUtil.h"
#include "util/TreeUtil.h"
#include "img/PSDReader.h"
#include "img/PSDUtil.h"
#include "img/BlendMode.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "ctrl/CmndName.h"
#include "gui/res/res_ImageSetter.h"
#include "gui/res/res_ResourceUpdater.h"
#include "img/Util.h"
#include "deps/zip_file.h"

// #define RESOURCE_UPDATER_DUMP(...) XC_DEBUG_REPORT(__VA_ARGS__)
#define RESOURCE_UPDATER_DUMP(...)

namespace gui::res {

    //-------------------------------------------------------------------------------------------------
    class NewTreePusher: public cmnd::Stable {
        core::ResourceHolder& mHolder;
        img::ResourceNode* mTree;
        QString mAbsFilePath;
        bool mDone;

    public:
        NewTreePusher(core::ResourceHolder& aHolder, img::ResourceNode* aTree, const QString& aFilePath):
            mHolder(aHolder), mTree(aTree), mAbsFilePath(QFileInfo(aFilePath).absoluteFilePath()), mDone(false) {
            XC_PTR_ASSERT(aTree);
        }

        ~NewTreePusher() {
            if (!mDone) {
                delete mTree;
            }
        }

        virtual void undo() {
            mHolder.popImageTree();
            mDone = false;
        }

        virtual void redo() {
            mHolder.pushImageTree(*mTree, mAbsFilePath);
            mDone = true;
        }
    };

    //-------------------------------------------------------------------------------------------------
    class GUINewTreePusher: public cmnd::Stable {
        QTreeWidget& mTree;
        QTreeWidgetItem* mItem;
        bool mDone;

    public:
        GUINewTreePusher(QTreeWidget& aTree, QTreeWidgetItem* aItem): mTree(aTree), mItem(aItem), mDone(false) {
            XC_PTR_ASSERT(aItem);
        }

        ~GUINewTreePusher() {
            if (!mDone) {
                delete mItem;
            }
        }

        virtual void undo() {
            bool success = false;
            for (int i = 0; i < mTree.topLevelItemCount(); ++i) {
                if (mItem == mTree.topLevelItem(i)) {
                    mTree.takeTopLevelItem(i);
                    success = true;
                    break;
                }
            }
            XC_ASSERT(success);
            mDone = false;
        }

        virtual void redo() {
            mTree.addTopLevelItem(mItem);
            mDone = true;
        }
    };

    //-------------------------------------------------------------------------------------------------
    class TreeDeleter: public cmnd::Stable {
        core::ResourceHolder& mHolder;
        core::ResourceHolder::ImageTree mTree;
        int mIndex;
        bool mDone;

    public:
        TreeDeleter(core::ResourceHolder& aHolder, int aIndex):
            mHolder(aHolder), mTree(), mIndex(aIndex), mDone(false) {}

        ~TreeDeleter() {
            if (mDone) {
                delete mTree.topNode;
            }
        }

        virtual void exec() {
            mTree = mHolder.imageTree(mIndex);
            XC_PTR_ASSERT(mTree.topNode);
            redo();
        }

        virtual void undo() {
            mHolder.insertImageTree(mTree, mIndex);
            mDone = false;
        }

        virtual void redo() {
            mHolder.removeImageTree(mIndex);
            mDone = true;
        }
    };

    //-------------------------------------------------------------------------------------------------
    class GUITreeDeleter: public cmnd::Stable {
        QTreeWidget& mTree;
        QTreeWidgetItem* mTopItem;
        int mIndex;

    public:
        GUITreeDeleter(QTreeWidget& aTree, int aIndex): mTree(aTree), mTopItem(), mIndex(aIndex) {}

        ~GUITreeDeleter() {
            if (mTopItem) {
                delete mTopItem;
            }
        }

        virtual void undo() {
            mTree.insertTopLevelItem(mIndex, mTopItem);
            mTopItem = nullptr;
        }

        virtual void redo() { mTopItem = mTree.takeTopLevelItem(mIndex); }
    };

    //-------------------------------------------------------------------------------------------------
    class GUITreeItemAppender: public cmnd::Stable {
        QTreeWidgetItem& mParent;
        QTreeWidgetItem* mItem;
        bool mDone;

    public:
        GUITreeItemAppender(QTreeWidgetItem& aParent, QTreeWidgetItem* aItem):
            mParent(aParent), mItem(aItem), mDone(false) {}

        ~GUITreeItemAppender() {
            if (!mDone) {
                delete mItem;
            }
        }

        virtual void undo() {
            bool success = false;
            for (int i = 0; i < mParent.childCount(); ++i) {
                if (mItem == mParent.child(i)) {
                    mParent.takeChild(i);
                    success = true;
                    break;
                }
            }
            XC_ASSERT(success);
            mDone = false;
        }

        virtual void redo() {
            mParent.addChild(mItem);
            mDone = true;
        }
    };

    //-------------------------------------------------------------------------------------------------
    class GUITreeItemDeleter: public cmnd::Stable {
        QTreeWidgetItem& mParent;
        QTreeWidgetItem* mItem;
        int mIndex;
        bool mDone;

    public:
        GUITreeItemDeleter(QTreeWidgetItem& aParent, QTreeWidgetItem* aItem):
            mParent(aParent), mItem(aItem), mIndex(), mDone(false) {}

        ~GUITreeItemDeleter() {
            if (mDone) {
                delete mItem;
            }
        }

        virtual void exec() {
            for (mIndex = 0; mIndex < mParent.childCount(); ++mIndex) {
                if (mItem == mParent.child(mIndex))
                    break;
            }
            XC_ASSERT(mIndex < mParent.childCount());
            redo();
        }

        virtual void undo() {
            mParent.insertChild(mIndex, mItem);
            mDone = false;
        }

        virtual void redo() {
            mParent.takeChild(mIndex);
            mDone = true;
        }
    };

    //-------------------------------------------------------------------------------------------------
    void addTreeItemRecursive(const QTreeWidget& aTree, QTreeWidgetItem& aItem, img::ResourceNode& aNode) {
        for (auto childNode : aNode.children()) {
            auto childItem = new res::Item(aTree, *childNode, childNode->data().identifier());
            aItem.addChild(childItem);

            // recursive call
            addTreeItemRecursive(aTree, *childItem, *childNode);
        }
    }

    res::Item*
    ResourceUpdater::createGUITree(const QTreeWidget& aTree, img::ResourceNode& aNode, const QString& aIdentifier) {
        auto item = new res::Item(aTree, aNode, aIdentifier);
        addTreeItemRecursive(aTree, *item, aNode);
        return item;
    }

    //-------------------------------------------------------------------------------------------------
    ResourceUpdater::ResourceUpdater(ViaPoint& aViaPoint, core::Project& aProject):
        mViaPoint(aViaPoint), mProject(aProject), mPSDFormat() {}

    //-------------------------------------------------------------------------------------------------
    void ResourceUpdater::load(QTreeWidget& aTree, const QString& aFilePath) {
        if (aFilePath.isEmpty())
            return;

        auto newTree = createResourceTree(aFilePath, true);
        if (!newTree)
            return;

        auto& holder = mProject.resourceHolder();

        auto guiTree = createGUITree(aTree, *newTree, holder.relativeFilePath(aFilePath));
        XC_PTR_ASSERT(guiTree);

        {
            auto& stack = mProject.commandStack();

            cmnd::ScopedMacro macro(stack, CmndName::tr("Add new asset"));

            // notifier
            auto notifier = new AddNewOneNotifier(mViaPoint, mProject);
            notifier->event().setSingleTarget(*newTree);
            macro.grabListener(notifier);

            stack.push(new NewTreePusher(holder, newTree, aFilePath));
            stack.push(new GUINewTreePusher(aTree, guiTree));
        }
    }

    img::ResourceNode* ResourceUpdater::createResourceTree(const QString& aFilePath, bool aLoadImage) {
        const QFileInfo fileInfo(aFilePath);
        if (!fileInfo.isFile())
            return nullptr;

        if (fileInfo.suffix() == "psd") {
            return createPsdTree(aFilePath, aLoadImage);
        }
        if (fileInfo.suffix() == "ora") {
            return createOraTree(aFilePath, aLoadImage);
        } else {
            return createQImageTree(aFilePath, aLoadImage);
        }
    }

    img::ResourceNode* ResourceUpdater::createQImageTree(const QString& aFilePath, bool aLoadImage) const {
        const QFileInfo fileInfo(aFilePath);
        if (!fileInfo.isFile())
            return nullptr;

        QImage image(aFilePath);
        if (image.isNull()) {
            QMessageBox::warning(nullptr, tr("QImage Error"), tr("Failed to load image file."));
            return nullptr;
        }
        return img::Util::createResourceNode(image, fileInfo.baseName(), aLoadImage);
    }

    img::ResourceNode* ResourceUpdater::createPsdTree(const QString& aFilePath, bool aLoadImage) {
        // open file
        std::ifstream file(aFilePath.toLocal8Bit(), std::ios::binary);
        if (file.fail()) {
            QMessageBox::warning(nullptr, tr("FileIO Error"), tr("PSD file not found."));
            return nullptr;
        }

        // read psd
        img::PSDReader reader(file);
        if (reader.resultCode() != img::PSDReader::ResultCode_Success) {
            const QString errorText =
                "error(" + QString::number(reader.resultCode()) + ") " + QString::fromStdString(reader.resultMessage());
            QMessageBox::warning(nullptr, tr("PSD parse error"), errorText);
            return nullptr;
        }
        file.close();

        mPSDFormat = std::move(reader.format());

        // create resource tree
        return img::Util::createResourceNodes(*mPSDFormat, aLoadImage);
    }

    img::ResourceNode* ResourceUpdater::createOraTree(const QString& aFilePath, bool aLoadImage) {
        miniz_cpp::zip_file ora(aFilePath.toStdString());
        {
            const auto& path = aFilePath;
            XC_DEBUG_REPORT() << "ora path =" << path;
            try{
                if (!ora.has_file("mimetype")){
                    QMessageBox::warning(nullptr, tr("ORA parse error"), tr("Unable to find the MIME type of the selected file."));
                    return nullptr;
                }
                auto mimetype = ora.read("mimetype");
                if (mimetype != "image/openraster"){
                    QMessageBox::warning(nullptr, tr("ORA parse error"), tr("The MIME type is not valid."));
                    return nullptr;
                }
                XC_DEBUG_REPORT() << "ora file has valid mimetype";
            }
            catch (...){
                QMessageBox::warning(nullptr, tr("ORA parse error"), tr("The file could not be read."));
                    return nullptr;
            }
        }
        ora.reset();
        QMessageBox loadMerged;
        loadMerged.setWindowTitle(tr("Select ORA type"));
        loadMerged.setText(tr("How do you wish to load this ORA file?"));
        loadMerged.addButton(tr("Load layered"), QMessageBox::YesRole);
        QAbstractButton* mergeButton = loadMerged.addButton(tr("Load merged"), QMessageBox::YesRole);
        QAbstractButton* cancelButton = loadMerged.addButton(tr("Cancel file load"), QMessageBox::NoRole);
        loadMerged.exec();
        if(loadMerged.clickedButton() == cancelButton){ return nullptr; }
        bool merged = loadMerged.clickedButton() == mergeButton;
        // create resource tree
        return img::Util::createResourceNodes(merged, aFilePath.toStdString(), aLoadImage);
    }


    //-------------------------------------------------------------------------------------------------
    void ResourceUpdater::reload(Item& aItem) {
        auto& holder = mProject.resourceHolder();

        img::ResourceNode& node = aItem.node();
        img::ResourceNode& topNode = util::TreeUtil::getTreeRoot(node);
        QString filePath = holder.findAbsoluteFilePath(topNode);
        if (filePath.isEmpty())
            return;

        QScopedPointer<img::ResourceNode> newTree(createResourceTree(filePath, false));
        if (!newTree)
            return;

        RESOURCE_UPDATER_DUMP("begin reload");

        // reload images
        if (!tryReloadCorrespondingImages(aItem, newTree.data())) {
            return;
        }
        RESOURCE_UPDATER_DUMP("end reload");
    }

    void ResourceUpdater::forceReload(Item &aItem) {
        // reload images
        tryReloadCorrespondingImages(aItem, &aItem.node());
        RESOURCE_UPDATER_DUMP("end reload");
    }

    std::pair<int, img::ResourceNode*>
    findCorrespondingNode(const img::ResourceNode::Children& aSearchList, const img::ResourceNode& aNode) {
        {
            const int count = aNode.getCountOfSameSiblings();
            if (count > 0)
                return std::pair<int, img::ResourceNode*>(count + 1, nullptr);
        }

        // search
        QVector<img::ResourceNode*> sameNames;
        const bool targetIsLayer = aNode.data().isLayer();
        for (auto searchChild : aSearchList) {
            if (searchChild->data().identifier() == aNode.data().identifier()) {
                if (targetIsLayer == searchChild->data().isLayer()) {
                    sameNames.push_back(searchChild);
                }
            }
        }

        if (sameNames.count() == 1) {
            return std::pair<int, img::ResourceNode*>(1, sameNames.front());
        } else {
            return std::pair<int, img::ResourceNode*>(sameNames.count(), nullptr);
        }
    }

    std::pair<bool, QVector<img::ResourceNode*>>
    allChildrenCanBeIdentified(img::ResourceNode& aCurNode, img::ResourceNode& aNewNode) {
        std::pair<bool, QVector<img::ResourceNode*>> result;
        result.first = true;

        for (auto child : aNewNode.children()) {
            auto corresponds = findCorrespondingNode(aCurNode.children(), *child);
            if (corresponds.first > 1) {
                result.first = false;
                result.second.push_back(child);
            } else if (corresponds.first == 1) {
                auto childRes = allChildrenCanBeIdentified(*corresponds.second, *child);
                if (!childRes.first) {
                    result.first = false;
                    for (auto childError : childRes.second) {
                        result.second.push_back(childError);
                    }
                }
            }
        }
        return result;
    }

    img::ResourceNode* createNewAppendNode(ModificationNotifier& aNotifier, img::ResourceNode& aNode) {
        using img::PSDFormat;

        auto newNode = new img::ResourceNode(aNode.data().identifier());
        newNode->data().copyFrom(aNode.data());

        aNotifier.event().pushTarget(*newNode);

        if (newNode->data().isLayer()) {
            auto success = newNode->data().loadImage();
            XC_ASSERT(success);
            (void)success;
            XC_ASSERT(newNode->data().hasImage());
        }

        for (auto child : aNode.children()) {
            auto newChild = createNewAppendNode(aNotifier, *child);
            newNode->children().pushBack(newChild);
        }

        return newNode;
    }

    //-------------------------------------------------------------------------------------------------
    bool ResourceUpdater::createImageReloaderRecursive(
        cmnd::Stack& aStack,
        ModificationNotifier& aNotifier,
        QTreeWidgetItem& aCurItem,
        img::ResourceNode& aCurNode,
        img::ResourceNode& aNewNode
    ) {
        using img::PSDFormat;

        // This is to prevent a crash
        if (!(aCurItem.childCount() == (int)aCurNode.children().size())) {
            QMessageBox msgBox;
            QString aInfo =
                tr("Expected layer not found, if you wish to remove a layer or change its name, please do "
                   "so from within AnimeEffects.");
            msgBox.setIcon(QMessageBox::Critical);
            msgBox.setWindowTitle(tr("Import Error: Unable to find layer"));
            msgBox.setText(aInfo);
            msgBox.setStandardButtons(QMessageBox::Ok);
            msgBox.setDefaultButton(QMessageBox::Ok);
            msgBox.exec();
            return false;
        }

        RESOURCE_UPDATER_DUMP("reload image %s", aCurNode.data().identifier().toLatin1().data());

        // update abandon setting
        aCurNode.setAbandon(false);

        if (aCurNode.data().isLayer()) {
            XC_ASSERT(aNewNode.data().isLayer());

            // load new image
            auto success = aNewNode.data().loadImage();
            if (!success) {
                return false;
            }
            (void)success;

            // if layer data be modified
            if (!aCurNode.data().hasSameLayerDataWith(aNewNode.data())) {
                // push to targets
                aNotifier.event().pushTarget(aCurNode);

                // push reload image command
                XCMemBlock newImagePtr = aNewNode.data().image().block();
                const QRect newImageRect(aNewNode.data().pos(), aNewNode.data().image().pixelSize());
                aNewNode.data().releaseImage();
                aStack.push(new ImageSetter(aCurNode, newImagePtr, newImageRect));
            } else {
                aNewNode.data().freeImage();
            }
        }

        // each child
        for (auto child : aNewNode.children()) {
            auto corresponds = findCorrespondingNode(aCurNode.children(), *child);
            XC_ASSERT(corresponds.first <= 1); // check identifiability


            if (corresponds.first == 1) {
                auto childIndex = aCurNode.children().indexOf(corresponds.second);
                auto childItem = aCurItem.child(childIndex);
                XC_PTR_ASSERT(childItem);

                // reload node
                XC_PTR_ASSERT(corresponds.second);
                createImageReloaderRecursive(aStack, aNotifier, *childItem, *corresponds.second, *child);
            } else {
                // append new node
                auto newChild = createNewAppendNode(aNotifier, *child);
                aStack.push(new cmnd::PushBackNewTreeObject<img::ResourceNode>(&aCurNode.children(), newChild));

                // append new item
                auto newItem = new res::Item(*aCurItem.treeWidget(), *newChild, newChild->data().identifier());
                aStack.push(new GUITreeItemAppender(aCurItem, newItem));
            }
        }
        return true;
    }

    //-------------------------------------------------------------------------------------------------
    void ResourceUpdater::createAbandonedImageRemoverRecursive(
        cmnd::Stack& aStack, QTreeWidgetItem& aItem, img::ResourceNode& aNode
    ) {
        bool isKeeped = aNode.isKeeped();

        if (!isKeeped) {
            img::ResourceNode::ConstIterator itr(&aNode);
            while (itr.hasNext()) {
                if (itr.next()->isKeeped()) {
                    isKeeped = true;
                    break;
                }
            }
        }

        if (!isKeeped && aNode.isAbandoned()) {
            XC_PTR_ASSERT(aNode.parent()); // topnode will never be abandoned
            XC_PTR_ASSERT(aItem.parent());
            aStack.push(new cmnd::RemoveTreeByObj<img::ResourceNode>(&aNode.parent()->children(), &aNode));
            aStack.push(new cmnd::GrabDeleteObject<img::ResourceNode>(&aNode));
            aStack.push(new GUITreeItemDeleter(*aItem.parent(), &aItem));
            return;
        }

        XC_ASSERT(aItem.childCount() == (int)aNode.children().size());
        int childIndex = 0;
        for (auto child : aNode.children()) {
            auto childItem = aItem.child(childIndex);
            XC_PTR_ASSERT(childItem);

            createAbandonedImageRemoverRecursive(aStack, *childItem, *child);
            ++childIndex;
        }
    }

    //-------------------------------------------------------------------------------------------------
    bool ResourceUpdater::tryReloadCorrespondingImages(QTreeWidgetItem& aTarget, img::ResourceNode* aNewTree) {
        XC_PTR_ASSERT(aNewTree);

        auto item = res::Item::cast(&aTarget);
        XC_PTR_ASSERT(item);
        img::ResourceNode& targetNode = item->node();

        // read current tree position
        std::list<img::ResourceNode*> pos;
        for (auto ptr = &targetNode; ptr->parent(); ptr = ptr->parent()) {
            pos.push_front(ptr);
        }

        // find corresponding node
        img::ResourceNode* newNode = aNewTree;
        for (auto node : pos) {
            auto corresponds = findCorrespondingNode(newNode->children(), *node);
            if (corresponds.first != 1) {
                auto text = tr("Failed to find a corresponding node.") + " (" + node->data().identifier() + ")";
                QMessageBox::warning(nullptr, tr("Operation Error"), text);
                return false;
            }
            XC_PTR_ASSERT(corresponds.second);
            newNode = corresponds.second;
        }

        // check reloadable
        auto beIdentified = allChildrenCanBeIdentified(targetNode, *newNode);
        if (!beIdentified.first) {
            auto text = tr("Failed to identify nodes due to the following duplicates.") + "\n";
            for (auto& duplicated : beIdentified.second) {
                text += duplicated->treePath() + "\n";
            }
            QMessageBox::warning(nullptr, tr("Operation Error"), text);
            return false;
        }

        // reset abandon settings
        {
            img::ResourceNode::Iterator itr(&targetNode);
            while (itr.hasNext()) {
                auto node = itr.next();
                if (!node->parent())
                    continue; // topnode never be abandoned
                node->setAbandon(true);
            }
        }

        // reload
        {
            RESOURCE_UPDATER_DUMP(
                "create reload command %s, %s",
                targetNode.data().identifier().toLatin1().data(),
                newNode->data().identifier().toLatin1().data()
            );

            auto& stack = mProject.commandStack();
            cmnd::ScopedMacro macro(stack, CmndName::tr("Reload images"));

            // notifier
            auto notifier = new ModificationNotifier(mViaPoint, mProject, item->treePos());
            notifier->event().setRoot(targetNode);
            macro.grabListener(notifier);

            // create reload commands
            if (!createImageReloaderRecursive(stack, *notifier, aTarget, targetNode, *newNode)) {
                return false;
            }

            // create remove abandoned commands
            createAbandonedImageRemoverRecursive(stack, aTarget, targetNode);

            // create key updating commands
            stack.push(mProject.objectTree().createResourceUpdater(notifier->event()));
        }

        return true;
    }

    //-------------------------------------------------------------------------------------------------
    void ResourceUpdater::remove(QTreeWidget& aTree, Item& aTopItem) {
        auto& holder = mProject.resourceHolder();

        img::ResourceNode& node = aTopItem.node();
        img::ResourceNode& topNode = util::TreeUtil::getTreeRoot(node);
        XC_ASSERT(&node == &topNode);

        // check reference
        {
            img::ResourceNode::ConstIterator itr(&topNode);
            while (itr.hasNext()) {
                if (itr.next()->isKeeped()) {
                    QMessageBox::warning(
                        nullptr, tr("Operation Error"), tr("Some layers are still referenced by objects.")
                    );
                    return;
                }
            }
        }

        // find index
        int index = 0;
        for (auto& tree : holder.imageTrees()) {
            if (tree.topNode == &topNode)
                break;
            ++index;
        }
        if (index >= (int)holder.imageTrees().size())
            return;


        // find gui index
        int guiIndex = 0;
        for (int i = 0; i < aTree.topLevelItemCount(); ++i) {
            if (aTree.topLevelItem(i) == (QTreeWidgetItem*)(&aTopItem))
                break;
            ++guiIndex;
        }
        XC_ASSERT(guiIndex < aTree.topLevelItemCount());

        // invoke
        {
            auto& stack = mProject.commandStack();
            auto& holder = mProject.resourceHolder();

            cmnd::ScopedMacro macro(stack, CmndName::tr("Delete images"));

            // notifier
            auto notifier = new DeleteNotifier(mViaPoint, mProject);
            notifier->event().setSingleTarget(node);
            macro.grabListener(notifier);

            stack.push(new TreeDeleter(holder, index));
            stack.push(new GUITreeDeleter(aTree, guiIndex));
        }
    }

} // namespace gui::res

