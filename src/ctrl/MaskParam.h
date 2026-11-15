#ifndef CTRL_MASKPARAM_H
#define CTRL_MASKPARAM_H

#include "ctrl/Brush.h"
#include "core/TimeKeyType.h"
#include "core/FilterMask.h"

namespace ctrl {

// WHICH field the brush writes. There are two kinds and they differ in more than
// their address: a filter input is a per-lane side table whose neutral is per channel,
// while a layer mask is a KEY's own field whose neutral is reveal-all and which only
// exists on frames that own a key (or on the default key).
enum MaskTargetKind {
    MaskTargetKind_FilterInput,
    MaskTargetKind_LayerMask,
};

// The mask paint tool's parameters: the shared brush, plus WHICH field it writes
// (the filter lane and which of that filter's two mask inputs, or the node's matte).
class MaskParam {
public:
    MaskParam()
        : brush(),
          kind(MaskTargetKind_FilterInput),
          lane(core::TimeKeyType_Wave),
          channel(core::FilterMaskChannel_Amount),
          value(0.0f) {
        brush.radius = 50.0f;
        brush.hardness = 0.5f;
        brush.flow = 1.0f;
        brush.opacity = 1.0f;
        brush.spacing = 0.125f;
        // The field starts at its channel's neutral value, so a VALUE of 0 makes the
        // first stroke reduce the amount (the useful default for an amount mask);
        // picking the time-offset channel sets 1 instead (see setChannel).
    }

    BrushParam brush;
    MaskTargetKind kind;
    core::TimeKeyType lane;
    core::FilterMaskChannel channel;
    // Where the brush takes the field: 0 = no effect, 1 = full effect. A stamp moves
    // the texel TOWARD this value, so painting over an area converges instead of
    // accumulating, and the temporary eyedropper has an obvious thing to pick.
    float value;

    // The channel decides the useful value to start from: restricting an amount mask
    // means painting toward 0; adding a delay to a time-offset mask means toward 1.
    void setChannel(core::FilterMaskChannel aChannel) {
        if (channel == aChannel)
            return;
        channel = aChannel;
        value = (aChannel == core::FilterMaskChannel_TimeOffset) ? 1.0f : 0.0f;
    }

    // The layer mask's neutral is REVEAL ALL, so the useful stroke hides: the value
    // starts at 0 (hidden) and painting toward 1 restores what was cut away. That is
    // the opposite of a filter's amount mask, and it is why the kind is separate from
    // the channel rather than a fourth lane.
    void setKind(MaskTargetKind aKind) {
        if (kind == aKind)
            return;
        kind = aKind;
        value = (aKind == MaskTargetKind_LayerMask) ? 0.0f : value;
    }

    bool isLayerMask() const { return kind == MaskTargetKind_LayerMask; }
};

} // namespace ctrl

#endif // CTRL_MASKPARAM_H
