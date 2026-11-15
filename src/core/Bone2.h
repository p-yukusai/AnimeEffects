#ifndef CORE_BONE2_H
#define CORE_BONE2_H

#include <array>
#include <QVector2D>
#include <QMatrix4x4>

#include "Project.h"
#include "util/LifeLink.h"
#include "util/TreeNodeBase.h"
#include "util/TreeIterator.h"
#include "core/BoneShape.h"
#include "core/Serializer.h"
#include "core/Deserializer.h"
namespace core {
class ObjectNode;
}

namespace core {

class Bone2: public util::TreeNodeBase<Bone2> {
public:
    typedef util::TreeNodeBase<Bone2>::Children ChildrenType;
    typedef util::TreeIterator<Bone2, ChildrenType::Iterator> Iterator;
    typedef util::TreeIterator<const Bone2, ChildrenType::ConstIterator> ConstIterator;

    Bone2();
    explicit Bone2(const Bone2& aRhs);
    ~Bone2();

    // for original bone
    void setWorldPos(const QVector2D& aWorldPos, const Bone2* aParent);
    void setRange(int aIndex, const QVector2D& aRange);
    void setShape(const BoneShape& aShape);
    QList<ObjectNode*>& bindingNodes();
    const QList<ObjectNode*>& bindingNodes() const;
    bool isBinding(const ObjectNode& aNode) const;

    // geometry info
    const QVector2D& localPos() const;
    float localAngle() const;
    const QVector2D& range(int aIndex) const;
    bool hasValidRange() const;
    // Whether the influence build can evaluate this bone's envelope at all:
    // a parentless bone is a rig anchor, not an envelope, and an empty range
    // means the same. The build and the bake share this one predicate so a new
    // rule cannot land in one path only.
    bool hasInfluenceEnvelope() const { return parent() && hasValidRange(); }
    QVector2D blendedRange(float aRate) const;
    const BoneShape& shape() const;

    // Stable identity of an authored bone within a project. It is the key
    // authored skin weights address: the influence map's uint8 index is a GPU
    // attribute and follows tree order, which edits and undo can change.
    //
    // Assigned from a file-local counter on construction, preserved by the copy
    // constructor (a cloned BoneKey addresses the same bones) and restored from
    // the file on load. A shadow reports its origin's id: it is the same bone
    // posed at another frame. Ids start at 1 and 0 is never valid.
    int boneId() const { return mOrigin ? mOrigin->mBoneId : mBoneId; }
    // Raise the id counter so stored ids stay unique. Non-positive values are
    // not stored ids and never move the counter.
    static void reserveBoneId(int aValue);

    // Whether this and aRhs are the same authored envelope. This is the bake's
    // shared-rig test across the BoneKeys of one owner: the same id on two keys
    // may only share one baked field when this is true. Poses (mRotate) do not
    // count; the envelope is built from the rest geometry only.
    bool hasSameAuthoredGeometry(const Bone2& aRhs) const;

    // shadow bone
    Bone2* createShadow() const;

    // transform
    void setRotate(float aRotate);
    float rotate() const;

    // cache
    void updateWorldTransform();
    const QVector2D& worldPos() const;
    float worldAngle() const;
    QMatrix4x4 transformationMatrix(const QVector2D& aToPos, float aToAngle) const;
    QMatrix4x4 transformationMatrix(const Bone2& aTo) const;
    QMatrix4x4 transformationMatrix(const QMatrix4x4& aToMtx) const;

    // focus
    bool isFocused() const { return mFocus.isLinking(); }
    void setFocus(util::LifeLink& aLink) { mFocus = aLink; }

    // select
    bool isSelected() const { return mSelect.isLinking(); }
    void setSelect(util::LifeLink& aLink) { mSelect = aLink; }

    // serialize
    bool serialize(Serializer& aOut) const;
    void deserializeFromJson(QJsonObject json, Project *aProject, const Bone2 *origin);
    QJsonObject serializeToJson();
    util::IDAssigner<const void*> boneIDAssigner;
    bool deserialize(Deserializer& aIn);

private:
    // for shadow
    const Bone2* mOrigin;
    // identity (see boneId())
    int mBoneId;
    // geometry
    QVector2D mLocalPos;
    float mLocalAngle;
    std::array<QVector2D, 2> mRange;
    BoneShape mShape;
    // bind
    QList<ObjectNode*> mBindingNodes;
    // cache
    QVector2D mWorldPos;
    float mWorldAngle;
    // transform
    float mRotate;
    // focusing
    util::LifeLink::Node mFocus;
    util::LifeLink::Node mSelect;
};

} // namespace core

#endif // CORE_BONE2_H
