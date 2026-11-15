#ifndef GUI_FRAMERULERCANVAS_H
#define GUI_FRAMERULERCANVAS_H

#include "gui/FrameRuler.h"

class QCanvasPainter;

namespace gui {
namespace frameRuler {

// THE RULER, PAINTED BY CANVAS PAINTER - the migration target's first surface.
//
// It reads the SAME `Model` the QPainter renderer (`paintRuler`) reads, so the two
// hosts cannot disagree about a tick, a number or the play window's pills; only the
// ink path differs (QRhi triangles instead of Qt's raster engine). Kept beside the
// model rather than inside it: the model is engine-neutral and must stay usable by a
// build that has no Canvas Painter at all (Qt 6.11 has no working QRhiWidget host).
//
// COORDINATES follow Canvas2D's edge convention: a 1px stroke is centred on the line
// it is given, so a tick on column x is drawn at x + 0.5 and reaches exactly that
// column with antialiasing off. Text is anchored left/top, the anchor the caller's
// box states.
void paintRulerCanvas(QCanvasPainter& aPainter, const Model& aModel, const PaintInk& aInk);

} // namespace frameRuler
} // namespace gui

#endif // GUI_FRAMERULERCANVAS_H
