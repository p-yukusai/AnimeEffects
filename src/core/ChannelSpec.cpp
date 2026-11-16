#include "core/ChannelSpec.h"

#include "core/Constant.h"
#include "core/KeyEasing.h"
#include "core/WaveKey.h"

namespace core {

int propertyChannelCount(TimeKeyType aType) {
    switch (aType) {
    case TimeKeyType_Move:
        // position X/Y plus the hidden centroid X/Y. The centroid interpolates,
        // so it is a channel; it is not a graph channel.
        return 4;
    case TimeKeyType_Rotate:
    case TimeKeyType_Depth:
    case TimeKeyType_Opacity:
        return 1;
    case TimeKeyType_Scale:
    case TimeKeyType_Image:
        return 2;
    case TimeKeyType_Blur:
    case TimeKeyType_HSV:
        return 3;
    case TimeKeyType_Wave:
        return 5;
    default:
        // Bone, Pose, Mesh, FFD and Mask are structural: their data is not a
        // scalar channel and they keep whole keys.
        return 0;
    }
}

ChannelSpec channelSpec(TimeKeyType aType, int aComponent) {
    if (aComponent < 0 || propertyChannelCount(aType) <= aComponent) {
        return ChannelSpec();
    }

    const auto eased = [](float aMin, float aMax, float aDefault, bool aGraphable) {
        ChannelSpec out;
        out.valid = true;
        out.graphable = aGraphable;
        out.interpolation = ChannelInterpolation::Eased;
        out.minValue = aMin;
        out.maxValue = aMax;
        out.defaultValue = aDefault;
        return out;
    };

    switch (aType) {
    case TimeKeyType_Move: {
        ChannelSpec out = eased(Constant::transMin(), Constant::transMax(), 0.0f, true);
        // Components 2 and 3 are the centroid pair: keyable and interpolated,
        // but not drawn in the graph.
        out.graphable = aComponent < 2;
        return out;
    }

    case TimeKeyType_Rotate:
        return eased(Constant::rotateMin(), Constant::rotateMax(), 0.0f, true);

    case TimeKeyType_Scale:
        return eased(Constant::scaleMin(), Constant::scaleMax(), 1.0f, true);

    case TimeKeyType_Depth:
        return eased(Constant::transMin(), Constant::transMax(), 0.0f, true);

    case TimeKeyType_Opacity:
        return eased(0.0f, 1.0f, 1.0f, true);

    case TimeKeyType_Blur:
        // The old setters do not clamp a blur radius or angle.
        return eased(
            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), 0.0f, true
        );

    case TimeKeyType_HSV: {
        ChannelSpec out = eased(
            std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
            aComponent == 0 ? 0.0f : 100.0f, true
        );
        // Playback stores the components as ints and truncates the blended
        // float at the setter boundary; the track keeps the same storage rule.
        out.integral = true;
        return out;
    }

    case TimeKeyType_Wave: {
        switch (aComponent) {
        case 0: // strength
            return eased(
                std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
                WaveKey::kIdentityStrength, true
            );
        case 1: // wavelength
            return eased(1.0f, std::numeric_limits<float>::max(), WaveKey::Preset::wavelength, true);
        case 2: // angle
            return eased(
                std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
                WaveKey::Preset::angleDeg, true
            );
        case 3: // phase
            return eased(
                std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(),
                WaveKey::Preset::phase, true
            );
        default: // mix
            return eased(0.0f, 1.0f, WaveKey::Preset::mix, true);
        }
    }

    case TimeKeyType_Image: {
        ChannelSpec out = eased(Constant::transMin(), Constant::transMax(), 0.0f, true);
        // blendImageKey step-holds the last key; a curve would draw an
        // interpolation the document does not have.
        out.interpolation = ChannelInterpolation::Step;
        out.graphable = false;
        return out;
    }

    default:
        return ChannelSpec();
    }
}

bool hasChannelTracks(TimeKeyType aType) { return propertyChannelCount(aType) > 0; }

} // namespace core
