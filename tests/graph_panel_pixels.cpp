// The animation graph panel's PIXEL CONTRACT.
//
// This is the pixel gate of the ImGui panel (gui::drawPanel, src/gui/ImGuiOverlay.cpp)
// and the successor of tests/graph_render_pixels.cpp, which pinned the RASTER
// painter renderer (ctrl::graph::Renderer into a QImage). That renderer is deleted
// in the cutover, and the panel must not lose its pixel gate with it: this file
// drives the SHIPPING drawing path - a real ImGui frame, the real GL3 backend,
// the real framebuffer - and reads the pixels back with glReadPixels.
//
// WHAT IS DRIVEN. Not the application: no ctrl::GraphEditor, no document, no
// widget paint event. A SYNTHETIC gui::GraphPanelModel is built by hand from the
// model's own coordinates, and gui::drawPanel is handed it inside one ImGui
// frame. drawPanel is a pure function of (model, palette, focus), so the pixels
// it leaves in the framebuffer are the panel's pixels.
//
// WHAT IT PINS, and why each assertion is a regression gate:
//   1 no grid, no background. A rect inside the plot that holds none of the
//     model's geometry (verified against EVERY tick, label, curve sample, key,
//     playhead, zero rule and legend item the model publishes, each grown to its
//     own ink bound) must come back byte-identical to the clear colour. If a
//     background or a grid line is ever added, this is the assertion that fails.
//   2 the curve stroke lands where the model says: sampled points along the
//     synthetic polylines - including one between two key squares - carry ink.
//   3/5 the key MARKERS and the legend swatches are painted, and their colours
//     are the palette's own tokens (extraction stated at each site). A key is a
//     DOT: the ink is a disc of radius ctrl::graph::kKeyDotRadius about
//     PanelCurve::keyDots, ringed in theme::Graph::keyEdge(), and it is NOT the
//     editor's pick geometry - the corners of the key's 2*kKeyRadius HIT square
//     carry no ink, which is what fails if the drawn marker is ever the pick
//     rectangle again.
//   4 the label halo, behind gui::kDrawTextHalo (SHIPPED OFF): with the flag on, a
//     pixel 1-2px outside the advance box is inked and is closer to
//     theme::Graph::halo() than to theme::Graph::axisText(), so it is the halo and
//     not a glyph; with it off - which is what ships - nothing is painted outside
//     the glyph box at all, which is the same statement from the other side. The
//     flag is read from src/gui/ImGuiOverlay.h, so this file cannot drift from the
//     panel's own configuration.
//   6 the empty state: the reason is drawn, and the rest of the plot stays clean.
//   7 the plot clip: a curve that leaves the value window in y paints NOTHING
//     outside the plot - not into the value column, not below the panel.
//   8 the zero rule: present at zeroY, absent at zeroY == -1.
//   9 a legend row with NO ROOM (an empty swatch) has no label box, paints nothing
//     (the region its derived box would land in stays clean) and contributes no
//     geometry at all - the row is still published by the model, so the guard is
//     the only thing that keeps its empty rect from becoming a control at the
//     panel's corner.
//  10 the Custom tangent handles: each handle's stem carries ink at its own
//     midpoint and each dot is theme::Graph::handleFill() at its own coordinates;
//     with io.MousePos on one dot (a second pass that differs ONLY in the pointer)
//     that dot turns theme::Graph::selected() while every other dot is unchanged -
//     the highlight at the same distance and in the same token the editor's own
//     hit-test grabs through.
//  17 the playhead's own extent: the document's frame is ONE pixel column that
//     covers the plot's whole vertical span in theme::Graph::selected() and puts
//     nothing in the neighbouring columns - the "while you are there" half of the
//     ruler assertion below.
//  18 the box-select marquee, as a pair: the same model with and without
//     gui::GraphPanelModel::marquee, whose body must be
//     theme::Graph::marqueeFill() composited on what is under it and whose 1px edge
//     the edge token - and nothing at all where no drag is live.
//  20 the handles' COST rule: with a focus, only the focused channel draws its tangent
//     handles (the panel's vertex budget - the application-level diagnosis measured
//     ~68 vertices per key with them against ~16 without) - the same model with and
//     without the focus, whose vertex delta and cleared positions are both asserted.
//  21 THE VALUE COLUMN'S TWO OBLIGATIONS, measured as a pair of models: a right-aligned
//     value label keeps kValueLabelGap (8px) from its box's right edge when the box can
//     spare it - the gutter that stops the top value label and the plot's first frame
//     number reading as one string (the timeline's own kNumberMargin for the same
//     defect) - and a label WIDER than its box is cut at the panel's own edge instead of
//     painting outside the panel. The first is measured against the glyph box the
//     drawing layer used, the second against a model whose panel starts inside the
//     framebuffer.
//  16 the frame numbers' reserved band: every frame label's box is inside the plot and
//     clear of the tick band the panel draws, and the glyph box the panel actually
//     used (drawLabel's own clamped placement) is inside the plot - so a top row of
//     numbers that could be cut by the plot clip fails here instead.
//  11-15 THE CONTRAST OUTLINE AND COMPOSITE PASS (gui::outline), driven directly
//     with hand-authored textures: a low-contrast stroke over a bright patch is
//     ringed in black and the ring's strength follows the stroke's coverage; the
//     same stroke over a contrasting patch is left alone BYTE FOR BYTE; an empty
//     overlay leaves a patterned backdrop byte-identical through the pass's own
//     blit (the inverse "always draws" assertion); a 50%-covered pixel blends with
//     the backdrop instead of replacing it; and the optional +/-2px second ring
//     reaches a pixel the shipped 1px ring leaves alone. See the block above those
//     checks for what each one is a regression gate on.
//  11 ALSO PINS THE THRESHOLD'S OWN VALUE, which is the knob the ring's eagerness
//     lives in: a stroke at 2.30:1 is left alone at the shipped
//     gui::outline::kMinContrast (2.0) and ringed at 3.0 - the pair that says the
//     constant is 2.0 rather than merely "some value the two sample inks straddle".
//  22-26 THE CURVE STROKE (gui::stroke), which is the panel's ink for the channel
//     polylines and the thing the user reported as "jagged and inconsistent line
//     thickness". ImDrawList::AddPolyline builds the antialiasing into the VERTICES
//     (a fringe quad per segment), so the width a stroke reads at depends on the
//     angle of its segments; gui::stroke measures the coverage PER FRAGMENT from the
//     distance to each segment's centreline, accumulates the segments' capsules with
//     GL_MAX (so a join cannot be blended twice) and composites that union once. The
//     assertions measure the SHAPE, and each one is stated with both implementations'
//     numbers:
//      22 equal thickness across the angle: the ink's average perpendicular extent
//         over a run's own interior, at four angles (horizontal, half-pixel off the
//         grid, 45 degrees, ~72 degrees) AND along a dense wavy polyline sampled the
//         way the panel samples a curve, asserted to spread by no more than 1px. The
//         measured spread is 0.053px across the angles and 0.049px along the line -
//         for the fringe stroke as well, because at this width ImGui takes its
//         texture path whose ramp is already perpendicular to every segment. The
//         assertion is a gate on the new pass, not the demonstration; 24 is that.
//      23 antialiased edges: the coverage profile across the stroke's boundary - on
//         the run deliberately placed half a pixel off the grid, where an
//         unantialiased edge has no partial pixel at all - asserted to carry
//         intermediate coverage on BOTH sides of the body and to ramp monotonically
//         into it and out of it, with the column's whole ink equal to the nominal
//         width;
//      24 no double-darkening at a join: every pixel within 5px of a right-angle
//         vertex is asserted to carry no more coverage than ONE segment's capsule
//         allows at that pixel's own distance - measured with the curve DIMMED,
//         because on a dimmed curve a doubled blend is ink darker than the stroke's
//         own body and on a full-strength one it is invisible - and the same bound is
//         required to catch the fringe stroke, which is what makes the measurement a
//         discrimination rather than a tautology;
//      25 the ink is still where the polyline is: every sample of every run lands on
//         an inked pixel, the frame's whole ink stays inside the runs' own bounds, the
//         coverage-weighted centre of the stroke's cross-section is on the centreline,
//         and the ENDS are the capsule's own round caps - the pixels beside an endpoint
//         carry ink (0.79 measured) where the fringe stroke's square-ended strip
//         carries none (0.00), which is what measuring the distance to the SEGMENT
//         rather than to its infinite line buys;
//      26 the pass is a guest in the frame (the same rule as the outline's own
//         assertion), driven from the very command ImGui recorded for the frame - so
//         the assertion that the pass is IN the draw list, between the ticks and the
//         key markers, is the same one that finds it;
//      27 the panel's OTHER 1px lines (the ticks, the zero rule, the playhead and the
//         handle stems), MEASURED FIRST and left unchanged: the three that are drawn
//         with ImGui's line AA explicitly off are only ever asked for an INTEGER
//         column (frameTickX/valueTickY/zeroY/playheadX are all `int` in the model), and
//         at an integer column the hard-edged line is exact - one column at full
//         coverage, its neighbours at zero, the column the model named, its ink centre
//         on that column's centre. Routing them through the coverage pass would put
//         0.500 in each of the two columns a boundary divides (the pass's own function,
//         which assertion 24 verifies against the drawn ink), i.e. it would trade one
//         crisp column for a two-column grey line - so this assertion pins the
//         measurement, and fails if someone makes that trade without re-deriving it.
//         The handle stem - 1px with AA ON and FRACTIONAL coordinates - was measured
//         first and WAS WRONG (its ink sat +0.68/+0.75/+0.76px off the requested line
//         at three stations, because ImGui's AddLine shifts geometry by (+0.5,+0.5), up
//         to 0.71px perpendicular on a diagonal; the pass measures -0.01/-0.00/+0.01px),
//         so it is now drawn by the same
//         coverage pass as the curves: same colour, same 1px width, same order, same
//         clip. The assertion re-measures the result - the ink's centroid within 0.5px
//         of the line, partial coverage at its edges, and 1px of ink per px of stem,
//         which is what a 1px stroke must put down and what a stroke that loses ink
//         cannot pass.
//     The two implementations are driven out of ONE process: the ink model is rendered
//     with the pass unbuilt (BEFORE: ImDrawList::AddPolyline) and with it built (AFTER),
//     and the crops under build/shots/graph_ink/ are those same two frames magnified
//     with no resampling.
//
// WHAT THIS FILE CANNOT SEE. The key menu is the APPLICATION'S OWN QMenu now
// (gui::MainDisplayWidget::showGraphKeyMenu -> the timeline's mSelectEasing), a
// native popup window drawn by Qt outside this framebuffer: there is no ImGui popup
// left to open, measure or click here, and the pixel gate owns no widget, no
// timeline and no document to build one from. What it can still say about the menu
// is what it said before - nothing, in pixels; the route itself (a right-click
// within ctrl::graph::kKeyHitRadius of a key's centre selects that key and calls the
// widget) is the code path's business, and the application-level probe's.
//
// Every number is printed as `MEASURED <what> ...` so a failure states the
// measurement, not only the verdict, and every precondition is asserted so a run
// that could not observe the behaviour fails loudly instead of passing quietly.
//
// HOW IT OWNS ITS PIXELS. The same bootstrap as tests/overlay_engine_primitives.cpp:
// a real core-profile QOpenGLWidget context (QSurfaceFormat + gl::Global::kVersion),
// gl::Global::setContext/setFunctions over QOpenGLFunctions_4_0_Core, and this
// file's OWN framebuffer, read with glReadPixels. Never QOpenGLWidget::grabFramebuffer(),
// which re-enters Qt's render and invalidates the colour buffer. The widget's
// paint event is never involved; run it under xvfb-run.
//
// The viewport is 400x320 while the panel is 400x300: the 20 rows below the panel
// are the visible "below the panel" probe of assertion 7.
#include <QApplication>
#include <QByteArray>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QOpenGLFunctions_4_0_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLWidget>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QSurfaceFormat>
#include <QVector>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_opengl3.h"

// ImDrawListSharedData, i.e. the frame's font and font size that gui::drawPanel
// reads off the draw list itself (src/gui/ImGuiOverlay.cpp does the same). The
// label geometry below has to be measured with THE SAME font the glyphs are
// rasterized with, or "outside the glyph box" would be a statement about a box
// nobody drew.
#include "imgui_internal.h"

#include "XCAssert.h"
#include "gl/BufferObject.h"
#include "gl/DeviceInfo.h"
#include "gl/EasyShaderProgram.h"
#include "gl/Framebuffer.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/VertexArrayObject.h"

#include "gui/GraphPanelModel.h"
#include "gui/ImGuiOverlay.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Graph.h"
#include "ctrl/graph/graph_Geometry.h"

// The ctrl/core archives this target links reference the GUI
// AudioPlaybackWidget::deserialize (core::Project.cpp uses it for a .aemus
// sidecar). Nothing here ever loads a project, so stub it with the same one-liner
// tests/overlay_engine_primitives.cpp uses and the target needs no gui library.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

// The overlay hands its key menu to the WIDGET it lives in
// (gui::MainDisplayWidget::showGraphKeyMenu). There is no gui library to link and this
// target's source list carries no widget (see the target's own comment), so the ONE
// symbol the overlay references is stubbed here - the same move the
// AudioPlaybackWidget stub above makes, and for the same reason: the gate drives
// gui::drawPanel and gui::outline directly, never a widget and never an ImGuiOverlay,
// so the stub is never called. A signature change in the real method fails this
// target's LINK rather than passing quietly.
#include "gui/MainDisplayWidget.h"
void gui::MainDisplayWidget::showGraphKeyMenu(const core::TimeLineEvent&, const QPoint&) {}

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
const QRect kPlot(90, 30, 290, 240);        // x 90..379, y 30..269
// The value column, in the model's OWN relationship to the plot: its right edge one
// pixel left of the plot's (GraphPanelModel.h: "It never overlaps the plot"), which is
// what puts the value numbers where the real panel puts them.
const QRect kValueColumn(6, 30, 83, 240);   // x 6..88, clear of the plot

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

// An OPAQUE fill (a key square's body, a legend swatch, the key ring) is the
// palette token itself, so it may be compared tightly.
constexpr int kFillTol = 10;

// An antialiased 1.5px stroke lands with partial coverage on its own fringes, so
// the curve colour is searched for in a small ring with a looser tolerance.
constexpr int kCurveTol = 48;

enum class Align { Left, Centre, Right };

gl::Framebuffer* gFbo = nullptr;

//-------------------------------------------------------------------------------------------------
// Read the whole attachment back as a TOP-DOWN RGBA image, with the FBO bound.
// GL's origin is bottom-left, so glReadPixels hands back the bottom row first:
// without the flip every sampled pixel below would come from the mirrored row.
QImage readFboTopDown() {
    QImage image(kViewW, kViewH, QImage::Format_RGBA8888);
    gl::Global::functions().glReadPixels(0, 0, kViewW, kViewH, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    return image.flipped(Qt::Vertical);
}

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

// How many pixel columns/rows separate two rects; 0 when they touch or overlap.
int rectGap(const QRect& a, const QRect& b) {
    const int dx = std::max(0, std::max(a.left() - b.right() - 1, b.left() - a.right() - 1));
    const int dy = std::max(0, std::max(a.top() - b.bottom() - 1, b.top() - a.bottom() - 1));
    return std::max(dx, dy);
}

// The glyph box drawLabel's own placement produces: the same font, the same
// alignment, the same CLAMPED vertical centring and the same right-gap rule as
// src/gui/ImGuiOverlay.cpp's drawLabel() (the glyphs are centred in the box, never
// above its top edge; a right-aligned label is pulled left by up to aRightGapPx,
// never by more than the box's spare width).
// Rebuilding it here is what lets the halo assertion say "this pixel is OUTSIDE the
// glyph box" rather than "somewhere near the text".
QRect glyphBox(
    const QRect& aBox, const QString& aText, ImFont* aFont, float aFontSize, Align aAlign, int aRightGapPx = 0
) {
    if (!aFont || aText.isEmpty() || aBox.isEmpty()) {
        return QRect();
    }
    const QByteArray utf8 = aText.toUtf8();
    const ImVec2 size = aFont->CalcTextSizeA(aFontSize, FLT_MAX, 0.0f, utf8.constData());
    float x = float(aBox.left());
    if (aAlign == Align::Centre) {
        x += (float(aBox.width()) - size.x) * 0.5f;
    } else if (aAlign == Align::Right) {
        const float spare = std::max(0.0f, float(aBox.width()) - size.x);
        x = float(aBox.left() + aBox.width()) - size.x - std::min(float(aRightGapPx), spare);
    }
    // The same clamp drawLabel applies: an over-tall font hangs into the box's own
    // interior instead of out of its top edge.
    const float y = float(aBox.top()) + std::max((float(aBox.height()) - size.y) * 0.5f, 0.0f);
    return QRect(int(std::floor(x)), int(std::floor(y)), int(std::ceil(size.x)), int(std::ceil(size.y)));
}

// The glyph box of a legend row's label: legendLabelRect()'s own formula (the
// swatch's right edge + kLegendLabelGap, vertically centred on the swatch) and
// then the left-aligned glyph box inside it.
QRect legendLabelGlyphBox(const gui::PanelLegendRow& aRow, ImFont* aFont, float aFontSize) {
    if (!aFont || aRow.label.isEmpty()) {
        return QRect();
    }
    const QByteArray utf8 = aRow.label.toUtf8();
    const ImVec2 size = aFont->CalcTextSizeA(aFontSize, FLT_MAX, 0.0f, utf8.constData());
    const int width = int(size.x + 0.5f);
    const int height = int(size.y + 0.5f);
    if (width <= 0 || height <= 0) {
        return QRect();
    }
    const QRect box(aRow.swatch.right() + 1 + 6, aRow.swatch.center().y() - height / 2, width, height);
    return glyphBox(box, aRow.label, aFont, aFontSize, Align::Left);
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
// the centre, grown by 1px for the 1px edge ring and 1px more for the
// antialiasing, so nothing the marker can paint falls outside it.
QRect keyDotInkBox(const QPointF& aDot) {
    const int radius = int(std::ceil(double(ctrl::graph::kKeyDotRadius))) + 2; // 3.75 -> 6
    return QRect(int(aDot.x()) - radius, int(aDot.y()) - radius, 2 * radius + 1, 2 * radius + 1);
}

gui::GraphPanelModel makeFullModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kPlot.top();   // no occluded top: the band starts at the plot

    model.frameTickX = {90, 140, 320};
    model.valueTickY = {70, 210};
    // THE CONGESTED CORNER, in the model's own box arithmetic: the first frame number
    // sits on the tick at the plot's left edge (so its box is nudged just inside it,
    // GraphPanelModel.cpp's buildFrameAxis) and the top value label is right-aligned in
    // its box - 7px apart, on the same rows, which is the string "10" assertion 21 is
    // about. The other two frame numbers are the ordinary mid-plot case.
    model.frameLabels = {
        {QRect(90, 40, 11, 14), QStringLiteral("0")},
        {QRect(130, 40, 20, 14), QStringLiteral("10")},
        {QRect(310, 40, 20, 14), QStringLiteral("20")},
    };
    model.valueLabels = {
        {valueLabelBox(60), QStringLiteral("1")},
        {valueLabelBox(240), QStringLiteral("0")},
    };
    model.zeroY = 180;
    model.playheadX = 200;

    const KeyFixture aKeys[] = {keyAt(150, 220), keyAt(250, 200)};
    gui::PanelCurve a;
    a.node = nullptr;
    a.channel = ctrl::graph::Channel(kTypeA, 0);
    a.points = {QPointF(110, 240), QPointF(150, 220), QPointF(200, 230), QPointF(250, 200), QPointF(300, 210)};
    a.keys = {aKeys[0].hit, aKeys[1].hit};
    a.keyDots = {aKeys[0].dot, aKeys[1].dot};
    a.keyFrames = {core::Frame(150), core::Frame(250)};
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
    a.color = aTheme.keyColor(kTypeA);
    a.legendIndex = 0;

    const KeyFixture bKeys[] = {keyAt(170, 140), keyAt(290, 130)};
    gui::PanelCurve b;
    b.node = nullptr;
    b.channel = ctrl::graph::Channel(kTypeB, 0);
    b.points = {QPointF(110, 120), QPointF(170, 140), QPointF(230, 100), QPointF(290, 130)};
    b.keys = {bKeys[0].hit, bKeys[1].hit};
    b.keyDots = {bKeys[0].dot, bKeys[1].dot};
    b.keyFrames = {core::Frame(170), core::Frame(290)};
    b.color = aTheme.keyColor(kTypeB);
    b.legendIndex = 1;

    model.curves = {a, b};

    gui::PanelLegendRow rowA;
    rowA.node = nullptr;
    rowA.channel = a.channel;
    rowA.label = QStringLiteral("Move X");
    rowA.color = a.color;
    rowA.swatch = QRect(96, 244, 10, 10);
    rowA.visible = true;

    gui::PanelLegendRow rowB;
    rowB.node = nullptr;
    rowB.channel = b.channel;
    rowB.label = QStringLiteral("Scale X");
    rowB.color = b.color;
    rowB.swatch = QRect(96, 256, 10, 10);
    rowB.visible = true;

    model.legend = {rowA, rowB};
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
    model.rulerBandTop = kPlot.top();   // no occluded top: the band starts at the plot

    gui::PanelCurve curve;
    curve.node = nullptr;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    // 650px above and 230px below a 240px-tall plot: the two steep segments leave
    // the plot's y range immediately, and the third runs across its middle.
    curve.points = {QPointF(110, -500), QPointF(150, 150), QPointF(250, 150), QPointF(290, 500)};
    curve.color = aTheme.keyColor(kTypeA);
    curve.legendIndex = -1;
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
    model.rulerBandTop = kPlot.top();   // no occluded top: the band starts at the plot
    model.frameTickX = {140};
    model.valueTickY = {70};
    model.frameLabels = {{QRect(130, 40, 20, 14), QStringLiteral("10")}};
    model.valueLabels = {{valueLabelBox(60), QStringLiteral("1")}};
    model.emptyState = QStringLiteral("No keys on the selected layers");
    return model;
}

//-------------------------------------------------------------------------------------------------
struct Rendered {
    QImage image;
    int vtx = 0;
    int idx = 0;
    int cmdLists = 0;
    ImFont* font = nullptr;
    float fontSize = 0.0f;
};

// One whole frame through the real backend, into this file's own framebuffer.
// The order is the one gui::ImGuiOverlay::paint() uses and it is load-bearing in
// 1.92: ImGui_ImplOpenGL3_NewFrame() (the caller's duty, it creates the backend's
// device objects) BEFORE ImGui::NewFrame().
Rendered renderPass(
    const gui::GraphPanelModel& aModel, const theme::Graph& aTheme, const gui::PanelFocus& aFocus,
    const char* aName, const ImVec2& aMouse = ImVec2(-FLT_MAX, -FLT_MAX), float aFontSize = 0.0f,
    bool aQuiet = false
) {
    auto& funcs = gl::Global::functions();

    gFbo->bind();
    // What ImGui_ImplOpenGL3_RenderDrawData does NOT restore (its scissor box and
    // viewport are left as the last draw command set them). A glClear() is itself
    // scissored, so both are re-established before the clear.
    funcs.glDisable(GL_SCISSOR_TEST);
    funcs.glScissor(0, 0, kViewW, kViewH);
    funcs.glViewport(0, 0, kViewW, kViewH);
    funcs.glClearColor(kClear.redF(), kClear.greenF(), kClear.blueF(), 1.0f);
    funcs.glClear(GL_COLOR_BUFFER_BIT);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(float(kViewW), float(kViewH));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    // drawPanel highlights the tangent handle nearest io.MousePos, so the pointer
    // is part of the frame this harness drives. Off-screen by default (no
    // highlight); the handle assertion passes a dot's own coordinates.
    io.MousePos = aMouse;

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    // A DRAWING FONT TALLER THAN THE MODEL'S BOXES, for the one pass that needs it:
    // the panel measures its own text with the font the DRAW LIST carries, so this is
    // exactly the "the layer's font is not the model's font" case, made explicit.
    // Left alone (0) for every other pass, and reset by the next NewFrame.
    if (aFontSize > 0.0f) {
        drawList->_Data->FontSize = aFontSize;
    }
    gui::drawPanel(*drawList, aModel, aTheme, aFocus);

    Rendered out;
    out.font = drawList->_Data ? drawList->_Data->Font : nullptr;
    out.fontSize = drawList->_Data ? drawList->_Data->FontSize : 0.0f;

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    out.vtx = drawData->TotalVtxCount;
    out.idx = drawData->TotalIdxCount;
    out.cmdLists = drawData->CmdListsCount;
    ImGui_ImplOpenGL3_RenderDrawData(drawData);

    out.image = readFboTopDown();
    gFbo->release();

    if (!aQuiet) {
        std::printf(
            "MEASURED pass=%s vtx=%d idx=%d cmd_lists=%d font_px=%d\n", aName, out.vtx, out.idx, out.cmdLists,
            int(std::lround(out.fontSize))
        );
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// WHAT A FRAME OF THIS PANEL COSTS, one number per stroke geometry: whole frames, timed, each
// ending in the harness's own glReadPixels (which is a GPU sync), so the figure is the full
// draw-measure-read-back cycle rather than a submission time - and the structural cost is
// stated beside it (the scratch's size, and the GL calls the pass adds per curve). The first
// frame is untimed because it is the one that creates the pass's GL objects.
struct FrameCost {
    double ms = 0.0;
    int vtx = 0;
    int idx = 0;
};

FrameCost timeFrames(
    const gui::GraphPanelModel& aModel, const theme::Graph& aTheme, const gui::PanelFocus& aFocus, int aFrames
) {
    FrameCost cost;
    renderPass(aModel, aTheme, aFocus, "cost_warmup", ImVec2(-FLT_MAX, -FLT_MAX), 0.0f, true);
    QElapsedTimer timer;
    timer.start();
    Rendered last;
    for (int i = 0; i < aFrames; ++i)
        last = renderPass(aModel, aTheme, aFocus, "cost", ImVec2(-FLT_MAX, -FLT_MAX), 0.0f, true);
    cost.ms = double(timer.nsecsElapsed()) / 1.0e6 / double(aFrames);
    cost.vtx = last.vtx;
    cost.idx = last.idx;
    return cost;
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
            // The stroke's own half extent, PLUS one pixel of slack: kCurveWidth is
            // 2.0px and ImGui's antialiased stroke reaches
            // thick/2 + 1 = 2px (ImDrawList::AddPolyline's half_draw_size for the
            // non-texture thick path) or thick/2 + 1 = 2px on the texture path, so a
            // 5x5 box around the floored sample grown by 1 is a bound, never a guess.
            aOut.push_back({"curve", grow(QRect(int(std::floor(x)) - 2, int(std::floor(y)) - 2, 5, 5), 1)});
        }
    }
}

std::vector<Geom> geometryOf(
    const gui::GraphPanelModel& aModel, ImFont* aFont, float aFontSize
) {
    std::vector<Geom> out;
    for (int x : aModel.frameTickX) {
        out.push_back({"frame_tick", grow(QRect(x, aModel.plot.top() - 1, 1, 6), 2)});
    }
    for (int y : aModel.valueTickY) {
        out.push_back({"value_tick", grow(QRect(aModel.plot.left() - 1, y, 6, 1), 2)});
    }
    for (const gui::PanelLabel& label : aModel.frameLabels) {
        out.push_back({"frame_label", grow(glyphBox(label.rect, label.text, aFont, aFontSize, Align::Centre), 2)});
    }
    for (const gui::PanelLabel& label : aModel.valueLabels) {
        out.push_back(
            {"value_label",
             grow(glyphBox(label.rect, label.text, aFont, aFontSize, Align::Right, gui::kValueLabelGap), 2)}
        );
    }
    if (aModel.zeroY >= 0) {
        out.push_back(
            {"zero_rule", grow(QRect(aModel.plot.left(), aModel.zeroY, aModel.plot.width() + 1, 1), 2)}
        );
    }
    if (aModel.playheadX >= 0) {
        out.push_back({"playhead", grow(QRect(aModel.playheadX, aModel.plot.top(), 1, aModel.plot.height() + 1), 2)});
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
    for (const gui::PanelLegendRow& row : aModel.legend) {
        out.push_back({"legend_swatch", grow(row.swatch, 2)});
        out.push_back({"legend_label", grow(legendLabelGlyphBox(row, aFont, aFontSize), 2)});
    }
    if (!aModel.emptyState.isEmpty()) {
        out.push_back(
            {"empty_state",
             grow(glyphBox(aModel.plot, aModel.emptyState, aFont, aFontSize, Align::Centre), 2)}
        );
    }
    return out;
}

//-------------------------------------------------------------------------------------------------
// 0: the machinery's own control. drawPanel returns immediately for a model with
// no panel rect, so a pass that draws nothing must count nothing - otherwise the
// readback or the counter invents pixels and every number below is worthless.
bool checkControl(const Rendered& aRendered) {
    const int ink = inkCount(aRendered.image, QRect(0, 0, kViewW, kViewH));
    std::printf("MEASURED control ink_pixels=%d of %d\n", ink, kViewW * kViewH);
    REQUIRE(
        aRendered.vtx == 0, "the control pass emitted %d vertices for a model with no panel", aRendered.vtx
    );
    REQUIRE(ink == 0, "the control pass painted %d pixels while nothing was drawn", ink);
    return true;
}

// 1: the inverse no-grid assertion.
bool checkNoGrid(const Rendered& aRendered, const gui::GraphPanelModel& aModel) {
    REQUIRE(aRendered.font != nullptr, "the draw list carried no font: the label geometry cannot be described");
    REQUIRE(!aModel.panel.isEmpty() && !aModel.plot.isEmpty(), "precondition: the model must have a plot");

    // A rect inside the plot, in the top-right quadrant, chosen against the model
    // below: the nearest thing to it is the second frame label's halo.
    const QRect probe(340, 200, 30, 30);
    REQUIRE(kPlot.contains(probe), "precondition: the probe rect must be inside the plot");
    REQUIRE(kPanel.contains(probe), "precondition: the probe rect must be inside the panel");
    REQUIRE(!probe.intersects(aModel.valueColumn), "precondition: the probe rect must be outside the value column");

    const std::vector<Geom> geoms = geometryOf(aModel, aRendered.font, aRendered.fontSize);
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
// the pixel, and within 1px where the point is a polyline VERTEX: ImGui's
// antialiased stroke quad does not fully cover its own terminal vertex pixel
// (measured: (300,210) at the end of curve A comes back clear while every
// interior sample, including the ones between two key squares, is 79..120 away
// from the clear colour). The tolerance is stated per sample and printed with the
// offset that was actually found, so "within 1px of the model's point" is the
// claim being made - not "somewhere on the panel".
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
// PanelCurve::keyDots, tinted by its channel, ringed in theme::Graph::keyEdge(), and
// NOT the pick square.
//
// EXTRACTION: drawPanel paints a key with AddCircleFilled(curve.color) +
// AddCircle(theme::Graph::keyEdge(), thickness 1) about the marker centre, and
// PanelCurve::color is theme::Graph::keyColor(channel.type()) - the same colour the
// curve is stroked in and the legend swatch is filled with (the model documents them
// as one). So the marker's CENTRE pixel is compared to its own key colour and the
// pixel one ring-width inside its edge to keyEdge, and - because "the key is tinted
// by its channel" is a statement about a RELATION - each centre is also required to
// be nearer its own channel's colour than the other curve's, with the two channels'
// colours asserted distinguishable first. A single hardcoded ink cannot satisfy that:
// it is the two-channels-at-once check.
//
// AND THE MARKER IS NOT THE PICK AREA (the model's visual/hit split). Three
// measurements say so, on the geometry the model still publishes for both:
//   * the ink's WIDTH through the centre is the marker's own diameter (2 *
//     kKeyDotRadius plus the 1px ring and its antialiasing) - 7..9px, where the
//     2*kKeyRadius hit square is 10;
//   * the four CORNERS of that hit square carry NO ink (they are ~7px from the
//     centre, past the marker's 4.25px of ink, and inside a 10px square's);
//   * the ring pixel's COLOUR is the edge token, not the fill - which is also what
//     the ring is for: a same-hue key on a same-hue stroke would otherwise vanish
//     into it.
//
// The selected-key token is deliberately not what this model paints: the focus the
// gate hands drawPanel is invalid (no focus, no selection), so every key is drawn in
// its own colour - see the file header.
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

bool checkKeys(const Rendered& aRendered, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme) {
    REQUIRE(aModel.curves.size() == 2, "precondition: the key assertion needs the two curves of the full model");
    const QColor colourA = aTheme.keyColor(aModel.curves.at(0).channel.type());
    const QColor colourB = aTheme.keyColor(aModel.curves.at(1).channel.type());
    REQUIRE(
        colourDistance(colourA, colourB) > kFillTol,
        "precondition: the two channels' key colours are indistinguishable (rgb=%s and rgb=%s, distance %d)",
        hex(colourA).c_str(), hex(colourB).c_str(), colourDistance(colourA, colourB)
    );
    REQUIRE(
        colourDistance(colourA, aTheme.keyEdge()) > kFillTol
            && colourDistance(colourB, aTheme.keyEdge()) > kFillTol,
        "precondition: a key colour is not distinguishable from theme::Graph::keyEdge() rgb=%s",
        hex(aTheme.keyEdge()).c_str()
    );

    // The marker's own two bounds, from the constant the drawing layer uses:
    // the drawn radius and the 1px ring around it, plus the antialiasing of both.
    const int dotRadius = int(std::lround(double(ctrl::graph::kKeyDotRadius)));   // 4
    const int ringStep = dotRadius - 1;                                          // the ring's own band
    const int minWidth = 2 * dotRadius - 1;                                      // 7: the drawn diameter, less AA
    const int maxWidth = 2 * dotRadius + 1;                                      // 9: ... plus the AA fringe

    int keys = 0;
    int tintedByOwn = 0;
    for (const gui::PanelCurve& curve : aModel.curves) {
        const QColor own = aTheme.keyColor(curve.channel.type());
        const QColor other = (curve.channel.type() == aModel.curves.at(0).channel.type()) ? colourB : colourA;
        REQUIRE(
            curve.keyDots.size() == curve.keys.size() && curve.keyFrames.size() == curve.keys.size(),
            "precondition: a curve's keyDots/keyFrames are not parallel to its keys (%d/%d/%d)",
            int(curve.keys.size()), int(curve.keyDots.size()), int(curve.keyFrames.size())
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
            const QColor inner = pixelAt(aRendered.image, centre.x(), centre.y());
            const QPoint ringPixel(centre.x() + ringStep, centre.y());
            const QColor ring = pixelAt(aRendered.image, ringPixel.x(), ringPixel.y());

            int negX = 0;
            int posX = 0;
            int negY = 0;
            int posY = 0;
            const int widthX = inkWidthThrough(aRendered.image, centre, 1, 0, 12, negX, posX);
            const int widthY = inkWidthThrough(aRendered.image, centre, 0, 1, 12, negY, posY);
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
                if (clearDistance(pixelAt(aRendered.image, corner.x(), corner.y())) > kInkMin) {
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
                "MEASURED key channel_type=%d dot=(%d,%d) hit_rect=(%d,%d %dx%d) centre_rgb=%s keyColor=%s d=%d"
                " other_keyColor=%s d=%d ring=(%d,%d) ring_rgb=%s keyEdge=%s d_ring_to_edge=%d d_ring_to_fill=%d"
                " width_x=%d (%d/%d) width_y=%d (%d/%d) dot_radius=%d ring_step=%d hit_corner_ink=%d\n",
                int(curve.channel.type()), centre.x(), centre.y(), hit.left(), hit.top(), hit.width(),
                hit.height(), hex(inner).c_str(), hex(own).c_str(), colourDistance(inner, own), hex(other).c_str(),
                colourDistance(inner, other), ringPixel.x(), ringPixel.y(), hex(ring).c_str(),
                hex(aTheme.keyEdge()).c_str(), colourDistance(ring, aTheme.keyEdge()),
                colourDistance(ring, own), widthX, negX, posX, widthY, negY, posY, dotRadius, ringStep, cornerInk
            );

            REQUIRE(
                clearDistance(inner) > kInkMin, "the key marker at (%d,%d) has no ink at its centre: rgb=%s",
                centre.x(), centre.y(), hex(inner).c_str()
            );
            REQUIRE(
                colourDistance(inner, own) <= kFillTol,
                "the key centre rgb=%s is not its channel's colour rgb=%s (channel type %d, distance %d > %d):"
                " the key is not tinted by its channel",
                hex(inner).c_str(), hex(own).c_str(), int(curve.channel.type()), colourDistance(inner, own), kFillTol
            );
            // The RING: the pixel one ring-width inside the marker's edge is nearer
            // theme::Graph::keyEdge() than the key's own fill - the 1px circle stroke
            // sits at radius-1..radius, so that pixel is mostly the edge colour over
            // the fill's own inner part. "Nearer the edge than the fill" is the whole
            // claim (a missing ring would make it pure fill, which fails it).
            REQUIRE(
                clearDistance(ring) > kInkMin,
                "the key marker at (%d,%d) has no ink at its ring pixel (%d,%d)", centre.x(), centre.y(),
                ringPixel.x(), ringPixel.y()
            );
            REQUIRE(
                colourDistance(ring, aTheme.keyEdge()) < colourDistance(ring, own),
                "the key ring pixel rgb=%s is nearer its own fill rgb=%s than theme::Graph::keyEdge() rgb=%s"
                " (%d against %d): the marker has no edge ring",
                hex(ring).c_str(), hex(own).c_str(), hex(aTheme.keyEdge()).c_str(), colourDistance(ring, own),
                colourDistance(ring, aTheme.keyEdge())
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

// 4: the label, and the halo behind gui::kDrawTextHalo.
//
// The halo is SHIPPED OFF, so what this asserts in the shipping configuration is
// the INVERSE: nothing is painted outside the glyph box. The haloed assertions
// below it are kept whole for the flag-on run - and which of the two runs is the
// shipped one is a property of the flag itself (gui::kDrawTextHalo, declared in
// src/gui/ImGuiOverlay.h), so this file cannot drift from the panel.
bool checkLabelHalo(const Rendered& aRendered, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme) {
    REQUIRE(!aModel.frameLabels.isEmpty(), "precondition: the model must carry a frame label");
    const gui::PanelLabel& label = aModel.frameLabels.at(0);
    REQUIRE(kPlot.contains(label.rect), "precondition: the label box must be inside the plot");

    const QRect glyph = glyphBox(label.rect, label.text, aRendered.font, aRendered.fontSize, Align::Centre);
    REQUIRE(!glyph.isEmpty(), "precondition: the frame font lays out '%s' with empty metrics", qPrintable(label.text));
    REQUIRE(kPlot.contains(glyph), "precondition: the glyph box must be inside the plot");

    // (a) the glyph itself is inked.
    std::vector<QColor> glyphInk;
    for (int y = glyph.top(); y <= glyph.bottom(); ++y) {
        for (int x = glyph.left(); x <= glyph.right(); ++x) {
            const QColor c = pixelAt(aRendered.image, x, y);
            if (clearDistance(c) > kInkMin) {
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
            if (glyph.contains(x, y) || !kPlot.contains(x, y)) {
                continue;
            }
            const QColor c = pixelAt(aRendered.image, x, y);
            if (clearDistance(c) <= kInkMin) {
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
        // SHIPPED: the halo is off, so the panel paints NOTHING outside a glyph box.
        // This is the assertion that catches the halo being re-enabled by accident,
        // and the one that pins "the ink protection is the outline pass's job now":
        // that protection is a separate pass with its own gate (assertions 11-15).
        REQUIRE(
            haloPixels == 0,
            "the halo flag is off but %d pixels 1-2px outside the glyph box carry ink (nearest halo distance %d):"
            " the label is still haloed",
            haloPixels, bestHaloDistance
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

// 16: THE FRAME NUMBERS' RESERVED BAND. The X-axis numbers are clipped at the plot's
// top edge if their glyphs start above it, and two things keep that from happening:
// the model puts every frame label's box BELOW the tick band (its own 4px marks), and
// the drawing layer clamps a { font taller than the box } into the box instead of
// letting it hang out of the top. This asserts both halves on the drawn pass: the
// boxes are inside the plot and clear of the tick band, and the glyph box the panel
// actually used (rebuilt by drawLabel's own rule, clamp included) is inside the plot -
// so a label the panel cannot draw inside the plot would fail here rather than being
// quietly cut by the clip.
bool checkRulerReserve(
    const Rendered& aRendered, const gui::GraphPanelModel& aModel, const Rendered& aTight,
    const gui::GraphPanelModel& aTightModel
) {
    REQUIRE(aRendered.font != nullptr, "the draw list carried no font: the ruler geometry cannot be described");
    REQUIRE(!aModel.frameLabels.isEmpty(), "precondition: the full model must carry frame labels");
    REQUIRE(!aModel.frameTickX.isEmpty(), "precondition: the full model must carry frame ticks");
    // The band is where the drawing layer starts the tick marks, so it has to be a
    // real line inside the plot (the model guarantees it; a model that forgot it would
    // draw the ruler at y = -1).
    REQUIRE(
        aModel.rulerBandTop >= kPlot.top() && aModel.rulerBandTop <= kPlot.bottom() + 1,
        "precondition: the ruler band top (%d) is outside the plot (%d..%d)", aModel.rulerBandTop, kPlot.top(),
        kPlot.bottom()
    );

    // The drawing layer's own tick length: the model documents 4px and the panel
    // draws exactly that (kAxisTickLength in src/gui/ImGuiOverlay.cpp, where the
    // model's kFrameTickLength is the model's half of the same convention).
    constexpr int kDrawnTickLength = 4;
    int checked = 0;
    for (const gui::PanelLabel& label : aModel.frameLabels) {
        REQUIRE(
            kPlot.contains(label.rect),
            "the frame label '%s' box (%d,%d %dx%d) is not inside the plot: a number outside the plot's top would"
            " be cut by the plot clip",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height()
        );
        REQUIRE(
            label.rect.top() >= kPlot.top() + kDrawnTickLength,
            "the frame label '%s' box starts at y=%d, inside the %dpx tick band that starts at the plot's top edge"
            " (%d): the numbers are not clear of the ruler's own marks",
            qPrintable(label.text), label.rect.top(), kDrawnTickLength, kPlot.top()
        );
        const QRect glyph = glyphBox(label.rect, label.text, aRendered.font, aRendered.fontSize, Align::Centre);
        REQUIRE(!glyph.isEmpty(), "precondition: the frame font lays out '%s' with empty metrics", qPrintable(label.text));
        REQUIRE(
            kPlot.contains(glyph),
            "the frame label '%s' draws its glyph box (%d,%d %dx%d) outside the plot: the top row of numbers is cut"
            " by the plot clip",
            qPrintable(label.text), glyph.left(), glyph.top(), glyph.width(), glyph.height()
        );
        const int ink = inkCount(aRendered.image, grow(glyph, 1));
        std::printf(
            "MEASURED ruler_reserve label=%s box=(%d,%d %dx%d) glyph=(%d,%d %dx%d) above_plot_top=%dpx"
            " below_tick_band=%dpx ink_pixels=%d\n",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(),
            glyph.left(), glyph.top(), glyph.width(), glyph.height(), glyph.top() - kPlot.top(),
            glyph.top() - (kPlot.top() + kDrawnTickLength), ink
        );
        REQUIRE(ink > 0, "the frame label '%s' painted no ink inside its own glyph box", qPrintable(label.text));
        ++checked;
    }
    REQUIRE(checked == aModel.frameLabels.size(), "walked %d of %d frame labels", checked,
            int(aModel.frameLabels.size()));

    // ---- AND THE CASE THE CLAMP EXISTS FOR: A FONT TALLER THAN THE BOX ----
    //
    // The model measures its boxes with the default UI font and draws with the
    // widget's, so a layer font with a taller line height puts a glyph cell bigger
    // than the box. Centred, half of that overflow goes ABOVE the box - i.e. above
    // the reserved band, into the plot's top edge, where the plot clip cuts it, which
    // is the "X-axis numbers clipped at the top" defect. drawLabel therefore clamps
    // the glyph to the box's top edge and lets the overflow hang into the box's own
    // interior instead. This pass is that configuration: the FULL model with every
    // frame label's box 8px tall, which is shorter than the draw list's font.
    //
    // MEASURED both ways round (the number in the message is the count of inked
    // pixels in the box's own columns ABOVE its top edge): with the clamp the count is
    // 0, and with the clamp removed the same model paints the glyph's upper rows
    // there. That is the pair this assertion is.
    int tightChecked = 0;
    for (const gui::PanelLabel& label : aTightModel.frameLabels) {
        const QByteArray utf8 = label.text.toUtf8();
        const ImVec2 size = aTight.font->CalcTextSizeA(aTight.fontSize, FLT_MAX, 0.0f, utf8.constData());
        REQUIRE(
            size.y > float(label.rect.height()),
            "precondition: the tight model's box (%dpx) is not shorter than the font (%.1fpx), so this is not the"
            " over-tall case",
            label.rect.height(), double(size.y)
        );
        const QRect glyph = glyphBox(label.rect, label.text, aTight.font, aTight.fontSize, Align::Centre);
        // The clamp's own statement: the glyph box starts at or below the box's top.
        REQUIRE(
            glyph.top() >= label.rect.top(),
            "the frame label '%s' draws its glyph box at y=%d, above its own box's top edge (y=%d): a taller font"
            " overflows out of the box and into the plot's top edge",
            qPrintable(label.text), glyph.top(), label.rect.top()
        );
        // And the pixels: the band between the plot's top edge and the box's top edge,
        // over the label's own columns, holds no GLYPH ink. The tick marks' own columns
        // are excluded - the panel draws its 4px ticks down from the plot's top edge,
        // and a tick that shares a column with the label is the ruler's ink, not the
        // number's (the tick's extent is the drawing layer's own convention, asserted
        // in checkRulerReserve's first half).
        int ink = 0;
        int tickPixels = 0;
        for (int x = label.rect.left(); x <= label.rect.right(); ++x) {
            bool tickColumn = false;
            for (const int tickX : aTightModel.frameTickX) {
                if (std::abs(x - tickX) <= 1) {
                    tickColumn = true;
                }
            }
            for (int y = kPlot.top(); y < label.rect.top(); ++y) {
                if (clearDistance(pixelAt(aTight.image, x, y)) > kInkMin) {
                    if (tickColumn) {
                        ++tickPixels;
                    } else {
                        ++ink;
                    }
                }
            }
        }
        const QRect above(label.rect.left(), kPlot.top(), label.rect.width(), label.rect.top() - kPlot.top());
        std::printf(
            "MEASURED ruler_tight label=%s box=(%d,%d %dx%d) font_line_height=%d/100 glyph_top=%d"
            " above_box_band=(%d,%d %dx%d) glyph_ink_pixels=%d tick_ink_pixels=%d\n",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(),
            int(size.y * 100.0f), glyph.top(), above.left(), above.top(), above.width(), above.height(), ink,
            tickPixels
        );
        REQUIRE(
            ink == 0,
            "the frame label '%s' painted %d pixels above its own box (rows %d..%d, the ticks' columns excluded):"
            " a taller font is not clamped into the box, so the plot clip would cut the top of the number",
            qPrintable(label.text), ink, above.top(), above.bottom()
        );
        ++tightChecked;
    }
    REQUIRE(tightChecked > 0, "precondition: the tight model must carry frame labels");
    return true;
}

// 5: the legend swatches. EXTRACTION: the swatch is AddRectFilled(row.color), so
// its centre is compared to row.color AND to theme::Graph::keyColor(row.channel.type())
// - the model documents them as the same colour, and this is where that is checked.
bool checkLegend(const Rendered& aRendered, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme) {
    REQUIRE(aModel.legend.size() == 2, "precondition: the full model must carry two legend rows");
    for (const gui::PanelLegendRow& row : aModel.legend) {
        REQUIRE(kPlot.contains(row.swatch), "precondition: a legend swatch lies outside the plot");
        REQUIRE(!row.swatch.isEmpty(), "precondition: a legend swatch is empty");
        const QPoint centre = row.swatch.center();
        const QColor c = pixelAt(aRendered.image, centre.x(), centre.y());
        const QColor keyColour = aTheme.keyColor(row.channel.type());

        std::printf(
            "MEASURED legend_swatch label=%s rect=(%d,%d %dx%d) centre_rgb=%s row_color=%s d=%d keyColor=%s d=%d\n",
            qPrintable(row.label), row.swatch.left(), row.swatch.top(), row.swatch.width(), row.swatch.height(),
            hex(c).c_str(), hex(row.color).c_str(), colourDistance(c, row.color), hex(keyColour).c_str(),
            colourDistance(c, keyColour)
        );
        REQUIRE(
            clearDistance(c) > kInkMin, "the legend swatch '%s' has no ink at its centre", qPrintable(row.label)
        );
        REQUIRE(
            colourDistance(c, row.color) <= kFillTol,
            "the legend swatch '%s' centre rgb=%s is not the row colour rgb=%s (distance %d > %d)",
            qPrintable(row.label), hex(c).c_str(), hex(row.color).c_str(), colourDistance(c, row.color), kFillTol
        );
        REQUIRE(
            colourDistance(c, keyColour) <= kFillTol,
            "the legend swatch '%s' centre rgb=%s is not theme::Graph::keyColor() rgb=%s (distance %d > %d)",
            qPrintable(row.label), hex(c).c_str(), hex(keyColour).c_str(), colourDistance(c, keyColour), kFillTol
        );

        // The whole swatch, not just its centre: every row's swatch must be filled.
        const int ink = inkCount(aRendered.image, row.swatch);
        std::printf(
            "MEASURED legend_swatch_fill label=%s ink_pixels=%d of %d\n", qPrintable(row.label), ink,
            row.swatch.width() * row.swatch.height()
        );
        REQUIRE(
            ink == row.swatch.width() * row.swatch.height(),
            "the legend swatch '%s' is not filled: %d of %d pixels carry ink", qPrintable(row.label), ink,
            row.swatch.width() * row.swatch.height()
        );
    }
    return true;
}

// 6: the empty state.
bool checkEmptyState(const Rendered& aRendered, const gui::GraphPanelModel& aModel) {
    REQUIRE(aModel.curves.isEmpty() && !aModel.emptyState.isEmpty(), "precondition: the model must be the empty one");
    REQUIRE(aRendered.font != nullptr, "the draw list carried no font: the empty state cannot be located");

    // The state text is drawn centred in the PLOT.
    const QRect text = glyphBox(aModel.plot, aModel.emptyState, aRendered.font, aRendered.fontSize, Align::Centre);
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
    const std::vector<Geom> geoms = geometryOf(aModel, aRendered.font, aRendered.fontSize);
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
            && aModel.valueTickY.isEmpty() && aModel.legend.isEmpty() && aModel.playheadX < 0 && aModel.zeroY < 0,
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

// 9: a legend row with NO ROOM (an empty swatch, GraphPanelModel's LEGEND note).
//
// The trap this pins is geometric, not cosmetic: QRect().right() is -1 and a null
// rect's centre is (0,0), so a label box derived from an empty swatch would land
// near (6, -height/2) - the panel's top-left corner - and be both a label sliver
// nobody drew and a hit box at a place no control is. The three statements below
// are the whole rule: the row has no label geometry (gui::legendLabelRect), nothing
// is painted for it (its pass draws strictly less, and the would-be sliver's region
// stays clean), and the row is not silently lost (the accessible summary names it -
// checked in the report, since this gate has no accessibility layer).
bool checkNoRoomRow(
    const Rendered& aLaidOut, const Rendered& aNoRoom, const gui::GraphPanelModel& aModel, ImFont* aFont,
    float aFontSize
) {
    REQUIRE(aModel.legend.size() == 2, "precondition: the no-room model must carry two legend rows");
    const gui::PanelLegendRow& laidOut = aModel.legend.at(0);
    const gui::PanelLegendRow& noRoom = aModel.legend.at(1);
    REQUIRE(!laidOut.swatch.isEmpty(), "precondition: the first row must have a swatch");
    REQUIRE(noRoom.swatch.isEmpty(), "precondition: the second row must have no swatch at all");

    // The control: a row WITH a swatch has a label box...
    const QRect laidOutBox = gui::legendLabelRect(laidOut, aFont, aFontSize);
    REQUIRE(!laidOutBox.isEmpty(), "control: a row with a swatch has no label box");

    // ...and a row WITHOUT one has none, which is what takes the phantom hit box
    // (press()/doubleClick() test exactly this rect) out of existence.
    const QRect noRoomBox = gui::legendLabelRect(noRoom, aFont, aFontSize);
    REQUIRE(noRoomBox.isEmpty(), "a row with no swatch still has a label box: %d,%d %dx%d",
            noRoomBox.left(), noRoomBox.top(), noRoomBox.width(), noRoomBox.height());

    // The region the naive derivation would have painted into: the block of the
    // panel left of the plot and above the first value label. Nothing the model
    // publishes is legitimately there - the value column's labels sit at y=60 and
    // y=240, the ticks start at the plot's edges, and every curve is clipped to the
    // plot - so ink here would be the sliver and nothing else.
    const QRect sliverBand(0, 0, kPlot.left(), 24);
    const int sliverInkWithRoom = inkCount(aLaidOut.image, sliverBand);
    const int sliverInkNoRoom = inkCount(aNoRoom.image, sliverBand);
    REQUIRE(
        sliverInkWithRoom == 0 && sliverInkNoRoom == 0,
        "the no-room row's derived label box painted at the panel's corner: %d px with the row laid out, %d without",
        sliverInkWithRoom, sliverInkNoRoom
    );

    // And the whole row contributes nothing: the two passes differ only in that
    // row's swatch, so the row with no room must draw strictly less (its swatch's
    // two rects and its label's glyph passes are gone).
    REQUIRE(
        aNoRoom.vtx < aLaidOut.vtx && aNoRoom.idx < aLaidOut.idx,
        "a row with no swatch still contributed geometry: %d/%d vertices/indices vs %d/%d",
        aNoRoom.vtx, aNoRoom.idx, aLaidOut.vtx, aLaidOut.idx
    );

    std::printf(
        "MEASURED no_room row=%s swatch_empty=1 label_box=%d,%d %dx%d control_box=%d,%d %dx%d"
        " sliver_band=(%d,0 %dx24) ink_with_room=%d ink_no_room=%d vtx_with_room=%d vtx_no_room=%d"
        " delta_vtx=%d\n",
        qPrintable(noRoom.label), noRoomBox.left(), noRoomBox.top(), noRoomBox.width(), noRoomBox.height(),
        laidOutBox.left(), laidOutBox.top(), laidOutBox.width(), laidOutBox.height(), sliverBand.left(),
        sliverBand.width(), sliverInkWithRoom, sliverInkNoRoom, aLaidOut.vtx, aNoRoom.vtx,
        aLaidOut.vtx - aNoRoom.vtx
    );
    return true;
}

// 10: the Custom easing's tangent handles. The panel publishes each handle as a
// stem (base -> dot) and a dot, and the drawing layer must put ink at exactly
// those coordinates - and highlight the dot the pointer is on, in the same token
// and at the same distance the editor's hit-test grabs through.
//
// The pair is the whole rule: with the pointer OFF (the default pass, io.MousePos
// off-screen) every dot is drawn in theme::Graph::handleFill() and the stem in
// theme::Graph::handleLine(); with the pointer ON one dot's centre (a second pass
// that differs ONLY in io.MousePos) that dot is theme::Graph::selected() while
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
            // to the line the stem is meant to draw - a pixel that only had ink because
            // ImGui's AddLine shifts its geometry by (+0.5,+0.5) and its fringe smears over
            // 1.5px, i.e. the old probe was measuring the misplaced edge of the stem rather
            // than the stem. Flooring picks the pixel whose centre is at most half a pixel
            // from the midpoint, which is where a 1px stroke's own core must be.
            const QPoint mid(
                int(std::floor((handle.base.x() + handle.dot.x()) * 0.5)),
                int(std::floor((handle.base.y() + handle.dot.y()) * 0.5))
            );
            const QColor stem = pixelAt(aOff.image, mid.x(), mid.y());
            const QColor dotOff = pixelAt(aOff.image, dot.x(), dot.y());
            const QColor dotOn = pixelAt(aOn.image, dot.x(), dot.y());

            const int dotDistance = colourDistance(dotOff, aTheme.handleFill());
            const int highlightDistance = colourDistance(dotOn, aTheme.selected());
            const int moved = colourDistance(dotOff, dotOn);
            const bool pointed = dot == aPointerDot;

            std::printf(
                "MEASURED handle base=(%d,%d) dot=(%d,%d) radius=%d stem_mid=(%d,%d) stem_rgb=%s stem_clear=%d"
                " stem_to_handleLine=%d line=%s dot_rgb=%s handleFill=%s d=%d dot_on_rgb=%s selected=%s d=%d"
                " delta=%d pointed=%d\n",
                int(handle.base.x()), int(handle.base.y()), dot.x(), dot.y(), handle.radius, mid.x(), mid.y(),
                hex(stem).c_str(), clearDistance(stem), colourDistance(stem, aTheme.handleLine()),
                hex(aTheme.handleLine()).c_str(), hex(dotOff).c_str(), hex(aTheme.handleFill()).c_str(),
                dotDistance, hex(dotOn).c_str(), hex(aTheme.selected()).c_str(), highlightDistance, moved,
                int(pointed)
            );
            // The pixel carrying the midpoint is within 0.707px of the stem's centreline
            // (its centre is at most half a pixel away in each axis), and a 1px stroke's
            // coverage there is at least 1 - 0.707 = 0.293, so the recorded colour is the
            // stem's own colour to within that much of the clear colour - which is what
            // kCurveTol (the file's own "an antialiased stroke's fringe" tolerance) admits.
            REQUIRE(clearDistance(stem) > kInkMin, "the handle stem has no ink at its midpoint (%d,%d): rgb=%s",
                    mid.x(), mid.y(), hex(stem).c_str());
            REQUIRE(colourDistance(stem, aTheme.handleLine()) <= kCurveTol,
                    "the handle stem's own midpoint (pixel %d,%d) is %s, %d from the stem's own colour %s: the stem"
                    " is not drawn where the model puts it",
                    mid.x(), mid.y(), hex(stem).c_str(), colourDistance(stem, aTheme.handleLine()),
                    hex(aTheme.handleLine()).c_str());
            REQUIRE(dotDistance <= kFillTol,
                    "the handle dot at (%d,%d) rgb=%s is not theme::Graph::handleFill() rgb=%s (distance %d > %d)",
                    dot.x(), dot.y(), hex(dotOff).c_str(), hex(aTheme.handleFill()).c_str(), dotDistance, kFillTol);

            if (pointed) {
                // The dot under the pointer is the highlighted one: it moved between
                // the passes and it is now theme::Graph::selected().
                REQUIRE(moved > 0, "the dot under the pointer (%d,%d) did not change between the two passes",
                        dot.x(), dot.y());
                REQUIRE(highlightDistance <= kFillTol,
                        "the dot under the pointer (%d,%d) rgb=%s is not theme::Graph::selected() rgb=%s"
                        " (distance %d > %d)",
                        dot.x(), dot.y(), hex(dotOn).c_str(), hex(aTheme.selected()).c_str(), highlightDistance,
                        kFillTol);
                ++highlighted;
            } else {
                // Every other dot is untouched: the two passes differ only in
                // io.MousePos, so a change here would be the highlight leaking.
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
// as one 1px column from the plot's top edge to its bottom edge, in
// theme::Graph::selected(). This is the "while you are there" half of the ruler
// assertion: the space the frame numbers are reserved is the plot's top band, and
// the playhead is the other mark that starts exactly there - so its extent is
// measured rather than assumed, i.e. it must reach the plot's first row, stop at its
// last, be exactly one pixel wide and put nothing outside.
bool checkPlayhead(const Rendered& aRendered, const gui::GraphPanelModel& aModel, const theme::Graph& aTheme) {
    REQUIRE(aModel.playheadX >= 0, "precondition: the full model must publish a playhead");
    REQUIRE(kPlot.contains(aModel.playheadX, kPlot.top()), "precondition: the playhead column must be inside the plot");

    // Every row of the plot's own vertical span, on the playhead's column.
    int missingRows = 0;
    int firstRow = -1;
    int lastRow = -1;
    for (int y = kPlot.top(); y <= kPlot.bottom(); ++y) {
        const QColor c = pixelAt(aRendered.image, aModel.playheadX, y);
        const bool isPlayhead = colourDistance(c, aTheme.selected()) <= kFillTol;
        if (!isPlayhead) {
            ++missingRows;
            if (firstRow < 0) {
                firstRow = y;
            }
            lastRow = y;
        }
    }
    // A row with no playhead colour can only be one that carries SOME ink (a curve, a
    // key, the zero rule): the playhead is drawn last, over all of them, so a row it
    // did not paint is a row it does not reach.
    const int widthProbeRow = kPlot.top() + 30; // clear of the frame labels and of every curve
    const QColor left = pixelAt(aRendered.image, aModel.playheadX - 1, widthProbeRow);
    const QColor right = pixelAt(aRendered.image, aModel.playheadX + 1, widthProbeRow);
    std::printf(
        "MEASURED playhead x=%d span=%d..%d rows_without_playhead_colour=%d first=%d last=%d"
        " probe_row=%d left_clear_distance=%d right_clear_distance=%d\n",
        aModel.playheadX, kPlot.top(), kPlot.bottom(), missingRows, firstRow, lastRow, widthProbeRow,
        clearDistance(left), clearDistance(right)
    );
    REQUIRE(
        missingRows == 0,
        "the playhead does not cover the plot's whole column at x=%d: %d rows lack it (first %d, last %d)",
        aModel.playheadX, missingRows, firstRow, lastRow
    );
    REQUIRE(
        clearDistance(left) <= kInkMin && clearDistance(right) <= kInkMin,
        "the playhead is wider than one pixel: the neighbouring columns at row %d carry ink (%d and %d from the"
        " clear colour)",
        widthProbeRow, clearDistance(left), clearDistance(right)
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

    const std::vector<Geom> geoms = geometryOf(aModel, aPlain.font, aPlain.fontSize);
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
    REQUIRE(aRendered.font != nullptr, "the draw list carried no font: the label geometry cannot be described");
    REQUIRE(aModel.valueLabels.size() == 2, "precondition: the full model must carry two value labels");

    // ---- the gutter, label by label ----
    int checked = 0;
    int pulled = 0;
    for (const gui::PanelLabel& label : aModel.valueLabels) {
        const QByteArray utf8 = label.text.toUtf8();
        const ImVec2 size = aRendered.font->CalcTextSizeA(aRendered.fontSize, FLT_MAX, 0.0f, utf8.constData());
        const float flushRight = float(label.rect.left() + label.rect.width()) - size.x;
        const QRect glyph =
            glyphBox(label.rect, label.text, aRendered.font, aRendered.fontSize, Align::Right, gui::kValueLabelGap);
        const int shift = int(std::lround(flushRight)) - glyph.left();
        const float spare = float(label.rect.width()) - size.x;
        const int expected = int(std::min(float(gui::kValueLabelGap), spare));
        std::printf(
            "MEASURED value_label text=%s box=(%d,%d %dx%d) text_width=%d spare=%d flush_right_x=%d glyph_x=%d"
            " shift_left=%d expected=%d gap=%d\n",
            qPrintable(label.text), label.rect.left(), label.rect.top(), label.rect.width(), label.rect.height(),
            int(std::lround(size.x)), int(std::lround(spare)), int(std::lround(flushRight)), glyph.left(), shift,
            expected, gui::kValueLabelGap
        );
        REQUIRE(
            spare > float(gui::kValueLabelGap),
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
    const QRect frameGlyph = glyphBox(frame.rect, frame.text, aRendered.font, aRendered.fontSize, Align::Centre);
    int bestGap = -1;
    QString bestLabel;
    for (const gui::PanelLabel& label : aModel.valueLabels) {
        const QRect glyph =
            glyphBox(label.rect, label.text, aRendered.font, aRendered.fontSize, Align::Right, gui::kValueLabelGap);
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
        glyphBox(aModel.valueLabels.at(0).rect, aModel.valueLabels.at(0).text, aRendered.font, aRendered.fontSize,
                 Align::Right, gui::kValueLabelGap)
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
    const QByteArray utf8 = overflowing.text.toUtf8();
    const ImVec2 size = aGuarded.font->CalcTextSizeA(aGuarded.fontSize, FLT_MAX, 0.0f, utf8.constData());
    REQUIRE(
        size.x > float(overflowing.rect.width()),
        "precondition: the guarded model's label (%dpx of text in a %dpx box) is not wider than its box",
        int(std::lround(size.x)), overflowing.rect.width()
    );

    // Where right-alignment alone would have put it, and where it may paint: the text
    // starts LEFT of the panel's own edge without the clip.
    const QRect flush = glyphBox(overflowing.rect, overflowing.text, aGuarded.font, aGuarded.fontSize, Align::Right);
    const QRect guarded =
        glyphBox(overflowing.rect, overflowing.text, aGuarded.font, aGuarded.fontSize, Align::Right, gui::kValueLabelGap);
    const QRect outsidePanel(0, 0, aGuardedModel.panel.left(), kViewH);
    const int outsideInk = inkCount(aGuarded.image, outsidePanel);
    const int insideInk = inkCount(aGuarded.image, aGuardedModel.panel);
    std::printf(
        "MEASURED value_label_guard panel_left=%d text=%s text_width=%d box=(%d,%d %dx%d) flush_x=%d glyph_x=%d"
        " outside_panel_ink=%d inside_panel_ink=%d\n",
        aGuardedModel.panel.left(), qPrintable(overflowing.text), int(std::lround(size.x)), overflowing.rect.left(),
        overflowing.rect.top(), overflowing.rect.width(), overflowing.rect.height(), flush.left(), guarded.left(),
        outsideInk, insideInk
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

    std::printf(
        "MEASURED handle_focus unfocused_vtx=%d focused_other_vtx=%d delta_vtx=%d unfocused_idx=%d"
        " focused_other_idx=%d delta_idx=%d\n",
        aUnfocused.vtx, aFocusedOther.vtx, aUnfocused.vtx - aFocusedOther.vtx, aUnfocused.idx,
        aFocusedOther.idx, aUnfocused.idx - aFocusedOther.idx
    );
    REQUIRE(
        aFocusedOther.vtx < aUnfocused.vtx && aFocusedOther.idx < aUnfocused.idx,
        "focusing a curve drew no fewer vertices: the unfocused curve's handles are still being submitted"
        " (%d/%d against %d/%d)",
        aFocusedOther.vtx, aFocusedOther.idx, aUnfocused.vtx, aUnfocused.idx
    );

    // ...and the pixels agree: the unfocused curve's handle geometry is GONE (nothing
    // is left where its stems and dots were). Each position is checked against the
    // unfocused pass first, so a position that is clean in BOTH would be a probe that
    // cannot see a handle at all.
    //
    // THE STEM PROBE IS THE PIXEL THAT CONTAINS THE MIDPOINT (floor, not rounding each
    // coordinate): the stem is a 1px stroke drawn by the coverage pass, so its ink is on the
    // line the model asked for and nowhere else - while the rounded probe (193,243) for the
    // midpoint (192.5,242.5) has its own centre 1.37px off that line, where a 1px stroke puts
    // no ink at all. Same re-derivation, same reason as checkHandles' stem probe.
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

//-------------------------------------------------------------------------------------------------
// THE CONTRAST OUTLINE AND COMPOSITE PASS (gui::outline, src/gui/ImGuiOverlay.cpp).
//
// WHAT IS DRIVEN. The pass itself, with two textures this file authors by hand: the
// OVERLAY (the panel as the ImGui backend leaves it - rgb PREMULTIPLIED by the
// coverage in alpha, because the backend blends source-over into a framebuffer
// cleared to zero) and the BACKDROP (the pixels the panel would sit on). The
// backdrop goes through the pass's own copy - copyBackdrop, a glBlitFramebuffer -
// from a source framebuffer, so the copy is covered as well as the composite.
//
// Every assertion here is a regression gate on a MEASURED decision:
//   11 a low-contrast stroke over a BRIGHT patch gets a black ring, and the ring's
//      strength follows the stroke's COVERAGE: the same ink at 40% gets a 40% ring.
//      This is the whole point of the pass - and the coverage is the ring's alpha,
//      which is a property of the TAPS and not of the pixel.
//   12 the same stroke over a CONTRASTING patch gets NO ring: the pixels around it
//      are byte-identical to the backdrop, and the stroke's own interior is the
//      ink - i.e. the pass is invisible where it is not needed.
//   13 the inverse "always draws" assertion: with NO overlay coverage anywhere, the
//      WHOLE framebuffer comes back byte-identical to a patterned backdrop, through
//      the blit. A pass that painted anything unconditionally cannot pass this.
//   14 the composite is not gated at alpha >= 1.0: a 50%-covered overlay pixel
//      blends with the backdrop and lands between the two (the >= 1.0 failure mode
//      would make it either replace the backdrop or vanish).
//   15 the +/-2px second ring (outline::Params::ringRadiusPx), which is the
//      extension for a ring that reads too thin: with it on, a pixel two away from
//      the stroke is ringed where the shipped 1px ring leaves the backdrop alone.
//   11 ALSO PINS THE THRESHOLD'S OWN VALUE, which is the knob the ring's eagerness
//      lives in and the one the user's "the ring shows up everywhere" report is
//      about. The shader rings where the contrast is BELOW the constant, so a higher
//      constant rings more: 3.0 (WCAG's large-text minimum) ringed white ink over
//      every backdrop darker than grey 149, which is most mid-tones; 2.0 rings it
//      only over grey 183 and brighter, i.e. only where the ink is nearly lost. The
//      pair below is a stroke BETWEEN the two constants (2.30:1, measured in linear
//      light): untouched at 2.0, ringed at 3.0.

// The authored geometry, in the pass's own texture space. The readback is aligned
// with it: uploadTexture flips for GL's bottom-up rows, and readFboTopDown
// un-flips the attachment again.
const QRect kBrightPatch(20, 30, 61, 131);     // x 20..80, y 30..160
const QRect kSolidStroke(40, 40, 10, 10);      // x 40..49, y 40..49
const QRect kThresholdStroke(60, 40, 10, 10);  // 2.30:1 over the bright patch
const QRect kPartialStroke(40, 140, 10, 10);   // the same ink at 40% coverage
const QRect kContrastStroke(40, 240, 10, 10);  // a contrasting ink over the dark area
const QRect kHalfPixel(240, 40, 10, 10);       // a 50%-covered overlay pixel

const QColor kBright(200, 200, 200);  // the patch the low-contrast inks sit on
const QColor kDark(60, 60, 60);       // the rest of the backdrop
const QColor kLowInk(180, 180, 180);  // ~1.24:1 against kBright: far below both
const QColor kWhiteInk(255, 255, 255); // ~11:1 against kDark: far above both
// The ink BETWEEN the two candidate thresholds. WCAG contrast in LINEAR light,
// computed from the sRGB channels (the shader's own luma): 2.30:1 against kBright -
// above the shipped 2.0 (no ring) and below the 3.0 this shipped with (ring).
const QColor kThresholdInk(130, 130, 130);

// The 40% stroke's alpha byte: 102/255 is exactly 0.4, which keeps every expected
// value below an exact number rather than a rounded one.
constexpr int kPartialAlpha = 102;
constexpr int kHalfAlpha = 128; // 128/255, the 50% pixel

QImage blankImage(const QColor& aFill) {
    QImage image(kViewW, kViewH, QImage::Format_RGBA8888);
    image.fill(aFill);
    return image;
}

// The overlay texel that an ImGui draw of aColour at aCoverage leaves behind: the
// backend's glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, ...)
// makes dst.rgb = src.rgb * src.a into a cleared target, so the rgb is
// PREMULTIPLIED. That is a property of the pass's INPUT, not an assumption of the
// pass: dividing it back out is what the shader has to do.
QColor overlayInk(const QColor& aColour, int aAlpha) {
    const auto premultiply = [aAlpha](int aChannel) {
        return int(std::lround(double(aChannel) * double(aAlpha) / 255.0));
    };
    return QColor(premultiply(aColour.red()), premultiply(aColour.green()), premultiply(aColour.blue()), aAlpha);
}

void fillRect(QImage& aImage, const QRect& aRect, const QColor& aColour) {
    for (int y = aRect.top(); y <= aRect.bottom(); ++y) {
        for (int x = aRect.left(); x <= aRect.right(); ++x)
            aImage.setPixelColor(x, y, aColour);
    }
}

// A backdrop that is DIFFERENT in every pixel, so "byte-identical" is a real
// statement about the whole pass and not about a flat fill.
QImage patternBackdrop() {
    QImage image(kViewW, kViewH, QImage::Format_RGBA8888);
    for (int y = 0; y < kViewH; ++y) {
        for (int x = 0; x < kViewW; ++x) {
            image.setPixelColor(
                x, y, QColor((x * 7 + y * 3) & 0xFF, (x * 5 + y * 11) & 0xFF, (x * 13 + y * 2) & 0xFF, 255)
            );
        }
    }
    return image;
}

// The authored images are TOP-DOWN, GL's texture rows are bottom-up, and the
// readback below is un-flipped by readFboTopDown: uploading the image FLIPPED is
// what makes pixelAt(readback, x, y) the authored (x, y) instead of its mirror.
//
// It re-uploads into the EXISTING texture object rather than calling
// gl::Texture::create() again: create() deletes the texture and generates a new
// one, and a framebuffer that was attached to the old name would then be reading a
// deleted texture (measured: the blit becomes a no-op and the backdrop comes back
// black).
void uploadTexture(gl::Texture& aTexture, const QImage& aTopDown) {
    const QImage bottomUp = aTopDown.flipped(Qt::Vertical);
    auto& funcs = gl::Global::functions();
    funcs.glBindTexture(GL_TEXTURE_2D, aTexture.id());
    funcs.glTexImage2D(
        GL_TEXTURE_2D, 0, GL_RGBA8, aTopDown.width(), aTopDown.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
        bottomUp.constBits()
    );
    funcs.glBindTexture(GL_TEXTURE_2D, 0);
}

// The harness's own GL-error gate: a blit or an upload that failed silently would
// otherwise be measured as "the pass produced black".
bool noGlError(const char* aWhere) {
    const GLuint error = gl::Global::functions().glGetError();
    if (error != GL_NO_ERROR) {
        std::printf("FAIL: GL error 0x%x %s\n", error, aWhere);
        return false;
    }
    return true;
}

// The pass's inputs and targets, at the size of this file's framebuffer. The
// backdrop is uploaded into a SOURCE framebuffer and then COPYBACKDROP-ed into the
// texture the pass reads, which is the shipping path (the widget's framebuffer
// becomes the backdrop) with a hand-authored pattern in place of the artwork.
struct OutlineHarness {
    gl::Texture overlayTexture;
    gl::Texture backdropSourceTexture;
    gl::Framebuffer backdropSourceFbo;
    gl::Texture backdropTexture;
    gl::Framebuffer backdropFbo;

    bool init() {
        const QSize size(kViewW, kViewH);
        overlayTexture.create(size);
        backdropSourceTexture.create(size);
        backdropTexture.create(size);
        // gl::Texture::create leaves the parameters at GL's defaults, and the
        // default MIN filter is a MIPMAP one: with no mipmaps the texture is
        // INCOMPLETE and samples as (0,0,0,1). The pass samples both 1:1 at the
        // texel centre, and CLAMP_TO_EDGE is what keeps a tap one pixel outside the
        // first column from wrapping to the last one.
        for (gl::Texture* texture : {&overlayTexture, &backdropSourceTexture, &backdropTexture}) {
            texture->setFilter(GL_NEAREST);
            texture->setWrap(GL_CLAMP_TO_EDGE);
        }
        backdropSourceFbo.setColorAttachment(0, backdropSourceTexture.id());
        backdropFbo.setColorAttachment(0, backdropTexture.id());
        return backdropSourceFbo.isComplete() && backdropFbo.isComplete();
    }

    // The uploads and the backdrop BLIT - everything before the pass itself, so a
    // caller that wants to arrange the state around the composite call alone can.
    bool prepare(const QImage& aBackdrop, const QImage& aOverlay) {
        uploadTexture(overlayTexture, aOverlay);
        uploadTexture(backdropSourceTexture, aBackdrop);
        if (!noGlError("uploading the pass's textures"))
            return false;
        gui::outline::copyBackdrop(backdropSourceFbo.id(), backdropFbo.id(), QSize(kViewW, kViewH));
        return noGlError("copyBackdrop");
    }

    // One whole composite: upload, blit, run the pass, read back. The framebuffer
    // is cleared to the clear colour first, so a pass that drew NOTHING leaves
    // magenta behind rather than passing quietly.
    QImage run(const QImage& aBackdrop, const QImage& aOverlay, const gui::outline::Params& aParams) {
        if (!prepare(aBackdrop, aOverlay))
            return QImage();

        const QSize size(kViewW, kViewH);
        auto& funcs = gl::Global::functions();
        gFbo->bind();
        funcs.glDisable(GL_SCISSOR_TEST);
        funcs.glScissor(0, 0, kViewW, kViewH);
        funcs.glViewport(0, 0, kViewW, kViewH);
        funcs.glClearColor(kClear.redF(), kClear.greenF(), kClear.blueF(), 1.0f);
        funcs.glClear(GL_COLOR_BUFFER_BIT);

        gui::outline::composite(overlayTexture.id(), backdropTexture.id(), size, aParams);
        if (!noGlError("the composite pass"))
            return QImage();

        const QImage image = readFboTopDown();
        gFbo->release();
        return image;
    }
};

// Whether every byte of two images agrees. The identity assertions are about the
// pass being a no-op, so they are byte comparisons and not tolerance tests.
bool imagesEqual(const QImage& a, const QImage& b) {
    if (a.size() != b.size() || a.format() != b.format())
        return false;
    for (int y = 0; y < a.height(); ++y) {
        const uchar* left = a.constScanLine(y);
        const uchar* right = b.constScanLine(y);
        if (std::memcmp(left, right, size_t(4 * a.width())) != 0)
            return false;
    }
    return true;
}

// The backdrop the ring assertions use: a bright patch in the top-left quadrant,
// dark everywhere else.
QImage ringBackdrop() {
    QImage image = blankImage(kDark);
    for (int y = kBrightPatch.top(); y <= kBrightPatch.bottom(); ++y) {
        for (int x = kBrightPatch.left(); x <= kBrightPatch.right(); ++x)
            image.setPixelColor(x, y, kBright);
    }
    return image;
}

// 11 + 12 + 14 + 15: ONE overlay, one backdrop, five statements about the ring and
// the composite (the threshold's own value included, at the end). The strokes are
// 10x10 blocks with integer edges, so every sampled pixel is decided by an exact tap
// and not by anti-aliasing.
bool checkOutlineRing(OutlineHarness& aHarness) {
    QImage overlay = blankImage(QColor(0, 0, 0, 0));
    fillRect(overlay, kSolidStroke, overlayInk(kLowInk, 255));
    fillRect(overlay, kThresholdStroke, overlayInk(kThresholdInk, 255));
    fillRect(overlay, kPartialStroke, overlayInk(kLowInk, kPartialAlpha));
    fillRect(overlay, kContrastStroke, overlayInk(kWhiteInk, 255));
    fillRect(overlay, kHalfPixel, overlayInk(kWhiteInk, kHalfAlpha));

    const QImage backdrop = ringBackdrop();
    gui::outline::Params params;
    params.colour = QColor(0, 0, 0);

    const QImage solid = aHarness.run(backdrop, overlay, params);

    // The pixel one column LEFT of each stroke: for the solid and the 40% stroke it
    // sits on the bright patch, and the only ink its taps can see is the stroke.
    const QColor ringSolid = pixelAt(solid, kSolidStroke.left() - 1, kSolidStroke.center().y());
    const QColor ringPartial = pixelAt(solid, kPartialStroke.left() - 1, kPartialStroke.center().y());
    const QColor fillSolid = pixelAt(solid, kSolidStroke.center().x(), kSolidStroke.center().y());
    const QColor fillPartial = pixelAt(solid, kPartialStroke.center().x(), kPartialStroke.center().y());
    const QColor half = pixelAt(solid, kHalfPixel.center().x(), kHalfPixel.center().y());
    const QColor besideHalf = pixelAt(solid, kHalfPixel.left() - 1, kHalfPixel.center().y());
    const QColor twoAwaySolid = pixelAt(solid, kSolidStroke.left() - 2, kSolidStroke.center().y());

    std::printf(
        "MEASURED outline_ring solid_ring=(%d,%d,%d,%d) partial_ring=(%d,%d,%d,%d) ring_distance=%d"
        " solid_fill=(%d,%d,%d,%d) partial_fill=(%d,%d,%d,%d) half=(%d,%d,%d,%d) beside_half=(%d,%d,%d,%d)"
        " two_away=(%d,%d,%d,%d)\n",
        ringSolid.red(), ringSolid.green(), ringSolid.blue(), ringSolid.alpha(), ringPartial.red(),
        ringPartial.green(), ringPartial.blue(), ringPartial.alpha(),
        colourDistance(ringSolid, ringPartial), fillSolid.red(), fillSolid.green(), fillSolid.blue(),
        fillSolid.alpha(), fillPartial.red(), fillPartial.green(), fillPartial.blue(), fillPartial.alpha(),
        half.red(), half.green(), half.blue(), half.alpha(), besideHalf.red(), besideHalf.green(),
        besideHalf.blue(), besideHalf.alpha(), twoAwaySolid.red(), twoAwaySolid.green(), twoAwaySolid.blue(),
        twoAwaySolid.alpha()
    );

    // 11: the solid stroke's ring is BLACK - the outline colour at full strength.
    REQUIRE(
        colourDistance(ringSolid, QColor(0, 0, 0)) <= 2,
        "the ring beside a solid low-contrast stroke is (%d,%d,%d), not the outline colour",
        ringSolid.red(), ringSolid.green(), ringSolid.blue()
    );
    // ... and the 40% stroke's ring is the SAME ink at 40%, i.e. 60% of the bright
    // patch. The exact value is the pass's arithmetic, so it is asserted exactly
    // (mix(200, 0, 102/255) = 120).
    REQUIRE(
        std::abs(ringPartial.red() - 120) <= 3 && ringPartial.red() == ringPartial.green() &&
            ringPartial.green() == ringPartial.blue(),
        "the ring beside a 40%% stroke is (%d,%d,%d), not 40%% of the outline over the patch (120)",
        ringPartial.red(), ringPartial.green(), ringPartial.blue()
    );
    // The two rings differ: the strength really does follow the coverage.
    REQUIRE(
        colourDistance(ringSolid, ringPartial) > 60,
        "a solid and a 40%% stroke produced rings %d apart: the ring does not follow the coverage",
        colourDistance(ringSolid, ringPartial)
    );
    // The stroke itself is the composite of the ink over the patch, i.e. the INK -
    // never the ring colour, and never the patch.
    REQUIRE(
        colourDistance(fillSolid, kLowInk) <= kFillTol,
        "the solid stroke's own pixel is (%d,%d,%d), not its ink (%d,%d,%d)", fillSolid.red(),
        fillSolid.green(), fillSolid.blue(), kLowInk.red(), kLowInk.green(), kLowInk.blue()
    );
    // The 40%-covered pixel ALSO has low-contrast ink among its taps, so the ring is
    // painted UNDER its own ink and shows through the coverage: 0.6 * ring(120) +
    // 0.4 * ink(180) = 144, not the plain composite (192). That order is the pass's,
    // and it is pinned here because it is the only place a translucency is darkened;
    // over the panel's own opaque ink the ring is simply hidden by it. It is also a
    // second, independent pin on the premultiplied overlay being divided back out:
    // compositing it premultiplied again would give 101.
    REQUIRE(
        std::abs(fillPartial.red() - 144) <= 3 && fillPartial.red() == fillPartial.green() &&
            fillPartial.green() == fillPartial.blue(),
        "a 40%%-covered low-contrast pixel came out (%d,%d,%d); ink at 40%% over its own 40%% ring is 144",
        fillPartial.red(), fillPartial.green(), fillPartial.blue()
    );
    // 14: the 50%-covered pixel blends. bg = 60, ink = 255, so the expected value is
    // 0.5*60 + 0.5*255 = 157.5 -> 157 or 158. Both the "replaced" (255) and the
    // "vanished" (60) failure modes are far outside the tolerance.
    REQUIRE(
        std::abs(half.red() - 158) <= 3,
        "a 50%%-covered overlay pixel came out (%d,%d,%d); the blend of %d and 255 is 158",
        half.red(), half.green(), half.blue(), kDark.red()
    );
    REQUIRE(half.red() == half.green() && half.green() == half.blue(), "the 50%% pixel is not grey");
    // 12 (the composite half): that pixel's surroundings are untouched, because a
    // white pixel over a dark backdrop has all the contrast it needs.
    REQUIRE(
        colourDistance(besideHalf, kDark) == 0,
        "the pixel beside the high-contrast stroke is (%d,%d,%d), not the untouched backdrop",
        besideHalf.red(), besideHalf.green(), besideHalf.blue()
    );
    // The shipped ring is exactly ONE pixel wide: two columns out is still backdrop.
    REQUIRE(
        colourDistance(twoAwaySolid, kBright) == 0,
        "the shipped 1px ring reached two pixels from the stroke: (%d,%d,%d)", twoAwaySolid.red(),
        twoAwaySolid.green(), twoAwaySolid.blue()
    );

    // 15: the +/-2px second ring, the same model and only the radius differs.
    gui::outline::Params wide = params;
    wide.ringRadiusPx = 2.0f;
    const QImage wideRing = aHarness.run(backdrop, overlay, wide);
    const QColor wideTwoAway = pixelAt(wideRing, kSolidStroke.left() - 2, kSolidStroke.center().y());
    const QColor narrowTwoAway = pixelAt(solid, kSolidStroke.left() - 2, kSolidStroke.center().y());
    std::printf(
        "MEASURED outline_ring_radius=2 two_away=(%d,%d,%d) one_away=(%d,%d,%d) narrow_two_away=(%d,%d,%d)\n",
        wideTwoAway.red(), wideTwoAway.green(), wideTwoAway.blue(),
        pixelAt(wideRing, kSolidStroke.left() - 1, kSolidStroke.center().y()).red(),
        pixelAt(wideRing, kSolidStroke.left() - 1, kSolidStroke.center().y()).green(),
        pixelAt(wideRing, kSolidStroke.left() - 1, kSolidStroke.center().y()).blue(), narrowTwoAway.red(),
        narrowTwoAway.green(), narrowTwoAway.blue()
    );
    REQUIRE(
        colourDistance(wideTwoAway, QColor(0, 0, 0)) <= 2,
        "with ringRadiusPx = 2 the pixel two away from the stroke is (%d,%d,%d), not ringed",
        wideTwoAway.red(), wideTwoAway.green(), wideTwoAway.blue()
    );
    REQUIRE(
        colourDistance(narrowTwoAway, kBright) == 0,
        "precondition: the 1px ring must leave the pixel two away alone, it is (%d,%d,%d)", narrowTwoAway.red(),
        narrowTwoAway.green(), narrowTwoAway.blue()
    );

    // 11 (the threshold's own value): THE PAIR THAT PINS gui::outline::kMinContrast.
    //
    // kThresholdInk is 2.30:1 against the bright patch - in linear light, which is the
    // only light the ratio means that in - so it sits BETWEEN the shipped 2.0 and the
    // 3.0 this shipped with. The pixel beside it must therefore be the UNTOUCHED
    // backdrop at the shipped constant, and BLACK (the outline colour) at 3.0: the two
    // runs differ in one field of Params and nothing else, which is what makes this a
    // statement about the constant rather than about the ink.
    const QColor besideThreshold = pixelAt(solid, kThresholdStroke.left() - 1, kThresholdStroke.center().y());
    const QColor thresholdFill = pixelAt(solid, kThresholdStroke.center().x(), kThresholdStroke.center().y());
    gui::outline::Params old = params;
    old.minContrast = 3.0f;
    const QImage atThree = aHarness.run(backdrop, overlay, old);
    const QColor besideThresholdAtThree = pixelAt(atThree, kThresholdStroke.left() - 1, kThresholdStroke.center().y());
    std::printf(
        "MEASURED outline_threshold ink_rgb=%s min_contrast=%.1f beside_ink=(%d,%d,%d) backdrop=(%d,%d,%d)"
        " distance=%d at_3_0=(%d,%d,%d)\n",
        hex(kThresholdInk).c_str(), double(gui::outline::kMinContrast), besideThreshold.red(),
        besideThreshold.green(), besideThreshold.blue(), kBright.red(), kBright.green(), kBright.blue(),
        colourDistance(besideThreshold, kBright), besideThresholdAtThree.red(), besideThresholdAtThree.green(),
        besideThresholdAtThree.blue()
    );
    REQUIRE(
        colourDistance(thresholdFill, kThresholdInk) <= kFillTol,
        "the threshold probe's own pixel is (%d,%d,%d), not its ink (%d,%d,%d): the probe is not where it thinks",
        thresholdFill.red(), thresholdFill.green(), thresholdFill.blue(), kThresholdInk.red(),
        kThresholdInk.green(), kThresholdInk.blue()
    );
    REQUIRE(
        colourDistance(besideThreshold, kBright) == 0,
        "a 2.30:1 stroke was ringed at the shipped kMinContrast = %.1f (the pixel beside it is (%d,%d,%d), not the"
        " untouched backdrop): the ring is drawn where the ink still reads",
        double(gui::outline::kMinContrast), besideThreshold.red(), besideThreshold.green(), besideThreshold.blue()
    );
    REQUIRE(
        colourDistance(besideThresholdAtThree, QColor(0, 0, 0)) <= 2,
        "precondition: the same stroke at minContrast = 3.0 is NOT ringed ((%d,%d,%d)): the two constants do not"
        " straddle this ink, so this pair pins nothing",
        besideThresholdAtThree.red(), besideThresholdAtThree.green(), besideThresholdAtThree.blue()
    );
    return true;
}

// 12: the same stroke over a CONTRASTING patch gets NO ring, and the backdrop
// around it survives byte for byte in every direction.
bool checkOutlineNoRing(OutlineHarness& aHarness) {
    QImage overlay = blankImage(QColor(0, 0, 0, 0));
    fillRect(overlay, kContrastStroke, overlayInk(kWhiteInk, 255));

    const QImage backdrop = ringBackdrop();
    gui::outline::Params params;
    params.colour = QColor(0, 0, 0);
    const QImage rendered = aHarness.run(backdrop, overlay, params);

    int checked = 0;
    int changed = 0;
    for (int y = kContrastStroke.top() - 2; y <= kContrastStroke.bottom() + 2; ++y) {
        for (int x = kContrastStroke.left() - 2; x <= kContrastStroke.right() + 2; ++x) {
            if (kContrastStroke.contains(x, y))
                continue;
            ++checked;
            if (colourDistance(pixelAt(rendered, x, y), pixelAt(backdrop, x, y)) != 0)
                ++changed;
        }
    }
    const QColor inside = pixelAt(rendered, kContrastStroke.center().x(), kContrastStroke.center().y());
    std::printf(
        "MEASURED outline_no_ring patch=%d,%d,%d stroke_ink=(%d,%d,%d) ring_pixels_checked=%d changed=%d\n",
        kDark.red(), kDark.green(), kDark.blue(), inside.red(), inside.green(), inside.blue(), checked, changed
    );
    REQUIRE(checked > 0, "the no-ring probe checked no pixels at all");
    REQUIRE(
        changed == 0, "%d of %d pixels around a high-contrast stroke changed: the ring is unconditional", changed,
        checked
    );
    REQUIRE(
        colourDistance(inside, kWhiteInk) <= kFillTol, "the high-contrast stroke's own pixel is (%d,%d,%d), not white",
        inside.red(), inside.green(), inside.blue()
    );
    return true;
}

// 13: the inverse assertion. With NO overlay coverage anywhere, the whole
// framebuffer must come back byte-identical to a patterned backdrop - through the
// blit AND through the pass. This is what catches an "always draws" regression:
// a backdrop that is one bit off, one ringed pixel, one blend applied to the panel
// area, and it fails.
bool checkOutlineBackdropIdentical(OutlineHarness& aHarness) {
    const QImage overlay = blankImage(QColor(0, 0, 0, 0));
    const QImage backdrop = patternBackdrop();
    gui::outline::Params params;
    params.colour = QColor(0, 0, 0);

    const QImage rendered = aHarness.run(backdrop, overlay, params);

    int changed = 0;
    int worst = 0;
    QRect worstAt;
    for (int y = 0; y < kViewH; ++y) {
        for (int x = 0; x < kViewW; ++x) {
            const int distance = colourDistance(pixelAt(rendered, x, y), pixelAt(backdrop, x, y));
            if (distance != 0) {
                ++changed;
                if (distance > worst) {
                    worst = distance;
                    worstAt = QRect(x, y, 1, 1);
                }
            }
        }
    }
    std::printf(
        "MEASURED outline_backdrop_identical pixels=%d changed=%d worst_distance=%d worst_at=(%d,%d)"
        " byte_equal=%d\n",
        kViewW * kViewH, changed, worst, worstAt.left(), worstAt.top(), int(imagesEqual(rendered, backdrop))
    );
    REQUIRE(
        changed == 0,
        "%d of %d pixels changed with an empty overlay (worst %d at (%d,%d)): the pass is not a no-op",
        changed, kViewW * kViewH, worst, worstAt.left(), worstAt.top()
    );
    return true;
}

// 16: the pass is a GUEST in the frame - the bindings it finds are the bindings it
// leaves. THIS IS NOT A COURTESY: the application's paint path keeps ONE default
// vertex array bound for the whole session (MainDisplayWidget::initializeGL creates
// it and comments "keep binding"), and the painter engine that runs at the TOP of
// the next paint relies on it - every glVertexAttribPointer it makes needs an array
// object bound in a core profile. A version of this pass that ended with
// glBindVertexArray(0) made the application's next paint fail with
// GL_INVALID_OPERATION in glVertexAttribPointer and glDrawArrays (measured on a real
// run; the error surfaced one call later as an assert in
// gl::EasyShaderProgram::release()). The unit itself is also asserted to be quiet:
// a pass that leaves a GL error behind fails this file's own noGlError checks.
bool checkOutlineRestoresBindings(OutlineHarness& aHarness) {
    auto& funcs = gl::Global::functions();

    const QImage overlay = blankImage(QColor(0, 0, 0, 0));
    const QImage backdrop = patternBackdrop();
    REQUIRE(aHarness.prepare(backdrop, overlay), "precondition: the harness could not prepare its textures");

    // A marker binding in every slot the pass touches: its own vertex array, its own
    // texture on each unit, and a NON-ZERO active unit (the pass restores the active
    // unit last, so leaving it elsewhere would be invisible otherwise).
    gl::VertexArrayObject markerVao;
    gl::Texture markerTexture0;
    gl::Texture markerTexture1;
    markerTexture0.create(QSize(4, 4));
    markerTexture1.create(QSize(4, 4));
    markerVao.bind();
    funcs.glActiveTexture(GL_TEXTURE0);
    funcs.glBindTexture(GL_TEXTURE_2D, markerTexture0.id());
    funcs.glActiveTexture(GL_TEXTURE1);
    funcs.glBindTexture(GL_TEXTURE_2D, markerTexture1.id());

    gFbo->bind();
    funcs.glDisable(GL_SCISSOR_TEST);
    funcs.glScissor(0, 0, kViewW, kViewH);
    funcs.glViewport(0, 0, kViewW, kViewH);
    funcs.glClearColor(kClear.redF(), kClear.greenF(), kClear.blueF(), 1.0f);
    funcs.glClear(GL_COLOR_BUFFER_BIT);

    gui::outline::Params params;
    params.colour = QColor(0, 0, 0);
    gui::outline::composite(aHarness.overlayTexture.id(), aHarness.backdropTexture.id(), QSize(kViewW, kViewH), params);

    GLint vao = 0;
    GLint activeTexture = 0;
    GLint texture0 = 0;
    GLint texture1 = 0;
    GLint program = -1;
    GLint drawFbo = 0;
    funcs.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
    funcs.glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
    funcs.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture1);
    funcs.glActiveTexture(GL_TEXTURE0);
    funcs.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
    funcs.glActiveTexture(GLenum(activeTexture));
    funcs.glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    funcs.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);

    std::printf(
        "MEASURED outline_state_restored vao=%d expected_vao=%u active_texture=0x%x expected=0x%x"
        " texture0=%d expected0=%u texture1=%d expected1=%u program=%d draw_fbo=%u expected_fbo=%u\n",
        vao, markerVao.id(), activeTexture, GLuint(GL_TEXTURE1), texture0, markerTexture0.id(), texture1,
        markerTexture1.id(), program, drawFbo, gFbo->id()
    );

    REQUIRE(
        vao == GLint(markerVao.id()),
        "the pass left vertex array %d bound, not the %u it found: the application's default VAO would be gone",
        vao, markerVao.id()
    );
    REQUIRE(
        activeTexture == GLint(GL_TEXTURE1), "the pass left texture unit 0x%x active, not the 0x%x it found",
        activeTexture, GLuint(GL_TEXTURE1)
    );
    REQUIRE(
        texture0 == GLint(markerTexture0.id()) && texture1 == GLint(markerTexture1.id()),
        "the pass left textures %d/%d bound on units 0/1, not the %u/%u it found", texture0, texture1,
        markerTexture0.id(), markerTexture1.id()
    );
    // The framebuffer is the one binding the pass does not put back, and it must not
    // need to: it is a full-screen pass and it writes into whatever is bound.
    REQUIRE(
        GLuint(drawFbo) == gFbo->id(), "the pass ended with FBO %u bound, not the %u it was asked to draw into",
        drawFbo, gFbo->id()
    );
    // The program is DELIBERATELY left unbound, exactly as the application's own
    // gl::EasyTextureDrawer leaves it at the end of every paint.
    REQUIRE(program == 0, "the pass left program %d bound; the application's drawers leave 0", program);
    REQUIRE(noGlError("the pass's own state"), "the pass left a GL error behind");
    return true;
}

//-------------------------------------------------------------------------------------------------
// THE CURVE STROKE: the coverage pass (gui::stroke) against the geometry/fringe stroke it
// replaces (ImDrawList::AddPolyline, which is what the panel shipped and what drawPanel
// still falls back to when the pass is not built).
//
// ONE MODEL, FIVE RUNS, TWO STROKE GEOMETRIES. The runs below are chosen so that EVERY
// measurement points at a region no other run's ink can reach, and the model publishes
// nothing else at all - no ticks, no labels, no zero rule, no playhead, no keys, no
// handles, no legend - so the ink in a region is that run's ink. The same model is then
// rendered twice out of ONE process: with the pass built (AFTER) and before it is built
// (BEFORE), which is what makes the two implementations comparable number for number
// rather than across two runs of the gate.
//
// WHY FOUR ASSERTIONS AND NOT ONE. The defect the user reported ("jagged and inconsistent
// line thickness") is a property of the stroke's SHAPE, and no single number describes a
// shape. Each assertion below pins a different way a stroke can be wrong, and each is
// stated with the numbers it measured (before AND after, printed) so a failure reads as a
// measurement rather than a verdict:
//   22 equal thickness across the angle - the ink's average PERPENDICULAR extent, over the
//      run's own interior, at four angles including a 45-degree one;
//   23 the edges are antialiased - the coverage ramp across the stroke's boundary, on the
//      run deliberately placed half a pixel off the grid, where an unantialiased edge has
//      no partial pixel at all;
//   24 no double-darkening at a join - the coverage of every pixel near a right-angle
//      corner against what ONE capsule's profile allows at that pixel's own distance, with
//      the curve dimmed so a double blend is visible as ink DARKER than the stroke's body;
//   25 the ink is still where the polyline is - every sample of every run lands on inked
//      pixels, the whole frame's ink is inside the runs' own bounds, and the stroke's
//      coverage-weighted centre is on the centreline.
// Plus 26, the guest rule, which the pass shares with the contrast outline's.

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
    model.rulerBandTop = kPlot.top();
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
        curve.color = aTheme.keyColor(kTypeA);
        curve.legendIndex = -1;
        return curve;
    };
    model.curves = {
        run({kFlatA, kFlatB}), run({kPhaseA, kPhaseB}), run({kDiagonalA, kDiagonalB}),
        run({kSteepA, kSteepB}), run({kCornerA, kCornerV, kCornerB}), run(denseWavyPoints()),
    };
    return model;
}

// A model shaped like ONE of the shipping panel's own curves: a single polyline sampled at
// the panel's own budget (one point per ~2px of plot width, GraphPanelModel.cpp), i.e. ~145
// samples for this plot. The cost measurement below uses it because the panel's curve is
// what the pass has to stroke every frame, and the ink model's fixtures are not that shape.
gui::GraphPanelModel makeDenseModel(const theme::Graph& aTheme) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kPlot.top();
    model.zeroY = -1;
    model.playheadX = -1;

    gui::PanelCurve curve;
    curve.node = nullptr;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    curve.color = aTheme.keyColor(kTypeA);
    curve.legendIndex = -1;
    for (int i = 0; i <= 144; ++i) {
        const double x = 92.0 + 2.0 * i;
        curve.points.append(QPointF(x, 150.0 + 60.0 * std::sin((x - 92.0) / 70.0)));
    }
    model.curves = {curve};
    return model;
}

// The four straight runs, as the measurements name them. `gridCount` says whether the literal
// whole-pixel count is a measurement of the stroke at all on that run: it is on the run that
// lies on the pixel grid, and it is not on the one deliberately placed half a pixel off it
// (where a stroke of this width straddles three rows at 0.5 apiece whatever an implementation
// does, so the count would be measuring the pixel grid rather than the ink).
struct InkRun {
    const char* name;
    QPointF a;
    QPointF b;
    bool gridCount;
};
const InkRun kInkRuns[] = {
    {"horizontal", kFlatA, kFlatB, true},
    {"horizontal_half_pixel_off", kPhaseA, kPhaseB, false},
    {"diagonal45", kDiagonalA, kDiagonalB, true},
    {"steep72", kSteepA, kSteepB, true},
};

// One curve's ink as the measurements read it: the colour, and the channel of the framebuffer
// it is furthest from the clear colour in. The panel composites source-over onto an opaque
// clear, so a pixel that carries coverage a is ink*a + clear*(1-a) - and inverting that along
// the widest channel recovers a for BOTH stroke geometries (the coverage pass and ImGui's own
// fringe both blend source-over). The recovered number is therefore the stroke's own alpha,
// and comparing the two implementations' alphas is comparing the two strokes.
struct InkProbe {
    QColor ink;
    int channel = 0;

    static InkProbe make(const QColor& aInk) {
        const int clear[3] = {kClear.red(), kClear.green(), kClear.blue()};
        const int rgb[3] = {aInk.red(), aInk.green(), aInk.blue()};
        InkProbe probe;
        probe.ink = aInk;
        int widest = -1;
        for (int c = 0; c < 3; ++c) {
            const int spread = std::abs(clear[c] - rgb[c]);
            if (spread > widest) {
                widest = spread;
                probe.channel = c;
            }
        }
        return probe;
    }

    double coverage(const QImage& aImage, int aX, int aY) const {
        const QColor c = pixelAt(aImage, aX, aY);
        const int clear[3] = {kClear.red(), kClear.green(), kClear.blue()};
        const int rgb[3] = {ink.red(), ink.green(), ink.blue()};
        const int got[3] = {c.red(), c.green(), c.blue()};
        const double a =
            double(clear[channel] - got[channel]) / double(clear[channel] - rgb[channel]);
        return std::min(1.0, std::max(0.0, a));
    }
};

// How wide one straight run's ink is. Both numbers are averages over the run's own INTERIOR
// (the segment pulled in by aMargin at each end, so the round caps - which lie beyond a
// segment's endpoints - are outside the measurement), and both are per unit length, so a
// run's own length cancels out:
//   * integrated: the sum of every covered pixel's coverage over the interior's length. The
//     sum of pixel coverages is the AREA of the ink band, and area over length is the band's
//     width at ANY angle - the threshold limit of "count the pixels over a coverage
//     threshold", with the sub-pixel part kept.
//   * overHalf: the literal count of pixels whose coverage is over 0.5, over the same length.
//     A whole-pixel measurement, and it is asserted on the run that lies ON the grid: at a
//     half-pixel phase a stroke of this width straddles three rows at 0.5 each and this
//     count reads 1 whatever the implementation does, which is a statement about the pixel
//     grid and not about the stroke.
struct RunWidth {
    double integrated = 0.0;
    double overHalf = 0.0;
    double length = 0.0;
    int pixels = 0;
};

RunWidth measureRun(
    const QImage& aImage, const InkProbe& aProbe, const QPointF& aA, const QPointF& aB, double aMargin = 6.0,
    double aWindow = 3.0
) {
    const double dx = aB.x() - aA.x();
    const double dy = aB.y() - aA.y();
    const double full = std::hypot(dx, dy);
    const double ux = dx / full;
    const double uy = dy / full;
    const double nx = -uy;
    const double ny = ux;
    const QPointF mid((aA.x() + aB.x()) * 0.5, (aA.y() + aB.y()) * 0.5);
    const double halfLength = std::max(0.0, full * 0.5 - aMargin);

    RunWidth out;
    out.length = halfLength * 2.0;
    const QRect bounds = grow(QRect(int(std::floor(std::min(aA.x(), aB.x()))), int(std::floor(std::min(aA.y(), aB.y()))), int(std::ceil(std::abs(dx))) + 1, int(std::ceil(std::abs(dy))) + 1), int(aWindow) + 2).intersected(aImage.rect());
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            // The pixel's own centre: the point its coverage is the area about.
            const double s = (double(x) + 0.5 - mid.x()) * ux + (double(y) + 0.5 - mid.y()) * uy;
            const double t = (double(x) + 0.5 - mid.x()) * nx + (double(y) + 0.5 - mid.y()) * ny;
            if (std::abs(s) > halfLength || std::abs(t) > aWindow)
                continue;
            const double cov = aProbe.coverage(aImage, x, y);
            out.integrated += cov;
            if (cov > 0.5)
                out.overHalf += 1.0;
            ++out.pixels;
        }
    }
    out.integrated /= out.length;
    out.overHalf /= out.length;
    return out;
}

// The coverage profile ACROSS a horizontal run, in one pixel column: the run's own
// perpendicular direction is the vertical one, so the pixels of the column ARE the samples -
// no interpolation anywhere, and every number below is a pixel's own coverage. The rows run
// from aFirstRow to aLastRow, and the centreline between them is aCentreY.
std::vector<double> columnProfile(
    const QImage& aImage, const InkProbe& aProbe, int aColumn, int aFirstRow, int aLastRow
) {
    std::vector<double> profile;
    for (int row = aFirstRow; row <= aLastRow; ++row)
        profile.push_back(aProbe.coverage(aImage, aColumn, row));
    return profile;
}

// The row window a profile about a horizontal centreline is read over: seven rows below it and
// eight above, which is wider than the stroke and its filter can reach on either side.
void profileRows(double aCentreY, int& aFirst, int& aLast) {
    aFirst = int(std::floor(aCentreY)) - 7;
    aLast = int(std::floor(aCentreY)) + 8;
}

// The distance from a point to a SEGMENT (not its line), which is what one capsule's ink
// reaches from: the pass measures exactly this, so it is also the bound a correct union of
// two segments' capsules must never exceed.
double distanceToSegment(const QPointF& aP, const QPointF& aA, const QPointF& aB) {
    const double dx = aB.x() - aA.x();
    const double dy = aB.y() - aA.y();
    const double lengthSquared = dx * dx + dy * dy;
    const double t = lengthSquared > 0.0
        ? std::min(1.0, std::max(0.0, ((aP.x() - aA.x()) * dx + (aP.y() - aA.y()) * dy) / lengthSquared))
        : 0.0;
    return std::hypot(aP.x() - aA.x() - t * dx, aP.y() - aA.y() - t * dy);
}

// One capsule's own coverage at a distance, in the pass's own arithmetic (a box filter of
// kFilterPx device pixels about the nominal edge) - the AFTER numbers are asserted against
// it, and the BEFORE's distance from it IS the defect, so it has to be stated rather than
// implied. aHalfWidth defaults to the panel's own stroke; assertion 27 applies the same
// function to a 1px hairline by passing 0.5.
double capsuleCoverage(double aDistancePx, double aStrength, double aHalfWidth = double(gui::stroke::kWidthPx) * 0.5) {
    const double filter = double(gui::stroke::kFilterPx);
    const double cov = std::min(1.0, std::max(0.0, (aHalfWidth + 0.5 * filter - aDistancePx) / filter));
    return cov * aStrength;
}

QString profileText(const std::vector<double>& aProfile) {
    QString text;
    for (size_t i = 0; i < aProfile.size(); ++i)
        text += QStringLiteral("%1%2").arg(i ? QStringLiteral(" ") : QString(), QString::number(aProfile[i], 'f', 3));
    return text;
}

//-------------------------------------------------------------------------------------------------
// THE PANEL'S OTHER 1px LINES (assertion 27). Two families, and they are not the same
// question:
//   * the ticks, the zero rule and the playhead are drawn with the draw list's
//     ImDrawListFlags_AntiAliasedLines turned OFF (drawPanel says so where it does it, with
//     the timeline's hairline rule as the reason), and every one of them is positioned by an
//     INTEGER in the model: frameTickX/valueTickY are QVector<int>, zeroY is an int, and
//     playheadX is an int - the frame is mapped through the scaler and resolved to a whole
//     column before a drawing layer ever sees it.
//   * the tangent-handle STEM is 1px too, but drawPanel leaves the flag alone for it, so it
//     goes through ImGui's texture-AA path - and it is the only 1px geometry in the panel
//     whose coordinates are fractional (PanelHandle::base/dot are QPointF).
// The two fixtures below put one family in front of the camera at a time.

// A model whose ONLY ink is the playhead, at aPixelX.
gui::GraphPanelModel makePlayheadModel(int aPixelX) {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kPlot.top();
    model.zeroY = -1;
    model.playheadX = aPixelX;
    return model;
}

// The panel's OTHER 1px line family, alone: one tangent handle, with the fractional
// coordinates a handle really has. Nothing else is published, so the ink in the frame is the
// stem and its dot.
const QPointF kStemBase(150.5, 250.25);
const QPointF kStemDot(230.5, 190.75);

gui::GraphPanelModel makeStemModel() {
    gui::GraphPanelModel model;
    model.panel = kPanel;
    model.plot = kPlot;
    model.valueColumn = kValueColumn;
    model.rulerBandTop = kPlot.top();
    model.zeroY = -1;
    model.playheadX = -1;

    gui::PanelCurve curve;
    curve.node = nullptr;
    curve.channel = ctrl::graph::Channel(kTypeA, 0);
    curve.legendIndex = -1;
    gui::PanelHandle handle;
    handle.segment = 0;
    handle.index = 0;
    handle.base = kStemBase;
    handle.dot = kStemDot;
    handle.radius = ctrl::graph::kHandleRadius;
    curve.handles = {handle};
    model.curves = {curve};
    return model;
}

// The playhead's own ink, column by column, over the plot's rows: the model above publishes
// nothing else, so this is the line's placement and nothing beside it.
std::vector<double> columnInk(const Rendered& aRendered, const InkProbe& aProbe, int aFirst, int aLast) {
    std::vector<double> ink;
    for (int x = aFirst; x <= aLast; ++x) {
        double sum = 0.0;
        for (int y = kPlot.top(); y <= kPlot.bottom(); ++y)
            sum += aProbe.coverage(aRendered.image, x, y);
        ink.push_back(sum);
    }
    return ink;
}

// 22: EQUAL THICKNESS ACROSS THE ANGLE. The same nominal width, stroked horizontally, at 45
// degrees and at ~72 degrees - and, along the second half of the assertion, along a DENSE
// WAVY polyline the way the panel's own curve is sampled. The ink's perpendicular extent
// must not care which way the stroke runs, nor where along the line it is measured.
//
// WHAT THE TWO NUMBERS SAID (MEASURED, and the reason the discrimination this file makes is
// named where it is rather than here): at the shipping width ImGui's own AddPolyline takes
// its TEXTURE path (a width with a zero fractional part, and the font atlas bakes the line
// texture), and that path's fringe is a 1px ramp perpendicular to each segment - so its
// area, which is what the extent below measures, comes out angle-independent too: 0.053px of
// spread for BOTH implementations. What the texture path does NOT get right is the JOIN,
// where its cross-section rotates with the sampled normal and its segments' fringes blend
// over one another - assertion 24 measures that, and it is the one the old stroke fails.
// This assertion is therefore a gate on the NEW path (a stroke that tied its width to its
// vertices, or that measured its fringe in the wrong space, would fail it) rather than a
// demonstration that the old one was wrong here.
bool checkStrokeAngle(const Rendered& aAfter, const Rendered& aBefore, const theme::Graph& aTheme) {
    REQUIRE(gui::stroke::isReady(), "precondition: the coverage pass must be built for this assertion");

    const InkProbe probe = InkProbe::make(aTheme.keyColor(kTypeA));
    std::printf(
        "MEASURED stroke_angle nominal_width=%.2fpx filter=%.2fpx framebuffer_scale=1:1 colour=%s channel=%d\n",
        double(gui::stroke::kWidthPx), double(gui::stroke::kFilterPx), hex(probe.ink).c_str(), probe.channel
    );

    double afterMin = 1e9;
    double afterMax = -1e9;
    double beforeMin = 1e9;
    double beforeMax = -1e9;
    double afterHalfMin = 1e9;
    double afterHalfMax = -1e9;
    int countable = 0;
    for (const InkRun& run : kInkRuns) {
        const RunWidth after = measureRun(aAfter.image, probe, run.a, run.b);
        const RunWidth before = measureRun(aBefore.image, probe, run.a, run.b);
        std::printf(
            "MEASURED stroke_angle run=%-24s length=%.1fpx interior=%dpx2 after_integrated=%.3f"
            " after_over_half=%.3f before_integrated=%.3f before_over_half=%.3f\n",
            run.name, after.length, after.pixels, after.integrated, after.overHalf, before.integrated,
            before.overHalf
        );
        REQUIRE(after.pixels > 100, "precondition: run %s measured only %d pixels", run.name, after.pixels);

        afterMin = std::min(afterMin, after.integrated);
        afterMax = std::max(afterMax, after.integrated);
        beforeMin = std::min(beforeMin, before.integrated);
        beforeMax = std::max(beforeMax, before.integrated);
        // The literal count only where the grid can count it: see InkRun::gridCount.
        if (run.gridCount) {
            afterHalfMin = std::min(afterHalfMin, after.overHalf);
            afterHalfMax = std::max(afterHalfMax, after.overHalf);
            countable += 1;
        }
    }
    REQUIRE(countable >= 3, "precondition: only %d runs are countable against the whole-pixel metric", countable);

    std::printf(
        "MEASURED stroke_angle_spread after_integrated=%.3fpx before_integrated=%.3fpx"
        " after_over_half=%.3fpx\n",
        afterMax - afterMin, beforeMax - beforeMin, afterHalfMax - afterHalfMin
    );

    // ...AND ALONG THE LINE. The panel's own curve is a polyline sampled every ~2px, so its
    // direction changes from sample to sample, and a stroke that tied its cross-section to
    // each SAMPLED normal would change its apparent width at every one of those turns. The
    // wavy fixture is that situation in isolation - samples 4px apart on a sine, turning by
    // up to ~33 degrees between neighbours - and the width is measured the same way, over
    // the 8px of line centred on every interior sample.
    //
    // MEASURED: 0.049px of spread (this pass) against 0.052px (the fringe stroke). The
    // extent is an AREA over its own length, and rotating a cross-section does not change
    // its area - so this metric does not see the rotation either, which the assertion on the
    // JOIN does (the rotation moves ink OUTSIDE the union, and that is what 24 catches). The
    // numbers are printed for both because that is what the measurement says.
    const QVector<QPointF> wavy = denseWavyPoints();
    double wavyMin = 1e9;
    double wavyMax = -1e9;
    double wavyBeforeMin = 1e9;
    double wavyBeforeMax = -1e9;
    QString wavyAfter;
    QString wavyBefore;
    for (int i = 1; i + 1 < wavy.size(); ++i) {
        const RunWidth after = measureRun(aAfter.image, probe, wavy.at(i - 1), wavy.at(i + 1), 0.0, 3.0);
        const RunWidth before = measureRun(aBefore.image, probe, wavy.at(i - 1), wavy.at(i + 1), 0.0, 3.0);
        wavyMin = std::min(wavyMin, after.integrated);
        wavyMax = std::max(wavyMax, after.integrated);
        wavyBeforeMin = std::min(wavyBeforeMin, before.integrated);
        wavyBeforeMax = std::max(wavyBeforeMax, before.integrated);
        wavyAfter += QStringLiteral("%1 ").arg(after.integrated, 0, 'f', 3);
        wavyBefore += QStringLiteral("%1 ").arg(before.integrated, 0, 'f', 3);
    }
    std::printf(
        "MEASURED stroke_angle_along_line samples=%d after_min=%.3f after_max=%.3f after_spread=%.3fpx"
        " before_min=%.3f before_max=%.3f before_spread=%.3fpx\n",
        int(wavy.size()) - 2, wavyMin, wavyMax, wavyMax - wavyMin, wavyBeforeMin, wavyBeforeMax,
        wavyBeforeMax - wavyBeforeMin
    );
    std::printf("MEASURED stroke_angle_along_line after=[%s]\n", qPrintable(wavyAfter));
    std::printf("MEASURED stroke_angle_along_line before=[%s]\n", qPrintable(wavyBefore));
    REQUIRE(
        wavyMax - wavyMin <= 0.5,
        "the stroke's thickness changes along a curve: %.3fpx to %.3fpx over %d samples (before: %.3fpx to %.3fpx)",
        wavyMin, wavyMax, int(wavy.size()) - 2, wavyBeforeMin, wavyBeforeMax
    );
    // THE ASSERTION. 1px is the tolerance the report states, and both offsets are printed
    // with it: the measured numbers are 0.053px (this pass) and 0.053px (the fringe stroke
    // it replaces) across the angles - see the note above for why the second number is not
    // the discrimination this file is making.
    REQUIRE(
        afterMax - afterMin <= 1.0,
        "the stroke's width still depends on the angle: %.3fpx to %.3fpx across the runs"
        " (before: %.3fpx to %.3fpx)",
        afterMin, afterMax, beforeMin, beforeMax
    );
    REQUIRE(
        afterHalfMax - afterHalfMin <= 1.0,
        "the whole-pixel width still depends on the angle: %.3fpx to %.3fpx", afterHalfMin, afterHalfMax
    );
    return true;
}

// 23: ANTIALIASED EDGES. The run placed half a pixel off the grid has partial coverage at
// both of its edges, and the profile ramps monotonically into the body and back out of it.
// This is a statement about the RAMP, so it is measured on the run whose edges cannot land on
// a pixel boundary: a stroke that ends exactly on the grid has no partial pixel in any
// implementation, and asserting a ramp there would assert the wrong thing.
bool checkStrokeEdges(const Rendered& aAfter, const Rendered& aBefore, const theme::Graph& aTheme) {
    REQUIRE(gui::stroke::isReady(), "precondition: the coverage pass must be built for this assertion");

    const InkProbe probe = InkProbe::make(aTheme.keyColor(kTypeA));
    int firstRow = 0;
    int lastRow = 0;
    profileRows(kPhaseA.y(), firstRow, lastRow);
    const int columns[] = {140, 185, 230};
    int ramps = 0;
    for (int column : columns) {
        const std::vector<double> after = columnProfile(aAfter.image, probe, column, firstRow, lastRow);
        const std::vector<double> before = columnProfile(aBefore.image, probe, column, firstRow, lastRow);

        // The peak and its index.
        int peak = 0;
        for (size_t i = 1; i < after.size(); ++i)
            if (after[i] > after[size_t(peak)])
                peak = int(i);
        double sum = 0.0;
        for (double c : after)
            sum += c;
        std::printf(
            "MEASURED stroke_edge column=%d rows=%d..%d after_sum=%.3fpx before_sum=%.3fpx after=[%s] before=[%s]\n",
            column, firstRow, lastRow, sum, std::accumulate(before.begin(), before.end(), 0.0),
            qPrintable(profileText(after)), qPrintable(profileText(before))
        );

        REQUIRE(
            after[size_t(peak)] >= 0.99, "column %d: the stroke's body is only %.3f covered", column,
            after[size_t(peak)]
        );
        // An intermediate sample on EACH side of the body, and nothing outside the ramp
        // reaching the body's own level.
        const auto intermediate = [](const std::vector<double>& aProfile, int aFrom, int aTo) {
            int found = 0;
            for (int i = aFrom; i <= aTo && i >= 0 && i < int(aProfile.size()); ++i)
                if (aProfile[size_t(i)] > 0.15 && aProfile[size_t(i)] < 0.85)
                    ++found;
            return found;
        };
        const int below = intermediate(after, peak - 3, peak - 1);
        const int above = intermediate(after, peak + 1, peak + 3);
        REQUIRE(
            below >= 1 && above >= 1,
            "column %d: the edge carries no intermediate coverage (%d samples below the body, %d above) - the"
            " stroke's edge is a hard step, not a ramp",
            column, below, above
        );
        // Monotone into the body and monotone out of it.
        for (int i = 1; i <= peak; ++i)
            REQUIRE(
                after[size_t(i)] >= after[size_t(i - 1)] - 0.02,
                "column %d: the rising edge is not monotone at sample %d (%.3f then %.3f)", column, i,
                after[size_t(i - 1)], after[size_t(i)]
            );
        for (int i = peak + 1; i < int(after.size()); ++i)
            REQUIRE(
                after[size_t(i)] <= after[size_t(i - 1)] + 0.02,
                "column %d: the falling edge is not monotone at sample %d (%.3f then %.3f)", column, i,
                after[size_t(i - 1)], after[size_t(i)]
            );
        // The ramp is a RAMP, not a smear: the whole column's ink is the nominal width.
        REQUIRE(
            std::abs(sum - double(gui::stroke::kWidthPx)) <= 0.6,
            "column %d: the perpendicular ink is %.3fpx against a nominal width of %.2fpx", column, sum,
            double(gui::stroke::kWidthPx)
        );
        ++ramps;
    }

    REQUIRE(ramps == 3, "precondition: only %d of 3 columns were measured", ramps);
    return true;
}

// 24: NO DOUBLE-DARKENING AT A JOIN. Two segments' capsules overlap around their shared
// vertex, and a stroke that composites one segment after the other blends the overlap twice.
// The curve measured here is DIMMED (the focus names another channel, so the panel strokes it
// at kUnfocusedAlpha) and that is what makes the assertion sharp: on a dimmed curve an
// overlap that both segments fully cover composites DARKER than the stroke's own body, which
// a full-strength curve would hide (opaque ink over opaque ink is the same pixel).
//
// The bound is the pass's own arithmetic: a pixel's coverage can never exceed what ONE
// segment's capsule produces at that pixel's distance, because the stroke is the UNION of the
// capsules - and every pixel near the corner is measured against it.
//
// MEASURED on the ink model's corner, dimmed (body 0.450): the worst pixel for the coverage
// stroke is +0.000 over the bound (measured 0.357 against a bound of 0.357 - the pass IS the
// bound), and for the fringe stroke +0.093 (0.450 against 0.357). At FULL strength the same
// pixel comes out 1.000 against 0.79, i.e. the fringe stroke puts 21% more ink on the join's
// outer shoulder than one capsule can explain.
bool checkStrokeJoin(const Rendered& aAfter, const Rendered& aBefore, const theme::Graph& aTheme) {
    REQUIRE(gui::stroke::isReady(), "precondition: the coverage pass must be built for this assertion");

    const InkProbe probe = InkProbe::make(aTheme.keyColor(kTypeA));
    // The curve's strength, MEASURED off its own body rather than assumed: the pixels along
    // arm A's centreline are fully covered, so their coverage IS the strength.
    double body = 0.0;
    for (int x = 305; x <= 365; ++x)
        body = std::max(body, probe.coverage(aAfter.image, x, 40));
    REQUIRE(
        body > 0.15 && body < 0.99,
        "precondition: the join assertion needs a DIMMED curve (measured body coverage %.3f)", body
    );

    struct Worst {
        double excess = -1e9;
        double measured = 0.0;
        double bound = 0.0;
        QPoint at;
    };
    const auto scan = [&probe, body](const Rendered& aRendered, const QPointF& aA, const QPointF& aB, const QPointF& aV) {
        Worst worst;
        for (int y = int(aV.y()) - 5; y <= int(aV.y()) + 5; ++y) {
            for (int x = int(aV.x()) - 5; x <= int(aV.x()) + 5; ++x) {
                const QPointF centre(double(x) + 0.5, double(y) + 0.5);
                // The union's coverage: the NEARER capsule, which is the larger of the two.
                const double d = std::min(distanceToSegment(centre, aA, aV), distanceToSegment(centre, aV, aB));
                const double bound = capsuleCoverage(d, body);
                const double measured = probe.coverage(aRendered.image, x, y);
                if (measured - bound > worst.excess) {
                    worst.excess = measured - bound;
                    worst.measured = measured;
                    worst.bound = bound;
                    worst.at = QPoint(x, y);
                }
            }
        }
        return worst;
    };
    const Worst before = scan(aBefore, kCornerA, kCornerB, kCornerV);
    const Worst after = scan(aAfter, kCornerA, kCornerB, kCornerV);

    // THE PIXELS AROUND THE VERTEX, as the excess of each pixel's own coverage over what ONE
    // capsule's profile allows at that pixel's centre - printed as whole percents, so a digit
    // is the claim itself: a stroke that matches the union shows zeros, and a stroke that
    // blends its segments twice shows the fringe pixels one or two steps dark.
    const auto excessGrid = [&probe, body](const Rendered& aRendered, const QPointF& aA, const QPointF& aB,
                                           const QPointF& aV) {
        QString text;
        for (int y = int(aV.y()) - 2; y <= int(aV.y()) + 2; ++y) {
            for (int x = int(aV.x()) - 2; x <= int(aV.x()) + 2; ++x) {
                const QPointF centre(double(x) + 0.5, double(y) + 0.5);
                const double d = std::min(distanceToSegment(centre, aA, aV), distanceToSegment(centre, aV, aB));
                const double excess = probe.coverage(aRendered.image, x, y) - capsuleCoverage(d, body);
                const int step = int(std::floor(excess * 100.0 + 0.5));
                text += step < 0 ? QLatin1Char('-') : QLatin1Char(char('0' + std::min(9, step)));
            }
            text += QLatin1Char(' ');
        }
        return text;
    };
    std::printf(
        "MEASURED stroke_join_box pixels=%d..%d x %d..%d before=[%s] after=[%s] (percents of coverage over"
        " one capsule's own profile, one digit per pixel, rows top to bottom)\n",
        int(kCornerV.y()) - 2, int(kCornerV.y()) + 2, int(kCornerV.x()) - 2, int(kCornerV.x()) + 2,
        qPrintable(excessGrid(aBefore, kCornerA, kCornerB, kCornerV)),
        qPrintable(excessGrid(aAfter, kCornerA, kCornerB, kCornerV))
    );

    std::printf(
        "MEASURED stroke_join vertex=(%d,%d) body_coverage=%.3f arms=A(%d,%d)->V(%d,%d)->B(%d,%d)\n",
        int(kCornerV.x()), int(kCornerV.y()), body, int(kCornerA.x()), int(kCornerA.y()), int(kCornerV.x()),
        int(kCornerV.y()), int(kCornerB.x()), int(kCornerB.y())
    );
    std::printf(
        "MEASURED stroke_join_worst after_excess=%+.3f after_measured=%.3f after_bound=%.3f at=(%d,%d)"
        " before_excess=%+.3f before_measured=%.3f before_bound=%.3f at=(%d,%d)\n",
        after.excess, after.measured, after.bound, after.at.x(), after.at.y(), before.excess, before.measured,
        before.bound, before.at.x(), before.at.y()
    );
    // THE ASSERTION: no pixel within 5px of the vertex carries more ink than the nearest
    // capsule's own profile allows at that pixel's own distance. A stroke that composites its
    // segments one after another breaks this by up to (1-a) of the covered amount.
    REQUIRE(
        after.excess <= 0.08,
        "the join is darker than a single segment can explain: %.3f of coverage over the bound %.3f"
        " (measured %.3f) at (%d,%d)",
        after.excess, after.bound, after.measured, after.at.x(), after.at.y()
    );
    // ... and no pixel there is darker than the stroke's own body.
    REQUIRE(
        after.measured <= body + 0.02,
        "the join is darker than the stroke's body: %.3f at (%d,%d) against a body of %.3f", after.measured,
        after.at.x(), after.at.y(), body
    );
    // The measurement DISCRIMINATES: the fringe stroke this pass replaces is the one the bound
    // catches (its segments' fringes are blended over each other). If that ever stops being
    // true, the assertion above is no longer measuring what it claims to.
    REQUIRE(
        before.excess > after.excess,
        "the join bound does not separate the two strokes: before %+.3f, after %+.3f", before.excess,
        after.excess
    );
    return true;
}

// 25: THE INK IS STILL WHERE THE POLYLINE IS. The stroke's width and its joins were the
// defect; its POSITION is not up for negotiation. Three statements, all of them about the
// shipping frame: every sample of every run has ink on the pixel under it, every inked pixel
// of the frame is inside the runs' own bounds (grown by the stroke's whole footprint), and
// the coverage-weighted centre of the stroke's cross-section is on the centreline.
bool checkStrokePosition(const Rendered& aAfter, const Rendered& aBefore, const theme::Graph& aTheme) {
    REQUIRE(gui::stroke::isReady(), "precondition: the coverage pass must be built for this assertion");

    const InkProbe probe = InkProbe::make(aTheme.keyColor(kTypeA));

    // (a) the ink covers the polyline: 17 samples of every segment, the ends included.
    double worst = 1.0;
    QPoint worstAt;
    const std::vector<std::pair<QPointF, QPointF>> segments = {
        {kFlatA, kFlatB}, {kPhaseA, kPhaseB}, {kDiagonalA, kDiagonalB}, {kSteepA, kSteepB}, {kCornerA, kCornerV},
        {kCornerV, kCornerB},
    };
    for (const auto& segment : segments) {
        for (int i = 0; i <= 16; ++i) {
            const double t = double(i) / 16.0;
            const double px = segment.first.x() + t * (segment.second.x() - segment.first.x());
            const double py = segment.first.y() + t * (segment.second.y() - segment.first.y());
            const int x = int(std::floor(px));
            const int y = int(std::floor(py));
            const double cov = probe.coverage(aAfter.image, x, y);
            if (cov < worst) {
                worst = cov;
                worstAt = QPoint(x, y);
            }
        }
    }
    std::printf(
        "MEASURED stroke_position samples=%d worst_coverage=%.3f at=(%d,%d) tolerance=0.45\n",
        int(segments.size()) * 17, worst, worstAt.x(), worstAt.y()
    );
    REQUIRE(
        worst >= 0.45, "the stroke does not cover its own polyline: a sample at (%d,%d) carries only %.3f",
        worstAt.x(), worstAt.y(), worst
    );

    // (b) no ink anywhere the runs are not. The bound is every run's own bounding box, grown
    // by the stroke's whole footprint (half the width, half the filter, and a pixel).
    QRect allowed;
    const int reach = 3;
    for (const InkRun& run : kInkRuns)
        allowed = allowed.united(grow(QRect(int(std::min(run.a.x(), run.b.x())), int(std::min(run.a.y(), run.b.y())), int(std::abs(run.a.x() - run.b.x())) + 1, int(std::abs(run.a.y() - run.b.y())) + 1), reach));
    allowed = allowed.united(grow(QRect(int(kCornerA.x()), int(kCornerA.y()), int(kCornerV.x() - kCornerA.x()) + 1, int(kCornerB.y() - kCornerA.y()) + 1), reach));
    for (const QPointF& point : denseWavyPoints())
        allowed = allowed.united(grow(QRect(int(point.x()), int(point.y()), 1, 1), reach));
    QRect inked;
    for (int y = kPlot.top(); y <= kPlot.bottom(); ++y) {
        for (int x = kPlot.left(); x <= kPlot.right(); ++x) {
            if (probe.coverage(aAfter.image, x, y) > 0.1) {
                const QRect pixel(x, y, 1, 1);
                inked = inked.isNull() ? pixel : inked.united(pixel);
            }
        }
    }
    std::printf(
        "MEASURED stroke_position ink_bbox=(%d,%d %dx%d) allowed=(%d,%d %dx%d) reach=%dpx\n", inked.left(),
        inked.top(), inked.width(), inked.height(), allowed.left(), allowed.top(), allowed.width(),
        allowed.height(), reach
    );
    REQUIRE(!inked.isNull(), "precondition: the ink model painted nothing at all");
    REQUIRE(
        allowed.contains(inked), "the stroke painted outside its own polylines: ink at (%d,%d %dx%d)",
        inked.left(), inked.top(), inked.width(), inked.height()
    );

    // (c) the cross-section's own centre: the coverage-weighted centroid of each perpendicular
    // profile must sit on the centreline, for the grid-aligned run and the half-phase one.
    struct Centroid {
        const char* name;
        QPointF a;
        QPointF b;
    };
    const Centroid centroids[] = {
        {"horizontal", kFlatA, kFlatB}, {"horizontal_half_pixel_off", kPhaseA, kPhaseB},
    };
    for (const Centroid& run : centroids) {
        int rowFirst = 0;
        int rowLast = 0;
        profileRows(run.a.y(), rowFirst, rowLast);
        const std::vector<double> profile = columnProfile(aAfter.image, probe, 185, rowFirst, rowLast);
        double weight = 0.0;
        double moment = 0.0;
        for (size_t i = 0; i < profile.size(); ++i) {
            const double y = double(rowFirst) + double(i) + 0.5;
            weight += profile[i];
            moment += profile[i] * y;
        }
        const double centroid = weight > 0.0 ? moment / weight : -1.0;
        std::printf(
            "MEASURED stroke_position centroid run=%-24s centre=%.1f measured=%.3f offset=%+.3fpx\n",
            run.name, run.a.y(), centroid, centroid - run.a.y()
        );
        REQUIRE(
            std::abs(centroid - run.a.y()) <= 0.5, "run %s: the ink's centre is %.3fpx off the centreline",
            run.name, centroid - run.a.y()
        );
    }

    // (d) the ENDS are the capsule's own round caps - the reason the pass measures the
    // distance to the SEGMENT and not to its infinite line. The pixels beside an endpoint
    // and inside the stroke's own half width are part of the cap's disc; a stroke whose ends
    // are cut square (which is what a quad strip's end is) puts nothing there at all.
    struct EndCap {
        const char* name;
        QPointF end;  // the polyline's own endpoint
        QPointF ask;  // the pixel to read: its CENTRE must be a corner of the cap's disc
        int outward;  // the direction that leaves the stroke, in pixels
    };
    const double half = double(gui::stroke::kWidthPx) * 0.5;
    const EndCap caps[] = {
        {"horizontal_start", kFlatA, QPointF(109, 60), -1},
        {"horizontal_end", kFlatB, QPointF(260, 60), +1},
    };
    for (const EndCap& cap : caps) {
        const QPointF centre(cap.ask.x() + 0.5, cap.ask.y() + 0.5);
        const double d = std::hypot(centre.x() - cap.end.x(), centre.y() - cap.end.y());
        REQUIRE(
            d < half, "precondition: the cap probe %s is %.2fpx from the endpoint, not inside %.1fpx", cap.name, d,
            half
        );
        const double after = probe.coverage(aAfter.image, int(cap.ask.x()), int(cap.ask.y()));
        const double before = probe.coverage(aBefore.image, int(cap.ask.x()), int(cap.ask.y()));
        const double beyond = probe.coverage(aAfter.image, int(cap.ask.x()) + 2 * cap.outward, int(cap.ask.y()));
        std::printf(
            "MEASURED stroke_position_cap %s endpoint=(%.1f,%.1f) probe=(%d,%d) distance=%.2fpx after=%.3f"
            " before=%.3f two_px_beyond=%.3f\n",
            cap.name, cap.end.x(), cap.end.y(), int(cap.ask.x()), int(cap.ask.y()), d, after, before, beyond
        );
        REQUIRE(
            after >= 0.5, "%s: the stroke's end carries %.3f of ink where the capsule's own round cap covers the"
            " pixel (the fringe stroke puts %.3f there)",
            cap.name, after, before
        );
        REQUIRE(beyond <= 0.05, "%s: the cap spills two pixels past its own end: %.3f", cap.name, beyond);
    }
    return true;
}

// 26: THE PASS IS A GUEST IN THE FRAME - the same rule the contrast outline's own check pins,
// and for the same reason. The ImGui backend keeps its shader program, its vertex array and
// its ARRAY BUFFER bound across the commands of a draw list and uploads the NEXT draw list's
// vertices with whatever array buffer is bound when it gets there, so the one place a leak
// would be invisible in a single-draw-list frame is exactly the place it would be fatal. The
// callback is therefore driven directly, with the participants bound to markers, from the
// command ImGui itself recorded for this frame.
bool checkStrokeGuestState() {
    REQUIRE(gui::stroke::isReady(), "precondition: the coverage pass must be built for this assertion");

    // The command the panel inserted: a callback command carrying this curve's own geometry.
    // Finding it is itself the structural assertion that the pass is IN the draw list (and so
    // between the ticks and the key markers) rather than a pass over the finished frame.
    ImDrawData* drawData = ImGui::GetDrawData();
    REQUIRE(drawData != nullptr, "precondition: the frame must still be current");
    const ImDrawList* list = nullptr;
    const ImDrawCmd* command = nullptr;
    int callbacks = 0;
    for (int i = 0; i < drawData->CmdListsCount; ++i) {
        const ImDrawList* candidate = drawData->CmdLists[i];
        for (int c = 0; c < candidate->CmdBuffer.Size; ++c) {
            if (candidate->CmdBuffer[c].UserCallback != gui::stroke::drawListCallback)
                continue;
            ++callbacks;
            if (!list) {
                list = candidate;
                command = &candidate->CmdBuffer[c];
            }
        }
    }
    REQUIRE(callbacks > 0, "the frame carries no stroke callback at all: drawPanel did not insert the pass");
    REQUIRE(command->UserCallbackData != nullptr, "the stroke callback carries no geometry");

    // Every participant the pass could disturb, bound to a marker: its own vertex array, its
    // own texture on unit 0 with a NON-ZERO active unit, its own array buffer, and a real
    // program (the pass binds two of its own).
    auto& funcs = gl::Global::functions();
    gl::VertexArrayObject markerVao;
    markerVao.bind();
    gl::Texture markerTexture;
    markerTexture.create(QSize(4, 4));
    gl::BufferObject markerBuffer(GL_ARRAY_BUFFER);
    markerBuffer.bind();
    gl::EasyShaderProgram markerProgram;
    REQUIRE(
        markerProgram.setVertexSource(QStringLiteral(
            "#version 400 core\nvoid main() { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); }\n"
        )) && markerProgram.setFragmentSource(QStringLiteral(
            "#version 400 core\nout vec4 c;\nvoid main() { c = vec4(0.0); }\n"
        )) && markerProgram.link(),
        "precondition: the marker program did not build"
    );
    markerProgram.bind();
    funcs.glActiveTexture(GL_TEXTURE1);
    funcs.glBindTexture(GL_TEXTURE_2D, markerTexture.id());
    funcs.glActiveTexture(GL_TEXTURE0);
    funcs.glBindTexture(GL_TEXTURE_2D, markerTexture.id());
    funcs.glActiveTexture(GL_TEXTURE1);
    // The rest of the state the pass touches, set to the values the ImGui backend itself
    // keeps while a draw list is being rendered - so "the state it found is the state it
    // leaves" is a statement about the backend's own state and not about the harness's.
    markerProgram.bind();
    funcs.glEnable(GL_BLEND);
    funcs.glBlendEquation(GL_FUNC_ADD);
    funcs.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    funcs.glEnable(GL_SCISSOR_TEST);
    funcs.glScissor(kPlot.left(), kPlot.top(), kPlot.width(), kPlot.height());

    gFbo->bind();
    funcs.glDisable(GL_SCISSOR_TEST);
    funcs.glScissor(0, 0, kViewW, kViewH);
    funcs.glViewport(0, 0, kViewW, kViewH);
    funcs.glClearColor(kClear.redF(), kClear.greenF(), kClear.blueF(), 1.0f);
    funcs.glClear(GL_COLOR_BUFFER_BIT);

    // Every piece of state the pass has any business touching, read before and after the
    // call: the assertion is that the two are the same, so a leak cannot be mistaken for a
    // value this harness happened to set.
    struct GuestState {
        GLint program = 0;
        GLint vao = 0;
        GLint arrayBuffer = 0;
        GLint activeTexture = 0;
        GLint texture1 = 0;
        GLint drawFbo = 0;
        GLint viewport[4] = {0, 0, 0, 0};
        GLint scissor[4] = {0, 0, 0, 0};
        GLint blend = 0;
        GLint dither = 0;
        GLint equation = 0;
        GLint src = 0;
        GLint dst = 0;
    };
    const auto readState = [&funcs]() {
        GuestState state;
        funcs.glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
        funcs.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vao);
        funcs.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
        funcs.glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
        funcs.glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture1);
        funcs.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state.drawFbo);
        funcs.glGetIntegerv(GL_VIEWPORT, state.viewport);
        funcs.glGetIntegerv(GL_SCISSOR_BOX, state.scissor);
        funcs.glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.equation);
        funcs.glGetIntegerv(GL_BLEND_SRC_RGB, &state.src);
        funcs.glGetIntegerv(GL_BLEND_DST_RGB, &state.dst);
        state.blend = funcs.glIsEnabled(GL_BLEND) ? 1 : 0;
        state.dither = funcs.glIsEnabled(GL_DITHER) ? 1 : 0;
        return state;
    };
    const GuestState found = readState();
    const bool quiet = noGlError("the harness's own state before the stroke callback");

    gui::stroke::drawListCallback(list, command);

    const GuestState left = readState();

    // The callback drew: its ink is in the framebuffer it was handed, through its own
    // scratch and composite, and not one pixel outside the plot.
    const QImage after = readFboTopDown();
    const int ink = inkCount(after, QRect(kPlot.left(), kPlot.top(), kPlot.width(), kPlot.height()));
    const int outside = inkCount(after, QRect(0, 0, kViewW, kViewH))
        - inkCount(after, QRect(0, 0, kViewW, kViewH).intersected(QRect(kPlot.left(), kPlot.top(), kPlot.width(), kPlot.height())));
    std::printf(
        "MEASURED stroke_state_restored callbacks=%d quiet_before=%d ink_in_plot=%d ink_outside=%d\n", callbacks,
        quiet ? 1 : 0, ink, outside
    );
    std::printf(
        "MEASURED stroke_state_restored found: program=%d vao=%d array_buffer=%d active_texture=0x%x texture1=%d"
        " draw_fbo=%d viewport=%d,%d,%d,%d scissor=%d,%d,%d,%d blend=%d dither=%d equation=0x%x src=0x%x dst=0x%x\n",
        found.program, found.vao, found.arrayBuffer, found.activeTexture, found.texture1, found.drawFbo,
        found.viewport[0], found.viewport[1], found.viewport[2], found.viewport[3], found.scissor[0],
        found.scissor[1], found.scissor[2], found.scissor[3], found.blend, found.dither, GLuint(found.equation),
        GLuint(found.src), GLuint(found.dst)
    );
    std::printf(
        "MEASURED stroke_state_restored left:  program=%d vao=%d array_buffer=%d active_texture=0x%x texture1=%d"
        " draw_fbo=%d viewport=%d,%d,%d,%d scissor=%d,%d,%d,%d blend=%d dither=%d equation=0x%x src=0x%x dst=0x%x\n",
        left.program, left.vao, left.arrayBuffer, left.activeTexture, left.texture1, left.drawFbo, left.viewport[0],
        left.viewport[1], left.viewport[2], left.viewport[3], left.scissor[0], left.scissor[1], left.scissor[2],
        left.scissor[3], left.blend, left.dither, GLuint(left.equation), GLuint(left.src), GLuint(left.dst)
    );

    REQUIRE(ink > 200, "the callback, driven directly, painted %d pixels of ink", ink);
    REQUIRE(outside == 0, "the callback painted %d pixels outside the plot's own clip", outside);
    // The state it was handed is not an accident of the harness: it is the backend's own.
    REQUIRE(
        found.program == GLint(markerProgram.id()) && found.vao == GLint(markerVao.id())
            && found.arrayBuffer == GLint(markerBuffer.id()) && found.activeTexture == GLint(GL_TEXTURE1)
            && found.texture1 == GLint(markerTexture.id()) && found.blend == 1
            && found.equation == GLint(GL_FUNC_ADD) && found.src == GLint(GL_SRC_ALPHA)
            && found.dst == GLint(GL_ONE_MINUS_SRC_ALPHA),
        "precondition: the harness could not present the pass with the frame's own bindings"
    );

    REQUIRE(
        left.program == found.program,
        "the pass left program %d bound, not the %d it found (the ImGui backend keeps ITS program bound across a"
        " draw list's commands, and every command after this one would be drawn with the wrong shader)",
        left.program, found.program
    );
    REQUIRE(
        left.vao == found.vao, "the pass left vertex array %d bound, not the %d it found", left.vao, found.vao
    );
    REQUIRE(
        left.arrayBuffer == found.arrayBuffer,
        "the pass left array buffer %d bound, not the %d it found (the backend uploads the next draw list's"
        " vertices with whatever is bound)",
        left.arrayBuffer, found.arrayBuffer
    );
    REQUIRE(
        left.activeTexture == found.activeTexture && left.texture1 == found.texture1,
        "the pass left texture unit 0x%x with texture %d bound, not the 0x%x/%d it found", left.activeTexture,
        left.texture1, found.activeTexture, found.texture1
    );
    REQUIRE(
        left.drawFbo == found.drawFbo,
        "the pass ended with FBO %d bound, not the %d it was asked to draw into", left.drawFbo, found.drawFbo
    );
    REQUIRE(
        left.scissor[0] == found.scissor[0] && left.scissor[1] == found.scissor[1]
            && left.scissor[2] == found.scissor[2] && left.scissor[3] == found.scissor[3],
        "the pass left the scissor box %d,%d,%d,%d, not the %d,%d,%d,%d it found", left.scissor[0],
        left.scissor[1], left.scissor[2], left.scissor[3], found.scissor[0], found.scissor[1], found.scissor[2],
        found.scissor[3]
    );
    REQUIRE(
        left.viewport[0] == found.viewport[0] && left.viewport[1] == found.viewport[1]
            && left.viewport[2] == found.viewport[2] && left.viewport[3] == found.viewport[3],
        "the pass left the viewport %d,%d,%d,%d, not the %d,%d,%d,%d it found", left.viewport[0],
        left.viewport[1], left.viewport[2], left.viewport[3], found.viewport[0], found.viewport[1],
        found.viewport[2], found.viewport[3]
    );
    REQUIRE(
        left.blend == found.blend && left.equation == found.equation && left.src == found.src
            && left.dst == found.dst && left.dither == found.dither,
        "the pass left blending as enabled %d equation 0x%x src 0x%x dst 0x%x (dither %d), not the"
        " %d/0x%x/0x%x/0x%x (dither %d) it found",
        left.blend, GLuint(left.equation), GLuint(left.src), GLuint(left.dst), left.dither, found.blend,
        GLuint(found.equation), GLuint(found.src), GLuint(found.dst), found.dither
    );
    REQUIRE(noGlError("the stroke pass's own state"), "the pass left a GL error behind");
    return true;
}

// The BEFORE/AFTER crops the report is read from: the same regions of the same two frames,
// magnified by the same nearest-neighbour zoom (a crop has to show the PIXELS, not a resampled
// interpretation of them), as PNGs under build/shots/graph_ink/.
bool writeInkCrops(const Rendered& aBefore, const Rendered& aAfter, const QString& aDir) {
    REQUIRE(QDir().mkpath(aDir), "the crop directory %s could not be created", qPrintable(aDir));

    struct Crop {
        const char* name;
        QRect region;
        int zoom;
    };
    const auto around = [](const QPointF& aA, const QPointF& aB) {
        const QPointF mid((aA.x() + aB.x()) * 0.5, (aA.y() + aB.y()) * 0.5);
        return QRect(int(mid.x()) - 16, int(mid.y()) - 16, 32, 32);
    };
    const Crop crops[] = {
        {"horizontal", around(kFlatA, kFlatB), 6},
        {"horizontal_half_pixel_off", around(kPhaseA, kPhaseB), 6},
        {"diagonal45", around(kDiagonalA, kDiagonalB), 6},
        {"steep72", around(kSteepA, kSteepB), 6},
        {"join90", QRect(int(kCornerV.x()) - 12, int(kCornerV.y()) - 12, 24, 24), 8},
        // The dense wavy fixture, cropped where it turns hardest: the "along the line" case.
        {"dense_curve_turn", QRect(150, 238, 32, 32), 6},
        // The horizontal run's END: the round cap against the fringe stroke's square end.
        {"cap_end", QRect(int(kFlatB.x()) - 12, int(kFlatB.y()) - 12, 24, 24), 8},
    };

    for (const Crop& crop : crops) {
        const QRect region = crop.region.intersected(QRect(0, 0, kViewW, kViewH));
        REQUIRE(!region.isEmpty(), "precondition: crop %s is outside the framebuffer", crop.name);
        const QStringList tags = {QStringLiteral("before"), QStringLiteral("after")};
        const Rendered* frames[] = {&aBefore, &aAfter};
        for (int i = 0; i < 2; ++i) {
            const QImage magnified = frames[i]->image.copy(region).scaled(
                region.size() * crop.zoom, Qt::IgnoreAspectRatio, Qt::FastTransformation
            );
            const QString path = QStringLiteral("%1/%2_%3.png").arg(aDir, tags.at(i), QString::fromLatin1(crop.name));
            REQUIRE(
                magnified.save(path, "PNG"), "the crop %s could not be written as a PNG", qPrintable(path)
            );
            std::printf(
                "MEASURED ink_crop file=%s region=(%d,%d %dx%d) zoom=%d pixels=%dx%d\n", qPrintable(path),
                region.left(), region.top(), region.width(), region.height(), crop.zoom, magnified.width(),
                magnified.height()
            );
        }
    }
    return true;
}

// 27: SUB-PIXEL PLACEMENT OF THE PANEL'S OTHER 1px LINES. MEASURED FIRST, and the answer is
// a REFUTATION, which is why nothing about these lines was changed:
//
//   * the non-AA family (ticks, zero rule, playhead) is only ever asked for an INTEGER
//     column, and at an integer column the hard-edged line is EXACT: one column at full
//     coverage, its neighbours at zero, and the column is the one the model named, to the
//     pixel. There is no phase for it to lose, so the "it jumps a whole pixel" the report
//     describes is not the renderer's - it is the model's own integer
//     (GraphPanelModel::playheadX is an `int`, so a fractional frame position is resolved
//     before the drawing layer sees it, and a fix belongs in that model rather than here).
//   * what routing the same 1px line through the coverage pass WOULD do is measured too,
//     with the pass's own coverage function (the function assertion 24 verifies against the
//     drawn ink to three decimals): a 1px stroke on a pixel boundary puts 0.500 in EACH of
//     the two columns the boundary divides. That is a two-column grey line where the panel
//     today has one crisp column - the exact trade the panel's own hairline comment names -
//     so the chrome stays as it is, and this assertion is what fails if someone routes it
//     through the pass without re-deriving that decision.
//   * the handle STEM - 1px, AA on, fractional coordinates - was measured FIRST and was
//     WRONG: at three stations along it the ink's centroid sat +0.68, +0.75 and +0.76px off
//     the line the model asked for, because ImGui's AddLine shifts its geometry by
//     (+0.5,+0.5) (its own 1px convention) and that shift is a perpendicular move of up to
//     0.71px on a diagonal stem (0.699 for this fixture). It is now drawn by the same
//     coverage pass as the curves, and re-measured at -0.01, -0.00 and +0.01px -
//     same colour, same 1px width, same order (between the key marker and its own dot), same
//     clip - and the measurement below is taken again on the result: the centroid within
//     0.5px of the line, and partial coverage still at both of its edges.
// Nothing about the integer chrome was changed, and that is what the assertion pins.
bool checkHairlinePlacement(
    const Rendered& aPlayhead, const Rendered& aPlayheadNext, const Rendered& aStem, const Rendered& aStemFringe,
    const theme::Graph& aTheme
) {
    REQUIRE(!aPlayhead.image.isNull() && !aPlayheadNext.image.isNull() && !aStem.image.isNull()
                && !aStemFringe.image.isNull(),
            "precondition: the hairline passes were not rendered");

    // (a) the playhead at the integer the model publishes: one column, full, exactly there.
    const InkProbe playhead = InkProbe::make(aTheme.selected());
    int x = 200;
    const std::vector<double> ink = columnInk(aPlayhead, playhead, x - 2, x + 2);
    const std::vector<double> inkNext = columnInk(aPlayheadNext, playhead, x - 2, x + 2);
    QString profile;
    QString profileNext;
    for (size_t i = 0; i < ink.size(); ++i) {
        profile += QStringLiteral("%1 ").arg(ink[i], 0, 'f', 1);
        profileNext += QStringLiteral("%1 ").arg(inkNext[i], 0, 'f', 1);
    }
    // The ink's own centre of mass across the columns, in columns: the position a line
    // placement claim is about. (The column at index i spans [x-2+i, x-1+i] and its centre
    // is x-1.5+i.)
    const auto centroidColumn = [&x](const std::vector<double>& aInk) {
        double weight = 0.0;
        double moment = 0.0;
        for (size_t i = 0; i < aInk.size(); ++i) {
            weight += aInk[i];
            moment += aInk[i] * (double(x) - 1.5 + double(i));
        }
        return weight > 0.0 ? moment / weight : -1.0;
    };
    const double centre = centroidColumn(ink);
    const double centreNext = centroidColumn(inkNext);
    const double rows = double(kPlot.height());
    std::printf(
        "MEASURED hairline_playhead model_x=%d columns=%d..%d ink_per_column=[%s] (full column = %.0f) centroid=%.3f"
        " (column centre %.1f) model_x+1 ink=[%s] centroid=%.3f\n",
        x, x - 2, x + 2, qPrintable(profile), rows, centre, double(x) + 0.5, qPrintable(profileNext), centreNext
    );
    // The measured claim: the drawn column is the model's own, fully covered, and its
    // neighbours carry nothing - no gain, no loss, no bleed.
    REQUIRE(
        ink[2] >= rows - 1.0, "the playhead's column at x=%d is only %.1f covered of %0.f rows", x, ink[2], rows
    );
    REQUIRE(
        ink[0] <= 0.5 && ink[1] <= 0.5 && ink[3] <= 0.5 && ink[4] <= 0.5,
        "the playhead bleeds outside its own column at x=%d: [%s]", x, qPrintable(profile)
    );
    REQUIRE(
        std::abs(centre - (double(x) + 0.5)) <= 0.05,
        "the playhead's ink centre is at %.3f, not the centre of the column it was asked for (%.1f)", centre,
        double(x) + 0.5
    );
    REQUIRE(
        inkNext[3] >= rows - 1.0,
        "moving the model one column did not move the line: the column at x+1 carries %.1f of %0.f rows",
        inkNext[3], rows
    );

    // (b) what the SAME 1px line would be through the coverage pass, at the same phase: the
    // stroke's centreline on the boundary between two columns puts half the ink in each. The
    // function below is the pass's own arithmetic (assertion 24 measures the drawn ink
    // against it to three decimals), applied to a 1px stroke: halfWidth 0.5.
    const double split = capsuleCoverage(0.5, 1.0, 0.5);
    std::printf(
        "MEASURED hairline_coverage_alternative line=1px phase=boundary (centreline on the column edge) column_left=%.3f"
        " column_right=%.3f columns=2 drawn_today=1 (column %d at %.1f)\n",
        split, split, x, ink[2]
    );
    REQUIRE(
        split >= 0.45 && split <= 0.55,
        "the coverage arithmetic no longer splits a 1px line on a boundary 50/50 (%.3f): the hairline decision below"
        " needs re-deriving",
        split
    );

    // (c) the handle stem, as drawn: 1px, AA on, fractional endpoints. The offset is measured
    // the only way it means anything - every pixel CENTRE in a window around the station,
    // weighted by its own coverage and by its own SIGNED perpendicular distance from the line
    // the model asked for - so the answer is "where is the ink, in pixels, relative to the
    // line", not "how far does some sampling scheme drift".
    const InkProbe stem = InkProbe::make(aTheme.handleLine());
    const QPointF direction = kStemDot - kStemBase;
    const double length = std::hypot(direction.x(), direction.y());
    const QPointF along(direction.x() / length, direction.y() / length);
    const QPointF across(-along.y(), along.x());
    // Both strokes, one method: the fringes' stem (ImGui's AddLine, the shipping path) and
    // the pass's - so the two numbers are comparable rather than two instruments.
    struct StemMeasure {
        double worstOffset = 0.0;
        double worstInk = 0.0;
        int ramps = 0;
        QString stations;
    };
    const auto measureStem = [&stem, &along, &across, &direction](const Rendered& aRendered) {
        StemMeasure out;
        for (double t : {0.25, 0.5, 0.75}) {
            const QPointF at(kStemBase.x() + t * direction.x(), kStemBase.y() + t * direction.y());
            const int originX = int(std::floor(at.x()));
            const int originY = int(std::floor(at.y()));
            double weight = 0.0;
            double moment = 0.0;
            double partials = 0.0;
            for (int dy = -3; dy <= 3; ++dy) {
                for (int dx = -3; dx <= 3; ++dx) {
                    const QPointF centre(double(originX + dx) + 0.5, double(originY + dy) + 0.5);
                    const QPointF rel(centre.x() - at.x(), centre.y() - at.y());
                    const double s = rel.x() * along.x() + rel.y() * along.y();
                    const double d = rel.x() * across.x() + rel.y() * across.y();
                    if (std::abs(s) > 2.0 || std::abs(d) > 2.5)
                        continue;
                    const double cov = stem.coverage(aRendered.image, originX + dx, originY + dy);
                    weight += cov;
                    moment += cov * d;
                    if (cov > 0.15 && cov < 0.85)
                        partials += 1.0;
                }
            }
            const double offset = weight > 0.0 ? moment / weight : 0.0;
            const double inked = weight / 4.0;
            out.worstOffset = std::max(out.worstOffset, std::abs(offset));
            out.worstInk = std::max(out.worstInk, std::abs(inked - 1.0));
            if (partials > 0.0)
                out.ramps += 1;
            out.stations += QStringLiteral("t%1:off%2/partials%3/ink%4 ")
                                .arg(t, 0, 'f', 2)
                                .arg(QString::asprintf("%+.2f", offset))
                                .arg(int(partials))
                                .arg(inked, 0, 'f', 2);
        }
        return out;
    };
    const StemMeasure stemFringe = measureStem(aStemFringe);
    const StemMeasure stemPass = measureStem(aStem);

    std::printf(
        "MEASURED hairline_stem base=(%.2f,%.2f) dot=(%.2f,%.2f) length=%.1fpx tolerance=0.50px (offset: + is one"
        " pixel to one side of the requested line; ink: px per px of stem, 1.00 for a 1px stroke)\n",
        kStemBase.x(), kStemBase.y(), kStemDot.x(), kStemDot.y(), length
    );
    std::printf(
        "MEASURED hairline_stem_fringe stations=[%s] worst_centroid_offset=%.2fpx worst_ink_error=%.2fpx (ImGui's"
        " AddLine, the stroke the panel shipped)\n",
        qPrintable(stemFringe.stations), stemFringe.worstOffset, stemFringe.worstInk
    );
    std::printf(
        "MEASURED hairline_stem_pass stations=[%s] worst_centroid_offset=%.2fpx worst_ink_error=%.2fpx (the coverage"
        " pass)\n",
        qPrintable(stemPass.stations), stemPass.worstOffset, stemPass.worstInk
    );
    REQUIRE(
        stemPass.ramps == 3,
        "the handle stem's edges carry no partial coverage at %d of 3 stations: it is not antialiased",
        3 - stemPass.ramps
    );
    REQUIRE(
        stemPass.worstOffset <= 0.5,
        "the handle stem's ink sits %.2fpx off the line the model asked for", stemPass.worstOffset
    );
    REQUIRE(
        stemPass.worstInk <= 0.25, "the handle stem loses ink: %.2fpx per px of stem against 1.00 for a 1px stroke",
        stemPass.worstInk
    );
    // The measurement discriminates: ImGui's own AddLine is the stroke that puts the ink off
    // the line, which is what made routing the stems through the pass worth doing.
    REQUIRE(
        stemFringe.worstOffset > stemPass.worstOffset,
        "the stem measurement does not separate the two strokes: AddLine %.2fpx, the pass %.2fpx",
        stemFringe.worstOffset, stemPass.worstOffset
    );
    return true;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // A live core-profile context, exactly as tests/overlay_engine_primitives.cpp
    // and roundtrip_main.cpp take one. The format has to be the default before the
    // widget creates its context, which happens on the first show()/makeCurrent().
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    fmt.setSamples(4);
    QSurfaceFormat::setDefaultFormat(fmt);

    QOpenGLWidget glWidget;
    glWidget.resize(kViewW, kViewH);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::fprintf(stderr, "FAIL: no GL context could be created\n");
        return 2;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::fprintf(stderr, "FAIL: no GL function table for the widget's context\n");
        return 2;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    gl::DeviceInfo device;
    device.vender = "harness";
    device.renderer = "harness";
    device.version = "4.0";
    device.maxTextureSize = 16384;
    device.maxRenderBufferSize = 16384;
    gl::DeviceInfo::setInstance(&device);

    // Every rect in this file is in the panel's own coordinates, and ImGui's
    // projection is built from io.DisplaySize: a window manager that resized this
    // widget would move every sampled pixel, so the size is asserted.
    if (glWidget.width() != kViewW || glWidget.height() != kViewH) {
        std::fprintf(
            stderr, "FAIL: the widget is %dx%d, not %dx%d\n", glWidget.width(), glWidget.height(), kViewW, kViewH
        );
        return 2;
    }

    std::printf(
        "INFO GL renderer=%s version=%s\n",
        reinterpret_cast<const char*>(funcs->glGetString(GL_RENDERER)),
        reinterpret_cast<const char*>(funcs->glGetString(GL_VERSION))
    );

    // A pending GL error would be turned into an abort by GL_CHECK_ERROR on the
    // first call below and read as that call's fault. Drop what the widget's setup
    // left behind.
    {
        GLuint glError = 0;
        while ((glError = funcs->glGetError()) != GL_NO_ERROR) {
        }
    }

    // A core-profile draw with no VAO bound is GL_INVALID_OPERATION.
    gl::VertexArrayObject vao;
    vao.bind();

    // This harness's OWN framebuffer, never the widget's (see the file header).
    gl::Texture colorTexture;
    colorTexture.create(QSize(kViewW, kViewH));
    gl::Framebuffer fbo;
    fbo.setColorAttachment(0, colorTexture.id());
    if (!fbo.isComplete()) {
        std::fprintf(stderr, "FAIL: the harness framebuffer is incomplete\n");
        return 2;
    }
    gFbo = &fbo;

    // The ImGui context. NO renderer backend other than the GL3 one compiled
    // through src/gui/ImGuiGLBackend.cpp: that TU carries the loader shim over
    // gl::Global::functions(), which is why the app's function table has to be
    // installed before ImGui_ImplOpenGL3_Init().
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // never write imgui.ini from a test
    io.LogFilename = nullptr;
    if (!io.Fonts->AddFontDefaultBitmap()) {
        std::fprintf(stderr, "FAIL: ImGui's default bitmap font could not be added\n");
        return 2;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 400 core")) {
        std::fprintf(stderr, "FAIL: the GL3 backend refused to initialize\n");
        return 2;
    }

    const theme::Graph palette = theme::Graph::from(theme::Colors::current());
    std::printf(
        "INFO palette halo=%s axisText=%s gridMajor=%s axisLine=%s selected=%s keyEdge=%s moveKey=%s"
        " scaleKey=%s marqueeFill=%s marqueeEdge=%s menuBody=%s menuHover=%s menuHoverText=%s clear=%s\n",
        hex(palette.halo()).c_str(), hex(palette.axisText()).c_str(), hex(palette.gridMajor()).c_str(),
        hex(palette.axisLine()).c_str(), hex(palette.selected()).c_str(), hex(palette.keyEdge()).c_str(),
        hex(palette.keyColor(kTypeA)).c_str(), hex(palette.keyColor(kTypeB)).c_str(),
        hex(palette.marqueeFill()).c_str(), hex(palette.marqueeEdge()).c_str(), hex(palette.menuBody()).c_str(),
        hex(palette.menuHover()).c_str(), hex(palette.menuHoverText()).c_str(), hex(kClear).c_str()
    );

    // The clear colour has to be far from everything the model can paint, or "this
    // pixel moved" would not mean "the panel painted here".
    {
        const QColor painted[] = {
            palette.halo(), palette.axisText(), palette.gridMajor(), palette.axisLine(), palette.selected(),
            palette.keyEdge(), palette.keyColor(kTypeA), palette.keyColor(kTypeB), palette.marqueeFill(),
            palette.marqueeEdge(), palette.menuBody(), palette.menuText(), palette.menuHover(),
            palette.menuHoverText(), palette.menuSeparator(),
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

    // The contrast outline pass's own gate (assertions 11-15): the pass has to
    // build on this context before any of it can be measured.
    OutlineHarness outlineHarness;
    if (!outlineHarness.init()) {
        std::printf("FAIL: the outline harness's framebuffers are incomplete\n");
        return 2;
    }
    if (!gui::outline::init()) {
        std::printf("FAIL: the contrast outline pass refused to build on this context\n");
        return 2;
    }
    std::printf("INFO outline minContrast=%.1f ringPx=%.1f outlineColour=%s\n", gui::outline::kMinContrast,
                gui::outline::kRingRadiusPx, hex(palette.outline()).c_str());

    const gui::GraphPanelModel full = makeFullModel(palette);
    const gui::GraphPanelModel clip = makeClipModel(palette);
    const gui::GraphPanelModel empty = makeEmptyModel();

    // An INVALID focus: every curve is drawn at full strength, so no assertion here
    // depends on the emphasis path (see the file header: it is deliberately not
    // covered).
    gui::PanelFocus focus;
    focus.valid = false;

    // The same model with the zero rule switched OFF: the ONLY difference is zeroY,
    // which is what makes assertion 8 a pair.
    gui::GraphPanelModel fullNoRule = full;
    fullNoRule.zeroY = -1;

    // The same model with the second legend row given NO ROOM: the ONLY difference
    // is that row's swatch, which is what makes assertion 9 a pair.
    gui::GraphPanelModel legendNoRoom = full;
    legendNoRoom.legend[1].swatch = QRect();

    // A model whose frame labels have a box SHORTER than the draw list's font: the
    // configuration the ruler clamp exists for (a drawing-layer font taller than the
    // model's box, which is what a different UI font or an atlas density produces).
    // Everything else is the full model, so the ONLY difference is the box height.
    gui::GraphPanelModel tightRuler = full;
    for (gui::PanelLabel& label : tightRuler.frameLabels) {
        label.rect.setHeight(8);
    }

    // The same model with a LIVE BOX-SELECT MARQUEE: the ONLY difference is the
    // marquee rectangle, which is what makes assertion 18 a pair.
    gui::GraphPanelModel fullMarquee = full;
    fullMarquee.marquee = kMarqueeRect;

    // Assertion 21's guard model: a panel that does NOT start at the framebuffer's left
    // edge and one value label whose text is far wider than its box (the model's measured
    // column cannot produce that - a drawing font wider than the model's can), so the
    // band left of the panel is the only thing to read. Nothing else is published, so
    // that band cannot be filled by any other element of the panel.
    gui::GraphPanelModel overflow;
    overflow.panel = QRect(60, 0, 340, 300);
    overflow.plot = QRect(150, 30, 240, 240);
    overflow.valueColumn = QRect(66, 30, 83, 240);
    overflow.rulerBandTop = overflow.plot.top();
    overflow.valueLabels = {{QRect(66, 60, 20, 14), QStringLiteral("-1.235e+07")}};

    // 0: the control, then one pass per model the assertions read.
    //
    // THE CURVE STROKE, BEFORE AND AFTER, OUT OF ONE PROCESS. The ink model is rendered
    // once with the coverage pass NOT built - which is exactly the panel the user
    // reported on, ImDrawList::AddPolyline - and once with it built. Every stroke
    // assertion below then reads the same model's ink twice, so the two geometries are
    // compared number for number rather than across two runs of the gate, and the crops
    // are the same two frames.
    const gui::GraphPanelModel ink = makeInkModel(palette);
    // The dimmed pair: a focus that names another channel strokes every curve in the model
    // at kUnfocusedAlpha, which is what makes a doubled join visible as ink darker than
    // the stroke's own body (see checkStrokeJoin).
    gui::PanelFocus inkDimmed;
    inkDimmed.valid = true;
    inkDimmed.node = nullptr;
    inkDimmed.channel = ctrl::graph::Channel(kTypeB, 0);
    const Rendered inkFringe = renderPass(ink, palette, focus, "curve_ink_fringe");
    const Rendered inkFringeDimmed = renderPass(ink, palette, inkDimmed, "curve_ink_fringe_dimmed");
    // Assertion 27's stem, drawn by ImGui's own AddLine: the before half of the stem
    // measurement, taken through the SAME method as the after half.
    const Rendered stemFringe = renderPass(makeStemModel(), palette, focus, "hairline_stem_fringe");
    if (!gui::stroke::init()) {
        std::printf("FAIL: the coverage stroke pass refused to build on this context\n");
        return 2;
    }
    const Rendered inkCoverage = renderPass(ink, palette, focus, "curve_ink_coverage");
    const Rendered inkCoverageDimmed = renderPass(ink, palette, inkDimmed, "curve_ink_coverage_dimmed");
    // The same developer aid the panel pass gets, for the two stroke geometries: the ink
    // model's own frame, per curve, through the model's geometry.
    dumpInkCrops(inkFringe, ink, QStringLiteral("ink_before"));
    dumpInkCrops(inkCoverage, ink, QStringLiteral("ink_after"));

    const Rendered control = renderPass(gui::GraphPanelModel(), palette, focus, "control");
    const Rendered rendered = renderPass(full, palette, focus, "full_model");
    const Rendered emptyRendered = renderPass(empty, palette, focus, "empty_state");
    const Rendered clipRendered = renderPass(clip, palette, focus, "clip");
    const Rendered noRuleRendered = renderPass(fullNoRule, palette, focus, "full_model_zeroY_off");
    const Rendered noRoomRendered = renderPass(legendNoRoom, palette, focus, "legend_no_room");
    const Rendered marqueeRendered = renderPass(fullMarquee, palette, focus, "full_model_marquee");
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
    const Rendered tightRulerRendered =
        renderPass(tightRuler, palette, focus, "full_model_tight_ruler", ImVec2(-FLT_MAX, -FLT_MAX), 26.0f);

    // The handle highlight pass: the SAME model with the pointer on the first
    // handle's dot, so the ONLY difference from `rendered` is io.MousePos - which
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
        ImVec2(float(pointerDot.x()), float(pointerDot.y()))
    );

    if (rendered.vtx <= 0) {
        std::printf("FAIL: the full model emitted no vertices at all\n");
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
    if (!checkKeys(rendered, full, palette)) {
        std::printf("FAILED: key squares\n");
        return 1;
    }
    if (!checkLabelHalo(rendered, full, palette)) {
        std::printf("FAILED: label halo\n");
        return 1;
    }
    if (!checkRulerReserve(rendered, full, tightRulerRendered, tightRuler)) {
        std::printf("FAILED: the frame number's reserved band\n");
        return 1;
    }
    if (!checkLegend(rendered, full, palette)) {
        std::printf("FAILED: legend swatches\n");
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
        std::printf("FAILED: zero rule\n");
        return 1;
    }
    if (!checkNoRoomRow(rendered, noRoomRendered, legendNoRoom, rendered.font, rendered.fontSize)) {
        std::printf("FAILED: legend row with no room\n");
        return 1;
    }
    if (!checkHandles(rendered, highlightRendered, full, palette, pointerDot)) {
        std::printf("FAILED: tangent handles\n");
        return 1;
    }
    if (!checkPlayhead(rendered, full, palette)) {
        std::printf("FAILED: the playhead's extent\n");
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
    // 22-26: THE CURVE STROKE, coverage against fringe, out of the same two frames.
    if (!checkStrokeAngle(inkCoverage, inkFringe, palette)) {
        std::printf("FAILED: the stroke's width across the angle\n");
        return 1;
    }
    if (!checkStrokeEdges(inkCoverage, inkFringe, palette)) {
        std::printf("FAILED: the stroke's antialiased edges\n");
        return 1;
    }
    if (!checkStrokeJoin(inkCoverageDimmed, inkFringeDimmed, palette)) {
        std::printf("FAILED: the stroke's join\n");
        return 1;
    }
    if (!checkStrokePosition(inkCoverage, inkFringe, palette)) {
        std::printf("FAILED: the stroke's position\n");
        return 1;
    }
    // The guest rule drives the callback from THIS frame's own command, so the frame is
    // rendered immediately before it: GetDrawData() has to be the frame it reads from.
    renderPass(ink, palette, focus, "curve_ink_guest");
    if (!checkStrokeGuestState()) {
        std::printf("FAILED: the stroke pass does not leave the frame's GL state as it found it\n");
        return 1;
    }
    // 27: the panel's OTHER 1px lines - measured, and left exactly as they were. Each of
    // these three frames carries one line family and nothing else.
    const Rendered playhead200 = renderPass(makePlayheadModel(200), palette, focus, "hairline_playhead_200");
    const Rendered playhead201 = renderPass(makePlayheadModel(201), palette, focus, "hairline_playhead_201");
    const Rendered stem = renderPass(makeStemModel(), palette, focus, "hairline_stem");
    if (!checkHairlinePlacement(playhead200, playhead201, stem, stemFringe, palette)) {
        std::printf("FAILED: the panel's 1px lines\n");
        return 1;
    }
    if (!writeInkCrops(inkFringe, inkCoverage, QStringLiteral("build/shots/graph_ink"))) {
        std::printf("FAILED: the before/after crops\n");
        return 1;
    }

    // THE COST, LAST, so a timing change can never move a measured pixel: the SAME model
    // drawn both ways, one number each, with the method and the structural cost printed
    // beside them (see timeFrames).
    const gui::GraphPanelModel dense = makeDenseModel(palette);
    const FrameCost denseAfter = timeFrames(dense, palette, focus, 40);
    const FrameCost afterCost = timeFrames(ink, palette, focus, 40);
    gui::stroke::shutdown();
    const FrameCost denseBefore = timeFrames(dense, palette, focus, 40);
    const FrameCost beforeCost = timeFrames(ink, palette, focus, 40);
    gui::stroke::init();
    std::printf(
        "MEASURED stroke_cost model=dense_curve(1 curve, %d segments - the shipping sampling) frames=40"
        " after_ms_per_frame=%.3f before_ms_per_frame=%.3f after_vtx=%d before_vtx=%d\n",
        int(dense.curves.at(0).points.size()) - 1, denseAfter.ms, denseBefore.ms, denseAfter.vtx, denseBefore.vtx
    );
    int inkSegments = 0;
    for (const gui::PanelCurve& curve : ink.curves)
        inkSegments += std::max(0, int(curve.points.size()) - 1);
    std::printf(
        "MEASURED stroke_cost model=curve_ink(%d curves, %d segments) frames=40 after_ms_per_frame=%.3f"
        " before_ms_per_frame=%.3f after_vtx=%d before_vtx=%d after_idx=%d before_idx=%d scratch=%dx%d rgba8=%.0fKB"
        " gl_calls_per_curve=3 (1 scratch clear + 1 segment draw + 1 composite) method=whole_frame_including_readback\n",
        int(ink.curves.size()), inkSegments, afterCost.ms, beforeCost.ms, afterCost.vtx, beforeCost.vtx, afterCost.idx, beforeCost.idx, kViewW, kViewH,
        double(kViewW) * double(kViewH) * 4.0 / 1024.0
    );
    if (!checkOutlineRing(outlineHarness)) {
        std::printf("FAILED: contrast outline ring\n");
        return 1;
    }
    if (!checkOutlineNoRing(outlineHarness)) {
        std::printf("FAILED: contrast outline draws where it is not needed\n");
        return 1;
    }
    if (!checkOutlineBackdropIdentical(outlineHarness)) {
        std::printf("FAILED: the composite pass is not a no-op without the panel\n");
        return 1;
    }
    if (!checkOutlineRestoresBindings(outlineHarness)) {
        std::printf("FAILED: the pass does not leave the GL bindings it found\n");
        return 1;
    }

    // The pass's GL objects go while this context is current, exactly as the
    // overlay's shutdown() does it.
    gui::stroke::shutdown();
    gui::outline::shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();

    std::printf(
        "OK: the graph panel's pixel contract holds (27 assertions, 20 panel passes + 5 outline passes)\n"
    );
    return 0;
}
