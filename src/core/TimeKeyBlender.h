#ifndef CORE_TIMEKEYBLENDER_H
#define CORE_TIMEKEYBLENDER_H

#include <array>
#include <QVector3D>
#include "util/ITreeSeeker.h"
#include "core/ObjectTree.h"
#include "core/TimeInfo.h"
#include "core/TimeLineEvent.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeKeyGatherer.h"
#include "core/TimeCacheLock.h"

namespace core {

class TimeKeyBlender {
public:
    struct SeekData {
        ObjectNode* objNode;
        TimeKeyExpans* expans;
    };
    typedef util::ITreeSeeker<SeekData, ObjectNode*> SeekerType;
    typedef SeekerType::Position PositionType;


    static QMatrix4x4 getLocalSRMatrix(const ObjectNode& aNode, const TimeInfo& aTime);

    static QMatrix4x4 getWorldCSRTMatrix(ObjectNode& aNode, const TimeInfo& aTime);
    static QMatrix4x4 getRelativeMatrix(ObjectNode& aNode, const TimeInfo& aTime, const ObjectNode* aParent);
    static LayerMesh* getAreaMesh(ObjectNode& aNode, const TimeInfo& aTime);
    // The key that OWNS that mesh (a Mesh key, or the Image key whose grid it is) with the
    // mesh itself: the pair a caller needs to attach an FFD key to its parent. Public because
    // the clipboard's paste has to re-attach a key it just made, and the tree used to own a
    // second copy of this query in the GUI.
    static std::pair<TimeKey*, LayerMesh*> getAreaMeshImpl(ObjectNode& aNode, const TimeInfo& aTime);
    static BoneKey* getAreaBone(ObjectNode& aNode, const TimeInfo& aTime);
    static BoneKey* getNearestInfluencerBone(ObjectNode& aNode, const TimeInfo& aTime);
    static QVector2D getImageOffset(ObjectNode& aNode, const TimeInfo& aTime);
    static QVector2D getOriginOffset(ObjectNode& aNode, const TimeInfo& aTime);

    // THE influence-map matrix: the node's relative matrix to the owner,
    // translated by its origin offset. The influence build, the bake and the
    // heatmap all read this one function; before it they each assembled the
    // product by hand, so a change in one could silently stop matching the
    // other. The overload takes an inner matrix already computed (BoneKey's
    // cache) so the heatmap can reuse it instead of recomputing the relative
    // matrix.
    static QMatrix4x4 getInfluenceMapMatrix(
        const QMatrix4x4& aInnerMtx, ObjectNode& aNode, const TimeInfo& aTime
    );
    static QMatrix4x4 getInfluenceMapMatrix(
        ObjectNode& aNode, const TimeInfo& aTime, const ObjectNode* aParent
    );
    static QVector2D getCentroid(const ObjectNode& aNode, const TimeInfo& aTime);

    TimeKeyBlender(ObjectTree& aTree);
    TimeKeyBlender(ObjectNode& aRootNode, bool aUseWorking);
    TimeKeyBlender(SeekerType& aSeeker, PositionType aRoot);

    void updateCurrents(ObjectNode* aRootNode, const TimeInfo& aTime);
    void clearCaches(ObjectNode* aRootNode);
    void clearCaches(TimeLineEvent& aEvent);

private:
    static MeshKey* getMeshKey(const ObjectNode& aNode, const TimeInfo& aTime);
    static ImageKey* getImageKey(const ObjectNode& aNode, const TimeInfo& aTime);

    static void getMoveExpans(SRTExpans& aExpans, const ObjectNode& aNode, const TimeInfo& aTime);
    static void getRotateExpans(SRTExpans& aExpans, const ObjectNode& aNode, const TimeInfo& aTime);
    static void getScaleExpans(SRTExpans& aExpans, const ObjectNode& aNode, const TimeInfo& aTime);

    void blendSRTKeys(PositionType aPos, const TimeInfo& aTime);
    void blendDepthKey(PositionType aPos, const TimeInfo& aTime);
    void blendOpacityKey(PositionType aPos, const TimeInfo& aTime);
    void blendHSVKey(PositionType aPos, const TimeInfo& aTime);
    void blendBlurKey(PositionType aPos, const TimeInfo& aTime);
    void blendWaveKey(PositionType aPos, const TimeInfo& aTime);
    void blendBoneKey(PositionType aPos, const TimeInfo& aTime);
    void blendPoseKey(PositionType aPos, const TimeInfo& aTime);
    void blendMeshKey(PositionType aPos, const TimeInfo& aTime);
    void blendFFDKey(PositionType aPos, const TimeInfo& aTime);
    void blendMaskKey(PositionType aPos, const TimeInfo& aTime);
    void blendImageKey(PositionType aPos, const TimeInfo& aTime);
    // void buildPosePalette(ObjectNode& aNode, PosePalette::KeyPairs& aPairs);
    void buildPosePalette(ObjectNode& aNode, PosePalette::KeyPair aPair);
    void setBoneInfluenceMaps(ObjectNode& aNode, const BoneKey* aKey, const TimeInfo& aTime);
    void setBinderBones(ObjectNode& aRootNode);
    void setBindingMatrices(ObjectNode& aNode, bool aAffectedByBinding, bool aUnderOfBinding, QMatrix4x4 aBindingMtx);

    SeekerType* mSeeker;
    SeekerType::Position mRoot;
};

} // namespace core

#endif // CORE_TIMEKEYBLENDER_H
