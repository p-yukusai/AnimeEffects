#ifndef CORE_HSVSTATEKEY_H
#define CORE_HSVSTATEKEY_H

#include "core/StateKey.h"

namespace core {

// The discrete mode of an HSV property: the absolute/relative colour flag.
class HSVStateKey: public StateKey {
public:
    HSVStateKey();

    TimeKeyType type() const override { return TimeKeyType_HSV; }

    bool absolute() const { return mAbsolute; }
    void setAbsolute(bool aValue) { mAbsolute = aValue; }

    StateKey* cloneState() const override;
    bool serialize(Serializer& aOut) const override;
    bool deserialize(Deserializer& aIn) override;

private:
    bool mAbsolute;
};

} // namespace core

#endif // CORE_HSVSTATEKEY_H
