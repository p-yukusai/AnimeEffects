#include "core/StateKey.h"

#include "core/BlurStateKey.h"
#include "core/HSVStateKey.h"
#include "core/ImageKey.h"
#include "core/MoveStateKey.h"
#include "core/WaveStateKey.h"

namespace core {

StateKey::StateKey(): TimeKey() {}

void StateKey::reserveId(int aValue) { TimeKey::reserveId(aValue); }

std::unique_ptr<StateKey> createStateKey(TimeKeyType aType) {
    switch (aType) {
    case TimeKeyType_Move:
        return std::unique_ptr<StateKey>(new MoveStateKey());
    case TimeKeyType_Blur:
        return std::unique_ptr<StateKey>(new BlurStateKey());
    case TimeKeyType_HSV:
        return std::unique_ptr<StateKey>(new HSVStateKey());
    case TimeKeyType_Wave:
        return std::unique_ptr<StateKey>(new WaveStateKey());
    case TimeKeyType_Image:
        return std::unique_ptr<StateKey>(new ImageKey());
    default:
        return nullptr;
    }
}

} // namespace core
