#ifndef GUI_FRAMERULER_H
#define GUI_FRAMERULER_H

#include <QColor>
#include <QRect>
#include <QString>
#include <QVector>
#include <functional>

#include "core/Frame.h"
#include "core/TimeFormat.h"
#include "ctrl/time/time_Scaler.h"
#include "util/Range.h"

class QPainter;

namespace gui {

// THE FRAME RULER: one set of rules for the strip above the timeline's lanes and for
// the band above the graph panel.
//
// WHAT THIS OWNS. Which frames are ticked, at which tier, how a tier looks (its alpha,
// its one shared length), which frames are NUMBERED, how a number yields to the
// project's own first/last frame labels, how those two labels collapse into one
// "first-last" label, how the playhead's pill suppresses the number in its column, and
// the temporary playback window's band, stems and label pills. It publishes GEOMETRY
// and CLASSIFICATION only - never ink: the timeline paints it with QPainter
// (paintRuler) and the graph panel paints the same model with ImGui
// (GraphPanelFrame.cpp), each with its own tokens, and the two therefore cannot
// disagree about which frame is numbered or where a label sits.
//
// WHY A MODEL AND NOT A SHARED PAINT ROUTINE. The two hosts' paint stacks have nothing
// in common (QPainter over a raster widget, ImGui over a draw list in a GL widget), and
// their fonts are measured differently (QFontMetrics vs the ImGui atlas). What they CAN
// share is the decision - and the decision is where the two implementations used to
// drift: every "the two views disagree" defect in this area was a rule implemented
// twice (the hairline's alpha, the label column, the ring's ink). The rule now lives
// once; the ink stays the host's.
//
// THE PIXEL CONTRACT. Text widths are the host's: a frame number RESERVES 6px per
// character (the timeline's own rule, so a label's slot is known before the drawing
// font is), while a pill's width is measured by the host and passed in. A label is
// published as (left, top, width), not as a QRect, because the two hosts form their
// text rects with different inclusive conventions; each host keeps the rect it always
// used and the pixels do not move.
namespace frameRuler {

// THE METRICS. The first five are the timeline editor's public constants (re-exported
// here and static_asserted against them in FrameRuler.cpp); the rest are the ruler's
// own numbers.
constexpr int kBandHeight = 22;
constexpr int kNumberTop = 1;
constexpr int kNumberHeight = 14;
constexpr int kTickLength = 5; // every tier shares one length
constexpr int kPillPadding = 5;
constexpr int kPillRadius = 3;
// The pill rides one row below the number text's top (gui::TimeCursor's pillTop), and
// the playhead line starts under it.
constexpr int kPillTop = kNumberTop + 1;
constexpr int kLineTop = kPillTop + kNumberHeight;
// A frame number reserves this much per character: a label's slot is known before the
// drawing font is (the timeline's own reserve, not a measurement).
constexpr int kNumberCharWidth = 6;
// Two numbers never come closer than this, or they read as one.
constexpr int kNumberMargin = 8;
// How far a tick outside the ACTIVE playback range recedes (multiplied into its tier
// alpha), and the same factor for a number.
constexpr qreal kOutsideRangeFactor = 0.4;

// A tier's ink strength. One table for both hosts: the timeline and the panel have
// always shared these three numbers, and a change here must move both.
qreal tickAlpha(int aLevel);

// The frame axis, in the host's own space. Both ctrl::time::Scaler (the timeline) and
// ctrl::graph::Scaler (the panel, whose frame axis is a time::Scaler plus a pushed
// window) can answer these; the model does not care which.
struct Axis {
    // Pixel of a frame, in the host's coordinates. Fractional for a pushed window.
    std::function<double(int)> pixelOfFrame;
    // Frame covered by a pixel column (the panel's window edges come from this).
    std::function<int(int)> frameOfPixel;
    // The ladder's classification AT THE HOST'S RATE (a pushed window changes the
    // spacing, so the tier is a function of the rate, not of the frame alone).
    std::function<ctrl::time::Scaler::Attribute(int, double)> attributeAt;
    // The rate the ticks are classified at, and the finest stride the ladder draws.
    double pixelsPerFrame = 1.0;
    int tickStride = 1;
};

struct Tick {
    int x = 0;
    int level = 0; // ctrl::time::Scaler::TickLevel
    bool inActiveRange = true;
    // TRUE for the project's own first/last frame marks. A boundary tick ALWAYS wins its
    // column: it is the mark that says where the project begins and ends, and the old
    // ruler drew it last for exactly that reason.
    bool boundary = false;
};

// A number. `left` is the box's left edge and `width` the ladder's 6px-per-character
// reserve, so the host centres/aligns the glyphs exactly as it always has.
struct Label {
    int left = 0;
    int top = 0;
    int width = 0;
    QString text;
    // The frame this label NAMES (or -1 for a label that names none), and the merged
    // boundary label's second frame (or -1). Stated so a consumer never recovers them
    // from the pixels.
    int frame = -1;
    int endFrame = -1;
    bool inActiveRange = true;
    // TRUE for the project's own first/last frame labels and the merged pair. They own
    // their slots, and a ladder number yields to them.
    bool boundary = false;
};

struct Pill {
    QRect rect;
    QString text;
    bool isValid() const { return !rect.isEmpty(); }
};

// THE TEMPORARY PLAYBACK WINDOW'S OVERLAY. Geometry and text only; the host paints
// the band, then the stems, then the pills (a merged pill wider than the window must
// not hide the stems' lower half).
struct PlayWindowOverlay {
    bool active = false;
    QRect band;      // the wash, clipped to the visible columns
    QRect startStem; // 2px, from the pill row to the band's bottom
    QRect endStem;
    Pill start;
    Pill end;
    Pill merged; // the one centred "start-end" label; empty when the arms fit
};

struct Model {
    QRect band;
    QVector<Tick> ticks; // ascending by x, one per column
    QVector<Label> labels;
    // The playhead's frame-number pill and the top of its line. Empty/0 when the host
    // publishes no playhead.
    Pill playhead;
    int playheadLineTop = 0;
    PlayWindowOverlay window;
};

// Everything the builder reads. `band` is the strip in the host's coordinates and
// `visible` the columns the host can paint (the wash is clipped to it); `labelTop` is
// the absolute y of the number row's top.
struct Inputs {
    QRect band;
    QRect visible;
    int labelTop = 0;
    // The project's own first/last frame: their labels own the ends of the strip.
    util::Range frameRange;
    bool hasFrameRange = false;
    // The fade basis (ctrl::TimeLineEditor::playbackRange): the project's range, or the
    // play window while it is active.
    util::Range activeRange;
    // The frames the host wants enumerated (its own visible window, already widened).
    util::Range visibleFrames;
    // The playhead. `hasPlayhead` is explicit because a frame index may be NEGATIVE (the
    // graph panel places one at frame -3, outside the project range); the width is the
    // host's own measurement of the text.
    bool hasPlayhead = false;
    int playheadFrame = 0;
    QString playheadText;
    int playheadTextWidth = 0;
    // The temporary playback window: frames -1 = inactive. Widths are the host's.
    int windowStart = -1;
    int windowEnd = -1;
    QString windowStartText;
    QString windowEndText;
    int windowStartTextWidth = 0;
    int windowEndTextWidth = 0;
    int windowMergedTextWidth = 0;
    // The formatter's basis: the PROJECT's frame range and rate. Padding therefore does
    // not change as the view scrolls (the panel's rule, adopted for both rulers).
    int fps = 60;
    core::TimeFormatType formatType = core::TimeFormatType_Frames_From0;
};

Model build(const Inputs& aInputs, const Axis& aAxis);

// The ink the timeline's ruler paints, from the model. The panel paints the same model
// through ImGui with theme::Graph's tokens instead.
struct PaintInk {
    QColor content;     // ticks and numbers (theme::TimeLine::headerContentColor)
    QColor windowBand;  // the temporary window's wash
    QColor windowEdge;  // its stems and pills
    QColor windowText;  // its numbers
    // THE PLAYHEAD'S PILL. It is the model's (`Model::playhead`) and it is painted by
    // whoever paints the ruler, so the pill has ONE spelling: the cursor widget used to
    // recompute the same rect from the same font, which is the second spelling this moved
    // out of. `theme::Colors::accentBright` / `accentBrightText`.
    QColor playheadFill;
    QColor playheadText;
};

void paintRuler(QPainter& aPainter, const Model& aModel, const PaintInk& aInk);

// The number inside a pill: the pill's own text column (left + kPillPadding), on the
// ruler's number row. Stated as the integer it is, so the panel can reproduce the same
// column without QFontMetrics at draw time.
void paintPillText(QPainter& aPainter, const QRect& aPill, const QString& aText, const QColor& aInk, int aTop);

} // namespace frameRuler
} // namespace gui

#endif // GUI_FRAMERULER_H
