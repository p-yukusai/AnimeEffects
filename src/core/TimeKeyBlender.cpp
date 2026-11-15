    #include "util/TreeIterator.h"
#include "util/TreeSeekIterator.h"
#include "util/MathUtil.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeKeyBlender.h"
#include "core/AutoEasing.h"
#include "core/LayerMesh.h"
#include "core/ObjectNodeUtil.h"
#include "core/DepthKey.h"
#include "core/MoveEvaluator.h"
#include "core/HSVStateKey.h"
#include "core/WaveStateKey.h"
#include "core/BlurKey.h"
#include "gl/PagePool.h"

namespace core {

//-------------------------------------------------------------------------------------------------
class ObjectTreeSeeker: public util::ITreeSeeker<TimeKeyBlender::SeekData, ObjectNode*> {
    bool mUseCache;

public:
    explicit ObjectTreeSeeker(bool aUseCache): mUseCache(aUseCache) {}

    virtual Position position(ObjectNode* aNode) const { return aNode; }

    virtual Data data(Position aPos) const {
        TimeKeyBlender::SeekData data = {};
        ObjectNode* node = static_cast<ObjectNode*>(aPos);
        if (node && node->timeLine()) {
            data.objNode = node;
            data.expans = mUseCache ? &(node->timeLine()->working()) : &(node->timeLine()->current());
            return data;
        }
        return data;
    }

    virtual Position parent(Position aPos) const {
        if (!aPos)
            return nullptr;
        return static_cast<ObjectNode*>(aPos)->parent();
    }

    virtual Position child(Position aPos) const {
        ObjectNode* node = static_cast<ObjectNode*>(aPos);
        if (!node || node->children().empty())
            return nullptr;
        return node->children().front();
    }

    virtual Position prevSib(Position aPos) const {
        ObjectNode* node = static_cast<ObjectNode*>(aPos);
        if (!node)
            return nullptr;
        return node->prevSib();
    }

    virtual Position nextSib(Position aPos) const {
        ObjectNode* node = static_cast<ObjectNode*>(aPos);
        if (!node)
            return nullptr;
        return node->nextSib();
    }
};

} // namespace core


namespace core {
//-------------------------------------------------------------------------------------------------
QMatrix4x4 TimeKeyBlender::getLocalSRMatrix(const ObjectNode& aNode, const TimeInfo& aTime) {
    XC_ASSERT(aTime.frame.get() != TimeLine::kBaseValueIndex);
    if (aNode.timeLine()) {
        SRTExpans expans;
        getRotateExpans(expans, aNode, aTime);
        getScaleExpans(expans, aNode, aTime);
        return expans.localSRMatrix();
    }
    return QMatrix4x4();
}

//-------------------------------------------------------------------------------------------------
QMatrix4x4 TimeKeyBlender::getWorldCSRTMatrix(ObjectNode& aNode, const TimeInfo& aTime) {
    XC_ASSERT(aTime.frame.get() != TimeLine::kBaseValueIndex);
    if (aNode.timeLine()) {
        ObjectTreeSeeker seeker(true);
        TimeKeyBlender blender(seeker, seeker.position(&aNode));
        blender.blendSRTKeys(seeker.position(&aNode), aTime);
        return aNode.timeLine()->working().srt().worldCSRTMatrix();
    }
    return QMatrix4x4();
}

QMatrix4x4 TimeKeyBlender::getRelativeMatrix(ObjectNode& aNode, const TimeInfo& aTime, const ObjectNode* aParent) {
    XC_ASSERT(aTime.frame.get() != TimeLine::kBaseValueIndex);
    QMatrix4x4 result;

    if (aNode.timeLine()) {
        ObjectTreeSeeker seeker(true);
        TimeKeyBlender blender(seeker, seeker.position(&aNode));
        blender.blendSRTKeys(seeker.position(&aNode), aTime);

        // calculate matrix
        for (auto curr = &aNode; curr && curr != aParent; curr = curr->parent()) {
            auto currLine = curr->timeLine();
            if (currLine) {
                result = currLine->working().srt().localCSRTMatrix() * result;
            }
        }
    }
    return result;
}

LayerMesh* TimeKeyBlender::getAreaMesh(ObjectNode& aNode, const TimeInfo& aTime) {
    return getAreaMeshImpl(aNode, aTime).second;
}

std::pair<TimeKey*, LayerMesh*> TimeKeyBlender::getAreaMeshImpl(ObjectNode& aNode, const TimeInfo& aTime) {
    if (aNode.timeLine()) {
        // find parent key
        ImageKey* imageKey = getImageKey(aNode, aTime);
        MeshKey* meshKey = getMeshKey(aNode, aTime);

        if (imageKey && (!meshKey || imageKey->frame() > meshKey->frame())) {
            return std::pair<TimeKey*, LayerMesh*>(imageKey, &(imageKey->data().gridMesh()));
        } else if (meshKey) {
            return std::pair<TimeKey*, LayerMesh*>(meshKey, &(meshKey->data()));
        }
    }
    return std::pair<TimeKey*, LayerMesh*>(nullptr, nullptr);
}

BoneKey* TimeKeyBlender::getAreaBone(ObjectNode& aNode, const TimeInfo& aTime) {
    if (aNode.timeLine()) {
        auto& map = aNode.timeLine()->map(TimeKeyType_Bone);
        TimeKey* key = TimeKeyGatherer::findLastKey(map, aTime.frame);
        if (key) {
            TIMEKEY_PTR_TYPE_ASSERT(key, Bone);
            return (BoneKey*)key;
        }
    }
    return nullptr;
}

BoneKey* TimeKeyBlender::getNearestInfluencerBone(ObjectNode& aNode, const TimeInfo& aTime) {
    for (ObjectNode* node = &aNode; node; node = node->parent()) {
        auto areaBone = getAreaBone(aNode, aTime);
        if (areaBone)
            return areaBone;
    }
    return nullptr;
}

QVector2D TimeKeyBlender::getImageOffset(ObjectNode& aNode, const TimeInfo& aTime) {
    auto imageKey = getImageKey(aNode, aTime);
    return imageKey ? imageKey->data().imageOffset() : QVector2D();
}

QMatrix4x4 TimeKeyBlender::getInfluenceMapMatrix(
    const QMatrix4x4& aInnerMtx, ObjectNode& aNode, const TimeInfo& aTime
) {
    QMatrix4x4 mtx = aInnerMtx;
    const QVector2D offset = getOriginOffset(aNode, aTime);
    mtx.translate(offset.x(), offset.y());
    return mtx;
}

QMatrix4x4 TimeKeyBlender::getInfluenceMapMatrix(
    ObjectNode& aNode, const TimeInfo& aTime, const ObjectNode* aParent
) {
    return getInfluenceMapMatrix(getRelativeMatrix(aNode, aTime, aParent), aNode, aTime);
}

QVector2D TimeKeyBlender::getOriginOffset(ObjectNode& aNode, const TimeInfo& aTime) {
    auto areaMesh = getAreaMeshImpl(aNode, aTime);
    if (areaMesh.second) {
        return areaMesh.second->originOffset();
    }
    return QVector2D();
}

QVector2D TimeKeyBlender::getCentroid(const ObjectNode& aNode, const TimeInfo& aTime) {
    XC_ASSERT(aTime.frame.get() != TimeLine::kBaseValueIndex);
    if (aNode.timeLine()) {
        SRTExpans expans;
        getMoveExpans(expans, aNode, aTime);
        return expans.centroid();
    }
    return QVector2D();
}

//-------------------------------------------------------------------------------------------------
TimeKeyBlender::TimeKeyBlender(ObjectTree& aTree): mSeeker(), mRoot() {
    static ObjectTreeSeeker sSeeker(false);
    mSeeker = &sSeeker;
    mRoot = sSeeker.position(aTree.topNode());
}

TimeKeyBlender::TimeKeyBlender(ObjectNode& aRootNode, bool aUseWorking): mSeeker(), mRoot() {
    if (aUseWorking) {
        static ObjectTreeSeeker sSeeker(true);
        mSeeker = &sSeeker;
        mRoot = sSeeker.position(&aRootNode);
    } else {
        static ObjectTreeSeeker sSeeker(false);
        mSeeker = &sSeeker;
        mRoot = sSeeker.position(&aRootNode);
    }
}

TimeKeyBlender::TimeKeyBlender(SeekerType& aSeeker, PositionType aRoot): mSeeker(&aSeeker), mRoot(aRoot) {}

void TimeKeyBlender::updateCurrents(ObjectNode* aRootNode, const TimeInfo& aTime) {
    {
        util::TreeSeekIterator<SeekData, ObjectNode*> itr(*mSeeker, mRoot);

        while (itr.hasNext()) {
            auto pos = itr.next();
            XC_ASSERT(pos);

            // build move, rotate and scale
            blendSRTKeys(pos, aTime);

            // build depth
            blendDepthKey(pos, aTime);

            // build the opacity expansion
            blendOpacityKey(pos, aTime);

            // build hsv
            blendHSVKey(pos, aTime);

            // build blur
            blendBlurKey(pos, aTime);

            // build wave deform
            blendWaveKey(pos, aTime);

            // build mesh
            blendMeshKey(pos, aTime);

            // build image
            blendImageKey(pos, aTime);

            // build bone
            blendBoneKey(pos, aTime);

            // build pose
            blendPoseKey(pos, aTime);

            // build ffd
            blendFFDKey(pos, aTime);

            // build mask
            blendMaskKey(pos, aTime);
        }
    }

    // build skeletal animation matrix
    if (aRootNode) {
        // palette
#if 0
        PosePalette::KeyPairs pairs;
        buildPosePalette(*aRootNode, pairs);
#else
        PosePalette::KeyPair pair = {};
        buildPosePalette(*aRootNode, pair);
#endif
        // set map
        setBoneInfluenceMaps(*aRootNode, nullptr, aTime);
        // set binder
        setBinderBones(*aRootNode);
    }

    // set master cache
    {
        util::TreeSeekIterator<SeekData, ObjectNode*> itr(*mSeeker, mRoot);

        while (itr.hasNext()) {
            auto pos = itr.next();
            XC_ASSERT(pos);
            auto seekData = mSeeker->data(pos);
            if (!seekData.expans)
                continue;
            seekData.expans->setMasterCache(aTime.frame);
            // The blend above rewrote THIS cache IN PLACE - its storage pointers are
            // stable - so a consumer that caches a derived result (LayerNode's
            // skinned positions) tells the new contents from the old ones by this
            // stamp. It is the stamp of the cache that was written, so a blend of
            // `working` cannot invalidate a result derived from `current`. Bumped
            // once per node per rebuild, after the walk has finished writing.
            seekData.expans->bumpRevision();
        }
    }
}

void TimeKeyBlender::clearCaches(TimeLineEvent& aEvent) {
    for (TimeLineEvent::Target& target : aEvent.targets()) {
        target.pos.line()->current().clearCaches();
        target.pos.line()->working().clearCaches();

        // clear master caches of parents
        XC_PTR_ASSERT(target.node);
        auto parent = target.node->parent();
        while (parent) {
            auto pline = parent->timeLine();
            if (pline) {
                pline->current().clearMasterCache();
                pline->working().clearMasterCache();
            }
            parent = parent->parent();
        }
    }
}

void TimeKeyBlender::clearCaches(ObjectNode* aRootNode) {
    ObjectNode::Iterator itr(aRootNode);
    while (itr.hasNext()) {
        ObjectNode* node = itr.next();
        XC_PTR_ASSERT(node);

        if (node->timeLine()) {
            node->timeLine()->current().clearCaches();
            node->timeLine()->working().clearCaches();
        }
    }

    // clear master caches of parents
    if (aRootNode) {
        ObjectNode* parent = aRootNode->parent();
        while (parent) {
            auto pline = parent->timeLine();
            if (pline) {
                pline->current().clearMasterCache();
                pline->working().clearMasterCache();
            }
            parent = parent->parent();
        }
    }
}

template<class tKey>
float getEasingRateFromTwoKeys(const TimeKeyGatherer& aGatherer, int aComponent) {
    // blend keys
    auto p0 = aGatherer.point(0);
    auto p1 = aGatherer.point(1);
    XC_ASSERT(p0 && p1);
    const float frame = p1.relativeFrame - p0.relativeFrame;
    // A frame OUTSIDE the project range can make the loop's two neighbours the
    // SAME position: the key at the range's end and the key at its start share a
    // relative frame once the loop span is added. There is no interval to solve;
    // use the earlier key instead of aborting the whole application (the crash a
    // drag that leaves keys past the frame range used to cause).
    if (frame == 0.0f) {
        return 0.0f;
    }

    const tKey* k0 = static_cast<const tKey*>(p0.key);
    const tKey* k1 = static_cast<const tKey*>(p1.key);

    // The component's OWN param: dragging one component's handle must not warp
    // another's curve, so every component reads the rate of its own index.
    const util::Easing::Param& easing = k0->data().easing(aComponent);

    // A Custom curve's two control points are derived per side: handleOut from
    // the FROM key's param, handleIn from the TO key's (the arriving handle
    // belongs to the TO key even though its stored value lives in the FROM
    // key's param). The neighbours the automatic sides read are the gatherer's
    // point(-1)/point(2), loop-aware and null at either end of the key list, so
    // deriving them here needs no new plumbing. Both sides use ONE context, so
    // playback and the graph draw the same pair.
    if (easing.type == util::Easing::Type_Custom) {
        const util::Easing::HandleMode inMode = k1->data().easing(aComponent).handleIn;
        if (util::Easing::isDerived(easing.handleOut) || util::Easing::isDerived(inMode)) {
            const auto prev = aGatherer.point(-1);
            const auto next = aGatherer.point(2);
            auto ctx = core::autoEasingContext(
                *k0, *k1, aComponent, prev ? prev.key : nullptr, next ? next.key : nullptr);
            return util::Easing::calculate(easing, ctx, -p0.relativeFrame, 0.0f, 1.0f, frame);
        }
    }

    // calculate easing
    return util::Easing::calculate(easing, -p0.relativeFrame, 0.0f, 1.0f, frame);
}

template<class tKey, TimeKeyType tType>
typename tKey::Data getBaseValueData(const ObjectNode& aNode) {
    if (aNode.timeLine()) {
        auto key = (tKey*)aNode.timeLine()->baseValue(tType);
        if (key)
            return key->data();
    }
    return typename tKey::Data();
}

void TimeKeyBlender::getMoveExpans(SRTExpans& aExpans, const ObjectNode& aNode, const TimeInfo& aTime) {
    XC_ASSERT(aNode.timeLine());
    if (TimeLine::usesChannelTracks(TimeKeyType_Move)) {
        const PropertyTrack* prop = aNode.timeLine()->property(TimeKeyType_Move);
        if (prop) {
            aExpans.setPos(MoveEvaluator::samplePosition(*prop, aTime));
            aExpans.setCentroid(MoveEvaluator::sampleCentroid(*prop, aTime));
        }
        return;
    }

    TimeKeyGatherer blend(aNode.timeLine()->map(TimeKeyType_Move), aTime);

    if (blend.isEmpty()) { // no key is exists
        auto keyData = getBaseValueData<MoveKey, TimeKeyType_Move>(aNode);
        aExpans.setPos(keyData.pos());
        aExpans.setCentroid(keyData.centroid());
    } else if (blend.hasSameFrame()) { // a key is exists
        auto& keyData = ((const MoveKey*)blend.point(0).key)->data();
        aExpans.setPos(keyData.pos());
        aExpans.setCentroid(keyData.centroid());
    } else if (blend.isSingle()) { // perfect following
        auto& keyData = ((const MoveKey*)blend.singlePoint().key)->data();
        aExpans.setPos(keyData.pos());
        aExpans.setCentroid(keyData.centroid());
    } else {
        // blend keys
        auto p0 = blend.point(0);
        auto p1 = blend.point(1);
        const MoveKey* k0 = (const MoveKey*)p0.key;
        const MoveKey* k1 = (const MoveKey*)p1.key;
        const MoveKey* kn = (const MoveKey*)blend.point(-1).key;
        const MoveKey* k2 = (const MoveKey*)blend.point(2).key;

        // calculate easing PER COMPONENT: the spline is one spatial path, but
        // each axis is warped by its own curve, so x is read at x's rate and y
        // at y's. The centroid has no graph channel of its own, so it follows
        // component 0 (the position's x curve), exactly as it followed the one
        // shared rate before.
        const float timeX = getEasingRateFromTwoKeys<MoveKey>(blend, 0);
        const float timeY = getEasingRateFromTwoKeys<MoveKey>(blend, 1);

        // use spline cache
        if (aExpans.hasSplineCache(aTime.frame)) {
            const auto& spline = aExpans.spline();
            aExpans.setPos(QVector2D(spline.getByLinear(timeX).x(), spline.getByLinear(timeY).y()));
        } else {
            // linear blending
            auto vels = MoveKey::getCatmullRomVels(kn, k0, k1, k2);
            aExpans.spline().set(k0->pos().toVector3D(), k1->pos().toVector3D(), vels[0].toVector3D(), vels[1].toVector3D());
            aExpans.setSplineCache(util::Range(p0.frame, p1.frame));

            const auto& spline = aExpans.spline();
            aExpans.setPos(QVector2D(spline.getByLinear(timeX).x(), spline.getByLinear(timeY).y()));
        }

        aExpans.setCentroid(k0->centroid() * (1.0f - timeX) + k1->centroid() * timeX);
    }
}

void TimeKeyBlender::getRotateExpans(SRTExpans& aExpans, const ObjectNode& aNode, const TimeInfo& aTime) {
    XC_ASSERT(aNode.timeLine());

    if (TimeLine::usesChannelTracks(TimeKeyType_Rotate)) {
        const ChannelTrack* track = aNode.timeLine()->channelTrack(TimeKeyType_Rotate, 0);
        aExpans.setRotate(track ? track->sample(aTime) : 0.0f);
        return;
    }

    TimeKeyGatherer blend(aNode.timeLine()->map(TimeKeyType_Rotate), aTime);
    if (blend.isEmpty()) {
        // No key exists
        aExpans.setRotate(getBaseValueData<RotateKey, TimeKeyType_Rotate>(aNode).rotate());
    } else if (blend.hasSameFrame()) {
        // Inserted point from easing
        aExpans.setRotate(((const RotateKey*)blend.point(0).key)->data().rotate());
        // Single point
    } else if (blend.isSingle()) { // perfect following
        aExpans.setRotate(((const RotateKey*)blend.singlePoint().key)->data().rotate());
    } else {
        // Multiple points
        const RotateKey* k0 = (const RotateKey*)blend.point(0).key;
        const RotateKey* k1 = (const RotateKey*)blend.point(1).key;
        // calculate easing (one component: Rotate has no per-component work)
        const float time = getEasingRateFromTwoKeys<RotateKey>(blend, 0);
        aExpans.setRotate(k0->rotate() * (1.0f - time) + k1->rotate() * time);
    }
}

void TimeKeyBlender::getScaleExpans(SRTExpans& aExpans, const ObjectNode& aNode, const TimeInfo& aTime) {
    XC_ASSERT(aNode.timeLine());
    if (TimeLine::usesChannelTracks(TimeKeyType_Scale)) {
        const ChannelTrack* x = aNode.timeLine()->channelTrack(TimeKeyType_Scale, 0);
        const ChannelTrack* y = aNode.timeLine()->channelTrack(TimeKeyType_Scale, 1);
        aExpans.setScale(QVector2D(x ? x->sample(aTime) : 1.0f, y ? y->sample(aTime) : 1.0f));
        return;
    }

    TimeKeyGatherer blend(aNode.timeLine()->map(TimeKeyType_Scale), aTime);

    if (blend.isEmpty()) { // no key is exists
        aExpans.setScale(getBaseValueData<ScaleKey, TimeKeyType_Scale>(aNode).scale());
    } else if (blend.hasSameFrame()) { // a key is exists
        aExpans.setScale(((const ScaleKey*)blend.point(0).key)->data().scale());
    } else if (blend.isSingle()) { // perfect following
        aExpans.setScale(((const ScaleKey*)blend.singlePoint().key)->data().scale());
    } else {
        const ScaleKey* k0 = (const ScaleKey*)blend.point(0).key;
        const ScaleKey* k1 = (const ScaleKey*)blend.point(1).key;
        // calculate easing per component: scale x and y carry their own curves
        const float timeX = getEasingRateFromTwoKeys<ScaleKey>(blend, 0);
        const float timeY = getEasingRateFromTwoKeys<ScaleKey>(blend, 1);
        aExpans.setScale(QVector2D(
            k0->scale().x() * (1.0f - timeX) + k1->scale().x() * timeX,
            k0->scale().y() * (1.0f - timeY) + k1->scale().y() * timeY
        ));
    }
}

void TimeKeyBlender::blendSRTKeys(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;
    auto frame = aTime.frame;

    expans.setKeyCache(TimeKeyType_Move, frame);
    getMoveExpans(expans.srt(), node, aTime);

    expans.setKeyCache(TimeKeyType_Rotate, frame);
    getRotateExpans(expans.srt(), node, aTime);

    expans.setKeyCache(TimeKeyType_Scale, frame);
    getScaleExpans(expans.srt(), node, aTime);

    // update matrix
    expans.srt().setParentMatrix(QMatrix4x4());
    {
        auto ppos = mSeeker->parent(aPos);
        while (ppos) {
            auto pdata = mSeeker->data(ppos);
            if (!pdata.expans) {
                ppos = mSeeker->parent(ppos);
                continue;
            }

            auto hasMoveCache = pdata.expans->hasKeyCache(TimeKeyType_Move, frame);
            auto hasRotateCache = pdata.expans->hasKeyCache(TimeKeyType_Rotate, frame);
            auto hasScaleCache = pdata.expans->hasKeyCache(TimeKeyType_Scale, frame);

            if (!hasMoveCache || !hasRotateCache || !hasScaleCache) {
                blendSRTKeys(ppos, aTime);
            }

            expans.srt().setParentMatrix(pdata.expans->srt().worldCSRTMatrix());
            break;
        }
    }
}

void TimeKeyBlender::blendDepthKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    // set cache frame
    expans.setKeyCache(TimeKeyType_Depth, aTime.frame);

    if (TimeLine::usesChannelTracks(TimeKeyType_Depth)) {
        const ChannelTrack* track = node.timeLine()->channelTrack(TimeKeyType_Depth, 0);
        expans.setDepth(track ? track->sample(aTime) : DepthKey::Data().depth());
    } else {
        // get blend info
        TimeKeyGatherer blend(node.timeLine()->map(TimeKeyType_Depth), aTime);

        // no key is exists
        if (blend.isEmpty()) {
            auto baseValue = (DepthKey*)node.timeLine()->baseValue(TimeKeyType_Depth);
            expans.setDepth(baseValue ? baseValue->depth() : DepthKey::Data().depth());
        } else if (blend.hasSameFrame()) {
            // a key is exists
            expans.setDepth(((const DepthKey*)blend.point(0).key)->depth());
        } else if (blend.isSingle()) {
            // perfect following
            expans.setDepth(((const DepthKey*)blend.singlePoint().key)->depth());
        } else {
            const DepthKey* k0 = (const DepthKey*)blend.point(0).key;
            const DepthKey* k1 = (const DepthKey*)blend.point(1).key;
            // calculate easing
            const float time = getEasingRateFromTwoKeys<DepthKey>(blend, 0);
            // blend
            expans.setDepth(k0->depth() * (1.0f - time) + k1->depth() * time);
        }
    }

    // sum depths of parents
    {
        expans.setWorldDepth(expans.depth());
        for (auto ppos = mSeeker->parent(aPos); ppos; ppos = mSeeker->parent(ppos)) {
            auto pdata = mSeeker->data(ppos);
            if (!pdata.expans)
                continue;
            // check cache
            if (!pdata.expans->hasKeyCache(TimeKeyType_Depth, aTime.frame)) {
                blendDepthKey(ppos, aTime);
            }
            // world depth
            expans.setWorldDepth(pdata.expans->worldDepth() + expans.depth());
            break;
        }
    }
}

void TimeKeyBlender::blendHSVKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    // set cache frame
    expans.setKeyCache(TimeKeyType_HSV, aTime.frame);

    if (TimeLine::usesChannelTracks(TimeKeyType_HSV)) {
        const ChannelTrack* hue = node.timeLine()->channelTrack(TimeKeyType_HSV, 0);
        const ChannelTrack* sat = node.timeLine()->channelTrack(TimeKeyType_HSV, 1);
        const ChannelTrack* val = node.timeLine()->channelTrack(TimeKeyType_HSV, 2);
        expans.hsv().setHue(hue ? static_cast<int>(hue->sample(aTime)) : 0);
        expans.hsv().setSaturation(sat ? static_cast<int>(sat->sample(aTime)) : 100);
        expans.hsv().setValue(val ? static_cast<int>(val->sample(aTime)) : 100);
        const PropertyTrack* prop = node.timeLine()->property(TimeKeyType_HSV);
        const auto* state = prop
            ? static_cast<const HSVStateKey*>(prop->state().sampleState(aTime.frame.get())) : nullptr;
        expans.hsv().setAbsolute(state && state->absolute() ? 1 : 0);
        return;
    }

    // get blend info
    TimeKeyGatherer blend(node.timeLine()->map(TimeKeyType_HSV), aTime);

    // no key is exists
    if (blend.isEmpty()) {
        auto baseValue = (HSVKey*)node.timeLine()->baseValue(TimeKeyType_HSV);
        expans.hsv() = baseValue ? baseValue->data() : HSVKey::Data();
    } else if (blend.hasSameFrame()) {
        // a key is exists
        expans.hsv() = ((const HSVKey*)blend.point(0).key)->data();
    } else if (blend.isSingle()) {
        // perfect following
        expans.hsv() = ((const HSVKey*)blend.singlePoint().key)->data();
    } else {
        // calculate hsv change, one rate per component (hue, saturation, value)
        const float timeHue = getEasingRateFromTwoKeys<HSVKey>(blend, 0);
        const float timeSaturation = getEasingRateFromTwoKeys<HSVKey>(blend, 1);
        const float timeValue = getEasingRateFromTwoKeys<HSVKey>(blend, 2);
        const HSVKey* k0 = (const HSVKey*)blend.point(0).key;
        const HSVKey* k1 = (const HSVKey*)blend.point(1).key;
        expans.hsv().setHue(
            k0->hsv().at(0) * (1.0f - timeHue) + k1->hsv().at(0) * timeHue
        );
        expans.hsv().setSaturation(
            k0->hsv().at(1) * (1.0f - timeSaturation) + k1->hsv().at(1) * timeSaturation
        );
        expans.hsv().setValue(
            k0->hsv().at(2) * (1.0f - timeValue) + k1->hsv().at(2) * timeValue
        );
    }
}


void TimeKeyBlender::blendBlurKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    // set cache frame
    expans.setKeyCache(TimeKeyType_Blur, aTime.frame);

    if (TimeLine::usesChannelTracks(TimeKeyType_Blur)) {
        const ChannelTrack* x = node.timeLine()->channelTrack(TimeKeyType_Blur, 0);
        const ChannelTrack* y = node.timeLine()->channelTrack(TimeKeyType_Blur, 1);
        const ChannelTrack* angle = node.timeLine()->channelTrack(TimeKeyType_Blur, 2);
        expans.setBlurEllipse(
            x ? x->sample(aTime) : 0.0f,
            y ? y->sample(aTime) : 0.0f,
            angle ? angle->sample(aTime) : 0.0f
        );
        return;
    }

    // get blend info
    TimeKeyGatherer blend(node.timeLine()->map(TimeKeyType_Blur), aTime);

    // no key is exists
    if (blend.isEmpty()) {
        auto baseValue = (BlurKey*)node.timeLine()->baseValue(TimeKeyType_Blur);
        if (baseValue) {
            const auto& d = baseValue->data();
            expans.setBlurEllipse(d.blurX(), d.blurY(), d.angleDeg());
        } else {
            expans.setBlurEllipse(0.0f, 0.0f, 0.0f);
        }
    } else if (blend.hasSameFrame()) {
        // a key is exists
        const auto& d = ((const BlurKey*)blend.point(0).key)->data();
        expans.setBlurEllipse(d.blurX(), d.blurY(), d.angleDeg());
    } else if (blend.isSingle()) {
        // perfect following
        const auto& d = ((const BlurKey*)blend.singlePoint().key)->data();
        expans.setBlurEllipse(d.blurX(), d.blurY(), d.angleDeg());
    } else {
        // calculate easing; the angle interpolates linearly like the rotate key, so a
        // multi-turn spin (e.g. 0 -> 3600) keeps its accumulated turns and the easing
        // modulates the spin speed instead of taking the shortest arc. Each of the
        // three components is warped by its own curve.
        const float timeX = getEasingRateFromTwoKeys<BlurKey>(blend, 0);
        const float timeY = getEasingRateFromTwoKeys<BlurKey>(blend, 1);
        const float timeAngle = getEasingRateFromTwoKeys<BlurKey>(blend, 2);
        const BlurKey* k0 = (const BlurKey*)blend.point(0).key;
        const BlurKey* k1 = (const BlurKey*)blend.point(1).key;
        const auto& d0 = k0->data();
        const auto& d1 = k1->data();
        expans.setBlurEllipse(
            d0.blurX() * (1.0f - timeX) + d1.blurX() * timeX,
            d0.blurY() * (1.0f - timeY) + d1.blurY() * timeY,
            d0.angleDeg() * (1.0f - timeAngle) + d1.angleDeg() * timeAngle
        );
    }
}

void TimeKeyBlender::blendWaveKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    // set cache frame
    expans.setKeyCache(TimeKeyType_Wave, aTime.frame);

    if (TimeLine::usesChannelTracks(TimeKeyType_Wave)) {
        WaveKey::Data data;
        for (int c = 0; c < 5; ++c) {
            const ChannelTrack* track = node.timeLine()->channelTrack(TimeKeyType_Wave, c);
            const float value = track ? track->sample(aTime) : 0.0f;
            switch (c) {
            case 0: data.setStrength(value); break;
            case 1: data.setWavelength(value); break;
            case 2: data.setAngleDeg(value); break;
            case 3: data.setPhase(value); break;
            default: data.setMix(value); break;
            }
        }
        const PropertyTrack* prop = node.timeLine()->property(TimeKeyType_Wave);
        if (prop) {
            if (const auto* state = static_cast<const WaveStateKey*>(
                    prop->state().sampleState(aTime.frame.get()))) {
                data.setSpeed(state->speed());
                data.setCrest(state->crest());
                data.setDualAngleDeg(state->dualAngleDeg());
                data.setDualScale(state->dualScale());
                data.setTimeOffset(state->timeOffset());
                data.setDeform(state->deform());
            }
        }
        expans.wave() = data;
        return;
    }

    // get blend info
    TimeKeyGatherer blend(node.timeLine()->map(TimeKeyType_Wave), aTime);

    // no key is exists
    if (blend.isEmpty()) {
        auto baseValue = (WaveKey*)node.timeLine()->baseValue(TimeKeyType_Wave);
        expans.wave() = baseValue ? baseValue->data() : WaveKey::Data();
    } else if (blend.hasSameFrame()) {
        // a key is exists
        expans.wave() = ((const WaveKey*)blend.point(0).key)->data();
    } else if (blend.isSingle()) {
        // perfect following
        expans.wave() = ((const WaveKey*)blend.singlePoint().key)->data();
    } else {
        // The five graph channels each follow their own curve; the shape
        // decisions (speed, crest, the dual wave's angle and scale, the deform
        // mode) are STEP-HELD from the earlier key - they are not values that
        // play as a curve, and interpolating `speed` in particular would make
        // the phase (speed * time) jump at every key.
        const WaveKey* k0 = (const WaveKey*)blend.point(0).key;
        const WaveKey* k1 = (const WaveKey*)blend.point(1).key;
        const auto& d0 = k0->data();
        const auto& d1 = k1->data();
        WaveKey::Data data = d0;

        const float timeStrength = getEasingRateFromTwoKeys<WaveKey>(blend, 0);
        const float timeWavelength = getEasingRateFromTwoKeys<WaveKey>(blend, 1);
        const float timeAngle = getEasingRateFromTwoKeys<WaveKey>(blend, 2);
        const float timePhase = getEasingRateFromTwoKeys<WaveKey>(blend, 3);
        const float timeMix = getEasingRateFromTwoKeys<WaveKey>(blend, 4);
        const auto lerp = [](float a0, float a1, float aRate) { return a0 * (1.0f - aRate) + a1 * aRate; };

        data.setStrength(lerp(d0.strength(), d1.strength(), timeStrength));
        data.setWavelength(lerp(d0.wavelength(), d1.wavelength(), timeWavelength));
        // The angle interpolates linearly like rotate/blur, so an accumulated
        // multi-turn spin keeps its turns instead of taking the shortest arc.
        data.setAngleDeg(lerp(d0.angleDeg(), d1.angleDeg(), timeAngle));
        data.setPhase(lerp(d0.phase(), d1.phase(), timePhase));
        data.setMix(lerp(d0.mix(), d1.mix(), timeMix));
        expans.wave() = data;
    }
}

void TimeKeyBlender::blendOpacityKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    // set cache frame
    expans.setKeyCache(TimeKeyType_Opacity, aTime.frame);

    if (TimeLine::usesChannelTracks(TimeKeyType_Opacity)) {
        const ChannelTrack* track = node.timeLine()->channelTrack(TimeKeyType_Opacity, 0);
        expans.opacity().setOpacity(track ? track->sample(aTime) : 1.0f);
        return;
    }

    // get blend info
    TimeKeyGatherer blend(node.timeLine()->map(TimeKeyType_Opacity), aTime);

    // no key is exists
    if (blend.isEmpty()) {
        auto baseValue = (OpacityKey*)node.timeLine()->baseValue(TimeKeyType_Opacity);
        expans.opacity() = baseValue ? baseValue->data() : OpacityKey::Data();
    } else if (blend.hasSameFrame()) {
        // a key is exists
        expans.opacity() = ((const OpacityKey*)blend.point(0).key)->data();
    } else if (blend.isSingle()) {
        // perfect following
        expans.opacity() = ((const OpacityKey*)blend.singlePoint().key)->data();
    } else {
        const OpacityKey* k0 = (const OpacityKey*)blend.point(0).key;
        const OpacityKey* k1 = (const OpacityKey*)blend.point(1).key;
        // calculate easing
        const float time = getEasingRateFromTwoKeys<OpacityKey>(blend, 0);
        // blend
        expans.opacity().setOpacity(k0->opacity() * (1.0f - time) + k1->opacity() * time);
    }

    // multiply opacity of parents
    {
        expans.setWorldOpacity(expans.opacity().opacity());
        for (auto ppos = mSeeker->parent(aPos); ppos; ppos = mSeeker->parent(ppos)) {
            // find a parent
            auto pdata = mSeeker->data(ppos);
            if (!pdata.expans)
                continue;
            // check cache
            if (!pdata.expans->hasKeyCache(TimeKeyType_Opacity, aTime.frame)) {
                blendOpacityKey(ppos, aTime);
            }
            // world opacity
            expans.setWorldOpacity(pdata.expans->worldOpacity() * expans.opacity().opacity());
            break;
        }
    }
}

void TimeKeyBlender::blendBoneKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    expans.setKeyCache(TimeKeyType_Bone, aTime.frame);
    auto areaBone = getAreaBone(node, aTime);

    // check bone validity
    if (areaBone) {
        XC_ASSERT(expans.hasKeyCache(TimeKeyType_Mesh, aTime.frame));
        XC_ASSERT(expans.hasKeyCache(TimeKeyType_Image, aTime.frame));

        auto boneFrame = areaBone->frame();
        auto meshFrame = expans.areaMeshKey() ? expans.areaMeshKey()->frame() : -1;
        auto imageFrame = expans.areaImageKey() ? expans.areaImageKey()->frame() : -1;

        if (boneFrame < meshFrame || boneFrame < imageFrame) {
            areaBone = nullptr;
        }
    }

    expans.bone().setAreaKey(areaBone);
}

void TimeKeyBlender::blendPoseKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    // area bone
    XC_ASSERT(expans.hasKeyCache(TimeKeyType_Bone, aTime.frame));
    auto areaBoneKey = expans.bone().areaKey();
    expans.setPoseParent(areaBoneKey);

    // get blend info
    TimeKeyGatherer blend(
        node.timeLine()->map(TimeKeyType_Pose), aTime, TimeKeyGatherer::ForceType_AssignedParent, areaBoneKey
    );

    // no key is exists
    if (blend.isEmpty()) {
        expans.pose() = PoseKey::Data();
        expans.setPoseParent(nullptr);
    } else if (blend.hasSameFrame()) {
        // a key is exists
        auto poseKey = (const PoseKey*)(blend.point(0).key);
        expans.pose() = poseKey->data();

        TIMEKEY_PTR_TYPE_ASSERT(poseKey->parent(), Bone);
        XC_ASSERT(poseKey->parent() == (TimeKey*)areaBoneKey);
    } else if (blend.isSingle()) {
        // perfect following
        auto poseKey = (const PoseKey*)(blend.singlePoint().key);
        expans.pose() = poseKey->data();

        TIMEKEY_PTR_TYPE_ASSERT(poseKey->parent(), Bone);
        XC_ASSERT(poseKey->parent() == (TimeKey*)areaBoneKey);
    } else {
        // blend keys
        auto p0 = blend.point(0);
        auto p1 = blend.point(1);
        auto key0 = (const PoseKey*)p0.key;
        auto key1 = (const PoseKey*)p1.key;
        XC_PTR_ASSERT(key0->parent());
        XC_PTR_ASSERT(key1->parent());
        XC_ASSERT(key0->parent() == key1->parent());
        TIMEKEY_PTR_TYPE_ASSERT(key0->parent(), Bone);
        XC_ASSERT(key0->parent() == (TimeKey*)areaBoneKey);

        // initialize
        expans.pose() = key0->data();

        // calculate easing
        const float time = getEasingRateFromTwoKeys<PoseKey>(blend, 0);

        // blend
        int index = 0;
        for (Bone2* bone : expans.pose().topBones()) {
            const Bone2* bone0 = key0->data().topBones().at(index);
            const Bone2* bone1 = key1->data().topBones().at(index);

            Bone2::Iterator itr(bone);
            Bone2::ConstIterator itr0(bone0);
            Bone2::ConstIterator itr1(bone1);

            while (itr.hasNext()) {
                XC_ASSERT(itr0.hasNext());
                XC_ASSERT(itr1.hasNext());

                const float rotate = itr0.next()->rotate() * (1.0f - time) + itr1.next()->rotate() * time;

                auto target = itr.next();
                target->setRotate(rotate);
                target->updateWorldTransform();
            }
            ++index;
        }
    }
}

void TimeKeyBlender::blendMeshKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    expans.setKeyCache(TimeKeyType_Mesh, aTime.frame);
    expans.setAreaMeshKey(getMeshKey(node, aTime));
}

MeshKey* TimeKeyBlender::getMeshKey(const ObjectNode& aNode, const TimeInfo& aTime) {
    if (!aNode.timeLine())
        return nullptr;
    const TimeLine::MapType& map = aNode.timeLine()->map(TimeKeyType_Mesh);
    return (MeshKey*)TimeKeyGatherer::findLastKey(map, aTime.frame);
}

void TimeKeyBlender::blendFFDKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    // find area key and mesh
    auto areaMeshPair = getAreaMeshImpl(node, aTime);
    TimeKey* areaKey = areaMeshPair.first;
    LayerMesh* areaMesh = areaMeshPair.second;
    expans.setFFDMesh(areaMesh);
    expans.setFFDMeshParent(areaKey);

    // no mesh exists
    if (!areaMesh || areaMesh->vertexCount() == 0) {
        expans.ffd().clear();
        return;
    }

    // get blend info
    TimeKeyGatherer blend(
        node.timeLine()->map(TimeKeyType_FFD), aTime, TimeKeyGatherer::ForceType_AssignedParent, areaKey
    );

    // no key is exists
    if (blend.isEmpty()) {
        expans.ffd().allocAndWrite(areaMesh->positions(), areaMesh->vertexCount());
    } else if (blend.hasSameFrame()) {
        // a key is exists
        XC_ASSERT(blend.point(0).key->parent() == areaKey);
        expans.ffd().allocAndWrite(((const FFDKey*)(blend.point(0).key))->data().positions(), areaMesh->vertexCount());
    } else if (blend.isSingle()) {
        // perfect following
        XC_ASSERT(blend.singlePoint().key->parent() == areaKey);
        expans.ffd().allocAndWrite(
            ((const FFDKey*)(blend.singlePoint().key))->data().positions(), areaMesh->vertexCount()
        );
    } else {
        // blend keys
        auto p0 = blend.point(0);
        auto p1 = blend.point(1);
        auto key0 = (const FFDKey*)p0.key;
        auto key1 = (const FFDKey*)p1.key;
        const gl::Vector3* v0 = key0->data().positions();
        const gl::Vector3* v1 = key1->data().positions();
        const int count = key0->data().count();
        XC_ASSERT(count == key1->data().count());
        XC_ASSERT(key0->parent() == areaKey);
        XC_ASSERT(key1->parent() == areaKey);

        // alloc if needed
        expans.ffd().alloc(count);

        // calculate easing
        expans.ffd().easing(0) = key0->data().easing(0);
        const float time = getEasingRateFromTwoKeys<FFDKey>(blend, 0);

        // linear blend
        gl::Vector3* dst = expans.ffd().positions();
        for (int i = 0; i < count; ++i) {
            dst[i] = v0[i] * (1.0f - time) + v1[i] * time;
        }
    }
}

// A MASK animates by keying SHAPES: each key carries a whole alpha field, and a
// frame between two keys is the per-texel blend of them. That is the same bet
// FFD makes with its vertices, and it is why a mask needs no per-pixel curve:
// the field itself is the animated value, and the easing shapes how it crosses.
void TimeKeyBlender::blendMaskKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    expans.setKeyCache(TimeKeyType_Mask, aTime.frame);

    // NOT CLEARED UP FRONT: a blended matte owns a VIEW whose pool resource must survive the
    // frame (see setBlend), and every branch below either assigns a key (which takes that key's
    // holder), clears, or materializes a new field (alloc, which drops the holder). Clearing here
    // would destroy the view - and its pages - on every frame.
    MaskKey::Data& out = expans.mask();

    TimeKeyGatherer blend(node.timeLine()->map(TimeKeyType_Mask), aTime);

    if (blend.isEmpty()) {
        // no key at all: the base value's field, or no mask
        out.clear();
        auto baseValue = (const MaskKey*)node.timeLine()->baseValue(TimeKeyType_Mask);
        if (baseValue && baseValue->data().isValid())
            out.assign(baseValue->data());
        return;
    }

    if (blend.hasSameFrame()) {
        out.assign(((const MaskKey*)blend.point(0).key)->data());
        return;
    }

    if (blend.isSingle()) {
        // a mask does not extrapolate: outside the keyed span it holds the
        // nearest key's shape, which is what a hold looks like
        out.assign(((const MaskKey*)blend.singlePoint().key)->data());
        return;
    }

    auto key0 = (const MaskKey*)blend.point(0).key;
    auto key1 = (const MaskKey*)blend.point(1).key;
    const MaskKey::Data& d0 = key0->data();
    const MaskKey::Data& d1 = key1->data();

    if (!d0.isValid()) {
        out.assign(d1);
        return;
    }
    if (!d1.isValid()) {
        out.assign(d0);
        return;
    }

    const float time = getEasingRateFromTwoKeys<MaskKey>(blend, 0);

    // The per-frame path never RESAMPLES: two keys on the same texel grid and
    // the same content rect blend texel by texel, and any other pair cannot be
    // blended without a scale per frame, which would cost more than the whole
    // present. Such a pair takes the nearer key and says nothing - the authoring
    // path keeps one grid per node, so this is the "someone re-imported the
    // image at another size" case, not an ordinary one.
    if (d0.texelSize() != d1.texelSize() || d0.contentRect() != d1.contentRect()) {
        out.assign(time < 0.5f ? d0 : d1);
        return;
    }

    const int fixedTime = int(time * 256.0f + 0.5f);   // one rounding, integer lerp
    const bool forceFull = d0.forceFullTexture() || d1.forceFullTexture();

    // TWO NEUTRAL FIELDS HIDE NOTHING, whatever the weight: the neutral rule must hold for an
    // animation too, so this frame has no matte at all (and no resource to keep).
    if (!d0.isPainted() && !d1.isPainted()) {
        out.clear();
        return;
    }

    // THE VIEW: WITH THE POOL ON, THE BLENDED FIELD IS NEVER MATERIALIZED. Every level is a
    // linear filter of the field, so the blended matte is the lerp of the two keys' OWN pyramids
    // at this weight (MaskPyramid::setBlend), and its pool resource keeps its pages across
    // frames: the per-frame work is the always-resident coarse chain plus the pages the view
    // samples, not a fresh field, a fresh chain, a readback and a fresh pool registration. The
    // keys' pyramids are built once each, exactly as a static matte's is.
    if (!forceFull && gl::PagePool::enabled() && out.setBlend(d0, d1, fixedTime)) {
        out.easing(0) = d0.easing(0);
        return;
    }

    // THE MATERIALIZING FORM: the full-resolution control (AE_VIRTUAL_TEXTURE=0 or
    // setForceFullTexture), and the fallback for a pair the view cannot take (two grids, a field
    // that cannot be captured). This is the pre-paging blend, kept exact.
    out.alloc(d0.texelSize(), d0.contentRect(), MaskKey::kNeutral);
    out.easing(0) = d0.easing(0);
    // THE A/B CONTROL RIDES THE VALUE THROUGH A BLEND TOO: `Data::setForceFullTexture` is the
    // mask twin of ImageKey's control and the renderer reads the EXPANSION's copy, so without
    // this an animated matte could not be asked for the full chain at all. Either side forcing
    // is enough - the control only ever costs the full chain.
    out.setForceFullTexture(forceFull);
    // AND SO DOES "THE MATTE HIDES SOMETHING". In the open interval between the keys a texel either
    // key hides is a lerp of 255 and a value below it, i.e. still below 255, so the source flags
    // answer exactly - and this flag is what makes the draw and the demand pass treat a blended
    // matte as a matte at all (a neutral one must stay free, so it cannot simply be assumed).
    out.setPainted(d0.isPainted() || d1.isPainted());

    const int w = d0.texelSize().width();
    const int h = d0.texelSize().height();
    for (int y = 0; y < h; ++y) {
        const uchar* s0 = d0.field().constScanLine(y);
        const uchar* s1 = d1.field().constScanLine(y);
        uchar* dst = out.field().scanLine(y);
        for (int x = 0; x < w; ++x) {
            dst[x] = uchar((int(s0[x]) * (256 - fixedTime) + int(s1[x]) * fixedTime) >> 8);
        }
    }
}

void TimeKeyBlender::blendImageKey(PositionType aPos, const TimeInfo& aTime) {
    auto seekData = mSeeker->data(aPos);
    if (!seekData.objNode || !seekData.expans)
        return;
    auto& node = *seekData.objNode;
    auto& expans = *seekData.expans;
    if (!node.timeLine())
        return;

    expans.setKeyCache(TimeKeyType_Image, aTime.frame);

    auto imageKey = getImageKey(node, aTime);
    expans.setAreaImageKey(imageKey);

    // 0.25: the offset is its own step-held channel pair, sampled here. A legacy
    // file (or a project created before its first offset key) has the base value
    // migrated into the channel track, so this is the one playback read.
    QVector2D offset = imageKey ? imageKey->data().imageOffset() : QVector2D();
    if (const core::PropertyTrack* prop = node.timeLine()->property(TimeKeyType_Image)) {
        const core::ChannelTrack* x = prop->channel(0);
        const core::ChannelTrack* y = prop->channel(1);
        // 0.25 files always carry the offset block, and migrated 0.24 files have
        // keys here. A fixture that inserts an ImageKey directly (the tests do)
        // has neither: fall back to the key's own field so old fixtures still
        // render, and let a nonzero offset base (seedBaseValue) win.
        const bool authored = (x && x->keyCount() > 0) || (y && y->keyCount() > 0)
            || (x && x->baseValue() != 0.0f) || (y && y->baseValue() != 0.0f);
        if (authored) {
            offset.setX(x ? x->sample(aTime) : offset.x());
            offset.setY(y ? y->sample(aTime) : offset.y());
        }
    }
    expans.setImageOffset(offset);
}

ImageKey* TimeKeyBlender::getImageKey(const ObjectNode& aNode, const TimeInfo& aTime) {
    if (!aNode.timeLine())
        return nullptr;
    const TimeLine::MapType& map = aNode.timeLine()->map(TimeKeyType_Image);
    auto imageKey = (ImageKey*)TimeKeyGatherer::findLastKey(map, aTime.frame);
    return imageKey ? imageKey : (ImageKey*)aNode.timeLine()->baseValue(TimeKeyType_Image);
}

void TimeKeyBlender::buildPosePalette(ObjectNode& aNode, PosePalette::KeyPair aPair) {
    if (aNode.timeLine()) {
        XC_PTR_ASSERT(mSeeker->data(&aNode).expans);
        auto& expans = *(mSeeker->data(&aNode).expans);

        if (expans.poseParent()) {
            PosePalette::KeyPair pair = {&expans.poseParent()->data(), &expans.pose()};
            aPair = pair;
        } else if (expans.bone().areaKey()) {
            PosePalette::KeyPair pair = {};
            aPair = pair;
        }

        // build
        if (aPair.origin && aPair.pose) {
            PosePalette::KeyPairs pairs;
            pairs.push_back(aPair);
            expans.posePalette().build(pairs);
        } else {
            expans.posePalette().clear();
        }
    }

    // iterate children
    for (auto child : aNode.children()) {
        XC_PTR_ASSERT(child);
        buildPosePalette(*child, aPair);
    }
}

void TimeKeyBlender::setBoneInfluenceMaps(ObjectNode& aNode, const BoneKey* aKey, const TimeInfo& aTime) {
    const BoneKey* key = aKey;

    if (aNode.timeLine()) {
        XC_PTR_ASSERT(mSeeker->data(&aNode).expans);
        auto& expans = *(mSeeker->data(&aNode).expans);

        auto areaBoneKey = expans.bone().areaKey();
        if (areaBoneKey)
            key = areaBoneKey;

        const LayerMesh* mesh = nullptr;
        BoneInfluenceMap* influence = nullptr;
        QMatrix4x4 outerMtx;
        QMatrix4x4 innerMtx;

        // check bone validity
        if (key) {
            XC_ASSERT(expans.hasKeyCache(TimeKeyType_Mesh, aTime.frame));
            XC_ASSERT(expans.hasKeyCache(TimeKeyType_Image, aTime.frame));

            auto boneFrame = key->frame();
            auto meshFrame = expans.areaMeshKey() ? expans.areaMeshKey()->frame() : -1;
            auto imageFrame = expans.areaImageKey() ? expans.areaImageKey()->frame() : -1;

            if (boneFrame < meshFrame || boneFrame < imageFrame) {
                key = nullptr;
            }
        }

        if (key) {
            auto cache = key->findCache(aNode);
            if (cache) {
                // qDebug() << "infl" << cache->node() << (&aNode) << cache->influence().vertexCount();
                mesh = TimeKeyBlender::getAreaMesh(aNode, aTime);
                if (mesh) {
                    // A cached node may have no area mesh at this frame; leave
                    // influence/innerMtx null so the node renders undeformed.
                    XC_ASSERT(cache->frameSign() == mesh->frameSign());

                    influence = &cache->influence();
                    innerMtx = cache->innerMatrix();
                }
            }

            auto cacheOwner = key->cacheOwner();
            if (cacheOwner) {
                auto ownerExpans = mSeeker->data(cacheOwner).expans;
                if (ownerExpans) {
                    outerMtx = ownerExpans->srt().worldCSRTMatrix();
                }
            }
        }
        expans.bone().setTargetMesh(mesh);
        expans.bone().setInfluenceMap(influence);
        expans.bone().setOuterMatrix(outerMtx);
        expans.bone().setInnerMatrix(innerMtx);
    }

    for (auto child : aNode.children()) {
        setBoneInfluenceMaps(*child, key, aTime);
    }
}

void TimeKeyBlender::setBinderBones(ObjectNode& aRootNode) {
    // reset binding references
    {
        ObjectNode::Iterator itr(&aRootNode);
        while (itr.hasNext()) {
            auto seekData = mSeeker->data(mSeeker->position(itr.next()));
            if (!seekData.expans)
                continue;
            seekData.expans->bone().setBinderIndex(-1);
        }
    }
    // update binding references
    {
        ObjectNode::Iterator itr(&aRootNode);
        while (itr.hasNext()) {
            auto node = itr.next();
            auto seekData = mSeeker->data(mSeeker->position(node));
            if (!seekData.objNode || !seekData.expans)
                continue;

            auto boneKey = seekData.expans->bone().areaKey();
            if (!boneKey)
                continue;

            for (auto bindingCache : boneKey->bindingCaches()) {
                auto bound = mSeeker->data(mSeeker->position(bindingCache.node));
                if (bound.expans) {
                    bound.expans->bone().setBinderIndex(bindingCache.boneIndex);
                    bound.expans->bone().setBindingRoot(node);
                    bound.expans->bone().setBindingMatrix(bindingCache.innerMtx);
                }
            }
        }
    }

    // set binding parameters
    {
        auto root = mSeeker->data(mSeeker->position(&aRootNode));
        if (root.expans) {
            setBindingMatrices(
                aRootNode,
                root.expans->bone().isAffectedByBinding(),
                root.expans->bone().isUnderOfBinding(),
                root.expans->bone().outerMatrix()
            );
        } else {
            setBindingMatrices(aRootNode, false, false, QMatrix4x4());
        }
    }
}

// Very good var names hidefuku, I can totally understand what the fuck is going on at first glance

void TimeKeyBlender::setBindingMatrices(
    ObjectNode& aNode, bool aAffectedByBinding, bool aUnderOfBinding, QMatrix4x4 aBindingMtx
) {
    auto seekData = mSeeker->data(mSeeker->position(&aNode));
    if (seekData.objNode && seekData.expans) {
        auto& expans = *seekData.expans;

        if (aUnderOfBinding) {
            if (aAffectedByBinding) {
                aBindingMtx = aBindingMtx * expans.srt().localCSRTMatrix();
                expans.bone().setOuterMatrix(aBindingMtx);
                QMatrix4x4 innerMtx;
                expans.bone().setInnerMatrix(innerMtx);
            } else {
                expans.bone().setOuterMatrix(aBindingMtx);
            }
        }

        if (expans.bone().isBoundByBone()) {
            // qDebug() << "bound by bone";
            aAffectedByBinding = true;
            aUnderOfBinding = true;
            XC_PTR_ASSERT(expans.bone().bindingRoot());
            auto root = mSeeker->data(mSeeker->position(expans.bone().bindingRoot()));
            if (root.expans) {
                auto boneIndex = expans.bone().binderIndex();
                XC_ASSERT(boneIndex >= 0);
                if (boneIndex < PosePalette::kMaxCount) {
                    // A palette can be CLEARED (no pose key at this frame) while
                    // the binding cache still names its bones; those bones are in
                    // rest, so the transform is identity. The palette is sized to
                    // the used bones now, so this cannot index past its array.
                    const int usedBones = root.expans->posePalette().usedCount();
                    const QMatrix4x4 transform = (boneIndex < usedBones)
                        ? root.expans->posePalette().matrices()[boneIndex]
                        : QMatrix4x4();
                    aBindingMtx = root.expans->bone().outerMatrix() * transform * expans.bone().bindingMatrix();
                    expans.bone().setOuterMatrix(aBindingMtx);
                    expans.bone().setInnerMatrix(QMatrix4x4());
                    // qDebug() << "transform bound by bone";
                }
            }
        }

        expans.bone().setIsUnderOfBinding(aUnderOfBinding);
        expans.bone().setIsAffectedByBinding(aAffectedByBinding);

        if (expans.bone().areaKey()) {
            aAffectedByBinding = false;
            aBindingMtx = expans.bone().outerMatrix();
        }
    }

    // recursive call
    for (auto child : aNode.children()) {
        setBindingMatrices(*child, aAffectedByBinding, aUnderOfBinding, aBindingMtx);
    }
}

} // namespace core
