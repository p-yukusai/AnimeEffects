// The animation graph panel's PIXEL CONTRACT.
//
// The panel's drawing decisions live once, in gui::drawPanel (src/gui/GraphPanelFrame.cpp),
// expressed over gui::ink::Ink. On a Qt 6.12 tree the SHIPPING host is QCanvasPainterWidget
// (gui::GraphPanelWidget) fed by gui::ink::InkCanvas: this file builds that same widget with
// a SYNTHETIC gui::GraphPanelModel, pulls one frame back with grabFramebuffer() and samples
// it. drawPanel is a pure function of (model, palette, focus, pointer), so the pixels it
// leaves are the panel's pixels; no application window, document or editor is involved in
// the pixel passes.
//
// WHAT IS DRIVEN. The real widget class, the real backend and the real drawing decisions.
// The model is built by hand from the model's own coordinates; the pointer is handed to the
// drawing layer exactly as the host hands its cursor. THE ONE EXCEPTION is the
// hover-vs-press assertion (checkHoverMatchesPress): it builds a REAL ctrl::GraphEditor over
// a real TimeLine, presses it, and asks gui::panelPickAt about the model that editor
// publishes - no widget involved, so it runs before the first frame and pins the ink and
// the press to one answer.
//
// A SECOND, STRUCTURAL GATE runs the same models through InkQPainter (Qt's own rasteriser)
// and InkCanvas and compares them: geometry and fills exact, translucent ink allowed one
// unit per channel, text compared for presence per published label box. The shipping host
// is Canvas, and the parity block is what keeps a shape decision from being one
// rasteriser's accident.
//
// WHAT IT PINS, and why each assertion is a regression gate:
//   1 no grid, no background. A rect inside the plot that holds none of the
//     model's geometry (verified against EVERY tick, label, curve sample, key,
//     playhead, zero rule and name chip the model publishes, each grown to its own
//     ink bound) must come back byte-identical to the clear colour. If a background
//     or a grid line is ever added, this is the assertion that fails.
//   2 the curve stroke lands where the model says: sampled points along the
//     synthetic polylines - including one between two key squares - carry ink.
//   3 the key MARKERS are painted, and their colours are the palette's own
//     tokens (extraction stated at each site). A key is a
//     DOT: the ink is a disc of radius ctrl::graph::kKeyDotRadius about
//     PanelCurve::keyDots, ringed in theme::Graph::keyEdge(), and it is NOT the
//     editor's pick geometry - the corners of the key's 2*kKeyRadius HIT square
//     carry no ink, which is what fails if the drawn marker is ever the pick
//     rectangle again. The marker's own colour is read in BOTH states of the
//     emphasis decision (gui::curveStrength, off the model's marker flags, from
//     passes of one model that differ only in those flags): with NOTHING selected
//     every marker is dimmed - still tinted by its own channel, but not the
//     undimmed token - and in the pass that puts its channel in play it IS the
//     channel's own colour, except for the marker the pass SELECTS, whose fill
//     moves toward the keyframe highlight (theme::Graph::axisText()) and which
//     alone carries the detached 1px ring in the TIMELINE's ink: the channel's RAW
//     hue at ctrl::time::kSelectRingAlpha (0.85), the same token time_Renderer's
//     key pass draws, not the fill's mixed colour. Item 29 is the three marker
//     states side by side.
//   4 the label halo, behind gui::kDrawTextHalo (SHIPPED OFF): with the flag on, a
//     pixel 1-2px outside the advance box is inked and is closer to
//     theme::Graph::halo() than to theme::Graph::axisText(), so it is the halo and
//     not a glyph; with it off - which is what ships - nothing is painted outside
//     the glyph box at all, which is the same statement from the other side. The
//     flag is read from src/gui/GraphPanelFrame.h, so this file cannot drift from the
//     panel's own configuration.
//   6 the empty state: the reason is drawn, and the rest of the plot stays clean.
//   7 the plot clip: a curve that leaves the value window in y paints NOTHING
//     outside the plot - not into the value column, not below the panel.
//   8 the zero rule: present at zeroY, absent at zeroY == -1.
//   9 the panel's CHROME, on the model's own geometry: a curve's name CHIP is drawn
//     ONLY while that curve is in play (the channels that own a selected key - the
//     pair is the empty-selection pass, where no chip is drawn at all - plus whatever
//     the pointer is over); the chips are drawn BEFORE the playhead (a chip moved onto
//     the playhead's column cannot cover it); and an Auto segment's GHOST handle is a
//     SOLID dot of its reduced-chroma channel hue where a Custom one's dot is filled
//     in theme::Graph::handleFill().
//  10 the Custom tangent handles: each handle's stem carries ink at its own
//     midpoint (the channel hue mixed 0.2 toward the panel's halo, so it reads on its
//     own curve) and each stored dot is theme::Graph::handleFill() at its own
//     coordinates; with the pointer on one dot (a second pass that differs ONLY in
//     the pointer) that dot takes the HUE-DERIVED highlight (the same reduced hue
//     lifted 0.45 toward axisText, not the structural accent) while every other dot
//     is unchanged - the highlight at the same distance the editor's own hit-test
//     grabs through.
//  16 the frame numbers' reserved band: it is a SECTION ABOVE THE PLOT (band.bottom()
//     + 1 == plot.top(), the two rects disjoint), every frame label's box is inside it
//     and clear of the tick rows, and the glyph box the panel actually used
//     (drawLabel's own clamped placement) is inside it - so a number that left the
//     ruler section fails here instead. ROW FOR ROW WITH THE TIMELINE, on the pixels
//     of a pair of passes: a frame number's first inked row is the one the backend's
//     TOP-ALIGNED placement produces (box.top + the font's ascent + the glyph's tight
//     top), read at a pixel size where a CENTRED glyph would start two rows lower -
//     and a frame tick inks the band's last row AND the kTickLength rows above it.
//  17 the playhead's own extent: the document's frame is ONE pixel column that
//     runs from the ruler band's pill column (above the plot) through the plot's
//     whole vertical span in theme::Graph::selected() and puts nothing in the
//     neighbouring columns - the structural accent crossing the section boundary.
//     Its pass also proves the chips are drawn BEFORE it: a chip moved onto the
//     playhead's column cannot cover it.
//  18 the box-select marquee, as a pair: the same model with and without
//     gui::GraphPanelModel::marquee, whose body must be
//     theme::Graph::marqueeFill() composited on what is under it and whose 1px edge
//     the edge token - and nothing at all where no drag is live.
//  20 the handles' FOCUS rule: with a focus, only the focused channel draws its
//     tangent handles. The pair is the same model with and without a focus on the
//     curve that has none, and the assertion is the INK: the unfocused curve's dots
//     and stem pixels are inked in one pass and clean in the other. (An earlier form
//     counted submitted vertices; the Canvas host has no such counter, and the ink is
//     the stronger statement anyway.)
//  21 THE VALUE COLUMN'S TWO OBLIGATIONS, measured as a pair of models: a right-aligned
//     value label keeps kValueLabelGap (8px) from its box's right edge when the box can
//     spare it - the gutter that stops the top value label and the plot's first frame
//     number reading as one string (the timeline's own kNumberMargin for the same
//     defect) - and a label WIDER than its box is cut at the panel's own edge instead of
//     painting outside the panel. The first is measured against the glyph box the
//     drawing layer used, the second against a model whose panel starts inside the
//     framebuffer.
//  28 THE TEMPORARY PLAYBACK WINDOW'S OVERLAY: the ruler-band wash, the 2px bracket
//     stems and the label arms in the timeline's own tokens and order; the text column
//     is the timeline's (left + kPillPadding, not a second centring on the drawing
//     font's advance), the arms' outer corners are cut by the shared pill radius, and a
//     window narrower than its labels collapses to ONE centred merged pill with no arms.
//  29 THE THREE MARKER STATES, on one key's two components: the SELECTED marker's
//     fill moves toward the keyframe highlight and its detached 1px ring is drawn in
//     the timeline's own ink - the channel's raw hue at 0.85; the SIBLING marker (its
//     key selected on the other component) keeps a FAINT ring at the same radius, the
//     same hue at 0.35, and a fill that did not brighten; the HOVERED marker brightens
//     and adds NO ring. Read against an unflagged pass of the same model, so each claim
//     is about the state and not about the fixture.
//  31 THE SCROLL BARS ARE THE APPLICATION'S: the model's lanes and runs are drawn
//     as src/gui/AppStyle.cpp's CC_ScrollBar - a 4px, radius-2 pill in
//     theme::Colors::outline, an INVISIBLE trough - with no ink outside the pills,
//     and the hovered bar's pill in theme::Colors::hover while the other bar's stays
//     at rest (measured in BOTH directions). A zero-travel lane is still a full-length
//     pill in EITHER direction: the vertical bar never hides. Read against the same
//     model's empty-rects pass, so each claim is about the drawing layer and not the
//     fixture. 31b pins the ruler's hairline row to the opaque Colors::hairline token,
//     over the band's own surface - the one rule both views draw.
//  32 THE PROPORTIONAL MARKER IS A RING: on one channel's absolute/proportional
//     pair, both selected, the proportional marker's CENTRE is the panel floor
//     (a hole), and its hue is a band at ctrl::time::kProportionalRingOuterInset /
//     kProportionalHoleInset; the absolute marker beside it is a solid selected
//     dot. The edge stroke keeps its own ink, strength and coverage, and the
//     selection ring is byte-identical to the same marker in a control pass whose
//     only difference is keyProportional[1] = false. A build that still lowers the
//     fill's alpha leaves the proportional centre filled, which fails here.
//  33 THE FRAME RANGE'S GUIDES: two 1px columns in theme::Graph::selected(), inside the
//     plot's whole vertical span and clear of every other mark, with the receded
//     classification drawn at its own alpha - a pair that isolates the guides from the
//     rest of the model.
//
// WHAT THIS FILE CANNOT SEE. The key menu is the APPLICATION'S OWN QMenu
// (gui::MainDisplayWidget::showGraphKeyMenu -> the timeline's key menu), a native popup
// window drawn by Qt outside the widget's frame: there is no popup to open, measure or
// click here, and the pixel gate owns no document to build one from. What it can still
// say about the menu is nothing, in pixels; the route itself (a right-click within
// ctrl::graph::kKeyHitRadius of a key's centre selects that key and calls the widget)
// is the code path's business, and the application-level probe's.
//
// Every number is printed as `MEASURED <what> ...` so a failure states the
// measurement, not only the verdict, and every precondition is asserted so a run
// that could not observe the behaviour fails loudly instead of passing quietly.
//
// HOW IT OWNS ITS PIXELS. QCanvasPainterWidget owns the RHI (GL) target its paint runs
// on, and grabFramebuffer() hands back the composed frame; renderPass() creates one
// widget per pass, sizes it to 400x320 and reads it back. The gate needs a window system
// with a real GL implementation (scripts/gui/gpu_headless.sh is the default; xvfb-run
// works), and it asserts the readback's size, because every sampled pixel is in the
// widget's own logical coordinates.
//
// The viewport is 400x320 while the panel is 400x300: the 20 rows below the panel
// are the visible "below the panel" probe of assertion 7.

#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QtMath>
#include <QDir>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonObject>
#include <QPair>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

#include "XCAssert.h"

#include "gui/GraphPanelModel.h"
#include "gui/GraphPanelFrame.h"
#include "gui/InkQPainter.h"
// THE SHIPPING HOST'S TWO PIECES: the Qt 6.12 Canvas Painter module and the backend that
// implements gui::ink::Ink over it. The gate renders through this pair and nothing else.
#include <QCanvasPainter>
#include <QCanvasPainterWidget>
#include "gui/InkCanvas.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Graph.h"
#include "ctrl/graph/graph_Geometry.h"
// The pill every bar assertion below measures against: the same header the panel's
// drawing layer and AppStyle's CC_ScrollBar paint from.
#include "ctrl/graph/graph_ScrollBarGeometry.h"
// The selected key ring is ONE token for both views: its alpha and its offset live in
// the timeline renderer's own header, and the marker assertions below read them.
#include "ctrl/time/time_Renderer.h"
// The marker fixture's two LAYERS: the marker hover is answered by gui::panelKeyAt, which
// names a key by (node, channel, frame), so a hand-built model that leaves `node` null
// cannot be hovered (the model a real build makes never does: its rows come from the
// editor's own targets). A FolderNode is the cheapest concrete ObjectNode in the tree and
// nothing here renders it - only its address is compared.
#include "core/FolderNode.h"

// The ctrl/core archives this target links reference the GUI
// AudioPlaybackWidget::deserialize (core::Project.cpp uses it for a .aemus
// sidecar). Nothing here ever loads a project, so stub it with the same one-liner
// tests/overlay_engine_primitives.cpp uses and the target needs no gui library.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

// THE HOVER-VS-PRESS ASSERTION drives a REAL ctrl::GraphEditor (see
// checkHoverMatchesPress below): the press half has to be the shipping hit-test, not a
// restatement of it, and the hover half is gui::panelPickAt on the model that editor
// builds. QMouseEvent is only the press/release the cursor consumes.
#include <QMouseEvent>
#include "cmnd/BasicCommands.h"
#include "cmnd/Stack.h"
#include "core/ChannelKey.h"
#include "core/AbstractCursor.h"
#include "core/Animator.h"
#include "core/CameraInfo.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "core/RotateKey.h"
#include "core/TimeKeyType.h"
#include "core/TimeLine.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/UILogger.h"
#include "util/Easing.h"

// The overlay hands its key menu to the WIDGET it lives in
// (gui::MainDisplayWidget::showGraphKeyMenu), and its CHANNEL list to the same widget
// (showGraphChannelMenu - the panel's one control opens the application's own QMenu).
// There is no gui library to link and this
// target's source list carries no widget (see the target's own comment), so the TWO
// symbols MainDisplayWidget's menu routes declare are stubbed here - the same move the
// AudioPlaybackWidget stub above makes. The gate drives gui::drawPanel, gui::outline and
// the frame driver directly and calls neither of them (the frame driver stopped naming
// the widget when the overlay host was removed), so the stubs are there to keep a future
// reference from failing this target's LINK quietly.
#include "gui/MainDisplayWidget.h"
bool gui::MainDisplayWidget::showGraphKeyMenu(const core::TimeLineEvent::Target&, const QPoint&) { return false; }
void gui::MainDisplayWidget::showGraphChannelMenu(const QVector<gui::PanelChannelRow>&, const QPoint&) {}

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    // An engine invariant firing means this harness can no longer describe what
    // it measured (a pending GL error, an unexpected state). Die loudly.
    void failure() const override { std::abort(); }
};

class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString& aText, const QString& aInfo, const QString& aDetail) const override {
        std::fprintf(stderr, "XC_FATAL %s %s %s\n", qPrintable(aText), qPrintable(aInfo), qPrintable(aDetail));
        std::abort();
    }
};

HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

#define REQUIRE(cond, ...)                                                                              \
    do {                                                                                                \
        if (!(cond)) {                                                                                  \
            std::printf("FAIL %d: ", __LINE__);                                                         \
            std::printf(__VA_ARGS__);                                                                   \
            std::printf("\n");                                                                          \
            return false;                                                                               \
        }                                                                                               \
    } while (false)

std::string hex(const QColor& aColor) { return aColor.name(QColor::HexRgb).toStdString(); }

// The framebuffer. WIDER AND TALLER THAN THE PANEL on purpose: the panel covers
// a 400x300 viewport, so rows 300..319 of the 400x320 attachment are "below the
// panel" and can be sampled (assertion 7).
constexpr int kViewW = 400;
constexpr int kViewH = 320;

const QRect kPanel(0, 0, 400, 300);
// The plot's top is the panel's kMargin plus the scaler's 22 reserved band rows - the
// shipping rule (Scaler::plotRect) - so the band's slot is the panel's top row.
const QRect kPlot(90, 26, 290, 240);        // x 90..379, y 26..265
// THE RULER BAND, as a SECTION AT THE PANEL'S TOP ROW (the model's own geometry):
// 22 rows starting at panel.top(), with the scaler's kMargin rows between the band's
// hairline and the plot's first row - the same rule the real model publishes
// (GraphPanelModel::rulerBand). It is outside the plot clip, so no
// curve/key/handle/chip can ink it.
const QRect kRulerBand(
    kPlot.left(), kPlot.top() - ctrl::graph::Scaler::kMargin - gui::frameRuler::kBandHeight, kPlot.width(),
    gui::frameRuler::kBandHeight
);
// The value column, in the model's OWN relationship to the plot: its right edge one
// pixel left of the plot's (GraphPanelModel.h: "It never overlaps the plot"), which is
// what puts the value numbers where the real panel puts them.
const QRect kValueColumn(6, 26, 83, 240);   // x 6..88, clear of the plot

// A value label's box, in the model's own arithmetic: the column,
// ctrl::graph::Scaler::kAxisLabelPad (4px) narrower on the right (GraphPanelModel.h's
// LABELS note), the drawing layer right-aligning the text in it. This is the box that
// makes the top value label and the plot's first frame number meet at the panel's
// top-left corner - the congestion assertion 21 measures.
QRect valueLabelBox(int aTop) {
    return QRect(kValueColumn.left(), aTop, kValueColumn.width() - ctrl::graph::Scaler::kAxisLabelPad, 14);
}

// The marquee rectangle the assertion 18 pass uses: inside the plot, and clear of
// every other piece of the full model's geometry (verified, grown by 2px, in
// checkMarquee - a marquee body is only measurable where nothing else is painted).
const QRect kMarqueeRect(155, 62, 40, 30);

// THE SCROLL BARS, as this file's synthetic model publishes them: the shipping layout
// of ctrl::graph::Scaler::scrollBars - the app's 12px lane
// (ctrl::graph::scrollBar::kLaneThickness) FLUSH with the panel's bottom and right
// edges, exactly as the timeline's own bar is:
// the horizontal lane is the panel's last 12 rows across its whole width, the vertical
// one the panel's last 12 columns from the plot's first row to the row above the
// horizontal lane (the corner is the horizontal bar's). A run is the visible window's
// fraction of the content rather than the whole track. The real model computes these
// from the editor's scaler; this file builds its models by hand, so the drawing layer
// is measured against exactly the four rects a model can carry. Both lanes are OUTSIDE
// the plot (the plot's right/bottom inset IS the lane) and INSIDE the panel.
const QRect kHScrollTrack(
    kPanel.left(), kPanel.bottom() - ctrl::graph::scrollBar::kLaneThickness + 1, kPanel.width(),
    ctrl::graph::scrollBar::kLaneThickness
);
const QRect kHScrollThumb(kHScrollTrack.left() + 60, kHScrollTrack.top(), 90, kHScrollTrack.height());
const QRect kVScrollTrack(
    kPanel.right() - ctrl::graph::scrollBar::kLaneThickness + 1, kPlot.top(),
    ctrl::graph::scrollBar::kLaneThickness,
    kPanel.bottom() - ctrl::graph::scrollBar::kLaneThickness - kPlot.top() + 1
);
const QRect kVScrollThumb(kVScrollTrack.left(), kVScrollTrack.top() + 40, kVScrollTrack.width(), 70);

// The PILL the drawing layer puts in a lane, from the ONE definition the drawing
// layer itself paints from (ctrl/graph/graph_ScrollBarGeometry.h): AppStyle's 4px,
// centred in the lane across the run (the model's thumb rect). The tests measure the
// ink against this, so the geometry the style and the panel agree on is asked for
// here rather than restated - if the panel's draw ever moved off it, the ink would
// land outside this rect and the assertions below fail.
QRect scrollPill(const QRect& aTrack, const QRect& aRun, bool aHorizontal) {
    return ctrl::graph::scrollBar::pill(aTrack, aRun, aHorizontal ? Qt::Horizontal : Qt::Vertical).rect;
}

const core::TimeKeyType kTypeA = core::TimeKeyType_Move;
const core::TimeKeyType kTypeB = core::TimeKeyType_Scale;

// The colour every pass clears to. Magenta: the panel draws no background, so
// everything the panel DOES draw has to move away from it. The precondition in
// main() proves that claim for every colour this model can paint, instead of
// assuming it.
const QColor kClear(255, 0, 255);

// "this pixel carries ink": the largest per-channel distance from the clear
// colour. 24 is far above the readback's noise (there is none: every pass is a
// fresh clear plus one blur-free draw) and far below any real stroke.
constexpr int kInkMin = 24;

// An OPAQUE fill (a key marker's body, a tangent handle's dot, the key ring) is the
// palette token itself, so it may be compared tightly.
constexpr int kFillTol = 10;

// An antialiased 1.5px stroke lands with partial coverage on its own fringes, so
// the curve colour is searched for in a small ring with a looser tolerance.
constexpr int kCurveTol = 48;

enum class Align { Left, Centre, Right };
// drawLabel's vertical axis (src/gui/GraphPanelFrame.cpp's LabelVAlign): Centre is
// the clamped centring every non-ruler label uses, Top is the ruler numbers' own
// placement - the glyph line box starting on the box's first row, which is what Qt's
// top-aligned drawText does for TimeRuler.
enum class VAlign { Centre, Top };

//-------------------------------------------------------------------------------------------------
QColor pixelAt(const QImage& aImage, int aX, int aY) {
    const uchar* line = aImage.constScanLine(aY);
    const uchar* pixel = line + 4 * aX;
    return QColor(int(pixel[0]), int(pixel[1]), int(pixel[2]), int(pixel[3]));
}

int colourDistance(const QColor& a, const QColor& b) {
    return std::max(
        {std::abs(a.red() - b.red()), std::abs(a.green() - b.green()), std::abs(a.blue() - b.blue()),
         std::abs(a.alpha() - b.alpha())}
    );
}

int clearDistance(const QColor& aColor) { return colourDistance(aColor, kClear); }

// Source-over of a TRANSLUCENT token onto the clear colour: exactly the blend the
// GL backend leaves for a partially transparent source in front of an opaque
// destination (dst = src * a + dst * (1 - a), with the separate alpha term leaving
// the destination alpha at 1). The marquee's two tokens carry the timeline's own
// alphas (24/255 below, 230/255 on the edge), so a pixel they paint is this
// composite and not the token itself - comparing against the token would fail for
// every correct implementation.
QColor over(const QColor& aSource, const QColor& aDestination) {
    const double a = aSource.alphaF();
    return QColor(
        int(std::lround(aSource.red() * a + aDestination.red() * (1.0 - a))),
        int(std::lround(aSource.green() * a + aDestination.green() * (1.0 - a))),
        int(std::lround(aSource.blue() * a + aDestination.blue() * (1.0 - a))), 255
    );
}

// The same token with its alpha replaced, i.e. the ink a STRENGTH leaves behind: the
// drawing layer's toImU32Strength scales the colour's alpha by the strength, so it is
// the source of `over(...)` for a covered pixel.
QColor withAlpha(const QColor& aColour, float aAlpha) {
    QColor out = aColour;
    out.setAlphaF(aAlpha);
    return out;
}

// GraphPanelFrame.cpp's mixedToward: the channel-wise sRGB mix the panel uses for a
// key's selected fill/ring and for a handle's reduced-chroma hue. Restated here so an
// expected ink is the layer's own arithmetic, not a guessed colour.
QColor mixedToward(const QColor& aColour, const QColor& aToward, float aMix) {
    const float t = std::clamp(aMix, 0.0f, 1.0f);
    const auto step = [t](int aFrom, int aTo) {
        return int(std::lround(double(aFrom) + (double(aTo) - double(aFrom)) * double(t)));
    };
    return QColor(
        step(aColour.red(), aToward.red()), step(aColour.green(), aToward.green()),
        step(aColour.blue(), aToward.blue())
    );
}

// An OPAQUE ring pixel over the clear colour is the ink itself at its coverage. A
// framebuffer sample on a 1px ring is a partial-coverage blend, so "is this pixel the
// ink" has to be read as "is it on the line between the clear colour and that ink":
// the projection below recovers the coverage, and `over` rebuilds the exact blend a
// correct implementation leaves (the same arithmetic the drawing layer's AA path
// produces over the cleared frame).
double coverageOfTone(const QColor& aSample, const QColor& aInk) {
    const int sample[3] = {aSample.red(), aSample.green(), aSample.blue()};
    const int ink[3] = {aInk.red(), aInk.green(), aInk.blue()};
    const int clear[3] = {kClear.red(), kClear.green(), kClear.blue()};
    double num = 0.0;
    double den = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double e = double(ink[i] - clear[i]);
        num += double(sample[i] - clear[i]) * e;
        den += e * e;
    }
    return den > 0.0 ? std::clamp(num / den, 0.0, 1.0) : 0.0;
}

QColor toneAtCoverage(const QColor& aInk, double aCoverage) {
    return over(withAlpha(aInk, float(aCoverage)), kClear);
}

int inkCount(const QImage& aImage, const QRect& aRect) {
    const QRect r = aRect.intersected(aImage.rect());
    int count = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            if (clearDistance(pixelAt(aImage, x, y)) > kInkMin) {
                ++count;
            }
        }
    }
    return count;
}

QRect grow(const QRect& aRect, int aPixels) { return aRect.adjusted(-aPixels, -aPixels, aPixels, aPixels); }

// How many pixels of aRect carry the ink aColour ITSELF (within aColour aTolerance, 0
// by default). The panel's chrome is drawn in opaque palette tokens, so a covered pixel
// is the token exactly - the tolerance exists for the 1/255 of an antialiased edge and
// is 0 wherever the assertion is about the token itself.
int toneCount(const QImage& aImage, const QRect& aRect, const QColor& aColour, int aTolerance = 0) {
    const QRect r = aRect.intersected(aImage.rect());
    int count = 0;
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            if (colourDistance(pixelAt(aImage, x, y), aColour) <= aTolerance) {
                ++count;
            }
        }
    }
    return count;
}

// The rows (aHorizontal false) or columns (true) of aProbe that carry aColour within
// aTolerance: the first and last such one, or (0,-1) when the probe carries none.
// Used to pin the DRAWN pill's band to the shared rect's own edges: the probes that
// call it cross the pill through its middle, where the fill is opaque, so the extent
// measured is the pill's and not an antialiased fringe's. aTolerance is the rounded
// fill's edge coverage: Canvas2D leaves its straight edges at ~31/32 even when the
// shape is pixel-aligned, so the bar probes admit kFillTol while still failing a
// one-pixel shift (the floor is 190 away).
QPair<int, int> tokenSpan(
    const QImage& aImage, const QRect& aProbe, const QColor& aColour, bool aHorizontal, int aTolerance = 0
) {
    int lo = std::numeric_limits<int>::max();
    int hi = std::numeric_limits<int>::min();
    const QRect r = aProbe.intersected(aImage.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            if (colourDistance(pixelAt(aImage, x, y), aColour) > aTolerance)
                continue;
            const int at = aHorizontal ? x : y;
            lo = std::min(lo, at);
            hi = std::max(hi, at);
        }
    }
    return lo > hi ? QPair<int, int>(0, -1) : QPair<int, int>(lo, hi);
}

// How many pixel columns/rows separate two rects; 0 when they touch or overlap.
int rectGap(const QRect& a, const QRect& b) {
    const int dx = std::max(0, std::max(a.left() - b.right() - 1, b.left() - a.right() - 1));
    const int dy = std::max(0, std::max(a.top() - b.bottom() - 1, b.top() - a.bottom() - 1));
    return std::max(dx, dy);
}

// The glyph box the SHIPPING BACKEND's own placement produces: the same font, the same
// alignment, the same vertical rule and the same right-gap rule as gui::ink::InkCanvas::text()
// (a centred glyph is clamped so it never starts above the box's top edge; a Top-aligned one
// starts its LINE box on the box's first row, the timeline's own rule; a right-aligned label
// is pulled left by up to aRightGapPx, never by more than the box's spare width).
// Rebuilding it here is what lets the halo assertion say "this pixel is OUTSIDE the
// glyph box" rather than "somewhere near the text".
QRect glyphBox(
    const QRect& aBox, const QString& aText, const QFont& aFont, Align aAlign,
    VAlign aVAlign = VAlign::Centre, int aRightGapPx = 0
) {
    if (aText.isEmpty() || aBox.isEmpty()) {
        return QRect();
    }
    const QFontMetricsF metrics(aFont);
    const double advance = metrics.horizontalAdvance(aText);
    double x = double(aBox.left());
    if (aAlign == Align::Centre) {
        x += (double(aBox.width()) - advance) * 0.5;
    } else if (aAlign == Align::Right) {
        const double spare = std::max(0.0, double(aBox.width()) - advance);
        x = double(aBox.left() + aBox.width()) - advance - std::min(double(aRightGapPx), spare);
    }
    const double y = aVAlign == VAlign::Top
        ? double(aBox.top())
        : std::floor(double(aBox.top()) + std::max((double(aBox.height()) - metrics.height()) * 0.5, 0.0));
    return QRect(int(std::floor(x)), int(y), int(std::ceil(advance)), int(std::ceil(metrics.height())));
}

//-------------------------------------------------------------------------------------------------
// The synthetic model. Every coordinate below is chosen so that the assertions
// can point at a region and say exactly which geometry is and is not in it.
//
// ONE KEY, in the two currencies the model publishes: the HIT square
// (ctrl::graph::keyRect's own shape, 2 * kKeyRadius across - the editor's pick
// geometry, which drawPanel does not paint) and the MARKER centre
// (PanelCurve::keyDots, the point the dot is drawn about). They are the same
// centre, which is what makes "the marker sits on the key" checkable.
struct KeyFixture {
    QRect hit;
    QPointF dot;
};

KeyFixture keyAt(int aCentreX, int aCentreY) {
    KeyFixture key;
    key.hit = QRect(
        aCentreX - ctrl::graph::kKeyRadius, aCentreY - ctrl::graph::kKeyRadius,
        2 * ctrl::graph::kKeyRadius, 2 * ctrl::graph::kKeyRadius
    );
    key.dot = QPointF(aCentreX, aCentreY);
    return key;
}

// The ink bound of one key MARKER: the circle of radius kKeyDotRadius drawn about
// the centre, grown by 1px for the 1px edge stroke centred on that radius and 1px
// more for its antialiasing, so nothing the marker can paint falls outside it.
QRect keyDotInkBox(const QPointF& aDot) {
    const int radius = int(std::ceil(double(ctrl::graph::kKeyDotRadius))) + 2; // 3.75 -> 6
    return QRect(int(aDot.x()) - radius, int(aDot.y()) - radius, 2 * radius + 1, 2 * radius + 1);
}

gui::GraphPanelModel makeFullModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();   // no occluded top: the band is the section above the plot
    // THE RULER BAND the panel paints: the model's own 22px section ABOVE the plot,
    // with the numbers in its first 14 rows and the ticks rising from its last row.
    model.rulerBand = kRulerBand;

    model.frameTickX = {90, 140, 320};
    model.valueTickY = {70, 210};
    // THE RULER'S NUMBER ROW, at the model's own geometry: every box starts at
    // band.top() + kNumberTop and is one kNumberHeight tall (see checkRulerReserve).
    // THE CONGESTED CORNER is unchanged in x: the first frame number sits on the tick
    // at the plot's left edge, and the top value label is right-aligned in its box.
    model.frameLabels = {
        {QRect(90, kRulerBand.top() + gui::frameRuler::kNumberTop, 11, gui::frameRuler::kNumberHeight),
         QStringLiteral("0"), 0},
        {QRect(130, kRulerBand.top() + gui::frameRuler::kNumberTop, 20, gui::frameRuler::kNumberHeight),
         QStringLiteral("10"), 10},
        {QRect(310, kRulerBand.top() + gui::frameRuler::kNumberTop, 20, gui::frameRuler::kNumberHeight),
         QStringLiteral("20"), 20},
    };
    model.valueLabels = {
        {valueLabelBox(60), QStringLiteral("1")},
        {valueLabelBox(240), QStringLiteral("0")},
    };
    model.zeroY = 180;
    model.playheadX = 200;
    // THE PLAYHEAD'S PILL, as the model publishes it: the frame's own string and the
    // 14px rounded box around it, at the band's pill row, with the line starting
    // under it (frameRuler::kLineTop).
    model.playheadText = QStringLiteral("200");
    model.playheadPill = QRect(
        200 - 15, kRulerBand.top() + gui::frameRuler::kPillTop, 30, gui::frameRuler::kNumberHeight
    );
    // The line begins under the pill INSIDE the band and runs through the plot: the
    // playhead spans both sections.
    model.playheadLineTop = kRulerBand.top() + gui::frameRuler::kLineTop;

    const KeyFixture aKeys[] = {keyAt(150, 220), keyAt(250, 200)};
    gui::PanelCurve a;
    a.node = nullptr;
    a.channel = ctrl::graph::Channel(kTypeA, 0);
    a.points = {QPointF(110, 240), QPointF(150, 220), QPointF(200, 230), QPointF(250, 200), QPointF(300, 210)};
    a.keys = {aKeys[0].hit, aKeys[1].hit};
    a.keyDots = {aKeys[0].dot, aKeys[1].dot};
    a.keyFrames = {core::Frame(150), core::Frame(250)};
    // The marker state, parallel to the geometry as buildGraphPanelModel publishes it - all
    // false here (a panel with nothing selected), which is what makes the emphasis passes
    // the ONLY difference between the frames this file renders (each sets one flag).
    a.keySelected = {false, false};
    a.keySiblingSelected = {false, false};
    // Two tangent handles, as a Custom segment's pair. Their bases sit ON the
    // curve (a real handle's base is the key it belongs to, but a base on a key
    // centre would put the stem's own 1px over that key's sampled centre and
    // confound the key-square assertion), and their dots are clear of every
    // other element. All four points are integers, so the pointer probe below
    // can name the dot exactly.
    {
        gui::PanelHandle first;
        first.segment = 0;
        first.index = 0;
        first.base = QPointF(200, 230);
        first.dot = QPointF(185, 255);
        first.radius = ctrl::graph::kHandleRadius;
        gui::PanelHandle second;
        second.segment = 1;
        second.index = 1;
        second.base = QPointF(275, 205);
        second.dot = QPointF(300, 240);
        second.radius = ctrl::graph::kHandleRadius;
        a.handles = {first, second};
    }
    a.color = aTheme.channelColor(kTypeA, 0);
    a.channelRow = 0;
    // The name CHIP, at a box inside the plot whose own ink is measurable ALONE: it is
    // clear of every other element (nothing of curve A reaches past x=300 and its
    // stroke is at y>=200, so only the chip's own backing and glyphs ink this box),
    // and it is clear of the gate's own probe rects (the no-grid probe at
    // (340,200 30x30) and the marquee's rectangle) so those stay statements about the
    // panel's background.
    a.chip = QRect(300, 160, 72, 14);
    a.chipText = QStringLiteral("Move X");

    const KeyFixture bKeys[] = {keyAt(170, 140), keyAt(290, 130)};
    gui::PanelCurve b;
    b.node = nullptr;
    b.channel = ctrl::graph::Channel(kTypeB, 1);
    b.points = {QPointF(110, 120), QPointF(170, 140), QPointF(230, 100), QPointF(290, 130)};
    b.keys = {bKeys[0].hit, bKeys[1].hit};
    b.keyDots = {bKeys[0].dot, bKeys[1].dot};
    b.keyFrames = {core::Frame(170), core::Frame(290)};
    b.keySelected = {false, false};
    b.keySiblingSelected = {false, false};
    // The SECOND component: under the channel hue table this is the Y axis (green),
    // so the fixture has one X channel and one Y channel - two hues the ink can tell
    // apart, which is what the marker passes need (two first components now share X
    // red by design; section 7's group, not the hue, tells those apart).
    b.color = aTheme.channelColor(kTypeB, 1);
    b.channelRow = 1;
    // This curve leaves the window at (290,130), 10px left of its own chip box, so the
    // box is COMPLETELY clear of every other element: "a chip is drawn only for a curve
    // in play" is asserted on it (zero ink while the curve is dimmed, the chip's own
    // backing while it is in play).
    b.chip = QRect(300, 100, 72, 14);
    b.chipText = QStringLiteral("Scale Y");

    model.curves = {a, b};

    gui::PanelChannelRow rowA;
    rowA.node = nullptr;
    rowA.channel = a.channel;
    rowA.layer = QStringLiteral("A");
    rowA.name = a.chipText;
    rowA.color = a.color;
    rowA.visible = true;
    rowA.focused = false;

    gui::PanelChannelRow rowB;
    rowB.node = nullptr;
    rowB.channel = b.channel;
    rowB.layer = QStringLiteral("B");
    rowB.name = b.chipText;
    rowB.color = b.color;
    rowB.visible = true;
    rowB.focused = false;

    model.channels = {rowA, rowB};
    return model;
}

// The clip model: ONE curve and nothing else, because every other element of the
// panel is legitimately drawn outside the plot (the value labels live in the
// value column). With nothing else in the model, "no ink outside the plot" is a
// statement about the CLIP alone.
gui::GraphPanelModel makeClipModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();   // no occluded top: the band is the section above the plot

    gui::PanelCurve curve;
    curve.node = nullptr;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    // 650px above and 230px below a 240px-tall plot: the two steep segments leave
    // the plot's y range immediately, and the third runs across its middle.
    curve.points = {QPointF(110, -500), QPointF(150, 150), QPointF(250, 150), QPointF(290, 500)};
    curve.color = aTheme.channelColor(kTypeA, 0);
    curve.channelRow = -1;
    model.curves = {curve};
    // A second in-plot ink source that ALSO leaves the plot on every side: the editor
    // clips its marquee to the plot, so this is not a rectangle the editor would
    // publish - it is the drawing layer's own clip put under the same assertion the
    // curve is under. Every edge of it is outside the plot, so the only thing it can
    // paint on screen is its body, and the bands below stay clean only if the clip
    // holds.
    model.marquee = QRect(-100, -100, 900, 900);
    return model;
}

gui::GraphPanelModel makeEmptyModel() {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();   // no occluded top: the band is the section above the plot
    model.frameTickX = {140};
    model.valueTickY = {70};
    model.frameLabels = {{QRect(130, 40, 20, 14), QStringLiteral("10")}};
    model.valueLabels = {{valueLabelBox(60), QStringLiteral("1")}};
    model.emptyState = QStringLiteral("No keys on the selected layers");
    return model;
}

// The ghost model: ONE curve that carries BOTH kinds of handle pair, and nothing else
// that paints (no ticks, no labels, no keys, no chips) - so the pixels at the two dots
// are the handles' own ink and nothing else. The stored pair is a Custom segment's (the
// document holds it); the ghost pair is an Auto segment's
// DERIVED handles (PanelHandle::ghost), which the drawing layer must show as a
// smaller, solid dot of the reduced-chroma channel hue (never the stored surface).
gui::GraphPanelModel makeGhostModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();   // no occluded top: the band is the section above the plot

    gui::PanelCurve curve;
    curve.node = nullptr;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    // The curve's own stroke, well clear of both dots and both stems (y=150 against
    // their y>=240), so nothing but a handle can ink the pixels read below.
    curve.points = {QPointF(120, 150), QPointF(200, 150)};
    curve.color = aTheme.channelColor(kTypeA, 0);
    curve.channelRow = -1;

    gui::PanelHandle stored;
    stored.segment = 0;
    stored.index = 0;
    stored.base = QPointF(250, 240);
    stored.dot = QPointF(270, 260);
    stored.radius = ctrl::graph::kHandleRadius;
    stored.ghost = false;

    gui::PanelHandle ghost;
    ghost.segment = 1;
    ghost.index = 1;
    ghost.base = QPointF(300, 240);
    ghost.dot = QPointF(320, 260);
    ghost.radius = ctrl::graph::kHandleRadius;
    ghost.ghost = true;

    curve.handles = {stored, ghost};
    model.curves = {curve};
    return model;
}

//-------------------------------------------------------------------------------------------------
// The marker model: the TWO COMPONENTS OF ONE KEY, one marker each, and nothing else that
// paints (no ticks, no labels, no chips, no handles) - so the pixels about each dot are
// that marker's own ink and nothing else.
//
// WHY THE TWO CURVES SHARE A KEY TYPE. The design's three marker states are about ONE KEY
// drawn on two channels: "the marker of the same key that is not selected keeps a faint
// ring and its curve stays at full strength - one key, two channels, both in play"
// (index.html:244-245). A SIBLING is another component of the same key, so the pair has to
// be one type's two components - but they are NOT one hue: under the channel hue table the
// first component is X (red) and the second Y (green), so each marker's fill is its OWN
// channel's hue (PanelCurve::color == theme::Graph::channelColor(type, component)). That is
// what makes the per-curve "nearer its own channel than the other" claims meaningful.
//
// The markers sit 100px apart, each on the middle of its own horizontal stroke, so no
// probe about one can reach the other's ink: the ring probes below reach 7px, the stroke
// reaches 1px either side of its own row.
constexpr int kMarkerX = 200;
constexpr int kMarkerYTop = 120;
constexpr int kMarkerYBottom = 220;

gui::GraphPanelModel makeMarkerModel(const theme::Graph& aTheme, core::ObjectNode* aLayerX, core::ObjectNode* aLayerY) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top(); // no occluded top: the band is the section above the plot

    for (int component = 0; component < 2; ++component) {
        const int y = (component == 0) ? kMarkerYTop : kMarkerYBottom;
        const KeyFixture key = keyAt(kMarkerX, y);
        gui::PanelCurve curve;
        curve.node = (component == 0) ? aLayerX : aLayerY;
        curve.channel = ctrl::graph::Channel(kTypeA, component);
        curve.points = {QPointF(kMarkerX - 60, y), QPointF(kMarkerX + 60, y)};
        curve.keys = {key.hit};
        curve.keyDots = {key.dot};
        curve.keyFrames = {core::Frame(7)}; // ONE key, shared by both components
        // Both curves publish the marker state as the editor would: THIS fixture leaves it
        // unflagged (a panel with nothing selected), and the passes built from it set the
        // one flag each assertion is about.
        curve.keySelected = {false};
        curve.keySiblingSelected = {false};
        curve.color = aTheme.channelColor(kTypeA, component);
        curve.channelRow = component;
        model.curves.append(curve);
    }

    gui::PanelChannelRow rowA;
    rowA.node = aLayerX;
    rowA.channel = model.curves.at(0).channel;
    rowA.layer = aLayerX->name();
    rowA.name = QStringLiteral("Move X");
    rowA.color = model.curves.at(0).color;
    rowA.visible = true;
    gui::PanelChannelRow rowB = rowA;
    rowB.node = aLayerY;
    rowB.channel = model.curves.at(1).channel;
    rowB.layer = aLayerY->name();
    rowB.name = QStringLiteral("Move Y");
    rowB.color = model.curves.at(1).color;
    model.channels = {rowA, rowB};
    return model;
}

//-------------------------------------------------------------------------------------------------
// The PROPORTIONAL fixture: ONE channel, TWO markers - the first ABSOLUTE, the second
// PROPORTIONAL - and nothing else that paints. Both markers are SELECTED, so the channel
// is in play at full strength and each marker carries the same selection ring: the ONLY
// thing the drawing layer may change for the proportional one is its BODY - a hollow
// ring with the panel floor in the hole instead of a filled dot.
//
// The two markers are the same channel by construction (one curve), which is what makes
// the centre comparison a statement about the flag and not about two hues. No polyline is
// published, so each marker's centre is its fill over the panel's floor (the clear
// colour: this panel paints no background) and the expected ink can be stated exactly.
constexpr int kProportionalMarkerX = 220;
constexpr int kAbsoluteMarkerY = 120;
constexpr int kProportionalMarkerY = 200;

gui::GraphPanelModel makeProportionalMarkerModel(const theme::Graph& aTheme, core::ObjectNode* aLayer) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();

    const KeyFixture absolute = keyAt(kProportionalMarkerX, kAbsoluteMarkerY);
    const KeyFixture proportional = keyAt(kProportionalMarkerX, kProportionalMarkerY);
    gui::PanelCurve curve;
    curve.node = aLayer;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    curve.keys = {absolute.hit, proportional.hit};
    curve.keyDots = {absolute.dot, proportional.dot};
    curve.keyFrames = {core::Frame(7), core::Frame(8)};
    curve.keySelected = {true, true};
    curve.keySiblingSelected = {false, false};
    // The document's flag, per key: the first is absolute, the second proportional.
    curve.keyProportional = {false, true};
    curve.color = aTheme.channelColor(kTypeA, 0);
    curve.channelRow = 0;
    model.curves.append(curve);
    return model;
}

//-------------------------------------------------------------------------------------------------
// THE SHIPPING HOST, DRIVEN WITH A SYNTHETIC MODEL.
//
// On a Qt 6.12 tree the panel's only host is QCanvasPainterWidget (gui::GraphPanelWidget): its
// paint() hands the model to gui::drawPanel over gui::ink::InkCanvas, and the widget clears its
// texture to the host's floor colour. The gate drives that pair directly - the same widget class,
// the same backend, the same drawPanel - and reads the frame back with the widget's own
// grabFramebuffer(), so what is asserted below is the drawing layer the application ships, not a
// rasteriser the test chose. No paint event of the application is involved: the widget is never
// shown, and the synthetic model is its only input.
struct Rendered {
    QImage image;
    QFont font;
};

// A pointer that is over nothing: the hover rules (a bar's pill, a handle's dot, a chip) must not
// fire in a pass that does not name the pointer itself. The old harness used FLT_MAX for the same
// reason; Canvas takes QPointF, so a coordinate no widget can hold does the job.
const QPointF kPointerOff(-1.0e9, -1.0e9);

class PanelCanvasWidget: public QCanvasPainterWidget {
public:
    PanelCanvasWidget(
        const gui::GraphPanelModel& aModel, const theme::Graph& aTheme, const gui::PanelFocus& aFocus,
        const gui::ink::Pointer& aPointer, const QFont& aFont
    )
        : mModel(aModel), mTheme(aTheme), mFocus(aFocus), mPointer(aPointer), mFont(aFont) {}

protected:
    void paint(QCanvasPainter* aPainter) override {
        gui::ink::InkCanvas ink(*aPainter, mFont);
        gui::drawPanel(ink, mModel, mTheme, mFocus, mPointer);
    }

private:
    const gui::GraphPanelModel& mModel;
    const theme::Graph& mTheme;
    const gui::PanelFocus& mFocus;
    const gui::ink::Pointer& mPointer;
    QFont mFont;
};

// One whole frame through the shipping host.
//
// THE FLOOR IS THE GATE'S MAGENTA, not the application's base colour: every assertion below reads
// "this pixel moved away from the clear colour", and a floor that is itself a panel token could be
// mistaken for ink. The widget clears to it every frame, exactly as the page clears to Colors::base.
//
// `aMouse` is the pointer the panel's hover rules read (off-screen by default); `aFont` overrides
// the drawing face (the over-tall-font case), and the application font is used when it is default.
Rendered renderPass(
    const gui::GraphPanelModel& aModel, const theme::Graph& aTheme, const gui::PanelFocus& aFocus,
    const char* aName, const QPointF& aMouse = kPointerOff, const QFont& aFont = QFont(), bool aQuiet = false
) {
    gui::ink::Pointer pointer;
    pointer.pos = aMouse;
    pointer.pressPos = kPointerOff;
    pointer.leftDown = false;

    PanelCanvasWidget widget(aModel, aTheme, aFocus, pointer, aFont);
    widget.setFillColor(kClear);
    widget.resize(kViewW, kViewH);

    Rendered out;
    // THE GATE'S PIXEL ACCESSORS READ RGBA BYTES, so the frame is normalised once here rather than
    // guessed at behind every sample: grabFramebuffer's format is the widget's, not this file's.
    out.image = widget.grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
    out.font = aFont;
    if (out.image.size() != QSize(kViewW, kViewH)) {
        qFatal(
            "the Canvas host returned a %dx%d frame for a %dx%d widget: every sampled pixel would be off",
            out.image.width(), out.image.height(), kViewW, kViewH
        );
    }
    if (!aQuiet) {
        std::printf(
            "MEASURED pass=%s size=%dx%d font_px=%d\n", aName, out.image.width(), out.image.height(),
            QFontInfo(out.font).pixelSize()
        );
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// AE_PANEL_CROPS=<dir>: write magnified crops of the ink this file just read back,
// as binary PPMs (no image plugin, no Qt codec involved), so a stroke can be looked
// at pixel by pixel instead of argued about. A DEVELOPER AID: nothing is written
// unless the variable is set, and every region is derived from the model's own
// geometry, so a crop cannot drift from what it claims to be a crop of.
void writePpm(const QImage& aImage, const QRect& aRegion, const QString& aPath, int aZoom) {
    const QRect r = aRegion.intersected(aImage.rect());
    if (r.isEmpty())
        return;
    FILE* out = std::fopen(aPath.toLocal8Bit().constData(), "wb");
    if (!out)
        return;
    std::fprintf(out, "P6\n%d %d\n255\n", r.width() * aZoom, r.height() * aZoom);
    std::vector<uchar> row(size_t(r.width()) * 3);
    for (int y = 0; y < r.height() * aZoom; ++y) {
        for (int x = 0; x < r.width(); ++x) {
            // Nearest neighbour: a magnified crop has to show the pixels themselves,
            // not a resampled blur of them.
            const QColor c = pixelAt(aImage, r.left() + x, r.top() + y / aZoom);
            row[size_t(x) * 3 + 0] = uchar(c.red());
            row[size_t(x) * 3 + 1] = uchar(c.green());
            row[size_t(x) * 3 + 2] = uchar(c.blue());
        }
        for (int z = 0; z < aZoom; ++z) {
            std::fwrite(row.data(), 1, row.size(), out);
        }
    }
    std::fclose(out);
    std::printf(
        "MEASURED crop file=%s region=(%d,%d %dx%d) zoom=%d\n", qPrintable(aPath), r.left(), r.top(), r.width(),
        r.height(), aZoom
    );
}

void dumpInkCrops(const Rendered& aRendered, const gui::GraphPanelModel& aModel, const QString& aTag) {
    const char* dir = std::getenv("AE_PANEL_CROPS");
    if (!dir || !*dir)
        return;
    const int zoom = 10;
    // The whole attachment as well: an analysis can then point at any region without
    // another run of the gate.
    writePpm(aRendered.image, aRendered.image.rect(), QStringLiteral("%1/%2_full.ppm").arg(dir, aTag), 1);
    for (const gui::PanelCurve& curve : aModel.curves) {
        if (curve.points.size() < 2) {
            continue;
        }
        // A vertex in the interior of the polyline: the one place a joint shows.
        const QPointF mid = curve.points.at(curve.points.size() / 2);
        writePpm(
            aRendered.image, QRect(int(mid.x()) - 14, int(mid.y()) - 9, 28, 18),
            QStringLiteral("%1/%2_curve_type%3.ppm").arg(dir, aTag).arg(int(curve.channel.type())), zoom
        );
        for (int i = 0; i < curve.keys.size(); ++i) {
            writePpm(
                aRendered.image, grow(curve.keys.at(i), 4),
                QStringLiteral("%1/%2_key_type%3_%4.ppm").arg(dir, aTag).arg(int(curve.channel.type())).arg(i), zoom
            );
        }
    }
    if (aModel.playheadX >= 0) {
        writePpm(
            aRendered.image, QRect(aModel.playheadX - 6, aModel.plot.top() + 40, 13, 24),
            QStringLiteral("%1/%2_playhead.ppm").arg(dir, aTag), zoom
        );
        writePpm(
            aRendered.image, QRect(aModel.playheadX - 6, aModel.plot.bottom() - 20, 13, 24),
            QStringLiteral("%1/%2_playhead_bottom.ppm").arg(dir, aTag), zoom
        );
    }
    for (const gui::PanelLabel& label : aModel.frameLabels) {
        writePpm(
            aRendered.image, grow(label.rect, 4),
            QStringLiteral("%1/%2_ruler_%3.ppm").arg(dir, aTag, label.text), zoom
        );
    }
}

//-------------------------------------------------------------------------------------------------
// The model's own geometry, as ink bounds. Used to PROVE that the no-geometry
// probe rect of assertion 1 holds nothing: a curve is represented by its whole
// polyline sampled at 1px steps (a stroke is a line, not just its vertices), and
// every rect is grown by the ink its own draw call can add beyond its box.
struct Geom {
    std::string name;
    QRect rect;
};

void addCurveSamples(std::vector<Geom>& aOut, const QVector<QPointF>& aPoints) {
    for (int i = 0; i + 1 < aPoints.size(); ++i) {
        const QPointF a = aPoints.at(i);
        const QPointF b = aPoints.at(i + 1);
        const int steps = std::max(1, int(std::hypot(b.x() - a.x(), b.y() - a.y())) + 1);
        for (int s = 0; s <= steps; ++s) {
            const double t = double(s) / double(steps);
            const double x = a.x() + t * (b.x() - a.x());
            const double y = a.y() + t * (b.y() - a.y());
            // The stroke's own half extent, PLUS one pixel of slack: the panel strokes its
            // curves at 2.0px (gui::kCurveWidth), so a 2px band reaches 1px either side of
            // the centreline and its antialiasing adds at most one more - a 5x5 box around
            // the floored sample grown by 1 is a bound, never a guess.
            aOut.push_back({"curve", grow(QRect(int(std::floor(x)) - 2, int(std::floor(y)) - 2, 5, 5), 1)});
        }
    }
}

std::vector<Geom> geometryOf(const gui::GraphPanelModel& aModel, const QFont& aFont) {
    std::vector<Geom> out;
    if (!aModel.rulerBand.isEmpty()) {
        // The band's surface and its hairline row, plus the playhead's pill when one
        // is published: all three are ink the drawing layer puts down.
        out.push_back({"ruler_band", grow(aModel.rulerBand, 1)});
        out.push_back(
            {"ruler_hairline",
             grow(QRect(aModel.rulerBand.left(), aModel.rulerBand.bottom() + 1, aModel.rulerBand.width(), 1), 1)}
        );
    }
    if (!aModel.playheadPill.isEmpty()) {
        out.push_back({"playhead_pill", grow(aModel.playheadPill, 1)});
    }
    // The frame ticks hang UP from the band's last row (frameRuler::kTickLength rows).
    const int tickBaseline = aModel.rulerBand.isEmpty() ? aModel.rulerBandTop + gui::frameRuler::kBandHeight - 1
                                                        : aModel.rulerBand.bottom();
    for (int x : aModel.frameTickX) {
        out.push_back(
            {"frame_tick",
             grow(QRect(x, tickBaseline - gui::frameRuler::kTickLength + 1, 1, gui::frameRuler::kTickLength), 2)}
        );
    }
    for (int y : aModel.valueTickY) {
        out.push_back({"value_tick", grow(QRect(aModel.plot.left() - 1, y, 6, 1), 2)});
    }
    for (const gui::PanelLabel& label : aModel.frameLabels) {
        out.push_back(
            {"frame_label", grow(glyphBox(label.rect, label.text, aFont, Align::Left, VAlign::Top), 2)}
        );
    }
    for (const gui::PanelLabel& label : aModel.valueLabels) {
        out.push_back(
            {"value_label",
             grow(glyphBox(
                      label.rect, label.text, aFont, Align::Right, VAlign::Centre, gui::kValueLabelGap
                  ),
                  2)}
        );
    }
    if (aModel.zeroY >= 0) {
        out.push_back(
            {"zero_rule", grow(QRect(aModel.plot.left(), aModel.zeroY, aModel.plot.width() + 1, 1), 2)}
        );
    }
    if (aModel.playheadX >= 0) {
        const int lineTop = aModel.playheadLineTop >= 0 ? aModel.playheadLineTop : aModel.plot.top();
        out.push_back(
            // The line now runs to the PANEL's last row (the timeline's playhead
            // crosses its whole body), so the ink bound ends there.
            {"playhead", grow(QRect(aModel.playheadX, lineTop, 1, aModel.panel.bottom() - lineTop + 1), 2)}
        );
    }
    for (const gui::PanelCurve& curve : aModel.curves) {
        addCurveSamples(out, curve.points);
        // The key MARKER's ink bound - the circle drawPanel paints, not the hit
        // square it does not. The hit rects are still published by the model and are
        // still what the editor picks with, so a probe that has to be clear of the
        // panel's ink must clear the DOT; the hit rects are not ink at all.
        for (const QPointF& dot : curve.keyDots) {
            out.push_back({"key_dot", keyDotInkBox(dot)});
        }
        for (const gui::PanelHandle& handle : curve.handles) {
            addCurveSamples(out, QVector<QPointF>{handle.base, handle.dot}); // the stem
            const QRect dot(
                int(handle.dot.x()) - handle.radius, int(handle.dot.y()) - handle.radius,
                2 * handle.radius + 1, 2 * handle.radius + 1
            );
            out.push_back({"handle_dot", grow(dot, 2)});
        }
    }
    // The panel's CHROME, on the model's own geometry: a chip is drawn only while its
    // curve is in play, and the geometry set carries every box a pass in this file could
    // draw, so "no ink outside the geometry" stays a statement about the panel rather
    // than about the passes that happened not to draw one.
    for (const gui::PanelCurve& curve : aModel.curves) {
        if (curve.chip.isEmpty()) {
            continue;
        }
        out.push_back({"chip", grow(curve.chip, 2)});
        out.push_back({"chip_text", grow(glyphBox(curve.chip, curve.chipText, aFont, Align::Centre), 2)});
    }
    if (!aModel.emptyState.isEmpty()) {
        out.push_back(
            {"empty_state",
             grow(glyphBox(aModel.plot, aModel.emptyState, aFont, Align::Centre), 2)}
        );
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// 0: the machinery's own control. drawPanel returns immediately for a model with
// no panel rect, so a pass that draws nothing must leave the widget's floor in every
// pixel - otherwise the readback or the counter invents pixels and every number below
// is worthless.
bool checkControl(const Rendered& aRendered) {
    const int ink = inkCount(aRendered.image, QRect(0, 0, kViewW, kViewH));
    int offFloor = 0;
    for (int y = 0; y < aRendered.image.height(); ++y) {
        for (int x = 0; x < aRendered.image.width(); ++x) {
            if (colourDistance(pixelAt(aRendered.image, x, y), kClear) != 0)
                ++offFloor;
        }
    }
    std::printf("MEASURED control ink_pixels=%d off_floor_pixels=%d of %d\n", ink, offFloor, kViewW * kViewH);
    REQUIRE(offFloor == 0, "the control pass painted %d pixels while nothing was drawn", offFloor);
    REQUIRE(ink == 0, "the control pass's ink counter disagrees with its floor count (%d)", ink);
    return true;
}

// 1: the inverse no-grid assertion.
bool checkNoGrid(const Rendered& aRendered, const gui::GraphPanelModel& aModel) {
    // The gate's own font is the application font unless a pass overrode it; the canvas backend
    // has already been handed it, so the glyph arithmetic below and the glyphs share one face.
    REQUIRE(!aModel.panel.isEmpty() && !aModel.plot.isEmpty(), "precondition: the model must have a plot");

    // A rect inside the plot, in the top-right quadrant, chosen against the model
    // below: the nearest thing to it is the second frame label's halo.
    const QRect probe(340, 200, 30, 30);
    REQUIRE(kPlot.contains(probe), "precondition: the probe rect must be inside the plot");
    REQUIRE(kPanel.contains(probe), "precondition: the probe rect must be inside the panel");
    REQUIRE(!probe.intersects(aModel.valueColumn), "precondition: the probe rect must be outside the value column");

    const std::vector<Geom> geoms = geometryOf(aModel, aRendered.font);
    std::string nearest = "<none>";
    int minGap = -1;
    for (const Geom& geom : geoms) {
        REQUIRE(
            !probe.intersects(geom.rect),
            "precondition: the no-geometry probe rect (%d,%d %dx%d) intersects %s (%d,%d %dx%d)", probe.left(),
            probe.top(), probe.width(), probe.height(), geom.name.c_str(), geom.rect.left(), geom.rect.top(),
            geom.rect.width(), geom.rect.height()
        );
        const int gap = rectGap(probe, geom.rect);
        if (minGap < 0 || gap < minGap) {
            minGap = gap;
            nearest = geom.name;
        }
    }

    const int ink = inkCount(aRendered.image, probe);
    // The control that guards against a false negative: the SAME rect, moved onto
    // the zero rule, must come back inked. If it did not, "0 ink" above would be a
    // property of the counter rather than of the panel.
    const QRect onRule(340, aModel.zeroY - 15, 30, 30);
    const int ruleInk = inkCount(aRendered.image, onRule);
    std::printf(
        "MEASURED no_grid sensitivity_control rect=(%d,%d %dx%d) contains_zero_rule=1 ink_pixels=%d of %d\n",
        onRule.left(), onRule.top(), onRule.width(), onRule.height(), ruleInk, onRule.width() * onRule.height()
    );
    REQUIRE(
        ruleInk > 0,
        "the no-grid probe counted 0 pixels over a rect that contains the zero rule: the probe cannot see ink at all"
    );
    std::printf(
        "MEASURED no_grid rect=(%d,%d %dx%d) geometry_items=%d nearest=%s gap_px=%d ink_pixels=%d of %d\n",
        probe.left(), probe.top(), probe.width(), probe.height(), int(geoms.size()), nearest.c_str(), minGap, ink,
        probe.width() * probe.height()
    );
    std::printf(
        "MEASURED no_grid margins: left_to_value_column=%dpx right_to_plot_edge=%dpx top_to_zero_rule=%dpx "
        "bottom_to_plot_edge=%dpx\n",
        probe.left() - aModel.valueColumn.right() - 1, kPlot.right() - probe.right(), probe.top() - aModel.zeroY,
        kPlot.bottom() - probe.bottom()
    );
    REQUIRE(
        ink == 0,
        "the panel painted %d pixels in a rect the model says holds no geometry: the panel must have no"
        " background and no grid line (nearest geometry: %s at %dpx)",
        ink, nearest.c_str(), minGap
    );
    return true;
}

// 2: the curve stroke lands where the model says.
//
// Each model point is sampled EXACTLY (tolerance 0) where the stroke runs through
// the pixel, and within 1px where the point is a polyline ENDPOINT: a round cap's
// coverage at its own terminal pixel can be partial, and the 2px stroke's antialiasing
// lands the strongest ink one pixel in (measured on this gate's Canvas host: (300,210)
// at the end of curve A reads full ink at (299,209), while every interior sample,
// including the ones between two key squares, is inked at its own point). The tolerance
// is stated per sample and printed with the offset that was actually found, so "within
// 1px of the model's point" is the claim being made - not "somewhere on the panel".
bool checkCurveInk(const Rendered& aRendered, const gui::GraphPanelModel& aModel) {
    struct Sample {
        QPointF point;
        const char* what;
        int tolerance;
    };
    // The samples at x=175/275 (curve A) and x=200 (curve B) sit BETWEEN two key
    // squares, where only the stroke can put ink.
    const std::vector<Sample> samples = {
        {{110, 240}, "curve_a endpoint", 1},
        {{130, 230}, "curve_a mid-segment", 0},
        {{175, 225}, "curve_a between two keys", 0},
        {{225, 215}, "curve_a mid-segment", 0},
        {{275, 205}, "curve_a between two keys", 0},
        {{300, 210}, "curve_a endpoint", 1},
        {{110, 120}, "curve_b endpoint", 1},
        {{140, 130}, "curve_b mid-segment", 0},
        {{200, 120}, "curve_b between two keys", 0},
        {{260, 115}, "curve_b mid-segment", 0},
        {{275, 122}, "curve_b mid-segment", 0},
    };

    std::vector<QRect> keyInk;
    for (const gui::PanelCurve& curve : aModel.curves) {
        for (const QPointF& dot : curve.keyDots) {
            keyInk.push_back(keyDotInkBox(dot));
        }
    }

    int checked = 0;
    for (const Sample& sample : samples) {
        const int x = int(std::lround(sample.point.x()));
        const int y = int(std::lround(sample.point.y()));
        REQUIRE(kPlot.contains(x, y), "precondition: sample %s (%d,%d) is outside the plot", sample.what, x, y);
        for (const QRect& ink : keyInk) {
            REQUIRE(
                !grow(ink, 1 + sample.tolerance).contains(x, y),
                "precondition: the neighbourhood of sample %s (%d,%d) reaches a key marker", sample.what, x, y
            );
        }

        int best = clearDistance(pixelAt(aRendered.image, x, y));
        int bestX = x;
        int bestY = y;
        for (int dy = -sample.tolerance; dy <= sample.tolerance; ++dy) {
            for (int dx = -sample.tolerance; dx <= sample.tolerance; ++dx) {
                const QColor c = pixelAt(aRendered.image, x + dx, y + dy);
                if (clearDistance(c) > best) {
                    best = clearDistance(c);
                    bestX = x + dx;
                    bestY = y + dy;
                }
            }
        }
        std::printf(
            "MEASURED curve_ink sample=%s model=(%d,%d) tolerance=%dpx best=(%d,%d) offset=(%+d,%+d) rgb=%s"
            " clear_distance=%d\n",
            sample.what, x, y, sample.tolerance, bestX, bestY, bestX - x, bestY - y,
            hex(pixelAt(aRendered.image, bestX, bestY)).c_str(), best
        );
        REQUIRE(
            best > kInkMin,
            "the curve stroke has no ink within %dpx of %s (%d,%d): the nearest sample is %d from the clear colour",
            sample.tolerance, sample.what, x, y, best
        );
        ++checked;
    }
    std::printf("MEASURED curve_ink samples_with_ink=%d of %d\n", checked, int(samples.size()));
    return true;
}

// 3: the key MARKERS - a dot of radius ctrl::graph::kKeyDotRadius about
// PanelCurve::keyDots, tinted by its channel, ringed in theme::Graph::keyEdge(), in one
// of the three states the MODEL classifies (PanelCurve::keySelected /
// keySiblingSelected) - and NOT the pick square.
//
// EXTRACTION: drawPanel paints a key with AddCircleFilled + AddCircle(keyEdge) about the
// marker centre, and PanelCurve::color is theme::Graph::channelColor(type, component) - the
// same colour the curve is stroked in, its chip's text is drawn in and its channel row
// carries (the model documents them as one). So the marker's CENTRE pixel is compared to
// its own channel colour and the pixel inside the edge stroke's band (the 1px stroke is
// centred on ctrl::graph::kKeyDotRadius) to keyEdge, and -
// because "the key is tinted by its channel" is a statement about a RELATION - each centre
// is also required to be nearer its own channel's colour than the other curve's, with the
// two channels' colours asserted distinguishable first (which is why the fixture uses one
// X component and one Y component: two first components now share the X hue by design). A
// single hardcoded ink cannot satisfy that: it is the two-channels-at-once check.
//
// THE MARKER'S STRENGTH IS THE SELECTION'S (gui::curveStrength, off the model's own
// marker flags): the fill and the edge ring both take the channel's strength, so each of
// these claims is measured in a pass where the marker is STRONG enough to read - the
// no-selection pass shows every key dimmed at kUnselectedAlpha (still tinted by its own
// channel, but not the undimmed token), and the selection passes put one channel in play,
// where its markers are the channel's own colour at full strength. The SELECTED marker's
// own ink is the one that changes (its fill moves toward the keyframe highlight and it
// gains a detached 1px ring), and it is read from the selection passes: see the state
// assertions in checkKeys, and assertion 29 for the three states side by side on one key.
//
// AND THE MARKER IS NOT THE PICK AREA (the model's visual/hit split). Three
// measurements say so, on the geometry the model still publishes for both:
//   * the ink's WIDTH through the centre is the marker's own diameter: the 7.5px
//     dot, its 1px stroke centred on that radius and their antialiasing (the width
//     assertion's own 7..11px; MEASURED 10px). A 10px hit square would measure the
//     same width, so it is the CORNERS below - not this width - that tell a dot
//     from the square;
//   * the four CORNERS of that hit square carry NO ink (they are ~7px from the
//     centre, past the marker's own ink - the edge band ends at 4.25 and its
//     antialiasing at 4.5 - and inside a 10px square's);
//   * the EDGE pixel's COLOUR is the dark edge ink, not the fill - which is also
//     what the edge is for: a same-hue key on a same-hue stroke would otherwise
//     vanish into it.
// ALL THREE are measured in a pass where that key carries NO selection ring (see the
// geometry pass selection in checkKeys): the ring is detached BY DESIGN (its ink starts
// ~5px out), so a width taken through the centre of a ringed marker would measure the
// marker AND its ring and say nothing about either one's size.
//
// The marker's ink width along a line through its centre: the furthest inked step on
// each side, so the caller can assert both the width AND that the ink is centred.
int inkWidthThrough(
    const QImage& aImage, const QPoint& aCentre, int aDx, int aDy, int aLimit, int& aNegative, int& aPositive
) {
    int negative = 0;
    int positive = 0;
    for (int step = 1; step <= aLimit; ++step) {
        if (clearDistance(pixelAt(aImage, aCentre.x() - step * aDx, aCentre.y() - step * aDy)) > kInkMin) {
            negative = step;
        }
        if (clearDistance(pixelAt(aImage, aCentre.x() + step * aDx, aCentre.y() + step * aDy)) > kInkMin) {
            positive = step;
        }
    }
    aNegative = negative;
    aPositive = positive;
    return negative + positive + 1;
}

// The pixel carrying the DETACHED ring's ink, out of the two rows a 1px stroke at
// ctrl::graph::kKeyDotRadius + ctrl::time::kSelectRingOffset can land in, read on the
// VERTICAL axis (a marker sits on its own curve, so the horizontal neighbour of a key
// pixel is the stroke, while the row below/above it is clear).
//
// WHY TWO ROWS AND NOT ONE. The ring is the TIMELINE's token (one expression, both
// views): its 1px band is CENTRED on dotRadius + kSelectRingOffset = 6.35, i.e.
// [5.85, 6.85]. The drawing layer asks gui::ink::InkCanvas::circleStroke for that radius
// plus half a pixel, and the backend centres its 1px stroke inside the radius, so the
// band lands where a QPainter pen at 6.35 does. The framebuffer samples pixel centres at
// half-integers while a key's centre is an integer coordinate, so the two rows a nominal
// radius can land in sample 5.5px and 6.5px out: with the band at [5.85, 6.85] the OUTER
// row carries the body and the inner one its AA fringe. A single-row probe could read the
// fringe and call a real ring missing, or too faint to name, so the reading takes the
// stronger of the two - which is the ring's own ink if the ring is there at all - and the
// claims built on it are about WHICH TOKEN that ink is, never about one nominal pixel.
//
// The pixel is reported in aPixel so a failure can name it.
QColor detachedRingInk(const QImage& aImage, const QPoint& aCentre, QPoint& aPixel) {
    const int first =
        int(std::lround(double(ctrl::graph::kKeyDotRadius) + ctrl::time::kSelectRingOffset)); // 6
    const QPoint rows[2] = {
        QPoint(aCentre.x(), aCentre.y() - first), QPoint(aCentre.x(), aCentre.y() - first - 1)
    };
    aPixel = rows[0];
    QColor best = pixelAt(aImage, rows[0].x(), rows[0].y());
    int bestDistance = clearDistance(best);
    const QColor beyond = pixelAt(aImage, rows[1].x(), rows[1].y());
    if (clearDistance(beyond) > bestDistance) {
        best = beyond;
        aPixel = rows[1];
    }
    return best;
}

// 3: the key markers, in BOTH states the emphasis decision has, plus the two the marker
// rule adds.
//
// THE EMPHASIS PAIR. The marker's fill and edge ring take the same strength as their curve
// (gui::curveStrength): a channel that owns a SELECTED marker - or the same key on a
// sibling channel - is drawn at full strength, every other channel is dimmed to
// kUnselectedAlpha, and with an EMPTY selection every channel is dimmed. The no-selection
// pass (`aRendered`) therefore shows the dimmed state of every key, and the three
// selection passes each put one channel in play, so every key can also be read at full
// strength. All of them render the SAME model (a hand-built copy with the marker flags
// set: this file has no editor) with the same invalid focus, so the flags are the only
// difference between them.
//
// THE GEOMETRIC PASS, and why there are THREE selection passes. These are:
//   * `aFirstSelected`  - the first curve's first key selected;
//   * `aSecondSelected` - the second curve's first key selected;
//   * `aSelectedLast`   - BOTH curves' LAST key selected.
// The size, the centring and the corner claims are read in the pass where the key under
// test is IN PLAY and UNRINGED: the first key of a curve is unringed in `aSelectedLast`
// (its curve is in play because its last key is selected) and every later key is unringed
// in its own curve's pass. Without that third pass the first key of each curve would only
// ever be measured with its selection ring on - and the ring's ink reaches 7.65px from the
// centre, past the hit square's own corners.
bool checkKeys(
    const Rendered& aRendered, const Rendered& aFirstSelected, const Rendered& aSecondSelected,
    const Rendered& aSelectedLast, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme
) {
    REQUIRE(aModel.curves.size() == 2, "precondition: the key assertion needs the two curves of the full model");
    // The channel hue table: the fixture's two channels are an X component and a Y
    // component, so their hues are the axis red and green - the SAME table the
    // timeline reads. Per-channel, not per-type: two first components would share
    // the X hue by design, which is why the fixture uses one of each.
    const QColor colourA = aTheme.channelColor(
        aModel.curves.at(0).channel.type(), aModel.curves.at(0).channel.component()
    );
    const QColor colourB = aTheme.channelColor(
        aModel.curves.at(1).channel.type(), aModel.curves.at(1).channel.component()
    );
    // THE KEYFRAME HIGHLIGHT, as this layer reads it: theme::Graph::axisText(), the
    // palette's own brightest panel ink (see the marker rule in GraphPanelFrame.cpp for why
    // the mock's literal white is not used).
    const QColor highlight = aTheme.axisText();
    REQUIRE(
        colourDistance(colourA, colourB) > kFillTol,
        "precondition: the two channels' hues are indistinguishable (rgb=%s and rgb=%s, distance %d)",
        hex(colourA).c_str(), hex(colourB).c_str(), colourDistance(colourA, colourB)
    );
    REQUIRE(
        colourDistance(colourA, aTheme.keyEdge()) > kFillTol
            && colourDistance(colourB, aTheme.keyEdge()) > kFillTol,
        "precondition: a key colour is not distinguishable from theme::Graph::keyEdge() rgb=%s",
        hex(aTheme.keyEdge()).c_str()
    );
    // The markers' state ink is read RELATIVELY (see the state assertions below): a 1px
    // ANTIALIASED ring is a partial-coverage blend and never the token itself, and the
    // claims are which token a pixel is made of and which way a fill moved. That reading
    // needs the tokens it compares to be far enough apart for a fringe-sized blend to name
    // one of them - the keyframe highlight, the floor ring and the two channel colours.
    REQUIRE(
        colourDistance(highlight, aTheme.keyEdge()) > 2 * kFillTol
            && colourDistance(highlight, colourA) > 2 * kFillTol
            && colourDistance(highlight, colourB) > 2 * kFillTol,
        "precondition: the keyframe highlight rgb=%s is not distinguishable from keyEdge rgb=%s or the key"
        " colours rgb=%s/rgb=%s: a blended marker pixel could not be read against them",
        hex(highlight).c_str(), hex(aTheme.keyEdge()).c_str(), hex(colourA).c_str(), hex(colourB).c_str()
    );

    // The marker's own bounds, from the constant the drawing layer uses: the drawn
    // radius, the 1px keyEdge stroke CENTRED on it (circleStroke(dotRadius + 0.5) has its
    // path pulled in by half the width, so the band is [dotRadius - 0.5, dotRadius + 0.5]
    // = [3.5, 4.5] and its AA reaches 4.5), and the antialiasing of both.
    const int dotRadius = int(std::lround(double(ctrl::graph::kKeyDotRadius)));   // 4
    // THE EDGE BAND'S OWN SAMPLE, MEASURED. A key's centre is an integer coordinate
    // while the framebuffer samples pixel centres at half-integers, so the pixel
    // `step` away from it samples at step + 0.5 from the centre. With the band at
    // [3.5, 4.5], step 3 (sample 3.5) sits ON its inner edge and step 4 (4.5) on its
    // outer one: the +3 pixel reads the band's ink on the shipped marker (MEASURED
    // #993270 against the keyEdge #171717), while +4 reads the clear frame on a steep
    // run and, on the shallow X curve, the CURVE's own stroke. Probing dotRadius (4)
    // therefore measures the fringe, or the curve, and never the marker's edge.
    const int edgeStep = dotRadius - 1;                                          // 3: sample radius 3.5
    const int minWidth = 2 * dotRadius - 1;                                      // 7: the drawn diameter, less AA
    const int maxWidth = 2 * dotRadius + 3;                                      // 11: ... plus the stroke and AA

    int keys = 0;
    int tintedByOwn = 0;
    for (int c = 0; c < aModel.curves.size(); ++c) {
        const gui::PanelCurve& curve = aModel.curves.at(c);
        const QColor own = aTheme.channelColor(curve.channel.type(), curve.channel.component());
        const QColor other = (c == 0) ? colourB : colourA;
        // The pass in which THIS curve's channel is in play - its first key is the one
        // that pass names as selected (see main) - and the x that names that key.
        const Rendered& inPlay = (c == 0) ? aFirstSelected : aSecondSelected;
        const int selectedX = int(curve.keyDots.isEmpty() ? -1 : curve.keyDots.at(0).x());
        REQUIRE(
            curve.keyDots.size() == curve.keys.size() && curve.keyFrames.size() == curve.keys.size(),
            "precondition: a curve's keyDots/keyFrames are not parallel to its keys (%d/%d/%d)",
            int(curve.keys.size()), int(curve.keyDots.size()), int(curve.keyFrames.size())
        );
        REQUIRE(
            curve.keySelected.size() == curve.keyDots.size()
                && curve.keySiblingSelected.size() == curve.keyDots.size(),
            "precondition: a curve's marker state is not parallel to its keyDots (%d/%d/%d)",
            int(curve.keyDots.size()), int(curve.keySelected.size()), int(curve.keySiblingSelected.size())
        );
        for (int i = 0; i < curve.keyDots.size(); ++i) {
            const QPointF& dot = curve.keyDots.at(i);
            const QRect& hit = curve.keys.at(i);
            REQUIRE(kPlot.contains(hit), "precondition: a key's hit square lies outside the plot");
            // The dot and the rect are two views of one key: ctrl::graph::keyRect is
            // centred on ctrl::graph::keyDot's own point (QRect's half-open right
            // column makes the rect's own centre() half a pixel short of it, which is
            // the model's geometry and not something to correct here).
            REQUIRE(
                std::abs(hit.center().x() - int(dot.x())) <= 1 && std::abs(hit.center().y() - int(dot.y())) <= 1,
                "precondition: the key at (%d,%d) publishes its hit square at (%d,%d): the two are not one key",
                int(dot.x()), int(dot.y()), hit.center().x(), hit.center().y()
            );

            const QPoint centre(int(dot.x()), int(dot.y()));
            // The dimmed state, from the no-selection pass...
            const QColor inner = pixelAt(aRendered.image, centre.x(), centre.y());
            const QPoint edgePixel(centre.x() + edgeStep, centre.y());
            const QColor edge = pixelAt(aRendered.image, edgePixel.x(), edgePixel.y());
            // ...and the full-strength state, from the pass that puts this key's channel
            // in play.
            const QColor inPlayInner = pixelAt(inPlay.image, centre.x(), centre.y());
            const QColor inPlayEdge = pixelAt(inPlay.image, edgePixel.x(), edgePixel.y());
            const bool selectedInPlay = int(dot.x()) == selectedX;
            // THE DETACHED RING's own reading (the outermost state ink), out of the two rows
            // a 1px stroke at that radius can land in - see detachedRingInk for why one
            // nominal pixel is not enough. The probe is VERTICAL: the marker sits on its
            // curve, so the pixel 6-7 columns to its right lands on the stroke itself, while
            // 6-7 ROWS away is clear of a stroke that can only reach 2px from its centreline.
            QPoint outerRingPixel;
            const QColor inPlayOuterRing = detachedRingInk(inPlay.image, centre, outerRingPixel);
            // THE GEOMETRIC PASS for this key: its curve in play and this marker UNRINGED
            // (see checkKeys' comment) - the pass its size, centring and corners are read in.
            const Rendered& geometric = (i == 0) ? aSelectedLast : inPlay;

            int negX = 0;
            int posX = 0;
            int negY = 0;
            int posY = 0;
            const int widthX = inkWidthThrough(geometric.image, centre, 1, 0, 12, negX, posX);
            const int widthY = inkWidthThrough(geometric.image, centre, 0, 1, 12, negY, posY);
            // THE HIT SQUARE'S CORNERS. A key sits ON its curve, so a corner within the
            // stroke's reach of the polyline carries the CURVE's ink and says nothing
            // about the marker: those corners are excluded, and the ones the model's own
            // geometry leaves clear are the probe. At least two always remain (a corner
            // is ~7px from the centre and the stroke reaches 2px).
            const auto reachesCurve = [&curve](const QPoint& aPoint, double aReach) {
                for (int p = 0; p + 1 < curve.points.size(); ++p) {
                    const QPointF a = curve.points.at(p);
                    const QPointF b = curve.points.at(p + 1);
                    const double length2 = (b.x() - a.x()) * (b.x() - a.x()) + (b.y() - a.y()) * (b.y() - a.y());
                    double t = 0.0;
                    if (length2 > 0.0) {
                        t = ((aPoint.x() - a.x()) * (b.x() - a.x()) + (aPoint.y() - a.y()) * (b.y() - a.y()))
                            / length2;
                    }
                    t = std::clamp(t, 0.0, 1.0);
                    if (std::hypot(a.x() + t * (b.x() - a.x()) - aPoint.x(), a.y() + t * (b.y() - a.y()) - aPoint.y())
                        <= aReach) {
                        return true;
                    }
                }
                return false;
            };
            const QPoint corners[4] = {
                QPoint(hit.left(), hit.top()), QPoint(hit.right(), hit.top()), QPoint(hit.left(), hit.bottom()),
                QPoint(hit.right(), hit.bottom()),
            };
            int cornerInk = 0;
            int cornerProbed = 0;
            for (const QPoint& corner : corners) {
                if (reachesCurve(corner, 2.5)) {
                    continue; // the curve inks there; the marker cannot be read
                }
                ++cornerProbed;
                if (clearDistance(pixelAt(geometric.image, corner.x(), corner.y())) > kInkMin) {
                    ++cornerInk;
                }
            }
            REQUIRE(
                cornerProbed >= 2,
                "precondition: only %d of the key's hit-square corners are clear of its own curve, so the drawn"
                " marker cannot be told from the stroke there",
                cornerProbed
            );

            std::printf(
                "MEASURED key channel_type=%d dot=(%d,%d) hit_rect=(%d,%d %dx%d) centre_rgb=%s channel_hue=%s d=%d"
                " other_channel_hue=%s d=%d edge=(%d,%d) edge_rgb=%s keyEdge=%s d_edge_to_keyEdge=%d"
                " d_edge_to_fill=%d width_x=%d (%d/%d) width_y=%d (%d/%d) dot_radius=%d edge_step=%d"
                " hit_corner_ink=%d selected=%d in_play_centre_rgb=%s d_in_play_to_fill=%d"
                " d_in_play_to_highlight=%d in_play_edge_rgb=%s d_in_play_edge_to_keyEdge=%d"
                " outer_ring=(%d,%d) outer_ring_rgb=%s d_outer_to_highlight=%d d_outer_to_fill=%d\n",
                int(curve.channel.type()), centre.x(), centre.y(), hit.left(), hit.top(), hit.width(),
                hit.height(), hex(inner).c_str(), hex(own).c_str(), colourDistance(inner, own), hex(other).c_str(),
                colourDistance(inner, other), edgePixel.x(), edgePixel.y(), hex(edge).c_str(),
                hex(aTheme.keyEdge()).c_str(), colourDistance(edge, aTheme.keyEdge()),
                colourDistance(edge, own), widthX, negX, posX, widthY, negY, posY, dotRadius, edgeStep, cornerInk,
                int(selectedInPlay), hex(inPlayInner).c_str(), colourDistance(inPlayInner, own),
                colourDistance(inPlayInner, highlight), hex(inPlayEdge).c_str(),
                colourDistance(inPlayEdge, aTheme.keyEdge()), outerRingPixel.x(), outerRingPixel.y(),
                hex(inPlayOuterRing).c_str(), colourDistance(inPlayOuterRing, highlight),
                colourDistance(inPlayOuterRing, own)
            );

            REQUIRE(
                clearDistance(inner) > kInkMin, "the key marker at (%d,%d) has no ink at its centre: rgb=%s",
                centre.x(), centre.y(), hex(inner).c_str()
            );
            // THE FILL, in the two states the emphasis decision has.
            //
            // DIMMED (the no-selection pass): nothing is selected, so every channel is
            // drawn at kUnselectedAlpha and the centre is the channel's colour COMPOSITED
            // at that strength - not the token. The composite itself is deliberately not
            // pinned to a number: the marker sits ON its own curve, so the pixel it paints
            // is the channel colour blended twice (the stroke underneath and the marker's
            // fill over it) over the pass's clear colour, which is a statement about the
            // stroke's geometry as much as about the marker's strength. What is pinned is
            // what this assertion was written for - the key is TINTED by its own channel
            // (nearer its colour than the other channel's, below) - and that the fill is
            // NOT the undimmed token, which is the regression an empty selection must not
            // have.
            REQUIRE(
                colourDistance(inner, own) > kFillTol,
                "the key centre rgb=%s IS its channel's colour rgb=%s (channel type %d, distance %d <= %d) with"
                " NOTHING selected: an empty selection must draw every channel dimmed",
                hex(inner).c_str(), hex(own).c_str(), int(curve.channel.type()), colourDistance(inner, own), kFillTol
            );
            // FULL STRENGTH (the pass that puts this key's channel in play): the marker is
            // opaque, so the pixel IS the ink the marker rule gives it - the channel's own
            // colour for a marker with no state, or the channel colour mixed toward the
            // keyframe highlight for the marker this pass selects.
            if (selectedInPlay) {
                // "THE FILL BRIGHTENS" (index.html:279-280), read as the rule it is: the
                // selected marker's ink is the channel hue mixed 0.45 toward the theme's TEXT
                // colour - the same token and amount the drawing layer mixes
                // (time_Renderer.cpp's mixTowardText, GraphPanelFrame's kKeySelectedFillMix).
                // An earlier form asked whether the pixel was nearer the highlight than the
                // hue, which silently encoded a minimum MIX AMOUNT and then rejected the
                // design's own 0.45; and a per-channel "every component rose" test is wrong
                // too, because the text ink (245) sits below this hue's red (251), so one
                // component legitimately falls. The exact ink is the assertion.
                const QColor textInk = theme::Colors::current().text;
                const auto mixTowardText = [](int aChannel, int aTarget) {
                    return static_cast<int>(std::lround(aChannel + (aTarget - aChannel) * 0.45));
                };
                const QColor expected(
                    mixTowardText(own.red(), textInk.red()),
                    mixTowardText(own.green(), textInk.green()),
                    mixTowardText(own.blue(), textInk.blue())
                );
                REQUIRE(
                    colourDistance(inPlayInner, expected) <= kFillTol,
                    "the selected key's centre rgb=%s is not its channel's colour mixed 0.45 toward the text ink"
                    " rgb=%s (expected %s, distance %d > %d): a selected marker's fill does not brighten by the"
                    " rule the drawing layer applies",
                    hex(inPlayInner).c_str(), hex(textInk).c_str(), hex(expected).c_str(),
                    colourDistance(inPlayInner, expected), kFillTol
                );
            } else {
                REQUIRE(
                    colourDistance(inPlayInner, own) <= kFillTol,
                    "the key centre rgb=%s is not its channel's colour rgb=%s (channel type %d, distance %d > %d)"
                    " in the pass that selects another key of ITS channel: a channel in play must be at full"
                    " strength, not dimmed",
                    hex(inPlayInner).c_str(), hex(own).c_str(), int(curve.channel.type()),
                    colourDistance(inPlayInner, own), kFillTol
                );
            }
            // The EDGE RING (the palette's own, drawn for every marker whether or not it is
            // in a state): the pixel at the band the drawing actually paints carries ink,
            // and it is the marker's own dark band rather than the fill - checked below in
            // both states. The stroke is CENTRED on ctrl::graph::kKeyDotRadius
            // (circleStroke(dotRadius + 0.5) puts the 1px band at
            // [dotRadius - 0.5, dotRadius + 0.5] = [3.5, 4.5]), and the sample at
            // edgeStep (3.5px from the centre) sits on its inner edge. The dimmed claim is also
            // relational - the edge pixel is nearer theme::Graph::keyEdge() than the
            // centre is, which a pure fill fails - and it holds in BOTH states: the
            // selected marker's fill is the highlight mix rather than the accent, and the
            // edge still outweighs it at that pixel (the edge is drawn over the fill, and
            // the detached selection ring starts past 5px - beyond this probe).
            REQUIRE(
                clearDistance(edge) > kInkMin,
                "the key marker at (%d,%d) has no ink at its edge pixel (%d,%d): the marker's own %.2f..%.2fpx"
                " edge band is empty",
                centre.x(), centre.y(), edgePixel.x(), edgePixel.y(),
                double(ctrl::graph::kKeyDotRadius) - 0.5, double(ctrl::graph::kKeyDotRadius) + 0.5
            );
            {
                // THE MARKER'S OWN SEPARATING EDGE. A 1px band antialiased into a single
                // probe pixel is a FILL/EDGE BLEND by construction, so asking whether the
                // sample is "nearer the edge colour than the fill" cannot pass: it measured
                // a true edge ring as a failure once the band was centred on the reference
                // radius. What the edge must do is DARKEN the fill it is drawn over: every
                // channel at or below the fill pixel's, at least one strictly below. The
                // comparison is against inPlayInner - the marker's OWN fill in this pass
                // (the channel's hue, or the selected marker's highlight mix), which is the
                // ink the edge is painted over. A marker with no edge reads as that fill and
                // fails this; a fill that changed but an edge that did not darken it fails
                // too, which is why the mixed selected fill cannot hide a missing edge.
                REQUIRE(
                    inPlayEdge.red() <= inPlayInner.red() && inPlayEdge.green() <= inPlayInner.green()
                        && inPlayEdge.blue() <= inPlayInner.blue()
                        && (inPlayEdge.red() < inPlayInner.red() || inPlayEdge.green() < inPlayInner.green()
                            || inPlayEdge.blue() < inPlayInner.blue()),
                    "the key's edge pixel rgb=%s (selected=%d) is not darker than its own fill rgb=%s:"
                    " the marker has no edge ring",
                    hex(inPlayEdge).c_str(), int(selectedInPlay), hex(inPlayInner).c_str()
                );
            }
            // THE DETACHED RING - the marker rule's own ink, and the pair this assertion is
            // about: the pass that SELECTS a key paints the TIMELINE'S 1px ring (the raw
            // channel hue at kSelectRingAlpha, 0.85) at dotRadius + kSelectRingOffset, and
            // the pass that puts the same channel in play without selecting THAT key paints
            // NOTHING there (the marker's own ink's AA ends at ~4.5px, and these rows
            // sample 5.5px out and beyond). So the ring is
            // the selection's mark and not a bigger marker.
            if (selectedInPlay) {
                REQUIRE(
                    clearDistance(inPlayOuterRing) > kInkMin,
                    "the selected key at (%d,%d) has no ink at the ring probe (%d,%d): a selected marker must"
                    " carry its 1px ring %.1fpx clear of the marker",
                    centre.x(), centre.y(), outerRingPixel.x(), outerRingPixel.y(),
                    double(ctrl::time::kSelectRingOffset)
                );
                // THE RING IS THE TIMELINE'S INK: the channel's RAW hue at
                // ctrl::time::kSelectRingAlpha (217/255), not the fill's mixed
                // colour. The probe is a 1px stroke's partial coverage over the
                // clear frame, so it lies on the line from the clear colour to that
                // hue; the projection recovers the COMBINED coverage (the token's
                // 0.85 alpha times the AA coverage), and `over` rebuilds the exact
                // sample. The bounds on that number are what pin the token: it can
                // never exceed the token's own 0.85, and the chosen probe row carries
                // its body (so it is well above a fringe).
                const QColor ringHue = own;
                const double inkAlpha = double(ctrl::time::kSelectRingAlpha) / 255.0;
                REQUIRE(
                    colourDistance(ringHue, highlight) > kInkMin,
                    "precondition: the channel hue rgb=%s is indistinguishable from the highlight rgb=%s, so"
                    " the ring's ink cannot be named",
                    hex(ringHue).c_str(), hex(highlight).c_str()
                );
                const double ringCoverage = coverageOfTone(inPlayOuterRing, ringHue);
                const QColor ringPredicted = toneAtCoverage(ringHue, ringCoverage);
                REQUIRE(
                    ringCoverage > 0.5 * inkAlpha && ringCoverage <= inkAlpha + 0.02
                        && colourDistance(inPlayOuterRing, ringPredicted) <= kCurveTol,
                    "the selected key's ring pixel rgb=%s is not the channel hue rgb=%s at %.3f..%.3f"
                    " coverage (measured %.3f, expected %s, distance %d > %d): the selection ring must be the"
                    " timeline's hue at kSelectRingAlpha (%d/255), not a mixed fill ink",
                    hex(inPlayOuterRing).c_str(), hex(ringHue).c_str(), 0.5 * inkAlpha, inkAlpha,
                    ringCoverage, hex(ringPredicted).c_str(),
                    colourDistance(inPlayOuterRing, ringPredicted), kCurveTol,
                    ctrl::time::kSelectRingAlpha
                );
                // ...and it is OUTSIDE the dot's own ink: the probe rows sample 5.5px and
                // 6.5px out, past the marker's own reach (the edge band ends at
                // dotRadius + 0.5 = 4.25, its antialiasing at 4.5).
                REQUIRE(
                    ctrl::time::kSelectRingOffset >= 2.0,
                    "the selection ring's centre (dotRadius + %.2f) is not at least 2px outside the dot's radius",
                    double(ctrl::time::kSelectRingOffset)
                );
            } else {
                REQUIRE(
                    clearDistance(inPlayOuterRing) <= kInkMin,
                    "the key at (%d,%d) paints rgb=%s at the ring probe (%d,%d) although it is NOT selected:"
                    " only a selected marker carries the 1px ring",
                    centre.x(), centre.y(), hex(inPlayOuterRing).c_str(), outerRingPixel.x(), outerRingPixel.y()
                );
            }
            // The edge ring of a DIMMED marker, where the edge carries the marker's
            // strength too: the claim that survives the dimming is the DIRECTION of the
            // shift - the edge pixel is nearer the edge colour than the fully covered
            // body pixel is (both are the same blend at the same strength, so the
            // inequality scales and cannot depend on the strength). A marker with no edge
            // fails it: the two pixels would be the same colour.
            REQUIRE(
                colourDistance(edge, aTheme.keyEdge()) < colourDistance(inner, aTheme.keyEdge()),
                "the dimmed key's edge pixel rgb=%s is not nearer theme::Graph::keyEdge() rgb=%s than its own"
                " centre rgb=%s is (%d against %d): the marker has no edge ring",
                hex(edge).c_str(), hex(aTheme.keyEdge()).c_str(), hex(inner).c_str(),
                colourDistance(edge, aTheme.keyEdge()), colourDistance(inner, aTheme.keyEdge())
            );
            // THE MARKER'S SIZE: the dot's own diameter, and not the hit square's
            // 2*kKeyRadius. THE WIDTH IS THE SMALLER OF THE TWO, because the curve the
            // key sits on runs through its centre and can only ADD ink along its own
            // direction: the narrow one is the marker's own extent, whichever it is.
            const int width = std::min(widthX, widthY);
            REQUIRE(
                width >= minWidth && width <= maxWidth,
                "the key marker at (%d,%d) inks %dpx across and %dpx down - neither the %d..%d of a %.2fpx-radius"
                " dot (the hit square is %dpx)",
                centre.x(), centre.y(), widthX, widthY, minWidth, maxWidth, double(ctrl::graph::kKeyDotRadius),
                2 * ctrl::graph::kKeyRadius
            );
            const int neg = (widthX <= widthY) ? negX : negY;
            const int pos = (widthX <= widthY) ? posX : posY;
            REQUIRE(
                neg == pos || std::abs(neg - pos) == 1,
                "the key marker at (%d,%d) is not centred on its own key: %dpx one way, %dpx the other",
                centre.x(), centre.y(), neg, pos
            );
            // ...and the hit square is NOT what is drawn: its corners are past the
            // marker's ink and inside a square's.
            REQUIRE(
                cornerInk == 0,
                "the key at (%d,%d) paints %d of the %d hit-square corners its own curve leaves clear (%d,%d %dx%d):"
                " the drawn marker is the pick rectangle, not the %.2fpx-radius dot",
                centre.x(), centre.y(), cornerInk, cornerProbed, hit.left(), hit.top(), hit.width(), hit.height(),
                double(ctrl::graph::kKeyDotRadius)
            );

            // ...and it is the channel THAT KEY belongs to, not merely a palette
            // colour that happens to match: its centre is nearer its own than the
            // other curve's.
            if (colourDistance(inner, own) < colourDistance(inner, other)) {
                ++tintedByOwn;
            } else {
                std::printf(
                    "FAIL: key at (%d,%d) rgb=%s is not nearer its own channel colour rgb=%s than the other"
                    " curve's rgb=%s (%d against %d)\n",
                    centre.x(), centre.y(), hex(inner).c_str(), hex(own).c_str(), hex(other).c_str(),
                    colourDistance(inner, own), colourDistance(inner, other)
                );
                return false;
            }
            ++keys;
        }
    }
    REQUIRE(keys == 4, "expected 4 key markers in the full model, walked %d", keys);
    REQUIRE(tintedByOwn == keys, "only %d of %d key markers are tinted by their own channel", tintedByOwn, keys);
    std::printf(
        "MEASURED key_tint key_colours=%s/%s keys=%d tinted_by_own_channel=%d\n", hex(colourA).c_str(),
        hex(colourB).c_str(), keys, tintedByOwn
    );
    return true;
}

// 29: THE THREE MARKER STATES, side by side on ONE KEY's two components.
//
// The model classifies each marker (PanelCurve::keySelected / keySiblingSelected) and
// gui::drawPanel inks the states. What this asserts is that the three inks are the three
// the design names, and that they are told apart from each other:
//
//   * SELECTED - the fill brightens toward the keyframe highlight AND a 1px ring sits
//     clear of the marker in THE TIMELINE'S INK. Read as: the centre moved TOWARD the
//     highlight against the same marker's unflagged fill, and the ring's probe is the
//     channel's RAW HUE at ctrl::time::kSelectRingAlpha (0.85) at its own antialiased
//     coverage - the same expression time_Renderer's key pass draws, not the fill's mix.
//   * SIBLING (not selected, its key selected on the other component) - the FAINT ring,
//     and a fill that is NOT brightened. Read as: the ring's probe carries ink at the same
//     radius, that ink is the same raw hue at 0.35 (so it is fainter - nearer the clear
//     colour - than the selection ring, and never the mixed fill), and the centre is
//     still the channel's own colour exactly - the "no brighter fill" half of the rule.
//   * HOVERED - the fill brightens, no ring. Read as: the centre moved toward the highlight
//     against the unflagged pass, and the ring's probe carries NOTHING.
//
// The colour of the ring is also why this needs a fixture whose two curves are ONE key TYPE's
// two components: a sibling is another component of the same key. They do NOT share one hue -
// the channel hue table gives component 0 the X axis hue and component 1 the Y axis hue - so
// each "nearer its own hue than the highlight" claim is read against THAT component's hue,
// and "the two inks are distinguishable" is a precondition of the fixture.
bool checkMarkerStates(
    const Rendered& aSelected, const Rendered& aHovered, const Rendered& aPlain,
    const gui::GraphPanelModel& aModel, const theme::Graph& aTheme
) {
    REQUIRE(aModel.curves.size() == 2, "precondition: the marker assertion needs the two components of one key");
    REQUIRE(aModel.curves.at(0).keySelected.size() == 1 && aModel.curves.at(1).keySelected.size() == 1,
            "precondition: the marker fixture must carry one key per component");
    REQUIRE(aModel.curves.at(0).keySelected.at(0) && !aModel.curves.at(1).keySelected.at(0)
                && aModel.curves.at(1).keySiblingSelected.at(0),
            "precondition: the model this reads is not the selected/sibling state");

    // Each component carries its OWN channel hue (X red for component 0, Y green for
    // component 1) - the one table the timeline and the panel share.
    const QColor hue[2] = {
        aTheme.channelColor(aModel.curves.at(0).channel.type(), aModel.curves.at(0).channel.component()),
        aTheme.channelColor(aModel.curves.at(1).channel.type(), aModel.curves.at(1).channel.component()),
    };
    const QColor highlight = aTheme.axisText();
    REQUIRE(hue[0] == aModel.curves.at(0).color && hue[1] == aModel.curves.at(1).color,
            "precondition: the fixture's components do not carry their own channel hue");
    REQUIRE(
        colourDistance(hue[0], hue[1]) > kFillTol,
        "precondition: the fixture's two components do not carry distinguishable channel hues (rgb=%s / rgb=%s)",
        hex(hue[0]).c_str(), hex(hue[1]).c_str()
    );
    REQUIRE(
        colourDistance(highlight, hue[0]) > 2 * kFillTol && colourDistance(highlight, hue[1]) > 2 * kFillTol,
        "precondition: the keyframe highlight rgb=%s is not distinguishable from the channel hues rgb=%s / rgb=%s",
        hex(highlight).c_str(), hex(hue[0]).c_str(), hex(hue[1]).c_str()
    );

    // The detached ring's own reading, out of the two rows a 1px stroke at that radius can
    // land in (see detachedRingInk), for the three passes this assertion compares.
    QColor ringPixel[2];
    QColor plainRing[2];
    QColor hoverRing[2];
    QColor centrePixel[2];
    QColor plainCentre[2];
    QColor hoverCentre[2];
    QPoint ringProbe[2];
    for (int c = 0; c < 2; ++c) {
        const QPointF& dot = aModel.curves.at(c).keyDots.at(0);
        const QPoint centre(int(dot.x()), int(dot.y()));
        centrePixel[c] = pixelAt(aSelected.image, centre.x(), centre.y());
        plainCentre[c] = pixelAt(aPlain.image, centre.x(), centre.y());
        hoverCentre[c] = pixelAt(aHovered.image, centre.x(), centre.y());
        ringPixel[c] = detachedRingInk(aSelected.image, centre, ringProbe[c]);
        QPoint ignored;
        plainRing[c] = detachedRingInk(aPlain.image, centre, ignored);
        hoverRing[c] = detachedRingInk(aHovered.image, centre, ignored);
        std::printf(
            "MEASURED marker_state curve=%d dot=(%d,%d) ring_probe=(%d,%d) centre_rgb=%s plain_centre_rgb=%s"
            " hover_centre_rgb=%s ring_rgb=%s plain_ring_rgb=%s hover_ring_rgb=%s d_centre_to_highlight=%d"
            " d_plain_to_highlight=%d d_hover_to_highlight=%d d_ring_to_highlight=%d d_ring_to_hue=%d"
            " d_ring_to_clear=%d\n",
            c, centre.x(), centre.y(), ringProbe[c].x(), ringProbe[c].y(), hex(centrePixel[c]).c_str(),
            hex(plainCentre[c]).c_str(), hex(hoverCentre[c]).c_str(), hex(ringPixel[c]).c_str(),
            hex(plainRing[c]).c_str(), hex(hoverRing[c]).c_str(), colourDistance(centrePixel[c], highlight),
            colourDistance(plainCentre[c], highlight), colourDistance(hoverCentre[c], highlight),
            colourDistance(ringPixel[c], highlight), colourDistance(ringPixel[c], hue[c]), clearDistance(ringPixel[c])
        );
        // The two surrounding passes are the pair the state claims rest on: NEITHER of them
        // may put ink at the ring radius, and the unflagged pass is what the fills are
        // compared against.
        REQUIRE(
            clearDistance(plainRing[c]) <= kInkMin && clearDistance(hoverRing[c]) <= kInkMin,
            "precondition: the marker fixture inks the ring probe (%d,%d) with no state set: the probe is not"
            " reading the state rings alone",
            ringProbe[c].x(), ringProbe[c].y()
        );
    }

    // ---- THE SELECTED MARKER (curve 0): brightened fill, and a ring in the highlight ink.
    {
        const int toHighlight = colourDistance(centrePixel[0], highlight);
        const int plainToHighlight = colourDistance(plainCentre[0], highlight);
        REQUIRE(
            toHighlight < plainToHighlight,
            "the SELECTED marker's centre rgb=%s is not nearer the keyframe highlight rgb=%s than the same"
            " marker unflagged (%d against %d): a selected marker's fill does not brighten",
            hex(centrePixel[0]).c_str(), hex(highlight).c_str(), toHighlight, plainToHighlight
        );
        REQUIRE(
            clearDistance(ringPixel[0]) > kInkMin,
            "the SELECTED marker paints nothing at its ring probe: rgb=%s", hex(ringPixel[0]).c_str()
        );
        // THE RING IS THE TIMELINE'S INK: the channel's RAW hue at
        // ctrl::time::kSelectRingAlpha (217/255) - the same expression
        // time_Renderer's key pass uses - and NOT the fill's mixed colour. The probe
        // is a partial-coverage sample over the clear frame, so it lies on the line
        // from the clear colour to that hue; the recovered coverage is the token's
        // alpha times the AA coverage, so it is never above 0.85 and (on the body
        // row) never a fringe.
        const QColor ringHue = hue[0];
        const double inkAlpha = double(ctrl::time::kSelectRingAlpha) / 255.0;
        const double ringCoverage = coverageOfTone(ringPixel[0], ringHue);
        const QColor ringPredicted = toneAtCoverage(ringHue, ringCoverage);
        REQUIRE(
            ringCoverage > 0.5 * inkAlpha && ringCoverage <= inkAlpha + 0.02
                && colourDistance(ringPixel[0], ringPredicted) <= kCurveTol,
            "the SELECTED marker's ring pixel rgb=%s is not the channel hue rgb=%s at %.3f..%.3f coverage"
            " (measured %.3f, expected %s, distance %d > %d): the selection ring is the timeline's raw hue"
            " at kSelectRingAlpha (%d/255), not the fill's mixed ink",
            hex(ringPixel[0]).c_str(), hex(ringHue).c_str(), 0.5 * inkAlpha, inkAlpha, ringCoverage,
            hex(ringPredicted).c_str(), colourDistance(ringPixel[0], ringPredicted), kCurveTol,
            ctrl::time::kSelectRingAlpha
        );
        // ...and the fill did NOT take the ring's ink: the centre is still the mixed
        // highlight, which is a different colour from the hue the ring carries.
        REQUIRE(
            colourDistance(centrePixel[0], mixedToward(hue[0], highlight, 0.45f)) <= kFillTol,
            "the SELECTED marker's centre rgb=%s is not its fill mix rgb=%s: the fill rule must not follow the"
            " ring's raw-hue ink",
            hex(centrePixel[0]).c_str(), hex(mixedToward(hue[0], highlight, 0.45f)).c_str()
        );
    }

    // ---- THE SIBLING MARKER (curve 1): a faint ring that is NOT the selection's, and a fill
    // that is NOT brightened.
    {
        REQUIRE(
            clearDistance(ringPixel[1]) > kInkMin,
            "the SIBLING marker paints nothing at its ring probe: rgb=%s - a key in play must keep its faint"
            " ring",
            hex(ringPixel[1]).c_str()
        );
        REQUIRE(
            clearDistance(ringPixel[0]) > clearDistance(ringPixel[1]),
            "the SIBLING marker's ring (rgb=%s, %d from the clear colour) is not FAINTER than the SELECTED"
            " marker's (rgb=%s, %d): both are the channel's raw hue - the sibling at 0.35, the selection ring"
            " at kSelectRingAlpha",
            hex(ringPixel[1]).c_str(), clearDistance(ringPixel[1]), hex(ringPixel[0]).c_str(),
            clearDistance(ringPixel[0])
        );
        REQUIRE(
            colourDistance(ringPixel[1], hue[1]) < colourDistance(ringPixel[1], highlight),
            "the SIBLING marker's ring pixel rgb=%s is not nearer its channel's hue rgb=%s than the keyframe"
            " highlight rgb=%s (%d against %d)",
            hex(ringPixel[1]).c_str(), hex(hue[1]).c_str(), hex(highlight).c_str(),
            colourDistance(ringPixel[1], hue[1]), colourDistance(ringPixel[1], highlight)
        );
        REQUIRE(
            clearDistance(ringPixel[1]) < clearDistance(ringPixel[0]),
            "the SIBLING marker's ring (rgb=%s, %d from the clear colour) is not FAINTER than the SELECTED"
            " marker's (rgb=%s, %d): the faint ring is the same raw hue at 0.35 against the selection ring's"
            " 0.85",
            hex(ringPixel[1]).c_str(), clearDistance(ringPixel[1]), hex(ringPixel[0]).c_str(),
            clearDistance(ringPixel[0])
        );
        REQUIRE(
            colourDistance(centrePixel[1], hue[1]) <= kFillTol,
            "the SIBLING marker's centre rgb=%s is not its channel's hue rgb=%s (distance %d > %d): a marker"
            " whose key is selected elsewhere keeps the unselected fill",
            hex(centrePixel[1]).c_str(), hex(hue[1]).c_str(), colourDistance(centrePixel[1], hue[1]), kFillTol
        );
        REQUIRE(
            colourDistance(centrePixel[1], hue[1]) < colourDistance(centrePixel[0], hue[0]),
            "the SIBLING marker's fill moved as far from its channel's hue as the SELECTED one's did: only the"
            " selected marker's fill brightens"
        );
    }

    // ---- THE HOVERED MARKER (curve 0, unflagged model, the pointer on its dot): the fill
    // brightens and NOTHING is added at the ring radius.
    {
        const int toHighlight = colourDistance(hoverCentre[0], highlight);
        const int plainToHighlight = colourDistance(plainCentre[0], highlight);
        REQUIRE(
            toHighlight < plainToHighlight,
            "the HOVERED marker's centre rgb=%s is not nearer the keyframe highlight rgb=%s than it is with the"
            " pointer elsewhere (%d against %d): a hovered marker's fill does not brighten",
            hex(hoverCentre[0]).c_str(), hex(highlight).c_str(), toHighlight, plainToHighlight
        );
        REQUIRE(
            clearDistance(hoverRing[0]) <= kInkMin,
            "the HOVERED marker painted a ring: a hover brightens the fill and adds no ring (rgb=%s at the"
            " ring probe (%d,%d))",
            hex(hoverRing[0]).c_str(), ringProbe[0].x(), ringProbe[0].y()
        );
    }

    // ---- THE DOT'S OWN DIAMETER IS UNCHANGED BY SELECTION. The selected marker's fill
    // colour and its detached ring differ; the marker's own SIZE must not. The ink
    // width is measured through the centre with a 4px reach - through the marker's own
    // edge band (centred on kKeyDotRadius) and its antialiasing, which ends 4.5px out,
    // and short of the detached ring (whose 1px band starts at 5.85px, dotRadius +
    // ctrl::time::kSelectRingOffset - 0.5) - so the two passes compare the dot alone.
    {
        for (int c = 0; c < 2; ++c) {
            const QPointF& dot = aModel.curves.at(c).keyDots.at(0);
            const QPoint centre(int(dot.x()), int(dot.y()));
            int negSelected = 0;
            int posSelected = 0;
            int negPlain = 0;
            int posPlain = 0;
            const int widthSelected =
                inkWidthThrough(aSelected.image, centre, 1, 0, 4, negSelected, posSelected);
            const int widthPlain = inkWidthThrough(aPlain.image, centre, 1, 0, 4, negPlain, posPlain);
            std::printf(
                "MEASURED marker_diameter curve=%d dot=(%d,%d) width_selected=%dpx width_plain=%dpx\n", c,
                centre.x(), centre.y(), widthSelected, widthPlain
            );
            REQUIRE(
                std::abs(widthSelected - widthPlain) <= 1,
                "the selected marker at (%d,%d) inks %dpx across against the same marker unflagged at %dpx: the"
                " selection ring must not change the dot's own diameter (within a pixel of antialiasing)",
                centre.x(), centre.y(), widthSelected, widthPlain
            );
        }
    }

    return true;
}

// 32: THE PROPORTIONAL MARKER IS A RING, AND NOTHING ELSE MOVES.
//
// The fixture publishes ONE channel with two SELECTED markers - the first absolute, the
// second proportional - so both are in play at full strength and both carry the same
// selection ring. What is asserted, in the order the rule is stated:
//   * the absolute marker's centre is the selected fill outright (the channel hue mixed
//     0.45 toward the keyframe highlight, opaque);
//   * the proportional marker's centre is the PANEL FLOOR (aTheme.keyEdge()): the body
//     is hollow, so it cannot be mistaken for a softer copy of the absolute dot;
//   * the hue is a RING at ctrl::time::kProportionalHoleInset / kProportionalRingOuterInset
//     (the token the timeline's key pass reads), in the same selected fill ink;
//   * the EDGE stroke keeps its own token, strength and coverage, and the SELECTION
//     RING is byte-identical to the same marker unflagged (a second pass whose only
//     difference is keyProportional[1] = false) - so the radius, hue, edge and rings
//     are untouched and only the body became a ring.
// A build that still scales the fill's alpha leaves the proportional centre filled,
// which is what the last pair of assertions catches.
bool checkProportionalMarker(
    const Rendered& aProportional, const Rendered& aUnflagged, const gui::GraphPanelModel& aModel,
    const theme::Graph& aTheme
) {
    REQUIRE(aModel.curves.size() == 1, "precondition: the proportional fixture must carry one channel");
    const gui::PanelCurve& curve = aModel.curves.at(0);
    REQUIRE(curve.keyDots.size() == 2, "precondition: the fixture must carry two markers, walked %d",
            int(curve.keyDots.size()));
    REQUIRE(curve.keyProportional.size() == curve.keyDots.size(),
            "precondition: keyProportional is not parallel to the markers (%d/%d)",
            int(curve.keyProportional.size()), int(curve.keyDots.size()));
    REQUIRE(!curve.keyProportional.at(0) && curve.keyProportional.at(1),
            "precondition: the fixture's two markers are not the absolute/proportional pair");
    REQUIRE(curve.keySelected.size() == curve.keyDots.size() && curve.keySelected.at(0)
                && curve.keySelected.at(1),
            "precondition: both markers must be selected, so neither fill's alpha comes from the curve strength");
    REQUIRE(!aProportional.image.isNull() && !aUnflagged.image.isNull(),
            "precondition: both passes must have been rendered");

    const QColor hue = curve.color;
    const QColor highlight = aTheme.axisText();
    REQUIRE(colourDistance(hue, highlight) > kFillTol,
            "precondition: the channel hue rgb=%s is indistinguishable from the highlight rgb=%s",
            hex(hue).c_str(), hex(highlight).c_str());
    // The selected ring's ink, exactly as the drawing layer mixes it (kKeySelectedFillMix).
    const QColor selectedFill = mixedToward(hue, highlight, 0.45f);
    const QColor floorInk = aTheme.keyEdge();
    const int ringStep = 2; // inside the filled ring band [1.5, 3.25]; the edge is outside

    const QPoint absoluteCentre(int(curve.keyDots.at(0).x()), int(curve.keyDots.at(0).y()));
    const QPoint proportionalCentre(int(curve.keyDots.at(1).x()), int(curve.keyDots.at(1).y()));
    const QPoint ringPixel(proportionalCentre.x() + ringStep, proportionalCentre.y());
    const QColor absoluteInk = pixelAt(aProportional.image, absoluteCentre.x(), absoluteCentre.y());
    const QColor proportionalInk = pixelAt(aProportional.image, proportionalCentre.x(), proportionalCentre.y());
    const QColor ringInk = pixelAt(aProportional.image, ringPixel.x(), ringPixel.y());
    const QColor unflaggedInk = pixelAt(aUnflagged.image, proportionalCentre.x(), proportionalCentre.y());
    const QColor unflaggedAbsoluteInk = pixelAt(aUnflagged.image, absoluteCentre.x(), absoluteCentre.y());
    std::printf(
        "MEASURED proportional_marker abs=(%d,%d) rgb=%s d_clear=%d prop_centre=(%d,%d) rgb=%s"
        " floor=%s ring=(%d,%d) rgb=%s ring_step=%d\n",
        absoluteCentre.x(), absoluteCentre.y(), hex(absoluteInk).c_str(), clearDistance(absoluteInk),
        proportionalCentre.x(), proportionalCentre.y(), hex(proportionalInk).c_str(),
        hex(floorInk).c_str(), ringPixel.x(), ringPixel.y(), hex(ringInk).c_str(), ringStep
    );

    // The absolute marker: the fill's own ink, unsoftened.
    REQUIRE(colourDistance(absoluteInk, selectedFill) <= kFillTol,
            "the ABSOLUTE marker's centre rgb=%s is not the selected fill rgb=%s (distance %d > %d)",
            hex(absoluteInk).c_str(), hex(selectedFill).c_str(), colourDistance(absoluteInk, selectedFill),
            kFillTol);
    // The proportional marker: a HOLE at the centre, filled with the panel floor.
    // The hole is 1.25px radius (the 2px hue band the key body uses), so the centre
    // pixel is ~90% floor plus the last 10% of hue ink: floor-DOMINANT, not byte-equal
    // to the floor. kHoleTol states that; a filled marker is 0 from selectedFill and
    // fails the next assertion.
    constexpr int kHoleTol = 30;
    REQUIRE(colourDistance(proportionalInk, floorInk) <= kHoleTol,
            "the PROPORTIONAL marker's centre rgb=%s is not floor-dominant rgb=%s (distance %d > %d):"
            " the body is not hollow",
            hex(proportionalInk).c_str(), hex(floorInk).c_str(), colourDistance(proportionalInk, floorInk),
            kHoleTol);
    REQUIRE(colourDistance(proportionalInk, selectedFill) > kInkMin,
            "the PROPORTIONAL marker's centre rgb=%s is still the filled ink rgb=%s: the ring was not drawn",
            hex(proportionalInk).c_str(), hex(selectedFill).c_str());
    // ...and the hue is a ring at the timeline's own inset.
    REQUIRE(colourDistance(ringInk, selectedFill) <= kFillTol,
            "the PROPORTIONAL marker's ring pixel rgb=%s is not the selected fill rgb=%s (distance %d > %d)"
            " at step %d",
            hex(ringInk).c_str(), hex(selectedFill).c_str(), colourDistance(ringInk, selectedFill),
            kFillTol, ringStep);

    // THE FLAG IS WHAT MAKES THE RING, and nothing else in the picture moves with it: in
    // the pass whose only difference is keyProportional[1] = false, the same marker is a
    // FILLED absolute marker, while its edge and its selection ring are unchanged.
    REQUIRE(colourDistance(unflaggedInk, absoluteInk) <= kFillTol,
            "the same marker with its proportional flag OFF rgb=%s is not the absolute fill rgb=%s: the flag"
            " no longer controls the body",
            hex(unflaggedInk).c_str(), hex(absoluteInk).c_str());
    REQUIRE(colourDistance(unflaggedInk, proportionalInk) > kInkMin,
            "the proportional marker's centre is rgb=%s in BOTH passes: the ring is gone",
            hex(proportionalInk).c_str());
    REQUIRE(colourDistance(unflaggedAbsoluteInk, absoluteInk) == 0,
            "the ABSOLUTE marker's own ink changed between the two passes: the proportional flag leaked"
            " into its neighbour");

    // THE EDGE BAND's own sample, at the same place checkKeys measures a key's edge.
    // The band is a PARTIAL-coverage blend by construction, so its pixel is
    // `coverage * keyEdge + (1 - coverage) * <the ink beneath>`. The ink beneath is the
    // hole (the floor) for the proportional marker and the selected fill for the
    // unflagged one, so the pixel cannot be byte-identical; what must hold is that the
    // edging itself is untouched. kEdgeTol is that statement's bound.
    constexpr int kEdgeTol = 20;
    const int edgeStep = int(std::lround(double(ctrl::graph::kKeyDotRadius))) - 1; // 3 -> radius 3.5
    const QPoint edgePixel(proportionalCentre.x() + edgeStep, proportionalCentre.y());
    const QColor edgeProportional = pixelAt(aProportional.image, edgePixel.x(), edgePixel.y());
    const QColor edgeUnflagged = pixelAt(aUnflagged.image, edgePixel.x(), edgePixel.y());
    const QColor edgeAbsolute = pixelAt(
        aProportional.image, absoluteCentre.x() + edgeStep, absoluteCentre.y()
    );
    REQUIRE(clearDistance(edgeProportional) > kInkMin,
            "the PROPORTIONAL marker has no ink at its edge pixel (%d,%d)", edgePixel.x(), edgePixel.y());
    REQUIRE(edgeProportional.red() <= ringInk.red() && edgeProportional.green() <= ringInk.green()
                && edgeProportional.blue() <= ringInk.blue(),
            "the PROPORTIONAL marker's edge pixel rgb=%s is not darker than its ring rgb=%s",
            hex(edgeProportional).c_str(), hex(ringInk).c_str());
    REQUIRE(edgeAbsolute.red() <= absoluteInk.red() && edgeAbsolute.green() <= absoluteInk.green()
                && edgeAbsolute.blue() <= absoluteInk.blue(),
            "the ABSOLUTE marker's edge pixel rgb=%s is not darker than its own fill rgb=%s",
            hex(edgeAbsolute).c_str(), hex(absoluteInk).c_str());
    REQUIRE(colourDistance(edgeProportional, edgeUnflagged) <= kEdgeTol,
            "the PROPORTIONAL marker's edge pixel rgb=%s differs from the same marker unflagged rgb=%s by %d"
            " (bound %d): the edge stroke must not take the ring's body",
            hex(edgeProportional).c_str(), hex(edgeUnflagged).c_str(),
            colourDistance(edgeProportional, edgeUnflagged), kEdgeTol);

    QPoint ringPixelProportional;
    QPoint ringPixelUnflagged;
    const QColor selectionRing = detachedRingInk(aProportional.image, proportionalCentre, ringPixelProportional);
    const QColor selectionRingUnflagged = detachedRingInk(aUnflagged.image, proportionalCentre, ringPixelUnflagged);
    REQUIRE(clearDistance(selectionRing) > kInkMin,
            "the PROPORTIONAL marker paints nothing at its ring probe (%d,%d): a selected proportional key"
            " must keep the selection ring",
            ringPixelProportional.x(), ringPixelProportional.y());
    REQUIRE(ringPixelProportional == ringPixelUnflagged
                && colourDistance(selectionRing, selectionRingUnflagged) == 0,
            "the PROPORTIONAL marker's selection ring rgb=%s at (%d,%d) differs from the same marker unflagged"
            " rgb=%s at (%d,%d): the ring must not take the body's ink",
            hex(selectionRing).c_str(), ringPixelProportional.x(), ringPixelProportional.y(),
            hex(selectionRingUnflagged).c_str(), ringPixelUnflagged.x(), ringPixelUnflagged.y());
    const double ringAlpha = double(ctrl::time::kSelectRingAlpha) / 255.0;
    const double ringCoverage = coverageOfTone(selectionRing, hue);
    REQUIRE(ringCoverage > 0.5 * ringAlpha && ringCoverage <= ringAlpha + 0.02
                && colourDistance(selectionRing, toneAtCoverage(hue, ringCoverage)) <= kCurveTol,
            "the PROPORTIONAL marker's ring pixel rgb=%s is not the channel hue rgb=%s at up to %.3f coverage"
            " (measured %.3f, distance %d > %d): proportional keys keep the timeline's ring ink",
            hex(selectionRing).c_str(), hex(hue).c_str(), ringAlpha, ringCoverage,
            colourDistance(selectionRing, toneAtCoverage(hue, ringCoverage)), kCurveTol);
    return true;
}

// 33: THE FRAME RANGE'S GUIDES ARE INKED, and where the model says. The timeline draws
// two vertical hairlines at the project's first and last frame and the panel draws the
// same two marks from the model's columns (GraphPanelModel::rangeGuideX). Three passes
// of the same model say exactly what is claimed, each a statement the others cannot
// make: with the guides and without them (is a column inked at all?), with the recede
// flag on (does an active play window weaken it?), and against the model's own rows
// (does the ink stop at the plot's top and bottom, where the ruler and the scroll bar
// own the rows?).
//
// The columns are chosen FROM the model's geometry, not from what looks clear: the
// first is inside the VALUE COLUMN - the state the shared frame mapping puts the
// project's first frame in at the usual fit, and the reason the guides are panel
// columns and not plot ones - and the second inside the plot, clear of the fixture's
// chips (300..371), its keys and its playhead column.
bool checkRangeGuides(
    const Rendered& aControl, const Rendered& aGuided, const Rendered& aReceded,
    const gui::GraphPanelModel& aModel
) {
    REQUIRE(aModel.rangeGuideX.size() == 2, "precondition: the guide fixture must publish two columns");
    const int panelColumn = aModel.rangeGuideX.value(0);
    const int plotColumn = aModel.rangeGuideX.value(1);
    REQUIRE(panelColumn < aModel.plot.left() && panelColumn >= aModel.panel.left(),
            "precondition: the fixture's first guide (%d) must be a PANEL column left of the plot (%d)",
            panelColumn, aModel.plot.left());
    REQUIRE(aModel.plot.contains(plotColumn, aModel.plot.center().y()),
            "precondition: the fixture's second guide (%d) must be inside the plot", plotColumn);

    const int rows = aModel.plot.height();
    int checked = 0;
    for (const int x : {panelColumn, plotColumn}) {
        int inside = 0;
        int above = 0;
        int below = 0;
        int neighbours = 0;
        for (int y = 0; y < kViewH; ++y) {
            const bool differs =
                colourDistance(pixelAt(aGuided.image, x, y), pixelAt(aControl.image, x, y)) > kInkMin;
            if (!differs)
                continue;
            if (y < aModel.plot.top())
                ++above;
            else if (y > aModel.plot.bottom())
                ++below;
            else
                ++inside;
        }
        // The 1px column is one column: the drawn neighbours are the next contract.
        for (const int dx : {-1, 1}) {
            for (int y = aModel.plot.top(); y <= aModel.plot.bottom(); ++y) {
                if (colourDistance(pixelAt(aGuided.image, x + dx, y), pixelAt(aControl.image, x + dx, y)) > kInkMin)
                    ++neighbours;
            }
        }
        std::printf(
            "MEASURED range_guide x=%d rows=%d inked=%d above_plot=%d below_plot=%d neighbour_px=%d"
            "  panel_left=%d plot=(%d..%d) rows=(%d..%d)\n",
            x, rows, inside, above, below, neighbours, aModel.panel.left(), aModel.plot.left(),
            aModel.plot.right(), aModel.plot.top(), aModel.plot.bottom()
        );
        // Every row the plot owns, less the few where an OPAQUE mark of the panel covers
        // the guide (a curve crossing, a glyph) - and never a row outside them.
        REQUIRE(
            inside >= rows * 3 / 4,
            "the guide at x=%d inked %d of the plot's %d rows with the guides published and none without them",
            x, inside, rows
        );
        REQUIRE(
            above == 0 && below == 0,
            "the guide at x=%d painted outside the plot's rows: %d above plot.top()=%d, %d below plot.bottom()=%d",
            x, above, aModel.plot.top(), below, aModel.plot.bottom()
        );
        REQUIRE(neighbours == 0, "the guide at x=%d spilled onto its neighbouring columns: %d px", x, neighbours);
        ++checked;
    }

    // THE RECESSION: an active play window halves the guides' ink (the timeline's 40%
    // -> 25%), so the receded column is the SAME column, weaker. Sampled on one row of
    // the panel-column guide, which no other mark crosses.
    const int sampleY = aModel.plot.center().y();
    const QColor clear = pixelAt(aControl.image, panelColumn, sampleY);
    const QColor plain = pixelAt(aGuided.image, panelColumn, sampleY);
    const QColor weak = pixelAt(aReceded.image, panelColumn, sampleY);
    const int plainDistance = colourDistance(plain, clear);
    const int weakDistance = colourDistance(weak, clear);
    std::printf(
        "MEASURED range_guide_recede x=%d y=%d without=%s guided=%s receded=%s  distance=%d -> %d\n",
        panelColumn, sampleY, hex(clear).c_str(), hex(plain).c_str(), hex(weak).c_str(), plainDistance,
        weakDistance
    );
    REQUIRE(plainDistance > kInkMin, "the guide left no ink at x=%d (distance %d)", panelColumn, plainDistance);
    REQUIRE(
        weakDistance > 0 && weakDistance < plainDistance,
        "the receded guide reads distance %d from the panel, not weaker than the plain guide's %d",
        weakDistance, plainDistance
    );

    return checked == 2;
}

// 4: the label, and the halo behind gui::kDrawTextHalo.
//
// The halo is SHIPPED OFF, so what this asserts in the shipping configuration is
// the INVERSE: nothing is painted outside the glyph box. The haloed assertions
// below it are kept whole for the flag-on run - and which of the two runs is the
// shipped one is a property of the flag itself (gui::kDrawTextHalo, declared in
// src/gui/GraphPanelFrame.h), so this file cannot drift from the panel.
bool checkLabelHalo(
    const Rendered& aRendered, const gui::GraphPanelModel& aModel, const Rendered& aBare,
    const theme::Graph& aTheme
) {
    REQUIRE(!aModel.frameLabels.isEmpty(), "precondition: the model must carry a frame label");
    REQUIRE(!aModel.rulerBand.isEmpty(), "precondition: the model must carry the ruler band");
    const gui::PanelLabel& label = aModel.frameLabels.at(0);
    // THE FRAME NUMBERS LIVE IN THE RULER SECTION, above the plot (the plot's clip
    // starts below it), so their boxes and glyphs are bounded by the band.
    REQUIRE(aModel.rulerBand.contains(label.rect), "precondition: the label box must be inside the ruler band");

    const QRect glyph =
        glyphBox(label.rect, label.text, aRendered.font, Align::Left, VAlign::Top);
    REQUIRE(!glyph.isEmpty(), "precondition: the frame font lays out '%s' with empty metrics", qPrintable(label.text));
    REQUIRE(aModel.rulerBand.contains(glyph), "precondition: the glyph box must be inside the ruler band");

    // (a) the glyph itself is inked - read as a PAIR against the label-less pass, since
    // the ruler's band fills these rows and "not the clear colour" would count the band.
    REQUIRE(!aBare.image.isNull(), "precondition: the label-less pass must be rendered for the halo pair");
    std::vector<QColor> glyphInk;
    for (int y = glyph.top(); y <= glyph.bottom(); ++y) {
        for (int x = glyph.left(); x <= glyph.right(); ++x) {
            const QColor c = pixelAt(aRendered.image, x, y);
            if (colourDistance(c, pixelAt(aBare.image, x, y)) > kInkMin) {
                glyphInk.push_back(c);
            }
        }
    }
    std::printf(
        "MEASURED label box=(%d,%d %dx%d) glyph=(%d,%d %dx%d) text=%s ink_pixels_in_glyph_box=%d of %d"
        " halo_flag=%d\n",
        label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(), glyph.left(), glyph.top(),
        glyph.width(), glyph.height(), qPrintable(label.text), int(glyphInk.size()), glyph.width() * glyph.height(),
        int(gui::kDrawTextHalo)
    );
    REQUIRE(!glyphInk.empty(), "the label '%s' painted nothing inside its own glyph box", qPrintable(label.text));

    // (b) the ring 1-2px outside the glyph box, measured against BOTH tokens. The
    // pixel is outside the advance box drawLabel measured, so no glyph pass puts ink
    // there: while the halo is on, the only passes that do are its 16 offsets and
    // their colour is theme::Graph::halo().
    const QColor axisText = aTheme.axisText();
    const QColor halo = aTheme.halo();
    int haloPixels = 0;
    int bestHaloDistance = 1 << 20;
    int worstHaloDistance = -1;
    int bestHaloX = -1;
    int bestHaloY = -1;
    for (int y = glyph.top() - 2; y <= glyph.bottom() + 2; ++y) {
        for (int x = glyph.left() - 2; x <= glyph.right() + 2; ++x) {
            if (glyph.contains(x, y) || !aModel.rulerBand.contains(x, y)) {
                continue;
            }
            const QColor c = pixelAt(aRendered.image, x, y);
            // The ring is read as a PAIR where the label-less pass exists (the ruler's
            // band fills these rows, so "not the clear colour" is no longer "the
            // label's ink"): a pixel only the glyphs put down is the one that differs.
            const bool inked = aBare.image.isNull()
                ? clearDistance(c) > kInkMin
                : colourDistance(c, pixelAt(aBare.image, x, y)) > kInkMin;
            if (!inked) {
                continue;
            }
            ++haloPixels;
            const int d = colourDistance(c, halo);
            if (d < bestHaloDistance) {
                bestHaloDistance = d;
                bestHaloX = x;
                bestHaloY = y;
            }
            worstHaloDistance = std::max(worstHaloDistance, d);
        }
    }

    std::printf(
        "MEASURED halo ring=1..2px_outside_glyph inked_pixels=%d best=(%d,%d) distance_to_halo=%d"
        " distance_to_axisText=%d worst_distance_to_halo=%d halo=%s axisText=%s\n",
        haloPixels, bestHaloX, bestHaloY, bestHaloDistance,
        bestHaloX >= 0 ? colourDistance(pixelAt(aRendered.image, bestHaloX, bestHaloY), axisText) : -1,
        worstHaloDistance, hex(halo).c_str(), hex(axisText).c_str()
    );

    if (!gui::kDrawTextHalo) {
        // SHIPPED: the halo is off, so the panel paints NOTHING outside a glyph box
        // that the SAME model drawn without its frame labels does not paint. The
        // reading is a PAIR against that label-less pass rather than an absolute
        // "clear" count: the ruler's band fills these rows now, so a pixel the band
        // alone paints is not the label's ink.
        REQUIRE(!aBare.image.isNull(), "precondition: the label-less pass must be rendered for the halo pair");
        REQUIRE(
            haloPixels == 0,
            "the halo flag is off but %d pixels 1-2px outside the glyph box carry ink the label-less pass does not"
            " (first %d,%d): the label is still haloed",
            haloPixels, bestHaloX, bestHaloY
        );
        REQUIRE(
            colourDistance(halo, axisText) > kInkMin,
            "precondition: this palette's halo and text colours are indistinguishable, so the halo cannot be told from"
            " a glyph"
        );
        return true;
    }

    REQUIRE(
        haloPixels > 0, "the halo flag is on but no pixel within 1-2px of the glyph box carries the halo"
    );
    REQUIRE(
        bestHaloDistance < colourDistance(pixelAt(aRendered.image, bestHaloX, bestHaloY), axisText),
        "the inked pixel outside the glyph box at (%d,%d) is nearer the text colour than the halo colour: it is not"
        " the halo",
        bestHaloX, bestHaloY
    );
    REQUIRE(
        colourDistance(halo, axisText) > kInkMin,
        "precondition: this palette's halo and text colours are indistinguishable, so the halo cannot be told from"
        " a glyph"
    );
    return true;
}

// 16: THE FRAME NUMBERS' RESERVED BAND. The X-axis numbers are drawn in the ruler
// SECTION ABOVE THE PLOT (the scaler reserves its 22 rows, so the section is outside
// the plot clip), and two things keep them inside it: the model puts every frame
// label's box BELOW the tick band (its own 4px marks), and the drawing layer clamps a
// { font taller than the box } into the box instead of letting it hang out of the
// top. This asserts both halves on the drawn pass: the boxes are inside the band and
// clear of the tick rows, and the glyph box the panel actually used (rebuilt by
// drawLabel's own rule, clamp included) is inside the band - so a label the panel
// cannot draw inside its section would fail here rather than being quietly cut.
// It also pins the section boundary: band.bottom() + 1 == plot.top() and the two
// rects do not intersect.
bool checkRulerReserve(
    const Rendered& aRendered, const gui::GraphPanelModel& aModel, const Rendered& aTight,
    const gui::GraphPanelModel& aTightModel, const Rendered& aTightBare, const theme::Graph& aTheme
) {
    // The glyph arithmetic below and the glyphs share one face: the pass's own QFont.
    REQUIRE(!aModel.frameLabels.isEmpty(), "precondition: the full model must carry frame labels");
    REQUIRE(!aModel.frameTickX.isEmpty(), "precondition: the full model must carry frame ticks");
    REQUIRE(!aModel.rulerBand.isEmpty(), "precondition: the full model must carry the ruler band");
    // The band is the timeline's own strip, and its ticks rise from its LAST row
    // (the model's frameRuler metrics; a model that forgot them would draw the
    // ruler at y = -1).
    const QRect band = aModel.rulerBand;
    // THE BAND IS A SECTION AT THE PANEL'S TOP ROW, with the scaler's kMargin rows
    // between its hairline and the plot's first row, so it is outside the plot clip
    // and no curve/key/handle/chip can ink it.
    REQUIRE(
        aModel.rulerBandTop == band.top() && band.height() == gui::frameRuler::kBandHeight
            && band.top() == kPanel.top()
            && band.bottom() + 1 + ctrl::graph::Scaler::kMargin == kPlot.top() && !band.intersects(kPlot),
        "precondition: the ruler band (%d,%d %dx%d) is not the model's %dpx strip at the panel's top row (top %d)",
        band.left(), band.top(), band.width(), band.height(), gui::frameRuler::kBandHeight, kPanel.top()
    );

    // A pixel of the band that is NOT the band's own surface: the glyphs, the ticks and
    // the playhead's pill are the only ink that can sit on it.
    const auto bandInk = [&aTheme](const QImage& aImage, const QRect& aRect) {
        int count = 0;
        const QRect r = aRect.intersected(QRect(0, 0, aImage.width(), aImage.height()));
        for (int y = r.top(); y <= r.bottom(); ++y) {
            for (int x = r.left(); x <= r.right(); ++x) {
                if (colourDistance(pixelAt(aImage, x, y), aTheme.rulerBand()) > kInkMin)
                    ++count;
            }
        }
        return count;
    };

    int checked = 0;
    for (const gui::PanelLabel& label : aModel.frameLabels) {
        REQUIRE(
            band.contains(label.rect),
            "the frame label '%s' box (%d,%d %dx%d) is not inside the ruler band: the numbers live in the section"
            " above the plot",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height()
        );
        REQUIRE(
            label.rect.top() == band.top() + gui::frameRuler::kNumberTop
                && label.rect.height() == gui::frameRuler::kNumberHeight,
            "the frame label '%s' box (%d,%d %dx%d) is not in the band's number row (top %d, height %d): the"
            " numbers moved out of the ruler",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(),
            band.top() + gui::frameRuler::kNumberTop, gui::frameRuler::kNumberHeight
        );
        REQUIRE(
            label.rect.bottom() < band.top() + gui::frameRuler::kBandHeight - gui::frameRuler::kTickLength,
            "the frame label '%s' box reaches y=%d, into the %dpx tick rows that rise from the band's last row"
            " (%d): the numbers are not clear of the ruler's own marks",
            qPrintable(label.text), label.rect.bottom(), gui::frameRuler::kTickLength, band.bottom()
        );
        const QRect glyph =
            glyphBox(label.rect, label.text, aRendered.font, Align::Left, VAlign::Top);
        REQUIRE(
            !glyph.isEmpty(), "precondition: the frame font lays out '%s' with empty metrics", qPrintable(label.text)
        );
        REQUIRE(
            band.contains(glyph),
            "the frame label '%s' draws its glyph box (%d,%d %dx%d) outside the ruler band: the numbers have left"
            " their section",
            qPrintable(label.text), glyph.left(), glyph.top(), glyph.width(), glyph.height()
        );
        const int ink = bandInk(aRendered.image, grow(glyph, 1));
        std::printf(
            "MEASURED ruler_reserve label=%s box=(%d,%d %dx%d) glyph=(%d,%d %dx%d) band=(%d,%d %dx%d)"
            " above_band_top=%dpx below_tick_rows=%dpx ink_pixels=%d\n",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(),
            glyph.left(), glyph.top(), glyph.width(), glyph.height(), band.left(), band.top(), band.width(),
            band.height(), glyph.top() - band.top(),
            (band.top() + gui::frameRuler::kBandHeight - gui::frameRuler::kTickLength) - glyph.bottom(), ink
        );
        REQUIRE(ink > 0, "the frame label '%s' painted no ink inside its own glyph box", qPrintable(label.text));
        ++checked;
    }
    REQUIRE(checked == aModel.frameLabels.size(), "walked %d of %d frame labels", checked,
            int(aModel.frameLabels.size()));

    // ---- AND THE CASE THE CLAMP EXISTS FOR: A FONT TALLER THAN THE BOX ----
    //
    // The model measures its boxes from the ruler's fixed metrics and draws with the
    // widget's font, so a layer font with a taller line height puts a glyph cell bigger
    // than the box. Centred, half of that overflow goes ABOVE the box - i.e. out of the
    // band's number row, toward the band's top edge (the section's own boundary), which
    // is the "X-axis numbers clipped at the top" defect. drawLabel therefore clamps the
    // glyph to the box's top edge and lets the overflow hang into the box's own
    // interior instead. This pass is that configuration: the FULL model with every
    // frame label's box 8px tall, moved down into the band so there are rows above it.
    //
    // The reading is a PAIR: the same model with its labels removed (`aTightBare`) has
    // the identical band, ticks and pill, so a pixel above a box that differs between
    // the two passes is glyph ink and nothing else.
    int tightChecked = 0;
    for (const gui::PanelLabel& label : aTightModel.frameLabels) {
        const QFontMetricsF metrics(aTight.font);
        REQUIRE(
            metrics.height() > double(label.rect.height()),
            "precondition: the tight model's box (%dpx) is not shorter than the font (%.1fpx), so this is not the"
            " over-tall case",
            label.rect.height(), metrics.height()
        );
        const QRect glyph = glyphBox(label.rect, label.text, aTight.font, Align::Left, VAlign::Top);
        // THE PLACEMENT'S OWN STATEMENT, and it is about the INK: a Top-aligned run starts its
        // LINE box on the box's first row and lets an over-tall font hang BELOW the box, never
        // above the number row - the timeline's own rule (TimeRuler draws with TextBaseline::Top
        // at the band's number row). The scan below is what proves no pixel escapes upward.
        // And the pixels: the rows band.top() .. box.top()-1, over the label's own
        // columns, are IDENTICAL to the same model drawn without this label. The tick
        // columns are excluded: the ticks are the ruler's own ink, not the number's.
        int ink = 0;
        int tickPixels = 0;
        for (int x = label.rect.left(); x <= label.rect.right(); ++x) {
            bool tickColumn = false;
            for (const int tickX : aTightModel.frameTickX) {
                if (std::abs(x - tickX) <= 1)
                    tickColumn = true;
            }
            for (int y = band.top(); y < label.rect.top(); ++y) {
                if (colourDistance(pixelAt(aTight.image, x, y), pixelAt(aTightBare.image, x, y)) > kInkMin) {
                    if (tickColumn)
                        ++tickPixels;
                    else
                        ++ink;
                }
            }
        }
        const QRect above(label.rect.left(), band.top(), label.rect.width(), label.rect.top() - band.top());
        std::printf(
            "MEASURED ruler_tight label=%s box=(%d,%d %dx%d) font_line_height=%d/100 glyph_top=%d"
            " above_box_band=(%d,%d %dx%d) glyph_ink_pixels=%d tick_ink_pixels=%d\n",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(),
            int(metrics.height() * 100.0), glyph.top(), above.left(), above.top(), above.width(), above.height(),
            ink, tickPixels
        );
        REQUIRE(
            ink == 0,
            "the frame label '%s' painted %d pixels above its own box (rows %d..%d of the band, the ticks' columns"
            " excluded): a taller font is not clamped into the box",
            qPrintable(label.text), ink, above.top(), above.bottom()
        );
        ++tightChecked;
    }
    REQUIRE(tightChecked > 0, "precondition: the tight model must carry frame labels");
    return true;
}

// THE RULER'S TWO ROW CLAIMS, measured on the pixels: the frame numbers start ON the
// band's number row (not two rows below it, which is what centring a 14px box around a
// shorter line box did), and the frame ticks ink SIX rows - the band's last row AND the
// five above it, which is what the timeline's `drawLine(pos, pos + QPoint(0,
// -kTickLength))` inks with a 1px pen.
//
// The numbers are read as a PAIR (with and without the frame labels), so a row that
// differs is glyph ink and nothing else - the band, the ticks, the hairline and the
// playhead are identical in both passes. The pass is rendered with a font size where a
// CENTRED glyph would sit two rows lower than a top-aligned one (the shipped gate font
// is 13px against the 14px box, where the two placements differ by half a row and
// truncate to the same pixel row): at 10px the centring offset is (14 - 10) / 2 = 2
// rows, so a regression to centring moves the first inked row from
// `box.top() + bearing` to `box.top() + 2 + bearing` and fails the bound below.
bool checkRulerRows(
    const Rendered& aRendered, const Rendered& aBare, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme
) {
    REQUIRE(!aRendered.image.isNull() && !aBare.image.isNull(), "precondition: both ruler passes must be rendered");
    REQUIRE(!aModel.frameLabels.isEmpty(), "precondition: the model must carry a frame label");
    REQUIRE(!aModel.rulerBand.isEmpty(), "precondition: the model must carry the ruler band");
    const QRect& band = aModel.rulerBand;
    const gui::PanelLabel& label = aModel.frameLabels.at(0);
    const QRect glyph =
        glyphBox(label.rect, label.text, aRendered.font, Align::Left, VAlign::Top);

    // (a) THE NUMBER'S INK starts where the TOP-ALIGNED placement puts it, not where a
    // centred run would: InkCanvas draws a Top-aligned run with TextBaseline::Top at the
    // box's first row (the timeline's own rule), so the first inked row is
    // box.top() + ascent + the glyph's own tight top - and the centred placement of the
    // SAME font in the SAME box would start it (box.height - line height) / 2 lower.
    // The pass is rendered at a pixel size whose centring offset is at least two rows, so
    // the pair below discriminates the two placements rather than accepting either.
    int inkTop = -1;
    for (int y = label.rect.top(); y <= label.rect.bottom() && inkTop < 0; ++y) {
        for (int x = glyph.left(); x <= glyph.right() && inkTop < 0; ++x) {
            if (colourDistance(pixelAt(aRendered.image, x, y), pixelAt(aBare.image, x, y)) > kInkMin) {
                inkTop = y;
            }
        }
    }
    REQUIRE(inkTop >= 0, "the frame number '%s' painted no ink inside its own glyph box", qPrintable(label.text));
    const QFontMetricsF metrics(aRendered.font);
    const double tightTop = metrics.tightBoundingRect(label.text).top();
    const double topAnchor = double(label.rect.top()) + metrics.ascent() + tightTop;
    const int expectedInkTop = int(std::floor(topAnchor));
    const double centredOffset = std::max((double(label.rect.height()) - metrics.height()) * 0.5, 0.0);
    const int centredInkTop = int(std::floor(topAnchor + centredOffset));
    std::printf(
        "MEASURED ruler_number_rows label=%s box_top=%d line_box_top=%d ink_top=%d expected_ink_top=%d"
        " centred_would_be=%d font_px=%d line_height=%d/100\n",
        qPrintable(label.text), label.rect.top(), glyph.top(), inkTop, expectedInkTop, centredInkTop,
        QFontInfo(aRendered.font).pixelSize(), int(metrics.height() * 100.0)
    );
    REQUIRE(
        std::abs(inkTop - expectedInkTop) <= 1,
        "the frame number '%s' starts its ink on row %d, not on row %d: a Top-aligned run starts at box.top() +"
        " ascent + the glyph's tight top (the timeline's own rule), while centring the same font would start it on"
        " row %d",
        qPrintable(label.text), inkTop, expectedInkTop, centredInkTop
    );
    REQUIRE(
        centredInkTop - expectedInkTop >= 2,
        "precondition: this pass's font centres only %d row(s) below its top-aligned placement, so the assertion"
        " could not tell the two placements apart",
        centredInkTop - expectedInkTop
    );

    // (b) THE TICKS: the band's last row AND the kTickLength rows above it. A tick's
    // own column is used, chosen clear of the playhead's 1px line (which crosses every
    // row of the band). The number boxes end at `band.top() + kNumberHeight`, well
    // above the rows scanned here, so no label can contribute ink to them.
    const int tickTop = band.bottom() - gui::frameRuler::kTickLength;
    int tickX = -1;
    for (const int x : aModel.frameTickX) {
        if (aModel.playheadX < 0 || std::abs(x - aModel.playheadX) > 2) {
            tickX = x;
            break;
        }
    }
    REQUIRE(tickX >= 0, "precondition: the model must carry a frame tick clear of every number box and the pill");
    int first = -1;
    int last = -1;
    // The scan stops ON the band's last row: the row under it is the hairline, which
    // spans the band's whole width and would be read as the tick's own ink.
    for (int y = tickTop - 2; y <= band.bottom(); ++y) {
        const bool inked =
            colourDistance(pixelAt(aRendered.image, tickX, y), aTheme.rulerBand()) > kInkMin;
        if (inked) {
            if (first < 0)
                first = y;
            last = y;
        }
    }
    std::printf(
        "MEASURED ruler_tick_rows x=%d rows=%d..%d (band.last=%d, first expected %d, %d rows)\n", tickX, first, last,
        band.bottom(), tickTop, gui::frameRuler::kTickLength + 1
    );
    REQUIRE(
        first == tickTop,
        "the frame tick at x=%d starts its ink on row %d, not on row %d (the band's last row minus the %dpx tick"
        " length): the ruler's tick lost its top row",
        tickX, first, tickTop, gui::frameRuler::kTickLength
    );
    REQUIRE(
        last == band.bottom(),
        "the frame tick at x=%d ends its ink on row %d, not on the band's last row %d", tickX, last, band.bottom()
    );
    REQUIRE(
        last - first + 1 == gui::frameRuler::kTickLength + 1,
        "the frame tick at x=%d inks %d rows (%d..%d), not the %d the timeline's 1px pen inks", tickX, last - first + 1,
        first, last, gui::frameRuler::kTickLength + 1
    );
    return true;
}

// 28: THE TEMPORARY PLAYBACK WINDOW'S OVERLAY, on the model's own geometry. The model
// publishes the ruler-band wash, the two 2px bracket stems and the two label arms; the
// drawing layer must paint them with the timeline's tokens, in the timeline's order
// (band under, stems, pills over). The pass is the model with ONLY the band and the
// overlay (no ticks, labels or playhead), so every pixel read below is the overlay's.
// The second pass is the same window collapsed to two adjacent frames: the arms must
// be gone and ONE centred pill must be drawn instead.
//
// It also pins the two pill rules the user reported: the arm's TEXT COLUMN is the
// timeline's (`pill.left() + kPillPadding`, not a second centring on the drawing
// font's advance - the panel's old centring was 3px off on this pass), and the pill's
// outer corner is an ANTIALIASED radius (the 3x3 corner box carries partial-coverage
// pixels, where a hard filled corner has none). The timeline's own pill corners are
// gated in tests/timeline_visuals.cpp through the same gui::paintPill rule.
bool checkPlayWindowOverlay(
    const Rendered& aSplit, const gui::GraphPanelModel& aSplitModel, const Rendered& aMerged,
    const gui::GraphPanelModel& aMergedModel, const theme::Graph& aTheme, const theme::Colors& aColors
) {
    REQUIRE(!aSplitModel.playWindow.band.isEmpty(), "precondition: the split pass must carry a window band");
    REQUIRE(
        !aSplitModel.playWindow.startPill.isEmpty() && !aSplitModel.playWindow.endPill.isEmpty(),
        "precondition: the split pass must carry both label arms"
    );
    REQUIRE(
        aSplitModel.playWindow.mergedPill.isEmpty(), "precondition: the split arms must not be merged"
    );
    const QRect band = aSplitModel.rulerBand;
    REQUIRE(!band.isEmpty(), "precondition: the overlay must be in the ruler band");

    // The band's own surface, where nothing of the window lands.
    const QRect plainBand(band.left(), band.top(), aSplitModel.playWindow.band.left() - band.left(), band.height());
    int surface = 0;
    for (int y = plainBand.top(); y <= plainBand.bottom(); ++y)
        for (int x = plainBand.left(); x <= plainBand.right(); ++x)
            if (colourDistance(pixelAt(aSplit.image, x, y), aTheme.rulerBand()) <= kFillTol)
                ++surface;

    // THE WASH: the rows above the pill row are pure band + windowBand, nothing else can
    // land there (stems and pills start at kPillTop).
    const QRect washRows(
        aSplitModel.playWindow.band.left(), band.top(), aSplitModel.playWindow.band.width(),
        gui::frameRuler::kPillTop
    );
    const QColor washed = over(aColors.windowBand, aTheme.rulerBand());
    int wash = 0;
    for (int y = washRows.top(); y <= washRows.bottom(); ++y)
        for (int x = washRows.left(); x <= washRows.right(); ++x)
            if (colourDistance(pixelAt(aSplit.image, x, y), washed) <= kFillTol)
                ++wash;

    // THE STEMS: 2px, opaque windowEdge, exactly the published rects.
    const auto countInk = [](const QImage& aImage, const QRect& aRect, const QColor& aInk) {
        int count = 0;
        for (int y = aRect.top(); y <= aRect.bottom(); ++y)
            for (int x = aRect.left(); x <= aRect.right(); ++x)
                if (colourDistance(pixelAt(aImage, x, y), aInk) <= kFillTol)
                    ++count;
        return count;
    };
    const int startStemInk = countInk(aSplit.image, aSplitModel.playWindow.startStem, aColors.windowEdge);
    const int endStemInk = countInk(aSplit.image, aSplitModel.playWindow.endStem, aColors.windowEdge);

    // THE PILLS: filled windowEdge with the number in windowText punched through, and
    // the face against each stem squared (so the fill reaches the stem's column).
    const int startPillInk = countInk(aSplit.image, aSplitModel.playWindow.startPill, aColors.windowEdge);
    const int endPillInk = countInk(aSplit.image, aSplitModel.playWindow.endPill, aColors.windowEdge);
    int startTextInk = 0;
    int endTextInk = 0;
    const auto countText = [](const QImage& aImage, const QRect& aRect, const QColor& aText, const QColor& aFill) {
        int count = 0;
        for (int y = aRect.top(); y <= aRect.bottom(); ++y) {
            for (int x = aRect.left(); x <= aRect.right(); ++x) {
                const QColor c = pixelAt(aImage, x, y);
                if (colourDistance(c, aText) < colourDistance(c, aFill) && colourDistance(c, aText) <= 48)
                    ++count;
            }
        }
        return count;
    };
    startTextInk = countText(
        aSplit.image, aSplitModel.playWindow.startPill, aColors.windowText, aColors.windowEdge
    );
    endTextInk = countText(aSplit.image, aSplitModel.playWindow.endPill, aColors.windowText, aColors.windowEdge);

    // ---- THE TEXT COLUMN IS THE TIMELINE'S, ON THE PIXELS ----
    //
    // TimeRuler::drawPillText centres a pill it measured as
    // `advance + 2 * kPillPadding` with INTEGER halves, and that reduces to exactly
    // `pill.left() + kPillPadding` for every advance width (see the derivation at
    // that function). The panel used to centre again, on the DRAWING font's advance,
    // which is a different number: on this pass's 30px arm the gate's bitmap font
    // measures "20" at 14px against the model's 20, so the old origin was
    // 142 + (30-14)/2 = 150 while the timeline's is 142 + 5 = 147.
    //
    // The scan is the first column inside the arm that carries text ink (the same
    // read countText uses). Its first inked column is the run's origin plus the
    // first glyph's left bearing, which the atlas owns - so the assertion allows the
    // 2px a bearing (plus the boundary column's zero-coverage edge) can add, and
    // still fails an origin 3px off.
    // The scan starts ON the rule's own column, not at the pill's left edge: the
    // rounded corner's blend pixels sit in the first/last kPillRadius columns and a
    // fill blended toward the dark band can read as "text" under this distance test.
    // Starting at left + kPillPadding skips them and asks the only question that
    // matters - does ink begin where the rule says, or later (the old centring)?
    const auto firstTextColumn = [](const QImage& aImage, const QRect& aPill, const QColor& aText,
                                    const QColor& aFill) {
        for (int x = aPill.left() + gui::frameRuler::kPillPadding; x <= aPill.right(); ++x) {
            for (int y = aPill.top(); y <= aPill.bottom(); ++y) {
                const QColor c = pixelAt(aImage, x, y);
                if (colourDistance(c, aText) < colourDistance(c, aFill) && colourDistance(c, aText) <= 48)
                    return x;
            }
        }
        return -1;
    };
    // The same read from the right: the run's LAST inked column. This one does not
    // depend on the first glyph's left bearing at all, so it is the assertion that
    // cannot be fooled by a wide bearing: the run ends at `origin + advance`, and the
    // old centring moved that three columns right.
    const auto lastTextColumn = [](const QImage& aImage, const QRect& aPill, const QColor& aText,
                                   const QColor& aFill) {
        for (int x = aPill.right() - gui::frameRuler::kPillPadding; x >= aPill.left(); --x) {
            for (int y = aPill.top(); y <= aPill.bottom(); ++y) {
                const QColor c = pixelAt(aImage, x, y);
                if (colourDistance(c, aText) < colourDistance(c, aFill) && colourDistance(c, aText) <= 48)
                    return x;
            }
        }
        return -1;
    };
    // The pass's own QFont is what the backend laid the runs out with.
    const QFontMetricsF splitMetrics(aSplit.font);
    const int startExpected = aSplitModel.playWindow.startPill.left() + gui::frameRuler::kPillPadding;
    const int endExpected = aSplitModel.playWindow.endPill.left() + gui::frameRuler::kPillPadding;
    // THE ORIGIN THE OLD CENTRING PRODUCED, from the drawing font's own advance, so
    // the failure message states both numbers rather than a rule.
    const int startDrawnWidth = int(std::ceil(splitMetrics.horizontalAdvance(aSplitModel.playWindow.startText)));
    const int endDrawnWidth = int(std::ceil(splitMetrics.horizontalAdvance(aSplitModel.playWindow.endText)));
    const int startOldCentred = aSplitModel.playWindow.startPill.left()
        + (aSplitModel.playWindow.startPill.width() - startDrawnWidth) / 2;
    const int startFirstInk = firstTextColumn(
        aSplit.image, aSplitModel.playWindow.startPill, aColors.windowText, aColors.windowEdge
    );
    const int endFirstInk = firstTextColumn(
        aSplit.image, aSplitModel.playWindow.endPill, aColors.windowText, aColors.windowEdge
    );
    REQUIRE(
        startFirstInk >= startExpected && startFirstInk <= startExpected + 2,
        "the first label arm's text starts at column %d; the timeline's pill rule puts it on column %d (left +"
        " kPillPadding) and only a glyph bearing may add to that (%d is the old re-centring on the drawing font)",
        startFirstInk, startExpected, startOldCentred
    );
    REQUIRE(
        endFirstInk >= endExpected && endFirstInk <= endExpected + 2,
        "the second label arm's text starts at column %d; the timeline's pill rule puts it on column %d (left +"
        " kPillPadding) and only a glyph bearing may add to that",
        endFirstInk, endExpected
    );
    // ...and its LAST column, which is bearing-independent: a run laid out at
    // `origin = left + kPillPadding` ends inside `origin + advance`, while the old
    // centring ended `(oldOrigin - expected)` columns further right.
    const int startLastInk = lastTextColumn(
        aSplit.image, aSplitModel.playWindow.startPill, aColors.windowText, aColors.windowEdge
    );
    const int endLastInk = lastTextColumn(
        aSplit.image, aSplitModel.playWindow.endPill, aColors.windowText, aColors.windowEdge
    );
    REQUIRE(
        startLastInk >= startExpected && startLastInk <= startExpected + startDrawnWidth,
        "the first label arm's text ends at column %d; the timeline's rule puts its origin on %d and its advance is"
        " %dpx, so the run cannot end past %d (the old centring ended at %d)",
        startLastInk, startExpected, startDrawnWidth, startExpected + startDrawnWidth,
        startOldCentred + startDrawnWidth - 1
    );
    REQUIRE(
        endLastInk >= endExpected && endLastInk <= endExpected + endDrawnWidth,
        "the second label arm's text ends at column %d; the timeline's rule puts its origin on %d and its advance"
        " is %dpx, so the run cannot end past %d",
        endLastInk, endExpected, endDrawnWidth, endExpected + endDrawnWidth
    );

    // ---- THE CORNERS ARE A ROUNDED CUT, IN THIS RASTERISER'S OWN TERMS ----
    //
    // The playback window's arms are rounded on their OUTER corners (the faces against the
    // stems are squared by the overdraw above): the start arm's top-right and the end arm's
    // top-left. Canvas2D antialiases the arc ANALYTICALLY, so at this 3px radius on an
    // integer-aligned pill the arc's outer pixel is already fully outside the path - there is
    // no partial-coverage pixel to count, and a "blend pixel" reading would be a statement
    // about a tessellated arc rather than about the shape. What survives every
    // rasteriser is the CUT: the corner box carries at least one pixel that is NOT the pill's
    // fill, while the pill's face beside it does - a hard filled rect paints the whole box.
    // The timeline's playhead pill is gated on the same rule in tests/timeline_visuals.cpp.
    const auto cornerCut = [](const QImage& aImage, const QRect& aCorner, const QColor& aFill) {
        int cut = 0;
        int fill = 0;
        for (int y = aCorner.top(); y <= aCorner.bottom(); ++y) {
            for (int x = aCorner.left(); x <= aCorner.right(); ++x) {
                if (colourDistance(pixelAt(aImage, x, y), aFill) > kFillTol)
                    ++cut;
                else
                    ++fill;
            }
        }
        return QPair<int, int>(cut, fill);
    };
    const int radius = gui::frameRuler::kPillRadius;
    const QRect startRoundedCorner(
        aSplitModel.playWindow.startPill.right() - radius + 1, aSplitModel.playWindow.startPill.top(), radius,
        radius
    );
    const QRect endRoundedCorner(
        aSplitModel.playWindow.endPill.left(), aSplitModel.playWindow.endPill.top(), radius, radius
    );
    const QPair<int, int> startCorner = cornerCut(aSplit.image, startRoundedCorner, aColors.windowEdge);
    const QPair<int, int> endCorner = cornerCut(aSplit.image, endRoundedCorner, aColors.windowEdge);
    std::printf(
        "MEASURED play_window_corners start_box=%d/%d cut end_box=%d/%d cut (radius %d)\n",
        startCorner.first, startCorner.first + startCorner.second, endCorner.first,
        endCorner.first + endCorner.second, radius
    );
    REQUIRE(
        startCorner.first > 0 && startCorner.second > 0,
        "the first arm's rounded corner box has %d cut / %d fill pixels: the %dpx radius is being filled as a hard"
        " corner",
        startCorner.first, startCorner.second, radius
    );
    REQUIRE(
        endCorner.first > 0 && endCorner.second > 0,
        "the second arm's rounded corner box has %d cut / %d fill pixels: the %dpx radius is being filled as a hard"
        " corner",
        endCorner.first, endCorner.second, radius
    );
    std::printf(
        "MEASURED play_window split: band=(%d,%d %dx%d) wash_rows=(%d,%d %dx%d) washed_px=%d"
        " stems=%d/%d of %d/%d pills=%d/%d of %d/%d text_px=%d/%d"
        " text_cols=%d..%d/%d..%d expected=%d/%d corner_cut=%d/%d\n",
        aSplitModel.playWindow.band.left(), aSplitModel.playWindow.band.top(),
        aSplitModel.playWindow.band.width(), aSplitModel.playWindow.band.height(), washRows.left(),
        washRows.top(), washRows.width(), washRows.height(), wash,
        startStemInk, endStemInk, aSplitModel.playWindow.startStem.width() * aSplitModel.playWindow.startStem.height(),
        aSplitModel.playWindow.endStem.width() * aSplitModel.playWindow.endStem.height(), startPillInk,
        endPillInk, aSplitModel.playWindow.startPill.width() * aSplitModel.playWindow.startPill.height(),
        aSplitModel.playWindow.endPill.width() * aSplitModel.playWindow.endPill.height(), startTextInk,
        endTextInk, startFirstInk, startLastInk, endFirstInk, endLastInk, startExpected, endExpected,
        startCorner.first, endCorner.first
    );
    REQUIRE(surface > 0, "the ruler band's own surface was not painted where the window does not reach");
    REQUIRE(wash > 0, "the translucent window band painted nothing on the ruler's band");
    REQUIRE(
        startStemInk == aSplitModel.playWindow.startStem.width() * aSplitModel.playWindow.startStem.height(),
        "the first bracket stem painted %d of its %d pixels", startStemInk,
        aSplitModel.playWindow.startStem.width() * aSplitModel.playWindow.startStem.height()
    );
    REQUIRE(
        endStemInk == aSplitModel.playWindow.endStem.width() * aSplitModel.playWindow.endStem.height(),
        "the second bracket stem painted %d of its %d pixels", endStemInk,
        aSplitModel.playWindow.endStem.width() * aSplitModel.playWindow.endStem.height()
    );
    REQUIRE(
        startPillInk > 0 && endPillInk > 0, "the label arms painted no pill fill (%d/%d pixels)", startPillInk,
        endPillInk
    );
    REQUIRE(startTextInk > 0, "the first label arm painted no window text");
    REQUIRE(endTextInk > 0, "the second label arm painted no window text");

    // ---- THE MERGED PASS: one centred pill, no arms ----
    REQUIRE(
        !aMergedModel.playWindow.mergedPill.isEmpty(), "precondition: the merged pass must carry one pill"
    );
    REQUIRE(
        aMergedModel.playWindow.startPill.isEmpty() && aMergedModel.playWindow.endPill.isEmpty(),
        "precondition: a merged window must publish no arms"
    );
    const int mergedInk = countInk(aMerged.image, aMergedModel.playWindow.mergedPill, aColors.windowEdge);
    const int mergedTextInk = countText(
        aMerged.image, aMergedModel.playWindow.mergedPill, aColors.windowText, aColors.windowEdge
    );
    std::printf(
        "MEASURED play_window merged: pill=(%d,%d %dx%d) fill_px=%d text_px=%d\n",
        aMergedModel.playWindow.mergedPill.left(), aMergedModel.playWindow.mergedPill.top(),
        aMergedModel.playWindow.mergedPill.width(), aMergedModel.playWindow.mergedPill.height(), mergedInk,
        mergedTextInk
    );
    REQUIRE(mergedInk > 0, "the merged label pill painted no fill");
    REQUIRE(mergedTextInk > 0, "the merged label pill painted no text");

    // The merged pill's text is on the same rule (left + kPillPadding), not the
    // centre of the merged box: the two differ whenever the drawing font's advance
    // differs from the font the model measured the pill with.
    const int mergedExpected = aMergedModel.playWindow.mergedPill.left() + gui::frameRuler::kPillPadding;
    const int mergedFirstInk = firstTextColumn(
        aMerged.image, aMergedModel.playWindow.mergedPill, aColors.windowText, aColors.windowEdge
    );
    REQUIRE(
        mergedFirstInk >= mergedExpected && mergedFirstInk <= mergedExpected + 2,
        "the merged label's text starts at column %d; the timeline's pill rule puts it on column %d",
        mergedFirstInk, mergedExpected
    );
    return true;
}

// 9b: the name CHIPS, as a pair per curve - the emphasis rule the key markers are read
// under, on the other ink it decides. A chip is drawn while its curve is in play (it owns
// a selected key) OR while the POINTER is within kChipHoverDistance (6px) of the curve at
// the pointer's own column - which is why every pass here is rendered with
// the pointer OFF the plot (the harness's default): the hovered branch is a second,
// selection-independent way for a chip to appear, and a box read as byte-clean must have
// neither reason to be drawn. So:
//   * in the EMPTY-SELECTION pass (pointer off the plot) the box is byte-clean: the gate
//     places each chip box clear of every other element, so any ink there would be the
//     chip's own;
//   * in the pass that selects that curve's key the same box carries the chip's backing
//     (theme::Graph::halo() at kChipBackdropAlpha, a fully covered interior pixel) and
//     its glyphs.
bool checkChips(
    const Rendered& aRendered, const Rendered& aFirstSelected, const Rendered& aSecondSelected,
    const gui::GraphPanelModel& aModel, const theme::Graph& aTheme
) {
    constexpr float kChipToneAlpha = 0.82f; // GraphPanelFrame.cpp's kChipBackdropAlpha

    // The pass's own QFont is what the backend laid the chip out with.
    REQUIRE(aModel.curves.size() == 2, "precondition: the chip pair needs the full model's two curves");
    REQUIRE(aModel.channels.size() == 2, "precondition: the full model must carry the two channel rows");

    int checked = 0;
    for (int c = 0; c < aModel.curves.size(); ++c) {
        const gui::PanelCurve& curve = aModel.curves.at(c);
        REQUIRE(!curve.chip.isEmpty(), "precondition: curve %d publishes no chip box", c);
        REQUIRE(!curve.chipText.isEmpty(), "precondition: curve %d publishes no chip text", c);
        REQUIRE(kPlot.contains(curve.chip), "precondition: curve %d's chip box is outside the plot", c);
        // THE CHANNEL LIST, in the currency the model publishes it (no row geometry: the
        // list is the application's own QMenu). What a swatch used to be is now the
        // row's DATA, and the property is the same one: the row a curve belongs to names
        // the same channel, carries the same colour the panel draws the curve and its
        // chip text in, and publishes the states the panel acts on.
        REQUIRE(
            curve.channelRow >= 0 && curve.channelRow < aModel.channels.size(),
            "precondition: curve %d names row %d of %d channel rows", c, curve.channelRow,
            int(aModel.channels.size())
        );
        const gui::PanelChannelRow& row = aModel.channels.at(curve.channelRow);
        REQUIRE(row.channel == curve.channel, "curve %d's row names another channel", c);
        REQUIRE(row.color == curve.color, "curve %d's row carries a colour the panel does not draw it in", c);
        REQUIRE(row.name == curve.chipText, "curve %d's row and its chip name it differently", c);
        REQUIRE(!row.name.isEmpty() && !row.layer.isEmpty(), "precondition: channel row %d carries no words", c);
        const Rendered& inPlay = (c == 0) ? aFirstSelected : aSecondSelected;
        const int area = curve.chip.width() * curve.chip.height();
        const int dimmedInk = inkCount(aRendered.image, curve.chip);
        const int inPlayInk = inkCount(inPlay.image, curve.chip);
        const QRect glyph =
            glyphBox(curve.chip, curve.chipText, aRendered.font, Align::Centre);
        REQUIRE(!glyph.isEmpty(), "precondition: the chip '%s' has empty metrics", qPrintable(curve.chipText));
        const int textInk = inkCount(inPlay.image, glyph);

        // The backing alone: the box's left column, on its centre row, clear of the text.
        const QPoint backing(curve.chip.left() + 2, curve.chip.center().y());
        REQUIRE(!glyph.contains(backing), "precondition: curve %d's backing probe lands under its own text", c);
        QColor tone = aTheme.halo();
        tone.setAlphaF(tone.alphaF() * kChipToneAlpha);
        const QColor expected = over(tone, kClear);
        const QColor got = pixelAt(inPlay.image, backing.x(), backing.y());
        const int distance = colourDistance(got, expected);

        std::printf(
            "MEASURED chip curve=%d text=%s box=(%d,%d %dx%d) ink_dimmed=%d ink_in_play=%d of %d"
            " text_glyph=(%d,%d %dx%d) text_ink=%d backing=(%d,%d) rgb=%s halo_at_%.2f_over_clear=%s d=%d"
            " tolerance=%d\n",
            c, qPrintable(curve.chipText), curve.chip.left(), curve.chip.top(), curve.chip.width(),
            curve.chip.height(), dimmedInk, inPlayInk, area, glyph.left(), glyph.top(), glyph.width(),
            glyph.height(), textInk, backing.x(), backing.y(), hex(got).c_str(), double(kChipToneAlpha),
            hex(expected).c_str(), distance, kFillTol
        );
        REQUIRE(
            dimmedInk == 0,
            "curve %d's chip box carries %d pixels with NOTHING selected: a chip is drawn only for a curve in play",
            c, dimmedInk
        );
        REQUIRE(
            inPlayInk >= (area * 8) / 10,
            "curve %d's chip painted %d of its %d box pixels in the pass that puts its channel in play", c,
            inPlayInk, area
        );
        REQUIRE(textInk > 0, "curve %d's chip painted nothing in its own text's glyph box", c);
        REQUIRE(
            distance <= kFillTol,
            "curve %d's chip backing rgb=%s is not theme::Graph::halo() composited at %.2f on the clear colour"
            " rgb=%s (distance %d > %d)",
            c, hex(got).c_str(), double(kChipToneAlpha), hex(expected).c_str(), distance, kFillTol
        );
        ++checked;
    }
    REQUIRE(checked == 2, "expected the two curves' chips, walked %d", checked);
    return true;
}

// 6: the empty state.
bool checkEmptyState(const Rendered& aRendered, const gui::GraphPanelModel& aModel) {
    REQUIRE(aModel.curves.isEmpty() && !aModel.emptyState.isEmpty(), "precondition: the model must be the empty one");
    // The state's own glyph box, from the pass's font and the backend's placement.

    // The state text is drawn centred in the PLOT.
    const QRect text = glyphBox(aModel.plot, aModel.emptyState, aRendered.font, Align::Centre);
    REQUIRE(!text.isEmpty(), "precondition: the state string '%s' has empty metrics", qPrintable(aModel.emptyState));
    REQUIRE(kPlot.contains(text), "precondition: the centred state text must be inside the plot");

    const int textInk = inkCount(aRendered.image, grow(text, 2));
    std::printf(
        "MEASURED empty_state text=%s glyph=(%d,%d %dx%d) ink_pixels=%d\n", qPrintable(aModel.emptyState),
        text.left(), text.top(), text.width(), text.height(), textInk
    );
    REQUIRE(textInk > 0, "the empty panel painted nothing for its own reason: '%s'", qPrintable(aModel.emptyState));

    // And the rest of the plot is untouched. The probe rect is verified against
    // every other geometry item of THIS model (which has no curves at all).
    const QRect probe(350, 200, 25, 25);
    REQUIRE(kPlot.contains(probe), "precondition: the empty-state probe rect must be inside the plot");
    const std::vector<Geom> geoms = geometryOf(aModel, aRendered.font);
    for (const Geom& geom : geoms) {
        REQUIRE(
            !probe.intersects(geom.rect),
            "precondition: the empty-state probe rect intersects %s (%d,%d %dx%d)", geom.name.c_str(),
            geom.rect.left(), geom.rect.top(), geom.rect.width(), geom.rect.height()
        );
    }
    REQUIRE(!probe.intersects(grow(text, 2)), "precondition: the empty-state probe rect overlaps the state text");

    const int ink = inkCount(aRendered.image, probe);
    std::printf(
        "MEASURED empty_state_clean rect=(%d,%d %dx%d) margins: right_of_text+halo=%dpx above_plot_bottom=%dpx"
        " ink_pixels=%d of %d\n",
        probe.left(), probe.top(), probe.width(), probe.height(), probe.left() - (text.right() + 2),
        kPlot.bottom() - probe.bottom(), ink, probe.width() * probe.height()
    );
    REQUIRE(
        ink == 0,
        "the empty panel painted %d pixels in an otherwise empty part of the plot: the panel must have no background"
        " and no grid",
        ink
    );
    return true;
}

// 7: the plot clip.
bool checkClip(const Rendered& aRendered, const gui::GraphPanelModel& aModel) {
    REQUIRE(aModel.curves.size() == 1, "precondition: the clip model must carry exactly one curve");
    REQUIRE(!aModel.isEmpty(), "precondition: the clip model must not be an empty state");
    REQUIRE(
        aModel.valueLabels.isEmpty() && aModel.frameLabels.isEmpty() && aModel.frameTickX.isEmpty()
            && aModel.valueTickY.isEmpty() && aModel.channels.isEmpty()
            && aModel.playheadX < 0 && aModel.zeroY < 0,
        "precondition: the clip model must hold nothing but the curve and the marquee, or 'no ink outside the"
        " plot' would have to allow for the value column's own labels"
    );
    REQUIRE(
        !aModel.marquee.isEmpty() && !kPlot.contains(aModel.marquee),
        "precondition: the clip model's marquee must extend past the plot on every side"
    );
    // The marquee's own body is in the plot, so the in-plot control below counts both
    // ink sources - which is what makes the clean bands below a statement about the
    // clip holding BOTH of them, not about the curve alone.

    // The one in-plot part of the polyline: the horizontal segment at y=150.
    const QRect inPlot(180, 146, 40, 9);
    REQUIRE(kPlot.contains(inPlot), "precondition: the in-plot sample rect must be inside the plot");
    const int inPlotInk = inkCount(aRendered.image, inPlot);
    std::printf(
        "MEASURED clip in_plot_rect=(%d,%d %dx%d) ink_pixels=%d of %d\n", inPlot.left(), inPlot.top(),
        inPlot.width(), inPlot.height(), inPlotInk, inPlot.width() * inPlot.height()
    );
    REQUIRE(inPlotInk > 0, "the in-plot part of the clipped curve painted nothing: the clip cannot be measured");

    // Everything the clip must hold back. The bands cover the whole attachment
    // outside the plot, including the value column and the 20 rows below the panel.
    const QRect bands[] = {
        QRect(0, 0, kPlot.left(), kViewH),                                    // left of the plot (the value column)
        QRect(kPlot.right() + 1, 0, kViewW - kPlot.right() - 1, kViewH),      // right of the plot
        QRect(kPlot.left(), 0, kPlot.width(), kPlot.top()),                   // above the plot
        QRect(kPlot.left(), kPlot.bottom() + 1, kPlot.width(), kViewH - kPlot.bottom() - 1), // below the plot
        QRect(0, kPanel.height(), kViewW, kViewH - kPanel.height()),          // below the panel
    };
    const char* bandNames[] = {"left_of_plot", "right_of_plot", "above_plot", "below_plot", "below_panel"};

    const int valueColumnInk = inkCount(aRendered.image, aModel.valueColumn);
    std::printf(
        "MEASURED clip_value_column rect=(%d,%d %dx%d) ink_pixels=%d of %d\n", aModel.valueColumn.left(),
        aModel.valueColumn.top(), aModel.valueColumn.width(), aModel.valueColumn.height(), valueColumnInk,
        aModel.valueColumn.width() * aModel.valueColumn.height()
    );
    REQUIRE(
        valueColumnInk == 0,
        "the clipped curve painted %d pixels in the value column: a curve outside the value window must not reach it",
        valueColumnInk
    );

    int total = 0;
    for (int i = 0; i < 5; ++i) {
        const int ink = inkCount(aRendered.image, bands[i]);
        total += ink;
        std::printf(
            "MEASURED clip_band band=%s rect=(%d,%d %dx%d) ink_pixels=%d\n", bandNames[i], bands[i].left(),
            bands[i].top(), bands[i].width(), bands[i].height(), ink
        );
        REQUIRE(
            ink == 0, "the clipped curve painted %d pixels in %s (%d,%d %dx%d): the plot clip is not holding",
            ink, bandNames[i], bands[i].left(), bands[i].top(), bands[i].width(), bands[i].height()
        );
    }
    std::printf("MEASURED clip ink_pixels_outside_plot=%d of %d\n", total, kViewW * kViewH - kPlot.width() * kPlot.height());
    return true;
}

// 8: the zero rule, as a pair: the same model with zeroY set and with zeroY == -1.
bool checkZeroRule(const Rendered& aWithRule, const Rendered& aWithoutRule, const gui::GraphPanelModel& aModel) {
    REQUIRE(aModel.zeroY >= 0, "precondition: the 'with rule' model must publish zeroY");
    REQUIRE(kPlot.contains(kPlot.left(), aModel.zeroY) && kPlot.contains(kPlot.right(), aModel.zeroY),
            "precondition: zeroY must be inside the plot's y range");

    // x positions clear of every other mark (the playhead is at x=200).
    const int columns[] = {100, 160, 300, 370};

    // The rule is a 1px line with AA off: where it lands within the ±2 band is the
    // rasterizer's business, so the band is sampled, not one row.
    int withInk = 0;
    for (int x : columns) {
        REQUIRE(kPlot.contains(x, aModel.zeroY), "precondition: the sampled column x=%d must be inside the plot", x);
        int inkedRow = -1;
        for (int y = aModel.zeroY - 2; y <= aModel.zeroY + 2 && inkedRow < 0; ++y) {
            if (clearDistance(pixelAt(aWithRule.image, x, y)) > kInkMin) {
                inkedRow = y;
            }
        }
        bool cleanWithout = true;
        for (int y = aModel.zeroY - 2; y <= aModel.zeroY + 2; ++y) {
            if (clearDistance(pixelAt(aWithoutRule.image, x, y)) > kInkMin) {
                cleanWithout = false;
            }
        }
        std::printf(
            "MEASURED zero_rule x=%d zeroY=%d inked_row_with_rule=%d clean_band_without_rule=%d band=%d..%d\n", x,
            aModel.zeroY, inkedRow, int(cleanWithout), aModel.zeroY - 2, aModel.zeroY + 2
        );
        REQUIRE(
            inkedRow >= 0, "the zero rule left no ink at x=%d in the rows %d..%d around zeroY", x, aModel.zeroY - 2,
            aModel.zeroY + 2
        );
        REQUIRE(
            cleanWithout,
            "the row band around zeroY carries ink at x=%d with zeroY == -1: something other than the zero rule"
            " paints there, and the pair cannot tell the rule apart",
            x
        );
        ++withInk;
    }
    REQUIRE(withInk == 4, "expected the zero rule at 4 columns, measured %d", withInk);

    // And the rule stops at the plot's own edges: it must not reach into the value
    // column, nor past the plot's right edge.
    const int outsideLeft = inkCount(aWithoutRule.image, QRect(kValueColumn.left(), aModel.zeroY - 2, kValueColumn.width(), 5));
    const int outsideLeftWith = inkCount(aWithRule.image, QRect(kValueColumn.left(), aModel.zeroY - 2, kValueColumn.width(), 5));
    const int outsideRightWith = inkCount(aWithRule.image, QRect(kPlot.right() + 1, aModel.zeroY - 2, kViewW - kPlot.right() - 1, 5));
    std::printf(
        "MEASURED zero_rule_edges value_column_ink=%d (without rule %d) right_of_plot_ink=%d\n", outsideLeftWith,
        outsideLeft, outsideRightWith
    );
    REQUIRE(
        outsideLeftWith == 0 && outsideLeft == 0 && outsideRightWith == 0,
        "the zero rule reached outside the plot: value column %d px, right of the plot %d px", outsideLeftWith,
        outsideRightWith
    );
    return true;
}

// 31: THE SCROLL BARS ARE THE APPLICATION'S. The four rects the model publishes are
// lanes and runs; the panel paints AppStyle.cpp's CC_ScrollBar inside them - a 4px
// pill across the run in theme::Colors::outline, and NOTHING for the trough. The
// proof is a PAIR of passes: the same model with the four rects EMPTY. Outside the
// two pills (grown by 1px for the fill's antialiased cap) every pixel of the two
// frames must be identical, so a trough - or any other bar ink - is caught wherever
// it falls, including over the playhead's column, which crosses the panel and
// therefore the horizontal lane. Two further passes move the pointer onto each lane
// in turn: that lane's pill takes theme::Colors::hover while the other stays outline,
// which is both directions of the hover rule (and both transitions the host repaints
// on - gui::panelBarAt is asserted here too).
bool checkScrollBars(
    const Rendered& aWithBars, const Rendered& aWithoutBars, const Rendered& aHoveredH,
    const Rendered& aHoveredV, const gui::GraphPanelModel& aModel
) {
    const theme::Colors& colors = theme::Colors::current();
    const QRect hTrack = aModel.hTrack;
    const QRect hRun = aModel.hThumb;
    const QRect vTrack = aModel.vTrack;
    const QRect vRun = aModel.vThumb;
    const QRect hPill = scrollPill(hTrack, hRun, true);
    const QRect vPill = scrollPill(vTrack, vRun, false);

    REQUIRE(
        !hTrack.isEmpty() && !hRun.isEmpty() && !vTrack.isEmpty() && !vRun.isEmpty(),
        "precondition: the model must publish four bar rects"
    );
    // The layout the panel promises: outside the plot, inside the panel, the two
    // disjoint from each other and clear of the value column.
    REQUIRE(
        !hTrack.intersects(kPlot) && !vTrack.intersects(kPlot),
        "precondition: a bar track overlaps the plot (%d,%d %dx%d vs %d,%d %dx%d)", hTrack.left(), hTrack.top(),
        hTrack.width(), hTrack.height(), vTrack.left(), vTrack.top(), vTrack.width(), vTrack.height()
    );
    REQUIRE(aModel.panel.contains(hTrack) && aModel.panel.contains(vTrack), "precondition: a track leaves the panel");
    REQUIRE(!hTrack.intersects(vTrack), "precondition: the two tracks overlap");
    REQUIRE(!hTrack.intersects(kValueColumn) && !vTrack.intersects(kValueColumn), "precondition: a track covers the value column");
    REQUIRE(hTrack.contains(hRun) && vTrack.contains(vRun), "precondition: a run leaves its track");
    // The lane and the pill are the application's numbers, and the pill is centred in
    // the lane across the run.
    REQUIRE(
        hTrack.height() == ctrl::graph::scrollBar::kLaneThickness
            && vTrack.width() == ctrl::graph::scrollBar::kLaneThickness,
        "precondition: the lanes are not the app's 12px (%d x %d)", hTrack.height(), vTrack.width()
    );
    REQUIRE(
        hTrack.contains(hPill) && vTrack.contains(vPill) && hPill.width() == hRun.width()
            && vPill.height() == vRun.height(),
        "precondition: a pill leaves its lane/run (%d,%d %dx%d in %d,%d %dx%d)", hPill.left(), hPill.top(),
        hPill.width(), hPill.height(), hTrack.left(), hTrack.top(), hTrack.width(), hTrack.height()
    );
    // THE LANES ARE FLUSH WITH THE PANEL'S EDGES - the timeline's own bar geometry: the
    // horizontal lane's last row is the panel's last row and its right edge the panel's
    // right edge; the vertical lane's right edge is the panel's right edge and it ends
    // on the row above the horizontal lane. A lane inset from the edge is the divergence
    // this gate exists for.
    REQUIRE(
        hTrack.bottom() == aModel.panel.bottom() && hTrack.right() == aModel.panel.right()
            && hTrack.left() == aModel.panel.left(),
        "the horizontal lane (%d,%d %dx%d) is not flush with the panel (%d,%d %dx%d): bottom %d vs %d, right %d"
        " vs %d",
        hTrack.left(), hTrack.top(), hTrack.width(), hTrack.height(), aModel.panel.left(), aModel.panel.top(),
        aModel.panel.width(), aModel.panel.height(), hTrack.bottom(), aModel.panel.bottom(), hTrack.right(),
        aModel.panel.right()
    );
    REQUIRE(
        vTrack.right() == aModel.panel.right() && vTrack.bottom() + 1 == hTrack.top(),
        "the vertical lane (%d,%d %dx%d) is not flush with the panel's right edge / not above the horizontal"
        " lane (hTrack top %d)",
        vTrack.left(), vTrack.top(), vTrack.width(), vTrack.height(), hTrack.top()
    );
    // The two tokens must be tellable apart (and from the clear colour), or the
    // counts below would not know which ink a pixel is.
    REQUIRE(
        colors.outline != colors.hover && clearDistance(colors.outline) > kInkMin
            && clearDistance(colors.hover) > kInkMin,
        "precondition: the pill tokens are not distinct (outline %s, hover %s)",
        hex(colors.outline).c_str(), hex(colors.hover).c_str()
    );

    // FOUR PIXELS THICK, measured across the pill's middle: the horizontal pill's
    // column at the centre of the run, and the vertical pill's row.
    const int hPillThickness = toneCount(
        aWithBars.image, QRect(hPill.center().x(), hTrack.top(), 1, hTrack.height()), colors.outline, kFillTol
    );
    const int vPillThickness = toneCount(
        aWithBars.image, QRect(vTrack.left(), vPill.center().y(), vTrack.width(), 1), colors.outline, kFillTol
    );
    // ...AND WHERE THAT BAND IS. The same two probes cross the pill through its
    // straight section, where the fill is opaque, so the token's extent along the
    // thickness axis is the pill's own band: it must be exactly the shared
    // geometry's top..bottom (horizontal) / left..right (vertical) on the LANE's
    // axis - not merely four pixels somewhere in the lane. A pill centred one pixel
    // off, or drawn from a different thickness/lane rule, fails here.
    const QPair<int, int> hBand = tokenSpan(
        aWithBars.image, QRect(hPill.center().x(), hTrack.top(), 1, hTrack.height()), colors.outline, false,
        kFillTol
    );
    const QPair<int, int> vBand = tokenSpan(
        aWithBars.image, QRect(vTrack.left(), vPill.center().y(), vTrack.width(), 1), colors.outline, true, kFillTol
    );
    const int hPillInk = toneCount(aWithBars.image, hPill, colors.outline, kFillTol);
    const int vPillInk = toneCount(aWithBars.image, vPill, colors.outline, kFillTol);
    std::printf(
        "MEASURED scroll_bars outline=%s hover=%s lane=%d  hTrack(%d,%d %dx%d) hPill(%d,%d %dx%d) ink=%d thick=%d"
        " band=%d..%d (shared %d..%d)  vTrack(%d,%d %dx%d) vPill(%d,%d %dx%d) ink=%d thick=%d band=%d..%d"
        " (shared %d..%d)  (expected thickness %d)\n",
        hex(colors.outline).c_str(), hex(colors.hover).c_str(), ctrl::graph::scrollBar::kLaneThickness,
        hTrack.left(), hTrack.top(), hTrack.width(), hTrack.height(), hPill.left(), hPill.top(), hPill.width(),
        hPill.height(), hPillInk, hPillThickness, hBand.first, hBand.second, hPill.top(), hPill.bottom(),
        vTrack.left(), vTrack.top(), vTrack.width(), vTrack.height(), vPill.left(), vPill.top(), vPill.width(),
        vPill.height(), vPillInk, vPillThickness, vBand.first, vBand.second, vPill.left(), vPill.right(),
        ctrl::graph::scrollBar::kPillThickness
    );
    REQUIRE(hPillInk > 0, "the horizontal pill painted no outline pixel: the pill is not where the model says");
    REQUIRE(vPillInk > 0, "the vertical pill painted no outline pixel");
    REQUIRE(
        hPillThickness == ctrl::graph::scrollBar::kPillThickness,
        "the horizontal pill is %dpx thick, not the app's %d", hPillThickness,
        ctrl::graph::scrollBar::kPillThickness
    );
    REQUIRE(
        vPillThickness == ctrl::graph::scrollBar::kPillThickness,
        "the vertical pill is %dpx thick, not the app's %d", vPillThickness,
        ctrl::graph::scrollBar::kPillThickness
    );
    REQUIRE(
        hBand.first == hPill.top() && hBand.second == hPill.bottom(),
        "the horizontal pill's band is rows %d..%d, not the shared rect's %d..%d", hBand.first, hBand.second,
        hPill.top(), hPill.bottom()
    );
    REQUIRE(
        vBand.first == vPill.left() && vBand.second == vPill.right(),
        "the vertical pill's band is columns %d..%d, not the shared rect's %d..%d", vBand.first, vBand.second,
        vPill.left(), vPill.right()
    );

    // THE PILL'S ENDS ARE A ROUNDED, ANTIALIASED CAP - the Ink contract owes every filled
    // shape an antialiased edge even while the chrome's LINE antialias is off (the scroll
    // bars are drawn inside that block), and the bar is the one rounded fill that lives
    // there. A 3x3 box at the end's outer corner must carry at least one pixel that is
    // neither the pill's token nor the panel floor: that partial coverage IS the arc.
    // A hard-edged fill leaves only the two.
    const auto blendsIn = [&](const QImage& aImage, const QRect& aBox) {
        int blends = 0;
        for (int y = aBox.top(); y <= aBox.bottom(); ++y) {
            for (int x = aBox.left(); x <= aBox.right(); ++x) {
                const QColor c = pixelAt(aImage, x, y);
                if (colourDistance(c, colors.outline) > kFillTol && clearDistance(c) > kFillTol)
                    ++blends;
            }
        }
        return blends;
    };
    // The horizontal pill's cap is at its RIGHT end; the vertical one's at its BOTTOM.
    const int hCap = blendsIn(aWithBars.image, QRect(hPill.right() - 2, hPill.top(), 3, 3));
    const int vCap = blendsIn(aWithBars.image, QRect(vPill.left(), vPill.bottom() - 2, 3, 3));
    std::printf("MEASURED scroll_bars_caps h_end_blends=%d v_end_blends=%d of 9\n", hCap, vCap);
    REQUIRE(hCap > 0, "the horizontal pill's end has %d antialiased pixels: its %dpx cap is drawn hard",
            hCap, ctrl::graph::scrollBar::kPillRadius);
    REQUIRE(vCap > 0, "the vertical pill's end has %d antialiased pixels: its %dpx cap is drawn hard",
            vCap, ctrl::graph::scrollBar::kPillRadius);

    // THE TROUGH PAINTS NOTHING. Adding the four rects to the model may change pixels
    // ONLY inside the pills (grown by 1px for the fill's antialiased cap): every other
    // pixel of the two frames is identical.
    const QRect hPillReach = grow(hPill, 1);
    const QRect vPillReach = grow(vPill, 1);
    int stray = 0;
    for (int y = 0; y < aWithBars.image.height(); ++y) {
        for (int x = 0; x < aWithBars.image.width(); ++x) {
            if (hPillReach.contains(x, y) || vPillReach.contains(x, y))
                continue;
            if (pixelAt(aWithBars.image, x, y) != pixelAt(aWithoutBars.image, x, y))
                ++stray;
        }
    }
    std::printf(
        "MEASURED scroll_bars_stray %d px differ between the published bars and no bars outside the pills"
        " (of %d)\n",
        stray, aWithBars.image.width() * aWithBars.image.height()
    );
    REQUIRE(stray == 0, "the bars painted %d pixels outside their pills: the trough is not invisible", stray);

    // THE HOVERED PILL TAKES theme::Colors::hover, and the other bar does not - in
    // BOTH directions. Each pass differs from the rest frame only in the pointer, so a
    // colour change at a pill is the pointer's and nothing else.
    const QRect hProbe(hPill.center().x(), hTrack.top(), 1, hTrack.height());
    const QRect vProbe(vTrack.left(), vPill.center().y(), vTrack.width(), 1);
    const int hoveredHPill = toneCount(aHoveredH.image, hProbe, colors.hover, kFillTol);
    const int restVPill = toneCount(aHoveredH.image, vPill, colors.outline);
    const int hoveredVPill = toneCount(aHoveredV.image, vProbe, colors.hover, kFillTol);
    const int restHPill = toneCount(aHoveredV.image, hPill, colors.outline);
    // ...and with the pointer OFF neither pill is hot, so the change above is the
    // pointer's and not a constant.
    const int restHoverH = toneCount(aWithBars.image, hPill, colors.hover);
    const int restHoverV = toneCount(aWithBars.image, vPill, colors.hover);
    const QColor hHotPixel = pixelAt(aHoveredH.image, hPill.center().x(), hPill.center().y());
    const QColor vHotPixel = pixelAt(aHoveredV.image, vPill.center().x(), vPill.center().y());
    std::printf(
        "MEASURED scroll_bars_hover pointer_on_h: h_pill_hover_px_at_centre=%d rest_v_pill_outline_px=%d"
        " (pointer off: h_pill_hover_px=%d)  pointer_on_v: v_pill_hover_px_at_centre=%d"
        " rest_h_pill_outline_px=%d (pointer off: v_pill_hover_px=%d)  hot_rgb h=%s v=%s (token %s)\n",
        hoveredHPill, restVPill, restHoverH, hoveredVPill, restHPill, restHoverV,
        hex(hHotPixel).c_str(), hex(vHotPixel).c_str(), hex(colors.hover).c_str()
    );
    REQUIRE(
        hoveredHPill == ctrl::graph::scrollBar::kPillThickness,
        "a pointer on the horizontal bar did not turn the whole pill hover-coloured (%d px thick)",
        hoveredHPill
    );
    REQUIRE(
        hoveredVPill == ctrl::graph::scrollBar::kPillThickness,
        "a pointer on the vertical bar did not turn the whole pill hover-coloured (%d px thick)",
        hoveredVPill
    );
    REQUIRE(restVPill > 0, "the pointer on one bar changed the other bar's pill");
    REQUIRE(restHPill > 0, "the pointer on the vertical bar changed the horizontal one's pill");
    REQUIRE(restHoverH == 0 && restHoverV == 0, "a resting pill carries the hover token with no pointer on it");
    // THE EXACT TOKEN, not a blend of it: the pill's centre is the fill's opaque
    // interior, and AppStyle's CC_ScrollBar paints it in the hover colour itself.
    REQUIRE(
        hHotPixel == colors.hover && vHotPixel == colors.hover,
        "a hovered pill's centre is %s (h) / %s (v), not the hover token %s", hex(hHotPixel).c_str(),
        hex(vHotPixel).c_str(), hex(colors.hover).c_str()
    );

    // THE HOVER IDENTITY the host repaints on (gui::panelBarAt). The rule under test is
    // "repaint when it CHANGES, not per move", so entering a lane, leaving it and
    // crossing between the lanes are the three transitions that must differ, while two
    // points inside one lane must compare equal.
    const QPoint hOn = hTrack.center();
    const QPoint vOn = vTrack.center();
    const QPoint plotPoint = kPlot.center();
    const QPoint hOther(hOn.x() + 3, hOn.y());
    std::printf(
        "MEASURED scroll_bars_hover_identity off=%d h=%d h_other=%d v=%d  changed(enter h)=%d changed(leave h)=%d"
        " changed(h->v)=%d same_lane_changed=%d\n",
        int(gui::panelBarAt(aModel, plotPoint)), int(gui::panelBarAt(aModel, hOn)),
        int(gui::panelBarAt(aModel, hOther)), int(gui::panelBarAt(aModel, vOn)),
        int(gui::panelBarAt(aModel, hOn) != gui::panelBarAt(aModel, plotPoint)),
        int(gui::panelBarAt(aModel, plotPoint) != gui::panelBarAt(aModel, hOn)),
        int(gui::panelBarAt(aModel, vOn) != gui::panelBarAt(aModel, hOn)),
        int(gui::panelBarAt(aModel, hOther) != gui::panelBarAt(aModel, hOn))
    );
    REQUIRE(gui::panelBarAt(aModel, plotPoint) == gui::PanelBar::None, "the plot answers a bar");
    REQUIRE(gui::panelBarAt(aModel, hOn) == gui::PanelBar::Horizontal, "the horizontal lane is not its own identity");
    REQUIRE(gui::panelBarAt(aModel, vOn) == gui::PanelBar::Vertical, "the vertical lane is not its own identity");
    REQUIRE(
        gui::panelBarAt(aModel, hOther) == gui::panelBarAt(aModel, hOn),
        "a move inside one lane changed the hover identity: every move would repaint"
    );
    REQUIRE(
        gui::panelBarAt(aModel, hOn) != gui::panelBarAt(aModel, plotPoint)
            && gui::panelBarAt(aModel, plotPoint) != gui::panelBarAt(aModel, hOn),
        "entering/leaving a lane did not change the hover identity: the hover ink would never be painted"
    );
    REQUIRE(
        gui::panelBarAt(aModel, vOn) != gui::panelBarAt(aModel, hOn),
        "crossing between the two lanes did not change the hover identity"
    );
    return true;
}

// 31a: A LANE WITH NO TRAVEL IS STILL A DRAWN PILL, in BOTH directions. A model whose
// runs equal their tracks (the state Scaler::scrollBars publishes when the window
// covers its content) must paint a pill across the WHOLE lane in each bar, in the same
// 4px outline ink, with the trough invisible - the vertical lane does not vanish. A
// pointer on either full-length pill still turns it hover-coloured (hover is a drawing
// fact, not a hit-test one: the lane is not scrollable, but it is visible). The
// scrollability rule itself is the editor's and is gated in tests/graph_drag.cpp.
bool checkFullLaneBars(
    const Rendered& aWithBars, const Rendered& aWithoutBars, const Rendered& aHoveredH,
    const Rendered& aHoveredV, const gui::GraphPanelModel& aModel
) {
    const theme::Colors& colors = theme::Colors::current();
    const QRect hTrack = aModel.hTrack;
    const QRect vTrack = aModel.vTrack;
    const QRect hPill = scrollPill(hTrack, aModel.hThumb, true);
    const QRect vPill = scrollPill(vTrack, aModel.vThumb, false);
    REQUIRE(
        !hTrack.isEmpty() && !vTrack.isEmpty(), "precondition: the fixture must publish two lanes"
    );
    REQUIRE(
        aModel.hThumb == hTrack && aModel.vThumb == vTrack,
        "precondition: the fixture is not the zero-travel state (h %dx%d of %dx%d, v %dx%d of %dx%d)",
        aModel.hThumb.width(), aModel.hThumb.height(), hTrack.width(), hTrack.height(),
        aModel.vThumb.width(), aModel.vThumb.height(), vTrack.width(), vTrack.height()
    );
    const int hPillInk = toneCount(aWithBars.image, hPill, colors.outline, kFillTol);
    const int vPillInk = toneCount(aWithBars.image, vPill, colors.outline, kFillTol);
    const int hThickness = toneCount(
        aWithBars.image, QRect(hPill.center().x(), hTrack.top(), 1, hTrack.height()), colors.outline, kFillTol
    );
    const int vThickness = toneCount(
        aWithBars.image, QRect(vTrack.left(), vPill.center().y(), vTrack.width(), 1), colors.outline, kFillTol
    );
    // The band's POSITION as well as its thickness, off the same opaque cross-section
    // (see checkScrollBars): a full-lane pill is the shared rect's, row for row.
    const QPair<int, int> hBand = tokenSpan(
        aWithBars.image, QRect(hPill.center().x(), hTrack.top(), 1, hTrack.height()), colors.outline, false,
        kFillTol
    );
    const QPair<int, int> vBand = tokenSpan(
        aWithBars.image, QRect(vTrack.left(), vPill.center().y(), vTrack.width(), 1), colors.outline, true, kFillTol
    );
    std::printf(
        "MEASURED scroll_bars_zero_travel hPill(%d,%d %dx%d) ink=%d thick=%d band=%d..%d (shared %d..%d)"
        "  vPill(%d,%d %dx%d) ink=%d thick=%d band=%d..%d (shared %d..%d)  (lanes %dx%d / %dx%d)\n",
        hPill.left(), hPill.top(), hPill.width(), hPill.height(), hPillInk, hThickness, hBand.first,
        hBand.second, hPill.top(), hPill.bottom(), vPill.left(), vPill.top(), vPill.width(), vPill.height(),
        vPillInk, vThickness, vBand.first, vBand.second, vPill.left(), vPill.right(), hTrack.width(),
        hTrack.height(), vTrack.width(), vTrack.height()
    );
    REQUIRE(hPillInk > 0, "the zero-travel horizontal lane painted no pill");
    REQUIRE(vPillInk > 0, "the zero-travel VERTICAL lane painted no pill: the bar hid itself");
    REQUIRE(
        hThickness == ctrl::graph::scrollBar::kPillThickness
            && vThickness == ctrl::graph::scrollBar::kPillThickness,
        "a zero-travel pill is not the app's 4px (%d h / %d v)", hThickness, vThickness
    );
    REQUIRE(
        hBand.first == hPill.top() && hBand.second == hPill.bottom(),
        "the zero-travel horizontal pill's band is rows %d..%d, not the shared rect's %d..%d", hBand.first,
        hBand.second, hPill.top(), hPill.bottom()
    );
    REQUIRE(
        vBand.first == vPill.left() && vBand.second == vPill.right(),
        "the zero-travel vertical pill's band is columns %d..%d, not the shared rect's %d..%d", vBand.first,
        vBand.second, vPill.left(), vPill.right()
    );
    // FULL LENGTH: the fill covers the lane but for the ROUNDED CAPS at the two ends
    // (radius 2, i.e. a semicircle across the 4px bar) and their antialiased fringe, so
    // the allowance is two cap widths per end - and a partial run is far below it.
    const int cap = ctrl::graph::scrollBar::kPillRadius;
    REQUIRE(
        hPillInk >= (hPill.width() - 4 * cap) * ctrl::graph::scrollBar::kPillThickness,
        "the horizontal zero-travel pill covers only %d px of a %d px lane", hPillInk, hPill.width()
    );
    REQUIRE(
        vPillInk >= (vPill.height() - 4 * cap) * ctrl::graph::scrollBar::kPillThickness,
        "the vertical zero-travel pill covers only %d px of a %d px lane", vPillInk, vPill.height()
    );

    // The trough is still invisible: outside the two pills the frame is the same as the
    // no-bars frame.
    int stray = 0;
    const QRect hReach = grow(hPill, 1);
    const QRect vReach = grow(vPill, 1);
    for (int y = 0; y < aWithBars.image.height(); ++y) {
        for (int x = 0; x < aWithBars.image.width(); ++x) {
            if (hReach.contains(x, y) || vReach.contains(x, y))
                continue;
            if (pixelAt(aWithBars.image, x, y) != pixelAt(aWithoutBars.image, x, y))
                ++stray;
        }
    }
    REQUIRE(stray == 0, "a zero-travel lane painted %d px outside its pills", stray);

    // HOVER STILL LIGHTS A FULL-LENGTH PILL - both lanes.
    const int hHot = toneCount(
        aHoveredH.image, QRect(hPill.center().x(), hTrack.top(), 1, hTrack.height()), colors.hover, kFillTol
    );
    const int vHot = toneCount(
        aHoveredV.image, QRect(vTrack.left(), vPill.center().y(), vTrack.width(), 1), colors.hover, kFillTol
    );
    const int restVWhileHHot = toneCount(aHoveredH.image, vPill, colors.outline);
    const int restHWhileVHot = toneCount(aHoveredV.image, hPill, colors.outline);
    std::printf(
        "MEASURED scroll_bars_zero_travel_hover h_hot_px=%d v_hot_px=%d rest_v_outline_px=%d"
        " rest_h_outline_px=%d\n",
        hHot, vHot, restVWhileHHot, restHWhileVHot
    );
    REQUIRE(
        hHot == ctrl::graph::scrollBar::kPillThickness,
        "a pointer on the full-length horizontal pill painted %d hover px", hHot
    );
    REQUIRE(
        vHot == ctrl::graph::scrollBar::kPillThickness,
        "a pointer on the full-length vertical pill painted %d hover px", vHot
    );
    REQUIRE(
        restVWhileHHot > 0 && restHWhileVHot > 0,
        "hovering one full-length pill changed the other lane's ink (%d / %d outline px left)",
        restVWhileHHot, restHWhileVHot
    );

    // ...and the host's hover identity answers for them too, so the repaint that shows
    // this hover is scheduled even though the lane has no travel.
    REQUIRE(
        gui::panelBarAt(aModel, vTrack.center()) == gui::PanelBar::Vertical
            && gui::panelBarAt(aModel, hTrack.center()) == gui::PanelBar::Horizontal,
        "a full-length lane is not part of the hover identity: its hover would never repaint"
    );
    return true;
}

// 31b: THE RULER'S HAIRLINE IS THE PALETTE TOKEN, OPAQUE, on both views. The timeline's
// rule is a 1px pen on the integer row under its band with antialiasing OFF
// (TimeLineEditorWidget::paintEvent sets the hint itself, so the editor's renderer cannot
// leave it split across the boundary), and the panel draws theme::Graph::rulerLine() at
// full strength. Both resolve to Colors::hairline over Colors::base - rgb 64 over 38 in
// the dark palette, where the stray-AA build measured the timeline at 51. This gate reads
// the panel's row over its magenta floor at the full token AND proves the old half blend
// is gone, so reintroducing a coverage factor (or the timeline's stray AA) fails here.
bool checkRulerHairline(const Rendered& aRendered, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme) {
    REQUIRE(!aModel.rulerBand.isEmpty(), "precondition: the model publishes no ruler band");
    const int hairlineY = aModel.rulerBand.bottom() + 1;
    REQUIRE(hairlineY < kViewH, "precondition: the hairline row is outside the framebuffer");
    // The row's ink over the gate's magenta floor: the token at full strength.
    const QColor expected = over(withAlpha(aTheme.rulerLine(), 1.0f), kClear);
    // The negative control: the same token at half coverage, the stray-AA blend this gate
    // used to expect. It is what a regression to a half-coverage factor would draw.
    const QColor halfCovered = over(withAlpha(aTheme.rulerLine(), 0.5f), kClear);
    // Two columns, one at each end of the band, clear of the playhead's own column (the
    // accent line crosses every row) - the rule spans the whole band, so both must read
    // the same ink.
    const int probeX[] = {aModel.rulerBand.left() + 2, aModel.rulerBand.right() - 2};
    for (int x : probeX) {
        REQUIRE(
            x >= 0 && x < kViewW && std::abs(x - aModel.playheadX) > 2,
            "precondition: hairline probe column %d is outside the frame or on the playhead", x
        );
        const QColor c = pixelAt(aRendered.image, x, hairlineY);
        std::printf(
            "MEASURED ruler_hairline row=%d x=%d rgb=%s expected_token=%s half_coverage=%s d_token=%d d_half=%d\n",
            hairlineY, x, hex(c).c_str(), hex(expected).c_str(), hex(halfCovered).c_str(),
            colourDistance(c, expected), colourDistance(c, halfCovered)
        );
        REQUIRE(
            colourDistance(c, expected) <= 6,
            "the ruler hairline at (%d,%d) is %s, not the opaque token %s the timeline draws (distance %d)",
            x, hairlineY, hex(c).c_str(), hex(expected).c_str(), colourDistance(c, expected)
        );
        REQUIRE(
            colourDistance(c, halfCovered) > 24,
            "the ruler hairline at (%d,%d) is %s: the panel reproduces the timeline's stray-AA half coverage"
            " again (%s at 0.5), so the two rules are 0.5/1 apart",
            x, hairlineY, hex(c).c_str(), hex(halfCovered).c_str()
        );
    }
    // ...and the row ABOVE it (the band's last row) is the BAND'S OWN SURFACE, not the
    // hairline: the timeline's band is repainted by its strip, so the panel's band must be
    // the same - a rule that bled into the band would be two grey rows where both views
    // have one. The comparison is against the surface token, not a raw distance from the
    // hairline: in the dark palette the surface (#171717) sits only 15 from the hairline
    // (#262626), so "far from the hairline" is not the property - "the band's ink" is.
    const QColor above = pixelAt(aRendered.image, probeX[0], aModel.rulerBand.bottom());
    const QColor surface = over(withAlpha(aTheme.rulerBand(), 1.0f), kClear);
    std::printf(
        "MEASURED ruler_hairline_above row=%d rgb=%s (surface %s d_surface=%d, hairline %s d_token=%d)\n",
        aModel.rulerBand.bottom(), hex(above).c_str(), hex(surface).c_str(), colourDistance(above, surface),
        hex(expected).c_str(), colourDistance(above, expected)
    );
    REQUIRE(
        colourDistance(above, surface) <= 6,
        "the band's last row (%d) is %s, not the band's surface %s: the hairline bled into the band, which"
        " the timeline's own widget repaints over",
        aModel.rulerBand.bottom(), hex(above).c_str(), hex(surface).c_str()
    );
    return true;
}

// 10: the Custom easing's tangent handles. The panel publishes each handle as a
// stem (base -> dot) and a dot, and the drawing layer must put ink at exactly
// those coordinates - and highlight the dot the pointer is on, in the same token
// and at the same distance the editor's hit-test grabs through.
//
// The pair is the whole rule: with the pointer OFF (the default pass, the pointer
// off-screen) every STORED dot is filled with theme::Graph::handleFill() and BOTH the
// stem and the dot's stroke are the CURVE's hue mixed 0.2 toward
// theme::Graph::halo() - the stem at 0.85, the stroke at 0.9 (mock-graph.js's alphas
// over a hue lowered in chroma so the handle reads on its own stroke); with the
// pointer ON one dot's centre (a second pass that differs ONLY in the pointer) that
// dot takes the hue-derived highlight mixedToward(handleInk, axisText, 0.45) while
// every other dot keeps its fill. Nothing else in the model moved between the two
// passes, so a change at a dot's centre is the highlight and nothing else.
bool checkHandles(
    const Rendered& aOff, const Rendered& aOn, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme,
    const QPoint& aPointerDot
) {
    int handles = 0;
    int highlighted = 0;
    for (const gui::PanelCurve& curve : aModel.curves) {
        for (const gui::PanelHandle& handle : curve.handles) {
            ++handles;
            const QPoint dot(int(handle.dot.x()), int(handle.dot.y()));
            REQUIRE(kPlot.contains(dot), "precondition: handle dot (%d,%d) is outside the plot", dot.x(), dot.y());
            REQUIRE(handle.radius == ctrl::graph::kHandleRadius, "a handle dot carries radius %d, not kHandleRadius",
                    handle.radius);

            // The stem: the pixel that CONTAINS the segment's midpoint carries the stem's
            // ink. That pixel is chosen by flooring the midpoint rather than by rounding
            // each coordinate, and the difference is the whole assertion: the midpoint of
            // the segment (base+dot)/2 has fractional parts, and rounding (192.5, 242.5) to
            // (193,243) picks a pixel whose centre (193.5,243.5) sits 1.37px PERPENDICULAR
            // to the line the stem is meant to draw, where a 1px stroke puts no core. Flooring
            // picks the pixel whose centre is at most half a pixel from the midpoint, which
            // is where a 1px stroke's own core must be.
            const QPoint mid(
                int(std::floor((handle.base.x() + handle.dot.x()) * 0.5)),
                int(std::floor((handle.base.y() + handle.dot.y()) * 0.5))
            );
            const QColor stem = pixelAt(aOff.image, mid.x(), mid.y());
            const QColor dotOff = pixelAt(aOff.image, dot.x(), dot.y());
            const QColor dotOn = pixelAt(aOn.image, dot.x(), dot.y());

            // THE STORED PAIR'S INK IS THE HANDLE HUE (the user's rule for both views;
            // mock-graph.js's stored stem at 0.85 and stroke at 0.9, over the hue the
            // drawing layer derives): the channel colour MIXED 0.2 toward
            // theme::Graph::halo() so the handle reads on top of its own curve. Read
            // here through the drawing layer's own alphas and composited on the clear
            // colour the frame is drawn over.
            constexpr float kStoredStem = 0.85f;
            constexpr float kHandleChroma = 0.2f;
            const QColor handleInk = mixedToward(curve.color, aTheme.halo(), kHandleChroma);
            const QColor stemInk = over(withAlpha(handleInk, kStoredStem), kClear);
            // The strongest STROKE pixel among the offsets a 1px circle centred on
            // kHandleRadius can land on (the circle is a many-segment path, so its chord
            // can sit inside the nominal radius).
            QPoint strokePixel(dot.x() + ctrl::graph::kHandleRadius, dot.y());
            QColor stroke = pixelAt(aOff.image, strokePixel.x(), strokePixel.y());
            for (int step = ctrl::graph::kHandleRadius - 1; step <= ctrl::graph::kHandleRadius + 1; ++step) {
                const QColor candidate = pixelAt(aOff.image, dot.x() + step, dot.y());
                if (clearDistance(candidate) > clearDistance(stroke)) {
                    stroke = candidate;
                    strokePixel = QPoint(dot.x() + step, dot.y());
                }
            }

            const int dotDistance = colourDistance(dotOff, aTheme.handleFill());
            // THE HOVER IS HUE-DERIVED, NOT THE ACCENT: the handle's reduced-chroma
            // hue lifted toward axisText by the panel's 0.45 brightened step.
            const QColor hoverInk = mixedToward(handleInk, aTheme.axisText(), 0.45f);
            const int highlightDistance = colourDistance(dotOn, hoverInk);
            const int moved = colourDistance(dotOff, dotOn);
            const bool pointed = dot == aPointerDot;
            const int stemToHue = colourDistance(stem, handleInk);
            const int stemToGrey = colourDistance(stem, aTheme.handleFill());
            const int strokeToHue = colourDistance(stroke, handleInk);
            const int strokeToGrey = colourDistance(stroke, aTheme.handleFill());

            std::printf(
                "MEASURED handle base=(%d,%d) dot=(%d,%d) radius=%d stem_mid=(%d,%d) stem_rgb=%s stem_clear=%d"
                " stem_to_hue=%d stem_to_handleFill=%d dot_rgb=%s handleFill=%s d=%d stroke=(%d,%d) stroke_rgb=%s"
                " stroke_to_hue=%d stroke_to_handleFill=%d dot_on_rgb=%s hover_ink=%s d=%d delta=%d pointed=%d\n",
                int(handle.base.x()), int(handle.base.y()), dot.x(), dot.y(), handle.radius, mid.x(), mid.y(),
                hex(stem).c_str(), clearDistance(stem), stemToHue, stemToGrey, hex(dotOff).c_str(),
                hex(aTheme.handleFill()).c_str(), dotDistance, strokePixel.x(), strokePixel.y(), hex(stroke).c_str(),
                strokeToHue, strokeToGrey, hex(dotOn).c_str(), hex(hoverInk).c_str(), highlightDistance,
                moved, int(pointed)
            );
            // The pixel carrying the midpoint is within 0.707px of the stem's centreline
            // (its centre is at most half a pixel away in each axis), and a 1px stroke's
            // coverage there is at least 1 - 0.707 = 0.293, so the recorded colour is the
            // stem's own colour to within that much of the clear colour - which is what
            // kCurveTol (the file's own "an antialiased stroke's fringe" tolerance) admits.
            REQUIRE(clearDistance(stem) > kInkMin, "the handle stem has no ink at its midpoint (%d,%d): rgb=%s",
                    mid.x(), mid.y(), hex(stem).c_str());
            REQUIRE(stemToHue <= kCurveTol,
                    "the handle stem's own midpoint (pixel %d,%d) is %s, %d from the handle's reduced-chroma colour"
                    " %s: the stem is not drawn where the model puts it",
                    mid.x(), mid.y(), hex(stem).c_str(), stemToHue, hex(handleInk).c_str());
            REQUIRE(colourDistance(stem, stemInk) <= kCurveTol,
                    "the stored handle's stem midpoint rgb=%s is %d from the hue at the drawing layer's own strength"
                    " (%.2f) rgb=%s",
                    hex(stem).c_str(), colourDistance(stem, stemInk), double(kStoredStem),
                    hex(stemInk).c_str());
            REQUIRE(stemToHue < stemToGrey,
                    "the STORED handle's stem at (%d,%d) rgb=%s is not nearer its handle hue rgb=%s than the"
                    " raised surface's grey rgb=%s (%d against %d): a stored stem is the curve's hue, reduced in"
                    " chroma",
                    mid.x(), mid.y(), hex(stem).c_str(), hex(handleInk).c_str(), hex(aTheme.handleFill()).c_str(),
                    stemToHue, stemToGrey);
            REQUIRE(strokeToHue < strokeToGrey,
                    "the STORED handle's stroke at (%d,%d) rgb=%s is not nearer its handle hue rgb=%s than the"
                    " raised surface's grey rgb=%s (%d against %d): a stored dot is ringed in the curve's hue",
                    strokePixel.x(), strokePixel.y(), hex(stroke).c_str(), hex(handleInk).c_str(),
                    hex(aTheme.handleFill()).c_str(), strokeToHue, strokeToGrey);
            REQUIRE(dotDistance <= kFillTol,
                    "the handle dot at (%d,%d) rgb=%s is not theme::Graph::handleFill() rgb=%s (distance %d > %d)",
                    dot.x(), dot.y(), hex(dotOff).c_str(), hex(aTheme.handleFill()).c_str(), dotDistance, kFillTol);

            if (pointed) {
                // The dot under the pointer is the highlighted one: it moved between
                // the passes and it now carries the hue-derived hover ink.
                REQUIRE(moved > 0, "the dot under the pointer (%d,%d) did not change between the two passes",
                        dot.x(), dot.y());
                REQUIRE(highlightDistance <= kFillTol,
                        "the dot under the pointer (%d,%d) rgb=%s is not the hue-derived highlight rgb=%s"
                        " (distance %d > %d): the handle hover must not use the structural accent",
                        dot.x(), dot.y(), hex(dotOn).c_str(), hex(hoverInk).c_str(), highlightDistance, kFillTol);
                ++highlighted;
            } else {
                // Every other dot is untouched: the two passes differ only in
                // the pointer, so a change here would be the highlight leaking.
                REQUIRE(moved == 0, "the dot at (%d,%d) is not under the pointer but changed (delta %d)",
                        dot.x(), dot.y(), moved);
            }
        }
    }
    REQUIRE(handles > 0, "the model published no tangent handles: the handle contract cannot be observed");
    REQUIRE(highlighted == 1, "expected exactly one highlighted handle dot, measured %d of %d", highlighted, handles);
    return true;
}

// 17: THE PLAYHEAD'S OWN EXTENT. The panel's playhead is the document's frame, drawn
// as one 1px column in theme::Graph::selected() from the RULER's pill column (under
// the pill) through the PANEL's last row: the timeline's playhead crosses its whole
// body, and this one crosses the graph's, so the structural accent passes the section
// boundary and the plot's last row. Its extent is measured rather than assumed: it
// must ink the band's rows from playheadLineTop and run to the panel's last row, be
// exactly one pixel wide inside the plot and put nothing outside its sections.
bool checkPlayhead(
    const Rendered& aRendered, const Rendered& aNoFrame, const gui::GraphPanelModel& aModel,
    const gui::GraphPanelModel& aNoFrameModel, const theme::Graph& aTheme
) {
    REQUIRE(aModel.playheadX >= 0, "precondition: the full model must publish a playhead");
    REQUIRE(
        !aModel.rulerBand.isEmpty() && !aModel.playheadPill.isEmpty() && aModel.playheadLineTop > aModel.rulerBand.top(),
        "precondition: the full model must publish the band, the pill and the line's top"
    );
    REQUIRE(aNoFrameModel.playheadX < 0 && aNoFrameModel.playheadPill.isEmpty(),
            "precondition: the invalid-frame model must publish no playhead");
    REQUIRE(kPlot.contains(aModel.playheadX, kPlot.top()), "precondition: the playhead column must be inside the plot");
    // THE SECTION SPAN: the line starts in the BAND's pill column (above the plot)
    // and crosses into the graph. The drawing layer inks those band rows outside the
    // plot clip, because the plot now starts below the ruler.
    REQUIRE(
        aModel.playheadLineTop > aModel.rulerBand.top() && aModel.playheadLineTop < aModel.plot.top(),
        "precondition: the playhead's line (%d) is not inside the ruler's pill column above the plot (%d..%d)",
        aModel.playheadLineTop, aModel.rulerBand.top(), aModel.plot.top()
    );

    // THE LINE: every row from the model's playheadLineTop to the PANEL's bottom, on
    // the playhead's column, carries theme::Graph::selected() (except rows the scroll
    // bars paint over, which this model does not publish - the full model has none).
    // The rows above it are the pill's (and the ruler band's) - the line starts under
    // the pill, as TimeCursor's does.
    int missingRows = 0;
    int firstRow = -1;
    int lastRow = -1;
    for (int y = aModel.playheadLineTop; y <= kPanel.bottom(); ++y) {
        const QColor c = pixelAt(aRendered.image, aModel.playheadX, y);
        if (colourDistance(c, aTheme.selected()) > kFillTol) {
            ++missingRows;
            if (firstRow < 0) {
                firstRow = y;
            }
            lastRow = y;
        }
    }
    const int widthProbeRow = kPlot.top() + 30; // clear of the band and of every curve
    const QColor left = pixelAt(aRendered.image, aModel.playheadX - 1, widthProbeRow);
    const QColor right = pixelAt(aRendered.image, aModel.playheadX + 1, widthProbeRow);
    std::printf(
        "MEASURED playhead x=%d line=%d..%d rows_without_playhead_colour=%d first=%d last=%d"
        " probe_row=%d left_clear_distance=%d right_clear_distance=%d\n",
        aModel.playheadX, aModel.playheadLineTop, kPanel.bottom(), missingRows, firstRow, lastRow, widthProbeRow,
        clearDistance(left), clearDistance(right)
    );
    REQUIRE(
        missingRows == 0,
        "the playhead does not cover the panel's rows from its own top (y=%d) to its last (y=%d): %d rows lack it"
        " (first %d, last %d)",
        aModel.playheadLineTop, kPanel.bottom(), missingRows, firstRow, lastRow
    );
    REQUIRE(
        clearDistance(left) <= kInkMin && clearDistance(right) <= kInkMin,
        "the playhead is wider than one pixel: the neighbouring columns at row %d carry ink (%d and %d from the"
        " clear colour)",
        widthProbeRow, clearDistance(left), clearDistance(right)
    );

    // THE PILL: the frame's number rides the band's number row in the rounded accent
    // box the model published - and NOTHING is painted in that column when the frame
    // is invalid. The reading is per-pixel against the band's own surface, so the band
    // itself (the same in both passes) is not mistaken for the pill.
    const QRect pill = aModel.playheadPill;
    int pillInk = 0;
    int invalidInk = 0;
    for (int y = aModel.rulerBand.top(); y <= aModel.rulerBand.bottom(); ++y) {
        if (colourDistance(pixelAt(aRendered.image, aModel.playheadX, y), aTheme.rulerBand()) > kInkMin)
            ++pillInk;
        if (colourDistance(pixelAt(aNoFrame.image, aModel.playheadX, y), aTheme.rulerBand()) > kInkMin)
            ++invalidInk;
    }
    const QColor pillCorner = pixelAt(aRendered.image, pill.left() + 1, pill.top() + pill.height() / 2);
    std::printf(
        "MEASURED playhead_pill rect=(%d,%d %dx%d) column_ink_in_band=%d corner_rgb=%s selected=%s d=%d"
        " invalid_frame_column_ink_in_band=%d\n",
        pill.left(), pill.top(), pill.width(), pill.height(), pillInk, hex(pillCorner).c_str(),
        hex(aTheme.selected()).c_str(), colourDistance(pillCorner, aTheme.selected()), invalidInk
    );
    REQUIRE(
        aModel.rulerBand.contains(pill) && pill.bottom() < aModel.playheadLineTop,
        "the pill (%d,%d %dx%d) is not in the band's number row above the playhead's line (%d..%d)",
        pill.left(), pill.top(), pill.width(), pill.height(), aModel.playheadLineTop, kPlot.bottom()
    );
    REQUIRE(
        pillInk > 0,
        "the playhead's pill painted nothing in the band at the playhead's column (%d): a current frame must be"
        " marked in the ruler",
        aModel.playheadX
    );
    REQUIRE(
        colourDistance(pillCorner, aTheme.selected()) <= kFillTol,
        "the playhead's pill corner rgb=%s is not the accent fill rgb=%s (distance %d > %d)",
        hex(pillCorner).c_str(), hex(aTheme.selected()).c_str(), colourDistance(pillCorner, aTheme.selected()),
        kFillTol
    );
    // THE PILL'S NUMBER is the token the timeline puts ON an accent fill:
    // theme::Graph::playheadText() == theme::Colors::accentBrightText, the cell
    // gui::TimeCursor is handed (TimeLineEditorWidget.cpp). It is NOT the ruler's own
    // text ink (Colors::text): a number on the violet fill and a number on the base
    // surface are different tokens, and the wrong one measured as two near-white pixels
    // in the shipped build - an invisible number on a pill whose dark one reads.
    const theme::Colors& palette = theme::Colors::current();
    int pillTextInk = 0;
    int tokenInk = 0;
    for (int y = pill.top(); y <= pill.bottom(); ++y) {
        for (int x = pill.left(); x <= pill.right(); ++x) {
            const QColor c = pixelAt(aRendered.image, x, y);
            if (colourDistance(c, aTheme.playheadText()) < colourDistance(c, aTheme.selected())
                && colourDistance(c, aTheme.playheadText()) <= 48) {
                ++pillTextInk;
            }
            if (colourDistance(c, palette.accentBrightText) <= 24)
                ++tokenInk;
        }
    }
    std::printf(
        "MEASURED playhead_pill_text rect=(%d,%d %dx%d) text_rgb=%s px=%d accent_bright_text=%s token_px=%d"
        " ruler_text=%s\n",
        pill.left(), pill.top(), pill.width(), pill.height(), hex(aTheme.playheadText()).c_str(), pillTextInk,
        hex(palette.accentBrightText).c_str(), tokenInk, hex(palette.text).c_str()
    );
    REQUIRE(pillTextInk > 0, "the playhead's pill painted no number in theme::Graph::playheadText()");
    // THE INK IS THE PILL'S OWN TEXT TOKEN, pinned BY VALUE and not only through the
    // accessor: the gate would pass just as happily if the theme handed back some other
    // ink, so the token itself is compared - and the two tokens are proved different
    // first, or the readings below say nothing.
    REQUIRE(
        colourDistance(palette.accentBrightText, palette.text) > 24,
        "precondition: accentBrightText (%s) and text (%s) are the same ink, so the pill's number cannot be told"
        " from the ruler's",
        hex(palette.accentBrightText).c_str(), hex(palette.text).c_str()
    );
    REQUIRE(
        colourDistance(aTheme.playheadText(), palette.accentBrightText) == 0,
        "the pill's number is drawn in %s, not the accent fill's text token %s",
        hex(aTheme.playheadText()).c_str(), hex(palette.accentBrightText).c_str()
    );
    REQUIRE(
        tokenInk > 0,
        "the pill painted no pixel of theme::Colors::accentBrightText (%s) inside its box (%d,%d %dx%d)",
        hex(palette.accentBrightText).c_str(), pill.left(), pill.top(), pill.width(), pill.height()
    );
    REQUIRE(
        invalidInk == 0,
        "an INVALID frame painted %d pixels in the band at the playhead's column (%d): there must be no playhead"
        " ink when currentFrame() < 0",
        invalidInk, aModel.playheadX
    );
    return true;
}

// 17b: THE CHIPS ARE DRAWN BEFORE THE PLAYHEAD. The model's chip box is inside the
// PLOT, so it can never reach the band's pill row; the line, however, crosses the
// plot's rows, and a chip could sit on its column. The drawing order is therefore
// part of the contract: chips first, the playhead after them. This pass moves the
// selected curve's chip ONTO the playhead's column and reads the column through the
// chip's rows - every one must be the accent, and the chip's own backing must still
// be there beside it (so the reading is not "nothing was drawn").
bool checkChipUnderPlayhead(
    const Rendered& aRendered, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme
) {
    REQUIRE(aModel.playheadX >= 0, "precondition: the fixture must publish a playhead");
    REQUIRE(!aModel.curves.isEmpty(), "precondition: the fixture must carry a curve");
    const QRect& chip = aModel.curves.at(0).chip;
    REQUIRE(!chip.isEmpty() && kPlot.contains(chip), "precondition: the fixture's chip is not inside the plot");
    REQUIRE(
        chip.left() <= aModel.playheadX && aModel.playheadX <= chip.right(),
        "precondition: the fixture's chip (%d..%d) does not cover the playhead column %d",
        chip.left(), chip.right(), aModel.playheadX
    );

    int missing = 0;
    for (int y = chip.top(); y <= chip.bottom(); ++y) {
        const QColor c = pixelAt(aRendered.image, aModel.playheadX, y);
        if (colourDistance(c, aTheme.selected()) > kFillTol) {
            ++missing;
        }
    }
    // The chip's own backing, two columns clear of the line (the box is opaque at its
    // interior, so this is the chip and not the artwork).
    const int besideX = chip.left() + 2;
    const QColor beside = pixelAt(aRendered.image, besideX, chip.center().y());
    std::printf(
        "MEASURED chip_under_playhead chip=(%d,%d %dx%d) playhead_x=%d rows_without_accent=%d"
        " beside=(%d,%d) rgb=%s clear=%d\n",
        chip.left(), chip.top(), chip.width(), chip.height(), aModel.playheadX, missing, besideX, chip.center().y(),
        hex(beside).c_str(), clearDistance(beside)
    );
    REQUIRE(
        missing == 0,
        "the playhead's column lost the accent on %d of the chip's rows (%d..%d): the chip was drawn over the"
        " playhead",
        missing, chip.top(), chip.bottom()
    );
    REQUIRE(
        clearDistance(beside) > kInkMin,
        "the chip painted nothing beside the playhead (rgb=%s at %d,%d): the pass is not reading a drawn chip",
        hex(beside).c_str(), besideX, chip.center().y()
    );
    return true;
}

// 18: THE BOX-SELECT MARQUEE, as a pair of passes: the SAME model with and without
// gui::GraphPanelModel::marquee (the only difference), so the ink below can only be
// the marquee. The rectangle is chosen to hold none of the model's other geometry
// (asserted, grown by the same 2px every other bound uses), which is what makes the
// interior sample a statement about the marquee's own body.
//
// Both marquee tokens carry alpha (theme::Graph::marqueeFill/Edge are accentBright at
// 24 and 230, the timeline's own box-select colours), so the assertions are against
// the source-over composite `over(...)` and not against the tokens.
bool checkMarquee(
    const Rendered& aPlain, const Rendered& aWithMarquee, const gui::GraphPanelModel& aModel,
    const theme::Graph& aTheme, const QRect& aMarquee
) {
    REQUIRE(aModel.marquee == aMarquee, "precondition: the model must publish the marquee rectangle");
    REQUIRE(kPlot.contains(aMarquee), "precondition: the marquee rectangle must be inside the plot");

    const std::vector<Geom> geoms = geometryOf(aModel, aPlain.font);
    for (const Geom& geom : geoms) {
        REQUIRE(
            !grow(aMarquee, 2).intersects(geom.rect),
            "precondition: the marquee probe rectangle (%d,%d %dx%d) reaches %s (%d,%d %dx%d)", aMarquee.left(),
            aMarquee.top(), aMarquee.width(), aMarquee.height(), geom.name.c_str(), geom.rect.left(),
            geom.rect.top(), geom.rect.width(), geom.rect.height()
        );
    }

    // The pair: with no drag live, the rectangle holds nothing at all.
    const int plainInk = inkCount(aPlain.image, grow(aMarquee, 2));
    const int marqueeInk = inkCount(aWithMarquee.image, grow(aMarquee, 2));
    std::printf(
        "MEASURED marquee rect=(%d,%d %dx%d) ink_without=%d ink_with=%d of %d\n", aMarquee.left(),
        aMarquee.top(), aMarquee.width(), aMarquee.height(), plainInk, marqueeInk,
        grow(aMarquee, 2).width() * grow(aMarquee, 2).height()
    );
    REQUIRE(plainInk == 0, "the rectangle a marquee would occupy carries %d inked pixels with no marquee live",
            plainInk);
    REQUIRE(marqueeInk > 0, "the marquee painted nothing in its own rectangle");

    // The body: a fully covered interior pixel is the fill composited on the clear
    // colour.
    const QColor body = over(aTheme.marqueeFill(), kClear);
    const QColor bodyPixel = pixelAt(aWithMarquee.image, aMarquee.center().x(), aMarquee.center().y());
    const int bodyDistance = colourDistance(bodyPixel, body);
    std::printf(
        "MEASURED marquee_body at=(%d,%d) rgb=%s fill=%s over_clear=%s d=%d tolerance=%d\n",
        aMarquee.center().x(), aMarquee.center().y(), hex(bodyPixel).c_str(), hex(aTheme.marqueeFill()).c_str(),
        hex(body).c_str(), bodyDistance, kFillTol
    );
    REQUIRE(
        bodyDistance <= kFillTol,
        "the marquee's body rgb=%s is not its own fill rgb=%s composited on the clear colour rgb=%s"
        " (distance %d > %d)",
        hex(bodyPixel).c_str(), hex(aTheme.marqueeFill()).c_str(), hex(body).c_str(), bodyDistance, kFillTol
    );

    // The edge: a 1px antialiased stroke centres a ramp on the rectangle's own edge,
    // so the best match is searched for within 2px of it - the same "sample the band,
    // state the tolerance" rule the curve and zero-rule assertions use.
    const QColor edge = over(aTheme.marqueeEdge(), kClear);
    int best = 1 << 20;
    int bestY = -1;
    for (int y = aMarquee.top() - 2; y <= aMarquee.top() + 2; ++y) {
        const QColor c = pixelAt(aWithMarquee.image, aMarquee.center().x(), y);
        const int d = colourDistance(c, edge);
        if (d < best) {
            best = d;
            bestY = y;
        }
    }
    std::printf(
        "MEASURED marquee_edge x=%d edge_y=%d best_y=%d rgb=%s edge=%s d=%d tolerance=%d\n",
        aMarquee.center().x(), aMarquee.top(), bestY, hex(pixelAt(aWithMarquee.image, aMarquee.center().x(), bestY)).c_str(),
        hex(edge).c_str(), best, kCurveTol
    );
    REQUIRE(
        best <= kCurveTol,
        "the marquee's top edge is not its own edge colour rgb=%s within 2px (nearest is %d away)",
        hex(edge).c_str(), best
    );
    return true;
}

// 21: THE VALUE COLUMN'S TWO OBLIGATIONS - the gutter, and the panel guard.
//
// THE GUTTER. The value column and the plot's first frame number meet at the panel's
// top-left corner: the model's value box ends ctrl::graph::Scaler::kAxisLabelPad (4px)
// left of the plot and the frame axis nudges its first number just inside it, so the
// two numbers landed 7px apart on the same rows and read as the single string "10"
// (measured in build/shots/outline_designreview.png: the value label '1' at x 208..215,
// the frame label '0' at x 223..231). drawPanel now pulls a right-aligned value label
// gui::kValueLabelGap (8px) left of its box's right edge - the timeline's own
// kNumberMargin for the same defect - and this measures it on the drawn pass: the glyph
// box the drawing layer actually used sits that far left of where right-alignment alone
// would put it, the ink is inside that box, and the label that FILLS its box is left
// exactly where the model's measured column put it (the pull is bounded by the box's
// spare width, so it can never push a number out of the column).
//
// THE GUARD. The value labels are the one pass drawn outside every clip (they live in
// the column left of the plot), so a label WIDER than its box - which the model's
// measured column cannot produce, but a drawing font wider than the model's can - would
// paint outside the panel. drawPanel clips that pass to the panel, and the second model
// here is that case: a panel that does not start at the framebuffer's left edge, with a
// value label whose text is four times its box's width. The band left of the panel must
// come back clean while the label is demonstrably painted inside it.
bool checkValueLabels(
    const Rendered& aRendered, const gui::GraphPanelModel& aModel, const Rendered& aGuarded,
    const gui::GraphPanelModel& aGuardedModel
) {
    // The gate's own font is the application font unless a pass overrode it; the canvas backend
    // has already been handed it, so the glyph arithmetic below and the glyphs share one face.
    REQUIRE(aModel.valueLabels.size() == 2, "precondition: the full model must carry two value labels");

    // ---- the gutter, label by label ----
    int checked = 0;
    int pulled = 0;
    const QFontMetricsF labelMetrics(aRendered.font);
    for (const gui::PanelLabel& label : aModel.valueLabels) {
        const double advance = labelMetrics.horizontalAdvance(label.text);
        const double flushRight = double(label.rect.left() + label.rect.width()) - advance;
        const QRect glyph = glyphBox(
            label.rect, label.text, aRendered.font, Align::Right, VAlign::Centre,
            gui::kValueLabelGap
        );
        const int shift = int(std::lround(flushRight)) - glyph.left();
        const double spare = double(label.rect.width()) - advance;
        const int expected = int(std::min(double(gui::kValueLabelGap), spare));
        std::printf(
            "MEASURED value_label text=%s box=(%d,%d %dx%d) text_width=%d spare=%d flush_right_x=%d glyph_x=%d"
            " shift_left=%d expected=%d gap=%d\n",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(),
            int(std::lround(advance)), int(std::lround(spare)), int(std::lround(flushRight)), glyph.left(), shift,
            expected, gui::kValueLabelGap
        );
        REQUIRE(
            spare > double(gui::kValueLabelGap),
            "precondition: the label '%s' fills its box (spare %dpx), so the gutter could not be taken from it",
            qPrintable(label.text), int(std::lround(spare))
        );
        REQUIRE(
            shift == expected,
            "the value label '%s' moved %dpx left of right-alignment, not the %dpx the box could spare",
            qPrintable(label.text), shift, expected
        );
        REQUIRE(
            glyph.left() >= label.rect.left(),
            "the value label '%s' drew its glyph box at x=%d, left of its own box (x=%d): the gutter pushed it out",
            qPrintable(label.text), glyph.left(), label.rect.left()
        );
        REQUIRE(
            inkCount(aRendered.image, grow(glyph, 1)) > 0,
            "the value label '%s' painted nothing where its own glyph box is (%d,%d %dx%d)", qPrintable(label.text),
            glyph.left(), glyph.top(), glyph.width(), glyph.height()
        );
        ++checked;
        if (shift == gui::kValueLabelGap) {
            ++pulled;
        }
    }
    REQUIRE(checked == aModel.valueLabels.size(), "walked %d of %d value labels", checked, int(aModel.valueLabels.size()));
    REQUIRE(pulled == checked, "only %d of %d value labels took the full gutter", pulled, checked);

    // ---- and the frame label it was crowding is still where it was ----
    //
    // The two numbers are on the same rows, so the gutter is only worth anything if it
    // survives as a GAP between the two glyph runs. Measured, not assumed: the topmost
    // value label's glyph box and the nearest frame label's glyph box, in columns.
    REQUIRE(!aModel.frameLabels.isEmpty(), "precondition: the full model must carry a frame label");
    const gui::PanelLabel& frame = aModel.frameLabels.at(0);
    const QRect frameGlyph =
        glyphBox(frame.rect, frame.text, aRendered.font, Align::Left, VAlign::Top);
    int bestGap = -1;
    QString bestLabel;
    for (const gui::PanelLabel& label : aModel.valueLabels) {
        const QRect glyph = glyphBox(
            label.rect, label.text, aRendered.font, Align::Right, VAlign::Centre,
            gui::kValueLabelGap
        );
        const int gap = rectGap(glyph, frameGlyph);
        if (bestGap < 0 || gap < bestGap) {
            bestGap = gap;
            bestLabel = label.text;
        }
    }
    std::printf(
        "MEASURED value_label_gutter nearest_value_label=%s frame_label=%s value_glyph_right=%d frame_glyph_left=%d"
        " gap_px=%d gutter=%d\n",
        qPrintable(bestLabel), qPrintable(frame.text),
        glyphBox(aModel.valueLabels.at(0).rect, aModel.valueLabels.at(0).text, aRendered.font,
                 Align::Right, VAlign::Centre, gui::kValueLabelGap)
            .right(),
        frameGlyph.left(), bestGap, gui::kValueLabelGap
    );
    REQUIRE(
        bestGap >= gui::kValueLabelGap,
        "the closest value label is %dpx from the frame label '%s', less than the %dpx gutter: the two numbers"
        " read as one string",
        bestGap, qPrintable(frame.text), gui::kValueLabelGap
    );

    // ---- the guard: a label wider than its box cannot leave the panel ----
    REQUIRE(!aGuardedModel.panel.isEmpty(), "precondition: the guarded model must have a panel");
    REQUIRE(aGuardedModel.panel.left() > 0, "precondition: the guarded model's panel must start inside the framebuffer");
    REQUIRE(aGuardedModel.valueLabels.size() == 1, "precondition: the guarded model must carry one value label");
    const gui::PanelLabel& overflowing = aGuardedModel.valueLabels.at(0);
    const double guardedAdvance = QFontMetricsF(aGuarded.font).horizontalAdvance(overflowing.text);
    REQUIRE(
        guardedAdvance > double(overflowing.rect.width()),
        "precondition: the guarded model's label (%dpx of text in a %dpx box) is not wider than its box",
        int(std::lround(guardedAdvance)), overflowing.rect.width()
    );

    // Where right-alignment alone would have put it, and where it may paint: the text
    // starts LEFT of the panel's own edge without the clip.
    const QRect flush =
        glyphBox(overflowing.rect, overflowing.text, aGuarded.font, Align::Right, VAlign::Centre);
    const QRect guarded = glyphBox(
        overflowing.rect, overflowing.text, aGuarded.font, Align::Right, VAlign::Centre,
        gui::kValueLabelGap
    );
    const QRect outsidePanel(0, 0, aGuardedModel.panel.left(), kViewH);
    const int outsideInk = inkCount(aGuarded.image, outsidePanel);
    const int insideInk = inkCount(aGuarded.image, aGuardedModel.panel);
    std::printf(
        "MEASURED value_label_guard panel_left=%d text=%s text_width=%d box=(%d,%d %dx%d) flush_x=%d glyph_x=%d"
        " outside_panel_ink=%d inside_panel_ink=%d\n",
        aGuardedModel.panel.left(), qPrintable(overflowing.text), int(std::lround(guardedAdvance)),
        overflowing.rect.left(), overflowing.rect.top(), overflowing.rect.width(), overflowing.rect.height(),
        flush.left(), guarded.left(), outsideInk, insideInk
    );
    REQUIRE(
        flush.left() < aGuardedModel.panel.left(),
        "precondition: right-alignment alone would keep the label inside the panel (x=%d against %d), so the clip is"
        " not being tested",
        flush.left(), aGuardedModel.panel.left()
    );
    REQUIRE(
        outsideInk == 0,
        "a value label wider than its box painted %d pixels LEFT of the panel (x 0..%d): the value-label pass is not"
        " clipped to the panel",
        outsideInk, aGuardedModel.panel.left() - 1
    );
    REQUIRE(
        insideInk > 0, "the guarded model painted nothing at all inside its panel: the guard cannot be told from a"
                       " pass that drew nothing"
    );
    return true;
}

// 20: THE HANDLES ARE DRAWN FOR THE FOCUSED CHANNEL, and nothing else is. The
// tangent handles are the panel's vertex budget (the application-level performance
// diagnosis measured ~68 vertices per key with them against ~16 without, at ~50ns of
// Debug CPU per vertex), so drawPanel draws them only for the channel the user is
// working on - and for every channel while no focus exists (so nothing is hidden
// before the user has chosen).
//
// The pair here is the SAME model with and without a focus on the curve that HAS no
// handles (the gate's Scale curve): the only difference between the two passes is the
// focus, so the ink that leaves can only be the Focus-handle rule. The gate's model
// carries its handles on the Move curve, whose channel the focus does not name.
bool checkHandleFocus(
    const Rendered& aUnfocused, const Rendered& aFocusedOther, const gui::GraphPanelModel& aModel
) {
    REQUIRE(aModel.curves.size() == 2, "precondition: the focus probe needs the two curves of the full model");
    const gui::PanelCurve& withHandles = aModel.curves.at(0);
    const gui::PanelCurve& withoutHandles = aModel.curves.at(1);
    REQUIRE(!withHandles.handles.isEmpty(), "precondition: the first curve must carry the handles");
    REQUIRE(withoutHandles.handles.isEmpty(), "precondition: the focused curve must carry no handle of its own");

    // THE INK IS THE MEASUREMENT. drawPanel draws the tangent handles only for the focused
    // channel (every channel while no focus exists), so the unfocused curve's handle ink must be
    // GONE in the focused pass - while the unfocused pass proves the probes can see it at all.
    int positions = 0;
    int gone = 0;
    for (const gui::PanelHandle& handle : withHandles.handles) {
        const QPoint dot(int(handle.dot.x()), int(handle.dot.y()));
        const QPoint mid(
            int(std::floor((handle.base.x() + handle.dot.x()) * 0.5)),
            int(std::floor((handle.base.y() + handle.dot.y()) * 0.5))
        );
        for (const QPoint& p : {dot, mid}) {
            REQUIRE(
                kPlot.contains(p), "precondition: handle geometry at (%d,%d) is outside the plot", p.x(), p.y()
            );
            const int inkBefore = clearDistance(pixelAt(aUnfocused.image, p.x(), p.y()));
            const int inkAfter = clearDistance(pixelAt(aFocusedOther.image, p.x(), p.y()));
            std::printf(
                "MEASURED handle_focus_gone at=(%d,%d) ink_unfocused=%d ink_focused_other=%d\n", p.x(), p.y(),
                inkBefore, inkAfter
            );
            REQUIRE(inkBefore > kInkMin, "control: nothing is drawn at (%d,%d) even with every handle drawn",
                    p.x(), p.y());
            REQUIRE(inkAfter <= kInkMin, "the unfocused curve's handle still paints at (%d,%d): rgb=%s",
                    p.x(), p.y(), hex(pixelAt(aFocusedOther.image, p.x(), p.y())).c_str());
            ++positions;
            ++gone;
        }
    }
    std::printf("MEASURED handle_focus_gone positions=%d gone=%d\n", positions, gone);
    REQUIRE(positions == 4, "expected 2 handles x 2 positions, walked %d", positions);
    return true;
}

// 9d: the GHOST handle (PanelHandle::ghost). A Custom segment's stored pair is in the
// document and is drawn as a FILLED dot of theme::Graph::handleFill() at radius 5; an
// Auto segment's pair is DERIVED - a drag on it converts the key - so the drawing layer
// draws it as a SOLID dot of the handle hue (the channel's colour mixed 0.2 toward
// theme::Graph::halo()) at radius 3.5, filled at its 0.7 edge alpha, with the stem at
// 0.5 and the ring in that same hue. The model's own ghost model carries one of each,
// nothing else in the frame, so the two dots' pixels are the two drawings and the pair
// is the assertion: the stored dot's centre IS the raised fill token, the ghost dot's
// centre IS its own hue at 0.7 (solid, not hollow and not the stored surface), and the
// ghost's ring pixel is nearer the handle hue than the raised surface - which is the
// difference the flag exists for.
bool checkGhostHandle(const Rendered& aRendered, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme) {
    REQUIRE(aModel.curves.size() == 1, "precondition: the ghost model must carry one curve");
    const gui::PanelCurve& curve = aModel.curves.at(0);
    REQUIRE(curve.handles.size() == 2, "precondition: the ghost model must carry two handles, has %d",
            int(curve.handles.size()));

    const gui::PanelHandle* stored = nullptr;
    const gui::PanelHandle* ghost = nullptr;
    for (const gui::PanelHandle& handle : curve.handles) {
        if (handle.ghost) {
            ghost = &handle;
        } else {
            stored = &handle;
        }
    }
    REQUIRE(stored != nullptr && ghost != nullptr, "precondition: the model must carry one stored and one ghost pair");

    const QPoint storedCentre(int(stored->dot.x()), int(stored->dot.y()));
    const QPoint ghostCentre(int(ghost->dot.x()), int(ghost->dot.y()));
    REQUIRE(storedCentre != ghostCentre, "precondition: the two dots must be distinct points");
    for (const QPoint& p : {storedCentre, ghostCentre}) {
        REQUIRE(kPlot.contains(p), "precondition: a handle dot (%d,%d) is outside the plot", p.x(), p.y());
    }

    const QColor storedPixel = pixelAt(aRendered.image, storedCentre.x(), storedCentre.y());
    const QColor ghostPixel = pixelAt(aRendered.image, ghostCentre.x(), ghostCentre.y());
    // Three pixels out along the dot's own radius: on the 1px ring each drawing strokes
    // (kHandleRadius at 5; the ghost's is inset by kGhostHandleShrink, 1.5px, so its
    // radius is 3.5 and the stroke's 1px band is 2.5..3.5 about the centre).
    const QPoint ghostRing(ghostCentre.x() + 3, ghostCentre.y());
    const QColor ghostRingPixel = pixelAt(aRendered.image, ghostRing.x(), ghostRing.y());
    const QColor fill = aTheme.handleFill();
    // The ghost's hue is the shared handle ink: the channel colour mixed 0.2 toward the
    // panel's halo, filled at the ghost's own 0.7 edge alpha over the clear frame.
    const QColor handleInk = mixedToward(curve.color, aTheme.halo(), 0.2f);
    const QColor ghostFill = over(withAlpha(handleInk, 0.7f), kClear);
    const int storedToFill = colourDistance(storedPixel, fill);
    const int ghostToFill = colourDistance(ghostPixel, fill);
    const int ghostToOwn = colourDistance(ghostPixel, ghostFill);
    const int ringToColour = colourDistance(ghostRingPixel, handleInk);
    const int ringToEdge = colourDistance(ghostRingPixel, aTheme.handleFill());

    std::printf(
        "MEASURED ghost_handle stored_dot=(%d,%d) rgb=%s handleFill=%s d=%d | ghost_dot=(%d,%d) rgb=%s d_own=%d"
        " d_handleFill=%d | ghost_ring=(%d,%d) rgb=%s handle_hue=%s d=%d handleFill=%s d=%d | radius=%d/%d\n",
        storedCentre.x(), storedCentre.y(), hex(storedPixel).c_str(), hex(fill).c_str(), storedToFill,
        ghostCentre.x(), ghostCentre.y(), hex(ghostPixel).c_str(), ghostToOwn, ghostToFill, ghostRing.x(),
        ghostRing.y(), hex(ghostRingPixel).c_str(), hex(handleInk).c_str(), ringToColour,
        hex(aTheme.handleFill()).c_str(), ringToEdge, stored->radius, ghost->radius
    );
    REQUIRE(
        storedToFill <= kFillTol,
        "the STORED handle's dot rgb=%s is not theme::Graph::handleFill() rgb=%s (distance %d > %d): a stored pair"
        " is a filled dot",
        hex(storedPixel).c_str(), hex(fill).c_str(), storedToFill, kFillTol
    );
    REQUIRE(
        ghostToOwn <= kFillTol,
        "the GHOST handle's dot rgb=%s is not its own hue at the ghost's 0.7 alpha (expected %s, distance %d >"
        " %d): a derived pair is a SOLID dot of the channel's reduced-chroma hue",
        hex(ghostPixel).c_str(), hex(ghostFill).c_str(), ghostToOwn, kFillTol
    );
    REQUIRE(
        ghostToFill > kFillTol,
        "the GHOST handle's dot rgb=%s IS theme::Graph::handleFill() rgb=%s (distance %d): a derived pair must"
        " never be taken for the stored tangent's raised surface",
        hex(ghostPixel).c_str(), hex(fill).c_str(), ghostToFill
    );
    REQUIRE(
        ringToColour < ringToEdge,
        "the ghost handle's ring rgb=%s is not nearer its handle hue rgb=%s than the raised surface's grey"
        " rgb=%s (%d against %d): a ghost pair is ringed in the curve's reduced-chroma hue",
        hex(ghostRingPixel).c_str(), hex(handleInk).c_str(), hex(aTheme.handleFill()).c_str(), ringToColour,
        ringToEdge
    );
    return true;
}

// ---- THE HOVER NAMES EXACTLY WHAT A PRESS SELECTS ---------------------------------
//
// The drawing layer's hover ink (gui::drawPanel) and the host's hover-repaint rule
// (gui::GraphPanelWidget::updateHoverRepaint) both ask gui::panelPickAt, and that
// query settles key-versus-handle with ctrl::graph::pickTarget - the same function
// ctrl::GraphEditor::hitTest presses with. This block drives BOTH real paths over one
// geometry: a real editor over a real TimeLine for the press, and the model that
// editor builds for the hover. The geometry is tests/graph_drag.cpp's section 11b:
// frame 0 at pixel 0, 1 px/frame, the first key's centre at x=100 and the STORED
// outgoing handle 11px to its right on the same row, so the two grab rings overlap
// and the probes are
//
//   +3px from the key: key 3/10 = 0.30, handle 8/12 = 0.667 -> KEY
//   +8px from the key: key 8/10 = 0.80, handle 3/12 = 0.25  -> HANDLE
//   +5px from the key: key 5/10 = 0.50, handle 6/12 = 0.50  -> KEY (exact tie)
//
// and the hovered element must equal the pressed element at each one.

class HoverAnimator: public core::Animator {
    core::Frame mFrame;

public:
    core::Frame currentFrame() const override { return mFrame; }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

// A layer that owns a real TimeLine and nothing else (ObjectNode::timeLine() is
// virtual and null by default), exactly the fixture graph_drag's hit-test sections use.
class HoverNode: public core::ObjectNode {
    core::TimeLine mTimeLine;

public:
    core::ObjectType type() const override { return core::ObjectType_Layer; }
    void setName(const QString&) override {}
    const QString& name() const override {
        static QString s;
        return s;
    }
    float initialDepth() const override { return 0; }
    void setVisibility(bool) override {}
    bool isVisible() const override { return true; }
    void setSlimDown(bool) override {}
    bool isSlimmedDown() const override { return false; }
    bool canHoldChild() const override { return false; }
    void setInitialRect(const QRect&) override {}
    QRect initialRect() const override { return QRect(); }
    bool serialize(core::Serializer&) const override { return true; }
    bool deserialize(core::Deserializer&) override { return true; }
    core::TimeLine* timeLine() override { return &mTimeLine; }
    const core::TimeLine* timeLine() const override { return &mTimeLine; }
};

class HoverLogger: public ctrl::UILogger {
public:
    void pushLog(const QString&, ctrl::UILogType) override {}
};

// The press/release pair, delivered the way ctrl::Driver delivers them: the cursor
// consumes the QMouseEvent and the editor is handed that cursor.
bool hoverPress(
    ctrl::GraphEditor& aEditor, const core::CameraInfo& aCamera, core::AbstractCursor& aCursor,
    const QPoint& aPos
) {
    aEditor.setModifiers(Qt::NoModifier);
    QMouseEvent event(QEvent::MouseButtonPress, aPos, aPos, aPos, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    if (!aCursor.setMousePress(&event, aCamera))
        return false;
    return aEditor.updateCursor(aCamera, aCursor);
}

bool hoverRelease(
    ctrl::GraphEditor& aEditor, const core::CameraInfo& aCamera, core::AbstractCursor& aCursor, const QPoint& aPos
) {
    aEditor.setModifiers(Qt::NoModifier);
    QMouseEvent event(QEvent::MouseButtonRelease, aPos, aPos, aPos, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    if (!aCursor.setMouseRelease(&event, aCamera))
        return false;
    return aEditor.updateCursor(aCamera, aCursor);
}

bool checkHoverMatchesPress() {
    HoverAnimator animator;
    core::Project project("graph_panel_pixels.anie", animator, nullptr);
    cmnd::Stack setup;

    HoverNode node;
    auto* first = new core::ChannelKey(core::TimeKeyType_Rotate, 0);
    first->setFrame(100);
    first->setValue(0.0f);
    util::Easing::Param easing;
    easing.type = util::Easing::Type_Custom;
    easing.cubicBezier.x1 = 11.0f / 64.0f; // exactly 11px at 1 px/frame
    easing.cubicBezier.y1 = 0.0f;
    easing.cubicBezier.x2 = 0.9f; // the incoming dot stays clear of every probe
    easing.cubicBezier.y2 = 0.5f;
    first->easing() = easing;
    setup.push(new cmnd::GrabNewObject<core::ChannelKey>(first));
    setup.push(node.timeLine()->createChannelPusher(core::TimeKeyType_Rotate, 0, 100, first));

    auto* second = new core::ChannelKey(core::TimeKeyType_Rotate, 0);
    second->setFrame(164);
    second->setValue(0.0f); // the same value: key centres and the handle share one row
    setup.push(new cmnd::GrabNewObject<core::ChannelKey>(second));
    setup.push(node.timeLine()->createChannelPusher(core::TimeKeyType_Rotate, 0, 164, second));

    core::CameraInfo camera;
    camera.setScreenSize(QSize(800, 600));
    HoverLogger logger;
    ctrl::GraphEditor editor(project, logger);
    editor.setTargets({ &node });
    editor.clearSelection();

    // One cursor pass writes the panel rect the scale is pinned to and settles the
    // drawn-curve basis, so the window set below survives the presses.
    core::AbstractCursor settle;
    QMouseEvent settleEvent(
        QEvent::MouseMove, QPoint(5, 5), QPoint(5, 5), QPoint(5, 5), Qt::NoButton, Qt::NoButton, Qt::NoModifier
    );
    settle.setMouseMove(&settleEvent, camera);
    editor.updateCursor(camera, settle);

    ctrl::graph::Scaler& scale = editor.scale();
    REQUIRE(scale.applyFrameWindow(0.0, 800.0), "the hover geometry could not set its frame window");
    REQUIRE(scale.framePpf() == 1.0, "precondition: 1 px/frame (ppf=%.6f)", scale.framePpf());
    REQUIRE(scale.pixelXOfFrame(100.0) == 100.0, "precondition: the key is not at x=100");
    REQUIRE(scale.plotRect().height() % 2 == 0, "precondition: the plot has no integral middle row");
    const float plotHeight = static_cast<float>(scale.plotRect().height());
    scale.setValueWindow(-plotHeight, plotHeight);
    REQUIRE(
        scale.pixelYOfValue(0.0f) == scale.plotRect().top() + scale.plotRect().height() / 2.0,
        "precondition: value 0 did not land on an integral row"
    );

    const ctrl::graph::Curve curve(*node.timeLine(), ctrl::graph::Channel(core::TimeKeyType_Rotate, 0));
    REQUIRE(curve.segmentCount() == 1, "precondition: the fixture has %d segments", curve.segmentCount());
    const ctrl::graph::Curve::Segment segment = curve.segment(0);
    const QPointF keyCentre = QRectF(ctrl::graph::keyRect(scale, core::Frame(100), 0.0f)).center();
    const QPointF handleDot = ctrl::graph::handlePointAt(scale, segment, ctrl::graph::Curve::handle(segment, 0));
    REQUIRE(keyCentre == QPointF(100.0, scale.pixelYOfValue(0.0f)), "precondition: the key centre is not x=100");
    REQUIRE(handleDot == keyCentre + QPointF(11.0, 0.0),
            "precondition: the stored handle is not 11px right of the key");

    const int row = int(keyCentre.y());
    const int keyX = int(keyCentre.x());

    // THE PRESS: one press+release from a cleared selection, classified by what the
    // editor observably did - a key press selects the key it hit, a handle press
    // selects nothing and starts no marquee (GraphEditor's two remaining plot
    // gestures, which is how graph_drag's section 11b tells them apart too).
    struct Pressed {
        bool took;
        bool marquee;
        bool selected;
        int frame;
    };
    const auto pressAt = [&](const QPoint& aPos) {
        Pressed pressed{ false, false, false, -1 };
        editor.clearSelection();
        core::AbstractCursor cursor;
        pressed.took = hoverPress(editor, camera, cursor, aPos);
        pressed.marquee = editor.hasMarquee();
        core::TimeLineEvent selection;
        pressed.selected = editor.retrieveSelection(selection) && selection.targets().size() == 1;
        if (pressed.selected)
            pressed.frame = selection.targets()[0].pos.index();
        hoverRelease(editor, camera, cursor, aPos);
        return pressed;
    };

    const Pressed nearKey = pressAt(QPoint(keyX + 3, row));
    const Pressed nearHandle = pressAt(QPoint(keyX + 8, row));
    const Pressed tie = pressAt(QPoint(keyX + 5, row));
    REQUIRE(nearKey.took && nearKey.selected && !nearKey.marquee && nearKey.frame == 100,
            "the +3px press did not select the key (selected=%d frame=%d)", nearKey.selected, nearKey.frame);
    REQUIRE(nearHandle.took && !nearHandle.selected && !nearHandle.marquee,
            "the +8px press was not the handle drag (selected=%d marquee=%d)",
            nearHandle.selected, nearHandle.marquee);
    REQUIRE(tie.took && tie.selected && !tie.marquee && tie.frame == 100,
            "the +5px tie did not select the key (selected=%d frame=%d)", tie.selected, tie.frame);

    // THE HOVER SOURCE: the model the editor builds for this viewport, at the same
    // panel rect (buildGraphPanelModel re-rects a COPY of the editor's scaler to
    // GraphEditor::panelRect(viewport), which is this editor's own rect). The
    // published dots must be the geometry the presses used, or the comparison below
    // would be about two different pictures.
    const gui::GraphPanelModel model =
        gui::buildGraphPanelModel(editor, QSize(800, 600), theme::Graph::from(theme::Colors::current()));
    REQUIRE(model.curves.size() == 1, "precondition: the model published %d curves", int(model.curves.size()));
    const gui::PanelCurve& published = model.curves.at(0);
    REQUIRE(published.keyDots.size() == 2, "precondition: the model published %d key dots", int(published.keyDots.size()));
    REQUIRE(published.keyDots.at(0) == keyCentre, "the model's key dot is not the press geometry");
    REQUIRE(published.handles.size() == 2, "precondition: the model published %d handles", int(published.handles.size()));
    REQUIRE(published.handles.at(0).dot == handleDot, "the model's handle dot is not the press geometry");

    struct Probe {
        const char* name;
        QPoint at;
        double keyDistance;
        double handleDistance;
        Pressed pressed;
        const char* expected;
    };
    const Probe probes[] = {
        { "+3px", QPoint(keyX + 3, row), 3.0, 8.0, nearKey, "key" },
        { "+8px", QPoint(keyX + 8, row), 8.0, 3.0, nearHandle, "handle" },
        { "+5px", QPoint(keyX + 5, row), 5.0, 6.0, tie, "key" },
    };

    std::printf(
        "graph_panel_pixels hover: key centre x=%d, stored handle x=%d, row=%d\n", keyX, int(handleDot.x()), row
    );
    std::printf("  probe  at       key d/score    handle d/score    pressed hovered equal\n");
    for (const Probe& probe : probes) {
        const gui::PanelPick pick = gui::panelPickAt(model, probe.at);
        const char* hovered = pick.kind == ctrl::graph::PickKind::Key
                              ? "key"
                              : pick.kind == ctrl::graph::PickKind::Handle ? "handle" : "none";
        const char* pressed = !probe.pressed.took ? "none"
                              : probe.pressed.selected ? "key"
                              : probe.pressed.marquee ? "none" : "handle";
        const bool equal = std::strcmp(pressed, hovered) == 0;
        std::printf(
            "  %-5s (%d,%3d)  %4.1f / %.3f      %4.1f / %.3f        %-6s  %-6s  %s\n", probe.name, probe.at.x(),
            probe.at.y(), probe.keyDistance, probe.keyDistance / double(ctrl::graph::kKeyHitRadius),
            probe.handleDistance, probe.handleDistance / double(ctrl::graph::kHandleHitRadius), pressed, hovered,
            equal ? "yes" : "NO"
        );
        REQUIRE(equal, "the %s probe: a press selects %s but the hover ink names %s", probe.name, pressed, hovered);
        REQUIRE(std::strcmp(hovered, probe.expected) == 0, "the %s probe hovered %s, not %s", probe.name, hovered,
                probe.expected);
        if (pick.kind == ctrl::graph::PickKind::Key) {
            REQUIRE(pick.node == &node && pick.frame == core::Frame(100),
                    "the %s probe named the wrong key (node=%p frame=%d)", probe.name, (void*)pick.node,
                    pick.frame.get());
        } else if (pick.kind == ctrl::graph::PickKind::Handle) {
            REQUIRE(pick.curve == 0 && pick.handle == 0, "the %s probe named the wrong handle (%d,%d)", probe.name,
                    pick.curve, pick.handle);
        }
    }

    // The axis goes back before the node does (GraphEditor holds raw target pointers).
    editor.scale().clearFrameWindow();
    editor.setTargets({});
    return true;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

//-------------------------------------------------------------------------------------------------
// THE INK FIXTURE, kept for the two-backend parity block at the end of this file: six runs whose
// geometry exercises the stroke at four angles, a polyline joint, and the dense waviness of a real
// panel curve. Nothing else is published, so every pixel the parity walk reads is a stroke pixel.

// A horizontal run ON the pixel grid, and one HALF a pixel off it: see assertion 23 for why
// the phase is the whole point of the second one.
const QPointF kFlatA(110, 60);
const QPointF kFlatB(260, 60);
const QPointF kPhaseA(110, 95.5);
const QPointF kPhaseB(260, 95.5);
// 45 degrees, and a steeper ~71.6 degrees (run 3, rise 9): the angle the reported defect is
// about, and one far enough from it that a measurement which only handles the diagonal
// cannot pass both.
const QPointF kDiagonalA(110, 140);
const QPointF kDiagonalB(200, 230);
const QPointF kSteepA(240, 250);
const QPointF kSteepB(285, 115);
// The right-angle joint assertion 24 measures: arm A runs in to the vertex and arm B runs
// out of it, so the outer bisector from kCornerV is the one direction BOTH segments' own
// capsules must cover by themselves.
const QPointF kCornerV(370, 40);
const QPointF kCornerA(300, 40);
const QPointF kCornerB(370, 110);

// A DENSE WAVY POLYLINE: the panel's own curve is a polyline sampled every ~2px of plot
// width (GraphPanelModel.cpp's drawing budget), so its direction changes from sample to
// sample, and how a stroke treats those small turns is what "inconsistent line thickness"
// means along a curve. This fixture is that situation in isolation: samples 4px apart
// along a sine of amplitude 5px and wavelength 48px, which turns the local direction by up
// to ~33 degrees between neighbours - the middle of the range a curvy panel curve reaches.
QVector<QPointF> denseWavyPoints() {
    QVector<QPointF> points;
    for (int i = 0; i <= 30; ++i) {
        const double x = 110.0 + 4.0 * i;
        const double phase = (2.0 * M_PI) * (4.0 * i) / 48.0;
        points.append(QPointF(x, 253.0 + 5.0 * std::sin(phase)));
    }
    return points;
}

gui::GraphPanelModel makeInkModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();
    // NOTHING but the runs: see the block comment above.
    model.zeroY = -1;
    model.playheadX = -1;

    const auto run = [&aTheme](const QVector<QPointF>& aPoints) {
        gui::PanelCurve curve;
        curve.node = nullptr;
        curve.channel = ctrl::graph::Channel(kTypeA, 0);
        curve.points = aPoints;
        // ONE colour for all five: the regions are disjoint, so the ink's identity is
        // never in question, and the measurements below are all against this token.
        curve.color = aTheme.channelColor(kTypeA, 0);
        curve.channelRow = -1;
        return curve;
    };
    model.curves = {
        run({kFlatA, kFlatB}), run({kPhaseA, kPhaseB}), run({kDiagonalA, kDiagonalB}),
        run({kSteepA, kSteepB}), run({kCornerA, kCornerV, kCornerB}), run(denseWavyPoints()),
    };
    return model;
}

// ---- ONE INK, TWO PORTABLE RASTERISERS --------------------------------------------
//
// The panel's ink decisions live once (gui::drawPanel over gui::ink::Ink). This block renders
// the SAME model through the two portable backends - InkQPainter (Qt's own rasteriser, the
// reference) and InkCanvas (Canvas Painter, the shipping host) - and compares
// them under this file's own rules: geometry and fills exact, translucent ink allowed ONE unit
// per channel, text compared for PRESENCE per published label box (a label drawn by one engine
// and not the other is a failure; both-empty is the rule the ruler's pill suppression needs).
namespace {

int inkChannelDistance(QRgb a, QRgb b) {
    return std::max({std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
}

struct BackendVerdict {
    int textDiff = 0;
    int strokeDiff = 0;
    int blendLSB = 0;
    int geometry = 0;
    int labelFailures = 0;
    int labels = 0;
};

// A STROKE'S FRINGE IS THE RASTERISER'S: two engines antialias a 2px polyline with different
// kernels (measured: 1295 px around the same curves, the canvas one tinting a pixel further out
// and the Qt one not at all). So a pixel within a pixel and a half of a model POLYLINE is
// "ink-path" territory - the geometry is still checked, because the polyline itself is the
// model's - and everything else must match exactly.
bool nearPolyline(const QVector<QPointF>& aPoints, double aX, double aY, double aTolerance) {
    for (int i = 0; i + 1 < aPoints.size(); ++i) {
        const QPointF p = aPoints.at(i);
        const QPointF q = aPoints.at(i + 1);
        const QPointF d = q - p;
        const double len2 = d.x() * d.x() + d.y() * d.y();
        const double t = len2 > 0.0 ? qBound(0.0, ((aX - p.x()) * d.x() + (aY - p.y()) * d.y()) / len2, 1.0) : 0.0;
        const double dx = aX - (p.x() + d.x() * t);
        const double dy = aY - (p.y() + d.y() * t);
        if (dx * dx + dy * dy <= aTolerance * aTolerance)
            return true;
    }
    return false;
}

// A 1px RING IS THE SAME CLASS AS A STROKE FRINGE: Qt centres its pen on the path, Canvas
// Painter's AA spreads one column either side, and the panel draws a marker's edge, its
// selection ring and a handle's dot all as 1px circles. So a disc around every published marker
// centre - the dot's own radius plus the rings outside it - is ink-path territory.
bool nearCircle(const QPointF& aCentre, double aRadius, double aX, double aY) {
    const double d = std::hypot(aX - aCentre.x(), aY - aCentre.y());
    return d <= aRadius;
}

bool inStrokeRegion(const gui::GraphPanelModel& aModel, int aX, int aY) {
    // MEASURED, and the number is the stroke's own geometry: the panel's curves are 2px wide, so
    // a stroke on a half-pixel phase covers THREE rows (1.5px each side of the centreline), and
    // Canvas Painter antialiases one row further out than Qt does - rows 38..41 for a body of
    // 39..40 (measured on the flat run). 2.5px is the footprint plus that one row; everything
    // beyond it is not a stroke and must match exactly.
    constexpr double kTolerance = 2.5;
    for (const gui::PanelCurve& curve : aModel.curves) {
        if (nearPolyline(curve.points, aX, aY, kTolerance))
            return true;
        for (const gui::PanelHandle& handle : curve.handles) {
            if (nearPolyline({handle.base, handle.dot}, aX, aY, kTolerance))
                return true;
            if (nearCircle(handle.dot, double(handle.radius) + 2.5, aX, aY))
                return true;
        }
        for (const QPointF& key : curve.keyDots)
            if (nearCircle(key, double(ctrl::graph::kKeyDotRadius) + ctrl::time::kSelectRingOffset + 2.5, aX, aY))
                return true;
    }
    return false;
}


// A ROUNDED SHAPE'S 1px EDGE IS THE SAME CLASS as a stroke's fringe: Canvas2D's arc rasterises
// with one more column of coverage than Qt's (measured on the marquee's corner: the canvas inks
// #9a80e5 where Qt inks #8671c3, one pixel further out). The shape's OUTLINE is still the model's,
// so this excuses only a band of a pixel and a half around the border the model published.
double roundedBoxDistance(const QRectF& aRect, double aRadius, double aX, double aY) {
    const double cx = aRect.center().x();
    const double cy = aRect.center().y();
    const double hx = aRect.width() * 0.5 - aRadius;
    const double hy = aRect.height() * 0.5 - aRadius;
    const double qx = std::abs(aX - cx) - std::max(hx, 0.0);
    const double qy = std::abs(aY - cy) - std::max(hy, 0.0);
    const double ox = std::max(qx, 0.0);
    const double oy = std::max(qy, 0.0);
    return std::abs(std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0) - aRadius);
}

bool inShapeBorder(const gui::GraphPanelModel& aModel, int aX, int aY) {
    // 2.5, the same band a stroke gets, because a RADIUS-6 CORNER's arc is a stroke: its
    // antialiasing spills up to two pixels off the path, MEASURED on the marquee's corner (canvas
    // #33313c where Qt is floor at (123,55); #34303c against #322f3a two units apart at (258,111)).
    constexpr double kTolerance = 2.5;
    const auto near = [&](const QRect& aRect, double aRadius) {
        return !aRect.isEmpty() && roundedBoxDistance(QRectF(aRect), aRadius, aX, aY) <= kTolerance;
    };
    if (near(aModel.marquee, 6.0))
        return true;
    if (near(aModel.playheadPill, gui::frameRuler::kPillRadius))
        return true;
    if (near(aModel.playWindow.startPill, gui::frameRuler::kPillRadius)
        || near(aModel.playWindow.endPill, gui::frameRuler::kPillRadius)
        || near(aModel.playWindow.mergedPill, gui::frameRuler::kPillRadius))
        return true;
    if (near(aModel.hThumb, 2.0) || near(aModel.vThumb, 2.0))
        return true;
    for (const gui::PanelCurve& curve : aModel.curves)
        if (near(curve.chip, 4.0))
            return true;
    return false;
}

bool inTextRegion(const gui::GraphPanelModel& aModel, int aX, int aY) {
    constexpr int kFringe = 2;
    const auto hit = [&](const QRect& aRect) {
        return !aRect.isEmpty() && aRect.adjusted(-kFringe, -kFringe, kFringe, kFringe).contains(aX, aY);
    };
    for (const gui::PanelLabel& label : aModel.frameLabels)
        if (hit(label.rect))
            return true;
    for (const gui::PanelLabel& label : aModel.valueLabels)
        if (hit(label.rect))
            return true;
    for (const gui::PanelCurve& curve : aModel.curves)
        if (hit(curve.chip))
            return true;
    if (!aModel.emptyState.isEmpty() && aModel.plot.contains(aX, aY))
        return true;
    if (hit(aModel.playheadPill))
        return true;
    return false;
}

int inkPixelsIn(const QImage& aImage, const QRect& aBox) {
    const QRect box = aBox.intersected(aImage.rect());
    if (box.isEmpty())
        return 0;
    QHash<QRgb, int> hist;
    for (int y = box.top(); y <= box.bottom(); ++y)
        for (int x = box.left(); x <= box.right(); ++x)
            ++hist[aImage.pixel(x, y)];
    QRgb bg = 0;
    int best = -1;
    for (auto it = hist.cbegin(); it != hist.cend(); ++it)
        if (it.value() > best) { best = it.value(); bg = it.key(); }
    int ink = 0;
    for (int y = box.top(); y <= box.bottom(); ++y)
        for (int x = box.left(); x <= box.right(); ++x)
            if (aImage.pixel(x, y) != bg)
                ++ink;
    return ink;
}


// ---- THE BACKEND MATRIX: EDGE CASES, ONE MEASURED LINE EACH --------------------------
//
// One state is not evidence. This builds the states the panel actually gets into - an empty
// panel, one silent curve, a crowded plot, every marker state, both handle kinds, the play
// window, the marquee, both bar extremes, a panel too small for its own chrome, and a model
// with NO published labels - and paints each through both portable backends. Every state's
// verdict is printed with its own numbers, so a failure names the state and the count.
// The panel with nothing to show: the model's own empty state, no curves, no bars.
gui::GraphPanelModel makeInkModelEmpty() {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();
    model.zeroY = -1;
    model.playheadX = -1;
    model.emptyState = QStringLiteral("Select a layer to show its curves");
    return model;
}

gui::GraphPanelModel makeSilentCurveModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kRulerBand.top();
    model.zeroY = -1;
    model.playheadX = -1;
    gui::PanelCurve curve;
    curve.node = nullptr;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    curve.color = aTheme.channelColor(kTypeA, 0);
    curve.channelRow = -1;
    for (int i = 0; i <= 40; ++i) {
        const double t = i / 40.0;
        curve.points.append(QPointF(kPlot.left() + t * kPlot.width(), kPlot.center().y() + std::sin(t * 6.0) * 30.0));
    }
    model.curves = {curve};
    return model;
}

// EIGHT channels: the crowded plot, where chips de-collide and curves cross.
gui::GraphPanelModel makeCrowdedModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model = makeSilentCurveModel(aTheme);
    model.curves.clear();
    const core::TimeKeyType types[4] = {
        core::TimeKeyType_Move, core::TimeKeyType_Scale, core::TimeKeyType_Rotate, core::TimeKeyType_Opacity
    };
    for (int c = 0; c < 8; ++c) {
        gui::PanelCurve curve;
        curve.node = nullptr;
        curve.channel = ctrl::graph::Channel(types[c % 4], c / 4);
        curve.color = aTheme.channelColor(curve.channel.type(), curve.channel.component());
        curve.channelRow = c;
        for (int i = 0; i <= 20; ++i) {
            const double t = i / 20.0;
            curve.points.append(QPointF(
                kPlot.left() + t * kPlot.width(),
                kPlot.top() + 20.0 + (kPlot.height() - 40.0) * (0.5 + 0.4 * std::sin(t * 6.0 + c))
            ));
        }
        curve.keyDots = {curve.points.at(5), curve.points.at(15)};
        for (const QPointF& dot : curve.keyDots)
            curve.keys.append(QRect(int(dot.x()) - 5, int(dot.y()) - 5, 10, 10));
        curve.keyFrames = {core::Frame(20), core::Frame(60)};
        curve.keySelected = {c == 0, false};
        curve.keySiblingSelected = {false, c == 1};
        curve.keyProportional = {false, c == 2};
        curve.chip = QRect(kPlot.right() - 80, kPlot.top() + 8 + 16 * c, 74, 14);
        curve.chipText = QStringLiteral("Ch %1").arg(c);
        model.curves.append(curve);
    }
    return model;
}

// The marker states, on one key each, with the chip that names them.
gui::GraphPanelModel makeMarkerStatesModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model = makeSilentCurveModel(aTheme);
    gui::PanelCurve& curve = model.curves[0];
    curve.points = {QPointF(kPlot.left() + 20, kPlot.center().y()), QPointF(kPlot.right() - 20, kPlot.center().y())};
    curve.keyDots = {
        QPointF(150, 150), QPointF(200, 150), QPointF(250, 150), QPointF(300, 150), QPointF(350, 150)
    };
    for (const QPointF& dot : curve.keyDots)
        curve.keys.append(QRect(int(dot.x()) - 5, int(dot.y()) - 5, 10, 10));
    curve.keyFrames = {core::Frame(10), core::Frame(20), core::Frame(30), core::Frame(40), core::Frame(50)};
    curve.keySelected = {true, false, false, false, false};        // selected
    curve.keySiblingSelected = {false, true, false, false, false}; // sibling ring
    curve.keyProportional = {false, false, true, false, false};    // proportional alpha
    curve.chip = QRect(kPlot.right() - 84, kPlot.top() + 8, 74, 14);
    curve.chipText = QStringLiteral("Move X");
    return model;
}

// Both handle kinds on a focused channel, with a ghost beside the stored pair.
gui::GraphPanelModel makeHandleKindsModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model = makeMarkerStatesModel(aTheme);
    gui::PanelCurve& curve = model.curves[0];
    gui::PanelHandle stored;
    stored.base = curve.keyDots.at(0);
    stored.dot = stored.base + QPointF(30, -22);
    stored.radius = ctrl::graph::kHandleRadius;
    gui::PanelHandle ghost;
    ghost.base = curve.keyDots.at(2);
    ghost.dot = ghost.base + QPointF(-28, 26);
    ghost.radius = ctrl::graph::kHandleRadius;
    ghost.ghost = true;
    curve.handles = {stored, ghost};
    return model;
}

// The play window over the ruler band and the playhead parked over the VALUE COLUMN.
gui::GraphPanelModel makeChromeStressModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model = makeSilentCurveModel(aTheme);
    model.playWindow.active = true;
    model.playWindow.band = QRect(kPlot.left() + 40, kRulerBand.top(), 120, gui::frameRuler::kBandHeight);
    model.playWindow.startStem = QRect(model.playWindow.band.left(), kRulerBand.top(), 2, gui::frameRuler::kBandHeight);
    model.playWindow.endStem =
        QRect(model.playWindow.band.right() - 1, kRulerBand.top(), 2, gui::frameRuler::kBandHeight);
    model.playWindow.startPill = QRect(model.playWindow.band.left() - 6, kRulerBand.top() + 2, 30, 14);
    model.playWindow.endPill = QRect(model.playWindow.band.right() - 24, kRulerBand.top() + 2, 30, 14);
    model.playWindow.startText = QStringLiteral("40");
    model.playWindow.endText = QStringLiteral("160");
    // A playhead over the value column: the model puts frames on the timeline's own pixels.
    model.playheadX = 20;
    model.playheadLineTop = kRulerBand.bottom() + 1;
    model.playheadText = QStringLiteral("9999");
    model.playheadPill = QRect(4, kRulerBand.top() + gui::frameRuler::kNumberTop - 2, 34, gui::frameRuler::kNumberHeight);
    model.marquee = QRect(kPlot.left() + 30, kPlot.top() + 30, 140, 60);
    model.hTrack = QRect(kPlot.left(), kPlot.bottom() + 2, kPlot.width(), 12);
    model.hThumb = QRect(model.hTrack.left() + 20, model.hTrack.top(), 60, 12);
    model.vTrack = QRect(kPlot.right() + 2, kPlot.top(), 12, kPlot.height());
    model.vThumb = QRect(model.vTrack.left(), model.vTrack.top() + 40, 12, 50);
    return model;
}

// A panel too small for its own chrome: the plot has room but the value column does not.
gui::GraphPanelModel makeTinyModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = QRect(0, 0, 120, 90);
    model.plot = QRect(40, 26, 70, 50);
    model.valueColumn = QRect(0, 26, 40, 50);
    model.rulerBand = QRect(0, 0, 120, gui::frameRuler::kBandHeight);
    model.rulerBandTop = 0;
    model.zeroY = model.plot.center().y();
    model.playheadX = model.plot.center().x();
    model.playheadLineTop = gui::frameRuler::kBandHeight;
    gui::PanelCurve curve;
    curve.node = nullptr;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    curve.color = aTheme.channelColor(kTypeA, 0);
    curve.points = {QPointF(model.plot.left(), model.plot.top()), QPointF(model.plot.right(), model.plot.bottom())};
    curve.keyDots = {curve.points.first(), curve.points.last()};
    for (const QPointF& dot : curve.keyDots)
        curve.keys.append(QRect(int(dot.x()) - 5, int(dot.y()) - 5, 10, 10));
    curve.keySelected = {true, false};
    curve.keySiblingSelected = {false, false};
    curve.keyProportional = {false, false};
    model.curves = {curve};
    return model;
}

bool rendBackendParity(const gui::GraphPanelModel& aModel, const theme::Graph& aTheme, const gui::PanelFocus& aFocus,
                       const char* aName);

struct MatrixState {
    const char* name;
    bool expectsLabels;
};

bool renderBackendMatrix(const theme::Graph& aTheme, const gui::PanelFocus& aFocus) {
    const MatrixState states[] = {
        {"empty", false},      {"silent-curve", false}, {"crowded-8", true},  {"marker-states", true},
        {"handle-kinds", true}, {"chrome-stress", true}, {"tiny", false},
    };
    const auto build = [&aTheme](const char* aName) -> gui::GraphPanelModel {
        const std::string name(aName);
        if (name == "empty")
            return makeInkModelEmpty();
        if (name == "silent-curve")
            return makeSilentCurveModel(aTheme);
        if (name == "crowded-8")
            return makeCrowdedModel(aTheme);
        if (name == "marker-states")
            return makeMarkerStatesModel(aTheme);
        if (name == "handle-kinds")
            return makeHandleKindsModel(aTheme);
        if (name == "chrome-stress")
            return makeChromeStressModel(aTheme);
        return makeTinyModel(aTheme);
    };
    int cases = 0, failures = 0;
    for (const MatrixState& state : states) {
        const gui::GraphPanelModel model = build(state.name);
        const bool ok = rendBackendParity(model, aTheme, aFocus, state.name);
        if (!ok)
            ++failures;
        ++cases; // rendBackendParity prints its own geometry count, per state
    }
    std::printf("MEASURED backend_matrix states=%d failures=%d\n", cases, failures);
    return failures == 0;
}

bool rendBackendParity(const gui::GraphPanelModel& aModel, const theme::Graph& aTheme, const gui::PanelFocus& aFocus,
                       const char* aName = "ink") {
    const QColor floor = theme::Colors::current().base;
    const QFont font = QApplication::font();

    // Qt's rasteriser.
    QImage raster(kViewW, kViewH, QImage::Format_ARGB32_Premultiplied);
    raster.fill(floor);
    {
        QPainter painter(&raster);
        gui::ink::InkQPainter ink(painter, font);
        gui::ink::Pointer pointer;
        gui::drawPanel(ink, aModel, aTheme, aFocus, pointer);
    }

    // Canvas Painter, through the widget the dock will host.
    class Harness : public QCanvasPainterWidget {
    public:
        Harness(const gui::GraphPanelModel& aModel, const theme::Graph& aTheme, const gui::PanelFocus& aFocus,
                const QFont& aFont)
            : mModel(aModel), mTheme(aTheme), mFocus(aFocus), mFont(aFont) {}

    protected:
        void paint(QCanvasPainter* aPainter) override {
            gui::ink::InkCanvas ink(*aPainter, mFont);
            gui::ink::Pointer pointer;
            gui::drawPanel(ink, mModel, mTheme, mFocus, pointer);
        }

    private:
        const gui::GraphPanelModel& mModel;
        const theme::Graph& mTheme;
        const gui::PanelFocus& mFocus;
        QFont mFont;
    };
    Harness widget(aModel, aTheme, aFocus, font);
    widget.setFillColor(floor);
    widget.resize(kViewW, kViewH);
    const QImage canvas = widget.grabFramebuffer();
    if (canvas.isNull()) {
        std::printf("FAIL: the Canvas backend produced no frame\n");
        return false;
    }

    // A DUMP of both frames, for the investigation that a pixel COUNT cannot answer: the
    // geometry of a difference is the thing that names the mark that made it.
    if (qEnvironmentVariableIsSet("AE_BACKEND_DUMP")) {
        raster.save(QString::fromUtf8(qgetenv("AE_BACKEND_DUMP")) + "-qpainter.png");
        canvas.save(QString::fromUtf8(qgetenv("AE_BACKEND_DUMP")) + "-canvas.png");
    }
    BackendVerdict v;
    for (int y = 0; y < std::min(raster.height(), canvas.height()); ++y) {
        for (int x = 0; x < std::min(raster.width(), canvas.width()); ++x) {
            const QRgb a = raster.pixel(x, y);
            const QRgb b = canvas.pixel(x, y);
            if (a == b)
                continue;
            if (inkChannelDistance(a, b) <= 1) {
                ++v.blendLSB;
                continue;
            }
            if (inTextRegion(aModel, x, y)) {
                ++v.textDiff;
                continue;
            }
            if (inStrokeRegion(aModel, x, y) || inShapeBorder(aModel, x, y)) {
                ++v.strokeDiff;
                continue;
            }
            ++v.geometry;
        }
    }
    if (QString::fromUtf8(aName) == QLatin1String("chrome-stress")) {
        for (int y = 52; y <= 60; ++y) {
            std::printf("  marquee row y=%d  qpainter x=118..124:", y);
            for (int x = 118; x <= 124; ++x) std::printf(" %s", qPrintable(QColor::fromRgb(raster.pixel(x, y)).name()));
            std::printf("  canvas:");
            for (int x = 118; x <= 124; ++x) std::printf(" %s", qPrintable(QColor::fromRgb(canvas.pixel(x, y)).name()));
            std::printf("\n");
        }
    }
    // LABEL PRESENCE, per published box and in BOTH images.
    const auto check = [&](const QRect& aBox, const QString& aName) {
        ++v.labels;
        const int a = inkPixelsIn(raster, aBox);
        const int b = inkPixelsIn(canvas, aBox);
        const bool ok = (a == 0) == (b == 0) && (a == 0 || std::abs(a - b) * 2 <= std::max(a, b));
        if (!ok) {
            ++v.labelFailures;
            std::printf("  label %-24s box=(%d,%d %dx%d) qpainter=%3d canvas=%3d MISSING/DIFFERS\n",
                        qPrintable(aName), aBox.x(), aBox.y(), aBox.width(), aBox.height(), a, b);
        }
    };
    for (const gui::PanelLabel& label : aModel.frameLabels)
        check(label.rect, label.text);
    for (const gui::PanelLabel& label : aModel.valueLabels)
        check(label.rect, label.text);
    for (const gui::PanelCurve& curve : aModel.curves)
        if (!curve.chip.isEmpty())
            check(curve.chip, curve.chipText);
    if (!aModel.playheadPill.isEmpty())
        check(aModel.playheadPill, aModel.playheadText);

    // WHERE the differences are, so the next port has a lead instead of a number: a coarse map
    // by region, then the first dozen pixels with both colours.
    {
        int band = 0, plotArea = 0, valueCol = 0, outside = 0, shown = 0;
        for (int y = 0; y < kViewH; ++y) {
            for (int x = 0; x < kViewW; ++x) {
                const QRgb a = raster.pixel(x, y);
                const QRgb b = canvas.pixel(x, y);
                if (a == b || inkChannelDistance(a, b) <= 1 || inTextRegion(aModel, x, y)
                    || inStrokeRegion(aModel, x, y) || inShapeBorder(aModel, x, y))
                    continue;
                const QPoint p(x, y);
                if (!aModel.rulerBand.isEmpty() && aModel.rulerBand.contains(p))
                    ++band;
                else if (aModel.valueColumn.contains(p))
                    ++valueCol;
                else if (aModel.plot.contains(p))
                    ++plotArea;
                else
                    ++outside;
                if (shown < 12) {
                    ++shown;
                    // HOW FAR from the nearest model curve point, so the lead is a distance and
                    // not a guess: a fringe is <2px, a mark is where the model says it is.
                    double nearest = 1e9;
                    const char* nearestName = "-";
                    for (const gui::PanelCurve& curve : aModel.curves) {
                        for (const QPointF& pt : curve.points) {
                            const double d = std::hypot(pt.x() - x, pt.y() - y);
                            if (d < nearest) { nearest = d; nearestName = "curve"; }
                        }
                        for (const gui::PanelHandle& handle : curve.handles) {
                            const double d = std::hypot(handle.dot.x() - x, handle.dot.y() - y);
                            if (d < nearest) { nearest = d; nearestName = "handle.dot"; }
                        }
                        for (const QPointF& key : curve.keyDots) {
                            const double d = std::hypot(key.x() - x, key.y() - y);
                            if (d < nearest) { nearest = d; nearestName = "key"; }
                        }
                    }
                    std::printf("  diff @(%d,%d) qpainter=%s canvas=%s nearest=%s %.1fpx\n", x, y,
                                qPrintable(QColor::fromRgb(a).name()), qPrintable(QColor::fromRgb(b).name()),
                                nearestName, nearest);
                }
            }
        }
        std::printf("  diff regions: band=%d plot=%d valueColumn=%d outside=%d\n", band, plotArea, valueCol, outside);
        int x0 = 1e6, x1 = -1, y0 = 1e6, y1 = -1;
        for (int y = 0; y < kViewH; ++y)
            for (int x = 0; x < kViewW; ++x) {
                const QRgb a = raster.pixel(x, y);
                const QRgb b = canvas.pixel(x, y);
                if (a == b || inkChannelDistance(a, b) <= 1 || inTextRegion(aModel, x, y)
                    || inStrokeRegion(aModel, x, y) || inShapeBorder(aModel, x, y))
                    continue;
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
        std::printf("  diff bounds: x %d..%d  y %d..%d  (plot %d,%d %dx%d; marquee %d,%d %dx%d)\n",
                    x0, x1, y0, y1, aModel.plot.x(), aModel.plot.y(), aModel.plot.width(), aModel.plot.height(),
                    aModel.marquee.x(), aModel.marquee.y(), aModel.marquee.width(), aModel.marquee.height());
    }
    std::printf(
        "MEASURED backends %-14s qpainter=%dx%d canvas=%dx%d textDiff=%5d strokeDiff=%5d blendLSB=%5d"
        " GEOMETRY=%4d labels=%d/%d\n",
        aName, kViewW, kViewH, canvas.width(), canvas.height(), v.textDiff, v.strokeDiff, v.blendLSB, v.geometry,
        v.labelFailures, v.labels
    );
    if (v.geometry != 0) {
        std::printf("FAILED: %d px differ between the two portable backends outside text and 1-LSB blends\n", v.geometry);
        return false;
    }
    if (v.labelFailures != 0) {
        std::printf("FAILED: %d label(s) inked by one backend and not the other\n", v.labelFailures);
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // THE HOVER MUST NAME WHAT A PRESS SELECTS (see checkHoverMatchesPress): a real
    // editor's press and the model's pick at the three overlapping-ring probes. It
    // needs no window, GL or frame - a document, a scaler and the model - so it runs
    // before the first Canvas frame, and a failure stops the gate here.
    if (!checkHoverMatchesPress())
        return 1;

    // NO GL CONTEXT OF THIS FILE'S OWN: QCanvasPainterWidget creates the RHI (GL) device
    // its paint runs on, and renderPass() owns one widget per frame. The gate still needs
    // a window system with a real GL implementation - scripts/gui/gpu_headless.sh is the
    // default, xvfb-run the fallback - because the widget refuses to render otherwise.
    // Every rect below is in the widget's own logical pixels, the widget is sized by
    // renderPass, and the DPR is asserted at 1 by the frame-size check there.

    const theme::Graph palette = theme::Graph::from(theme::Colors::current());
    std::printf(
        "INFO palette halo=%s axisText=%s gridMajor=%s axisLine=%s selected=%s keyEdge=%s channelA=%s"
        " channelB=%s marqueeFill=%s marqueeEdge=%s clear=%s\n",
        hex(palette.halo()).c_str(), hex(palette.axisText()).c_str(), hex(palette.gridMajor()).c_str(),
        hex(palette.axisLine()).c_str(), hex(palette.selected()).c_str(), hex(palette.keyEdge()).c_str(),
        hex(palette.channelColor(kTypeA, 0)).c_str(), hex(palette.channelColor(kTypeB, 1)).c_str(),
        hex(palette.marqueeFill()).c_str(), hex(palette.marqueeEdge()).c_str(), hex(kClear).c_str()
    );

    // The clear colour has to be far from everything the model can paint, or "this
    // pixel moved" would not mean "the panel painted here".
    {
        const QColor painted[] = {
            palette.halo(), palette.axisText(), palette.gridMajor(), palette.axisLine(), palette.selected(),
            palette.keyEdge(), palette.channelColor(kTypeA, 0), palette.channelColor(kTypeB, 1),
            palette.marqueeFill(), palette.marqueeEdge(),
            // The scroll bars' own two tokens (assertion 31): the pill's rest and
            // hover inks are read out of the framebuffer by colour, so each has to be
            // far from the clear colour like every other ink here.
            theme::Colors::current().outline, theme::Colors::current().hover,
        };
        for (const QColor& color : painted) {
            if (clearDistance(color) <= 60) {
                std::printf(
                    "FAIL: precondition: the clear colour rgb=%s is only %d from the panel colour rgb=%s\n",
                    hex(kClear).c_str(), clearDistance(color), hex(color).c_str()
                );
                return 1;
            }
        }
    }

    const gui::GraphPanelModel full = makeFullModel(palette);
    const gui::GraphPanelModel clip = makeClipModel(palette);
    const gui::GraphPanelModel empty = makeEmptyModel();

    // An INVALID focus, which decides two things and neither of them is the emphasis:
    // the panel draws EVERY channel's tangent handles for it (see drawPanel: the
    // handles are drawn for the focused channel only, and an invalid focus draws them
    // all), and the EMPHASIS is the SELECTION's (gui::curveStrength), which is empty
    // here - so every curve and every marker in this pass is drawn DIMMED, and the two
    // selection passes below are the full-strength state of each channel (see checkKeys).
    gui::PanelFocus focus;
    focus.valid = false;

    // The same model with the zero rule switched OFF: the ONLY difference is zeroY,
    // which is what makes assertion 8 a pair.
    gui::GraphPanelModel fullNoRule = full;
    fullNoRule.zeroY = -1;

    // The SAME model with the playhead invalid (no current frame): the pair assertion
    // 17 reads the pill's own ink against. Everything else - the band, the ticks, the
    // numbers - is identical, so a pixel at the playhead's column inside the band that
    // differs between the two passes is the playhead's ink and nothing else.
    gui::GraphPanelModel fullNoFrame = full;
    fullNoFrame.playheadX = -1;
    fullNoFrame.playheadText.clear();
    fullNoFrame.playheadPill = QRect();
    fullNoFrame.playheadLineTop = -1;

    // A model whose one curve carries BOTH a stored handle pair and a derived (ghost)
    // one, and nothing else that paints: the two dots' own pixels are the pair
    // assertion 9d reads.
    const gui::GraphPanelModel ghost = makeGhostModel(palette);

    // A model whose frame labels have a box SHORTER than the draw list's font: the
    // configuration the ruler clamp exists for (a drawing-layer font taller than the
    // model's box, which is what a different UI font or an atlas density produces).
    // Everything else is the full model, so the ONLY difference is the box height.
    gui::GraphPanelModel tightRuler = full;
    for (gui::PanelLabel& label : tightRuler.frameLabels) {
        label.rect.setHeight(8);
        // DOWN into the band, so the rows above the short box are band surface and the
        // clamp is observable (the band's number row starts at kNumberTop).
        label.rect.moveTop(kRulerBand.top() + gui::frameRuler::kNumberTop + 5);
    }
    // The SAME models with their frame labels removed: the pair the halo and ruler
    // assertions read "the label's own ink" against, now that the ruler's band
    // legitimately fills every one of those rows.
    gui::GraphPanelModel fullNoLabels = full;
    fullNoLabels.frameLabels.clear();
    gui::GraphPanelModel tightRulerBare = tightRuler;
    tightRulerBare.frameLabels.clear();

    // The same model with a LIVE BOX-SELECT MARQUEE: the ONLY difference is the
    // marquee rectangle, which is what makes assertion 18 a pair.
    gui::GraphPanelModel fullMarquee = full;
    fullMarquee.marquee = kMarqueeRect;

    // THE PLAYBACK WINDOW OVERLAY (assertion 28): a model carrying ONLY the ruler band
    // and the window's own geometry, so every pixel inside a published rect is the
    // overlay's ink. ONE fixture has two label arms; the other collapses them into the
    // centred merged pill (a window narrower than its two labels).
    gui::GraphPanelModel playWindowSplit;
    playWindowSplit.panel = kPanel;
    playWindowSplit.plot = kPlot;
    playWindowSplit.valueColumn = kValueColumn;
    playWindowSplit.rulerBandTop = kRulerBand.top();
    playWindowSplit.rulerBand = kRulerBand;
    playWindowSplit.playWindow.active = true;
    playWindowSplit.playWindow.band =
        QRect(140, kRulerBand.top(), 141, gui::frameRuler::kBandHeight);
    playWindowSplit.playWindow.startStem = QRect(
        140, kRulerBand.top() + gui::frameRuler::kPillTop, 2,
        gui::frameRuler::kBandHeight - gui::frameRuler::kPillTop
    );
    playWindowSplit.playWindow.endStem = QRect(
        279, kRulerBand.top() + gui::frameRuler::kPillTop, 2,
        gui::frameRuler::kBandHeight - gui::frameRuler::kPillTop
    );
    playWindowSplit.playWindow.startPill =
        QRect(142, kRulerBand.top() + gui::frameRuler::kPillTop, 30, gui::frameRuler::kNumberHeight);
    playWindowSplit.playWindow.endPill =
        QRect(249, kRulerBand.top() + gui::frameRuler::kPillTop, 30, gui::frameRuler::kNumberHeight);
    playWindowSplit.playWindow.startText = QStringLiteral("20");
    playWindowSplit.playWindow.endText = QStringLiteral("80");

    gui::GraphPanelModel playWindowMerged = playWindowSplit;
    playWindowMerged.playWindow.band = QRect(200, kRulerBand.top(), 6, gui::frameRuler::kBandHeight);
    playWindowMerged.playWindow.startStem = QRect(
        200, kRulerBand.top() + gui::frameRuler::kPillTop, 2,
        gui::frameRuler::kBandHeight - gui::frameRuler::kPillTop
    );
    playWindowMerged.playWindow.endStem = QRect(
        204, kRulerBand.top() + gui::frameRuler::kPillTop, 2,
        gui::frameRuler::kBandHeight - gui::frameRuler::kPillTop
    );
    playWindowMerged.playWindow.startPill = QRect();
    playWindowMerged.playWindow.endPill = QRect();
    playWindowMerged.playWindow.startText.clear();
    playWindowMerged.playWindow.endText.clear();
    playWindowMerged.playWindow.mergedText = QString::fromUtf8("20\u201321");
    playWindowMerged.playWindow.mergedPill =
        QRect(180, kRulerBand.top() + gui::frameRuler::kPillTop, 44, gui::frameRuler::kNumberHeight);

    // Assertion 21's guard model: a panel that does NOT start at the framebuffer's left
    // edge and one value label whose text is far wider than its box (the model's measured
    // column cannot produce that - a drawing font wider than the model's can), so the
    // band left of the panel is the only thing to read. Nothing else is published, so
    // that band cannot be filled by any other element of the panel.
    gui::GraphPanelModel overflow;
    overflow.panel = QRect(60, 0, 340, 300);
    overflow.plot = QRect(150, 30, 240, 240);
    overflow.valueColumn = QRect(66, 30, 83, 240);
    overflow.rulerBandTop = overflow.plot.top() - ctrl::graph::Scaler::kMargin - gui::frameRuler::kBandHeight;
    overflow.valueLabels = {{QRect(66, 60, 20, 14), QStringLiteral("-1.235e+07")}};

    // 0: the control, then one pass per model the assertions read.
    //
    // The ink fixture is no longer driven by a stroke assertion (the coverage pass it
    // compared against is gone with the ImGui backend): it is the parity block's stress
    // model, six runs at four angles, a joint and a dense polyline, painted through both
    // portable backends at the end of this file.
    const gui::GraphPanelModel ink = makeInkModel(palette);

    const Rendered control = renderPass(gui::GraphPanelModel(), palette, focus, "control");
    const Rendered rendered = renderPass(full, palette, focus, "full_model");
    const Rendered emptyRendered = renderPass(empty, palette, focus, "empty_state");
    const Rendered clipRendered = renderPass(clip, palette, focus, "clip");
    const Rendered noRuleRendered = renderPass(fullNoRule, palette, focus, "full_model_zeroY_off");
    const Rendered ghostRendered = renderPass(ghost, palette, focus, "ghost_handle");
    const Rendered marqueeRendered = renderPass(fullMarquee, palette, focus, "full_model_marquee");
    const Rendered playWindowSplitRendered =
        renderPass(playWindowSplit, palette, focus, "play_window_split");
    const Rendered playWindowMergedRendered =
        renderPass(playWindowMerged, palette, focus, "play_window_merged");
    const Rendered overflowRendered = renderPass(overflow, palette, focus, "value_label_overflow");

    // The same model with a focus on the curve that carries NO handles: the only
    // difference is the focus, which is what makes assertion 20 a pair.
    gui::PanelFocus focusOther;
    focusOther.valid = true;
    focusOther.node = full.curves.at(1).node;
    focusOther.channel = full.curves.at(1).channel;
    const Rendered focusedOtherRendered = renderPass(full, palette, focusOther, "full_model_focus_other_curve");
    // The tight-ruler pass draws with a font twice the box height: the defect is a
    // drawing font taller than the model's box, and this is it (see checkRulerReserve).
    QFont tightFont = QApplication::font();
    tightFont.setPixelSize(26);
    const Rendered tightRulerRendered =
        renderPass(tightRuler, palette, focus, "full_model_tight_ruler", kPointerOff, tightFont);
    // The label-less pair for the halo and ruler assertions: the same models with
    // their frame labels removed, so a pixel that differs is the LABEL's ink.
    const Rendered fullBareRendered = renderPass(fullNoLabels, palette, focus, "full_model_no_labels");
    const Rendered fullNoFrameRendered = renderPass(fullNoFrame, palette, focus, "full_model_no_frame");
    const Rendered tightRulerBareRendered =
        renderPass(tightRulerBare, palette, focus, "full_model_tight_ruler_bare", kPointerOff, tightFont);
    // THE RULER'S ROW PASSES (see checkRulerRows): the full model at a pixel size whose
    // centring offset against the 14px number row is AT LEAST TWO rows, and the same model
    // with its frame labels removed, so the number's own ink is read as the difference.
    QFont rowFont = QApplication::font();
    rowFont.setPixelSize(8);
    const Rendered rulerRowRendered =
        renderPass(full, palette, focus, "full_model_ruler_rows", kPointerOff, rowFont);
    const Rendered rulerRowBareRendered =
        renderPass(fullNoLabels, palette, focus, "full_model_ruler_rows_bare", kPointerOff, rowFont);

    // The handle highlight pass: the SAME model with the pointer on the first
    // handle's dot, so the ONLY difference from `rendered` is the pointer - which
    // is what makes "the dot changed" a statement about the highlight alone.
    if (full.curves.isEmpty() || full.curves.at(0).handles.isEmpty()) {
        std::printf("FAIL: the full model publishes no tangent handle to point at\n");
        return 1;
    }
    const QPoint pointerDot(
        int(full.curves.at(0).handles.at(0).dot.x()), int(full.curves.at(0).handles.at(0).dot.y())
    );
    const Rendered highlightRendered = renderPass(
        full, palette, focus, "full_model_pointer_on_handle",
        QPointF(pointerDot.x(), pointerDot.y())
    );

    // THE EMPHASIS PASSES. A channel is at full strength when it OWNS a selected marker,
    // or the same KEY is selected on a sibling channel (gui::curveStrength, off the
    // model's own PanelCurve::keySelected / keySiblingSelected). So each of these is
    // `rendered`'s SAME model and SAME invalid focus with ONE of its curves' first marker
    // flagged SELECTED - the flag is the only difference, and the invalid focus keeps the
    // handles drawn exactly as `rendered` draws them. One pass per curve, so every key
    // marker has a pass in which its own channel is in play (see checkKeys).
    //
    // THE FLAGS ARE THE SAME TWO VECTORS THE EDITOR FILLS (PanelCurve::keySelected is
    // ctrl::GraphEditor::isMarkerSelected per key), which is why this file can drive them
    // by hand: it has no editor (see the file header), and the model's contract is that a
    // dot's state travels with the dot.
    if (full.curves.size() < 2 || full.curves.at(0).keyDots.isEmpty() || full.curves.at(1).keyDots.isEmpty()) {
        std::printf("FAIL: the full model publishes no key markers to select\n");
        return 1;
    }
    const auto selectedMarkerModel = [&full](int aCurve) {
        gui::GraphPanelModel model = full;
        gui::PanelCurve& curve = model.curves[aCurve];
        // Every key of every curve starts unflagged (the fixture publishes none), and the
        // one marker this pass is about is the curve's first.
        for (gui::PanelCurve& each : model.curves) {
            each.keySelected.fill(false, int(each.keyDots.size()));
            each.keySiblingSelected.fill(false, int(each.keyDots.size()));
        }
        curve.keySelected[0] = true;
        return model;
    };
    const Rendered selectedFirst = renderPass(
        selectedMarkerModel(0), palette, focus, "full_model_selected_first_curve"
    );
    const Rendered selectedSecond = renderPass(
        selectedMarkerModel(1), palette, focus, "full_model_selected_second_curve"
    );
    // THE GEOMETRIC PASS: both curves' LAST key selected, so each curve is in play with its
    // first marker UNRINGED - the state the marker's size, centring and hit-square corners
    // are measured in (see checkKeys). Without it the first key of each curve could only be
    // measured with its selection ring on, and the ring's ink sits where the hit square's
    // corners are.
    const Rendered selectedLast = renderPass(
        [&full] {
            gui::GraphPanelModel model = full;
            for (gui::PanelCurve& curve : model.curves) {
                curve.keySelected.fill(false, int(curve.keyDots.size()));
                curve.keySiblingSelected.fill(false, int(curve.keyDots.size()));
                if (!curve.keySelected.isEmpty()) {
                    curve.keySelected.last() = true;
                }
            }
            return model;
        }(),
        palette, focus, "full_model_selected_last_curve"
    );

    // THE DRAW-ORDER PASS (assertion 17b): the selected curve's chip is moved onto
    // the playhead's column, deep inside the plot. The chip is drawn first and the
    // playhead after it, so the column must read as the accent through the chip's
    // rows - and the chip must still be visible beside it.
    gui::GraphPanelModel chipUnderPlayhead = selectedMarkerModel(0);
    chipUnderPlayhead.curves[0].chip = QRect(chipUnderPlayhead.playheadX - 30, 150, 60, 14);
    const Rendered chipUnderPlayheadRendered =
        renderPass(chipUnderPlayhead, palette, focus, "full_model_chip_under_playhead");

    // THE THREE MARKER STATES (assertion 29), on the ONE-KEY fixture: the same model, the
    // same invalid focus, and only the marker flags (and the pointer) differing between the
    // four passes. `markerSelected` is the editor's own state for "the Move X marker of one
    // key is selected": that marker selected, the OTHER COMPONENT of the same key in play as
    // its sibling.
    //
    // The two LAYERS are real ObjectNodes (the cheapest concrete one in the tree): the marker
    // hover is answered by gui::panelKeyAt, which names a key by (node, channel, frame), and a
    // model whose curves leave `node` null cannot be hovered at all. Nothing renders them -
    // only their addresses are compared.
    core::FolderNode markerLayerX(QStringLiteral("A"));
    core::FolderNode markerLayerY(QStringLiteral("B"));
    const gui::GraphPanelModel marker = makeMarkerModel(palette, &markerLayerX, &markerLayerY);
    if (marker.curves.size() != 2 || marker.curves.at(0).keyDots.size() != 1
        || marker.curves.at(1).keyDots.size() != 1) {
        std::printf("FAIL: the marker fixture does not publish the two components of one key\n");
        return 1;
    }
    const QPoint markerDot(
        int(marker.curves.at(0).keyDots.at(0).x()), int(marker.curves.at(0).keyDots.at(0).y())
    );
    gui::GraphPanelModel markerSelectedModel = marker;
    markerSelectedModel.curves[0].keySelected = {true};
    markerSelectedModel.curves[0].keySiblingSelected = {false};
    markerSelectedModel.curves[1].keySelected = {false};
    markerSelectedModel.curves[1].keySiblingSelected = {true};
    const Rendered markerPlain = renderPass(marker, palette, focus, "marker_plain");
    const Rendered markerSelected = renderPass(markerSelectedModel, palette, focus, "marker_selected");
    // The hover pass: the UNFLAGGED model with the pointer on the first marker, so the only
    // difference from `markerPlain` is the pointer.
    const Rendered markerHover = renderPass(
        marker, palette, focus, "marker_hover", QPointF(markerDot.x(), markerDot.y())
    );

    // THE PROPORTIONAL MARKER (assertion 32), on its own one-channel fixture: an absolute
    // and a proportional marker of the SAME channel, both selected. The CONTROL pass is
    // the same model with the proportional flag cleared, so the pair isolates exactly what
    // the flag changes: the fill's alpha, and nothing else.
    core::FolderNode proportionalLayer(QStringLiteral("P"));
    const gui::GraphPanelModel proportionalMarker = makeProportionalMarkerModel(palette, &proportionalLayer);
    gui::GraphPanelModel proportionalMarkerControl = proportionalMarker;
    proportionalMarkerControl.curves[0].keyProportional = {false, false};
    const Rendered proportionalMarkerRendered =
        renderPass(proportionalMarker, palette, focus, "marker_proportional");
    const Rendered proportionalMarkerControlRendered =
        renderPass(proportionalMarkerControl, palette, focus, "marker_proportional_control");

    // THE DRAG READOUT (assertion 30) IS GONE, with the ink it read: the panel prints no
    // gesture numbers any more, so there is no box at the pointer to measure.

    // THE FRAME RANGE'S GUIDES (assertion 33): the full model, plus two copies that
    // differ only in what the guides are - the columns (absent from `full`, so it is the
    // control) and the recede classification. The fixture's two columns are chosen from
    // the model's own geometry: one inside the VALUE COLUMN (where the shared mapping
    // puts the project's first frame at the usual fit) and one inside the plot, clear of
    // the chips at x 300..371, the keys, the handles and the playhead's column.
    gui::GraphPanelModel guideModel = full;
    guideModel.rangeGuideX = {30, 120};
    const Rendered guideRendered = renderPass(guideModel, palette, focus, "full_model_range_guides");
    gui::GraphPanelModel recededGuideModel = guideModel;
    recededGuideModel.rangeGuideRecede = true;
    const Rendered recededGuideRendered =
        renderPass(recededGuideModel, palette, focus, "full_model_range_guides_receded");

    if (rendered.image.isNull()) {
        std::printf("FAIL: the full model produced no frame at all\n");
        return 1;
    }
    if (inkCount(rendered.image, kPanel) <= 0) {
        std::printf("FAIL: the full model painted nothing inside its own panel\n");
        return 1;
    }

    dumpInkCrops(rendered, full, QStringLiteral("panel"));

    if (!checkControl(control)) {
        std::printf("FAILED: control\n");
        return 1;
    }
    if (!checkNoGrid(rendered, full)) {
        std::printf("FAILED: no grid / no background\n");
        return 1;
    }
    if (!checkCurveInk(rendered, full)) {
        std::printf("FAILED: curve stroke\n");
        return 1;
    }
    if (!checkKeys(rendered, selectedFirst, selectedSecond, selectedLast, full, palette)) {
        std::printf("FAILED: key squares\n");
        return 1;
    }
    if (!checkMarkerStates(markerSelected, markerHover, markerPlain, markerSelectedModel, palette)) {
        std::printf("FAILED: the marker states\n");
        return 1;
    }
    if (!checkProportionalMarker(
            proportionalMarkerRendered, proportionalMarkerControlRendered, proportionalMarker, palette
        )) {
        std::printf("FAILED: the proportional marker's fill\n");
        return 1;
    }
    if (!checkLabelHalo(rendered, full, fullBareRendered, palette)) {
        std::printf("FAILED: label halo\n");
        return 1;
    }
    if (!checkRulerReserve(rendered, full, tightRulerRendered, tightRuler, tightRulerBareRendered, palette)) {
        std::printf("FAILED: the frame number's reserved band\n");
        return 1;
    }
    if (!checkRulerRows(rulerRowRendered, rulerRowBareRendered, full, palette)) {
        std::printf("FAILED: the ruler's number and tick rows\n");
        return 1;
    }
    if (!checkPlayWindowOverlay(
            playWindowSplitRendered, playWindowSplit, playWindowMergedRendered, playWindowMerged, palette,
            theme::Colors::current()
        )) {
        std::printf("FAILED: the playback window overlay\n");
        return 1;
    }
    if (!checkChips(rendered, selectedFirst, selectedSecond, full, palette)) {
        std::printf("FAILED: the name chips\n");
        return 1;
    }
    if (!checkGhostHandle(ghostRendered, ghost, palette)) {
        std::printf("FAILED: the ghost handle\n");
        return 1;
    }
    if (!checkEmptyState(emptyRendered, empty)) {
        std::printf("FAILED: empty state\n");
        return 1;
    }
    if (!checkClip(clipRendered, clip)) {
        std::printf("FAILED: plot clip\n");
        return 1;
    }
    if (!checkZeroRule(rendered, noRuleRendered, full)) {
        std::printf("FAILED: the zero rule\n");
        return 1;
    }
    if (!checkRangeGuides(rendered, guideRendered, recededGuideRendered, guideModel)) {
        std::printf("FAILED: the frame range's guides\n");
        return 1;
    }
    if (!checkHandles(rendered, highlightRendered, full, palette, pointerDot)) {
        std::printf("FAILED: tangent handles\n");
        return 1;
    }
    if (!checkPlayhead(rendered, fullNoFrameRendered, full, fullNoFrame, palette)) {
        std::printf("FAILED: the playhead's extent\n");
        return 1;
    }
    if (!checkChipUnderPlayhead(chipUnderPlayheadRendered, chipUnderPlayhead, palette)) {
        std::printf("FAILED: the chips are drawn before the playhead\n");
        return 1;
    }
    if (!checkMarquee(rendered, marqueeRendered, fullMarquee, palette, kMarqueeRect)) {
        std::printf("FAILED: the box-select marquee\n");
        return 1;
    }
    if (!checkHandleFocus(rendered, focusedOtherRendered, full)) {
        std::printf("FAILED: the handles' focus rule\n");
        return 1;
    }
    if (!checkValueLabels(rendered, full, overflowRendered, overflow)) {
        std::printf("FAILED: the value column's gutter and its guard\n");
        return 1;
    }
    // 31: THE SCROLL BARS, on the passes that isolate them: the full model with the
    // four rects published, the SAME model with them empty (the state an empty plot
    // publishes), and hover passes that differ from the first only in the pointer - one
    // on each lane, so each direction of the hover rule is measured. A further pair is
    // the ZERO-TRAVEL state: both runs equal their tracks (the state the real scaler
    // publishes when the window covers its content), which must still paint a
    // full-length pill in BOTH lanes.
    gui::GraphPanelModel barModel = full;
    barModel.hTrack = kHScrollTrack;
    barModel.hThumb = kHScrollThumb;
    barModel.vTrack = kVScrollTrack;
    barModel.vThumb = kVScrollThumb;
    gui::GraphPanelModel noBarModel = full; // the four rects stay empty: nothing is drawn
    gui::GraphPanelModel fullBarModel = barModel;
    fullBarModel.hThumb = fullBarModel.hTrack;
    fullBarModel.vThumb = fullBarModel.vTrack;
    const Rendered barsRendered = renderPass(barModel, palette, focus, "scroll_bars");
    const Rendered noBarsRendered = renderPass(noBarModel, palette, focus, "scroll_bars_absent");
    const Rendered barsHoverHRendered = renderPass(
        barModel, palette, focus, "scroll_bars_hover_h",
        QPointF(kHScrollThumb.center())
    );
    const Rendered barsHoverVRendered = renderPass(
        barModel, palette, focus, "scroll_bars_hover_v",
        QPointF(kVScrollTrack.center().x(), kVScrollThumb.center().y())
    );
    if (!checkScrollBars(barsRendered, noBarsRendered, barsHoverHRendered, barsHoverVRendered, barModel)) {
        std::printf("FAILED: the scroll bars\n");
        return 1;
    }
    const Rendered fullLaneRendered = renderPass(fullBarModel, palette, focus, "scroll_bars_zero_travel");
    const Rendered fullLaneHoverHRendered = renderPass(
        fullBarModel, palette, focus, "scroll_bars_zero_travel_hover_h",
        QPointF(fullBarModel.hTrack.center())
    );
    const Rendered fullLaneHoverVRendered = renderPass(
        fullBarModel, palette, focus, "scroll_bars_zero_travel_hover_v",
        QPointF(fullBarModel.vTrack.center())
    );
    if (!checkFullLaneBars(
            fullLaneRendered, noBarsRendered, fullLaneHoverHRendered, fullLaneHoverVRendered, fullBarModel
        )) {
        std::printf("FAILED: the zero-travel scroll bars\n");
        return 1;
    }
    // 31b: THE RULER'S RULE, the opaque token both views draw on that row.
    if (!checkRulerHairline(rendered, full, palette)) {
        std::printf("FAILED: the ruler hairline's ink\n");
        return 1;
    }

    // THE TWO PORTABLE BACKENDS, on every state the panel gets into and on the ink fixture:
    // geometry and label presence must match the Qt rasteriser. This block is now the only
    // user of InkQPainter - the shipping path is InkCanvas, and asking the two rasterisers to
    // agree is what keeps a shape decision from being a rasteriser's accident.
    if (!renderBackendMatrix(palette, focus)) {
        std::printf("FAILED: the backend matrix disagrees in at least one state\n");
        return 1;
    }
    if (!rendBackendParity(ink, palette, focus)) {
        std::printf("FAILED: the portable backends disagree\n");
        return 1;
    }

    std::printf(
        "OK: the graph panel's pixel contract holds on the shipping Canvas Painter host (every assertion above"
        " printed its measurement)\n"
    );
    return 0;
}
