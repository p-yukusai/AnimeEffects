#include "util/MathUtil.h"
#include "core/MoveKey.h"
#include "core/Constant.h"

namespace core {
const MoveKey::SplineType MoveKey::kDefaultSplineType = MoveKey::SplineType_Linear;

//-------------------------------------------------------------------------------------------------
MoveKey::Data::Data(): mEasings(), mSpline(kDefaultSplineType), mPos(), mCentroid() {}

void MoveKey::Data::clampPos() {
    mPos.setX(xc_clamp(mPos.x(), Constant::transMin(), Constant::transMax()));
    mPos.setY(xc_clamp(mPos.y(), Constant::transMin(), Constant::transMax()));
}

void MoveKey::Data::clampCentroid() {
    mCentroid.setX(xc_clamp(mCentroid.x(), Constant::transMin(), Constant::transMax()));
    mCentroid.setY(xc_clamp(mCentroid.y(), Constant::transMin(), Constant::transMax()));
}

//-------------------------------------------------------------------------------------------------
std::array<QVector2D, 2>
MoveKey::getCatmullRomVels(const MoveKey* aKey0, const MoveKey* aKey1, const MoveKey* aKey2, const MoveKey* aKey3) {
    XC_ASSERT(aKey1 && aKey2);
    return getCatmullRomVels(
        aKey1->pos(), aKey2->pos(), aKey0 ? &aKey0->pos() : nullptr,
        aKey3 ? &aKey3->pos() : nullptr,
        aKey1->data().spline() == SplineType_Linear,
        aKey2->data().spline() == SplineType_Linear
    );
}

std::array<QVector2D, 2> MoveKey::getCatmullRomVels(
    const QVector2D& aP1, const QVector2D& aP2, const QVector2D* aP0, const QVector2D* aP3,
    bool aFromLinear, bool aToLinear
) {
    std::array<QVector2D, 2> result;

    if (aFromLinear) {
        aP0 = nullptr;
    }
    if (aToLinear) {
        aP3 = nullptr;
    }

    if (!aP0) {
        const QVector2D linear = aP2 - aP1;

        if (!aP3) {
            result[1] = linear;
            result[0] = linear;
        } else {
            result[1] = 0.5f * (*aP3 - aP1);
            result[0] = util::MathUtil::getAxisInversed(linear.normalized(), result[1]);
        }
    } else {
        if (!aP3) {
            const QVector2D linear = aP2 - aP1;

            result[0] = 0.5f * (aP2 - *aP0);
            result[1] = util::MathUtil::getAxisInversed(linear.normalized(), result[0]);
        } else {
            result[0] = 0.5f * (aP2 - *aP0);
            result[1] = 0.5f * (*aP3 - aP1);
        }
    }
    return result;
}

//-------------------------------------------------------------------------------------------------
MoveKey::MoveKey(): mData() {}

TimeKey* MoveKey::createClone() {
    auto newKey = new MoveKey();
    newKey->mData = this->mData;
    return newKey;
}

bool MoveKey::serialize(Serializer& aOut) const {
    aOut.writeEasings(&mData.easing(0), kComponents);
    aOut.write((int)mData.spline());
    aOut.write(mData.pos());
    aOut.write(mData.centroid());
    return aOut.checkStream();
}

bool MoveKey::deserialize(Deserializer& aIn) {
    aIn.pushLogScope("MoveKey");

    // easing
    if (!aIn.readEasings(&mData.easing(0), kComponents)) {
        return aIn.errored("invalid easing param");
    }

    // spline type
    {
        int splineIdx = 0;
        aIn.read(splineIdx);
        if (splineIdx < 0 || SplineType_TERM <= splineIdx) {
            return aIn.errored("invalid spline type");
        }
        mData.setSpline((SplineType)splineIdx);
    }

    // position
    mData.setPos(aIn.getRead<QVector2D>());

    // centroid
    mData.setCentroid(aIn.getRead<QVector2D>());

    aIn.popLogScope();
    return aIn.checkStream();
}

} // namespace core
