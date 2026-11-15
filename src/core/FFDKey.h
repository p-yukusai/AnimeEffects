#ifndef CORE_FFDKEY_H
#define CORE_FFDKEY_H

#include <array>
#include <QVector>
#include "XC.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"
#include "gl/Vector3.h"

namespace core {

class FFDKey: public TimeKey {
public:
    // FFD is structural: ONE easing shapes the whole key, addressed at index 0,
    // and it publishes no graph channel (core::easingComponentCount(_FFD) is 0).
    static constexpr int kComponents = easingStorageCount(TimeKeyType_FFD);

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        QVector<gl::Vector3> mBuffer;
        int mVtxCount;

    public:
        Data();
        void alloc(int aVtxCount);
        void write(const gl::Vector3* aSrc, int aVtxCount);
        void allocAndWrite(const gl::Vector3* aSrc, int aVtxCount);
        void clear();
        void swap(QVector<gl::Vector3>& aRhs);
        util::Easing::Param& easing(int aComponent) {
            XC_ASSERT(aComponent == 0);
            return mEasings[aComponent];
        }
        const util::Easing::Param& easing(int aComponent) const {
            XC_ASSERT(aComponent == 0);
            return mEasings[aComponent];
        }
        gl::Vector3* positions();
        const gl::Vector3* positions() const;
        int count() const;
        void insertVtx(int aIndex, const gl::Vector3& aPos);
        void pushBackVtx(const gl::Vector3& aPos);
        gl::Vector3 removeVtx(int aIndex);
        gl::Vector3 popBackVtx();
    };

    FFDKey();

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    bool belongsToDefaultParent() const { return !parent(); }

    virtual TimeKeyType type() const { return TimeKeyType_FFD; }
    virtual TimeKey* createClone();
    virtual bool serialize(Serializer& aOut) const;
    QJsonObject serializeToJson() const;
    void deserializeFromJson(QJsonObject json);
    virtual bool deserialize(Deserializer& aIn);

private:
    Data mData;
};

} // namespace core

#endif // CORE_FFDKEY_H
