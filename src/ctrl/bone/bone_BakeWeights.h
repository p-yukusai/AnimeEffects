#ifndef CTRL_BONE_BAKEWEIGHTS_H
#define CTRL_BONE_BAKEWEIGHTS_H

#include <QString>
#include <QVector>
#include "cmnd/Stable.h"
#include "ctrl/CmndName.h"
#include "util/LinkPointer.h"
#include "core/BoneKey.h"
#include "core/Project.h"
#include "core/SkinWeights.h"

namespace ctrl {
namespace bone {

// Evaluate one bone's envelope for every node the key skins at the key's frame
// and return the authored fields a bake would write.
//
// This is the one-off half of the bake command: it reads the document and never
// mutates it. A node with no area mesh at that frame is skipped (its state stays
// Envelope / Mixed); a node with a mesh gets a record even when this bone's
// envelope is zero everywhere, because the record's PRESENCE is the Baked state
// and an empty field contributes nothing (influence-painting-plan §2, §4).
//
// Refuses before evaluating anything when the owner's other BoneKeys do not
// carry a same-id bone with the same authored geometry: one baked field cannot
// describe two different skeletons. `aError` names the offending frame.
bool bakeBoneFields(
    core::Project& aProject, core::ObjectNode& aOwner, core::BoneKey& aKey, const core::Bone2& aBone,
    QVector<core::SkinWeights::NodeWeights>& aFields, QString* aError
);

// One undo step for a bake. The command carries the before/after records (a
// value replay, like every other command); it does not recompute an envelope.
class BakeWeightsCommand: public cmnd::Stable {
public:
    BakeWeightsCommand(
        core::Project& aProject, core::ObjectNode& aOwner,
        const QVector<core::SkinWeights::NodeWeights>& aFields
    );

    QString name() const override { return CmndName::tr("Bake bone weights"); }
    bool isUseless() const override { return mAfter.isEmpty() || !mOwner; }

    void exec() override;
    void redo() override;
    void undo() override;

private:
    util::LinkPointer<core::Project> mProject;
    util::LinkPointer<core::ObjectNode> mOwner;
    QVector<core::SkinWeights::NodeWeights> mAfter;
    // A record with a null node means "this node had no record" (Envelope),
    // which is distinct from an empty Baked record.
    QVector<core::SkinWeights::NodeWeights> mBefore;
    bool mCaptured = false;
};

} // namespace bone
} // namespace ctrl

#endif // CTRL_BONE_BAKEWEIGHTS_H
