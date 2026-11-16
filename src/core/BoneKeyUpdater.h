#ifndef CORE_BONEKEYUPDATER_H
#define CORE_BONEKEYUPDATER_H

#include "core/TimeLine.h"
#include "core/ObjectTreeEvent.h"
#include "core/ResourceEvent.h"
#include "core/TimeLineEvent.h"
#include "core/ProjectEvent.h"
namespace core {
class ObjectTree;
}

namespace core {

//-------------------------------------------------------------------------------------------------
class BoneUnbindWorkspace {
public:
    struct Unit {
        Unit();
        QVector<ObjectNode*> parents;
        ObjectNode* node;
    };
    BoneUnbindWorkspace();
    QList<Unit> units;

    void push(ObjectNode& aNode);
};
typedef std::shared_ptr<BoneUnbindWorkspace> BoneUnbindWorkspacePtr;

//-------------------------------------------------------------------------------------------------
class BoneKeyUpdater {
public:
    static void onTimeLineModified(TimeLineEvent& aEvent);

    static void onTreeRestructured(ObjectTreeEvent& aEvent);

    static void onResourceModified(ResourceEvent& aEvent);

    static void onProjectAttributeModified(ProjectEvent& aEvent);

    // One authored SkinWeights record on aOwner changed. Every BoneKey of that
    // owner reads the same field, so every key's influence caches are rebuilt;
    // a single-key update would leave the other frames showing the old paint.
    static void onSkinWeightsModified(Project& aProject, ObjectNode& aOwner, bool aIsUndo);

    // for tree restructuring
    static cmnd::Base* createNodeUnbinderForDelete(ObjectNode& aNode);
    // static cmnd::Base* createNodeUnbinderForMove(
    //         ObjectTree& aTree, const util::TreePos& aFrom, const util::TreePos& aTo);
    static cmnd::Base* createNodesUnbinderForMove(const BoneUnbindWorkspacePtr& aWorkspace);

private:
    static void onTimeLineModified(
        Project& aProject, ObjectNode& aNode, const QVector<ObjectNode*>& aUniqueRoots, bool aResetCacheList
    );

    static void resetInfluenceCachesOfChildren(Project& aProject, ObjectNode& aRoot);
    static void resetInfluenceCachesOfOneNode(Project& aProject, ObjectNode& aNode);
};

} // namespace core

#endif // CORE_BONEKEYUPDATER_H
