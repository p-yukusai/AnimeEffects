#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <QSet>
#include "util/LinkPointer.h"
#include "util/TreeUtil.h"
#include "cmnd/Stable.h"
#include "cmnd/Vector.h"
#include "cmnd/BasicCommands.h"
#include "core/ObjectTree.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "core/ImageKey.h"
#include "core/ObjectNodeUtil.h"
#include "core/ObjectTreeEvent.h"
#include "core/TimeCacheAccessor.h"
#include "core/BoneKeyUpdater.h"
#include "core/ImageKeyUpdater.h"
#include "core/FilterMask.h"
#include "core/TimeLine.h"
#include "gl/Global.h"

namespace core {

//-------------------------------------------------------------------------------------------------
class SortAndRenderCall {
    static bool compareDepth(Renderer::SortUnit a, Renderer::SortUnit b) { return a.depth < b.depth; }

    std::vector<Renderer::SortUnit> mArray;
    const TimeCacheAccessor* mAccessor;

public:
    SortAndRenderCall(): mArray(), mAccessor() {}

    void invoke(ObjectNode* aTopNode, const RenderInfo& aInfo, const TimeCacheAccessor& aAccessor) {
        mAccessor = &aAccessor;

        // prerender
        {
            ObjectNode::Iterator itr(aTopNode);
            while (itr.hasNext()) {
                auto renderer = itr.next()->renderer();
                if (renderer) {
                    renderer->prerender(aInfo, aAccessor);
                }
            }
        }

        // sort (folders with an active composite filter are pushed as single units)
        mArray.clear();
        ObjectNodeUtil::collectRenderUnits(*aTopNode, true, mArray, aAccessor, aInfo.time);
        std::stable_sort(mArray.begin(), mArray.end(), compareDepth);

        // render
        for (auto data : mArray) {
            data.renderer->render(aInfo, aAccessor);
        }
    }
};

} // namespace core

namespace core {

//-------------------------------------------------------------------------------------------------
ObjectTree::ObjectTree():
    mLifeLink(), mTopNode(), mCaller(new SortAndRenderCall()), mShaderHolder(), mTimeCacheLock() {}

ObjectTree::~ObjectTree() {}

void ObjectTree::render(const RenderInfo& aInfo, bool aUseWorkingCache) {
    if (mTopNode.data()) {
        TimeCacheAccessor accessor(*mTopNode.data(), mTimeCacheLock, aInfo.time, aUseWorkingCache);
        mCaller->invoke(mTopNode.data(), aInfo, accessor);
    }
}

qint64 ObjectTree::maskRamBytes() const {
    qint64 total = 0;
    if (!mTopNode.data())
        return total;
    std::function<void(ObjectNode&)> walk = [&](ObjectNode& aNode) {
        // the CURRENT expansion is what this frame samples: its matte is the blended field (which
        // shares the paged pyramid's holder, so the pyramid is counted once per node)
        if (const TimeLine* line = aNode.timeLine()) {
            total += line->current().mask().ramBytes();
            for (int type = 0; type < TimeKeyType_TERM; ++type)
                for (int channel = 0; channel < FilterMaskChannel_TERM; ++channel)
                    if (const FilterMask* mask =
                            line->filterMask((TimeKeyType)type, (FilterMaskChannel)channel))
                        total += mask->ramBytes();
        }
        for (auto child : aNode.children())
            walk(*child);
    };
    walk(*mTopNode.data());
    return total;
}

cmnd::Vector ObjectTree::createNodeDeleter(ObjectNode& aNode) {
    cmnd::Vector commands;

    core::ObjectNode* parent = aNode.parent();
    XC_PTR_ASSERT(parent);
    if (!parent)
        return commands; // fail-safe code

    auto index = parent->children().indexOf(&aNode);
    XC_ASSERT(index >= 0);
    if (index < 0)
        return commands; // fail-safe code

    commands.push(BoneKeyUpdater::createNodeUnbinderForDelete(aNode));
    commands.push(ImageKeyUpdater::createResourceSleeperForDelete(aNode));
    commands.push(new cmnd::RemoveTree<core::ObjectNode>(&(parent->children()), index));
    commands.push(new cmnd::GrabDeleteObject<core::ObjectNode>(&aNode));

    return commands;
}

#if 0
cmnd::Vector ObjectTree::createNodeMover(
        const util::TreePos& aFrom, const util::TreePos& aTo)
{
    class MoveNodeCommand : public cmnd::Stable
    {
        util::LinkPointer<ObjectTree> mTree;
        util::TreePos mFrom;
        util::TreePos mTo;

    public:
        MoveNodeCommand(ObjectTree& aTree, const util::TreePos& aFrom, const util::TreePos& aTo)
            : mTree(aTree.pointee())
            , mFrom(aFrom)
            , mTo(aTo)
        {
        }

        virtual void undo()
        {
            if (!mTree) return;
            ObjectNode* target = mTree->eraseNode(mTo);
            mTree->insertNode(mFrom, target);
        }

        virtual void redo()
        {
            if (!mTree) return;
            ObjectNode* target = mTree->eraseNode(mFrom);
            mTree->insertNode(mTo, target);
        }
    };

    cmnd::Vector commands;
    commands.push(BoneKeyUpdater::createNodeUnbinderForMove(*this, aFrom, aTo));
    commands.push(new MoveNodeCommand(*this, aFrom, aTo));
    return commands;
}
#endif

cmnd::Vector
ObjectTree::createNodesMover(const QVector<util::TreePos>& aRemoved, const QVector<util::TreePos>& aInserted) {
    class MoveNodesCommand: public cmnd::Stable {
        util::LinkPointer<ObjectTree> mTree;
        QVector<util::TreePos> mRemoved;
        QVector<util::TreePos> mInserted;
        BoneUnbindWorkspacePtr mWorkspace;

    public:
        MoveNodesCommand(
            ObjectTree& aTree,
            const QVector<util::TreePos>& aRemoved,
            const QVector<util::TreePos>& aInserted,
            const BoneUnbindWorkspacePtr& aWorkspace
        ):
            mTree(aTree.pointee()), mRemoved(aRemoved), mInserted(aInserted), mWorkspace(aWorkspace) {}

        virtual void exec() {
            redo();
            mWorkspace.reset();
        }

        virtual void undo() {
            if (!mTree)
                return;

            QVector<ObjectNode*> nodes;
            for (auto itr = mInserted.rbegin(); itr != mInserted.rend(); ++itr) {
                ObjectNode* node = mTree->eraseNode(*itr);
                XC_PTR_ASSERT(node);
                nodes.push_back(node);
            }
            {
                auto nodeItr = nodes.begin();
                for (auto itr = mRemoved.rbegin(); itr != mRemoved.rend(); ++itr) {
                    mTree->insertNode(*itr, *nodeItr);
                    ++nodeItr;
                }
            }
        }

        virtual void redo() {
            if (!mTree)
                return;

            QVector<ObjectNode*> nodes;
            for (auto itr = mRemoved.begin(); itr != mRemoved.end(); ++itr) {
                // record one node
                if (mWorkspace) {
                    ObjectNode* node = mTree->findNode(*itr);
                    XC_PTR_ASSERT(node);
                    mWorkspace->push(*node);
                }
                // erase one node
                {
                    ObjectNode* node = mTree->eraseNode(*itr);
                    XC_PTR_ASSERT(node);
                    nodes.push_back(node);
                }
            }
            {
                auto nodeItr = nodes.begin();
                for (auto itr = mInserted.begin(); itr != mInserted.end(); ++itr) {
                    mTree->insertNode(*itr, *nodeItr);
                    ++nodeItr;
                }
            }
        }
    };

    cmnd::Vector commands;
    BoneUnbindWorkspacePtr workspace = std::make_shared<BoneUnbindWorkspace>();
    commands.push(new MoveNodesCommand(*this, aRemoved, aInserted, workspace));
    commands.push(BoneKeyUpdater::createNodesUnbinderForMove(workspace));
    return commands;
}

ObjectNode* ObjectTree::findNode(const util::TreePos& aPos) {
    if (!aPos.isValid())
        return nullptr;

    ObjectNode::Children::Iterator itr;
    ObjectNode* current = topNode();

    for (int i = 1; i < aPos.depth() - 1; ++i) {
        itr = current->children().at(aPos.row(i));
        if (itr == current->children().end())
            return nullptr;
        current = *itr;
    }

    itr = current->children().at(aPos.tailRow());
    if (itr == current->children().end())
        return nullptr;

    ObjectNode* target = *itr;
    return target;
}

ObjectNode* ObjectTree::eraseNode(const util::TreePos& aPos) {
    ObjectNode::Children::Iterator itr;
    ObjectNode* current = topNode();

    for (int i = 1; i < aPos.depth() - 1; ++i) {
        itr = current->children().at(aPos.row(i));
        current = *itr;
    }

    itr = current->children().at(aPos.tailRow());
    // qDebug() << "erase" << current->name() << aPos.tailRow() << (itr != current->children().end() ? (*itr)->name() :
    // "invalid");

    ObjectNode* target = *itr;
    current->children().erase(itr);
    return target;
}

void ObjectTree::insertNode(const util::TreePos& aPos, ObjectNode* aNode) {
    XC_PTR_ASSERT(aNode);
    ObjectNode::Children::Iterator itr;
    ObjectNode* current = topNode();

    for (int i = 1; i < aPos.depth() - 1; ++i) {
        itr = current->children().at(aPos.row(i));
        current = *itr;
    }

    itr = current->children().at(aPos.tailRow());
    current->children().insert(itr, aNode);
    // qDebug() << "insert" << current->name() << aPos.tailRow() << aNode->name();
}

cmnd::Vector ObjectTree::createResourceUpdater(const ResourceEvent& aEvent) {
    cmnd::Vector result;
    if (mTopNode) {
        ObjectNode::Iterator itr(mTopNode.data());
        while (itr.hasNext()) {
            result.push(itr.next()->createResourceUpdater(aEvent));
        }
    }
    return result;
}

QVector<ObjectNode*> ObjectTree::findNodesUsingResource(const img::ResourceNode& aResourceTop) const {
    QVector<ObjectNode*> result;
    if (!mTopNode)
        return result;

    // Match against every layer the asset is made of, not just its top node: an
    // object renders from the layer node its ImageKey serializes the address of,
    // and that layer need not be the asset's root.
    QSet<const img::ResourceNode*> resourceNodes;
    {
        img::ResourceNode::ConstIterator itr(&aResourceTop);
        while (itr.hasNext()) {
            resourceNodes.insert(itr.next());
        }
    }

    ObjectNode::ConstIterator itr(mTopNode.data());
    while (itr.hasNext()) {
        const ObjectNode* node = itr.next();
        const TimeLine* timeLine = node->timeLine();
        if (!timeLine)
            continue; // a folder holds no image key

        // Both the base value and every key on the timeline hold a resource
        // handle; the object references the asset if any of them points into it.
        const auto usesTarget = [&resourceNodes](const TimeKey* aKey) {
            if (!aKey || aKey->type() != TimeKeyType_Image)
                return false;
            const auto& handle = static_cast<const ImageKey*>(aKey)->data().resource();
            return handle && resourceNodes.contains(handle->serialAddress());
        };

        bool uses = usesTarget(timeLine->baseValue(TimeKeyType_Image));
        if (!uses) {
            const auto& map = timeLine->map(TimeKeyType_Image);
            for (auto it = map.begin(); it != map.end(); ++it) {
                if (usesTarget(it.value())) {
                    uses = true;
                    break;
                }
            }
        }

        if (uses)
            result.push_back(const_cast<ObjectNode*>(node));
    }

    return result;
}

void ObjectTree::onTimeLineModified(TimeLineEvent& aEvent, bool) { BoneKeyUpdater::onTimeLineModified(aEvent); }

void ObjectTree::onTreeRestructured(ObjectTreeEvent& aEvent, bool) { BoneKeyUpdater::onTreeRestructured(aEvent); }

void ObjectTree::onResourceModified(ResourceEvent& aEvent, bool) { BoneKeyUpdater::onResourceModified(aEvent); }

void ObjectTree::onProjectAttributeModified(ProjectEvent& aEvent, bool) {
    BoneKeyUpdater::onProjectAttributeModified(aEvent);
}

bool ObjectTree::serialize(Serializer& aOut) const {
    static const std::array<uint8, 8> kSignature = {'O', 'b', 'j', 'T', 'r', 'e', 'e', '_'};

    // signature
    auto pos = aOut.beginBlock(kSignature);

    // top node count
    aOut.write(topNode() ? 1 : 0);

    // nodes
    if (topNode()) {
        if (!serializeNode(aOut, *topNode())) {
            return false;
        }
    }

    aOut.endBlock(pos);

    return aOut.checkStream();
}

bool ObjectTree::serializeSkinWeights(Serializer& aOut, const ObjectNode& aNode) const {
    static const std::array<uint8, 8> kSignature = {'S', 'k', 'i', 'n', 'W', 'g', 't', '_'};

    auto pos = aOut.beginBlock(kSignature);

    const SkinWeights& weights = aNode.skinWeights();
    int recordCount = 0;
    for (const SkinWeights::NodeWeights& record : weights.nodes()) {
        if (record.node) {
            ++recordCount;
        }
    }
    aOut.write(recordCount);

    for (const SkinWeights::NodeWeights& record : weights.nodes()) {
        // A record whose target was deleted is dormant; the save drops it
        // rather than inventing a reference. Undo has already restored a live
        // record in that case (the command owns the snapshot).
        if (!record.node) {
            continue;
        }

        aOut.writeID(record.node.get());
        aOut.write(record.vertexCount);
        aOut.write(static_cast<int>(static_cast<quint32>(record.meshSignature & 0xffffffffULL)));
        aOut.write(static_cast<int>(static_cast<quint32>(record.meshSignature >> 32)));
        aOut.write(static_cast<int>(record.bakedBones.size()));

        for (const SkinWeights::BakedBone& bone : record.bakedBones) {
            aOut.write(bone.boneId);
            aOut.write(static_cast<int>(bone.entries.size()));

            for (const SkinWeights::Entry& entry : bone.entries) {
                if (entry.vertex > quint32(std::numeric_limits<int>::max())) {
                    return false;
                }
                aOut.write(static_cast<int>(entry.vertex));
                aOut.write(entry.weight);
            }
        }
    }

    aOut.endBlock(pos);
    return aOut.checkStream();
}

bool ObjectTree::deserializeSkinWeights(Deserializer& aIn, ObjectNode& aNode) {
    // 0.21 and older carry no block at all; the node stays envelope-only.
    if (aIn.version().majorVersion() != 0 || aIn.version().minorVersion() < 22) {
        return true;
    }

    if (!aIn.beginBlock("SkinWgt_")) {
        return aIn.errored("invalid skin weights block");
    }

    int recordCount = 0;
    aIn.read(recordCount);
    // Every record is at least: node reference (4), vertex count (4), mesh
    // signature (8), baked-bone count (4). A count larger than that allows is
    // a corrupt stream, not something to reserve for.
    constexpr int kMaxWeightRecords = 1 << 20;
    // A node cannot carry more baked bones than a skeleton can have (256); a
    // deleted-bone history is small, so 65536 is already generous.
    constexpr int kMaxBakedBonesPerNode = 1 << 16;
    // A real mesh is far below this; the bound exists so a corrupt count cannot
    // make one reserve huge.
    constexpr int kMaxWeightEntries = 1 << 24;
    constexpr qint64 kMinRecordBytes = 20;
    if (recordCount < 0 || recordCount > kMaxWeightRecords
        || qint64(recordCount) * kMinRecordBytes > qint64(aIn.remainingBytes())) {
        return aIn.errored("invalid skin weight record count");
    }

    QVector<SkinWeights::NodeWeights> records;
    records.reserve(recordCount);

    for (int r = 0; r < recordCount; ++r) {
        records.push_back(SkinWeights::NodeWeights());
        const int index = records.size() - 1;

        // The target is a descendant serialized after this owner; defer the
        // binding to the tree's ID solver, like the pre-0.21 influence cache
        // did. An unresolved reference fails the load: these are authored
        // weights, and silently dropping them would lose the user's paint.
        auto solver = [&aNode, index](void* aPtr) {
            auto* target = static_cast<ObjectNode*>(aPtr);
            auto& nodes = aNode.skinWeights().nodes();
            if (target && index >= 0 && index < nodes.size()) {
                nodes[index].node = target->pointee();
            }
        };
        if (!aIn.orderIDData(solver)) {
            return aIn.errored("invalid weight node reference");
        }

        int vertexCount = 0;
        aIn.read(vertexCount);
        if (vertexCount < 0 || vertexCount > (1 << 30)) {
            return aIn.errored("invalid skin weight vertex count");
        }
        records[index].vertexCount = vertexCount;

        int signatureLow = 0;
        int signatureHigh = 0;
        aIn.read(signatureLow);
        aIn.read(signatureHigh);
        records[index].meshSignature =
            (quint64(quint32(signatureHigh)) << 32) | quint64(quint32(signatureLow));

        int bakedCount = 0;
        aIn.read(bakedCount);
        // Every baked bone is at least an id and an entry count (8 bytes).
        if (bakedCount < 0 || bakedCount > kMaxBakedBonesPerNode
            || qint64(bakedCount) * 8 > qint64(aIn.remainingBytes())) {
            return aIn.errored("invalid baked bone count");
        }
        records[index].bakedBones.reserve(bakedCount);

        int previousBoneId = 0;
        for (int b = 0; b < bakedCount; ++b) {
            SkinWeights::BakedBone bone;
            aIn.read(bone.boneId);
            if (bone.boneId <= 0 || bone.boneId <= previousBoneId) {
                return aIn.errored("invalid baked bone id");
            }
            previousBoneId = bone.boneId;

            int entryCount = 0;
            aIn.read(entryCount);
            if (entryCount < 0 || entryCount > kMaxWeightEntries
                || qint64(entryCount) * 8 > qint64(aIn.remainingBytes())) {
                return aIn.errored("invalid skin weight entry count");
            }
            bone.entries.reserve(entryCount);

            quint32 previousVertex = 0;
            bool hasPrevious = false;
            for (int e = 0; e < entryCount; ++e) {
                int vertex = 0;
                float weight = 0.0f;
                aIn.read(vertex);
                aIn.read(weight);
                if (vertex < 0) {
                    return aIn.errored("invalid skin weight vertex");
                }
                if (!std::isfinite(weight) || weight < 0.0f || weight > 1.0f) {
                    return aIn.errored("invalid skin weight value");
                }
                const quint32 vertexIndex = quint32(vertex);
                // The build walks entries as a cursor, so the file's order is
                // part of the contract; a duplicate would make which value wins
                // depend on the reader.
                if (hasPrevious && vertexIndex <= previousVertex) {
                    return aIn.errored("unsorted skin weight entries");
                }
                previousVertex = vertexIndex;
                hasPrevious = true;
                bone.entries.push_back(SkinWeights::Entry{vertexIndex, weight});
            }

            records[index].bakedBones.push_back(bone);
        }
    }

    if (!aIn.endBlock()) {
        return aIn.errored("invalid end of skin weights block");
    }

    // The solvers capture this vector's indices; publish it only when every
    // record was read, so a failed read never leaves a partially bound field.
    aNode.skinWeights().nodes() = records;
    return aIn.checkStream();
}

bool ObjectTree::serializeNode(Serializer& aOut, const ObjectNode& aNode) const {
    static const std::array<uint8, 8> kSignature = {'O', 'b', 'j', 'N', 'o', 'd', 'e', '_'};

    // block begin
    auto pos = aOut.beginBlock(kSignature);

    // type
    aOut.write((int)aNode.type());

    // child count
    aOut.write((int)aNode.children().size());

    // reference id
    aOut.writeID(&aNode);

    // serialize object node
    if (!aNode.serialize(aOut)) {
        return false;
    }

    // authored skin weights (format 0.22); every node carries the block, empty
    // when it owns no baked field
    if (!serializeSkinWeights(aOut, aNode)) {
        return false;
    }

    // block end
    aOut.endBlock(pos);

    // check failure
    if (aOut.failure()) {
        return false;
    }

    // iterate children
    if (aNode.canHoldChild()) {
        for (auto child : aNode.children()) {
            XC_PTR_ASSERT(child);

            if (!serializeNode(aOut, *child)) {
                return false;
            }
        }
    }

    return !aOut.failure();
}

bool ObjectTree::deserialize(Deserializer& aIn) {
    // clear
    mTopNode.reset();

    // check block begin
    if (!aIn.beginBlock("ObjTree_"))
        return aIn.errored("invalid signature of object tree");

    // dive log scope
    aIn.pushLogScope("ObjectTree");

    // top node count
    int topNodeCount = 0;
    aIn.read(topNodeCount);

    if (topNodeCount == 1) {
        aIn.pushLogScope("ObjectNodes");

        // deserialize each node
        if (!deserializeNode(aIn, nullptr))
            return false;

        aIn.popLogScope();
    } else if (topNodeCount != 0) {
        return aIn.errored("invalid top node count");
    }

    // check block end
    if (!aIn.endBlock())
        return aIn.errored("invalid end of object tree");

    // rise log scope
    aIn.popLogScope();

    // end
    return aIn.checkStream();
}

bool ObjectTree::deserializeNode(Deserializer& aIn, ObjectNode* aParent) {
    // check block begin
    if (!aIn.beginBlock("ObjNode_"))
        return aIn.errored("invalid signature of object node");

    // dive log scope
    if (aParent)
        aIn.pushLogScope(aParent->name());

    // node type
    int type = 0;
    aIn.read(type);
    if (type >= ObjectType_TERM)
        return aIn.errored("invalid node type");

    // child count
    int childCount = 0;
    aIn.read(childCount);
    if (childCount < 0)
        return aIn.errored("invalid child count");

    // create node
    ObjectNode* node(createSerialNode(type));
    if (!node)
        return aIn.errored("failed to create node");

    // reference id
    if (!aIn.bindIDData(node)) {
        delete node;
        return aIn.errored("failed to bind reference id");
    }

    // deserialize node
    if (!node->deserialize(aIn)) {
        delete node;
        return aIn.errored("failed to deserialize node");
    }

    // authored skin weights (format 0.22). Their node references are deferred:
    // the descendants they name are read after this owner.
    if (!deserializeSkinWeights(aIn, *node)) {
        delete node;
        return false;
    }

    // push to tree
    if (!aParent) {
        grabTopNode(node);
    } else {
        aParent->children().pushBack(node);
    }

    // check block end
    if (!aIn.endBlock())
        return aIn.errored("invalid end of object node");

    // check failure
    if (aIn.failure())
        return aIn.errored("stream error");

    // check holdability
    if (!node->canHoldChild() && childCount > 0)
        return aIn.errored("invalid holdability");

    // iterate children
    for (int i = 0; i < childCount; ++i) {
        if (!deserializeNode(aIn, node))
            return false;
    }

    // rise log scope
    if (aParent)
        aIn.popLogScope();

    // progress report
    aIn.reportCurrent();

    // end
    return aIn.checkStream();
}

ObjectNode* ObjectTree::createSerialNode(int aType) {
    ObjectNode* node = nullptr;

    if (aType == ObjectType_Layer) {
        // the LayerNode eagerly compiles its shaders, which needs a current GL context
        gl::Global::makeCurrentIfReady();
        LayerNode* layer = new LayerNode(QString("no name"), mShaderHolder);
        layer->setVisibility(true);
        node = layer;
    } else if (aType == ObjectType_Folder) {
        FolderNode* folder = new FolderNode(QString("no name"));
        folder->setVisibility(true);
        node = folder;
    }

    return node;
}

} // namespace core
