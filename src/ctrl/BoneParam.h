#ifndef CTRL_BONEPARAM
#define CTRL_BONEPARAM

#include <algorithm>
#include "ctrl/BoneEditMode.h"
#include "ctrl/Brush.h"

namespace ctrl {

// What one weight-brush stamp does to the authored value it covers. Add and
// Subtract build up across the stamps of a stroke; Replace converges toward the
// stamp's own falloff. This is the weight tool's own vocabulary - the shared
// brush's `erase` flag still means the influence modes' paint/erase pair, and
// folding Replace into it would leave one of the three unexpressible.
enum WeightPaintMode {
    WeightPaint_Add,
    WeightPaint_Subtract,
    WeightPaint_Replace,
    WeightPaintMode_TERM
};

// THE DEPOSIT RULE of one stamp on one vertex, in one place: the field is
// normalised to [0, 1], Replace ignores the previous value (that is its whole
// point), and Add/Subtract clamp at the ends. Factored out so all three modes are
// testable without a live cursor and a GL context.
inline float applyWeightStamp(WeightPaintMode aMode, float aBefore, float aAmount) {
    float after = aBefore;
    switch (aMode) {
    case WeightPaint_Subtract: after = aBefore - aAmount; break;
    case WeightPaint_Replace: after = aAmount; break;
    case WeightPaint_Add:
    default: after = aBefore + aAmount; break;
    }
    return std::min(std::max(after, 0.0f), 1.0f);
}

class BoneParam {
public:
    BoneParam(): mode(BoneEditMode_Create), brush() {
        brush.radius = 50.0f;
        brush.hardness = 0.5f;
        brush.flow = 1.0f;
    }
    BoneEditMode mode;

    // The shared brush. The envelope mode is ONE brush whose SIZE/HARDNESS are
    // only the footprint; the envelope range it paints toward is the explicit
    // value below, and flow limits how much one stamp moves it.
    BrushParam brush;

    // The envelope range the brush moves toward, in content pixels. Below the
    // current range it shrinks; above it grows. Independent of the brush size.
    float envelopeValue = 50.0f;

    // The PaintWeights brush's deposit rule (add / subtract / replace).
    WeightPaintMode weightMode = WeightPaint_Add;

    // The per-bone weight heatmap over the influenced layers. On by default in the
    // influence modes: the envelope polygons say WHERE a bone reaches, but only the
    // weights say how strongly, and that is what the paint tools are shaping.
    bool showWeightHeatmap = true;
};

} // namespace ctrl

#endif // BONEPARAM
