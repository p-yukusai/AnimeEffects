#include "ctrl/TreeActions.h"

#include <QHash>
#include <algorithm>
#include <functional>
#include <memory>

#include "ctrl/CmndName.h"
#include "ctrl/TimeLineUtil.h"
#include "core/Project.h"
#include "core/ObjectNode.h"
#include "core/LayerNode.h"
#include "core/FolderNode.h"
#include "core/ObjectTree.h"
#include "core/ObjectNodeUtil.h"
#include "core/ObjectTreeNotifier.h"
#include "core/MoveKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/DepthKey.h"
#include "core/OpaKey.h"
#include "core/HsvKey.h"
#include "core/PoseKey.h"
#include "core/FFDKey.h"
#include "core/BoneKey.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "img/ResourceNode.h"
#include "gl/Global.h"

namespace ctrl {
namespace tree {
namespace {

bool insertNewNode(
    core::Project& aProject, core::ObjectNode& aParent, int aIndex, core::ObjectNode* aNode,
    const QString& aMacroName
);

//-------------------------------------------------------------------------------------------------
// one node's name, as a command with an attribute event
class RenameCommand: public cmnd::Stable {
    core::ObjectNode& mNode;
    QString mPrevName;
    QString mNextName;

public:
    RenameCommand(core::ObjectNode& aNode, const QString& aName):
        mNode(aNode), mPrevName(), mNextName(aName) {}

    QString name() const override { return CmndName::tr("Rename object"); }
    void exec() override {
        mPrevName = mNode.name();
        redo();
    }
    void redo() override { mNode.setName(mNextName); }
    void undo() override { mNode.setName(mPrevName); }
};

//-------------------------------------------------------------------------------------------------
class VisibleCommand: public cmnd::Stable {
    core::ObjectNode& mNode;
    bool mPrev;
    bool mNext;

public:
    VisibleCommand(core::ObjectNode& aNode, bool aVisible):
        mNode(aNode), mPrev(aNode.isVisible()), mNext(aVisible) {}

    QString name() const override {
        return mNext ? CmndName::tr("Show object") : CmndName::tr("Hide object");
    }
    void exec() override { redo(); }
    void redo() override { mNode.setVisibility(mNext); }
    void undo() override { mNode.setVisibility(mPrev); }
};

//-------------------------------------------------------------------------------------------------
class SlimCommand: public cmnd::Stable {
    core::ObjectNode& mNode;
    bool mPrev;
    bool mNext;

public:
    SlimCommand(core::ObjectNode& aNode, bool aSlimmed):
        mNode(aNode), mPrev(aNode.isSlimmedDown()), mNext(aSlimmed) {}

    QString name() const override { return CmndName::tr("Contract object"); }
    void exec() override { redo(); }
    void redo() override { mNode.setSlimDown(mNext); }
    void undo() override { mNode.setSlimDown(mPrev); }
};

//-------------------------------------------------------------------------------------------------
// Every key of every channel of one timeline, cloned into another. The keys that hang off
// a mesh/image key (FFD, Pose) are re-linked to the CLONE of their parent, which is why
// this cannot be a per-type loop of createClone(): a clone copies its own data only, and
// the parent pointer a child carries names the original.
void copyKeys(const core::TimeLine& aSource, core::TimeLine& aDest) {
    QHash<const core::TimeKey*, core::TimeKey*> parents;
    struct Attached {
        core::TimeKey* key;
        core::TimeKey* parent;
    };
    QVector<Attached> attached;

    for (int t = 0; t != core::TimeKeyType_TERM; ++t) {
        const auto type = core::TimeKeyType(t);
        for (auto itr = aSource.map(type).begin(); itr != aSource.map(type).end(); ++itr) {
            core::TimeKey* key = itr.value();
            if (!key)
                continue;
            core::TimeKey* clone = key->createClone();
            if (!aDest.adoptKey(type, key->frame(), clone)) {
                delete clone;
                continue;
            }
            if (clone->canHoldChild())
                parents.insert(key, clone);
            if (key->parent())
                attached.append(Attached{clone, key->parent()});
        }
    }
    for (const Attached& child : attached) {
        core::TimeKey* parent = child.parent;
        const auto itr = parents.constFind(parent);
        if (itr != parents.constEnd())
            parent = itr.value();
        parent->children().pushBack(child.key);
    }
}

//-------------------------------------------------------------------------------------------------
// A deep copy of one node, built on the first redo and held by the command: redo re-attaches
// it, undo detaches it. It is ONE command, so a folder's whole subtree - and every key in it
// - comes back with a single Ctrl+Z. The widget could not do that: it pushed a command per
// node and per key, so undo peeled the copy apart one channel at a time, and the key commands
// (pushed before the node's own) named a timeline whose node was already gone.
class DuplicateCommand: public cmnd::Stable {
    core::Project& mProject;
    core::ObjectNode& mSource;
    core::ObjectNode* mParent;
    int mIndex;
    std::unique_ptr<core::ObjectNode> mCopy;

public:
    DuplicateCommand(core::Project& aProject, core::ObjectNode& aSource):
        mProject(aProject),
        mSource(aSource),
        mParent(aSource.parent()),
        mIndex(mParent ? mParent->children().indexOf(&aSource) + 1 : 0),
        mCopy() {}

    core::ObjectNode* copy() const { return mCopy.get(); }

    QString name() const override {
        return mSource.canHoldChild() ? CmndName::tr("Duplicate base folder") : CmndName::tr("Duplicate layer");
    }

    void exec() override { redo(); }

    void redo() override {
        if (!mParent)
            return;
        if (!mCopy) {
            mCopy.reset(buildNode(mSource));
            if (!mCopy)
                return;
            copyContents(mSource, *mCopy);
        }
        mParent->children().insert(mIndex, mCopy.get());
    }

    void undo() override {
        if (!mParent || !mCopy)
            return;
        // Undo is LIFO, so nothing else has touched this parent between this command's
        // redo and its undo: the recorded index still names the copy.
        if (mParent->children().indexOf(mCopy.get()) != mIndex)
            return;
        mParent->children().erase(mIndex);
    }

private:
    core::ObjectNode* buildNode(core::ObjectNode& aSource) {
        const QString name = aSource.name() + QStringLiteral(" (Copy)");
        if (aSource.canHoldChild())
            return new core::FolderNode(name);
        // the LayerNode eagerly compiles its shaders, which needs a current GL context; Qt
        // only guarantees one during paint, so harden this the way the widget did
        gl::Global::makeCurrentIfReady();
        return new core::LayerNode(name, mProject.objectTree().shaderHolder());
    }

    // The copy's own attributes and channels, then its children recursively. Clipping,
    // blend, depth and the default opacity live on the CONCRETE node (ObjectNode is the
    // thin base the tree walks), so they are copied through the type.
    void copyContents(core::ObjectNode& aSource, core::ObjectNode& aCopy) {
        aCopy.setVisibility(aSource.isVisible());
        aCopy.setSlimDown(aSource.isSlimmedDown());
        aCopy.setInitialRect(aSource.initialRect());
        if (auto* source = dynamic_cast<core::LayerNode*>(&aSource)) {
            if (auto* copy = dynamic_cast<core::LayerNode*>(&aCopy)) {
                copy->setClipped(source->isClipped());
                copy->setBlendMode(source->blendMode());
                copy->setDefaultDepth(source->initialDepth());
            }
        } else if (auto* source = dynamic_cast<core::FolderNode*>(&aSource)) {
            if (auto* copy = dynamic_cast<core::FolderNode*>(&aCopy)) {
                copy->setClipped(source->isClipped());
                copy->setBlendMode(source->blendMode());
                copy->setDefaultDepth(source->initialDepth());
            }
        }
        // the DEFAULT opacity belongs to the timeline the copy now owns (current() was
        // copied above), so it needs no separate write

        if (core::TimeLine* source = aSource.timeLine()) {
            if (core::TimeLine* copy = aCopy.timeLine()) {
                copy->current() = source->current();
                copyKeys(*source, *copy);
                for (core::TimeKey* bone : copy->map(core::TimeKeyType_Bone)) {
                    if (auto* boneKey = dynamic_cast<core::BoneKey*>(bone))
                        boneKey->resetCaches(mProject, aSource);
                }
                copy->current().clearCaches();
                copy->current().clearMasterCache();
            }
        }

        for (core::ObjectNode* child : aSource.children()) {
            std::unique_ptr<core::ObjectNode> childCopy(buildNode(*child));
            if (!childCopy)
                continue;
            copyContents(*child, *childCopy);
            aCopy.children().pushBack(childCopy.release());
        }
    }
};

//-------------------------------------------------------------------------------------------------
// One layer from an imported asset: its own name, image, posture, blend mode, rect and
// visibility, with its z-order taken from its place in the resource tree.
core::ObjectNode* buildLayerFromResource(
    core::Project& aProject, core::ObjectNode& aParent, int aIndex, const img::ResourceNode& aResource,
    int aResIndex, const QString& aMacroName
) {
    const img::ResourceHandle handle = aResource.handle();
    if (!handle)
        return nullptr;

    gl::Global::makeCurrentIfReady();
    auto* layer = new core::LayerNode(aResource.data().identifier(), aProject.objectTree().shaderHolder());
    layer->setDefaultImage(handle);
    layer->setDefaultPosture(handle->center());
    layer->setBlendMode(handle->blendMode());
    layer->setInitialRect(handle->rect());
    layer->setVisibility(handle->isVisible());
    layer->setDefaultDepth(std::max(1.0f, static_cast<float>(aResIndex)));
    layer->setDefaultOpacity(1.0f);
    layer->setClipped(false);

    if (!insertNewNode(aProject, aParent, aIndex, layer, aMacroName)) {
        delete layer;
        return nullptr;
    }
    return layer;
}

//-------------------------------------------------------------------------------------------------
// Whether one node of the set is covered by another (an ancestor, or a repeat): its own
// command would then name a node the ancestor's removal already took.
bool coveredBy(const QVector<core::ObjectNode*>& aAll, const core::ObjectNode* aNode) {
    for (const core::ObjectNode* other : aAll) {
        if (other == aNode || !other || !aNode)
            continue;
        for (const core::ObjectNode* parent = aNode->parent(); parent; parent = parent->parent()) {
            if (parent == other)
                return true;
        }
    }
    return false;
}

bool insertNewNode(
    core::Project& aProject, core::ObjectNode& aParent, int aIndex, core::ObjectNode* aNode,
    const QString& aMacroName
) {
    if (!aNode)
        return false;
    const int index = std::max(0, std::min(aIndex, static_cast<int>(aParent.children().size())));

    cmnd::ScopedMacro macro(aProject.commandStack(), aMacroName);
    {
        auto* notifier = new core::ObjectTreeNotifier(aProject);
        notifier->event().setType(core::ObjectTreeEvent::Type_Add);
        notifier->event().pushTarget(&aParent, *aNode);
        macro.grabListener(notifier);
    }
    aProject.commandStack().push(new cmnd::GrabNewObject<core::ObjectNode>(aNode));
    aProject.commandStack().push(
        new cmnd::InsertTree<core::ObjectNode>(&aParent.children(), index, aNode)
    );
    return true;
}

} // namespace

//-------------------------------------------------------------------------------------------------
void rename(core::Project& aProject, core::ObjectNode& aNode, const QString& aName) {
    if (aNode.name() == aName)
        return;
    cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Rename object"));
    macro.grabListener(new core::ObjectNodeUtil::AttributeNotifier(aProject, aNode));
    aProject.commandStack().push(new RenameCommand(aNode, aName));
}

void setVisibility(core::Project& aProject, core::ObjectNode& aNode, bool aVisible) {
    if (aNode.isVisible() == aVisible)
        return;
    cmnd::ScopedMacro macro(
        aProject.commandStack(), aVisible ? CmndName::tr("Show object") : CmndName::tr("Hide object")
    );
    macro.grabListener(new core::ObjectNodeUtil::AttributeNotifier(aProject, aNode));
    aProject.commandStack().push(new VisibleCommand(aNode, aVisible));
}

void setSlimmed(core::Project& aProject, core::ObjectNode& aNode, bool aSlimmed) {
    if (aNode.isSlimmedDown() == aSlimmed)
        return;
    cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Contract object"));
    macro.grabListener(new core::ObjectNodeUtil::AttributeNotifier(aProject, aNode));
    aProject.commandStack().push(new SlimCommand(aNode, aSlimmed));
}

bool canDelete(const QVector<core::ObjectNode*>& aNodes) {
    if (aNodes.isEmpty())
        return true;
    for (const core::ObjectNode* node : aNodes) {
        if (!node || !node->parent())
            return false;
    }
    return true;
}

bool appendDeleteNodes(
    core::Project& aProject, const QVector<core::ObjectNode*>& aNodes, cmnd::ScopedMacro& aMacro
) {
    if (!canDelete(aNodes) || aNodes.isEmpty())
        return false;

    struct Target {
        core::ObjectNode* node;
        core::ObjectNode* parent;
        int index;
    };
    QVector<Target> targets;
    for (core::ObjectNode* node : aNodes) {
        if (!node || !node->parent() || coveredBy(aNodes, node))
            continue;
        const int index = node->parent()->children().indexOf(node);
        if (index < 0)
            continue;
        targets.push_back({node, node->parent(), index});
    }
    if (targets.isEmpty())
        return false;

    // Highest index first within one parent: a removal renumbers the ones after it, and the
    // commands replay in the order they were pushed.
    std::sort(targets.begin(), targets.end(), [](const Target& aLhs, const Target& aRhs) {
        if (aLhs.parent != aRhs.parent)
            return std::less<core::ObjectNode*>()(aLhs.parent, aRhs.parent);
        return aLhs.index > aRhs.index;
    });

    auto* notifier = new core::ObjectTreeNotifier(aProject);
    notifier->event().setType(core::ObjectTreeEvent::Type_Delete);
    for (const Target& target : targets)
        notifier->event().pushTarget(target.parent, *target.node);
    aMacro.grabListener(notifier);

    for (const Target& target : targets)
        aProject.commandStack().push(aProject.objectTree().createNodeDeleter(*target.node));
    return true;
}

void deleteNodes(core::Project& aProject, const QVector<core::ObjectNode*>& aNodes) {
    if (!canDelete(aNodes) || aNodes.isEmpty())
        return;
    cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Delete object"));
    appendDeleteNodes(aProject, aNodes, macro);
}

void moveNodes(
    core::Project& aProject,
    const QVector<util::TreePos>& aRemoved,
    const QVector<util::TreePos>& aInserted
) {
    if (aRemoved.isEmpty() || aRemoved.size() != aInserted.size())
        return;
    cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Move object"));
    {
        auto* notifier = new core::ObjectTreeNotifier(aProject);
        notifier->event().setType(core::ObjectTreeEvent::Type_Move);
        for (const util::TreePos& pos : aRemoved) {
            core::ObjectNode* node = aProject.objectTree().findNode(pos);
            if (node && node->parent())
                notifier->event().pushTarget(node->parent(), *node);
        }
        macro.grabListener(notifier);
    }
    for (cmnd::Base* command : aProject.objectTree().createNodesMover(aRemoved, aInserted))
        aProject.commandStack().push(command);
}

core::ObjectNode* createFolder(
    core::Project& aProject, core::ObjectNode& aParent, int aIndex, float aDepth
) {
    auto* folder = new core::FolderNode(QStringLiteral("folder0"));
    folder->setDefaultPosture(QVector2D());
    folder->setDefaultDepth(aDepth);
    folder->setDefaultOpacity(1.0f);
    if (!insertNewNode(aProject, aParent, aIndex, folder, CmndName::tr("create a folder object"))) {
        delete folder;
        return nullptr;
    }
    return folder;
}

core::ObjectNode* createLayerFromResource(
    core::Project& aProject,
    core::ObjectNode& aParent,
    int aIndex,
    const img::ResourceNode& aResource,
    int aResIndex
) {
    return buildLayerFromResource(
        aProject, aParent, aIndex, aResource, aResIndex, CmndName::tr("Create a layer")
    );
}

core::ObjectNode* createFolderFromResource(
    core::Project& aProject,
    core::ObjectNode& aParent,
    int aIndex,
    const img::ResourceNode& aResource,
    int aResIndex
) {
    auto* folder = new core::FolderNode(aResource.data().identifier());
    folder->setClipped(false);
    folder->setVisibility(true);
    folder->setDefaultDepth(std::max(1.0f, static_cast<float>(aResIndex)));
    folder->setBlendMode(aResource.data().blendMode());
    folder->setInitialRect(aResource.data().rect());
    folder->setDefaultOpacity(1.0f);
    if (!insertNewNode(aProject, aParent, aIndex, folder, CmndName::tr("create a folder object"))) {
        delete folder;
        return nullptr;
    }
    return folder;
}

core::ObjectNode* duplicateNode(core::Project& aProject, core::ObjectNode& aSource) {
    if (!aSource.parent())
        return nullptr;
    const int index = aSource.parent()->children().indexOf(&aSource);
    if (index < 0)
        return nullptr;

    // THE COMMAND IS PUSHED INSIDE THE MACRO AND READ AFTER IT: a push into an open macro is
    // collected and executed when the macro closes (cmnd::ScopedMacro -> Stack::endMacro), so
    // the copy does not exist until this scope ends. Reading it inside answered null, which
    // is how "duplicate" once returned nothing while the copy really landed.
    auto* command = new DuplicateCommand(aProject, aSource);
    {
        cmnd::ScopedMacro macro(
            aProject.commandStack(),
            aSource.canHoldChild() ? CmndName::tr("Duplicate base folder") : CmndName::tr("Duplicate layer")
        );
        auto* notifier = new core::ObjectTreeNotifier(aProject);
        notifier->event().setType(core::ObjectTreeEvent::Type_Add);
        // the copy does not exist yet (the command builds it), so the event names the node
        // it is a copy OF: every viewer re-derives from the document, and what it needs to
        // know is that a node appeared after this one
        notifier->event().pushTarget(aSource.parent(), aSource);
        macro.grabListener(notifier);
        aProject.commandStack().push(command);
    }
    return command->copy();
}

void addDefaultKeys(core::Project& aProject, core::ObjectNode& aNode) {
    core::TimeLine* line = aNode.timeLine();
    if (!line)
        return;

    cmnd::ScopedMacro macro(aProject.commandStack(), CmndName::tr("Add default keys"));
    auto* notifier = new TimeLineUtil::Notifier(aProject);
    notifier->event().setType(core::TimeLineEvent::Type_PushKey);
    macro.grabListener(notifier);

    // one frame-0 key per channel the node is missing, named in the event as it is made
    const auto push = [&](core::TimeKey* aKey, core::TimeKey* aParentKey = nullptr) {
        if (!aKey)
            return;
        notifier->event().pushTarget(aNode, aKey->type(), aKey->frame());
        TimeLineUtil::appendNewKey(aProject, aNode, aKey->frame(), aKey, aParentKey);
    };
    const auto needs = [line](core::TimeKeyType aType) {
        const auto& map = line->map(aType);
        return map.isEmpty() || map.firstKey() != 0;
    };

    if (needs(core::TimeKeyType_Move)) {
        auto* key = new core::MoveKey();
        key->setPos(line->current().srt().pos());
        key->setCentroid(line->current().srt().centroid());
        push(key);
    }
    if (needs(core::TimeKeyType_Rotate)) {
        auto* key = new core::RotateKey();
        key->setRotate(line->current().srt().rotate());
        push(key);
    }
    if (needs(core::TimeKeyType_Scale)) {
        auto* key = new core::ScaleKey();
        key->setScale(line->current().srt().scale());
        push(key);
    }
    if (needs(core::TimeKeyType_Depth)) {
        auto* key = new core::DepthKey();
        key->setDepth(line->current().depth());
        push(key);
    }
    if (needs(core::TimeKeyType_Opa)) {
        auto* key = new core::OpaKey();
        key->setOpacity(line->current().opa().opacity());
        push(key);
    }
    if (needs(core::TimeKeyType_HSV)) {
        auto* key = new core::HSVKey();
        key->setHSV(line->current().hsv().hsv());
        push(key);
    }
    if (line->current().bone().areaKey() && needs(core::TimeKeyType_Pose)) {
        auto* key = new core::PoseKey();
        core::BoneKey* parentKey = line->current().bone().areaKey();
        if (parentKey == line->current().poseParent()) {
            key->data() = line->current().pose();
        } else {
            key->data().createBonesBy(*parentKey);
        }
        push(key, parentKey);
    }
    if (aNode.type() == core::ObjectType_Layer && needs(core::TimeKeyType_FFD)) {
        auto* key = new core::FFDKey();
        key->data() = line->current().ffd();
        core::TimeKey* parentKey = line->current().ffdMeshParent();
        push(key, parentKey);
    }
    if (aNode.type() == core::ObjectType_Folder) {
        for (core::ObjectNode* child : aNode.children())
            addDefaultKeys(aProject, *child);
    }
}

} // namespace tree
} // namespace ctrl
