#ifndef GUI_RES_RESOURCEUPDATER_H
#define GUI_RES_RESOURCEUPDATER_H

#include "cmnd/Stack.h"
#include "img/PSDFormat.h"
#include "core/Project.h"
#include "gui/ViaPoint.h"
#include "gui/res/res_Item.h"
#include "gui/res/res_Notifier.h"

namespace gui::res {

    class ResourceUpdater: public QObject {
        Q_OBJECT
    public:
        static res::Item* createGUITree(img::ResourceNode& aNode, const QString& aIdentifier);

        ResourceUpdater(ViaPoint& aViaPoint, core::Project& aProject);
        void load(QTreeWidget& aTree, const QString& aFilePath);
        void reload(Item& aItem);
        void forceReload(Item& aItem);

        // Remove the asset subtree. When aReferencingObjects is empty the asset
        // must have no live reference; if it does, nothing is removed, the user
        // is warned and false is returned. When it is non-empty those object
        // nodes (the layers that render from the asset) are deleted in the SAME
        // undo step, which is how the asset can be removed with them.
        //
        // Returns whether the asset was removed.
        bool remove(
            QTreeWidget& aTree,
            Item& aTopItem,
            const QVector<core::ObjectNode*>& aReferencingObjects = QVector<core::ObjectNode*>()
        );

    private:
        img::ResourceNode* createResourceTree(const QString& aFilePath, bool aLoadImage);
        img::ResourceNode* createQImageTree(const QString& aFilePath, bool aLoadImage) const;
        img::ResourceNode* createPsdTree(const QString& aFilePath, bool aLoadImage);
        img::ResourceNode* createOraTree(const QString& aFilePath, bool aLoadImage);

        bool createImageReloaderRecursive(
            cmnd::Stack& aStack,
            ModificationNotifier& aNotifier,
            QTreeWidgetItem& aCurItem,
            img::ResourceNode& aCurNode,
            img::ResourceNode& aNewNode
        );

        void
        createAbandonedImageRemoverRecursive(cmnd::Stack& aStack, QTreeWidgetItem& aItem, img::ResourceNode& aNode);

        bool tryReloadCorrespondingImages(QTreeWidgetItem& aTarget, img::ResourceNode* aNewTree);

        ViaPoint& mViaPoint;
        core::Project& mProject;
        std::unique_ptr<img::PSDFormat> mPSDFormat;
    };

} // namespace gui::res


#endif // GUI_RES_RESOURCEUPDATER_H
