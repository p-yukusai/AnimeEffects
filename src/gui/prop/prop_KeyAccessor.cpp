#include "cmnd/ScopedMacro.h"
#include "cmnd/BasicCommands.h"
#include "core/AutoEasing.h"
#include "core/ChannelKey.h"
#include "core/ChannelTrack.h"
#include "core/MoveStateKey.h"
#include "core/TimeKeyExpans.h"
#include "core/BlurKey.h"
#include "core/BlurStateKey.h"
#include "core/HSVStateKey.h"
#include "core/WaveStateKey.h"
#include "ctrl/TimeLineUtil.h"
#include "gui/prop/prop_KeyAccessor.h"

namespace {

// Centralised, null-tolerant key-data accessor shared by every key type: reads
// the explicit key at aFrame, falls back to the base value, and only then to a
// static empty Data. This keeps all getters consistent (a stale/unkeyed frame
// must never hard-assert) without duplicating the logic per key type.
template<class TKey, core::TimeKeyType TType>
const typename TKey::Data& getKeyData(const core::ObjectNode& aTarget, int aFrame) {
    const core::TimeLine& timeLine = *aTarget.timeLine();
    auto key = timeLine.timeKey(TType, aFrame);
    if (!key) {
        key = timeLine.baseValue(TType);
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
void seedKey(core::OpacityKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->data() = aCurrent.opacity();
}
void seedKey(core::HSVKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->data() = aCurrent.hsv();
}
void seedKey(core::BlurKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->data().setBlurX(aCurrent.blurX());
    aKey->data().setBlurY(aCurrent.blurY());
    aKey->data().setAngleDeg(aCurrent.angleDeg());
}
void seedKey(core::WaveKey* aKey, const core::TimeKeyExpans& aCurrent) {
    aKey->data() = aCurrent.wave();
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

// The channel-track equivalent of ensureKey: a migrated property's value lives
// in a ChannelKey, not in a typed bundle. The key is seeded from the live value
// so editing one channel never snaps another.
core::ChannelKey* ensureChannelKey(
    core::Project& aProject, core::ObjectNode& aTarget, core::TimeKeyType aType,
    int aComponent, int aFrame, float aSeedValue, bool aLinked = true
) {
    core::TimeLine& line = *aTarget.timeLine();
    core::ChannelTrack* track = line.channelTrack(aType, aComponent);
    if (!track) {
        return nullptr;
    }
    if (core::ChannelKey* existing = track->keyAt(aFrame)) {
        return existing;
    }

    auto* key = new core::ChannelKey(aType, aComponent);
    key->setValue(track->clamp(track->quantize(aSeedValue)));
    key->easing() = util::Easing::defaultParam();
    key->setLinked(aLinked);
    {
        cmnd::ScopedMacro macro(aProject.commandStack(), QStringLiteral("Add new key"));
        auto* notifier = new ctrl::TimeLineUtil::Notifier(aProject);
        notifier->event().setType(core::TimeLineEvent::Type_PushKey);
        notifier->event().pushTarget(
            aTarget, core::TimeKeyPos(line, aType, aFrame, aComponent)
        );
        macro.grabListener(notifier);

        auto& stack = aProject.commandStack();
        stack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
        stack.push(line.createChannelPusher(aType, aComponent, aFrame, key));
    }
    return key;
}

// Model 2: a property with no explicit keys is STATIC, so editing it changes the
// base value rather than silently creating animation. Once any explicit key
// exists the property is animated and edits land on the current frame (creating
// one when needed). The base values for Move/Rotate/Scale/Depth/Opacity/Image are
// created with their nodes, so the static branch always has a target.
template<class TKey, core::TimeKeyType TType>
int editFrame(const core::TimeLine& aTimeLine, int aFrame) {
    return aTimeLine.map(TType).isEmpty() ? core::TimeLine::kBaseValueIndex : aFrame;
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

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Depth)) {
            core::TimeLine* line = &currline();
            core::ChannelTrack* track = line->channelTrack(core::TimeKeyType_Depth, 0);
            if (!track) {
                return;
            }
            const float value = track->clamp(track->quantize(aNext));
            const float previous = track->baseValue();
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update depth"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Depth);
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Delegatable(
                [line, value]() { line->setChannelBaseValue(core::TimeKeyType_Depth, 0, value); },
                [line, previous]() { line->setChannelBaseValue(core::TimeKeyType_Depth, 0, previous); }
            ));
            return;
        }

        auto key = (const core::DepthKey*)currline().baseValue(core::TimeKeyType_Depth);
        XC_PTR_ASSERT(key);
        core::DepthKey::Data newData = key->data();
        newData.setDepth(aNext);
        ctrl::TimeLineUtil::assignDepthKeyData(*mProject, *mTarget, core::TimeLine::kBaseValueIndex, newData);
    }

    void KeyAccessor::assignDefaultOpacity(float aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Opacity)) {
            core::TimeLine* line = &currline();
            core::ChannelTrack* track = line->channelTrack(core::TimeKeyType_Opacity, 0);
            if (!track) {
                return;
            }
            const float value = track->clamp(track->quantize(aNext));
            const float previous = track->baseValue();
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update opacity"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Opacity);
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Delegatable(
                [line, value]() { line->setChannelBaseValue(core::TimeKeyType_Opacity, 0, value); },
                [line, previous]() { line->setChannelBaseValue(core::TimeKeyType_Opacity, 0, previous); }
            ));
            return;
        }

        auto key = (const core::OpacityKey*)currline().baseValue(core::TimeKeyType_Opacity);
        XC_PTR_ASSERT(key);
        core::OpacityKey::Data newData = key->data();
        newData.setOpacity(aNext);
        ctrl::TimeLineUtil::assignOpacityKeyData(*mProject, *mTarget, core::TimeLine::kBaseValueIndex, newData);
    }

    void KeyAccessor::assignDefaultHSV(QList<int> aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        auto key = (const core::HSVKey*)currline().baseValue(core::TimeKeyType_HSV);
        XC_PTR_ASSERT(key);
        core::HSVKey::Data newData = key->data();
        newData.setHSV(aNext);
        ctrl::TimeLineUtil::assignHSVKeyData(*mProject, *mTarget, core::TimeLine::kBaseValueIndex, newData);
    }

    void KeyAccessor::assignDefaultImageResource(img::ResourceNode& aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyResource(*mProject, *mTarget, core::TimeLine::kBaseValueIndex, aNext);
    }

    void KeyAccessor::assignDefaultImageOffset(const QVector2D& aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyOffset(*mProject, *mTarget, core::TimeLine::kBaseValueIndex, aNext);
    }

    void KeyAccessor::assignDefaultImageCellSize(int aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        ctrl::TimeLineUtil::assignImageKeyCellSize(*mProject, *mTarget, core::TimeLine::kBaseValueIndex, aNext);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignMoveEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
            core::TimeLine& line = currline();
            bool animated = false;
            for (int c = 0; c < 4; ++c) {
                animated = animated || line.hasChannelKeys(core::TimeKeyType_Move, c);
            }
            if (!animated) {
                return;
            }
            const int frame = getFrame();
            const QVector2D pos = line.current().srt().pos();
            const QVector2D centroid = line.current().srt().centroid();
            const float values[4] = { pos.x(), pos.y(), centroid.x(), centroid.y() };
            core::ChannelKey* keys[4] = {};
            for (int c = 0; c < 4; ++c) {
                keys[c] = ensureChannelKey(
                    *mProject, *mTarget, core::TimeKeyType_Move, c, frame, values[c]
                );
                if (!keys[c]) {
                    return;
                }
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update move key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            for (int c = 0; c < 4; ++c) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Move, frame, c)
                );
            }
            macro.grabListener(notifier);
            for (int c = 0; c < 4; ++c) {
                mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(&keys[c]->easing(), aNext));
            }
            return;
        }

        const int frame = editFrame<core::MoveKey, core::TimeKeyType_Move>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        for (int component = 0; component < core::MoveKey::kComponents; ++component) {
            newData.easing(component) = aNext;
        }

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignMoveSpline(int aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(0 <= aNext && aNext < core::MoveKey::SplineType_TERM);

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
            core::TimeLine& line = currline();
            core::PropertyTrack* prop = line.property(core::TimeKeyType_Move);
            if (!prop) {
                return;
            }
            auto* next = new core::MoveStateKey();
            next->setSpline(static_cast<core::MoveKey::SplineType>(aNext));
            const core::StateKey* previousState = prop->state().baseState();
            std::shared_ptr<core::StateKey> previous(
                previousState ? previousState->cloneState() : nullptr
            );
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update move spline"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Move);
            macro.grabListener(notifier);
            core::PropertyTrack* live = prop;
            mProject->commandStack().push(new cmnd::Delegatable(
                [live, next]() {
                    std::unique_ptr<core::StateKey> owned(next);
                    live->state().setBaseState(std::move(owned));
                },
                [live, prevState = std::move(previous)]() mutable {
                    std::unique_ptr<core::StateKey> owned(
                        prevState ? prevState->cloneState() : nullptr
                    );
                    live->state().setBaseState(std::move(owned));
                }
            ));
            return;
        }

        const int frame = editFrame<core::MoveKey, core::TimeKeyType_Move>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        newData.setSpline((core::MoveKey::SplineType)aNext);

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignMovePosition(const QVector2D& aNewPos, int aComponent) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* x = line.channelTrack(core::TimeKeyType_Move, 0);
            core::ChannelTrack* y = line.channelTrack(core::TimeKeyType_Move, 1);
            if (!x || !y) {
                return;
            }
            const QVector2D value(
                x->clamp(x->quantize(aNewPos.x())), y->clamp(y->quantize(aNewPos.y()))
            );
            const bool keyed = line.hasChannelKeys(core::TimeKeyType_Move, 0)
                || line.hasChannelKeys(core::TimeKeyType_Move, 1);
            if (!keyed) {
                const QVector2D previous(x->baseValue(), y->baseValue());
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update position"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Move);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, value]() {
                        live->setChannelBaseValue(core::TimeKeyType_Move, 0, value.x());
                        live->setChannelBaseValue(core::TimeKeyType_Move, 1, value.y());
                    },
                    [live, previous]() {
                        live->setChannelBaseValue(core::TimeKeyType_Move, 0, previous.x());
                        live->setChannelBaseValue(core::TimeKeyType_Move, 1, previous.y());
                    }
                ));
                return;
            }
            const int frame = getFrame();
            const bool whole = aComponent < 0 || line.frameEditIsLinked(core::TimeKeyType_Move, frame);
            if (whole) {
                const QVector2D pos = line.current().srt().pos();
                const float values[2] = { pos.x(), pos.y() };
                core::ChannelKey* keys[2] = {};
                for (int c = 0; c < 2; ++c) {
                    keys[c] = ensureChannelKey(
                        *mProject, *mTarget, core::TimeKeyType_Move, c, frame, values[c]
                    );
                    if (!keys[c]) {
                        return;
                    }
                }
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update move key"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                for (int c = 0; c < 2; ++c) {
                    notifier->event().pushTarget(
                        *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Move, frame, c)
                    );
                }
                macro.grabListener(notifier);
                mProject->commandStack().push(new cmnd::Assign<float>(&keys[0]->valueRef(), value.x()));
                mProject->commandStack().push(new cmnd::Assign<float>(&keys[1]->valueRef(), value.y()));
                return;
            }
            const int component = aComponent == 0 ? 0 : 1;
            const float seed = component == 0 ? value.x() : value.y();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Move, component, frame, seed, false
            );
            if (!key) return;
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update move channel key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Move, frame, component)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), seed));
            return;
        }

        const int frame = editFrame<core::MoveKey, core::TimeKeyType_Move>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        newData.setPos(aNewPos);

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignMoveCentroid(const QVector2D& aNewCentroid, int aComponent) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* x = line.channelTrack(core::TimeKeyType_Move, 2);
            core::ChannelTrack* y = line.channelTrack(core::TimeKeyType_Move, 3);
            if (!x || !y) {
                return;
            }
            const QVector2D value(
                x->clamp(x->quantize(aNewCentroid.x())), y->clamp(y->quantize(aNewCentroid.y()))
            );
            const bool keyed = line.hasChannelKeys(core::TimeKeyType_Move, 2)
                || line.hasChannelKeys(core::TimeKeyType_Move, 3);
            if (!keyed) {
                const QVector2D previous(x->baseValue(), y->baseValue());
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update centroid"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Move);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, value]() {
                        live->setChannelBaseValue(core::TimeKeyType_Move, 2, value.x());
                        live->setChannelBaseValue(core::TimeKeyType_Move, 3, value.y());
                    },
                    [live, previous]() {
                        live->setChannelBaseValue(core::TimeKeyType_Move, 2, previous.x());
                        live->setChannelBaseValue(core::TimeKeyType_Move, 3, previous.y());
                    }
                ));
                return;
            }
            const int frame = getFrame();
            const bool whole = aComponent < 0
                || line.frameEditIsLinked(core::TimeKeyType_Move, frame);
            if (whole) {
                const QVector2D centroid = line.current().srt().centroid();
                const float values[2] = { centroid.x(), centroid.y() };
                core::ChannelKey* keys[2] = {};
                for (int i = 0; i < 2; ++i) {
                    keys[i] = ensureChannelKey(
                        *mProject, *mTarget, core::TimeKeyType_Move, 2 + i, frame, values[i]
                    );
                    if (!keys[i]) {
                        return;
                    }
                }
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update centroid key"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                for (int i = 0; i < 2; ++i) {
                    notifier->event().pushTarget(
                        *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Move, frame, 2 + i)
                    );
                }
                macro.grabListener(notifier);
                mProject->commandStack().push(
                    new cmnd::Assign<float>(&keys[0]->valueRef(), value.x())
                );
                mProject->commandStack().push(
                    new cmnd::Assign<float>(&keys[1]->valueRef(), value.y())
                );
                return;
            }
            const int component = aComponent == 2 ? 2 : 3;
            const float seed = component == 2 ? value.x() : value.y();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Move, component, frame, seed, false
            );
            if (!key) return;
            cmnd::ScopedMacro macro(
                mProject->commandStack(), QStringLiteral("Update centroid channel key")
            );
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Move, frame, component)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), seed));
            return;
        }

        const int frame = editFrame<core::MoveKey, core::TimeKeyType_Move>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::MoveKey, core::TimeKeyType_Move>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::MoveKey, core::TimeKeyType_Move>(*mTarget, frame);
        newData.setCentroid(aNewCentroid);

        ctrl::TimeLineUtil::assignMoveKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignRotateEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Rotate)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Rotate, 0);
            if (!track || !line.hasChannelKeys(core::TimeKeyType_Rotate, 0)) {
                return;
            }
            const int frame = getFrame();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Rotate, 0, frame,
                line.current().srt().rotate()
            );
            if (!key) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update rotation key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Rotate, frame, 0)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(&key->easing(), aNext));
            return;
        }

        const int frame = editFrame<core::RotateKey, core::TimeKeyType_Rotate>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::RotateKey, core::TimeKeyType_Rotate>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::RotateKey, core::TimeKeyType_Rotate>(*mTarget, frame);
        for (int component = 0; component < core::RotateKey::kComponents; ++component) {
            newData.easing(component) = aNext;
        }

        ctrl::TimeLineUtil::assignRotateKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignRotateAngle(float aAngle) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Rotate)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Rotate, 0);
            if (!track) {
                return;
            }
            const float value = track->clamp(track->quantize(aAngle));
            if (!line.hasChannelKeys(core::TimeKeyType_Rotate, 0)) {
                const float previous = track->baseValue();
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update rotation"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Rotate);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, value]() { live->setChannelBaseValue(core::TimeKeyType_Rotate, 0, value); },
                    [live, previous]() { live->setChannelBaseValue(core::TimeKeyType_Rotate, 0, previous); }
                ));
                return;
            }
            const int frame = getFrame();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Rotate, 0, frame, line.current().srt().rotate()
            );
            if (!key) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update rotation key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Rotate, frame, 0)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), value));
            return;
        }

        const int frame = editFrame<core::RotateKey, core::TimeKeyType_Rotate>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::RotateKey, core::TimeKeyType_Rotate>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::RotateKey, core::TimeKeyType_Rotate>(*mTarget, frame);
        newData.setRotate(aAngle);

        ctrl::TimeLineUtil::assignRotateKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignScaleEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Scale)) {
            core::TimeLine& line = currline();
            if (!line.hasChannelKeys(core::TimeKeyType_Scale, 0)
                && !line.hasChannelKeys(core::TimeKeyType_Scale, 1)) {
                return;
            }
            const int frame = getFrame();
            const QVector2D current = line.current().srt().scale();
            core::ChannelKey* x = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Scale, 0, frame, current.x()
            );
            core::ChannelKey* y = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Scale, 1, frame, current.y()
            );
            if (!x || !y) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update scale key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            for (int component = 0; component < 2; ++component) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Scale, frame, component)
                );
            }
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(&x->easing(), aNext));
            mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(&y->easing(), aNext));
            return;
        }

        const int frame = editFrame<core::ScaleKey, core::TimeKeyType_Scale>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::ScaleKey, core::TimeKeyType_Scale>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::ScaleKey, core::TimeKeyType_Scale>(*mTarget, frame);
        for (int component = 0; component < core::ScaleKey::kComponents; ++component) {
            newData.easing(component) = aNext;
        }
        ctrl::TimeLineUtil::assignScaleKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignScaleRate(const QVector2D& aNewScale, int aComponent) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Scale)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* xTrack = line.channelTrack(core::TimeKeyType_Scale, 0);
            core::ChannelTrack* yTrack = line.channelTrack(core::TimeKeyType_Scale, 1);
            if (!xTrack || !yTrack) {
                return;
            }
            const QVector2D value(
                xTrack->clamp(xTrack->quantize(aNewScale.x())),
                yTrack->clamp(yTrack->quantize(aNewScale.y()))
            );
            const bool keyed = line.hasChannelKeys(core::TimeKeyType_Scale, 0)
                || line.hasChannelKeys(core::TimeKeyType_Scale, 1);
            if (!keyed) {
                const QVector2D previous(xTrack->baseValue(), yTrack->baseValue());
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update scale"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Scale);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, value]() {
                        live->setChannelBaseValue(core::TimeKeyType_Scale, 0, value.x());
                        live->setChannelBaseValue(core::TimeKeyType_Scale, 1, value.y());
                    },
                    [live, previous]() {
                        live->setChannelBaseValue(core::TimeKeyType_Scale, 0, previous.x());
                        live->setChannelBaseValue(core::TimeKeyType_Scale, 1, previous.y());
                    }
                ));
                return;
            }

            const int frame = getFrame();
            const bool whole = aComponent < 0 || line.frameEditIsLinked(core::TimeKeyType_Scale, frame);
            if (whole) {
                const QVector2D current = line.current().srt().scale();
                core::ChannelKey* x = ensureChannelKey(
                    *mProject, *mTarget, core::TimeKeyType_Scale, 0, frame, current.x()
                );
                core::ChannelKey* y = ensureChannelKey(
                    *mProject, *mTarget, core::TimeKeyType_Scale, 1, frame, current.y()
                );
                if (!x || !y) {
                    return;
                }
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update scale key"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                for (int component = 0; component < 2; ++component) {
                    notifier->event().pushTarget(
                        *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Scale, frame, component)
                    );
                }
                macro.grabListener(notifier);
                mProject->commandStack().push(new cmnd::Assign<float>(&x->valueRef(), value.x()));
                mProject->commandStack().push(new cmnd::Assign<float>(&y->valueRef(), value.y()));
                return;
            }
            const int component = aComponent == 0 ? 0 : 1;
            const float seed = component == 0 ? value.x() : value.y();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Scale, component, frame, seed, false
            );
            if (!key) return;
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update scale channel key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Scale, frame, component)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), seed));
            return;
        }

        const int frame = editFrame<core::ScaleKey, core::TimeKeyType_Scale>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::ScaleKey, core::TimeKeyType_Scale>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::ScaleKey, core::TimeKeyType_Scale>(*mTarget, frame);
        newData.setScale(aNewScale);

        ctrl::TimeLineUtil::assignScaleKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignDepthEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Depth)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Depth, 0);
            if (!track || !line.hasChannelKeys(core::TimeKeyType_Depth, 0)) {
                return;
            }
            const int frame = getFrame();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Depth, 0, frame, line.current().depth()
            );
            if (!key) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update depth key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Depth, frame, 0)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(&key->easing(), aNext));
            return;
        }

        const int frame = editFrame<core::DepthKey, core::TimeKeyType_Depth>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::DepthKey, core::TimeKeyType_Depth>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::DepthKey, core::TimeKeyType_Depth>(*mTarget, frame);
        for (int component = 0; component < core::DepthKey::kComponents; ++component) {
            newData.easing(component) = aNext;
        }

        ctrl::TimeLineUtil::assignDepthKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignDepthPosition(float aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Depth)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Depth, 0);
            if (!track) {
                return;
            }
            const float value = track->clamp(track->quantize(aNext));
            if (!line.hasChannelKeys(core::TimeKeyType_Depth, 0)) {
                const float previous = track->baseValue();
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update depth"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Depth);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, value]() { live->setChannelBaseValue(core::TimeKeyType_Depth, 0, value); },
                    [live, previous]() { live->setChannelBaseValue(core::TimeKeyType_Depth, 0, previous); }
                ));
                return;
            }
            const int frame = getFrame();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Depth, 0, frame, line.current().depth()
            );
            if (!key) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update depth key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Depth, frame, 0)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), value));
            return;
        }

        const int frame = editFrame<core::DepthKey, core::TimeKeyType_Depth>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::DepthKey, core::TimeKeyType_Depth>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::DepthKey, core::TimeKeyType_Depth>(*mTarget, frame);
        newData.setDepth(aNext);

        ctrl::TimeLineUtil::assignDepthKeyData(*mProject, *mTarget, frame, newData);
    }

    //-------------------------------------------------------------------------------------------------
    void KeyAccessor::assignOpacity(float aOpacity) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Opacity)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Opacity, 0);
            if (!track) {
                return;
            }
            const float value = track->clamp(track->quantize(aOpacity));

            if (!line.hasChannelKeys(core::TimeKeyType_Opacity, 0)) {
                // Static: the channel's base value is the edited value. The
                // undoable command carries both sides, like every other edit.
                const float previous = track->baseValue();
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update opacity"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Opacity);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, value]() { live->setChannelBaseValue(core::TimeKeyType_Opacity, 0, value); },
                    [live, previous]() { live->setChannelBaseValue(core::TimeKeyType_Opacity, 0, previous); }
                ));
                return;
            }

            const int frame = getFrame();
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Opacity, 0, frame, line.current().opacity().opacity()
            );
            if (!key) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update opacity key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(*mTarget, core::TimeKeyPos(line, core::TimeKeyType_Opacity, frame, 0));
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), value));
            return;
        }

        const int frame = editFrame<core::OpacityKey, core::TimeKeyType_Opacity>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::OpacityKey, core::TimeKeyType_Opacity>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::OpacityKey, core::TimeKeyType_Opacity>(*mTarget, frame);
        newData.setOpacity(aOpacity);

        ctrl::TimeLineUtil::assignOpacityKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignHSV(int aValue, QString aType) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_HSV)) {
            core::TimeLine& line = currline();
            if (aType == "abs") {
                core::PropertyTrack* prop = line.property(core::TimeKeyType_HSV);
                if (!prop) return;
                const core::StateKey* previousState = prop->state().baseState();
                std::shared_ptr<core::StateKey> previous(
                    previousState ? previousState->cloneState() : nullptr
                );
                auto* next = new core::HSVStateKey();
                next->setAbsolute(aValue != 0);
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update HSV mode"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_HSV);
                macro.grabListener(notifier);
                core::PropertyTrack* live = prop;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, next]() {
                        std::unique_ptr<core::StateKey> owned(next);
                        live->state().setBaseState(std::move(owned));
                    },
                    [live, prevState = std::move(previous)]() {
                        std::unique_ptr<core::StateKey> owned(
                            prevState ? prevState->cloneState() : nullptr
                        );
                        live->state().setBaseState(std::move(owned));
                    }
                ));
                return;
            }
            int component = 0;
            if (aType == "sat") component = 1;
            else if (aType == "val") component = 2;
            bool keyed = false;
            for (int c = 0; c < 3; ++c) {
                keyed = keyed || line.hasChannelKeys(core::TimeKeyType_HSV, c);
            }
            if (!keyed) {
                const float previous = line.channelBaseValue(core::TimeKeyType_HSV, component);
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update HSV"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_HSV);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, component, aValue]() { live->setChannelBaseValue(core::TimeKeyType_HSV, component, float(aValue)); },
                    [live, component, previous]() { live->setChannelBaseValue(core::TimeKeyType_HSV, component, previous); }
                ));
                return;
            }
            const int frame = getFrame();
            const QList<int> current = line.current().hsv().hsv();
            const bool whole = line.frameEditIsLinked(core::TimeKeyType_HSV, frame);
            if (whole) {
                core::ChannelKey* keys[3] = {
                    ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_HSV, 0, frame, float(current.value(0))),
                    ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_HSV, 1, frame, float(current.value(1, 100))),
                    ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_HSV, 2, frame, float(current.value(2, 100))),
                };
                if (!keys[0] || !keys[1] || !keys[2]) return;
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update HSV key"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                for (int c = 0; c < 3; ++c) {
                    notifier->event().pushTarget(
                        *mTarget, core::TimeKeyPos(line, core::TimeKeyType_HSV, frame, c)
                    );
                }
                macro.grabListener(notifier);
                mProject->commandStack().push(
                    new cmnd::Assign<float>(&keys[component]->valueRef(), float(aValue))
                );
                return;
            }
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_HSV, component, frame,
                float(current.value(component, component == 0 ? 0 : 100)), false
            );
            if (!key) return;
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update HSV channel key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_HSV, frame, component)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), float(aValue)));
            return;
        }

        const int frame = editFrame<core::HSVKey, core::TimeKeyType_HSV>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::HSVKey, core::TimeKeyType_HSV>(*mProject, *mTarget, frame);
        }
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

    void KeyAccessor::assignOpacityEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Opacity)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Opacity, 0);
            if (!track || !line.hasChannelKeys(core::TimeKeyType_Opacity, 0)) {
                return;
            }
            const int frame = getFrame();
            core::ChannelKey* key = track->keyAt(frame);
            if (!key) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update opacity key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(*mTarget, core::TimeKeyPos(line, core::TimeKeyType_Opacity, frame, 0));
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(&key->easing(), aNext));
            return;
        }

        const int frame = editFrame<core::OpacityKey, core::TimeKeyType_Opacity>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::OpacityKey, core::TimeKeyType_Opacity>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::OpacityKey, core::TimeKeyType_Opacity>(*mTarget, frame);
        for (int component = 0; component < core::OpacityKey::kComponents; ++component) {
            newData.easing(component) = aNext;
        }

        ctrl::TimeLineUtil::assignOpacityKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignHSVEasing(util::Easing::Param aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        XC_ASSERT(aNext.isValidParam());

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_HSV)) {
            core::TimeLine& line = currline();
            bool keyed = false;
            for (int c = 0; c < 3; ++c) keyed = keyed || line.hasChannelKeys(core::TimeKeyType_HSV, c);
            if (!keyed) return;
            const int frame = getFrame();
            const QList<int> current = line.current().hsv().hsv();
            core::ChannelKey* keys[3] = {
                ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_HSV, 0, frame, float(current.value(0))),
                ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_HSV, 1, frame, float(current.value(1, 100))),
                ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_HSV, 2, frame, float(current.value(2, 100))),
            };
            if (!keys[0] || !keys[1] || !keys[2]) return;
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update HSV key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            for (int c = 0; c < 3; ++c) {
                notifier->event().pushTarget(*mTarget, core::TimeKeyPos(line, core::TimeKeyType_HSV, frame, c));
            }
            macro.grabListener(notifier);
            for (int c = 0; c < 3; ++c) {
                mProject->commandStack().push(new cmnd::Assign<util::Easing::Param>(&keys[c]->easing(), aNext));
            }
            return;
        }

        const int frame = editFrame<core::HSVKey, core::TimeKeyType_HSV>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::HSVKey, core::TimeKeyType_HSV>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::HSVKey, core::TimeKeyType_HSV>(*mTarget, frame);
        for (int component = 0; component < core::HSVKey::kComponents; ++component) {
            newData.easing(component) = aNext;
        }

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
        const int frame = editFrame<core::ImageKey, core::TimeKeyType_Image>(currline(), getFrame());
        ctrl::TimeLineUtil::assignImageKeyResource(*mProject, *mTarget, frame, aNext);
    }

    void KeyAccessor::assignImageOffset(const QVector2D& aNext, int aComponent) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        core::TimeLine& line = currline();
        const core::PropertyTrack* prop = line.property(core::TimeKeyType_Image);
        const bool animated = !line.map(core::TimeKeyType_Image).isEmpty()
            || (prop && (!prop->channel(0)->isEmpty() || !prop->channel(1)->isEmpty()));
        const int frame = animated ? getFrame() : core::TimeLine::kBaseValueIndex;
        ctrl::TimeLineUtil::assignImageKeyOffset(*mProject, *mTarget, frame, aNext, aComponent);
    }

    void KeyAccessor::assignImageCellSize(int aNext) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = editFrame<core::ImageKey, core::TimeKeyType_Image>(currline(), getFrame());
        ctrl::TimeLineUtil::assignImageKeyCellSize(*mProject, *mTarget, frame, aNext);
    }

    void KeyAccessor::removeKey(core::TimeKeyType aType) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = getFrame();

        if (core::TimeLine::usesChannelTracks(aType)) {
            core::TimeLine* line = &currline();
            QVector<int> components;
            for (int component = 0; component < core::propertyChannelCount(aType); ++component) {
                const core::ChannelTrack* track = line->channelTrack(aType, component);
                if (track && track->hasKey(frame)) {
                    components.append(component);
                }
            }
            if (components.isEmpty()) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Remove key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_RemoveKey);
            for (int component : components) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(*line, aType, frame, component)
                );
            }
            macro.grabListener(notifier);
            for (int component : components) {
                mProject->commandStack().push(line->createChannelRemover(aType, component, frame, true));
            }
            return;
        }

        if (!currline().hasTimeKey(aType, frame))
            return;
        cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Remove key"));
        auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
        notifier->event().setType(core::TimeLineEvent::Type_RemoveKey);
        notifier->event().pushTarget(*mTarget, aType, frame);
        macro.grabListener(notifier);
        mProject->commandStack().push(currline().createRemover(aType, frame, true));
    }

    //-------------------------------------------------------------------------------------------------

    template<typename T>
    void assignParam(T* key) {
        // One reader of the stored defaults, so a retired Auto default becomes
        // the automatic (CLAMPED) pair and every new-key path agrees.
        const util::Easing::Param aNext = util::Easing::defaultParam();
        // A new key starts with every component on the same default curve; each
        // component then owns its own copy, so editing one cannot warp another.
        for (int component = 0; component < T::kComponents; ++component) {
            key->data().easing(component) = aNext;
        }
    }

    //-------------------------------------------------------------------------------------------------

    util::Easing::AutoContext autoContextOf(
        const core::TimeLineEvent::Target& aTarget, const core::TimeKey& aKey, int aComponent) {
        const core::TimeLine* line = aTarget.pos.line();
        if (!line)
            return util::Easing::AutoContext();

        if (core::TimeLine::usesChannelTracks(aTarget.pos.type())) {
            const core::ChannelTrack* track =
                line->channelTrack(aTarget.pos.type(), aComponent);
            if (!track) {
                return util::Easing::AutoContext();
            }
            const core::ChannelKey* from = track->keyAt(aKey.frame());
            if (!from) {
                return util::Easing::AutoContext();
            }
            const core::ChannelKey* to = track->keyAfter(aKey.frame());
            if (!to) {
                return util::Easing::AutoContext(); // a lane's last key has no segment
            }
            const core::ChannelKey* prev = track->keyBeforeOrAt(aKey.frame() - 1);
            const core::ChannelKey* next = track->keyAfter(to->frame());
            return core::autoEasingContext(*from, *to, aComponent, prev, next);
        }

        const core::TimeLine::MapType& map = line->map(aTarget.pos.type());
        auto from = map.constFind(aKey.frame());
        if (from == map.constEnd())
            return util::Easing::AutoContext();
        auto to = from;
        ++to;
        if (to == map.constEnd())
            return util::Easing::AutoContext(); // a lane's last key has no segment to shape

        const core::TimeKey* prev = nullptr;
        if (from != map.constBegin()) {
            auto p = from;
            --p;
            prev = p.value();
        }
        const core::TimeKey* next = nullptr;
        {
            auto n = to;
            ++n;
            if (n != map.constEnd())
                next = n.value();
        }
        return core::autoEasingContext(*from.value(), *to.value(), aComponent, prev, next);
    }

    //-------------------------------------------------------------------------------------------------

    void KeyAccessor::knockNewMove() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
            core::TimeLine& line = currline();
            const int frame = getFrame();
            const QVector2D currentPos = line.current().srt().pos();
            const QVector2D currentCentroid = line.current().srt().centroid();
            const float values[4] = {
                line.hasChannelKeys(core::TimeKeyType_Move, 0) ? currentPos.x()
                                                               : line.channelBaseValue(core::TimeKeyType_Move, 0),
                line.hasChannelKeys(core::TimeKeyType_Move, 1) ? currentPos.y()
                                                               : line.channelBaseValue(core::TimeKeyType_Move, 1),
                line.hasChannelKeys(core::TimeKeyType_Move, 2) ? currentCentroid.x()
                                                               : line.channelBaseValue(core::TimeKeyType_Move, 2),
                line.hasChannelKeys(core::TimeKeyType_Move, 3) ? currentCentroid.y()
                                                               : line.channelBaseValue(core::TimeKeyType_Move, 3),
            };
            core::ChannelTrack* tracks[4] = {};
            for (int c = 0; c < 4; ++c) {
                tracks[c] = line.channelTrack(core::TimeKeyType_Move, c);
                if (!tracks[c] || tracks[c]->hasKey(frame)) {
                    return;
                }
            }
            core::ChannelKey* keys[4] = {};
            for (int c = 0; c < 4; ++c) {
                keys[c] = new core::ChannelKey(core::TimeKeyType_Move, c);
                keys[c]->setFrame(frame);
                keys[c]->setValue(tracks[c]->clamp(tracks[c]->quantize(values[c])));
                keys[c]->easing() = util::Easing::defaultParam();
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new move key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            for (int c = 0; c < 4; ++c) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Move, frame, c)
                );
            }
            macro.grabListener(notifier);
            auto& stack = mProject->commandStack();
            for (int c = 0; c < 4; ++c) {
                stack.push(new cmnd::GrabNewObject<core::ChannelKey>(keys[c]));
                stack.push(line.createChannelPusher(core::TimeKeyType_Move, c, frame, keys[c]));
            }
            return;
        }

        auto newKey = new core::MoveKey();
        newKey->setPos(currline().current().srt().pos());
        newKey->setCentroid(currline().current().srt().centroid());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewMoveKey(*mProject, *mTarget, getFrame(), newKey);
    }
    void KeyAccessor::knockNewRotate() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Rotate)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Rotate, 0);
            const int frame = getFrame();
            if (!track || track->hasKey(frame)) {
                return;
            }
            auto* key = new core::ChannelKey(core::TimeKeyType_Rotate, 0);
            key->setValue(track->clamp(track->quantize(line.current().srt().rotate())));
            key->easing() = util::Easing::defaultParam();
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new rotation key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Rotate, frame, 0)
            );
            macro.grabListener(notifier);
            auto& stack = mProject->commandStack();
            stack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
            stack.push(line.createChannelPusher(core::TimeKeyType_Rotate, 0, frame, key));
            return;
        }

        auto newKey = new core::RotateKey();
        newKey->setRotate(currline().current().srt().rotate());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewRotateKey(*mProject, *mTarget, getFrame(), newKey);
    }
    void KeyAccessor::knockNewScale() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Scale)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* xTrack = line.channelTrack(core::TimeKeyType_Scale, 0);
            core::ChannelTrack* yTrack = line.channelTrack(core::TimeKeyType_Scale, 1);
            const int frame = getFrame();
            if (!xTrack || !yTrack || xTrack->hasKey(frame) || yTrack->hasKey(frame)) {
                return;
            }
            const QVector2D value = line.current().srt().scale();
            auto* x = new core::ChannelKey(core::TimeKeyType_Scale, 0);
            x->setFrame(frame);
            x->setValue(xTrack->clamp(xTrack->quantize(value.x())));
            x->easing() = util::Easing::defaultParam();
            auto* y = new core::ChannelKey(core::TimeKeyType_Scale, 1);
            y->setFrame(frame);
            y->setValue(yTrack->clamp(yTrack->quantize(value.y())));
            y->easing() = util::Easing::defaultParam();
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new scale key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            for (int component = 0; component < 2; ++component) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Scale, frame, component)
                );
            }
            macro.grabListener(notifier);
            auto& stack = mProject->commandStack();
            stack.push(new cmnd::GrabNewObject<core::ChannelKey>(x));
            stack.push(new cmnd::GrabNewObject<core::ChannelKey>(y));
            stack.push(line.createChannelPusher(core::TimeKeyType_Scale, 0, frame, x));
            stack.push(line.createChannelPusher(core::TimeKeyType_Scale, 1, frame, y));
            return;
        }

        auto newKey = new core::ScaleKey();
        newKey->setScale(currline().current().srt().scale());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewScaleKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewDepth() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Depth)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Depth, 0);
            const int frame = getFrame();
            if (!track || track->hasKey(frame)) {
                return;
            }
            auto* key = new core::ChannelKey(core::TimeKeyType_Depth, 0);
            key->setValue(track->clamp(track->quantize(line.current().depth())));
            key->easing() = util::Easing::defaultParam();
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new depth key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Depth, frame, 0)
            );
            macro.grabListener(notifier);
            auto& stack = mProject->commandStack();
            stack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
            stack.push(line.createChannelPusher(core::TimeKeyType_Depth, 0, frame, key));
            return;
        }

        auto newKey = new core::DepthKey();
        newKey->setDepth(currline().current().depth());
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewDepthKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewOpacity() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Opacity)) {
            core::TimeLine& line = currline();
            core::ChannelTrack* track = line.channelTrack(core::TimeKeyType_Opacity, 0);
            const int frame = getFrame();
            if (!track || track->hasKey(frame)) {
                return;
            }
            auto* key = new core::ChannelKey(core::TimeKeyType_Opacity, 0);
            key->setValue(track->clamp(track->quantize(line.current().opacity().opacity())));
            key->easing() = util::Easing::defaultParam();

            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new opacity key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Opacity, frame, 0)
            );
            macro.grabListener(notifier);

            auto& stack = mProject->commandStack();
            stack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
            stack.push(line.createChannelPusher(core::TimeKeyType_Opacity, 0, frame, key));
            return;
        }

        auto newKey = new core::OpacityKey();
        newKey->data() = currline().current().opacity();
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewOpacityKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewHSV() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_HSV)) {
            core::TimeLine& line = currline();
            const int frame = getFrame();
            if (line.hasChannelKey(core::TimeKeyType_HSV, 0, frame)) return;
            const QList<int> values = line.current().hsv().hsv();
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new HSV key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            for (int c = 0; c < 3; ++c) {
                notifier->event().pushTarget(*mTarget, core::TimeKeyPos(line, core::TimeKeyType_HSV, frame, c));
            }
            macro.grabListener(notifier);
            for (int c = 0; c < 3; ++c) {
                auto* key = new core::ChannelKey(core::TimeKeyType_HSV, c);
                key->setFrame(frame);
                key->setValue(float(values.value(c, c == 0 ? 0 : 100)));
                key->easing() = util::Easing::defaultParam();
                mProject->commandStack().push(new cmnd::GrabNewObject<core::ChannelKey>(key));
                mProject->commandStack().push(line.createChannelPusher(core::TimeKeyType_HSV, c, frame, key));
            }
            return;
        }

        auto newKey = new core::HSVKey();
        newKey->data() = currline().current().hsv();
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewHSVKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::assignBlur(float aAmount) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Blur)) {
            core::TimeLine& line = currline();
            bool keyed = false;
            for (int c = 0; c < 3; ++c) {
                keyed = keyed || line.hasChannelKeys(core::TimeKeyType_Blur, c);
            }
            const float value = line.channelTrack(core::TimeKeyType_Blur, 0)
                ? line.channelTrack(core::TimeKeyType_Blur, 0)->clamp(aAmount) : aAmount;
            if (!keyed) {
                const float prevX = line.channelBaseValue(core::TimeKeyType_Blur, 0);
                const float prevY = line.channelBaseValue(core::TimeKeyType_Blur, 1);
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update blur"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Blur);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, value]() {
                        live->setChannelBaseValue(core::TimeKeyType_Blur, 0, value);
                        live->setChannelBaseValue(core::TimeKeyType_Blur, 1, value);
                        live->setChannelBaseValue(core::TimeKeyType_Blur, 2, 0.0f);
                    },
                    [live, prevX, prevY]() {
                        live->setChannelBaseValue(core::TimeKeyType_Blur, 0, prevX);
                        live->setChannelBaseValue(core::TimeKeyType_Blur, 1, prevY);
                        live->setChannelBaseValue(core::TimeKeyType_Blur, 2, 0.0f);
                    }
                ));
                return;
            }
            const int frame = getFrame();
            const QVector3D current(
                line.current().blurX(), line.current().blurY(), line.current().angleDeg()
            );
            core::ChannelKey* x = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Blur, 0, frame, current.x());
            core::ChannelKey* y = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Blur, 1, frame, current.y());
            core::ChannelKey* angle = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Blur, 2, frame, current.z());
            if (!x || !y || !angle) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update blur key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            for (int c = 0; c < 3; ++c) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Blur, frame, c)
                );
            }
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&x->valueRef(), value));
            mProject->commandStack().push(new cmnd::Assign<float>(&y->valueRef(), value));
            mProject->commandStack().push(new cmnd::Assign<float>(&angle->valueRef(), 0.0f));
            return;
        }

        const int frame = editFrame<core::BlurKey, core::TimeKeyType_Blur>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        }
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
        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Blur)) {
            core::BlurKey::Data data;
            const auto* x = timeLine.channelKey(core::TimeKeyType_Blur, 0, frame);
            const auto* y = timeLine.channelKey(core::TimeKeyType_Blur, 1, frame);
            const auto* angle = timeLine.channelKey(core::TimeKeyType_Blur, 2, frame);
            if (x || y || angle) {
                data.setBlurX(x ? x->value() : timeLine.current().blurX());
                data.setBlurY(y ? y->value() : timeLine.current().blurY());
                data.setAngleDeg(angle ? angle->value() : timeLine.current().angleDeg());
            } else {
                const auto& current = timeLine.current();
                data.setBlurX(current.blurX());
                data.setBlurY(current.blurY());
                data.setAngleDeg(current.angleDeg());
            }
            const core::PropertyTrack* prop = timeLine.property(core::TimeKeyType_Blur);
            if (prop) {
                if (const auto* state = static_cast<const core::BlurStateKey*>(
                        prop->state().sampleState(frame))) {
                    data.setDirectional(state->directional());
                }
            }
            return data;
        }
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

    void KeyAccessor::assignBlurEllipse(float aBlurX, float aBlurY, float aAngleDeg, int aComponent) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Blur)) {
            core::TimeLine& line = currline();
            const float values[3] = { aBlurX, aBlurY, aAngleDeg };
            bool keyed = false;
            for (int c = 0; c < 3; ++c) {
                keyed = keyed || line.hasChannelKeys(core::TimeKeyType_Blur, c);
            }
            if (!keyed) {
                const float previous[3] = {
                    line.channelBaseValue(core::TimeKeyType_Blur, 0),
                    line.channelBaseValue(core::TimeKeyType_Blur, 1),
                    line.channelBaseValue(core::TimeKeyType_Blur, 2)
                };
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update blur"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Blur);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, values]() {
                        for (int c = 0; c < 3; ++c)
                            live->setChannelBaseValue(core::TimeKeyType_Blur, c, values[c]);
                    },
                    [live, previous]() {
                        for (int c = 0; c < 3; ++c)
                            live->setChannelBaseValue(core::TimeKeyType_Blur, c, previous[c]);
                    }
                ));
                return;
            }
            const int frame = getFrame();
            const bool whole = aComponent < 0 || line.frameEditIsLinked(core::TimeKeyType_Blur, frame);
            const QVector3D current(
                line.current().blurX(), line.current().blurY(), line.current().angleDeg()
            );
            if (whole) {
                core::ChannelKey* keys[3] = {
                    ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_Blur, 0, frame, current.x()),
                    ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_Blur, 1, frame, current.y()),
                    ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_Blur, 2, frame, current.z()),
                };
                if (!keys[0] || !keys[1] || !keys[2]) {
                    return;
                }
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update blur key"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                for (int c = 0; c < 3; ++c) {
                    notifier->event().pushTarget(
                        *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Blur, frame, c)
                    );
                }
                macro.grabListener(notifier);
                for (int c = 0; c < 3; ++c) {
                    mProject->commandStack().push(
                        new cmnd::Assign<float>(&keys[c]->valueRef(), values[c])
                    );
                }
                return;
            }
            const int component = qBound(0, aComponent, 2);
            const float seed = values[component];
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Blur, component, frame, seed, false
            );
            if (!key) return;
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update blur channel key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Blur, frame, component)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), seed));
            return;
        }

        const int frame = editFrame<core::BlurKey, core::TimeKeyType_Blur>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::BlurKey, core::TimeKeyType_Blur>(*mTarget, frame);
        newData.setBlurX(aBlurX);
        newData.setBlurY(aBlurY);
        newData.setAngleDeg(aAngleDeg);

        ctrl::TimeLineUtil::assignBlurKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignBlurDirectional(bool aIsDirectional) {
        ASSERT_AND_RETURN_INVALID_TARGET();
        const int frame = editFrame<core::BlurKey, core::TimeKeyType_Blur>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        }
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

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Blur)) {
            core::TimeLine& line = currline();
            bool keyed = false;
            for (int c = 0; c < 3; ++c) {
                keyed = keyed || line.hasChannelKeys(core::TimeKeyType_Blur, c);
            }
            if (!keyed) {
                return;
            }
            const int frame = getFrame();
            const QVector3D current(
                line.current().blurX(), line.current().blurY(), line.current().angleDeg()
            );
            core::ChannelKey* keys[3] = {
                ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_Blur, 0, frame, current.x()),
                ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_Blur, 1, frame, current.y()),
                ensureChannelKey(*mProject, *mTarget, core::TimeKeyType_Blur, 2, frame, current.z()),
            };
            if (!keys[0] || !keys[1] || !keys[2]) {
                return;
            }
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update blur key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            for (int c = 0; c < 3; ++c) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Blur, frame, c)
                );
            }
            macro.grabListener(notifier);
            for (int c = 0; c < 3; ++c) {
                mProject->commandStack().push(
                    new cmnd::Assign<util::Easing::Param>(&keys[c]->easing(), aNext)
                );
            }
            return;
        }

        const int frame = editFrame<core::BlurKey, core::TimeKeyType_Blur>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::BlurKey, core::TimeKeyType_Blur>(*mProject, *mTarget, frame);
        }
        auto newData = getKeyData<core::BlurKey, core::TimeKeyType_Blur>(*mTarget, frame);
        for (int component = 0; component < core::BlurKey::kComponents; ++component) {
            newData.easing(component) = aNext;
        }

        ctrl::TimeLineUtil::assignBlurKeyData(*mProject, *mTarget, frame, newData);
    }

    void KeyAccessor::assignWave(const core::WaveKey::Data& aData, int aComponent) {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Wave)) {
            core::TimeLine& line = currline();
            const float values[5] = {
                aData.strength(), aData.wavelength(), aData.angleDeg(), aData.phase(), aData.mix()
            };
            bool keyed = false;
            for (int c = 0; c < 5; ++c) keyed = keyed || line.hasChannelKeys(core::TimeKeyType_Wave, c);
            if (!keyed) {
                float previous[5] = {};
                for (int c = 0; c < 5; ++c) previous[c] = line.channelBaseValue(core::TimeKeyType_Wave, c);
                const core::WaveKey::Data previousData = line.current().wave();
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update wave"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                notifier->event().pushBaseValueTarget(*mTarget, core::TimeKeyType_Wave);
                macro.grabListener(notifier);
                core::TimeLine* live = &line;
                mProject->commandStack().push(new cmnd::Delegatable(
                    [live, values]() {
                        for (int c = 0; c < 5; ++c) live->setChannelBaseValue(core::TimeKeyType_Wave, c, values[c]);
                    },
                    [live, previous]() {
                        for (int c = 0; c < 5; ++c) live->setChannelBaseValue(core::TimeKeyType_Wave, c, previous[c]);
                    }
                ));
                return;
            }
            const int frame = getFrame();
            const core::WaveKey::Data current = line.current().wave();
            const float seed[5] = {
                current.strength(), current.wavelength(), current.angleDeg(), current.phase(), current.mix()
            };
            const bool whole = aComponent < 0 || line.frameEditIsLinked(core::TimeKeyType_Wave, frame);
            if (whole) {
                core::ChannelKey* keys[5] = {};
                for (int c = 0; c < 5; ++c) {
                    keys[c] = ensureChannelKey(
                        *mProject, *mTarget, core::TimeKeyType_Wave, c, frame, seed[c]
                    );
                    if (!keys[c]) return;
                }
                cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update wave key"));
                auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
                notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
                for (int c = 0; c < 5; ++c) {
                    notifier->event().pushTarget(
                        *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Wave, frame, c)
                    );
                }
                macro.grabListener(notifier);
                for (int c = 0; c < 5; ++c) {
                    mProject->commandStack().push(
                        new cmnd::Assign<float>(&keys[c]->valueRef(), values[c])
                    );
                }
                return;
            }
            const int component = qBound(0, aComponent, 4);
            core::ChannelKey* key = ensureChannelKey(
                *mProject, *mTarget, core::TimeKeyType_Wave, component, frame, seed[component], false
            );
            if (!key) return;
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Update wave channel key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_ChangeKeyValue);
            notifier->event().pushTarget(
                *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Wave, frame, component)
            );
            macro.grabListener(notifier);
            mProject->commandStack().push(new cmnd::Assign<float>(&key->valueRef(), values[component]));
            return;
        }

        const int frame = editFrame<core::WaveKey, core::TimeKeyType_Wave>(currline(), getFrame());
        if (frame != core::TimeLine::kBaseValueIndex) {
            ensureKey<core::WaveKey, core::TimeKeyType_Wave>(*mProject, *mTarget, frame);
        }
        ctrl::TimeLineUtil::assignWaveKeyData(*mProject, *mTarget, frame, aData);
    }

    core::WaveKey::Data KeyAccessor::waveData() const {
        if (!isValid()) {
            return core::WaveKey::Data();
        }
        const int frame = getFrame();
        const core::TimeLine& timeLine = *mTarget->timeLine();
        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Wave)) {
            core::WaveKey::Data data = timeLine.current().wave();
            bool exact = false;
            for (int c = 0; c < 5; ++c) {
                exact = exact || timeLine.hasChannelKey(core::TimeKeyType_Wave, c, frame);
            }
            if (exact) {
                const auto* s0 = timeLine.channelKey(core::TimeKeyType_Wave, 0, frame);
                const auto* s1 = timeLine.channelKey(core::TimeKeyType_Wave, 1, frame);
                const auto* s2 = timeLine.channelKey(core::TimeKeyType_Wave, 2, frame);
                const auto* s3 = timeLine.channelKey(core::TimeKeyType_Wave, 3, frame);
                const auto* s4 = timeLine.channelKey(core::TimeKeyType_Wave, 4, frame);
                data.setStrength(s0 ? s0->value() : data.strength());
                data.setWavelength(s1 ? s1->value() : data.wavelength());
                data.setAngleDeg(s2 ? s2->value() : data.angleDeg());
                data.setPhase(s3 ? s3->value() : data.phase());
                data.setMix(s4 ? s4->value() : data.mix());
            }
            if (const core::PropertyTrack* prop = timeLine.property(core::TimeKeyType_Wave)) {
                if (const auto* state = static_cast<const core::WaveStateKey*>(
                        prop->state().sampleState(frame))) {
                    data.setSpeed(state->speed());
                    data.setCrest(state->crest());
                    data.setDualAngleDeg(state->dualAngleDeg());
                    data.setDualScale(state->dualScale());
                    data.setTimeOffset(state->timeOffset());
                    data.setDeform(state->deform());
                }
            }
            return data;
        }
        if (!timeLine.hasTimeKey(core::TimeKeyType_Wave, frame)) {
            // Between keys the played value is the blended one; editing a field
            // there must keep the fields it does not name (the same rule
            // blurData() follows).
            return timeLine.current().wave();
        }
        return getKeyData<core::WaveKey, core::TimeKeyType_Wave>(*mTarget, frame);
    }

    void KeyAccessor::knockNewWave() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Wave)) {
            core::TimeLine& line = currline();
            const int frame = getFrame();
            if (line.hasChannelKey(core::TimeKeyType_Wave, 0, frame)) return;
            const core::WaveKey::Data data = line.current().wave();
            const float values[5] = {
                data.strength(), data.wavelength(), data.angleDeg(), data.phase(), data.mix()
            };
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new wave key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            for (int c = 0; c < 5; ++c) {
                notifier->event().pushTarget(*mTarget, core::TimeKeyPos(line, core::TimeKeyType_Wave, frame, c));
            }
            macro.grabListener(notifier);
            for (int c = 0; c < 5; ++c) {
                auto* key = new core::ChannelKey(core::TimeKeyType_Wave, c);
                key->setFrame(frame);
                key->setValue(values[c]);
                key->easing() = util::Easing::defaultParam();
                mProject->commandStack().push(new cmnd::GrabNewObject<core::ChannelKey>(key));
                mProject->commandStack().push(line.createChannelPusher(core::TimeKeyType_Wave, c, frame, key));
            }
            return;
        }

        auto newKey = new core::WaveKey();
        newKey->data() = currline().current().wave();
        assignParam(newKey);
        ctrl::TimeLineUtil::pushNewWaveKey(*mProject, *mTarget, getFrame(), newKey);
    }

    void KeyAccessor::knockNewBlur() {
        ASSERT_AND_RETURN_INVALID_TARGET();

        if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Blur)) {
            core::TimeLine& line = currline();
            const int frame = getFrame();
            if (line.hasChannelKey(core::TimeKeyType_Blur, 0, frame)) {
                return;
            }
            const float values[3] = {
                line.current().blurX(), line.current().blurY(), line.current().angleDeg()
            };
            cmnd::ScopedMacro macro(mProject->commandStack(), QStringLiteral("Add new blur key"));
            auto* notifier = new ctrl::TimeLineUtil::Notifier(*mProject);
            notifier->event().setType(core::TimeLineEvent::Type_PushKey);
            for (int c = 0; c < 3; ++c) {
                notifier->event().pushTarget(
                    *mTarget, core::TimeKeyPos(line, core::TimeKeyType_Blur, frame, c)
                );
            }
            macro.grabListener(notifier);
            for (int c = 0; c < 3; ++c) {
                auto* key = new core::ChannelKey(core::TimeKeyType_Blur, c);
                key->setFrame(frame);
                key->setValue(values[c]);
                key->easing() = util::Easing::defaultParam();
                mProject->commandStack().push(new cmnd::GrabNewObject<core::ChannelKey>(key));
                mProject->commandStack().push(
                    line.createChannelPusher(core::TimeKeyType_Blur, c, frame, key)
                );
            }
            return;
        }

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
