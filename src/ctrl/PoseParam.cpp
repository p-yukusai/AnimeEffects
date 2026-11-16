#include "ctrl/PoseParam.h"

namespace ctrl {

PoseParam::PoseParam(): mode(PoseEditMode_Move), diWeight(0.0f), brush() {
    brush.radius = 200.0f;
    brush.hardness = 0.5f;
    brush.flow = 1.0f;
    brush.erase = true;
}

} // namespace ctrl
