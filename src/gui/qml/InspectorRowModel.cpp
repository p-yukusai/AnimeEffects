#include "gui/qml/InspectorRowModel.h"

#include "cmnd/BasicCommands.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/Stack.h"
#include "core/ChannelKey.h"
#include "core/TimeKeyPos.h"
#include "ctrl/TimeLineUtil.h"

#include <QCoreApplication>
#include <QVector2D>

#include "core/Constant.h"
#include "core/DepthKey.h"
#include "core/FFDKey.h"
#include "core/ImageKey.h"
#include "core/MoveKey.h"
#include "core/MoveStateKey.h"
#include "core/OpacityKey.h"
#include "core/PoseKey.h"
#include "core/RotateKey.h"
#include "core/ScaleKey.h"
#include "core/TimeKeyExpans.h"
#include "core/TimeLine.h"
#include "gui/qml/InspectorViewModel.h"
#include "gui/theme/Colors.h"
#include "util/MathUtil.h"

namespace gui {

InspectorRowModel::InspectorRowModel(Kind aKind, InspectorViewModel& aOwner, QObject* aParent):
    QObject(aParent), mOwner(aOwner), mKind(aKind) {}

QString InspectorRowModel::kind() const {
    switch (mKind) {
    case Kind::Move: return QStringLiteral("move");
    case Kind::Rotate: return QStringLiteral("rotate");
    case Kind::Scale: return QStringLiteral("scale");
    case Kind::Depth: return QStringLiteral("depth");
    case Kind::Opacity: return QStringLiteral("opacity");
    case Kind::Image: return QStringLiteral("image");
    case Kind::Pose: return QStringLiteral("pose");
    case Kind::FFD: return QStringLiteral("ffd");
    }
    return QString();
}

QString InspectorRowModel::label() const {
    switch (mKind) {
    // Existing contexts: the inspector's labels keep the translations the
    // widget groups already had instead of introducing a second English copy.
    case Kind::Move: return QCoreApplication::translate("gui::prop::MoveKeyGroup", "Position");
    case Kind::Rotate: return QCoreApplication::translate("gui::prop::RotateKeyGroup", "Rotation");
    case Kind::Scale: return QCoreApplication::translate("gui::prop::ScaleKeyGroup", "Scale");
    case Kind::Depth: return QCoreApplication::translate("gui::prop::DepthKeyGroup", "Depth");
    case Kind::Opacity: return QCoreApplication::translate("gui::prop::OpacityKeyGroup", "Opacity");
    case Kind::Image: return QCoreApplication::translate("gui::prop::ImageKeyGroup", "Image");
    case Kind::Pose: return QCoreApplication::translate("gui::prop::PoseKeyGroup", "Pose");
    case Kind::FFD: return QCoreApplication::translate("gui::prop::FFDKeyGroup", "FFD");
    }
    return QString();
}

core::TimeKeyType InspectorRowModel::type() const {
    switch (mKind) {
    case Kind::Move: return core::TimeKeyType_Move;
    case Kind::Rotate: return core::TimeKeyType_Rotate;
    case Kind::Scale: return core::TimeKeyType_Scale;
    case Kind::Depth: return core::TimeKeyType_Depth;
    case Kind::Opacity: return core::TimeKeyType_Opacity;
    case Kind::Image: return core::TimeKeyType_Image;
    case Kind::Pose: return core::TimeKeyType_Pose;
    case Kind::FFD: return core::TimeKeyType_FFD;
    }
    return core::TimeKeyType_TERM;
}

void InspectorRowModel::readMove(core::TimeLine& aLine, int aFrame) {
    if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Move)) {
        const auto* x = aLine.channelKey(core::TimeKeyType_Move, 0, aFrame);
        const auto* y = aLine.channelKey(core::TimeKeyType_Move, 1, aFrame);
        const auto* cx = aLine.channelKey(core::TimeKeyType_Move, 2, aFrame);
        const auto* cy = aLine.channelKey(core::TimeKeyType_Move, 3, aFrame);
        if (x || y || cx || cy) {
            const auto& srt = aLine.current().srt();
            mX = x ? x->value() : srt.pos().x();
            mY = y ? y->value() : srt.pos().y();
            mCentroidX = cx ? cx->value() : srt.centroid().x();
            mCentroidY = cy ? cy->value() : srt.centroid().y();
        } else if (aLine.hasChannelKeys(core::TimeKeyType_Move, 0)
                   || aLine.hasChannelKeys(core::TimeKeyType_Move, 1)) {
            const auto& srt = aLine.current().srt();
            mX = srt.pos().x();
            mY = srt.pos().y();
            mCentroidX = srt.centroid().x();
            mCentroidY = srt.centroid().y();
        } else {
            mX = aLine.channelBaseValue(core::TimeKeyType_Move, 0);
            mY = aLine.channelBaseValue(core::TimeKeyType_Move, 1);
            mCentroidX = aLine.channelBaseValue(core::TimeKeyType_Move, 2);
            mCentroidY = aLine.channelBaseValue(core::TimeKeyType_Move, 3);
        }
        const core::PropertyTrack* prop = aLine.property(core::TimeKeyType_Move);
        const auto* state = prop
            ? static_cast<const core::MoveStateKey*>(prop->state().sampleState(aFrame))
            : nullptr;
        mSmooth = state && state->spline() == core::MoveKey::SplineType_CatmullRom;
        return;
    }

    const core::MoveKey::Data* data = nullptr;
    if (const auto* key = aLine.timeKey(core::TimeKeyType_Move, aFrame)) {
        data = &static_cast<const core::MoveKey*>(key)->data();
    } else if (!aLine.map(core::TimeKeyType_Move).isEmpty()) {
        if (const auto* def = aLine.baseValue(core::TimeKeyType_Move)) {
            mSmooth = static_cast<const core::MoveKey*>(def)->data().spline() == core::MoveKey::SplineType_CatmullRom;
        }
        const auto& srt = aLine.current().srt();
        mX = srt.pos().x();
        mY = srt.pos().y();
        mCentroidX = srt.centroid().x();
        mCentroidY = srt.centroid().y();
        return;
    } else if (const auto* def = aLine.baseValue(core::TimeKeyType_Move)) {
        data = &static_cast<const core::MoveKey*>(def)->data();
    }

    if (data) {
        mSmooth = data->spline() == core::MoveKey::SplineType_CatmullRom;
        mX = data->pos().x();
        mY = data->pos().y();
        mCentroidX = data->centroid().x();
        mCentroidY = data->centroid().y();
    }
}

void InspectorRowModel::readImage(core::TimeLine& aLine, int aFrame) {
    const core::ImageKey* key = nullptr;
    if (const auto* exact = aLine.timeKey(core::TimeKeyType_Image, aFrame)) {
        key = static_cast<const core::ImageKey*>(exact);
    } else if (!aLine.map(core::TimeKeyType_Image).isEmpty()) {
        key = aLine.current().areaImageKey();
    } else if (const auto* def = aLine.baseValue(core::TimeKeyType_Image)) {
        key = static_cast<const core::ImageKey*>(def);
    }
    if (!key)
        return;

    const core::ImageKey::Data& data = key->data();
    mAssetName = data.resource() ? data.resource()->identifier() : QString();
    QVector2D offset = data.imageOffset();
    if (const core::PropertyTrack* prop = aLine.property(core::TimeKeyType_Image)) {
        const auto valueAt = [&](int aComponent, float aFallback) {
            const core::ChannelTrack* track = prop->channel(aComponent);
            if (!track) {
                return aFallback;
            }
            const core::ChannelKey* before = track->keyBeforeOrAt(aFrame);
            return before ? before->value() : track->baseValue();
        };
        offset.setX(valueAt(0, offset.x()));
        offset.setY(valueAt(1, offset.y()));
    }
    mCenterX = -offset.x();
    mCenterY = -offset.y();
    mCellSize = data.gridMesh().cellSize();
}

void InspectorRowModel::refresh() {
    core::TimeLine* line = mOwner.timeLine();
    const bool baseVisible = line != nullptr;
    bool visible = baseVisible;
    const int frame = mOwner.frame();

    if (line) {
        switch (mKind) {
        case Kind::Image:
            visible = mOwner.target()->hasAnyImage()
                || line->hasTimeKey(core::TimeKeyType_Image, frame);
            break;
        case Kind::Pose:
            visible = line->current().bone().areaKey() != nullptr
                || line->hasTimeKey(core::TimeKeyType_Pose, frame);
            break;
        case Kind::FFD:
            visible = mOwner.target()->hasAnyMesh()
                || line->hasTimeKey(core::TimeKeyType_FFD, frame);
            break;
        default:
            break;
        }
    }

    const bool enabled = baseVisible && !mOwner.playbackActive();
    const bool keyEnabled = enabled;
    QString keyState;
    QColor keyColor;
    if (line) {
        const core::TimeKeyType keyType = type();
        if (core::TimeLine::usesChannelTracks(keyType)) {
            if (!line->hasChannelKeys(keyType, 0))
                keyState = QStringLiteral("static");
            else if (line->hasChannelKey(keyType, 0, frame))
                keyState = QStringLiteral("keyed");
            else
                keyState = QStringLiteral("animated");
        } else {
            if (line->map(keyType).isEmpty())
                keyState = QStringLiteral("static");
            else if (line->hasTimeKey(keyType, frame))
                keyState = QStringLiteral("keyed");
            else
                keyState = QStringLiteral("animated");
        }
        keyColor = theme::keyTypeColor(theme::Colors::current(), keyType);
    }

    const QVariantList valuesBefore{
        mX, mY, mCentroidX, mCentroidY, mSmooth, mAngle, mScaleX, mScaleY,
        mDepth, mOpacity, mAssetName, mCenterX, mCenterY, mCellSize
    };
    bool dirty = false;
    const auto set = [&dirty](auto& aTarget, const auto& aValue) {
        if (aTarget == aValue)
            return;
        aTarget = aValue;
        dirty = true;
    };
    set(mVisible, visible);
    set(mEnabled, enabled);
    set(mKeyEnabled, keyEnabled);
    set(mKeyState, keyState);
    set(mKeyColor, keyColor);

    if (line) {
        switch (mKind) {
        case Kind::Move:
            readMove(*line, frame);
            break;
        case Kind::Rotate: {
            if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Rotate)) {
                if (const core::ChannelKey* key =
                        line->channelKey(core::TimeKeyType_Rotate, 0, frame)) {
                    mAngle = util::MathUtil::getDegreeFromRadian(key->value());
                } else if (line->hasChannelKeys(core::TimeKeyType_Rotate, 0)) {
                    mAngle = util::MathUtil::getDegreeFromRadian(line->current().srt().rotate());
                } else {
                    mAngle = util::MathUtil::getDegreeFromRadian(
                        line->channelBaseValue(core::TimeKeyType_Rotate, 0)
                    );
                }
                break;
            }

            const core::RotateKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Rotate, frame))
                data = &static_cast<const core::RotateKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Rotate).isEmpty())
                mAngle = util::MathUtil::getDegreeFromRadian(line->current().srt().rotate());
            else if (const auto* def = line->baseValue(core::TimeKeyType_Rotate))
                data = &static_cast<const core::RotateKey*>(def)->data();
            if (data)
                mAngle = util::MathUtil::getDegreeFromRadian(data->rotate());
            break;
        }
        case Kind::Scale: {
            if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Scale)) {
                const core::ChannelKey* x = line->channelKey(core::TimeKeyType_Scale, 0, frame);
                const core::ChannelKey* y = line->channelKey(core::TimeKeyType_Scale, 1, frame);
                if (x || y) {
                    mScaleX = x ? x->value() : line->current().srt().scale().x();
                    mScaleY = y ? y->value() : line->current().srt().scale().y();
                } else if (line->hasChannelKeys(core::TimeKeyType_Scale, 0)
                           || line->hasChannelKeys(core::TimeKeyType_Scale, 1)) {
                    const QVector2D scale = line->current().srt().scale();
                    mScaleX = scale.x();
                    mScaleY = scale.y();
                } else {
                    mScaleX = line->channelBaseValue(core::TimeKeyType_Scale, 0);
                    mScaleY = line->channelBaseValue(core::TimeKeyType_Scale, 1);
                }
                break;
            }

            const core::ScaleKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Scale, frame))
                data = &static_cast<const core::ScaleKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Scale).isEmpty()) {
                const QVector2D scale = line->current().srt().scale();
                mScaleX = scale.x();
                mScaleY = scale.y();
            } else if (const auto* def = line->baseValue(core::TimeKeyType_Scale)) {
                data = &static_cast<const core::ScaleKey*>(def)->data();
            }
            if (data) {
                mScaleX = data->scale().x();
                mScaleY = data->scale().y();
            }
            break;
        }
        case Kind::Depth: {
            if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Depth)) {
                if (const core::ChannelKey* key =
                        line->channelKey(core::TimeKeyType_Depth, 0, frame)) {
                    mDepth = key->value();
                } else if (line->hasChannelKeys(core::TimeKeyType_Depth, 0)) {
                    mDepth = line->current().depth();
                } else {
                    mDepth = line->channelBaseValue(core::TimeKeyType_Depth, 0);
                }
                break;
            }

            const core::DepthKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Depth, frame))
                data = &static_cast<const core::DepthKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Depth).isEmpty())
                mDepth = line->current().depth();
            else if (const auto* def = line->baseValue(core::TimeKeyType_Depth))
                data = &static_cast<const core::DepthKey*>(def)->data();
            if (data)
                mDepth = data->depth();
            break;
        }
        case Kind::Opacity: {
            if (core::TimeLine::usesChannelTracks(core::TimeKeyType_Opacity)) {
                if (const core::ChannelKey* key =
                        line->channelKey(core::TimeKeyType_Opacity, 0, frame)) {
                    mOpacity = key->value();
                } else if (line->hasChannelKeys(core::TimeKeyType_Opacity, 0)) {
                    mOpacity = line->current().opacity().opacity();
                } else {
                    mOpacity = line->channelBaseValue(core::TimeKeyType_Opacity, 0);
                }
                break;
            }

            const core::OpacityKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Opacity, frame))
                data = &static_cast<const core::OpacityKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Opacity).isEmpty())
                mOpacity = line->current().opacity().opacity();
            else if (const auto* def = line->baseValue(core::TimeKeyType_Opacity))
                data = &static_cast<const core::OpacityKey*>(def)->data();
            if (data)
                mOpacity = data->opacity();
            break;
        }
        case Kind::Image:
            readImage(*line, frame);
            break;
        case Kind::Pose:
        case Kind::FFD:
            break;
        }
    }

    const QVariantList valuesAfter{
        mX, mY, mCentroidX, mCentroidY, mSmooth, mAngle, mScaleX, mScaleY,
        mDepth, mOpacity, mAssetName, mCenterX, mCenterY, mCellSize
    };
    if (valuesBefore != valuesAfter)
        dirty = true;

    if (dirty)
        emit changed();
}

void InspectorRowModel::refreshTheme() {
    core::TimeLine* line = mOwner.timeLine();
    const QColor color = line ? theme::keyTypeColor(theme::Colors::current(), type()) : QColor();
    if (color != mKeyColor) {
        mKeyColor = color;
        emit changed();
    }
}

void InspectorRowModel::knock() {
    switch (mKind) {
    case Kind::Move: mOwner.accessor().knockNewMove(); break;
    case Kind::Rotate: mOwner.accessor().knockNewRotate(); break;
    case Kind::Scale: mOwner.accessor().knockNewScale(); break;
    case Kind::Depth: mOwner.accessor().knockNewDepth(); break;
    case Kind::Opacity: mOwner.accessor().knockNewOpacity(); break;
    case Kind::Image: mOwner.addImageKey(); break;
    case Kind::Pose: mOwner.accessor().knockNewPose(); break;
    case Kind::FFD: mOwner.accessor().knockNewFFD(); break;
    }
}

bool InspectorRowModel::channelKeyExists(int aComponent) const {
    if (!mOwner.hasTarget()
        || (!core::TimeLine::usesChannelTracks(type()) && type() != core::TimeKeyType_Image)) {
        return false;
    }
    return mOwner.timeLine()->hasChannelKey(type(), aComponent, mOwner.frame());
}

QString InspectorRowModel::channelKeyAction(int aComponent) const {
    if (!mOwner.hasTarget()
        || (!core::TimeLine::usesChannelTracks(type()) && type() != core::TimeKeyType_Image)) {
        return QString();
    }
    QString name;
    switch (mKind) {
    case Kind::Move:
        name = aComponent < 2 ? (aComponent == 0 ? QStringLiteral("X") : QStringLiteral("Y"))
                              : (aComponent == 2 ? QStringLiteral("Centroid X") : QStringLiteral("Centroid Y"));
        break;
    case Kind::Scale:
        name = aComponent == 0 ? QStringLiteral("X") : QStringLiteral("Y");
        break;
    case Kind::Rotate:
        name = QStringLiteral("Angle");
        break;
    case Kind::Depth:
        name = QStringLiteral("Depth");
        break;
    case Kind::Opacity:
        name = QStringLiteral("Opacity");
        break;
    case Kind::Image:
        name = aComponent == 0 ? QStringLiteral("Center X") : QStringLiteral("Center Y");
        break;
    default:
        return QString();
    }
    return channelKeyExists(aComponent) ? QStringLiteral("Remove %1 Key").arg(name)
                                        : QStringLiteral("Add %1 Key").arg(name);
}

float InspectorRowModel::channelValue(int aComponent) const {
    switch (mKind) {
    case Kind::Move:
        if (aComponent == 0) return float(mX);
        if (aComponent == 1) return float(mY);
        if (aComponent == 2) return float(mCentroidX);
        return float(mCentroidY);
    case Kind::Scale:
        return aComponent == 0 ? float(mScaleX) : float(mScaleY);
    case Kind::Rotate:
        return float(mAngle * 3.14159265358979323846 / 180.0);
    case Kind::Depth:
        return float(mDepth);
    case Kind::Opacity:
        return float(mOpacity);
    case Kind::Image:
        return aComponent == 0 ? float(-mCenterX) : float(-mCenterY);
    default:
        return 0.0f;
    }
}

void InspectorRowModel::toggleChannelKey(int aComponent) {
    if (!mOwner.hasTarget() || !mOwner.project()
        || (!core::TimeLine::usesChannelTracks(type()) && type() != core::TimeKeyType_Image)) {
        return;
    }
    core::TimeLine& line = *mOwner.timeLine();
    const int frame = mOwner.frame();
    const core::TimeKeyType keyType = type();
    if (aComponent < 0 || core::propertyChannelCount(keyType) <= aComponent) {
        return;
    }

    cmnd::Stack& stack = mOwner.project()->commandStack();
    cmnd::ScopedMacro macro(stack, QStringLiteral("Toggle channel key"));
    auto* notifier = new ctrl::TimeLineUtil::Notifier(*mOwner.project());
    const bool exists = line.hasChannelKey(keyType, aComponent, frame);
    notifier->event().setType(
        exists ? core::TimeLineEvent::Type_RemoveKey : core::TimeLineEvent::Type_PushKey
    );
    notifier->event().pushTarget(
        *mOwner.target(), core::TimeKeyPos(line, keyType, frame, aComponent)
    );
    macro.grabListener(notifier);

    if (exists) {
        stack.push(line.createChannelRemover(keyType, aComponent, frame, true));
    } else {
        auto* key = new core::ChannelKey(keyType, aComponent);
        key->setFrame(frame);
        core::ChannelTrack* track = line.channelTrack(keyType, aComponent);
        key->setValue(track ? track->clamp(track->quantize(channelValue(aComponent)))
                            : channelValue(aComponent));
        key->easing() = util::Easing::defaultParam();
        stack.push(new cmnd::GrabNewObject<core::ChannelKey>(key));
        stack.push(line.createChannelPusher(keyType, aComponent, frame, key));
    }
    refresh();
}

void InspectorRowModel::toggleKey() {
    if (!mOwner.hasTarget() || !mKeyEnabled)
        return;
    if (mKeyState == QLatin1String("keyed"))
        mOwner.accessor().removeKey(type());
    else
        knock();
    mOwner.refreshKeys();
}

void InspectorRowModel::browseAsset() { mOwner.browseImageAsset(); }

void InspectorRowModel::setX(double aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMovePosition(QVector2D(float(aValue), float(mY)), 0);
    mOwner.refreshKeys();
}

void InspectorRowModel::setY(double aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMovePosition(QVector2D(float(mX), float(aValue)), 1);
    mOwner.refreshKeys();
}

void InspectorRowModel::setCentroidX(double aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMoveCentroid(QVector2D(float(aValue), float(mCentroidY)), 2);
    mOwner.refreshKeys();
}

void InspectorRowModel::setCentroidY(double aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMoveCentroid(QVector2D(float(mCentroidX), float(aValue)), 3);
    mOwner.refreshKeys();
}

void InspectorRowModel::setSmooth(bool aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMoveSpline(aValue ? core::MoveKey::SplineType_CatmullRom
                                             : core::MoveKey::SplineType_Linear);
    mOwner.refreshKeys();
}

void InspectorRowModel::setAngle(double aValue) {
    if (mKind != Kind::Rotate || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignRotateAngle(util::MathUtil::getRadianFromDegree(aValue));
    mOwner.refreshKeys();
}

void InspectorRowModel::setScaleX(double aValue) {
    if (mKind != Kind::Scale || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignScaleRate(QVector2D(float(aValue), float(mScaleY)), 0);
    mOwner.refreshKeys();
}

void InspectorRowModel::setScaleY(double aValue) {
    if (mKind != Kind::Scale || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignScaleRate(QVector2D(float(mScaleX), float(aValue)), 1);
    mOwner.refreshKeys();
}

void InspectorRowModel::setDepth(double aValue) {
    if (mKind != Kind::Depth || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignDepthPosition(float(aValue));
    mOwner.refreshKeys();
}

void InspectorRowModel::setOpacity(double aValue) {
    if (mKind != Kind::Opacity || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignOpacity(float(qBound(0.0, aValue, 1.0)));
    mOwner.refreshKeys();
}

void InspectorRowModel::setCenterX(double aValue) {
    if (mKind != Kind::Image || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignImageOffset(QVector2D(float(-aValue), float(-mCenterY)), 0);
    mOwner.refreshKeys();
}

void InspectorRowModel::setCenterY(double aValue) {
    if (mKind != Kind::Image || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignImageOffset(QVector2D(float(-mCenterX), float(-aValue)), 1);
    mOwner.refreshKeys();
}

void InspectorRowModel::setCellSize(int aValue) {
    if (mKind != Kind::Image || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignImageCellSize(qBound(core::Constant::imageCellSizeMin(), aValue,
                                                 core::Constant::imageCellSizeMax()));
    mOwner.refreshKeys();
}

} // namespace gui
