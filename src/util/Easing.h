#ifndef UTIL_EASING
#define UTIL_EASING

#include <QString>
#include <QSettings>
#include <QPointF>

namespace util {

class Easing {
public:
    enum Type {
        // Stepped holds: no interpolation between keys. Floor keeps the
        // previous key's value up to the next key's frame; Round flips to
        // the next key's value at the interval midpoint (rounded frame).
        Type_Floor,
        Type_Linear,
        Type_Sine,
        Type_Quad,
        Type_Cubic,
        Type_Quart,
        Type_Quint,
        Type_Expo,
        Type_Circ,
        Type_Back,
        Type_Elastic,
        Type_Bounce,
        Type_Custom,
        // Appended after Custom: existing ids 0-12 are serialized as-is and
        // must never shift.
        Type_Round,
        // ID 14 IS RETIRED. It was Type_Auto, and it is deliberately NOT
        // reused: a 0.14 file that holds it loads as Type_Custom with BOTH
        // handle sides AutoClamped (see Deserializer::read), which is what that
        // value meant. Type_TERM deliberately keeps the value 14 so nothing above it
        // can quietly take the retired slot.
        Type_TERM,
    };

    // Blender's five per-side bezier handle modes. A key owns TWO of them -
    // handleIn (its incoming tangent, stored as the PREVIOUS segment's x2/y2)
    // and handleOut (its outgoing one, stored as the NEXT segment's x1/y1) -
    // and each side derives its control point by its OWN mode:
    //
    //   Free         the stored value is the truth, untouched;
    //   Aligned      the stored value is the truth TOO - it is collinear with
    //                the other side by construction, because a drag keeps it so
    //                (the mode is a promise the gestures enforce, never an
    //                inference from the pixel angle);
    //   Vector       on the chord to the segment's other key, at the third
    //                points: the segment is a straight line (Blender's Vector);
    //   Auto         DERIVED per KEY from BOTH of that key's neighbours: one
    //                central-difference slope shared by whichever of the two
    //                handles is auto, so a symmetric key draws a symmetric pair
    //                (the reference's default 'uniform' tangent mode);
    //   AutoClamped  the same derivation, with the key's slope clamped so BOTH
    //                of its handles stay inside the value range of the key and
    //                its two neighbours, so a local extremum is flat instead of
    //                overshooting.
    //
    // The ordering is serialization order (Blender's own enum order) and 0.14
    // did not have it; see FormatVersion.h.
    enum HandleMode {
        HandleMode_Free,
        HandleMode_Aligned,
        HandleMode_Vector,
        HandleMode_Auto,
        HandleMode_AutoClamped,
        HandleMode_TERM,
    };

    enum Range { Range_In, Range_Out, Range_InOut, Range_TERM};

    struct CubicBezier {
        // Classic cubic ease-in-out (smoothstep): controls crossed at the
        // thirds — (2/3, 0) and (1/3, 1) — giving Y(t) = 3t^2 - 2t^3. Both
        // controls ON the diagonal would degenerate the curve to a straight
        // line, so the Ys sit at the extremes.
        float x1 = 2.0f / 3.0f;
        float y1 = 0.0f;
        float x2 = 1.0f / 3.0f;
        float y2 = 1.0f;
        // Control points are compared by value (set/read via the editor or
        // serialization), so a plain float compare is the right test.
        bool operator==(const CubicBezier& aRhs) const {
            return x1 == aRhs.x1 && y1 == aRhs.y1 && x2 == aRhs.x2 && y2 == aRhs.y2;
        }
    };

    // The automatic handle mode the APPLICATION uses for a DEFAULT automatic
    // easing (a stored Auto default, or a 0.14 Type_Auto migrated into the
    // per-side model), and for its explicit "Auto" PRESET (the picker's Auto
    // tile, the header's Auto quick button, the graph's Auto action). It is
    // AutoClamped: that is what the retired type meant, and the preset a user
    // reaches for must not overshoot. Unclamped HandleMode_Auto stays available
    // as a deliberate per-side choice in the picker's mode combo, where its
    // name reads "Auto" (see getParamName).
    static constexpr HandleMode kDefaultAutomaticHandleMode = HandleMode_AutoClamped;
    // The pair an explicit automatic preset writes on BOTH sides.
    static constexpr HandleMode kAutoPresetHandleMode = kDefaultAutomaticHandleMode;

    struct Param {
        // THE PRE-REFACTOR DEFAULT, preserved: an unedited key is a LINEAR curve
        // (range InOut, weight 1, and the smoothstep bezier stored for the day the
        // key is switched to Custom) with two stored (linked) sides. It is NOT an
        // automatic curve. The handle modes are inert on a non-Custom type, and a
        // key nobody edited must keep the shape it has always played; automatic
        // handles are a user-chosen preset, never the default shape.
        Param()
            :
            type(Type_Linear), range(Range_InOut), weight(1.0f) {}
        bool isValidParam() const;
        bool operator==(const Param& aRhs) const;

        bool operator!=(const Param& aRhs) const { return !(*this == aRhs); }
        mutable Type type;
        Range range;
        float weight;
        mutable CubicBezier cubicBezier;
        // The two handle modes of the KEY that owns this param: its incoming
        // tangent (handleIn, stored as the previous segment's x2/y2) and its
        // outgoing one (handleOut, stored as this param's x1/y1). A new key
        // starts Aligned on both sides - the old `aligned = true` default,
        // split into the two sides Blender edits. A gesture that drags one side
        // writes THAT side's mode; the other side follows according to ITS own
        // mode: a DERIVED partner is mirrored and freed, an Aligned one stays
        // collinear at its own length, a Free one is untouched (see
        // GraphEditor::updateDrag).
        HandleMode handleIn = HandleMode_Aligned;
        HandleMode handleOut = HandleMode_Aligned;
    };

    // ---- A segment's derived handles -------------------------------------
    // One neighbouring key of a segment. `value` is the shape's value expressed
    // in the SEGMENT's own change - 0 at the segment's `from` key and 1 at its
    // `to` key - so the handle math is unit-free: it reads the same whether the
    // channel is a position in pixels, an angle in radians or an opacity in
    // 0..1, and it never needs the easing to know what it is shaping.
    struct AutoKey {
        bool valid = false;
        float frame = 0.0f; // frame offset from the segment's from key
        float value = 0.0f; // (v - vFrom) / (vTo - vFrom)
        bool operator==(const AutoKey& aRhs) const {
            return valid == aRhs.valid && frame == aRhs.frame && value == aRhs.value;
        }
        bool operator!=(const AutoKey& aRhs) const { return !(*this == aRhs); }
    };

    // The keys around a segment's two ends. A segment of a two-key channel has
    // neither; the first key of a channel has no `prev`, the last no `next`.
    // `inMode` is the TO key's incoming mode: the arriving side of the segment
    // belongs to the TO key (its stored value lives in the FROM key's param),
    // so a caller that has a Param in hand is holding only one of the two modes.
    struct AutoContext {
        AutoKey prev; // the key before the segment's from key
        AutoKey next; // the key after the segment's to key
        HandleMode inMode = HandleMode_Aligned;
        // FULL equality, inMode included. A context is what a derived curve is
        // DRAWN from, and the arriving side's mode is half of that derivation: a
        // consumer that caches the context (the timeline chip) must repaint when
        // only the TO key's mode changes, so comparing the neighbours alone would
        // leave a stale glyph.
        bool operator==(const AutoContext& aRhs) const {
            return prev == aRhs.prev && next == aRhs.next && inMode == aRhs.inMode;
        }
        bool operator!=(const AutoContext& aRhs) const { return !(*this == aRhs); }
    };

    // Whether a handle mode derives its control point instead of reading the
    // stored one.
    static bool isDerived(HandleMode aMode) {
        return aMode == HandleMode_Vector || aMode == HandleMode_Auto
            || aMode == HandleMode_AutoClamped;
    }

    // The control point of ONE side of the segment aFrom -> its successor, in
    // normalized segment space (x 0..1 across the frame span, y in units of the
    // segment's own value change). aSide 0 is the outgoing point (the FROM
    // key's handleOut), 1 the incoming one (the TO key's mode, passed as
    // aInMode). aSpan is the segment's frame span, the unit autoEasingContext's
    // neighbour frames use.
    //
    //   * a stored mode (Free/Aligned) returns the stored (x,y) untouched;
    //   * Vector returns the chord's third point - (1/3,1/3) outgoing,
    //     (2/3,2/3) incoming - which is a straight segment;
    //   * Auto derives a slope per KEY, not per side: the key's central
    //     difference over BOTH of its neighbours (the outgoing side uses the
    //     FROM key's slope, the incoming side the TO key's), and only y is
    //     derived - the handle x stays at the third (Blender's automatic
    //     handle). A key with only one neighbour follows Blender's endpoint
    //     rule and takes the adjacent key's own third-point slope; a two-key
    //     channel (no interior key to borrow from) falls back to the chord, the
    //     identity thirds;
    //   * AutoClamped clamps the key's slope so both of that key's handles stay
    //     inside the value range of the key and its two neighbours, which
    //     flattens a local extremum.
    static QPointF controlPoint(
        const Param& aFrom, HandleMode aInMode, const AutoContext& aCtx, float aSpan, int aSide);

    static QString getTypeName(Type aType);
    static QString getRangeName(Range aRange);
    // The translated name of one handle mode, from the same EasingName catalog
    // the type names come from.
    static QString getHandleModeName(HandleMode aMode);
    // A Param's USER-FACING name, in the same vocabulary as getTypeName. For a
    // non-Custom interpolation it IS getTypeName(type). A Custom curve is named by
    // its two handle MODES, because that is where its identity lives now that
    // Type_Auto is retired:
    //   both Auto        -> "Auto"
    //   both AutoClamped -> "Auto Clamped"
    //   both Aligned     -> "Aligned"
    //   both Vector      -> "Vector"
    //   both Free        -> "Free"
    //   mixed            -> the two side names, INCOMING first: "Auto / Free".
    static QString getParamName(const Param& aParam);
    // A newly created key's easing, from the stored defaults. The ONE reader of
    // "generalsettings/easing" for key creation, so every path (SRT, Pose, FFD,
    // the dock's key knockers, the object tree) agrees. A stored value that meant
    // the retired Auto type (the name "Auto", or the pre-0.15 ordinal 14) becomes
    // the automatic pair on a Custom curve, at kDefaultAutomaticHandleMode; everything
    // else keeps the stored type. The range comes from its own setting.
    static Param defaultParam();
    // Picker display order: Linear, Floor and Round lead (the entries whose
    // shape is not one of the family's parameters - a straight line and the two
    // stepped holds), then the analytic curve family, then the bezier "Custom"
    // entry. Distinct from the enum ordinal (which is serialization order and
    // must not shift), so pickers build their layouts from these helpers
    // instead of assuming index == enum ordinal. Automatic handles are a MODE
    // now, not a type, so they are not in this table; the picker's Auto preset
    // writes both modes on a Custom param.
    static int displayIndexOf(Type aType);      // -1 when aType is not listed
    static Type typeAtDisplayIndex(int aIndex); // Type_Linear fallback
    static int displayCount();                  // number of pickable entries

    static float calculate(Type, Range, float t, float b, float c, float d);
    static float calculate(const Param&, float t, float b, float c, float d);
    // The Custom form with its two control points derived per side: aFrom is the
    // FROM key's param (its handleOut drives the leaving side and holds the
    // stored pair) and aCtx carries the neighbours plus the TO key's incoming
    // mode. An empty context (what a caller with no document hands in) makes
    // both derived sides fall back to the identity thirds; a non-Custom type
    // ignores the context entirely. aSpan must be the segment's length in the
    // same frame unit as aCtx's frames, exactly as `d` is for calculate().
    static float calculate(const Param&, const AutoContext& aCtx, float t, float b, float c, float d);

    static float sineIn(float t, float b, float c, float d);
    static float sineOut(float t, float b, float c, float d);
    static float sineInOut(float t, float b, float c, float d);

    static float quadIn(float t, float b, float c, float d);
    static float quadOut(float t, float b, float c, float d);
    static float quadInOut(float t, float b, float c, float d);

    static float cubicIn(float t, float b, float c, float d);
    static float cubicOut(float t, float b, float c, float d);
    static float cubicInOut(float t, float b, float c, float d);

    static float quartIn(float t, float b, float c, float d);
    static float quartOut(float t, float b, float c, float d);
    static float quartInOut(float t, float b, float c, float d);

    static float quintIn(float t, float b, float c, float d);
    static float quintOut(float t, float b, float c, float d);
    static float quintInOut(float t, float b, float c, float d);

    static float expoIn(float t, float b, float c, float d);
    static float expoOut(float t, float b, float c, float d);
    static float expoInOut(float t, float b, float c, float d);

    static float circIn(float t, float b, float c, float d);
    static float circOut(float t, float b, float c, float d);
    static float circInOut(float t, float b, float c, float d);

    static float backIn(float t, float b, float c, float d);
    static float backOut(float t, float b, float c, float d);
    static float backInOut(float t, float b, float c, float d);

    static float elasticIn(float t, float b, float c, float d);
    static float elasticOut(float t, float b, float c, float d);
    static float elasticInOut(float t, float b, float c, float d);

    static float bounceIn(float t, float b, float c, float d);
    static float bounceOut(float t, float b, float c, float d);
    static float bounceInOut(float t, float b, float c, float d);

    static Easing::Type easingToEnum(QString easing);
    static Easing::Range rangeToEnum(QString range);

private:
    Easing() {}
};

} // namespace util

#endif // UTIL_EASING
