#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "core/TimeKeyExpans.h"
#include "core/BlurKey.h"
#include "ctrl/TimeLineUtil.h"
#include "gui/prop/prop_KeyAccessor.h"

namespace {

// Centralised, null-tolerant key-data accessor shared by every key type: reads
// the explicit key at aFrame, falls back to the default key, and only then to a
// static empty Data. This keeps all getters consistent (a stale/unkeyed frame
// must never hard-assert) without duplicating the logic per key type.
template<class TKey, core::TimeKeyType TType>
const typename TKey::Data& getKeyData(const core::ObjectNode& aTarget, int aFrame) {
    const core::TimeLine& timeLine = *aTarget.timeLine();
    auto key = timeLine.timeKey(TType, aFrame);
    if (!key) {
        key = timeLine.defaultKey(TType);
    }
    if (key) {
        return static_cast<const TKey*>(key)->data();
    }
    static const typename TKey::Data sEmpty;
    return sEmpty;
}

// Seed a freshly auto-created key from the timeline's live (interpolated) value
// at the edited frame, so editing one channel on an unkeyed frame doesn't snap
// the other channels to zero — the same seeding the "add a key" buttons use.
void seedKey(core::MoveKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->setPos(aCurrent.srt().pos());
    aKey->setCentroid(aCurrent.srt().centroid());
}
void seedKey(core::RotateKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->setRotate(aCurrent.srt().rotate());
}
void seedKey(core::ScaleKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->setScale(aCurrent.srt().scale());
}
void seedKey(core::DepthKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->setDepth(aCurrent.depth());
}
void seedKey(core::OpaKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->data() = aCurrent.opa();
}
void seedKey(core::HSVKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->data() = aCurrent.hsv();
}
void seedKey(core::BlurKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->data().setBlurX(aCurrent.blurX());
    aKey->data().setBlurY(aCurrent.blurY());
    aKey->data().setAngleDeg(aCurrent.angleDeg());
}

// Shared "ensure an explicit key of the given type exists at aFrame before
// writing" helper. If the key is absent it auto-creates one so a stale/unkeyed
// frame can be edited instead of dereferencing a missing key; if present it is a
// no-op. Used uniformly by every key type's writers.
template<class TKey, core::TimeKeyType TType>
void ensureKey(core::Project& aProject, core::ObjectNode& aTarget, int aFrame) {
    core::TimeLine& timeLine = *aTarget.timeLine();
    if (timeLine.hasTimeKey(TType, aFrame)) {
        return;
    }
    auto* newKey = new TKey();
    seedKey(newKey, timeLine.current());
    newKey->setFrame(aFrame);
    {
        cmnd::ScopedMacro macro(aProject.commandStack(), QStringLiteral("Add new key"));
        auto* notifier = new ctrl::TimeLineUtil::Notifier(aProject);
        notifier->event().setType(core::TimeLineEvent::Type_PushKey);
        notifier->event().pushTarget(aTarget, TType, aFrame);
        macro.grabListener(notifier);
        auto& stack = aProject.commandStack();
        stack.push(new cmnd::GrabNewObject<TKey>(newKey));
        stack.push(timeLine.createPusher(TType, aFrame, newKey));
    }
}

} // namespace

namespace gui {
namespace prop {

    KeyAccessor::KeyAccessor(): mProject(), mTarget() {}

    void KeyAccessor::setProject(core::Project* aProject) { mProject = aProject; }

    void KeyAccessor::setTarget(core::ObjectNode* aTarget) { mTarget = aTarget; }

//-------------------------------------------------------------------------------------------------
#define ASSERT_AND_RETURN_INVALID_TARGET() \
    XC_ASSERT(isValid()); \
    if (!isValid()) \
        return;


    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignDefaultDepth(float aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto key = (const core::DepthKey*)currline().defaultKey(core::TimeKeyType_Depth);
        XC_PTR_ASSERT(key);
        core::DepthKey::Data newData = key->data();
        newData.setDepth(aNext);
        ctrl::TimeLineUtil::assignDepthKeyData(*mProject, *mTarget, core::TimeLine::kDefaultKeyIndex, newData);
    }

    void KeyAccessor::assignDefaultOpacity(float aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto key = (const core::OpaKey*)currline().defaultKey(core::TimeKeyType_Opa);
        XC_PTR_ASSERT(key);
        core::OpaKey::Data newData = key->data();
        newData.setOpacity(aNext);
        ctrl::TimeLineUtil::assignOpaKeyData(*mProject, *mTarget, core::TimeLine::kDefaultKeyIndex, newData);
    }

    void KeyAccessor::assignDefaultHSV(QList<int> aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto key = (const core::HSVKey*)currline().defaultKey(core::TimeKeyType_HSV);
        XC_PTR_ASSERT(key);
        core::HSVKey::Data newData = key->data();
        newData.setHSV(aNext);
        ctrl::TimeLineUtil::assignHSVKeyData(*mProject, *mTarget, core::TimeLine::kDefaultKeyIndex, newData);
    }

    void KeyAccessor::assignDefaultImageResource(img::ResourceNode& aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyResource(*mProject, *mTarget, core::TimeLine::kDefaultKeyIndex, aNext);
    }

    void KeyAccessor::assignDefaultImageOffset(const QVector2D& aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyOffset(*mProject, *mTarget, core::TimeLine::kDefaultKeyIndex, aNext);
    }

    void KeyAccessor::assignDefaultImageCellSize(int aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyCellSize(*mProject, *mTarget, core::TimeLine::kDefaultKeyIndex, aNext);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignMoveEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        const int frame = getFrame();
        ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        newData.easing() = aNext;

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignMoveSpline(int aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(0 <= aNext && aNext < core::MoveKey::SplineType_TERM);
        const int frame = getFrame();
        ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        newData.setSpline((core::MoveKey::SplineType)aNext);

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignMovePosition(const QVector2D& aNewPos) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        newData.setPos(aNewPos);

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignMoveCentroid(const QVector2D& aNewCentroid) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        newData.setCentroid(aNewCentroid);

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignRotateEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        const int frame = getFrame();
        ensureKey<core::RotateKey, core::TimeKeyType_Rotate>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::RotateKey, core::TimeKeyType_Rotate>(*mTarget, frame);
        newData.easing() = aNext;

        ctrl::TimeLineUtil::assignRotateKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignRotateAngle(float aAngle) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::RotateKey, core::TimeKeyType_Rotate>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::RotateKey, core::TimeKeyType_Rotate>(*mTarget, frame);
        newData.setRotate(aAngle);

        ctrl::TimeLineUtil::assignRotateKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignScaleEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        const int frame = getFrame();
        ensureKey<core::ScaleKey, core::TimeKeyType_Scale>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::ScaleKey, core::TimeKeyType_Scale>(*mTarget, frame);
        newData.easing() = aNext;
        ctrl::TimeLineUtil::assignScaleKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignScaleRate(const QVector2D& aNewScale) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::ScaleKey, core::TimeKeyType_Scale>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::ScaleKey, core::TimeKeyType_Scale>(*mTarget, frame);
        newData.setScale(aNewScale);

        ctrl::TimeLineUtil::assignScaleKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignDepthEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        const int frame = getFrame();
        ensureKey<core::DepthKey, core::TimeKeyType_Depth>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::DepthKey, core::TimeKeyType_Depth>(*mTarget, frame);
        newData.easing() = aNext;

        ctrl::TimeLineUtil::assignDepthKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignDepthPosition(float aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::DepthKey, core::TimeKeyType_Depth>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::DepthKey, core::TimeKeyType_Depth>(*mTarget, frame);
        newData.setDepth(aNext);

        ctrl::TimeLineUtil::assignDepthKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignOpacity(float aOpacity) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::OpaKey, core::TimeKeyType_Opa>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::OpaKey, core::TimeKeyType_Opa>(*mTarget, frame);
        newData.setOpacity(aOpacity);

        ctrl::TimeLineUtil::assignOpaKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignHSV(int aValue, QString aType) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::HSVKey, core::TimeKeyType_HSV>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::HSVKey, core::TimeKeyType_HSV>(*mTarget, frame);
        if (aType == "hue") {
            newData.setHue(aValue);
        } else if (aType == "sat") {
            newData.setSaturation(aValue);
        } else if (aType == "val") {
            newData.setValue(aValue);
        } else if (aType == "abs") {
            newData.setAbsolute(aValue);
        }

        ctrl::TimeLineUtil::assignHSVKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignOpaEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        const int frame = getFrame();
        ensureKey<core::OpaKey, core::TimeKeyType_Opa>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::OpaKey, core::TimeKeyType_Opa>(*mTarget, frame);
        newData.easing() = aNext;

        ctrl::TimeLineUtil::assignOpaKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignHSVEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        const int frame = getFrame();
        ensureKey<core::HSVKey, core::TimeKeyType_HSV>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::HSVKey, core::TimeKeyType_HSV>(*mTarget, frame);
        newData.easing() = aNext;

        ctrl::TimeLineUtil::assignHSVKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignPoseEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        ctrl::TimeLineUtil::assignPoseKeyEasing(*mProject, *mTarget, getFrame(), aNext);
    }

    void KeyAccessor::assignFFDEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        ctrl::TimeLineUtil::assignFFDKeyEasing(*mProject, *mTarget, getFrame(), aNext);
    }

    void KeyAccessor::assignImageResource(img::ResourceNode& aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyResource(*mProject, *mTarget, getFrame(), aNext);
    }

    void KeyAccessor::assignImageOffset(const QVector2D& aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyOffset(*mProject, *mTarget, getFrame(), aNext);
    }

    void KeyAccessor::assignImageCellSize(int aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyCellSize(*mProject, *mTarget, getFrame(), aNext);
    }

    //-------------------------------------------------------------------------------------------------

    template<typename T>
    void assignParam(T* key) {
        QSettings settings;
        util::Easing::Param aNext;
        aNext.type = util::Easing::easingToEnum(QString());
        aNext.range = util::Easing::rangeToEnum(QString());
        key->data().easing() = aNext;
    }

    //-------------------------------------------------------------------------------------------------

    void KeyAccessor::knockNewMove() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::MoveKey();
        newKey->setPos(currline().current().srt().pos());
        newKey->setCentroid(currline().current().srt().centroid());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewMoveKey(*mProject, *mTarget, getFrame(), newKey);
    }
    void KeyAccessor::knockNewRotate() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::RotateKey();
        newKey->setRotate(currline().current().srt().rotate());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewRotateKey(*mProject, *mTarget, getFrame(), newKey);
    }
    void KeyAccessor::knockNewScale() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::ScaleKey();
        newKey->setScale(currline().current().srt().scale());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewScaleKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewDepth() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::DepthKey();
        newKey->setDepth(currline().current().depth());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewDepthKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewOpacity() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::OpaKey();
        newKey->data() = currline().current().opa();
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewOpaKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewHSV() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::HSVKey();
        newKey->data() = currline().current().hsv();
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewHSVKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::assignBlur(float aAmount) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::BlurKey, core::TimeKeyType_Blur>(*mTarget, frame);
        newData.setAmount(aAmount);

        ctrl::TimeLineUtil::assignBlurKeyData(*mProject, *mTarget, frame, newData);
    }

    core::BlurKey::Data KeyAccessor::blurData() const {
        if (!isValid()) {
            return core::BlurKey::Data();
        }
        const int frame = getFrame();
        const core::TimeLine& timeLine = *mTarget->timeLine();
        if (!timeLine.hasTimeKey(core::TimeKeyType_Blur, frame)) {
            // No explicit blur key at this frame: report the live (blended)
            // value so editing one axis doesn't reset the others. The blended
            // value is the panel's "current" read when between keys.
            core::BlurKey::Data data;
            const core::TimeKeyExpans& current = timeLine.current();
            data.setBlurX(current.blurX());
            data.setBlurY(current.blurY());
            data.setAngleDeg(current.angleDeg());
            return data;
        }
        return getKeyData<core::BlurKey, core::TimeKeyType_Blur>(*mTarget, frame);
    }

    void KeyAccessor::assignBlurEllipse(float aBlurX, float aBlurY, float aAngleDeg) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::BlurKey, core::TimeKeyType_Blur>(*mTarget, frame);
        newData.setBlurX(aBlurX);
        newData.setBlurY(aBlurY);
        newData.setAngleDeg(aAngleDeg);

        ctrl::TimeLineUtil::assignBlurKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignBlurDirectional(bool aIsDirectional) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();
        ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::BlurKey, core::TimeKeyType_Blur>(*mTarget, frame);
        newData.setDirectional(aIsDirectional);
        // opening the directional mode keeps the current size as the initial X/Y radius
        if (aIsDirectional) {
            newData.setBlurY(newData.blurX());
        }

        ctrl::TimeLineUtil::assignBlurKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignBlurEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());
        const int frame = getFrame();
        ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        auto newData = getKeyData<core::BlurKey, core::TimeKeyType_Blur>(*mTarget, frame);
        newData.easing() = aNext;

        ctrl::TimeLineUtil::assignBlurKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::knockNewBlur() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::BlurKey();
        newKey->setBlurX(currline().current().blurX());
        newKey->setBlurY(currline().current().blurY());
        newKey->setAngleDeg(currline().current().angleDeg());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewBlurKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewPose() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::PoseKey();
        core::BoneKey* parentKey = currline().current().bone().areaKey();
        XC_PTR_ASSERT(parentKey);
        if (parentKey == currline().current().poseParent()) {
            newKey->data() = currline().current().pose();
        } else {
            newKey->data().createBonesBy(*parentKey);
        }
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewPoseKey(*mProject, *mTarget, getFrame(), newKey, parentKey);
    }

    void KeyAccessor::knockNewFFD() {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::FFDKey();
        newKey->data() = currline().current().ffd();
        core::TimeKey* parentKey = currline().current().ffdMeshParent();
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewFFDKey(*mProject, *mTarget, getFrame(), newKey, parentKey);
    }

    void KeyAccessor::knockNewImage(const img::ResourceHandle& aHandle) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto newKey = new core::ImageKey();
        newKey->setImage(aHandle);
        newKey->resetGridMesh();
        newKey->setImageOffsetByCenter();
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewImageKey(*mProject, *mTarget, getFrame(), newKey);
    }

} // namespace prop
} // namespace gui
