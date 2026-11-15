#ifndef CORE_SCALEKEY_H
#define CORE_SCALEKEY_H

#include <array>
#include "XC.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"

namespace core {

class ScaleKey: public TimeKey {
public:
    // One easing per graph channel: 0 is scale x, 1 is scale y.
    static constexpr int kComponents = easingComponentCount(TimeKeyType_Scale);

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        QVector2D mScale;
        void clamp();

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

        void setScale(const QVector2D& aScale) {
            mScale = aScale;
            clamp();
        }
        void setScaleX(float aScaleX) {
            mScale.setX(aScaleX);
            clamp();
        }
        void setScaleY(float aScaleY) {
            mScale.setY(aScaleY);
            clamp();
        }
        const QVector2D& scale() const { return mScale; }
    };

    ScaleKey();

    void setScale(const QVector2D& aScale) { mData.setScale(aScale); }
    const QVector2D& scale() const { return mData.scale(); }

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    virtual TimeKeyType type() const { return TimeKeyType_Scale; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

private:
    Data mData;
};

} // namespace core

#endif // CORE_SCALEKEY_H
