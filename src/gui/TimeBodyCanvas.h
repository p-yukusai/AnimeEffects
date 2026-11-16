#ifndef GUI_TIMEBODYCANVAS_H
#define GUI_TIMEBODYCANVAS_H

#include "ctrl/time/time_BodyModel.h"

class QCanvasPainter;

namespace gui {
namespace timeBodyCanvas {

// THE TIMELINE BODY, PAINTED BY CANVAS PAINTER - the migration's second surface, reading the
// same models the QPainter renderers read (`ctrl::time::Lines` / `KeyPass` / `RangeMarker` /
// `SelectionBox`), so the two hosts cannot disagree about geometry.
//
// THREE THINGS CANVAS2D CANNOT DO THE WAY QT DOES, and each is drawn the closest honest way:
//   * no dashed strokes; TYPE separators are 4-on/4-off 1px marks in the lane's 40%
//     separator ink (the solid 70% row seams are a different concept),
//     2 off) and the marquee's edge is segmented the same way;
//   * text is anchored, not boxed: a lane label is centred on its box (`Center`/`Middle`);
//   * strokes are centred on their coordinates, so every 1px mark is drawn at +0.5 (the ruler's
//     lesson, FrameRulerCanvas.cpp).
void paintLinesCanvas(QCanvasPainter& aPainter, const ctrl::time::Lines& aLines);
void paintKeysCanvas(QCanvasPainter& aPainter, const ctrl::time::KeyPass& aPass);
void paintGuidesCanvas(
    QCanvasPainter& aPainter, const QVector<ctrl::time::RangeMarker>& aMarkers,
    const ctrl::time::SelectionBox& aMarquee
);

} // namespace timeBodyCanvas
} // namespace gui

#endif // GUI_TIMEBODYCANVAS_H
