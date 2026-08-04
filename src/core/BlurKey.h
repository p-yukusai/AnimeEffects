#ifndef CORE_BLURKEY_H
#define CORE_BLURKEY_H

#include <algorithm>
#include "util/Easing.h"
#include "core/TimeKey.h"

namespace core {

class BlurKey: public TimeKey {
public:
    class Data {
        util::Easing::Param mEasing;
        float mBlurX;      // content-space radius on the blur's own X axis
        float mBlurY;      // content-space radius on the blur's own Y axis
        float mAngleDeg;   // content-space rotation of the blur's X axis (degrees)
        bool mDirectional; // UI mode flag; false = single Amount (blurX == blurY, angle 0)

    public:
        Data(): mEasing(), mBlurX(0.0f), mBlurY(0.0f), mAngleDeg(0.0f), mDirectional(false) {}

        util::Easing::Param& easing() { return mEasing; }
        const util::Easing::Param& easing() const { return mEasing; }

        // isotropic convenience: sets both radii and clears the angle
        void setAmount(float aAmount) { mBlurX = mBlurY = aAmount; mAngleDeg = 0.0f; }
        float amount() const { return mBlurX; }

        void setBlurX(float aValue) { mBlurX = aValue; }
        float blurX() const { return mBlurX; }
        void setBlurY(float aValue) { mBlurY = aValue; }
        float blurY() const { return mBlurY; }
        void setAngleDeg(float aValue) { mAngleDeg = aValue; }
        float angleDeg() const { return mAngleDeg; }
        void setDirectional(bool aValue) { mDirectional = aValue; }
        bool isDirectional() const { return mDirectional; }

        float maxRadius() const { return std::max(mBlurX, mBlurY); }
        bool isZero() const { return maxRadius() <= 0.0f; }
    };

    BlurKey(): mData() {}

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    void setAmount(float aAmount) { mData.setAmount(aAmount); }
    float amount() const { return mData.amount(); }
    void setBlurX(float aValue) { mData.setBlurX(aValue); }
    float blurX() const { return mData.blurX(); }
    void setBlurY(float aValue) { mData.setBlurY(aValue); }
    float blurY() const { return mData.blurY(); }
    void setAngleDeg(float aValue) { mData.setAngleDeg(aValue); }
    float angleDeg() const { return mData.angleDeg(); }
    void setDirectional(bool aValue) { mData.setDirectional(aValue); }
    bool isDirectional() const { return mData.isDirectional(); }

    virtual TimeKeyType type() const { return TimeKeyType_Blur; }
    virtual TimeKey* createClone() {
        auto newKey = new BlurKey();
        newKey->mData = this->mData;
        return newKey;
    }
    virtual bool serialize(Serializer& aOut) const {
        aOut.write(mData.easing());
        aOut.write(mData.blurX());
        aOut.write(mData.blurY());
        aOut.write(mData.angleDeg());
        aOut.write(mData.isDirectional());
        return aOut.checkStream();
    }
    virtual bool deserialize(Deserializer& aIn) {
        aIn.pushLogScope("BlurKey");
        if (!aIn.read(mData.easing(), false)) {
            return aIn.errored("invalid easing param");
        }
        // Blur is a single feature shipped with one AE_PROJECT_FORMAT_MINOR_VERSION bump
        // (8 -> 9), so there is no historical BlurKey layout to be compatible with: the
        // layout written by serialize() is the layout read here.
        float blurX = 0.0f;
        float blurY = 0.0f;
        float angle = 0.0f;
        bool directional = false;
        aIn.read(blurX);
        aIn.read(blurY);
        aIn.read(angle);
        aIn.read(directional);
        mData.setBlurX(blurX);
        mData.setBlurY(blurY);
        mData.setAngleDeg(angle);
        mData.setDirectional(directional);
        aIn.popLogScope();
        return aIn.checkStream();
    }

private:
    Data mData;
};

} // namespace core

#endif // CORE_BLURKEY_H
