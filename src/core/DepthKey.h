#ifndef CORE_DEPTHKEY_H
#define CORE_DEPTHKEY_H

#include <array>
#include "XC.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"

namespace core {

class DepthKey: public TimeKey {
public:
    static constexpr int kComponents = easingComponentCount(TimeKeyType_Depth);

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        float mDepth;
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
        void setDepth(float aDepth) {
            mDepth = aDepth;
            clamp();
        }
        void addDepth(float aAdd) {
            mDepth += aAdd;
            clamp();
        }
        const float& depth() const { return mDepth; }
    };

    DepthKey();

    void setDepth(float aDepth) { mData.setDepth(aDepth); }
    const float& depth() const { return mData.depth(); }

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    virtual TimeKeyType type() const { return TimeKeyType_Depth; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

private:
    Data mData;
};

} // namespace core

#endif // CORE_DEPTHKEY_H
