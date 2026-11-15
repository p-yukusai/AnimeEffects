#ifndef CTRL_BONEPARAM
#define CTRL_BONEPARAM

#include "ctrl/BoneEditMode.h"
#include "ctrl/Brush.h"

namespace ctrl {

class BoneParam {
public:
    BoneParam(): mode(BoneEditMode_Create), brush() {
        brush.radius = 50.0f;
        brush.hardness = 0.5f;
        brush.flow = 1.0f;
    }
    BoneEditMode mode;

    // The influence brush, SHARED with every other painting tool: paint and erase
    // are the same brush with `erase` set, so switching between them keeps the size
    // and the hardness (they used to be four separate sliders).
    BrushParam brush;
};

} // namespace ctrl

#endif // BONEPARAM
