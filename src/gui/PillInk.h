#ifndef GUI_PILLINK_H
#define GUI_PILLINK_H

#include <QColor>
#include <QPainter>
#include <QRect>
#include <QRectF>

namespace gui {

// THE ROUNDED PILL, ONE WAY. The playhead's frame pill (gui::TimeCursor), the
// temporary playback window's label arms (gui::TimeRuler::renderPlayWindow) and the
// same arms in the graph panel all end on a kPillRadius rounded corner. The panel
// draws through ImGui (whose filled path is antialiased by construction); the two
// Qt widgets draw through QPainter, whose default hint is OFF - a QWidget's painter
// inherits no antialiasing at all - so a bare drawRoundedRect there produced a hard,
// stair-stepped corner on the playhead pill while the window pills, which set the
// hint themselves, were smooth. Neither radius nor ink differed; only the hint did.
//
// This is therefore the ONE draw call for a rounded pill in the Qt views: same
// rect convention (the QRect's inclusive edges), same radius, and the antialiasing
// hint owned by the rule instead of by whichever caller happened to leave it on.
// A caller that needs a square face against the playback window's bracket stem
// overdraws those slivers after this call (the window arms' inner corners).
inline void paintPill(QPainter& aPainter, const QRect& aPill, const QColor& aInk, int aRadius) {
    if (aPill.isEmpty()) {
        return;
    }
    aPainter.save();
    aPainter.setRenderHint(QPainter::Antialiasing, true);
    aPainter.setPen(Qt::NoPen);
    aPainter.setBrush(aInk);
    aPainter.drawRoundedRect(QRectF(aPill), aRadius, aRadius, Qt::AbsoluteSize);
    aPainter.restore();
}

} // namespace gui

#endif // GUI_PILLINK_H
