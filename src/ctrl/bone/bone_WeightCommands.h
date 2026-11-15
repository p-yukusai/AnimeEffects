#ifndef CTRL_BONE_WEIGHTCOMMANDS_H
#define CTRL_BONE_WEIGHTCOMMANDS_H

#include <QString>
#include <QVector>
#include "cmnd/Stable.h"
#include "core/SkinWeights.h"
#include "util/LinkPointer.h"

namespace core {
class Bone2;
class BoneKey;
class ObjectNode;
class Project;
} // namespace core

namespace ctrl {
namespace bone {

// ONE authored-weight edit as ONE undo step: a value replay of the owner's
// records for the nodes it touched. `before`/`after` are parallel lists; a
// record whose node is null means "the owner had no record for this node", which
// is distinct from an empty baked field (its presence is the state).
//
// The command OWNS the rebuild event, rather than a macro listener: the paint
// stroke rewrites this command in place while Stack::isModifiable holds, and a
// listener would fire on the first stamp only. redo/undo emit the owner event
// every time, so an undo of a stroke and a redo of a bake rebuild exactly once.
class AssignSkinWeightsCommand: public cmnd::Stable {
public:
    AssignSkinWeightsCommand(
        core::Project& aProject, core::ObjectNode& aOwner,
        const QVector<core::SkinWeights::NodeWeights>& aBefore,
        const QVector<core::SkinWeights::NodeWeights>& aAfter, const QString& aName
    );

    QString name() const override { return mName; }
    bool isUseless() const override;

    void exec() override;
    void redo() override;
    void undo() override;

    // Paint-stroke rewrite: replace the after state for one node (or append it)
    // without touching the before state. Only meaningful while the command is
    // the stack's modifiable one.
    void setAfter(const core::SkinWeights::NodeWeights& aRecord);

    const QVector<core::SkinWeights::NodeWeights>& before() const { return mBefore; }
    const QVector<core::SkinWeights::NodeWeights>& after() const { return mAfter; }

private:
    void writeAfter();
    void writeBefore();

    util::LinkPointer<core::Project> mProject;
    util::LinkPointer<core::ObjectNode> mOwner;
    QVector<core::SkinWeights::NodeWeights> mBefore;
    QVector<core::SkinWeights::NodeWeights> mAfter;
    QString mName;
};

// Bake ONE bone's envelope into the nodes the key skins at its frame.
//
// aReplace == false (Bake): writes the nodes that are still Envelope and leaves
// an already-baked node's field untouched, so baking a newly added layer never
// drops the paint on the rest.
// aReplace == true (Rebake): replaces the field on every target from the current
// envelope and refreshes the record's vertex count and mesh signature; manual
// edits on the replaced nodes are lost (one undo restores them).
//
// Returns false with a message in aError when the bone cannot be baked (no
// target mesh, every target already baked, or the shared-rig refusal from
// bakeBoneFields, which names the offending frame).
bool bakeBoneWeights(
    core::Project& aProject, core::ObjectNode& aOwner, core::BoneKey& aKey, const core::Bone2& aBone,
    bool aReplace, QString* aError
);

// Remove the bone's baked field from every record that carries it, returning
// those nodes to the live envelope. One undo restores the field.
bool revertBoneWeights(
    core::Project& aProject, core::ObjectNode& aOwner, const core::Bone2& aBone, QString* aError
);

// Empty the bone's baked field but keep every record present, so the bone stays
// Baked and contributes zero. One undo restores the entries.
bool clearBoneWeights(
    core::Project& aProject, core::ObjectNode& aOwner, const core::Bone2& aBone, QString* aError
);

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_WEIGHTCOMMANDS_H
