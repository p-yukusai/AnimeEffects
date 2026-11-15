#ifndef CTRL_POSEPARAM_H
#define CTRL_POSEPARAM_H

#include "ctrl/PoseEditMode.h"
#include "ctrl/Brush.h"

namespace ctrl {

class PoseParam {
public:
    PoseParam();

    PoseEditMode mode;

    // for drawing mode
    float diWeight;
    // for eraser mode: the shared influence brush (ctrl/Brush.h)
    BrushParam brush;
};

} // namespace ctrl

#endif // CTRL_POSEPARAM_H
