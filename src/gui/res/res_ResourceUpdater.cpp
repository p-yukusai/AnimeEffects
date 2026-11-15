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
#include "ctrl/TreeActions.h"
#include "gui/ResourceTreeWidget.h"
#include "img/Util.h"
#include <zip_file.h>

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
    void addTreeItemRecursive(QTreeWidgetItem& aItem, img::ResourceNode& aNode) {
        for (auto childNode : aNode.children()) {
            auto childItem = new res::Item(*childNode, childNode->data().identifier());
            aItem.addChild(childItem);

            // recursive call
            addTreeItemRecursive(*childItem, *childNode);
        }
    }

    res::Item*
    ResourceUpdater::createGUITree(img::ResourceNode& aNode, const QString& aIdentifier) {
        auto item = new res::Item(aNode, aIdentifier);
        addTreeItemRecursive(*item, aNode);
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

        auto guiTree = createGUITree(*newTree, holder.relativeFilePath(aFilePath));
        XC_PTR_ASSERT(guiTree);

        {
            auto& stack = mProject.commandStack();

            cmnd::ScopedMacro macro(stack, CmndName::tr("Add new asset"));

            // notifier
            auto notifier = new AddNewOneNotifier(mProject);
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

        // The relink walker must name a layer's kind the same way the import does
        // (img::psdUnsupportedLayerName). Such a layer is not unreadable -- a fill
        // or adjustment layer simply has no RGB planes, and a text or smart object
        // only has its flattened raster -- so the generic "could not be read" note
        // below would be the wrong explanation for the empty node it produces.
        for (const auto& layer : mPSDFormat->layerAndMaskInfo().layers) {
            if (layer->entryType != img::PSDFormat::LayerEntryType_Layer) {
                continue;
            }
            const auto kind = img::psdUnsupportedLayerKind(*layer);
            if (kind == img::PsdUnsupportedLayer_None) {
                continue;
            }
            const QString name =
                QString::fromStdString(layer->unicodeName.empty() ? layer->name : layer->unicodeName);
            mViaPoint.pushLog(
                tr("Layer \"%1\" is not supported (%2); AnimeEffects cannot reproduce it.")
                    .arg(name, QString::fromLatin1(img::psdUnsupportedLayerName(kind))),
                ctrl::UILogType_Warn
            );
        }

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
        // A forced relink re-reads the asset from disk.
        //
        // It used to hand the live tree to the matcher as its own "new" tree,
        // so every layer was compared with itself. The comparison finds
        // identical ResourceData on both sides -- by construction, it is the
        // same data -- and the "nothing changed" branch then FREED the live
        // pixel buffer while the ImageKeys still held a handle to it: a blank
        // layer that also never rebuilt its texture cache. The other half of
        // that path (a node imported with its pixels already, which has no lazy
        // loader) failed the loader call and bailed out with every node of the
        // asset already flagged abandoned, so the batch did nothing but poison
        // the next one. Re-parsing the file is what "force" has to mean, and it
        // is the only form in which the live tree is compared with data it does
        // not own.
        reload(aItem);
        RESOURCE_UPDATER_DUMP("end reload");
    }

    // The number of siblings of aNode that share its identity. A layer is
    // matched by its identity (ResourceData::identityId: Photoshop's stable
    // per-layer id, falling back to the display name for a source that carries
    // none -- see img::Util::layerIdentity), so the duplicate guard counts
    // identities rather than display names. Two layers the source renamed onto
    // one name must be refused as a pair, never matched to each other.
    int countSameIdentitySiblings(const img::ResourceNode& aNode) {
        const auto parent = aNode.parent();
        if (!parent)
            return 0;

        int count = 0;
        for (auto child : parent->children()) {
            if (child != &aNode &&
                child->data().identityId() == aNode.data().identityId() &&
                child->data().isLayer() == aNode.data().isLayer()) {
                ++count;
            }
        }
        return count;
    }

    std::pair<int, img::ResourceNode*>
    findCorrespondingNode(const img::ResourceNode::Children& aSearchList, const img::ResourceNode& aNode) {
        {
            const int count = countSameIdentitySiblings(aNode);
            if (count > 0)
                return std::pair<int, img::ResourceNode*>(count + 1, nullptr);
        }

        // search
        QVector<img::ResourceNode*> sameIdentities;
        const bool targetIsLayer = aNode.data().isLayer();
        for (auto searchChild : aSearchList) {
            if (searchChild->data().identityId() == aNode.data().identityId()) {
                if (targetIsLayer == searchChild->data().isLayer()) {
                    sameIdentities.push_back(searchChild);
                }
            }
        }

        if (sameIdentities.count() == 1) {
            return std::pair<int, img::ResourceNode*>(1, sameIdentities.front());
        } else {
            return std::pair<int, img::ResourceNode*>(sameIdentities.count(), nullptr);
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

    //-------------------------------------------------------------------------------------------------
    // Analysis pass of a relink: everything that can refuse the batch, decided
    // before anything is applied.
    //
    // A relink builds one cmnd::ScopedMacro, and that macro's destructor COMMITS
    // it (Stack::endMacro pushes the macro and executes it). Bailing out partway
    // through the batch therefore used to apply half a document -- every command
    // already pushed -- while the layer it stopped on stayed half-reloaded and
    // flagged abandoned, which in turn broke the next relink of the same asset.
    // So the batch is validated first: a GUI tree that no longer describes its
    // resource tree, a layer renamed onto a name the project already uses, and a
    // source layer that will not give up its pixels. Only once all three pass is
    // the macro opened, and the pass below cannot fail.
    //
    // Nothing the relink applies is applied here. The live tree is only read;
    // the freshly parsed tree has its pixels loaded, because a loader that runs
    // once is not allowed to run again once the batch is applying.

    // Does the GUI tree still describe this resource tree? Indexing one with the
    // other mislabels every layer below the first mismatch, so a tree that has
    // drifted apart is a refusal, not a relabel.
    bool hasCorrespondingTree(QTreeWidgetItem& aItem, img::ResourceNode& aNode) {
        if (aItem.childCount() != (int)aNode.children().size()) {
            return false;
        }
        int index = 0;
        for (auto child : aNode.children()) {
            auto childItem = aItem.child(index);
            if (!childItem || !hasCorrespondingTree(*childItem, *child)) {
                return false;
            }
            ++index;
        }
        return true;
    }

    // Reads the pixels of every layer of the freshly parsed tree. A loader that
    // refuses to run (an already-spent one-shot loader, a layer that will not
    // decode) is a batch failure found here rather than halfway through a macro
    // that is already applying.
    bool loadSourceImagesRecursive(img::ResourceNode& aNode) {
        if (aNode.data().isLayer() && !aNode.data().hasImage()) {
            if (!aNode.data().loadImage()) {
                return false;
            }
        }
        for (auto child : aNode.children()) {
            if (!loadSourceImagesRecursive(*child)) {
                return false;
            }
        }
        return true;
    }

    // Collects the layers whose display name changed in the source file.
    //
    // Layers are matched by identity, so a matched pair whose names differ is a
    // rename. Applying it is dangerous exactly when the new name already belongs
    // to a sibling: the layer's bitmap, rect and blend mode would be written onto
    // that sibling's node, and every project object hanging off it would then
    // display the other layer's pixels. The pair is reported instead of applied,
    // and the name is the user's to change from inside AnimeEffects.
    //
    // A source that carries no stable id (ORA, an older PSD, a plain image file)
    // has the name for its identity, so a renamed layer simply does not match and
    // is treated as new content: the detection cannot misfire there.
    void findRenamesRecursive(
        img::ResourceNode& aCurNode, img::ResourceNode& aNewNode, QVector<img::ResourceNode*>& aRenames)
    {
        if (aCurNode.data().isLayer() &&
            aCurNode.data().identifier() != aNewNode.data().identifier()) {
            aRenames.push_back(&aCurNode);
        }

        for (auto child : aNewNode.children()) {
            auto corresponds = findCorrespondingNode(aCurNode.children(), *child);
            if (corresponds.first == 1) {
                findRenamesRecursive(*corresponds.second, *child, aRenames);
            }
        }
    }

    // A layer, and its whole subtree, that the source file no longer contains.
    //
    // A node the project still renders from -- its handle has an origin keeper,
    // here or in a descendant -- is never removed by a relink. Deleting it would
    // destroy work the file cannot restore and leave the document blank, so it is
    // kept instead and its disappearance is made visible: a note naming the layer
    // goes to the project log, and the entry in the resource tree is marked so a
    // stale bitmap is never mistaken for a live one. Removing a project layer
    // stays a manual action.
    //
    // A node nothing references is an orphan: the source dropped it and no object
    // needs it, so it is flagged for the batch's remover pass. A node appended by
    // an earlier relink is in exactly that position, so content the source later
    // drops is cleaned up with it rather than accumulating.
    void resolveAbsentLayerRecursive(
        ViaPoint& aViaPoint, cmnd::Stack& aStack, QTreeWidgetItem& aItem, img::ResourceNode& aNode)
    {
        // referenced by a project object, directly or through a descendant?
        bool keeped = aNode.isKeeped();
        if (!keeped) {
            img::ResourceNode::ConstIterator itr(&aNode);
            while (itr.hasNext()) {
                if (itr.next()->isKeeped()) {
                    keeped = true;
                    break;
                }
            }
        }

        // The flag means "this batch removes this node", and is set nowhere
        // else, so a node that survives this relink is not left carrying a
        // removal flag a later one would act on.
        aNode.setAbandon(!keeped);

        if (!keeped) {
            return; // the batch's remover pass deletes this one
        }

        aViaPoint.pushLog(
            ResourceUpdater::tr(
                "Relink: layer \"%1\" is not in the source file. It stays, because the project still "
                "renders from it; remove the layer in AnimeEffects to delete it.")
                .arg(aNode.data().identifier()),
            ctrl::UILogType_Warn);

        const QString marker = ResourceUpdater::tr(
            "This layer is not in the source file. The project keeps the last version it read.");
        const QString previousToolTip =
            aItem.toolTip(gui::ResourceTreeWidget::kItemColumn);
        aStack.push(new cmnd::Delegatable(
            [item = &aItem, marker]() { item->setToolTip(gui::ResourceTreeWidget::kItemColumn, marker); },
            [item = &aItem, previousToolTip]() {
                item->setToolTip(gui::ResourceTreeWidget::kItemColumn, previousToolTip);
            }));

        int childIndex = 0;
        for (auto child : aNode.children()) {
            auto childItem = aItem.child(childIndex);
            XC_PTR_ASSERT(childItem);
            resolveAbsentLayerRecursive(aViaPoint, aStack, *childItem, *child);
            ++childIndex;
        }
    }

    img::ResourceNode* createNewAppendNode(ModificationNotifier& aNotifier, img::ResourceNode& aNode) {
        using img::PSDFormat;

        // The identity travels with the node: a source that carries a stable
        // layer id keeps it, so a later relink still matches this layer to
        // itself instead of appending a second copy of it.
        auto newNode = new img::ResourceNode(aNode.data().identifier(), aNode.data().identityId());
        // copyFrom deep-copies the pixel buffer, and the analysis pass has
        // already read every layer of the freshly parsed tree (its loaders are
        // one-shot and must not be run a second time), so the appended node
        // starts out holding what the file has.
        newNode->data().copyFrom(aNode.data());

        aNotifier.event().pushTarget(*newNode);

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
        // The analysis pass checked this for the whole subtree before the macro
        // was opened. Refuse again rather than index one tree with the other and
        // mislabel every layer below this one.
        if (aCurItem.childCount() != (int)aCurNode.children().size()) {
            return false;
        }

        RESOURCE_UPDATER_DUMP("reload image %s", aCurNode.data().identifier().toLatin1().data());

        // update abandon setting. The flag means only "this batch removes this
        // node"; it is cleared for every node that is matched, so a node that
        // survives this relink is not deleted by a later one.
        aCurNode.setAbandon(false);

        if (aCurNode.data().isLayer()) {
            XC_ASSERT(aNewNode.data().isLayer());

            // The layer's pixels were read by the analysis pass; its loader is
            // one-shot and must not be run again here.

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

        // each child of the source: reload the layer the project has, or append
        // the ones it does not.
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
                // A layer the source has and the project does not: it is new
                // content and is appended. (The analysis pass has already refused
                // a source layer that matched a project layer under a new name,
                // so an append is never a rename in disguise.) An appended node
                // is referenced by nothing yet; when the source later drops that
                // layer, a later relink removes it like any other unreferenced
                // node.
                auto newChild = createNewAppendNode(aNotifier, *child);
                aStack.push(new cmnd::PushBackNewTreeObject<img::ResourceNode>(&aCurNode.children(), newChild));

                // append new item
                auto newItem = new res::Item(*newChild, newChild->data().identifier());
                aStack.push(new GUITreeItemAppender(aCurItem, newItem));
            }
        }

        // each child of the project the source no longer has
        int absentIndex = 0;
        for (auto child : aCurNode.children()) {
            auto corresponds = findCorrespondingNode(aNewNode.children(), *child);
            if (corresponds.first != 1) {
                auto childItem = aCurItem.child(absentIndex);
                XC_PTR_ASSERT(childItem);
                resolveAbsentLayerRecursive(mViaPoint, aStack, *childItem, *child);
            }
            ++absentIndex;
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

        // validate the whole batch before anything is applied: the GUI tree
        // must still describe this resource tree, no layer may have been renamed
        // onto a name the project already uses, and every source layer must be
        // able to give up its pixels. All three are decided here, while the
        // macro that will apply the batch is still unopened -- a refusal that
        // arrives after the first commands have been pushed applies half a
        // document and leaves the layer it stopped on flagged for removal.

        // the GUI tree and the resource tree must still describe the same
        // document
        if (!hasCorrespondingTree(aTarget, targetNode)) {
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

        // a layer must not have been renamed onto a name the project uses
        QVector<img::ResourceNode*> renames;
        findRenamesRecursive(targetNode, *newNode, renames);
        if (!renames.isEmpty()) {
            QString text =
                tr("Relink: the source file renamed a layer onto a name the project already uses, which "
                   "would put that layer's pixels on another layer:") + "\n";
            for (auto renamed : renames) {
                text += "  " + renamed->treePath() + "\n";
            }
            text += tr("Nothing was changed. Rename the layer inside AnimeEffects, then reload again.");

            // A note, not a modal box: a relink can drop several layers at once
            // and must not interrupt itself.
            mViaPoint.pushLog(text, ctrl::UILogType_Warn);
            return false;
        }

        // every source layer must be readable
        if (!loadSourceImagesRecursive(*newNode)) {
            mViaPoint.pushLog(
                tr("Relink: a layer of the source file could not be read, so nothing was changed."),
                ctrl::UILogType_Warn);
            return false;
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
    bool ResourceUpdater::remove(
        QTreeWidget& aTree, Item& aTopItem, const QVector<core::ObjectNode*>& aReferencingObjects
    ) {
        auto& holder = mProject.resourceHolder();

        img::ResourceNode& node = aTopItem.node();
        img::ResourceNode& topNode = util::TreeUtil::getTreeRoot(node);
        XC_ASSERT(&node == &topNode);

        // check reference
        {
            img::ResourceNode::ConstIterator itr(&topNode);
            while (itr.hasNext()) {
                if (itr.next()->isKeeped()) {
                    // A live reference the caller did not offer to delete: refuse
                    // rather than pull an asset out from under the layers that
                    // render from it.
                    if (aReferencingObjects.isEmpty()) {
                        QMessageBox::warning(
                            nullptr, tr("Operation Error"), tr("Some layers are still referenced by objects.")
                        );
                        return false;
                    }
                    break;
                }
            }
        }

        // The deletion of the referencing layers rides in this macro as a DOCUMENT edit
        // (ctrl::tree::appendDeleteNodes). It used to need the object tree's VIEW - its
        // widget items had to move in the same step or they were orphaned - which is the
        // coupling the QML tree removed: a row is derived from the document, so a node that
        // is gone is a row that is gone. A node with no parent (the document itself) is
        // still refused rather than corrupted.
        if (!aReferencingObjects.isEmpty() && !ctrl::tree::canDelete(aReferencingObjects)) {
            QMessageBox::warning(
                nullptr, tr("Operation Error"), tr("Some layers are still referenced by objects.")
            );
            return false;
        }

        // find index
        int index = 0;
        for (auto& tree : holder.imageTrees()) {
            if (tree.topNode == &topNode)
                break;
            ++index;
        }
        if (index >= (int)holder.imageTrees().size())
            return false;


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
            auto notifier = new DeleteNotifier(mProject);
            notifier->event().setSingleTarget(node);
            macro.grabListener(notifier);

            // Delete the layers that render from this asset in the same undo step.
            // Appended BEFORE the asset's own removal so that, in the macro's
            // forward order, the references are gone by the time the resource
            // tree is taken out of the holder.
            if (!aReferencingObjects.isEmpty()) {
                ctrl::tree::appendDeleteNodes(mProject, aReferencingObjects, macro);
            }

            stack.push(new TreeDeleter(holder, index));
            stack.push(new GUITreeDeleter(aTree, guiIndex));
        }

        return true;
    }

} // namespace gui::res

