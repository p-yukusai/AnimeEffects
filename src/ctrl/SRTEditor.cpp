#include "core/TimeKeyExpans.h"
#include "core/MoveStateKey.h"
#include "core/ObjectNodeUtil.h"
#include "ctrl/SRTEditor.h"
#include "ctrl/srt/srt_MoveMode.h"
#include "ctrl/srt/srt_CentroidMode.h"

using namespace core;

namespace ctrl {


//-----------------------------------------------------------------------------------
SRTEditor::SRTEditor(Project& aProject, UILogger& aUILogger):
    mProject(aProject), mLifeLink(), mUILogger(aUILogger), mParam(), mTarget(), mKeyOwner() {}

SRTEditor::~SRTEditor() { finalize(); }

bool SRTEditor::initializeKey(TimeLine& aLine, QString* aMessage) {
    TimeKeyExpans& current = aLine.current();
    const int frame = mProject.animator().currentFrame().get();

    // A PROPERTY WITH NO EXPLICIT KEYS IS STATIC. Editing it writes the BASE VALUE,
    // exactly as the inspector's KeyAccessor::editFrame does; only an animated channel
    // (or a checked "Create ... key" box) gets a new key at the playhead. The old code
    // treated every missing frame key as owned and pushed it on the first drag, which
    // is the "it creates a key for no reason" report.
    const auto keyFor = [&](TimeKeyType aType, bool aForce, bool& aOwns, bool& aIsDefault) -> TimeKey* {
        const TimeLine::MapType& map = aLine.map(aType);
        const bool atFrame = map.contains(frame);
        const bool animated = !map.isEmpty();
        aIsDefault = false;
        aOwns = !atFrame && (animated || aForce);
        if (atFrame)
            return map.value(frame);
        if (aOwns)
            return nullptr; // the caller seeds its typed key from the current transform
        aIsDefault = true;
        switch (aType) {
        case TimeKeyType_Move: return getOrCreateBaseValue<MoveKey, TimeKeyType_Move>(aLine);
        case TimeKeyType_Rotate: return getOrCreateBaseValue<RotateKey, TimeKeyType_Rotate>(aLine);
        case TimeKeyType_Scale: return getOrCreateBaseValue<ScaleKey, TimeKeyType_Scale>(aLine);
        default: return nullptr;
        }
    };

    // moveKey
    mKeyOwner.isBound = aLine.current().bone().isUnderOfBinding() || aLine.working().bone().isUnderOfBinding();
    if (mKeyOwner.isBound) {
        if (aMessage) {
            *aMessage = UILog::tr("The transformed object has one or more bones bound to it, SRT keys may not work as expected.");
            mUILogger.pushLog(UILog::tr("SRT Editor : ") + *aMessage, UILogType_Warn);
        }
    }

    if (TimeLine::usesChannelTracks(TimeKeyType_Move)) {
        const ChannelKey* keys[4] = {
            aLine.channelKey(TimeKeyType_Move, 0, frame),
            aLine.channelKey(TimeKeyType_Move, 1, frame),
            aLine.channelKey(TimeKeyType_Move, 2, frame),
            aLine.channelKey(TimeKeyType_Move, 3, frame),
        };
        const bool atFrame = keys[0] && keys[1] && keys[2] && keys[3];
        bool animated = false;
        for (int c = 0; c < 4; ++c) {
            animated = animated || aLine.hasChannelKeys(TimeKeyType_Move, c);
        }

        mKeyOwner.moveIsTrack = true;
        mKeyOwner.moveTrackProxy = true;
        mKeyOwner.ownsMoveKey = !atFrame && (animated || mParam.necessarilyMove);
        mKeyOwner.moveIsBaseValue = !atFrame && !mKeyOwner.ownsMoveKey;

        auto* proxy = new MoveKey();
        QVector2D pos = current.srt().pos();
        QVector2D centroid = current.srt().centroid();
        if (atFrame) {
            pos = QVector2D(keys[0]->value(), keys[1]->value());
            centroid = QVector2D(keys[2]->value(), keys[3]->value());
        } else if (mKeyOwner.moveIsBaseValue) {
            pos = QVector2D(
                aLine.channelBaseValue(TimeKeyType_Move, 0),
                aLine.channelBaseValue(TimeKeyType_Move, 1)
            );
            centroid = QVector2D(
                aLine.channelBaseValue(TimeKeyType_Move, 2),
                aLine.channelBaseValue(TimeKeyType_Move, 3)
            );
        }
        proxy->setPos(pos);
        proxy->setCentroid(centroid);
        const PropertyTrack* prop = aLine.property(TimeKeyType_Move);
        if (prop) {
            if (const auto* state = static_cast<const MoveStateKey*>(prop->state().sampleState(frame))) {
                proxy->data().setSpline(state->spline());
            }
        }
        mKeyOwner.moveKey = proxy;
    } else {
        TimeKey* base = keyFor(TimeKeyType_Move, mParam.necessarilyMove, mKeyOwner.ownsMoveKey,
                               mKeyOwner.moveIsBaseValue);
        if (mKeyOwner.ownsMoveKey) {
            mKeyOwner.moveKey = new MoveKey();
            const util::Easing::Param aParam = util::Easing::defaultParam();
            for (int component = 0; component < MoveKey::kComponents; ++component) {
                mKeyOwner.moveKey->data().easing(component) = aParam;
            }
            mKeyOwner.moveKey->setPos(current.srt().pos());
            mKeyOwner.moveKey->setCentroid(current.srt().centroid());
        } else {
            mKeyOwner.moveKey = static_cast<MoveKey*>(base);
        }
        mKeyOwner.moveIsTrack = false;
        mKeyOwner.moveTrackProxy = false;
    }

    // rotateKey
    if (TimeLine::usesChannelTracks(TimeKeyType_Rotate)) {
        ChannelTrack* track = aLine.channelTrack(TimeKeyType_Rotate, 0);
        const bool atFrame = aLine.hasChannelKey(TimeKeyType_Rotate, 0, frame);
        const bool animated = aLine.hasChannelKeys(TimeKeyType_Rotate, 0);

        mKeyOwner.rotateIsTrack = true;
        mKeyOwner.rotateTrackProxy = true;
        mKeyOwner.ownsRotateKey = !atFrame && (animated || mParam.necessarilyRotate);
        mKeyOwner.rotateIsBaseValue = !atFrame && !mKeyOwner.ownsRotateKey;

        auto* proxy = new RotateKey();
        float value = current.srt().rotate();
        if (atFrame) {
            value = aLine.channelKey(TimeKeyType_Rotate, 0, frame)->value();
        } else if (mKeyOwner.rotateIsBaseValue && track) {
            value = track->baseValue();
        }
        proxy->setRotate(value);
        mKeyOwner.rotateKey = proxy;
    } else {
        TimeKey* base = keyFor(TimeKeyType_Rotate, mParam.necessarilyRotate, mKeyOwner.ownsRotateKey,
                               mKeyOwner.rotateIsBaseValue);
        if (mKeyOwner.ownsRotateKey) {
            mKeyOwner.rotateKey = new RotateKey();
            const util::Easing::Param aParam = util::Easing::defaultParam();
            for (int component = 0; component < RotateKey::kComponents; ++component) {
                mKeyOwner.rotateKey->data().easing(component) = aParam;
            }
            mKeyOwner.rotateKey->setRotate(current.srt().rotate());
        } else {
            mKeyOwner.rotateKey = static_cast<RotateKey*>(base);
        }
        mKeyOwner.rotateIsTrack = false;
        mKeyOwner.rotateTrackProxy = false;
    }

    // scaleKey
    if (TimeLine::usesChannelTracks(TimeKeyType_Scale)) {
        const ChannelKey* x = aLine.channelKey(TimeKeyType_Scale, 0, frame);
        const ChannelKey* y = aLine.channelKey(TimeKeyType_Scale, 1, frame);
        const bool atFrame = x && y;
        const bool animated = aLine.hasChannelKeys(TimeKeyType_Scale, 0)
            || aLine.hasChannelKeys(TimeKeyType_Scale, 1);

        mKeyOwner.scaleIsTrack = true;
        mKeyOwner.scaleTrackProxy = true;
        mKeyOwner.ownsScaleKey = !atFrame && (animated || mParam.necessarilyScale);
        mKeyOwner.scaleIsBaseValue = !atFrame && !mKeyOwner.ownsScaleKey;

        auto* proxy = new ScaleKey();
        QVector2D value = current.srt().scale();
        if (atFrame) {
            value = QVector2D(x->value(), y->value());
        } else if (mKeyOwner.scaleIsBaseValue) {
            value = QVector2D(
                aLine.channelBaseValue(TimeKeyType_Scale, 0),
                aLine.channelBaseValue(TimeKeyType_Scale, 1)
            );
        }
        proxy->setScale(value);
        mKeyOwner.scaleKey = proxy;
    } else {
        TimeKey* base = keyFor(TimeKeyType_Scale, mParam.necessarilyScale, mKeyOwner.ownsScaleKey,
                               mKeyOwner.scaleIsBaseValue);
        if (mKeyOwner.ownsScaleKey) {
            mKeyOwner.scaleKey = new ScaleKey();
            const util::Easing::Param aParam = util::Easing::defaultParam();
            for (int component = 0; component < ScaleKey::kComponents; ++component) {
                mKeyOwner.scaleKey->data().easing(component) = aParam;
            }
            mKeyOwner.scaleKey->setScale(current.srt().scale());
        } else {
            mKeyOwner.scaleKey = static_cast<ScaleKey*>(base);
        }
        mKeyOwner.scaleIsTrack = false;
        mKeyOwner.scaleTrackProxy = false;
    }

    // check validity
    XC_ASSERT(mKeyOwner);

    // setup matrix
    if (!mKeyOwner.updatePosture(current)) {
        mKeyOwner.deleteOwningKeys();
        if (aMessage) {
            *aMessage = UILog::tr("An object with invalid posture was given.");
        }
        return false;
    }

    return true;
}

void SRTEditor::finalize() {
    mCurrent.reset();
    mKeyOwner.deleteOwningKeys();
}

void SRTEditor::createMode() {
    mCurrent.reset();

    if (!mTarget || !mKeyOwner)
        return;

    switch (mParam.mode) {
    case 0:
        mCurrent.reset(new srt::MoveMode(mProject, *mTarget, mKeyOwner));
        break;
    case 1:
        mCurrent.reset(new srt::CentroidMode(mProject, *mTarget, mKeyOwner));
        break;
    default:
        break;
    }

    if (mCurrent) {
        mCurrent->updateParam(mParam);
    }
}

bool SRTEditor::setTarget(ObjectNode* aTarget) {
    finalize();

    mTarget = nullptr;

    if (aTarget && aTarget->timeLine()) {
        mTarget = aTarget;
        QString message;
        if (initializeKey(*mTarget->timeLine(), &message)) {
            createMode();
        } else {
            mTarget = nullptr;
            if (!message.isEmpty()) {
                mUILogger.pushLog(UILog::tr("SRT Editor : ") + message, UILogType_Warn);
            }
        }
    }
    return mTarget && mKeyOwner;
}

void SRTEditor::updateParam(const SRTParam& aParam) {
    const SRTParam prev = mParam;
    mParam = aParam;

    // The three create-key toggles decide whether a missing frame key is an owned
    // key or the static default, so changing one while the tool is up must rebuild
    // the key state. A mode change already rebuilt it.
    if (prev.mode != mParam.mode || prev.necessarilyMove != mParam.necessarilyMove
        || prev.necessarilyRotate != mParam.necessarilyRotate
        || prev.necessarilyScale != mParam.necessarilyScale) {
        resetCurrentTarget();
    }

    if (mCurrent) {
        mCurrent->updateParam(mParam);
    }
}

bool SRTEditor::updateCursor(const CameraInfo& aCamera, const AbstractCursor& aCursor) {
    if (mCurrent) {
        return mCurrent->updateCursor(aCamera, aCursor);
    }
    return false;
}

void SRTEditor::updateEvent(EventType) { resetCurrentTarget(); }

void SRTEditor::resetCurrentTarget() {
    finalize();

    if (mTarget) {
        initializeKey(*mTarget->timeLine());
        createMode();
    }
}

void SRTEditor::renderQt(const RenderInfo& aInfo, QPainter& aPainter) {
    if (mCurrent) {
        return mCurrent->renderQt(aInfo, aPainter);
    }
}

} // namespace ctrl
