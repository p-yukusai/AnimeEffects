#ifndef CTRL_POSEKEY_H
#define CTRL_POSEKEY_H

#include <array>
#include <memory>
#include <QList>
#include "XC.h"
#include "util/LinkPointer.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"
#include "core/Bone2.h"
namespace core {
class BoneKey;
}

namespace core {

class PoseKey: public TimeKey {
public:
    // Pose is structural: it carries ONE easing that shapes the whole key's
    // rotation blend, addressed at index 0. It publishes no graph channel, so
    // core::easingComponentCount(TimeKeyType_Pose) is 0 (see KeyEasing.h).
    static constexpr int kComponents = easingStorageCount(TimeKeyType_Pose);

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        QList<Bone2*> mTopBones;

    public:
        Data();
        Data(const Data& aRhs);
        Data& operator=(const Data& aRhs);
        ~Data();

        util::Easing::Param& easing(int aComponent) {
            XC_ASSERT(aComponent == 0);
            return mEasings[aComponent];
        }
        const util::Easing::Param& easing(int aComponent) const {
            XC_ASSERT(aComponent == 0);
            return mEasings[aComponent];
        }

        void createBonesBy(BoneKey& aAreaBone);

        QList<Bone2*>& topBones();
        const QList<Bone2*>& topBones() const;

        bool isEmpty() const { return mTopBones.empty(); }
        void deleteAll();
    };

    PoseKey();

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    virtual TimeKeyType type() const { return TimeKeyType_Pose; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    virtual bool deserialize(Deserializer& aIn);

private:
    bool serializeBone(Serializer& aOut, const Bone2* aBone) const;
    bool deserializeBone(Deserializer& aIn, Bone2* aBone);

    Data mData;
};

} // namespace core

#endif // CTRL_POSEKEY_H
