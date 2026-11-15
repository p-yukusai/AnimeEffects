#ifndef GUI_IMGUIOVERLAY_H
#define GUI_IMGUIOVERLAY_H

#include <QColor>
#include <QElapsedTimer>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QtCore/qnamespace.h>
#include <QtOpenGL/QtOpenGL>

#include "core/TimeKeyType.h"
#include "ctrl/graph/graph_Geometry.h"
#include "gui/GraphPanelModel.h"
#include "gui/Ink.h"

// The shared frame API's return type. Forward-declared rather than pulled in through
// imgui.h: the header is included by targets that do not carry ImGui at all (the graph
// tests stub the gui symbols), and a pointer return needs only the name.
struct ImDrawData;

// The palette the frame is drawn from is part of a host's frame state
// (gui::PanelFrameState below), so the token class itself is named here rather than
// forward-declared.
#include "gui/theme/Graph.h"

class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QOpenGLWidget;
class QTabletEvent;
class QTouchEvent;
class QWheelEvent;

struct ImDrawCmd;
struct ImDrawList;
struct ImFont;
struct ImGuiContext;

namespace core {
class Project;
class TimeLineEvent;
}

namespace gui {

// What the drawing layer reads off the editor: the STICKY FOCUS, and nothing else.
// Passed in rather than read here, so the drawing stays a pure function of its
// inputs (and a headless pixel gate can hand over a synthetic one).
//
// THE SELECTION IS NOT HERE, and that is deliberate: which marker is selected, and
// which marker's key has a selected sibling, are published per key by the MODEL
// (PanelCurve::keySelected / keySiblingSelected), straight from the editor's own
// two queries. A list of selected keys here would be a SECOND source of the same
// fact - one that had to be matched back to a marker by pixel x, and that could
// silently disagree with the flags the model just published.
//
// The one thing the focus still decides is EACH CURVE'S TANGENT HANDLES
// (node/channel/valid): the panel draws them for one channel at a time because they
// cost ~52 of the ~68 vertices a key spends (the measured rule in GraphPanelFrame.cpp),
// and the editor's hit-test focuses a channel on a handle press. The curve EMPHASIS
// is the selection's, and it comes from the model's marker flags.
struct PanelFocus {
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    bool valid = false;
};

// Draws the panel into aDrawList. The RULER is a SECTION AT THE PANEL'S TOP ROW: its
// band, hairline, ticks and frame numbers are drawn first, outside the plot's clip
// (the scaler reserved their rows in plotRect()), so nothing the plot draws can reach
// them. A tick or a number OUTSIDE the model's active range is drawn at the timeline's
// 0.4 recession alpha. The temporary PLAYBACK WINDOW is painted over the ruler
// content, in the timeline's order: the translucent band, the two 2px bracket stems,
// the label pills (or one merged pill) - all in the ruler band, before the plot clip
// and before the playhead. Then, clipped to the plot: the value ticks (in the
// numbers' own token, each
// frame mark at the tier's alpha the model publishes for it - one ink for the ruler,
// the timeline's rule), the zero rule, the curves (the
// channels that own a selected marker, or the SIBLING of one, at full colour and
// every other curve dimmed - see curveStrength), the key markers (a filled dot of
// radius ctrl::graph::kKeyDotRadius about each key's centre, ringed in
// theme::Graph::keyEdge(), in one of the THREE states the model classifies: a
// selected marker brightens its fill and takes the TIMELINE'S 1px ring - the channel's
// raw hue at 0.85, clear of the marker, a marker whose key has a selected sibling keeps the faint raw-hue
// ring, and the marker under the pointer brightens its fill alone - see the marker
// pass), every segment's tangent handles (both kinds are solid dots: a stored pair
// filled with the raised surface, an Auto segment's derived pair filled with the
// channel hue at a smaller radius; both stems and both rings take that hue mixed 0.2
// toward the panel's halo so they read on top of their own curve, and the grabbed one
// is lifted toward axisText from io.MousePos), the box-select marquee, the name CHIP
// of every curve in play, and the empty state. The PLAYHEAD is drawn last, outside
// the plot clip and over the chips, from the pill's row in the band through the
// PANEL's last row (the timeline's playhead crosses its whole body), so no chip can
// cover it. The two SCROLL BARS are drawn after everything else and clipped to the
// PANEL (they are outside the plot by construction, in the margin the scaler reserves
// for them): they are the application's scroll bar - an INVISIBLE 12px trough and a
// 4px, radius-2 pill across the slider's run, in theme::Colors::outline at rest and
// theme::Colors::hover while the pointer is on that bar or the bar is being dragged
// (src/gui/AppStyle.cpp's CC_ScrollBar; the geometry itself is
// ctrl::graph::scrollBar::pill, in src/ctrl/graph/graph_ScrollBarGeometry.h, and both
// painters call it). THE RUN IS ALWAYS THE SLIDER: at zero travel
// BOTH lanes paint a pill across their WHOLE length (the model publishes the handle
// over the union track in every state), exactly as the timeline's AlwaysOn horizontal
// bar does; a full-length run simply cannot be grabbed. The timeline has no vertical
// bar to copy (TimeLineWidget builds a horizontal bar only), so this panel's value
// bar never hides - see panelBarAt for the hover identity the two lanes share. It
// keeps no state - which is what lets the overlay call it per frame and a headless
// pixel gate call it once. It reads no font off the current context (the draw list
// carries its own), but the target pick (gui::panelPickAt, the key or tangent handle
// the pointer lights), the handle highlight and the hovered-curve chip DO read
// ImGui::GetIO().MousePos, so a caller must own a context: the overlay's frame and
// the headless pixel gate both do.
//
// It draws the MODEL and nothing else: every rect it uses comes out of aModel
// (or is derived from the draw list's own font, for the chips' text boxes). No
// background and no grid are drawn - the model publishes none, and the panel
// must leave the artwork showing through (a plot area with nothing in it is
// therefore untouched by this function).
// THE PANEL'S INK, expressed in the backend's vocabulary (gui/Ink.h) - one implementation of
// every SHAPE and INK decision, drawn by whichever backend the host supplies (ImGui today, Qt
// and Canvas Painter next). The pointer is the HOST's: a backend reads no pointer, so the same
// function paints a headless gate with any hover state and a window with the real cursor.
void drawPanel(
    ink::Ink& aInk, const GraphPanelModel& aModel, const theme::Graph& aTheme, const PanelFocus& aFocus,
    const ink::Pointer& aPointer
);

// THE IMGUI BACKEND'S STROKE HOOK (ink::InkImGui::StrokeSink). The per-fragment coverage pass
// belongs to that backend - it exists because ImGui's own antialiased polyline is not
// area-exact - and its GL objects are owned by this file, which is why the hook is declared
// here rather than inside the backend. Answers false when the pass is not ready, and the
// backend then strokes the same polyline with ImGui's own path.
bool panelStrokeSink(ImDrawList& aList, const QVector<QPointF>& aPoints, const QColor& aColour, double aHalfWidth);

// WHICH SCROLL-BAR LANE THE POINTER IS OVER. One answer for the three things that
// must agree about it: the drawing layer's hot test (a lane's pill takes
// theme::Colors::hover while the pointer is on it), the host's hover-repaint rule
// (gui::GraphPanelWidget::updateHoverRepaint repaints when this answer CHANGES, so
// entering a lane, leaving it, or crossing from one lane to the other is painted),
// and a pixel gate that asserts the hover ink. Reading the model's published tracks
// - not a second copy of the lane geometry - is what keeps them on one rect.
//
// The order is the model's own: the tracks are disjoint (the corner belongs to the
// horizontal bar), so at most one can contain the point; Horizontal is asked first
// only because its rect is the wider one.
enum class PanelBar { None, Horizontal, Vertical };

inline PanelBar panelBarAt(const GraphPanelModel& aModel, const QPoint& aPointer) {
    if (!aModel.hTrack.isEmpty() && aModel.hTrack.contains(aPointer))
        return PanelBar::Horizontal;
    if (!aModel.vTrack.isEmpty() && aModel.vTrack.contains(aPointer))
        return PanelBar::Vertical;
    return PanelBar::None;
}

// Whether drawPanel draws the LABEL HALO: the same glyphs 16 more times, at the
// eight compass offsets of radius 1 and radius 2, in theme::Graph::halo().
// SHIPPED OFF. The halo is unconditional - it prints an opaque panel-toned smear
// around every label, including the labels sitting on artwork that never needed
// one - and it costs 17 glyph passes for a single line of text. The contrast
// outline pass below rings the same ink, glyphs included, but only where that ink
// would otherwise have been lost against what is behind it, and in the outline
// colour rather than a panel tone. The halo's code is kept and this flag is the
// only thing that switches it off, so the two can still be compared.
constexpr bool kDrawTextHalo = false;
// The halo's geometry and ink: 2 rings of 8 offsets at 0.85 of the theme's halo tone. They
// live here beside the switch because the TEXT BACKEND (gui/InkImGui.cpp) draws the halo,
// while the switch is the panel's decision.
constexpr int kHaloRadius = 2;
constexpr float kHaloAlpha = 0.85f;

// The gutter a VALUE label keeps from the right edge of its box, when the box has
// that much room to spare (see drawLabel's right-alignment): the top value label and
// the plot's first frame number otherwise land 7px apart on the same rows and read as
// the single string "10" - measured in the design-review capture (the value label '1'
// at x 208..215 and the frame label '0' at x 223..231 of a 1440x900 shot).
//
// WHY 8. The timeline answers this exact defect with its own margin:
// src/gui/TimeRuler.cpp's kNumberMargin = 8 ("Number labels never come closer than
// this gap: without a margin, adjacent numbers ... read as one"). The panel takes the
// application's own number for the same problem, and 8px on top of the model's own
// ctrl::graph::Scaler::kAxisLabelPad (4px) and the frame number's inset leaves the two
// numbers ~15px apart - more than two 7px digits of clear gutter.
//
// IT IS A MAXIMUM, NOT A SHIFT: the pull is limited to the box's spare width, so a
// label that fills its box keeps the position the model's measured column gave it and
// the gutter can never push a number out of the column.
//
// Declared here rather than in the .cpp because tests/graph_panel_pixels.cpp asserts
// the drawn gutter against THIS number, so the bar and the drawing cannot drift (the
// same reason kDrawTextHalo lives here).
constexpr int kValueLabelGap = 8;

// THE ROW THE RULER'S NUMBERS INK FROM: how many rows below its box's TOP row the
// first row of a frame number (or of a playhead/window-pill number) carries ink - on
// BOTH views. The box top is `rulerTop + kNumberTop`, and at this face and size the
// digits' ink starts three rows below it (Qt's ascent 11.6px minus the digits' cap
// top 3.3px -> row 3).
//
// WHO NEEDS THE NUMBER. The Qt and Canvas backends (gui::InkQPainter, gui::InkCanvas -
// the SHIPPED path) get that row from the engine itself: `VAlign::Top` puts the LINE
// BOX'S top on the box's top row, exactly as the timeline's rulers draw their numbers,
// and the ink follows three rows down. MEASURED 2026-10-23 by probe_graph_parity: with
// that rule the panel's numbers ink rows 243..251 against the timeline's 243..252, i.e.
// the same rows - the ink-row arithmetic this constant expresses read 242..250, one row
// high, and that was the last parity failure the probe had.
//
// gui::InkImGui still needs it: ImGui places text by its own line box, whose ink sits
// one row deeper, so drawLabel subtracts the font's own top bearing and pins the run's
// first inked row to `box.top() + kRulerNumberInkTop` (whatever size the layer draws
// at - the pixel gate renders it at 10px and reads the same row). That backend is the
// GATE's reference now, not the application's.
//
// Declared here rather than in the .cpp because tests/graph_panel_pixels.cpp asserts
// the drawn row against THIS number, so the placement and the assertion cannot drift
// (the same reason kDrawTextHalo and kValueLabelGap live here).
constexpr int kRulerNumberInkTop = 3;

// THE SELECTION RING'S OFFSET AND INK ARE NOT TOKENS OF THIS LAYER: the ring is the
// TIMELINE's own (ctrl::time::kSelectRingOffset / kSelectRingAlpha in
// ctrl/time/time_Renderer.h - the channel's raw hue at 0.85, 1px, centred that many
// pixels past the marker's radius). The marker pass reads those two cells directly, so
// the two views cannot drift; tests/graph_panel_pixels.cpp reads them too, to find the
// ring's pixels and to name its ink.

// The contrast outline and composite: ONE full-screen pass that rings panel ink
// whose own colour is too close to the artwork underneath it, and then composites
// the panel over that artwork.
//
// WHY IT IS A FREE UNIT AND NOT A MEMBER OF THE HOST: the same reason drawPanel
// is one - a headless pixel gate has to be able to drive it with synthetic
// textures and no overlay instance, a real context and the real framebuffer.
//
// WHAT IT READS. Two textures of the same pixel size:
//   * the overlay: the panel rendered into its OWN framebuffer, where rgb is the
//     panel's ink and alpha is its coverage. That render is the ImGui backend's
//     own source-over blend into a framebuffer cleared to (0,0,0,0), so the rgb
//     the pass reads is PREMULTIPLIED by the coverage (the backend uses
//     glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, ...),
//     which leaves dst.rgb = src.rgb * src.a). The pass divides the coverage back
//     out before it does anything else, so its own formulas are written in the
//     panel's colours. Without that step a partially covered pixel would composite
//     at alpha SQUARED - the artefact the "do not gate at alpha >= 1" rule exists
//     to avoid would be replaced by a different one on every AA fringe.
//   * the backdrop: the pixels the panel would sit on if it were not there. It is
//     a GPU copy of the widget's framebuffer (copyBackdrop, a glBlitFramebuffer
//     resolve, never a readback), so the pass can ask what the artwork under a
//     label actually is.
//
// THE RING IS BUILT FROM TAPS, NOT FROM THE PIXEL'S OWN COVERAGE. The four taps
// one pixel away from the ink carry the ring: a 40%-covered stroke gets a 40%
// ring, two adjacent strokes fill in without double-darkening, and nothing is
// drawn where no ink is nearby. The contrast test compares each TAP's own colour
// against the backdrop at THAT pixel, so a glyph straddling a light/dark boundary
// is ringed only on the side that needs it.
namespace outline {

// The ratio below which the ring is drawn. The shader rings ink whose contrast
// against the backdrop is BELOW this number, so a HIGHER constant rings MORE: it
// is the "the ink would otherwise have been lost" line, not a target for the ink.
//
// WHY 2.0 AND NOT 3.0 (the value this shipped with). 3.0 is WCAG's minimum for
// large text and UI components, and read as a ring TRIGGER it is far too eager:
// the panel's ink is theme::Colors::text, and in linear light white ink reaches
// 3:1 against any backdrop darker than grey 149/255 - i.e. the ring was drawn over
// almost every mid-tone in the artwork, which is the "the ring shows up
// everywhere" report. 2.0 draws it only where the ink is genuinely near-invisible:
// white ink rings over a backdrop brighter than grey 183/255, and every darker
// backdrop is left alone. The relation is measured in the gate
// (tests/graph_panel_pixels.cpp, the outline block): a grey-130 stroke over the
// bright 200 patch is 2.30:1, untouched at 2.0 and ringed at 3.0 - the pair that
// pins this number rather than merely accepting it.
//
// The ratio is only that number in LINEAR light, so luma() linearises the sRGB
// channels first (see the fragment shader).
constexpr float kMinContrast = 2.0f;

// The shipped ring: the four taps one pixel away. The alternative measured here is
// 2, which adds a second ring at +/-2px and takes maxA over all eight taps - the
// extension for a ring that reads too thin next to 13px labels. It is a parameter
// rather than a compile-time constant so the two can be compared without a second
// build; the pass pays for the extra four taps only when it is set.
constexpr float kRingRadiusPx = 1.0f;

struct Params {
    float minContrast = kMinContrast;
    float ringRadiusPx = kRingRadiusPx;
    QColor colour = QColor(0, 0, 0); // theme::Graph::outline() in the shipping path
};

// Builds the pass's program and its full-screen quad on the CURRENT GL context.
// Idempotent (safe to call per frame) and false when the context cannot compile
// it - the caller must then draw without it rather than draw nothing.
bool init();
// Releases the program and the quad, with the context that made them current.
void shutdown();
// Drops the pass WITHOUT calling GL, for a teardown on a context that does not own its
// names (a reparent destroyed the context they were made on). The wrapper is deliberately
// LEAKED: its destructor deletes the program by name, and on a context that never owned
// it that is a wrong operation (GL_INVALID_VALUE, measured), not a no-op. See
// destroyPanelObjects in GraphPanelFrame.cpp.
void abandon();

// Copies the framebuffer aSourceFbo (aPixelSize pixels) into aDestinationFbo with
// glBlitFramebuffer: a GPU copy, never a readback. The widget's own framebuffer is
// 4x multisampled (MainWindow's QSurfaceFormat), and a blit from it RESOLVES it -
// which is why the filter is GL_NEAREST, the only filter a multisample resolve
// accepts. Both framebuffers must have the same internal format (GL_RGBA8 here)
// and the same size.
void copyBackdrop(GLuint aSourceFbo, GLuint aDestinationFbo, const QSize& aPixelSize);

// Runs the pass into the CURRENTLY BOUND framebuffer, which holds aPixelSize
// pixels. The overlay (unit 0) and the backdrop (unit 1) are sampled, and the pass
// sets every piece of GL state it depends on: this is the last thing in the frame,
// and the ImGui backend leaves blending, the scissor box and the viewport as its
// own last draw left them.
void composite(GLuint aOverlay, GLuint aBackdrop, const QSize& aPixelSize, const Params& aParams);

} // namespace outline

// The CURVE INK: the channel polylines, stroked by a coverage pass of our own
// instead of by the draw list's own ImDrawList::AddPolyline - and the tangent HANDLE STEMS,
// which are 1px lines with fractional ends and the one 1px family in the panel whose ink can
// sit off the line it means.
//
// WHY NOT AddPolyline. ImGui antialiases a polyline by building the fringe into the
// GEOMETRY: each segment is a quad whose edge vertices carry a partial alpha, so how
// thick the stroke looks is a property of its vertices - and a horizontal run and a
// steep diagonal of the same nominal width then disagree. This pass measures the
// coverage PER FRAGMENT, from the distance to the segment's own centreline, so the
// width is a property of the SHAPE. It is the technique of Chan & Durand, "Rendering
// Antialiased Lines on the GPU" (2005) and the treatment in Rougier, "Shader-Based
// Antialiased, Dashed, Stroked Polylines", JCGT 2(2) 2013 (whose body distance is the
// absolute perpendicular coordinate and whose filter radius is a parameter) - the same
// family Vello/piet-gpu draw strokes with.
//
// WHY THE STEMS ARE HERE TOO. A handle stem is one pixel wide and its ends are fractional
// (PanelHandle::base/dot are QPointF), and ImGui's AddLine shifts its geometry by (+0.5,+0.5)
// (the 1px rasterization convention in imgui_draw.cpp) - which is a pure translation, but the
// coverage that matters is measured PERPENDICULAR to the line, so on a stem at a diagonal the
// ink lands up to 0.71px off the line the model asked for (the (0.5,0.5) shift's component
// along the stem's own normal is 0.699 for the gate's fixture). MEASURED on that fixture, at
// three stations along it, through one method for both strokes: +0.68, +0.75 and +0.76px for
// AddLine, against -0.01, -0.00 and +0.01px for this pass. The pass measures the
// distance to the segment that was asked for, so the offset goes away; the stem is already
// antialiased today (drawPanel does NOT turn the draw list's line AA off for it), so its
// crispness is unchanged - only its position.
//
// The integer-positioned chrome (the frame and value ticks, the zero rule and the playhead) does
// NOT come through here, and that is a measured decision rather than an omission: those three
// are drawn with the line AA explicitly OFF (the timeline's hairline rule), every one of them is
// positioned by an INTEGER column, and at an integer column the hard-edged line is exact - one
// column at full coverage, on the column the model named. A 1px stroke through this pass on a
// pixel boundary puts half its ink in each of two columns, i.e. it would trade one crisp column
// for two grey ones. tests/graph_panel_pixels.cpp pins both halves of that (its assertion 27),
// so the decision fails loudly if someone makes it by accident.
//
// HOW IT DRAWS. One quad per segment, expanded on every side to
// halfWidth + filter, so a segment's capsule already has its round join and cap and
// no join geometry is needed; each vertex carries the segment's own two endpoints.
// The fragment computes the distance to the SEGMENT (never to its infinite line) and
// turns it into a coverage with a one-device-pixel box filter, so the 50% crossing
// sits exactly on the nominal edge. Below one device pixel the stroke keeps the 1px
// footprint with its alpha scaled down rather than vanishing.
//
// NO DOUBLE-DARKENING AT THE JOINS. Neighbouring segments' quads overlap by the
// filter's own width, and compositing them one after another would blend the overlap
// twice - which is worse than it sounds, because a dimmed curve is drawn at 45%
// alpha and even a FULLY covered overlap composites darker than the stroke's own body.
// So the coverage is accumulated with glBlendEquation(GL_MAX) into a scratch texture
// (the union of the capsules, which IS the round-joined stroke) and composited once
// into the frame with the ImGui backend's own source-over function, in the
// premultiplied convention the contrast outline pass documents.
//
// WHERE IT SITS IN THE FRAME. The curves belong between the ticks and the key
// markers, and the ImGui backend renders a whole draw list in one call, so the pass is
// inserted INTO the draw list: drawPanel puts one ImDrawList::AddCallback per curve,
// carrying that curve's polyline, colour and strength in the callback's own COPIED
// data, and the callback draws into whichever framebuffer is bound (the overlay's
// coverage framebuffer in the application, the harness's own in the pixel gate) using
// the backend's own ortho matrix - so a curve lands on exactly the pixels AddPolyline
// would have inked.
//
// IT IS A GUEST IN THE FRAME and puts back every binding it changes: the ImGui backend
// keeps its program, its vertex array and its array buffer bound across commands and
// uploads the next draw list's vertices with them.
//
// WITHOUT init() - a context that refuses the programs, or a caller that never built
// the pass - drawPanel keeps the old AddPolyline path: a panel drawn with the fringe
// stroke beats a panel with no curves at all. The pixel gate drives both, which is
// what makes the two stroke geometries comparable in one run.
namespace stroke {

// The stroke's nominal width in LOGICAL pixels - gui::kCurveWidth, the number both
// paths stroke with. Declared here because tests/graph_panel_pixels.cpp asserts the
// measured width against it, the same reason gui::kValueLabelGap lives in the header.
constexpr float kWidthPx = 2.0f;

// The coverage filter's width in DEVICE pixels: the box filter the fragment turns the
// distance to the centreline into a coverage with. Rougier's r, and 1 is both the
// paper's usual value and this shipment, so the ramp spans the pixel either side of the
// nominal edge. Read only by the pass; named here so the gate can say what it measured
// against.
constexpr float kFilterPx = 1.0f;

// Builds the pass's programs, its vertex array and its coverage scratch on the CURRENT
// GL context. Idempotent (safe to call per frame) and false when the context cannot
// compile the programs - drawPanel then strokes with ImDrawList::AddPolyline.
bool init();
// Releases the programs, the vertex array and the scratch, with the context that made
// them current.
void shutdown();
// Drops the pass WITHOUT calling GL, for a teardown on a context that does not own its
// names (see outline::abandon and destroyPanelObjects in GraphPanelFrame.cpp).
void abandon();
// Whether init() has built the pass, i.e. whether drawPanel will insert it. The gate
// uses this to render the SAME model through both stroke geometries in one run.
bool isReady();

// The draw-list callback drawPanel inserts where a curve's ink belongs (an
// ImDrawCallback, one per curve). Public for the same reason drawPanel and
// outline::composite are: the pixel gate has to be able to drive it directly, which is
// how it is checked for leaving the frame's GL state exactly as it found it. It draws
// only what the ImDrawCmd it is handed carries - the curve's polyline, colour and
// strength, in the callback data ImGui copied for it - and it reads the projection
// from the frame's own ImDrawData, so it must be called while that frame is current.
// It returns without drawing when there is no frame to project against.
void drawListCallback(const ImDrawList* aDrawList, const ImDrawCmd* aCommand);

} // namespace stroke

// ---------------------------------------------------------------------------
// THE PANEL'S FRAME DRIVER: what the panel's ONE host runs, and what the headless
// pixel gate drives directly.
//
// The panel lives in the TIMELINE DOCK (the Graph tab's page, gui::GraphPanelWidget,
// design section 5) and it used to have a second host - the viewport overlay that drew
// it over the artwork, with gui::MainDisplayWidget forwarding its input. That host is
// GONE, and this file is where the machinery it needed still lives, because the panel
// is not a widget and is not an ImGui window either: it is drawn into the host's GL
// framebuffer from a model, and every piece of that is a free unit a test can drive.
//
// WHAT ONE FRAME IS (gui::runPanelFrame, below): the io setup from the host widget's
// own size and device pixel ratio, the GL3 backend's NewFrame, ImGui::NewFrame, the
// curve ink's pass, the model (gui::buildGraphPanelModel) and its ink
// (gui::drawPanel), and ImGui::Render(). The host then renders the returned draw data
// into its own framebuffer - the dock's page does it with
// ImGui_ImplOpenGL3_RenderDrawData and nothing else, because its floor is opaque and
// there is no artwork to ring (see gui::outline for the pass the overlay host needed
// and the pixel gate still drives).
//
// THE PANEL'S GL OBJECTS BELONG TO A CONTEXT, AND THAT IS CHECKED. Sharing is not
// enough for them: Qt's sharable context group gives buffers, textures, shaders and
// programs, but NOT vertex-array objects - a VAO name created in one context is
// GL_INVALID_OPERATION in another on a core profile (measured: the second host bound
// the stroke pass's VAO in its own context and GL_CHECK_ERROR() aborted on 1282).
// acquirePanelContext() therefore records the QOpenGLContext the ImGui context, the
// backend and both GL passes were created against, and runPanelFrame() re-creates them
// whenever a frame arrives with a different one. That is not only the second-host guard
// it started as: Qt DESTROYS and re-creates a QOpenGLWidget's context when the widget is
// reparented into another window, which is exactly what undocking the Animation dock
// does - so a drag of the dock runs this path, and the objects are built again on the new
// context. The old names are NOT deleted there (glDeleteProgram answers 1281 for a name
// the current context never owned - measured): they died with their context, so the
// wrappers are abandoned rather than destroyed, and nothing is left pending for the next
// paint's resetRenderState. The panel also owns its own vertex array object, created and
// bound on the new context before the first GL call of the paint
// (bindPanelVertexArray): a core profile has no default array object, and the one
// MainDisplayWidget keeps bound is not valid here once this widget is in another window.
//
// WHAT THE HOST STILL OWNS ITSELF: the widget it draws into (io.DisplaySize and
// io.DisplayFramebufferScale come from ITS size and ratio, which is what puts the frame
// in its own viewport at its own ratio; the backend sets the GL viewport and the scissor
// boxes from draw_data->DisplaySize * FramebufferScale, i.e. in device pixels), its own
// input bridge (gui::ImGuiInputBridge), its own frame state below for its own hit-tests,
// and the framebuffer it renders the frame into.

// The state of ONE host, between its frames. Each host owns one and hands it to
// runPanelFrame() every frame.
struct PanelFrameState {
    // The frame the pixels were drawn from and the editor it was built from (null
    // while the panel is hidden). A host's OWN interactions - the channel button, a
    // double-click on the plot, the accessible items - are tested against THIS
    // geometry, so the clickable control is exactly the drawn control.
    GraphPanelModel model;
    ctrl::GraphEditor* editor = nullptr;
    // The palette that frame was built and drawn with: theme::Graph::from of the LIVE
    // theme::Colors::current(), derived once per frame here (the app changes palette at
    // runtime, so there is nothing to cache). It travels in the state because a host's
    // work AFTER the frame still needs it (gui::outline's composite takes its
    // outline() colour) and a second derivation would be the same work twice.
    theme::Graph palette;
    // How many LOGICAL pixels of this host's top another widget covers. The dock's page
    // leaves it at 0 - nothing floats over the dock, the tab names the page and the
    // header belongs to the dock - so the ruler band occupies the panel's top row. (The
    // field and the clamp stay: the inset is the model's,
    // GraphPanelModel's THE OCCLUDED TOP, and a host that floats something over the
    // panel sets it rather than the model inferring one. The band is never pushed into
    // the plot; an occluder that reaches its slot hides the strip for that frame.)
    int topInset = 0;
    // io.DeltaTime, from the real interval between THIS host's frames.
    QElapsedTimer frameTimer;
    bool frameTimerStarted = false;
};

// The panel's Dear ImGui context, GL3 backend and font atlas, brought up on aHost's own
// GL context - together with the two GL passes that are context objects (gui::stroke's
// programs and scratch, gui::outline's program and quad). The host calls this from
// initializeGL, with that context current, and the LAST release tears them down again
// (also with a context current).
//
// THE OBJECTS REMEMBER THEIR CONTEXT and are re-created if a frame ever arrives with a
// different one (see the note at the top of this section): VAOs are not shared between
// contexts, so a second host attaching to this one's objects would be a GL error rather
// than a sharing success. The atlas is built from aHost's font - the face and the size
// Qt rasterizes the rest of the application with, and the dock's page sets no font of
// its own.
bool acquirePanelContext(QOpenGLWidget& aHost);
void releasePanelContext(QOpenGLWidget& aHost);
// Whether the context is up, i.e. whether a frame can be run at all.
bool panelContextReady();

// BINDS THE PANEL'S OWN VERTEX ARRAY OBJECT on aHost's context, building the panel's
// objects on that context first if they belong to another one (a reparent gives the
// widget a new context; see the section note above). Call at the TOP of a paint, before
// the first GL call of the frame: a core profile has no default array object, and the
// one the application keeps bound is MainDisplayWidget's - a name that is not valid in
// this widget's context once it is in another window. False when no context is up (the
// caller may still paint its floor; nothing it does needs an array object).
bool bindPanelVertexArray(QOpenGLWidget& aHost);

// ONE FRAME of the panel, for aHost. This is the whole per-frame body: the io setup,
// the backend's NewFrame, ImGui::NewFrame (in that order - the backend's own NewFrame
// is the CALLER's duty in 1.92 and is what creates its device objects), the curve
// ink's pass, the model, drawPanel and ImGui::Render().
//
// aHostStyle is the host's own kind (gui::GraphPanelHost): it is republished in the
// model as the classification of which floor the panel is on (it places nothing - the
// panel's one control, the channels button, belongs to the dock's header). aEditor is
// the panel's editor (ctrl::Driver::graphEditor()) and aVisible whether the panel is up
// at all: with false the frame draws nothing but still drains ImGuiIO's queue, and
// aState comes back with an empty model and a null editor.
//
// It also guards the CONTEXT: a frame whose host draws with a context other than the one
// the panel's GL objects were created against re-creates them on that context first (see
// the note at the top of this section). False from that re-creation - a context that
// cannot host the backend - and the function returns null.
//
// aPointer is the HOST's pointer, in the host's logical pixels - the same space as every
// rect in the model and in the draw list. The panel paints when its host asks for a frame
// (a docked page gets one when something it draws changed, not at the input event rate),
// and Dear ImGui trickles its input queue across NewFrame() calls, so io.MousePos can lag
// the pointer that the host itself is tracking - measured: in the docked host a readout
// that followed the pointer was drawn a whole drag behind it, at the press position (that
// readout is gone, but the hovered chip and the marker the pointer lights still read
// io.MousePos, so the pointer still has to come from the host that sees every event).
// The pointer is therefore passed in from the host that sees every event, and the frame
// makes it the io.MousePos the drawing layer reads (the panel has no ImGui window, so
// nothing else in the frame reads it). A host without a pointer passes a point outside
// the panel and nothing hovers.
//
// The frame's vertices are NOT drawn here. The returned draw data is rendered into the
// host's own framebuffer - the dock's page with ImGui_ImplOpenGL3_RenderDrawData and
// nothing else. Null when no context is up, i.e. when the host must draw nothing.
// THE COMPOSITION OF ONE FRAME, independent of any rasteriser: the model, the focus, the
// palette and the state a host publishes for its own hit tests and the accessibility layer.
// runPanelFrame() is the ImGui host's spelling of it (its draw list, its pointer, its ink);
// a QCanvasPainter or QPainter host composes the SAME value and paints it with its own
// backend, so two hosts cannot disagree about what the panel says.
struct PanelFrame {
    GraphPanelModel model;
    PanelFocus focus;
    theme::Graph palette;
    bool drawn = false;
};

PanelFrame composePanelFrame(
    ctrl::GraphEditor* aEditor, bool aVisible, const QSize& aViewport, PanelFrameState& aState,
    const QFont& aMeasuredFont, GraphPanelHost aHostStyle
);

ImDrawData* runPanelFrame(
    QOpenGLWidget& aHost, GraphPanelHost aHostStyle, ctrl::GraphEditor* aEditor, bool aVisible,
    PanelFrameState& aState, const QPoint& aPointer
);

// THE PANEL'S ACCESSIBILITY PUBLICATION.
//
// gui::a11y keeps ONE installation for the process (one factory, one widget, one
// published model), so the panel can be published exactly once, from the widget that
// owns it. That widget is the host that draws the panel (gui::GraphPanelWidget), which
// installed the factory for itself in its CONSTRUCTOR (installFactoryOnly - see
// gui/ImGuiA11y.h, "WHERE TO CALL IT": a widget shown or parented while accessibility is
// active caches its ordinary interface first, and no factory installed later is reached
// for it). This call is the second half of that pairing: a repeat install for the SAME
// widget, which keeps the interface and the id and only takes over the action callback.
//
// The panel is published while the host is ON SCREEN and the EMPTY model while it is
// not: the tree says "no panel" rather than showing the last frame of a hidden tab. All
// of that is one rule here because there is exactly ONE host - if a second one ever
// returns, this is the function whose ownership rule has to be re-thought, together with
// the GL guard (the note at the top of this section is what it cost last time).
//
// aOnAction(aUser, aId) is the host's own handler for the ids below, called
// synchronously from the accessibility call that activated the item.
// aHost is the WIDGET whose accessibility interface is published - its rasteriser is irrelevant
// (QWidget& rather than QOpenGLWidget&, so the Canvas Painter host can be published too).
void publishPanelAccessibility(
    QWidget& aHost, PanelFrameState& aState, void (*aOnAction)(void* aUser, int aId), void* aUser
);

// The panel's accessible item ids. No item is a row index (the channel list and the key
// menu are the application's own QMenus, not in the tree), so every id is a fixed
// negative; the host dispatches on them.
constexpr int kPanelSummaryItemId = -1;
constexpr int kPanelFitItemId = -2;
constexpr int kPanelKeyMenuItemId = -3;

// The drawn marker of the one key aTargets names, in the model's own pixels: the
// (node, channel type, frame) triple - the timeline's own name for a key - looked up
// in PanelCurve::keyFrames/keyDots, which are published beside each other. False when
// the selection names no single key the model carries (a key outside the plot's
// window, or on a hidden channel). It is the rectangle of the key menu's accessible
// action and the position the picker opens at.
bool panelSelectedKeyDot(const GraphPanelModel& aModel, const core::TimeLineEvent& aTargets, QPoint& aDot);

// The key under aPos, by the EDITOR's own rule: the nearest key whose centre is within
// ctrl::graph::kKeyHitRadius of it - the same distance, and the same `<=`,
// ctrl::GraphEditor::hitTest grabs a key with - reported as the triple (node, channel,
// frame) the timeline names a key by. The centre is PanelCurve::keyDots, not the key's
// HIT rect and not the drawn marker: the rect is that rule's square form (too small at
// the diagonals), and the marker is the ink, deliberately smaller than the target.
//
// False when no key is that close, which is a right-click's whole answer: the panel's
// right button has one action (the easing picker on the key under the pointer) and opens
// nothing when there is no key. It is the one right-click hit test. The HOVER is not it:
// the pointer lights either a key or a tangent handle, and which one it names is the
// shared pick gui::panelPickAt makes for the drawing layer and the host alike (see
// PanelPick below), because a right-click may name a key where a plain press at the
// same point selects a relatively nearer handle.
bool panelKeyAt(
    const GraphPanelModel& aModel, const QPoint& aPos, core::ObjectNode*& aNode,
    ctrl::graph::Channel& aChannel, core::Frame& aFrame
);

// WHAT THE POINTER'S POSITION NAMES ON THE PANEL - one answer, one rule, for the
// hover ink (drawPanel) and the hover-repaint scheduling
// (gui::GraphPanelWidget::updateHoverRepaint).
//
// This is the panel-level half of ctrl::graph::pickTarget: it finds the NEAREST key
// (over every curve the model publishes, PanelCurve::keyDots) and the NEAREST
// published handle (PanelCurve::handles - and the model publishes exactly the handles
// the drawing layer inks), then lets pickTarget() decide which class the point names.
// The press decides the same question from the same geometry through the same
// function (ctrl::GraphEditor::hitTest), so the marker or handle the pointer lights is
// exactly the target a press at that point selects. That is the whole point: the two
// used to be independent scans - the drawing layer found the nearest handle within its
// own ring and lit a key whenever any key was inside ITS ring, without comparing the
// two - so the ink could light a handle a press would never take.
//
// The tie-break inside each class is the one this file always used: the LAST
// candidate at the same distance wins (`<=`).
//
// A HANDLE THE DRAWING LAYER DOES NOT INK is not in the model, so it is not a hover
// candidate here: with a focus set, only that channel's row publishes handles (the
// vertex-budget rule drawPanel states). A press there still GRABS the handle - the
// editor's hit-test scans the document, not the picture - and a handle press focuses
// that channel, so the handle is published, drawn and highlighted from the next frame
// on. Keys are published for every curve, so they are always candidates.
struct PanelPick {
    ctrl::graph::PickKind kind = ctrl::graph::PickKind::None;
    // The key, as the timeline's own triple - set when kind == Key. A curve whose
    // node is null is not hoverable (there is no layer to name), exactly as
    // gui::panelKeyAt answers it.
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    core::Frame frame;
    // The handle, as indices into aModel.curves and that curve's `handles` - set
    // when kind == Handle, -1 otherwise.
    int curve = -1;
    int handle = -1;
};

PanelPick panelPickAt(const GraphPanelModel& aModel, const QPoint& aPos);

// WHAT IS LEFT IN THIS FILE, and why it is a file at all.
//
// The class that used to live here was the panel's VIEWPORT OVERLAY host: it drew the
// panel over the artwork, forwarded the viewport widget's Qt events into ImGui, owned
// the contrast-outline composite and published the panel's accessibility model. That host
// is GONE (design section 5 puts the panel in the timeline dock's Graph tab, and one host
// means one GL context, which is also what the per-context guard above exists for). What
// remains here is everything the ONE host and the headless pixel gate still share:
//
//   * the panel's INK: gui::drawPanel and its helpers (labels, curves, markers, chips
//     and the marquee), which draw a model into a draw list and read no font off the
//     current context;
//   * the model's PIXEL-LEVEL palette decisions: gui::kDrawTextHalo and
//     gui::kValueLabelGap, here rather than in the .cpp because the gate asserts against
//     them;
//   * the CURVE STROKE pass (gui::stroke) and the CONTRAST OUTLINE pass (gui::outline):
//     the stroke is what the panel strokes its curves with; the outline is the pass the
//     overlay host used to ring ink that would have been lost against arbitrary artwork,
//     and it is kept as a free unit for the reason it always was - the gate drives it
//     with synthetic textures and no widget (tests/graph_panel_pixels.cpp), so its
//     arithmetic is still pinned even though the floor the panel sits on is now opaque;
//   * the FRAME DRIVER above: the panel's ImGui context, backend and atlas, one frame's
//     body, and the accessibility publication;
//   * the two HIT TESTS the host and the a11y items share: gui::panelKeyAt (the
//     right-click's key, and the marker the pointer lights up) and
//     gui::panelSelectedKeyDot (the key the selection names).
//
// Every user-visible string in the .cpp goes through QCoreApplication::translate with the
// "gui::ImGuiOverlay" context: that name is a CATALOG key the shipped .ts files already
// carry, so it is frozen (see the note beside kTranslationContext in the .cpp) - the file
// and the class it was named after are not what it describes.

} // namespace gui

#endif // GUI_IMGUIOVERLAY_H
