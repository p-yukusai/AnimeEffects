#ifndef HSVKEY_H
#define HSVKEY_H

#include <array>
#include "XC.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"
#include "util/MathUtil.h"

namespace core {

class HSVKey: public TimeKey {
public:
    // One easing per graph channel: 0 hue, 1 saturation, 2 value.
    static constexpr int kComponents = easingComponentCount(TimeKeyType_HSV);

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        int mHue;
        int mSaturation;
        int mValue;
        int mAbsolute;
        QList<int> mHSV;

    public:
        Data();

        util::Easing::Param& easing(int aComponent) {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }
        const util::Easing::Param& easing(int aComponent) const {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }

        void setHue(int aHue) {
            mHue = aHue;
            updateHSV();
        }
        void setSaturation(int aSaturation) {
            mSaturation = aSaturation;
            updateHSV();
        }
        void setValue(int aValue) {
            mValue = aValue;
            updateHSV();
        }
        void setAbsolute(int aAbsolute) {
            mAbsolute = aAbsolute;
            updateHSV();
        }
        void setHSV(QList<int> aHSV) {
            mHue = aHSV[0];
            mSaturation = aHSV[1];
            mValue = aHSV[2];
            mAbsolute = aHSV[3];
            updateHSV();
        }
        void updateHSV() {
            mHSV.clear();
            mHSV = {mHue, mSaturation, mValue, mAbsolute};
        }

        const QList<int>& hsv() const { return mHSV; }

        bool isZero() const;
    };

    HSVKey();

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    void setHue(int aHue) { mData.setHue(aHue); }
    void setSaturation(int aSaturation) { mData.setSaturation(aSaturation); }
    void setValue(int aValue) { mData.setValue(aValue); }
    void setAbsolute(int aAbsolute) { mData.setAbsolute(aAbsolute); }
    void setHSV(QList<int> aHSV) {
        mData.setHue(aHSV[0]);
        mData.setSaturation(aHSV[1]), mData.setValue(aHSV[2]), mData.setAbsolute(aHSV[3]);
    }
    void updateHSV() { mData.updateHSV(); }

    const QList<int>& hsv() const { return mData.hsv(); }

    virtual TimeKeyType type() const { return TimeKeyType_HSV; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

private:
    Data mData;
};

} // namespace core
#endif // HSVKEY_H
