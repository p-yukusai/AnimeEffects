#include "gui/qml/InspectorRowModel.h"

#include <QCoreApplication>
#include <QVector2D>

#include "core/Constant.h"
#include "core/DepthKey.h"
#include "core/FFDKey.h"
#include "core/ImageKey.h"
#include "core/MoveKey.h"
#include "core/OpaKey.h"
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
    case Kind::Opacity: return QCoreApplication::translate("gui::prop::OpaKeyGroup", "Opacity");
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
    case Kind::Opacity: return core::TimeKeyType_Opa;
    case Kind::Image: return core::TimeKeyType_Image;
    case Kind::Pose: return core::TimeKeyType_Pose;
    case Kind::FFD: return core::TimeKeyType_FFD;
    }
    return core::TimeKeyType_TERM;
}

void InspectorRowModel::readMove(core::TimeLine& aLine, int aFrame) {
    const core::MoveKey::Data* data = nullptr;
    if (const auto* key = aLine.timeKey(core::TimeKeyType_Move, aFrame)) {
        data = &static_cast<const core::MoveKey*>(key)->data();
    } else if (!aLine.map(core::TimeKeyType_Move).isEmpty()) {
        if (const auto* def = aLine.defaultKey(core::TimeKeyType_Move)) {
            mSmooth = static_cast<const core::MoveKey*>(def)->data().spline() == core::MoveKey::SplineType_CatmullRom;
        }
        const auto& srt = aLine.current().srt();
        mX = srt.pos().x();
        mY = srt.pos().y();
        mCentroidX = srt.centroid().x();
        mCentroidY = srt.centroid().y();
        return;
    } else if (const auto* def = aLine.defaultKey(core::TimeKeyType_Move)) {
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
    } else if (const auto* def = aLine.defaultKey(core::TimeKeyType_Image)) {
        key = static_cast<const core::ImageKey*>(def);
    }
    if (!key)
        return;

    const core::ImageKey::Data& data = key->data();
    mAssetName = data.resource() ? data.resource()->identifier() : QString();
    mCenterX = -data.imageOffset().x();
    mCenterY = -data.imageOffset().y();
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
        if (line->map(keyType).isEmpty())
            keyState = QStringLiteral("static");
        else if (line->hasTimeKey(keyType, frame))
            keyState = QStringLiteral("keyed");
        else
            keyState = QStringLiteral("animated");
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
            const core::RotateKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Rotate, frame))
                data = &static_cast<const core::RotateKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Rotate).isEmpty())
                mAngle = util::MathUtil::getDegreeFromRadian(line->current().srt().rotate());
            else if (const auto* def = line->defaultKey(core::TimeKeyType_Rotate))
                data = &static_cast<const core::RotateKey*>(def)->data();
            if (data)
                mAngle = util::MathUtil::getDegreeFromRadian(data->rotate());
            break;
        }
        case Kind::Scale: {
            const core::ScaleKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Scale, frame))
                data = &static_cast<const core::ScaleKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Scale).isEmpty()) {
                const QVector2D scale = line->current().srt().scale();
                mScaleX = scale.x();
                mScaleY = scale.y();
            } else if (const auto* def = line->defaultKey(core::TimeKeyType_Scale)) {
                data = &static_cast<const core::ScaleKey*>(def)->data();
            }
            if (data) {
                mScaleX = data->scale().x();
                mScaleY = data->scale().y();
            }
            break;
        }
        case Kind::Depth: {
            const core::DepthKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Depth, frame))
                data = &static_cast<const core::DepthKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Depth).isEmpty())
                mDepth = line->current().depth();
            else if (const auto* def = line->defaultKey(core::TimeKeyType_Depth))
                data = &static_cast<const core::DepthKey*>(def)->data();
            if (data)
                mDepth = data->depth();
            break;
        }
        case Kind::Opacity: {
            const core::OpaKey::Data* data = nullptr;
            if (const auto* key = line->timeKey(core::TimeKeyType_Opa, frame))
                data = &static_cast<const core::OpaKey*>(key)->data();
            else if (!line->map(core::TimeKeyType_Opa).isEmpty())
                mOpacity = line->current().opa().opacity();
            else if (const auto* def = line->defaultKey(core::TimeKeyType_Opa))
                data = &static_cast<const core::OpaKey*>(def)->data();
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
    mOwner.accessor().assignMovePosition(QVector2D(float(aValue), float(mY)));
    mOwner.refreshKeys();
}

void InspectorRowModel::setY(double aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMovePosition(QVector2D(float(mX), float(aValue)));
    mOwner.refreshKeys();
}

void InspectorRowModel::setCentroidX(double aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMoveCentroid(QVector2D(float(aValue), float(mCentroidY)));
    mOwner.refreshKeys();
}

void InspectorRowModel::setCentroidY(double aValue) {
    if (mKind != Kind::Move || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignMoveCentroid(QVector2D(float(mCentroidX), float(aValue)));
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
    mOwner.accessor().assignScaleRate(QVector2D(float(aValue), float(mScaleY)));
    mOwner.refreshKeys();
}

void InspectorRowModel::setScaleY(double aValue) {
    if (mKind != Kind::Scale || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignScaleRate(QVector2D(float(mScaleX), float(aValue)));
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
    mOwner.accessor().assignImageOffset(QVector2D(float(-aValue), float(-mCenterY)));
    mOwner.refreshKeys();
}

void InspectorRowModel::setCenterY(double aValue) {
    if (mKind != Kind::Image || !mOwner.hasTarget())
        return;
    mOwner.accessor().assignImageOffset(QVector2D(float(-mCenterX), float(-aValue)));
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
