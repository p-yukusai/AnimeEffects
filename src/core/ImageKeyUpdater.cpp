#include "core/ImageKeyUpdater.h"
#include "core/GridWeightsRemap.h"
#include "core/ImageKey.h"
#include "core/Project.h"

namespace core {

//-------------------------------------------------------------------------------------------------
class ImageResourceUpdaterBase: public cmnd::Stable {
public:
    ImageResourceUpdaterBase(
        Project& aProject, ObjectNode& aNode, const ResourceUpdatingWorkspacePtr& aWorkspace,
        bool aCreateTransitions
    ):
        mProject(&aProject), mNode(&aNode), mTargets(), mWorkspace(aWorkspace),
        mCreateTransitions(aCreateTransitions) {}

    virtual ~ImageResourceUpdaterBase() {}

    virtual void exec() {
        for (auto& target : mTargets) {
            auto key = target.key;
            GridMesh::TransitionCreater transer(key->data().gridMesh(), key->data().resource()->pos());

            auto cellSize = target.nextCellSize > 0 ? target.nextCellSize : key->data().gridMesh().cellSize();
            auto imageOffset = key->data().imageOffset();

            // swap grid mesh
            target.anotherMesh = std::make_shared<GridMesh>();
            target.anotherMesh->swap(key->data().gridMesh());

            // update image
            key->setImage(target.nextImage);

            // reset grid mesh
            key->resetGridMesh(cellSize);

            // update image offset
            const QVector2D move(target.nextImage->pos() - target.prevImage->pos());
            target.prevOffset = imageOffset;
            target.nextOffset = target.prevOffset + move;
            key->data().setImageOffset(target.nextOffset);
            if (!target.channelCaptured) {
                captureChannel(target, move);
            }
            applyChannel(target, true);

            // authored weights follow the regenerated grid in this same undo
            // step; an ImageKey owns one grid, and a record is keyed to it
            target.weightRemaps = remapGridWeights(
                *mNode, *key, *target.anotherMesh, target.prevImage->pos(),
                key->data().gridMesh(), target.nextImage->pos()
            );
            applyWeightRemaps(target, false);

            // create transition data
            if (mCreateTransitions) {
                auto& trans = mWorkspace->makeSureTransitions(key, key->data().gridMesh());
                trans = transer.create(
                    key->data().gridMesh().positions(),
                    key->data().gridMesh().vertexCount(),
                    key->data().resource()->pos()
                );
            }
        }
        mWorkspace.reset(); // finish using
        notifyWeightRemaps(false);
    }

    virtual void redo() {
        for (auto& target : mTargets) {
            target.key->setImage(target.nextImage);
            target.key->data().gridMesh().swap(*target.anotherMesh);
            target.key->data().setImageOffset(target.nextOffset);
            applyChannel(target, true);
            applyWeightRemaps(target, false);
        }
        notifyWeightRemaps(false);
    }

    virtual void undo() {
        for (auto& target : mTargets) {
            applyWeightRemaps(target, true);
            target.key->setImage(target.prevImage);
            target.key->data().gridMesh().swap(*target.anotherMesh);
            target.key->data().setImageOffset(target.prevOffset);
            applyChannel(target, false);
        }
        notifyWeightRemaps(true);
    }

protected:
    struct Target {
        Target(ImageKey* aKey):
            key(aKey), prevImage(), nextImage(), prevOffset(), nextOffset(), anotherMesh(),
            nextCellSize(0), weightRemaps() {}
        ImageKey* key;
        img::ResourceHandle prevImage;
        img::ResourceHandle nextImage;
        QVector2D prevOffset;
        QVector2D nextOffset;
        // The offset CHANNEL's value at this key's frame, when one exists.
        QVector2D prevChannelOffset;
        QVector2D nextChannelOffset;
        bool channelCaptured = false;
        bool channelSync = false;
        std::shared_ptr<GridMesh> anotherMesh;
        int nextCellSize;
        QVector<GridWeightsRemap> weightRemaps;
    };

    QList<Target>& targets() { return mTargets; }
    const QList<Target>& targets() const { return mTargets; }

private:
    // The offset channel has one value per property frame; a resource swap moves
    // the ImageKey's offset by the image position delta, so the authored channel
    // value at the same frame must move with it. A channel with no key at that
    // frame has no authored offset there (the previous/next channel key wins).
    QVector2D channelOffsetAt(Target& aTarget) const {
        if (!mNode || !mNode->timeLine() || !mNode->timeLine()->property(TimeKeyType_Image)) {
            return QVector2D();
        }
        core::TimeLine& line = *mNode->timeLine();
        const int frame = aTarget.key->frame();
        if (frame == TimeLine::kBaseValueIndex) {
            return QVector2D(
                line.channelBaseValue(TimeKeyType_Image, 0),
                line.channelBaseValue(TimeKeyType_Image, 1)
            );
        }
        const ChannelKey* x = line.channelKey(TimeKeyType_Image, 0, frame);
        const ChannelKey* y = line.channelKey(TimeKeyType_Image, 1, frame);
        return QVector2D(x ? x->value() : 0.0f, y ? y->value() : 0.0f);
    }

    bool channelExistsAt(Target& aTarget) const {
        if (!mNode || !mNode->timeLine() || !mNode->timeLine()->property(TimeKeyType_Image)) {
            return false;
        }
        if (aTarget.key->frame() == TimeLine::kBaseValueIndex) {
            return true;
        }
        return mNode->timeLine()->hasChannelKey(TimeKeyType_Image, 0, aTarget.key->frame())
            || mNode->timeLine()->hasChannelKey(TimeKeyType_Image, 1, aTarget.key->frame());
    }

    void captureChannel(Target& aTarget, const QVector2D& aMove) {
        aTarget.prevChannelOffset = channelOffsetAt(aTarget);
        aTarget.nextChannelOffset = aTarget.prevChannelOffset + aMove;
        aTarget.channelSync = channelExistsAt(aTarget);
        aTarget.channelCaptured = true;
    }

    void applyChannel(Target& aTarget, bool aNext) {
        if (!aTarget.channelSync || !mNode || !mNode->timeLine()) {
            return;
        }
        core::TimeLine& line = *mNode->timeLine();
        const QVector2D value = aNext ? aTarget.nextChannelOffset : aTarget.prevChannelOffset;
        if (aTarget.key->frame() == TimeLine::kBaseValueIndex) {
            line.setChannelBaseValue(TimeKeyType_Image, 0, value.x());
            line.setChannelBaseValue(TimeKeyType_Image, 1, value.y());
        } else {
            if (ChannelKey* x = line.channelKey(TimeKeyType_Image, 0, aTarget.key->frame())) {
                x->setValue(value.x());
            }
            if (ChannelKey* y = line.channelKey(TimeKeyType_Image, 1, aTarget.key->frame())) {
                y->setValue(value.y());
            }
        }
    }

    void applyWeightRemaps(Target& aTarget, bool aUndo) {
        for (GridWeightsRemap& remap : aTarget.weightRemaps) {
            if (remap.owner) {
                remap.owner->skinWeights().setNodeWeights(aUndo ? remap.before : remap.after);
            }
        }
    }

    void notifyWeightRemaps(bool aUndo) {
        if (!mProject) {
            return;
        }
        QVector<ObjectNode*> owners;
        for (const Target& target : mTargets) {
            for (const GridWeightsRemap& remap : target.weightRemaps) {
                ObjectNode* owner = remap.owner.get();
                if (owner && !owners.contains(owner)) {
                    owners.push_back(owner);
                    mProject->onSkinWeightsModified(*owner, aUndo);
                }
            }
        }
    }

    Project* mProject;
    ObjectNode* mNode;
    QList<Target> mTargets;
    ResourceUpdatingWorkspacePtr mWorkspace;
    bool mCreateTransitions;
};

//-------------------------------------------------------------------------------------------------
class ImageReloader: public ImageResourceUpdaterBase {
    TimeLine& mTimeLine;
    const ResourceEvent& mEvent;

    void tryPushTarget(ImageKey* aKey) {
        // A key whose image reference never resolved has no resource handle at
        // all, and its address is only reachable through one. Skipping it keeps a
        // partially-deserialized tree from being dereferenced here; the id solver
        // refuses that load outright for the same reason (util::IDSolver::solve).
        if (aKey && aKey->data().resource()) {
            auto node = mEvent.findTarget(aKey->data().resource()->serialAddress());
            if (node) {
                this->targets().push_back(Target(aKey));
                this->targets().back().prevImage = aKey->data().resource();
                this->targets().back().nextImage = node->handle();
            }
        }
    }

public:
    ImageReloader(
        ObjectNode& aNode, Project& aProject, const ResourceEvent& aEvent,
        const ResourceUpdatingWorkspacePtr& aWorkspace, bool aCreateTransitions
    ):
        ImageResourceUpdaterBase(aProject, aNode, aWorkspace, aCreateTransitions),
        mTimeLine(*aNode.timeLine()), mEvent(aEvent) {}

    virtual void exec() {
        // push base value
        tryPushTarget((ImageKey*)mTimeLine.baseValue(TimeKeyType_Image));

        auto& map = mTimeLine.map(TimeKeyType_Image);
        for (auto itr = map.begin(); itr != map.end(); ++itr) {
            TimeKey* key = itr.value();
            TIMEKEY_PTR_TYPE_ASSERT(key, Image);

            // push key
            tryPushTarget((ImageKey*)key);
        }

        ImageResourceUpdaterBase::exec();
    }
};

//-------------------------------------------------------------------------------------------------
class ImageChanger: public ImageResourceUpdaterBase {
public:
    ImageChanger(
        ImageKey& aKey, ObjectNode& aNode, Project& aProject, img::ResourceNode& aNewResource,
        const ResourceUpdatingWorkspacePtr& aWorkspace, bool aCreateTransitions
    ):
        ImageResourceUpdaterBase(aProject, aNode, aWorkspace, aCreateTransitions) {
        this->targets().push_back(Target(&aKey));
        this->targets().back().prevImage = aKey.data().resource();
        this->targets().back().nextImage = aNewResource.handle();
    }
};

//-------------------------------------------------------------------------------------------------
class GridMeshUpdater: public ImageResourceUpdaterBase {
public:
    GridMeshUpdater(
        ImageKey& aKey, ObjectNode& aNode, Project& aProject, int aNewCellSize,
        const ResourceUpdatingWorkspacePtr& aWorkspace, bool aCreateTransitions
    ):
        ImageResourceUpdaterBase(aProject, aNode, aWorkspace, aCreateTransitions) {
        this->targets().push_back(Target(&aKey));
        this->targets().back().prevImage = aKey.data().resource();
        this->targets().back().nextImage = aKey.data().resource();
        this->targets().back().nextCellSize = aNewCellSize;
    }
};

//-------------------------------------------------------------------------------------------------
cmnd::Stable* ImageKeyUpdater::createResourceUpdater(
    ObjectNode& aNode,
    const ResourceEvent& aEvent,
    const ResourceUpdatingWorkspacePtr& aWorkspace,
    bool aCreateTransitions
) {
    if (!aNode.timeLine())
        return nullptr;
    return new ImageReloader(aNode, aEvent.project(), aEvent, aWorkspace, aCreateTransitions);
}

cmnd::Stable* ImageKeyUpdater::createResourceUpdater(
    ImageKey& aKey, ObjectNode& aNode, Project& aProject, img::ResourceNode& aNewResource,
    const ResourceUpdatingWorkspacePtr& aWorkspace, bool aCreateTransitions
) {
    return new ImageChanger(aKey, aNode, aProject, aNewResource, aWorkspace, aCreateTransitions);
}

cmnd::Stable* ImageKeyUpdater::createGridMeshUpdater(
    ImageKey& aKey, ObjectNode& aNode, Project& aProject, int aNewCellSize,
    const ResourceUpdatingWorkspacePtr& aWorkspace, bool aCreateTransitions
) {
    return new GridMeshUpdater(aKey, aNode, aProject, aNewCellSize, aWorkspace, aCreateTransitions);
}

//-------------------------------------------------------------------------------------------------
class ImageSleeper: public cmnd::Stable {
    ObjectNode& mNode;
    QList<ImageKey*> mTargets;

public:
    ImageSleeper(ObjectNode& aNode): mNode(aNode), mTargets() {}

    virtual void exec() {
        ObjectNode::Iterator itr(&mNode);
        while (itr.hasNext()) {
            auto node = itr.next();
            if (!node || !node->timeLine())
                continue;
            TimeLine& timeLine = *(node->timeLine());

            // push base value
            auto dfltKey = (ImageKey*)timeLine.baseValue(TimeKeyType_Image);
            if (dfltKey)
                mTargets.push_back(dfltKey);

            auto& map = timeLine.map(TimeKeyType_Image);
            for (auto itr = map.begin(); itr != map.end(); ++itr) {
                TimeKey* key = itr.value();
                TIMEKEY_PTR_TYPE_ASSERT(key, Image);
                // push key
                mTargets.push_back((ImageKey*)key);
            }
        }

        redo();
    }

    virtual void redo() {
        for (auto& target : mTargets) {
            target->sleep();
        }
    }

    virtual void undo() {
        for (auto& target : mTargets) {
            target->awake();
        }
    }
};

//-------------------------------------------------------------------------------------------------
cmnd::Base* ImageKeyUpdater::createResourceSleeperForDelete(ObjectNode& aNode) { return new ImageSleeper(aNode); }

} // namespace core
