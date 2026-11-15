#ifndef CORE_WAVEKEY_H
#define CORE_WAVEKEY_H

#include <algorithm>
#include <array>
#include <cmath>
#include "XC.h"
#include "util/Easing.h"
#include "core/KeyEasing.h"
#include "core/TimeKey.h"

namespace core {

// A WAVE DEFORM filter: a post-process UV displacement applied to an ISOLATED
// layer/folder composite (see LayerNode::renderComposite / FilterFrame::waveTexture).
//
// WHY A POST-PROCESS AND NOT GEOMETRY. A layer is drawn through its own mesh,
// whose cells are triangles that bone, mesh and FFD keys deform independently.
// Moving vertices would move cells apart and tear the seams between them (and
// the silhouette against every other layer); displacing the SAMPLE COORDINATE
// of the already-composited pixels cannot tear anything, because the composite
// is one continuous image - neighbouring cells share pixels by construction.
//
// THE FIELD is two traveling sine waves evaluated in CONTENT space (the image's
// own pixels for a layer, the folder's local frame for a folder), so the wave is
// glued to the artwork while the layer moves, scales, rotates or the camera
// zooms. Wave 1 travels along `angleDeg` with `wavelength`; wave 2 travels at
// `dualAngleDeg` off that direction with `wavelength * dualScale`, and `mix`
// blends it in. The two displacements are:
//   * Deform_Ripple - the field's GRADIENT. The classic refraction offset used
//     for water surfaces, ripples and heat haze: the artwork appears to move
//     back and forth along the wave's travel.
//   * Deform_Sway   - the field's HEIGHT across its travel direction. Cloth,
//     hair and flags: the content is pushed sideways as the wave passes.
//
// SPEED advances the phase from the DOCUMENT FRAME (frame / fps), never a wall
// clock, so scrubbing, playback and an export agree on every frame. It is
// STEP-HELD between keys (the earlier key's value plays) because the phase is
// `speed * time`: smoothly interpolating a rate would make the phase - and so
// the picture - jump at every key. Animate `phase` instead when the motion has
// to ease; `speed` is for a constant drift that needs no keyframes at all.
//
// THE STATIC fields (speed, crest, dual angle/scale, deform) are stored per key
// and read from the earlier key of an interpolated segment; they are shape
// decisions, not animated values, so they are not graph channels.
class WaveKey: public TimeKey {
public:
    // One easing per graph channel:
    //   0 strength (px), 1 wavelength (px), 2 angleDeg, 3 phase (cycles), 4 mix.
    static constexpr int kComponents = easingComponentCount(TimeKeyType_Wave);

    enum Deform {
        Deform_Ripple = 0, // gradient displacement: water, ripples, heat haze
        Deform_Sway = 1,   // transverse displacement: cloth, hair, flags
    };

    // The identity: nothing is displaced. A key with this data leaves the
    // composite untouched, which is what lets a freshly deserialized filter be
    // inert until the user gives it values.
    static constexpr float kIdentityStrength = 0.0f;

    // Below this the displacement is not a visible fraction of a pixel, so the
    // renderer does not isolate the node for it (the same role
    // FilterFrame::kMinActiveBlurRadius plays for blur).
    static constexpr float kMinActiveStrength = 0.01f;

    // The starter values `addFilter` seeds. They are NOT the Data defaults (a
    // neutral Data must render nothing); they are a visible, gentle water
    // surface, so adding the filter shows what it does before it is edited.
    // Measured on a 1000px-wide layer: a crest every 140px, moving 0.4 cycles a
    // second, with a 35% second wave crossing it.
    struct Preset {
        static constexpr float strength = 12.0f;
        static constexpr float wavelength = 140.0f;
        static constexpr float angleDeg = 0.0f;
        static constexpr float phase = 0.0f;
        static constexpr float mix = 0.35f;
        static constexpr float speed = 0.4f;
        static constexpr float crest = 1.0f;
        static constexpr float dualAngleDeg = 62.0f;
        static constexpr float dualScale = 0.61f;
        // half a cycle: a painted delay reads as "this part moves a little later"
        static constexpr float timeOffset = 0.5f;
    };

    class Data {
        std::array<util::Easing::Param, kComponents> mEasings;
        // graph channels
        float mStrength;   // peak displacement, content pixels
        float mWavelength; // content pixels per cycle along the travel direction
        float mAngleDeg;   // travel direction, content-space degrees
        float mPhase;      // cycles, added to speed*time
        float mMix;        // 0..1, the second wave's share
        // step-held shape
        float mSpeed;        // cycles per second, sign = travel sense
        float mCrest;        // 0.25..4, exponent applied to the wave's sine
        float mDualAngleDeg; // the second wave's direction, relative to angleDeg
        float mDualScale;    // the second wave's wavelength, relative to wavelength
        float mTimeOffset;   // cycles a fully white TIME OFFSET mask delays the phase
        Deform mDeform;

    public:
        Data():
            mEasings(), mStrength(kIdentityStrength), mWavelength(Preset::wavelength),
            mAngleDeg(Preset::angleDeg), mPhase(Preset::phase), mMix(Preset::mix),
            mSpeed(Preset::speed), mCrest(Preset::crest), mDualAngleDeg(Preset::dualAngleDeg),
            mDualScale(Preset::dualScale), mTimeOffset(Preset::timeOffset), mDeform(Deform_Ripple) {}

        // the starter values `TimeLineUtil::addFilter` writes for a new filter
        static Data presetData() {
            Data data;
            data.setStrength(Preset::strength);
            data.setWavelength(Preset::wavelength);
            data.setAngleDeg(Preset::angleDeg);
            data.setPhase(Preset::phase);
            data.setMix(Preset::mix);
            data.setSpeed(Preset::speed);
            data.setCrest(Preset::crest);
            data.setDualAngleDeg(Preset::dualAngleDeg);
            data.setDualScale(Preset::dualScale);
            data.setTimeOffset(Preset::timeOffset);
            data.setDeform(Deform_Ripple);
            return data;
        }

        util::Easing::Param& easing(int aComponent) {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }
        const util::Easing::Param& easing(int aComponent) const {
            XC_ASSERT(0 <= aComponent && aComponent < kComponents);
            return mEasings[aComponent];
        }

        void setStrength(float aValue) { mStrength = aValue; }
        float strength() const { return mStrength; }

        // A wavelength of zero (or less) would divide by zero in the field; the
        // renderer floors it, and the setter keeps a positive value so the graph
        // cannot leave a key that only the renderer knows how to interpret.
        void setWavelength(float aValue) { mWavelength = std::max(aValue, 1.0f); }
        float wavelength() const { return mWavelength; }

        void setAngleDeg(float aValue) { mAngleDeg = aValue; }
        float angleDeg() const { return mAngleDeg; }

        void setPhase(float aValue) { mPhase = aValue; }
        float phase() const { return mPhase; }

        // A share, so it is clamped: 1 already gives the second wave the same
        // weight as the first.
        void setMix(float aValue) { mMix = std::min(std::max(aValue, 0.0f), 1.0f); }
        float mix() const { return mMix; }

        void setSpeed(float aValue) { mSpeed = aValue; }
        float speed() const { return mSpeed; }

        // The shaping exponent; 1 is a plain sine. Below 1 the crest is broad
        // and the trough sharp, above 1 the opposite. Floored away from zero
        // because the shader takes `pow(|sin|, crest - 1)` for the derivative.
        void setCrest(float aValue) { mCrest = std::min(std::max(aValue, 0.25f), 4.0f); }
        float crest() const { return mCrest; }

        void setDualAngleDeg(float aValue) { mDualAngleDeg = aValue; }
        float dualAngleDeg() const { return mDualAngleDeg; }

        void setDualScale(float aValue) { mDualScale = std::min(std::max(aValue, 0.1f), 4.0f); }
        float dualScale() const { return mDualScale; }

        // How much a fully white TIME OFFSET mask delays the phase, in cycles. 0
        // disables that mask for this key. Step-held like the other shape fields.
        void setTimeOffset(float aValue) { mTimeOffset = aValue; }
        float timeOffset() const { return mTimeOffset; }

        void setDeform(Deform aValue) { mDeform = aValue; }
        Deform deform() const { return mDeform; }

        // "Nothing is displaced": the renderer only isolates the node for a
        // wave whose strength can move a pixel.
        bool isIdentity() const { return std::abs(mStrength) <= kIdentityStrength; }
        bool isActive() const { return std::abs(mStrength) > kMinActiveStrength; }
    };

    WaveKey(): mData() {}

    Data& data() { return mData; }
    const Data& data() const { return mData; }

    void setStrength(float aValue) { mData.setStrength(aValue); }
    float strength() const { return mData.strength(); }
    void setWavelength(float aValue) { mData.setWavelength(aValue); }
    float wavelength() const { return mData.wavelength(); }
    void setAngleDeg(float aValue) { mData.setAngleDeg(aValue); }
    float angleDeg() const { return mData.angleDeg(); }
    void setPhase(float aValue) { mData.setPhase(aValue); }
    float phase() const { return mData.phase(); }
    void setMix(float aValue) { mData.setMix(aValue); }
    float mix() const { return mData.mix(); }
    void setSpeed(float aValue) { mData.setSpeed(aValue); }
    float speed() const { return mData.speed(); }
    void setCrest(float aValue) { mData.setCrest(aValue); }
    float crest() const { return mData.crest(); }
    void setDualAngleDeg(float aValue) { mData.setDualAngleDeg(aValue); }
    float dualAngleDeg() const { return mData.dualAngleDeg(); }
    void setDualScale(float aValue) { mData.setDualScale(aValue); }
    float dualScale() const { return mData.dualScale(); }
    void setDeform(Deform aValue) { mData.setDeform(aValue); }
    Deform deform() const { return mData.deform(); }
    void setTimeOffset(float aValue) { mData.setTimeOffset(aValue); }
    float timeOffset() const { return mData.timeOffset(); }

    virtual TimeKeyType type() const { return TimeKeyType_Wave; }
    virtual TimeKey* createClone() {
        auto newKey = new WaveKey();
        newKey->mData = this->mData;
        return newKey;
    }
    // Format 0.18 introduces this layout, so there is no earlier one to read:
    // the bytes written here are the bytes read.
    virtual bool serialize(Serializer& aOut) const {
        aOut.writeEasings(&mData.easing(0), kComponents);
        aOut.write(mData.strength());
        aOut.write(mData.wavelength());
        aOut.write(mData.angleDeg());
        aOut.write(mData.phase());
        aOut.write(mData.mix());
        aOut.write(mData.speed());
        aOut.write(mData.crest());
        aOut.write(mData.dualAngleDeg());
        aOut.write(mData.dualScale());
        // 0.19 added the time-offset scale; a 0.18 file has no field here
        aOut.write(mData.timeOffset());
        aOut.write(static_cast<int>(mData.deform()));
        return aOut.checkStream();
    }
    virtual bool deserialize(Deserializer& aIn) {
        aIn.pushLogScope("WaveKey");
        if (!aIn.readEasings(&mData.easing(0), kComponents, false)) {
            return aIn.errored("invalid easing param");
        }
        float strength = kIdentityStrength;
        float wavelength = Preset::wavelength;
        float angle = Preset::angleDeg;
        float phase = Preset::phase;
        float mix = Preset::mix;
        float speed = Preset::speed;
        float crest = Preset::crest;
        float dualAngle = Preset::dualAngleDeg;
        float dualScale = Preset::dualScale;
        float timeOffset = Preset::timeOffset;
        int deform = Deform_Ripple;
        aIn.read(strength);
        aIn.read(wavelength);
        aIn.read(angle);
        aIn.read(phase);
        aIn.read(mix);
        aIn.read(speed);
        aIn.read(crest);
        aIn.read(dualAngle);
        aIn.read(dualScale);
        // the field arrived in 0.19; a 0.18 key keeps the preset
        if (aIn.version().majorVersion() == 0 && aIn.version().minorVersion() >= 19) {
            aIn.read(timeOffset);
        }
        aIn.read(deform);
        mData.setStrength(strength);
        mData.setWavelength(wavelength);
        mData.setAngleDeg(angle);
        mData.setPhase(phase);
        mData.setMix(mix);
        mData.setSpeed(speed);
        mData.setCrest(crest);
        mData.setDualAngleDeg(dualAngle);
        mData.setDualScale(dualScale);
        mData.setTimeOffset(timeOffset);
        mData.setDeform(deform == Deform_Sway ? Deform_Sway : Deform_Ripple);
        aIn.popLogScope();
        return aIn.checkStream();
    }

private:
    Data mData;
};

} // namespace core

#endif // CORE_WAVEKEY_H
