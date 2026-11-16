#ifndef CORE_IMAGEKEYUPDATER_H
#define CORE_IMAGEKEYUPDATER_H

#include "cmnd/Stable.h"
#include "core/ObjectNode.h"
#include "core/ResourceEvent.h"
#include "core/ResourceUpdatingWorkspace.h"
namespace core {
class ImageStateKey;
using ImageKey = ImageStateKey;
class Project;
}

namespace core {

class ImageKeyUpdater {
public:
    static cmnd::Stable* createResourceUpdater(
        ObjectNode& aNode,
        const ResourceEvent& aEvent,
        const ResourceUpdatingWorkspacePtr& aWorkspace,
        bool aCreateTransitions
    );

    // The node is the image key's owner: an upload, a replacement or a cell
    // size edit regenerates the grid the node's authored weights are stored
    // against, so the command carries the records across the same undo step.
    static cmnd::Stable* createResourceUpdater(
        ImageKey& aKey, ObjectNode& aNode, Project& aProject, img::ResourceNode& aNewResource,
        const ResourceUpdatingWorkspacePtr& aWorkspace, bool aCreateTransitions
    );

    static cmnd::Stable* createGridMeshUpdater(
        ImageKey& aKey, ObjectNode& aNode, Project& aProject, int aNewCellSize,
        const ResourceUpdatingWorkspacePtr& aWorkspace, bool aCreateTransitions
    );

    static cmnd::Base* createResourceSleeperForDelete(ObjectNode& aNode);
};

} // namespace core

#endif // CORE_IMAGEKEYUPDATER_H
