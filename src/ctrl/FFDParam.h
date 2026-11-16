#ifndef CTRL_FFDPARAM
#define CTRL_FFDPARAM

#include "ctrl/Brush.h"

namespace ctrl {

// The FFD tool's parameters. The BRUSH is the shared one (ctrl/Brush.h): size,
// hardness, flow, pressure switches and the erase flag, so this tool reads and
// behaves like the mask brush and the influence brush instead of carrying its own
// three-step hardness and a duplicate erase parameter set.
//
// `type` stays what it always was - a MODE, not a brush property: Drag moves one
// vertex, Pencil paints the mesh, Eraser pulls it back. Erase is expressed twice
// only because both readings are useful: `type` selects the mode for the editor,
// `brush.erase` is what the stroke stamps carry.
class FFDParam {
public:
    enum Type { Type_Drag, Type_Pencil, Type_Eraser, Type_TERM };

    FFDParam(): type(Type_Pencil), brush(), blur(0.0f), focusRadius(1.0f) {
        brush.radius = 100.0f;
        brush.hardness = 0.5f;
        brush.flow = 0.5f;
        brush.spacing = 0.125f;
        // the mesh brush has no per-stroke cap (a displacement cannot be re-blended
        // like alpha), so its panel shows Flow and not Opacity
        brush.opacity = 1.0f;
    }

    Type type;
    BrushParam brush;
    float blur;         // the pencil's smoothing pass, 0..1
    float focusRadius;  // the drag mode's pick radius (world px)

    // keeps `brush.erase` and the mode in step, whichever one is written
    void setType(Type aType) {
        type = aType;
        brush.erase = (aType == Type_Eraser);
    }
};

} // namespace ctrl

#endif // CTRL_FFDPARAM
