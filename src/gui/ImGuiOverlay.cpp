#include "gui/ImGuiOverlay.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFont>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLWidget>
#include <QPointer>
#include <QTabletEvent>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QTouchEvent>
#include <QVarLengthArray>
#include <QWheelEvent>

#include "imgui.h"
// The draw list's own font lives in ImDrawListSharedData, which imgui.h only
// forward-declares. Reaching it here is deliberate: it is what lets drawPanel
// measure and draw text from the DRAW LIST alone, with no ImGui context and no
// frame - the property a headless pixel gate needs. (The vendored ImGui is
// version-pinned, and the Qt font loader already depends on this header.)
#include "imgui_internal.h"
#include "backends/imgui_impl_opengl3.h"

#include "core/Frame.h"
#include "core/ObjectNode.h"
#include "core/TimeLineEvent.h"
#include "ctrl/GraphEditor.h"
#include "ctrl/graph/graph_Geometry.h"
#include "ctrl/graph/graph_Scaler.h"
#include "gl/EasyShaderProgram.h"
#include "gl/Framebuffer.h"
#include "gl/Global.h"
#include "gl/Texture.h"
#include "gl/Vector2.h"
#include "gl/VertexArrayObject.h"
#include "gui/ImGuiA11y.h"
#include "gui/ImGuiInputBridge.h"
#include "gui/ImGuiQtFontLoader.h"
// The overlay's key menu is the WIDGET's (MainDisplayWidget::showGraphKeyMenu), so
// this file names the class it lives in. One direction only - MainDisplayWidget.cpp
// includes this header, and its own header includes neither this one nor ImGui.
#include "gui/MainDisplayWidget.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Graph.h"

namespace gui {
namespace {

// The GLSL version string handed to the GL3 backend. The application's context is
// a 4.0 CORE profile (gl::Global::kVersion), and the backend's own table maps GL
// 4.0 to "#version 400 core"; a core profile rejects the pre-150 strings the
// backend would otherwise default to on desktop.
const char* kGlslVersion = "#version 400 core";

// The curve stroke width, in LOGICAL pixels - the one number the shipping coverage
// pass (gui::stroke) and the AddPolyline fallback below both stroke with.
//
// WHY 2.0. The curve is read at its full 2px on the plot, and the 1.5px it had before
// that cost 1.71px of ink per column on the pixel gate's own slope-0.5 segment against
// 2.21 (AE_PANEL_CROPS, measured across 41 columns, both constant to 3 decimals) - so
// the weight class is 2.0 on a real measurement, not on taste.
//
// WHAT IT IS FOR THE FALLBACK, where a whole number still matters: ImGui antialiases a
// polyline by one of two paths and the WIDTH decides which (imgui_draw.cpp,
// ImDrawList::AddPolyline). A width with a zero fractional part uses the baked LINE
// TEXTURE - one continuous quad strip whose edges the GPU's bilinear filter turns into
// a ramp, 2 vertices and 6 indices per point - while any fractional width falls back to
// the GEOMETRY path, 4 vertices and 18 indices per point whose joins overlap (each
// segment's fringe is blended over its neighbour's). 2.0 keeps the fallback on the
// cheap path; the SHIPPING path is gui::stroke, which builds its own geometry and
// measures the coverage per fragment, and it is what makes the stroke's width stop
// depending on the angle (see tests/graph_panel_pixels.cpp's angle assertion).
constexpr float kCurveWidth = stroke::kWidthPx;

// Length of the short axis marks. The model publishes positions and leaves the
// length to the drawing layer; 4 is the convention its own KFrameTickLength
// documents (and the room it reserves above a frame number).
constexpr int kAxisTickLength = 4;

// The box-select marquee's corner radius, in pixels: the timeline's own box
// (ctrl/time/Renderer.cpp draws a 6px rounded rect) so the two views' selection
// gestures are the same shape.
constexpr float kMarqueeRounding = 6.0f;

// The label halo: the same glyphs drawn at the eight compass offsets of radius 1
// and radius 2, so the ring reaches 2px past the glyph ink in every direction.
// Read only while gui::kDrawTextHalo is on (shipped off: see addHaloedText).
constexpr int kHaloRadius = 2;
constexpr float kHaloAlpha = 0.85f;

// Emphasis: the alpha an unfocused curve and its keys are drawn at WHILE a focus
// exists (with no focus every curve is full). 45% keeps the shape readable and
// leaves no doubt about which curve is the focused one.
constexpr float kUnfocusedAlpha = 0.45f;

// A hidden legend row keeps its swatch and its label - that is how the user sees
// what is hidden and clicks it back - so both read at a third of the strength
// rather than disappearing.
constexpr float kHiddenAlpha = 0.35f;

// Gap between a legend swatch and its label. The model insets the swatch from the
// plot by its own kLegendLeftGap (6px); the label starts the same distance after
// the swatch, so a row reads as one line.
constexpr int kLegendLabelGap = 6;

// The text size used when the widget's font states no size at all (neither a pixel
// nor a usable point size: gui::imgui_qt::panelFontPixelSize returns 0). 13 is
// ImGui's own default font size, which keeps the panel legible instead of empty.
constexpr float kFallbackFontPixelSize = 13.0f;

// The accessibility ids that are not a legend row index. Legend rows are the ids
// 0..n-1, so the fixed items are negative - one namespace, no collision.
constexpr int kSummaryItemId = -1;
constexpr int kFitItemId = -2;
// The key menu's own action (see the class comment): published only while the
// selection names exactly one key, and it opens the application's own easing menu
// at that key's marker.
constexpr int kKeyMenuItemId = -3;

// lupdate's context for everything this file says to the user: the class is not a
// QObject, so there is no tr() member for it to find.
const char* kTranslationContext = "gui::ImGuiOverlay";

// The runtime half of the translation pair. The class is not a QObject, so it asks
// with an EXPLICIT context.
//
// THE EXTRACTOR HALF IS AT THE CALL SITE, and it has to be: lupdate cannot see a
// string that only travels as an argument to a wrapper - a literal here would
// compile, run, and reach no catalog at all, shipping English in every language
// with nothing failing. So every user-visible literal in this file is written as
// QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "...") (or its _N_ plural form for the two
// that carry %n) INSIDE the call, which expands to the plain literal at compile
// time and puts the message and its context in front of the extractor, in the one
// expression where the two halves cannot drift. The repository's own precedent is
// gui::graphPanelTr + QT_TRANSLATE_NOOP("GraphPanel", ...) in
// src/gui/GraphPanelModel.cpp, and scripts/i18n/check_tr_contexts.py polices the
// pairing.
QString trText(const char* aSource, int aPlural = -1) {
    return QCoreApplication::translate(kTranslationContext, aSource, nullptr, aPlural);
}

ImU32 toImU32(const QColor& aColor) {
    return IM_COL32(aColor.red(), aColor.green(), aColor.blue(), aColor.alpha());
}

// The colour as it should be painted under a strength factor. Emphasis and
// "hidden" both scale ALPHA and leave the hue alone, so a dimmed curve still
// reads as its own channel.
ImU32 toImU32Strength(const QColor& aColor, float aStrength) {
    QColor color = aColor;
    color.setAlphaF(qBound(0.0f, color.alphaF() * aStrength, 1.0f));
    return toImU32(color);
}

// The window rect as ImGui's half-open rect: right()/bottom() are the last
// INCLUDED pixel columns.
ImVec2 rectMin(const QRect& aRect) { return ImVec2(float(aRect.left()), float(aRect.top())); }
ImVec2 rectMax(const QRect& aRect) { return ImVec2(float(aRect.right() + 1), float(aRect.bottom() + 1)); }

// The draw list's own font and size. drawPanel must NOT go through
// ImGui::CalcTextSize(): that one reads the current context (g.Font), and the
// drawing has to be callable from a headless pixel gate that owns nothing but a
// draw list. ImDrawList keeps the frame's font in its shared data, so this is
// the same font the glyphs are drawn with.
ImFont* drawListFont(ImDrawList& aDrawList) { return aDrawList._Data ? aDrawList._Data->Font : nullptr; }
float drawListFontSize(ImDrawList& aDrawList) { return aDrawList._Data ? aDrawList._Data->FontSize : 0.0f; }

ImVec2 measureText(ImFont* aFont, float aFontSize, const char* aText) {
    if (!aFont || !aText || !*aText)
        return ImVec2(0.0f, 0.0f);
    return aFont->CalcTextSizeA(aFontSize, FLT_MAX, 0.0f, aText);
}

// One glyph run, haloed: the 16 halo passes, then the text itself. The halo is
// what kept a label readable over arbitrary artwork - the panel has no background
// - and the 16 offsets are what make the ring solid rather than four smudges (see
// the constant above for the geometry).
//
// SHIPPED OFF (gui::kDrawTextHalo, in the header): the 17 glyph passes per label
// are now the contrast outline pass's job, which rings the same glyphs - and every
// other piece of panel ink - but only where the ink would otherwise have been
// lost, and in the outline colour rather than a panel tone. The body below is kept
// whole so the flag is the only difference between the two.
void addHaloedText(
    ImDrawList& aDrawList, ImFont* aFont, float aFontSize, const ImVec2& aPos, ImU32 aColor, ImU32 aHalo,
    const char* aText
) {
    if (!aFont || !aText || !*aText)
        return;

    if (kDrawTextHalo) {
        static const int kOffsets[8][2] = {
            {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
        };
        for (int radius = kHaloRadius; radius >= 1; --radius) {
            for (const int* offset : kOffsets) {
                aDrawList.AddText(
                    aFont, aFontSize,
                    ImVec2(aPos.x + float(offset[0] * radius), aPos.y + float(offset[1] * radius)), aHalo, aText
                );
            }
        }
    }
    aDrawList.AddText(aFont, aFontSize, aPos, aColor, aText);
}

enum class LabelAlign { Left, Centre, Right };

// One label inside the box the model decided, at the alignment the model
// documented for it, with the halo. The box is the model's; only the placement
// inside it is the drawing layer's.
//
// THE GLYPHS ARE CLAMPED INTO THE BOX. The model measures its boxes with the
// default UI font (it has no painter) and this layer draws with the widget's, so
// the two line heights are not the same number; a layer font taller than the box
// would put half its overflow ABOVE the box's top edge - and for a frame number
// that is above the plot's top edge, i.e. inside the plot clip, which is how the
// top row of ruler numbers came out with their caps cut off. Centring therefore
// takes the larger of "centred" and "flush with the box's top": an over-tall font
// hangs into the box's own interior (where there is room) instead of out of it.
void drawLabel(
    ImDrawList& aDrawList, ImFont* aFont, float aFontSize, const QRect& aBox, const QString& aText, LabelAlign aAlign,
    ImU32 aColor, ImU32 aHalo, int aRightGapPx = 0
) {
    if (!aFont || aText.isEmpty() || aBox.isEmpty())
        return;

    const QByteArray utf8 = aText.toUtf8();
    const ImVec2 size = measureText(aFont, aFontSize, utf8.constData());
    float x = float(aBox.left());
    if (aAlign == LabelAlign::Centre) {
        x += (float(aBox.width()) - size.x) * 0.5f;
    } else if (aAlign == LabelAlign::Right) {
        // THE RIGHT GAP IS TAKEN FROM THE BOX'S OWN SPARE WIDTH (see
        // kValueLabelGap): a label that fills its box is left exactly where
        // right-alignment puts it, so the gap can never push a number out of the
        // column the model measured for it. What is left over - a label wider than
        // its box, which the measured column cannot produce but a wider drawing font
        // can - is the caller's clip to hold back.
        const float spare = ImMax(0.0f, float(aBox.width()) - size.x);
        x = float(aBox.left() + aBox.width()) - size.x - ImMin(float(aRightGapPx), spare);
    }
    const float y = float(aBox.top()) + ImMax((float(aBox.height()) - size.y) * 0.5f, 0.0f);
    addHaloedText(aDrawList, aFont, aFontSize, ImVec2(x, y), aColor, aHalo, utf8.constData());
}

// Whether a key MARKER is one the editor's selection carries. The model publishes
// the marker centres without their frames (PanelCurve::keyDots) and the focus's
// own SelectedKey carries the same pixel from ctrl::graph::keyDot through the
// same scaler, so the marker's x is the whole identity available here.
bool isKeySelected(const PanelFocus& aFocus, core::TimeKeyType aType, const QPointF& aDot) {
    if (aFocus.selectedKeys.isEmpty())
        return false;

    const int x = int(aDot.x());
    for (const PanelFocus::SelectedKey& key : aFocus.selectedKeys) {
        if (key.type == aType && key.x == x)
            return true;
    }
    return false;
}

} // namespace

//-------------------------------------------------------------------------------------------------
// THE CONTRAST OUTLINE AND COMPOSITE PASS (declared and described in the header).
//
// WHY THE GLSL IS A STRING HERE AND NOT A FILE IN assets/shaders. Every other
// shader in this repository is a resource (`:/shaders/*.glsl`, from
// assets/assets.qrc). This pass cannot be: its own pixel gate
// (tests/graph_panel_pixels.cpp) is the reason the pass is a free unit at all, and
// that target compiles THIS translation unit from a source list in
// tests/CMakeLists.txt that has never carried assets.qrc - a fragment shader that
// is only a resource is a shader the gate cannot load. gl::EasyTextureDrawer, the
// other full-screen textured-quad unit here, embeds its shaders for the same kind
// of reason. One copy, reachable by both.
namespace {

// A full-screen triangle generated from gl_VertexID: the three corners (0,0) (2,0)
// (0,2) in UV space land on (-1,-1) (3,-1) (-1,3) in clip space, so the pass needs
// no vertex buffer and no vertex attributes - only a bound VAO, which a core
// profile requires for any draw call.
const char* kOutlineVertexSource =
    "#version 400 core\n"
    "out vec2 vUv;\n"
    "void main() {\n"
    "    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    vUv = corner;\n"
    "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

const char* kOutlineFragmentSource =
    "#version 400 core\n"
    "in vec2 vUv;\n"
    "out vec4 FragColor;\n"
    "uniform sampler2D uOverlay;\n"
    "uniform sampler2D uBackdrop;\n"
    "uniform vec2 uTexel;\n"
    "uniform float uMinContrast;\n"
    "uniform vec3 uOutlineColour;\n"
    "uniform float uRingRadiusPx;\n"
    "\n"
    "// WCAG relative luminance: the sRGB channels are linearised first, because\n"
    "// uMinContrast is WCAG's 3:1 and that ratio only means 3:1 in linear light.\n"
    "float linearise(float aChannel) {\n"
    "    return aChannel <= 0.04045 ? aChannel / 12.92 : pow((aChannel + 0.055) / 1.055, 2.4);\n"
    "}\n"
    "float luma(vec3 aRgb) {\n"
    "    return dot(vec3(0.2126, 0.7152, 0.0722),\n"
    "               vec3(linearise(aRgb.r), linearise(aRgb.g), linearise(aRgb.b)));\n"
    "}\n"
    "float contrast(float aLuma, float aOther) {\n"
    "    float hi = max(aLuma, aOther);\n"
    "    float lo = min(aLuma, aOther);\n"
    "    return (hi + 0.05) / (lo + 0.05);\n"
    "}\n"
    "\n"
    "// One tap of the overlay, in the panel's OWN colour. The overlay target holds\n"
    "// the ImGui backend's own source-over blend, so its rgb arrives PREMULTIPLIED\n"
    "// by the coverage in alpha; dividing that back out is what lets the contrast\n"
    "// test and the composite below be written in the panel's colours. Without it a\n"
    "// partially covered pixel would composite at alpha squared.\n"
    "vec4 tap(vec2 aUv) {\n"
    "    vec4 t = texture(uOverlay, aUv);\n"
    "    return vec4(t.a > 0.0 ? t.rgb / t.a : vec3(0.0), t.a);\n"
    "}\n"
    "\n"
    "// The ink test is alpha > 0.0 and not an epsilon: a hairline at partial\n"
    "// coverage still counts as ink. The tail of the anti-aliasing can therefore\n"
    "// make the ring about a pixel wider than the visible ink; if it ever looks fat,\n"
    "// this comparison is the knob (an epsilon on aTap.a).\n"
    "bool lowContrastInk(vec4 aTap, float aBackdropLuma) {\n"
    "    return aTap.a > 0.0 && contrast(luma(aTap.rgb), aBackdropLuma) < uMinContrast;\n"
    "}\n"
    "\n"
    "void main() {\n"
    "    vec4 ov = tap(vUv);\n"
    "    vec4 tW = tap(vUv + vec2(-uTexel.x, 0.0));\n"
    "    vec4 tE = tap(vUv + vec2( uTexel.x, 0.0));\n"
    "    vec4 tN = tap(vUv + vec2(0.0, -uTexel.y));\n"
    "    vec4 tS = tap(vUv + vec2(0.0,  uTexel.y));\n"
    "\n"
    "    // The optional second ring, two pixels out. A uniform branch, so the\n"
    "    // shipped 1px ring pays for four taps and not eight.\n"
    "    vec4 tW2 = vec4(0.0);\n"
    "    vec4 tE2 = vec4(0.0);\n"
    "    vec4 tN2 = vec4(0.0);\n"
    "    vec4 tS2 = vec4(0.0);\n"
    "    if (uRingRadiusPx > 1.5) {\n"
    "        tW2 = tap(vUv + vec2(-2.0 * uTexel.x, 0.0));\n"
    "        tE2 = tap(vUv + vec2( 2.0 * uTexel.x, 0.0));\n"
    "        tN2 = tap(vUv + vec2(0.0, -2.0 * uTexel.y));\n"
    "        tS2 = tap(vUv + vec2(0.0,  2.0 * uTexel.y));\n"
    "    }\n"
    "\n"
    "    // The outline's strength is the STRONGEST TAP, never this pixel's own\n"
    "    // coverage: that is what makes the ring track the shape - a 40% stroke gets\n"
    "    // a 40% ring, adjacent strokes fill in without double-darkening, and no ink\n"
    "    // nearby draws nothing at all.\n"
    "    float maxA = max(max(max(tW.a, tE.a), max(tN.a, tS.a)),\n"
    "                     max(max(tW2.a, tE2.a), max(tN2.a, tS2.a)));\n"
    "\n"
    "    vec4 backdrop = texture(uBackdrop, vUv);\n"
    "    float backdropLuma = luma(backdrop.rgb);\n"
    "    // Per pixel, against THIS pixel's backdrop, so a glyph straddling a light\n"
    "    // and a dark region is ringed only on the side that needs it.\n"
    "    bool low = lowContrastInk(tW, backdropLuma) || lowContrastInk(tE, backdropLuma)\n"
    "            || lowContrastInk(tN, backdropLuma) || lowContrastInk(tS, backdropLuma)\n"
    "            || lowContrastInk(tW2, backdropLuma) || lowContrastInk(tE2, backdropLuma)\n"
    "            || lowContrastInk(tN2, backdropLuma) || lowContrastInk(tS2, backdropLuma);\n"
    "\n"
    "    vec3 col = backdrop.rgb;\n"
    "    if (low && maxA > 0.0)\n"
    "        col = mix(col, uOutlineColour, maxA);\n"
    "    // The panel composites on top at its own coverage, NEVER gated at\n"
    "    // alpha >= 1.0: every edge and every hairline is antialiased, so clipping\n"
    "    // to full coverage would jag the edges and erase the thin strokes.\n"
    "    col = mix(col, ov.rgb, ov.a);\n"
    "    // The backdrop's own alpha survives where nothing is drawn, so the pass is\n"
    "    // an exact no-op on every pixel the panel does not touch.\n"
    "    FragColor = vec4(col, mix(backdrop.a, 1.0, ov.a));\n"
    "}\n";

// The pass's GL objects, held as a RAW OWNED POINTER rather than a smart one, and
// deliberately: every destructor here calls GL, and a namespace-scope object is
// destroyed by exit() - by which time the context this pass was built on is long
// gone, which is a segfault inside glDeleteVertexArrays (measured). The objects are
// therefore destroyed exactly once, by outline::shutdown(), while a context is
// current; if that call is never made, the process is going away anyway.
struct OutlinePass {
    gl::EasyShaderProgram shader;
    std::unique_ptr<gl::VertexArrayObject> quad;
    int overlayLocation = -1;
    int backdropLocation = -1;
    int texelLocation = -1;
    int minContrastLocation = -1;
    int colourLocation = -1;
    int ringLocation = -1;
};

OutlinePass* gOutline = nullptr;

// The pixel size of a framebuffer's colour attachment. The backdrop copy is a
// BLIT, which requires the source and the destination rectangles to be the same
// size and inside their framebuffers, so the size has to be the widget
// framebuffer's OWN - and that is not a number this file may re-derive: measured
// on this machine (Qt 6.11, 401x321 logical at QT_SCALE_FACTOR=1.5),
// QOpenGLWidget's framebuffer is 602x482, i.e. qRound(w * dpr), while truncating
// gives 601x481 and would leave a one-pixel seam at fractional ratios. The size
// is asked of the attachment instead: QOpenGLWidget's is a multisampled
// RENDERBUFFER (measured: samples=4, GL_RENDERBUFFER), which is also what makes
// the source of the blit a resolve. Zero when there is nothing to ask.
QSize framebufferPixels(GLuint aFramebuffer) {
    gl::Global::Functions& ggl = gl::Global::functions();

    GLint previousRead = 0;
    GLint previousDraw = 0;
    ggl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    ggl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    ggl.glBindFramebuffer(GL_FRAMEBUFFER, aFramebuffer);

    QSize size;
    GLint type = 0;
    GLint name = 0;
    ggl.glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &type
    );
    ggl.glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &name
    );
    if (type == GL_RENDERBUFFER) {
        GLint width = 0;
        GLint height = 0;
        ggl.glBindRenderbuffer(GL_RENDERBUFFER, GLuint(name));
        ggl.glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &width);
        ggl.glGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_HEIGHT, &height);
        size = QSize(width, height);
    } else if (type == GL_TEXTURE) {
        // A single-sample texture attachment has its size at mip level 0.
        GLint width = 0;
        GLint height = 0;
        ggl.glBindTexture(GL_TEXTURE_2D, GLuint(name));
        ggl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &width);
        ggl.glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &height);
        ggl.glBindTexture(GL_TEXTURE_2D, 0);
        size = QSize(width, height);
    }

    ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousRead));
    ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousDraw));
    return size;
}

// The contrast outline pass's two targets, both at the widget framebuffer's own
// pixel size: the BACKDROP (the pixels the panel would sit on - a copy) and the
// panel's own COVERAGE render. Rebuilt only when that size changes (a resize, or
// a device-pixel-ratio change).
struct OutlineTargets {
    QSize pixels;
    std::unique_ptr<gl::Texture> backdropTexture;
    std::unique_ptr<gl::Framebuffer> backdropFbo;
    std::unique_ptr<gl::Texture> overlayTexture;
    std::unique_ptr<gl::Framebuffer> overlayFbo;

    // The GL objects go while a context is current: called before a rebuild and
    // from the overlay's teardown.
    void release() {
        pixels = QSize();
        backdropFbo.reset();
        backdropTexture.reset();
        overlayFbo.reset();
        overlayTexture.reset();
    }

    // False when the driver refuses one of the four objects; the caller then draws
    // the panel without the pass rather than drawing nothing.
    bool ensure(const QSize& aPixels) {
        if (pixels == aPixels && overlayFbo)
            return true;
        release();

        backdropTexture.reset(new gl::Texture());
        backdropTexture->create(aPixels);
        // Both textures are sampled at the texel centre 1:1, and the taps must not
        // WRAP: with the default REPEAT, a tap one pixel left of the first column
        // would read the ink at the right edge of the screen.
        backdropTexture->setFilter(GL_NEAREST);
        backdropTexture->setWrap(GL_CLAMP_TO_EDGE);
        backdropFbo.reset(new gl::Framebuffer());
        backdropFbo->setColorAttachment(0, backdropTexture->id());

        overlayTexture.reset(new gl::Texture());
        overlayTexture->create(aPixels);
        overlayTexture->setFilter(GL_NEAREST);
        overlayTexture->setWrap(GL_CLAMP_TO_EDGE);
        overlayFbo.reset(new gl::Framebuffer());
        overlayFbo->setColorAttachment(0, overlayTexture->id());

        if (!backdropFbo->isComplete() || !overlayFbo->isComplete()) {
            release();
            return false;
        }
        pixels = aPixels;
        return true;
    }
};

} // namespace

namespace outline {

bool init() {
    if (gOutline)
        return true;

    std::unique_ptr<OutlinePass> pass(new OutlinePass());
    if (!pass->shader.setVertexSource(QString::fromLatin1(kOutlineVertexSource))
        || !pass->shader.setFragmentSource(QString::fromLatin1(kOutlineFragmentSource))
        || !pass->shader.link()) {
        // Not fatal: the caller draws the panel into the widget framebuffer
        // directly, i.e. exactly the frame this pass was added to.
        XC_DEBUG_REPORT("the contrast outline pass did not build: %s", qPrintable(pass->shader.log()));
        return false;
    }
    pass->quad.reset(new gl::VertexArrayObject());
    pass->overlayLocation = pass->shader.uniformLocation("uOverlay");
    pass->backdropLocation = pass->shader.uniformLocation("uBackdrop");
    pass->texelLocation = pass->shader.uniformLocation("uTexel");
    pass->minContrastLocation = pass->shader.uniformLocation("uMinContrast");
    pass->colourLocation = pass->shader.uniformLocation("uOutlineColour");
    pass->ringLocation = pass->shader.uniformLocation("uRingRadiusPx");

    // Released by shutdown(), never by exit(): see gOutline above.
    gOutline = pass.release();
    return true;
}

void shutdown() {
    delete gOutline;
    gOutline = nullptr;
}

void copyBackdrop(GLuint aSourceFbo, GLuint aDestinationFbo, const QSize& aPixelSize) {
    if (aPixelSize.isEmpty())
        return;

    gl::Global::Functions& ggl = gl::Global::functions();
    GLint previousRead = 0;
    GLint previousDraw = 0;
    ggl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
    ggl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);

    // A blit is scissored exactly like a draw, and the ImGui backend leaves
    // GL_SCISSOR_TEST enabled at the box its own last draw command set.
    ggl.glDisable(GL_SCISSOR_TEST);
    ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, aSourceFbo);
    ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, aDestinationFbo);
    ggl.glBlitFramebuffer(
        0, 0, aPixelSize.width(), aPixelSize.height(), 0, 0, aPixelSize.width(), aPixelSize.height(),
        GL_COLOR_BUFFER_BIT, GL_NEAREST
    );

    // The framebuffer bindings go back as they were found: this is a copy, not a
    // change of target.
    ggl.glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousRead));
    ggl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousDraw));
    GL_CHECK_ERROR();
}

void composite(GLuint aOverlay, GLuint aBackdrop, const QSize& aPixelSize, const Params& aParams) {
    if (!gOutline || aPixelSize.isEmpty())
        return;

    gl::Global::Functions& ggl = gl::Global::functions();

    // THE PASS IS A GUEST IN THE FRAME, AND IT PUTS BACK WHAT IT CHANGES. The
    // bindings it saves here are not courtesies: the application's paint path keeps
    // ONE default vertex array bound for the whole session
    // (MainDisplayWidget::initializeGL creates it and comments "keep binding"), and
    // the painter engine that runs at the TOP of the next paint relies on that -
    // every glVertexAttribPointer it makes needs an array object bound in a core
    // profile. MEASURED: a version of this pass that ended with glBindVertexArray(0)
    // made the next frame's painter pass fail with GL_INVALID_OPERATION in
    // glVertexAttribPointer and glDrawArrays, and the error surfaced one call later
    // as an assert in gl::EasyShaderProgram::release(). The ImGui backend restores
    // the same binding for the same reason.
    GLint savedVao = 0;
    GLint savedActiveTexture = 0;
    GLint savedTexture[2] = {0, 0};
    ggl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &savedVao);
    ggl.glGetIntegerv(GL_ACTIVE_TEXTURE, &savedActiveTexture);
    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture[0]);
    ggl.glActiveTexture(GL_TEXTURE1);
    ggl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &savedTexture[1]);

    // Every piece of state this pass depends on is SET rather than assumed: it runs
    // at the very end of the frame, where the ImGui backend has left blending on
    // with its own function, the scissor box enabled, and the viewport at whatever
    // its last draw command asked for.
    ggl.glDisable(GL_BLEND);
    ggl.glDisable(GL_DEPTH_TEST);
    ggl.glDisable(GL_CULL_FACE);
    ggl.glDisable(GL_SCISSOR_TEST);
    // Dithering is on by default in GL and would perturb the 8-bit write by a bit,
    // which would stop the pass from being an exact no-op on the pixels the panel
    // does not touch (and off it is again on the next frame: paintGL resets the
    // render state at the top of every paint).
    ggl.glDisable(GL_DITHER);
    ggl.glViewport(0, 0, aPixelSize.width(), aPixelSize.height());

    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, aOverlay);
    ggl.glActiveTexture(GL_TEXTURE1);
    ggl.glBindTexture(GL_TEXTURE_2D, aBackdrop);

    OutlinePass& pass = *gOutline;
    pass.quad->bind();
    pass.shader.bind();
    pass.shader.setUniformValue(pass.overlayLocation, 0);
    pass.shader.setUniformValue(pass.backdropLocation, 1);
    pass.shader.setUniformValue(
        pass.texelLocation,
        QVector2D(GLfloat(1.0) / GLfloat(aPixelSize.width()), GLfloat(1.0) / GLfloat(aPixelSize.height()))
    );
    pass.shader.setUniformValue(pass.minContrastLocation, aParams.minContrast);
    pass.shader.setUniformValue(
        pass.colourLocation,
        QVector3D(GLfloat(aParams.colour.redF()), GLfloat(aParams.colour.greenF()), GLfloat(aParams.colour.blueF()))
    );
    pass.shader.setUniformValue(pass.ringLocation, aParams.ringRadiusPx);
    ggl.glDrawArrays(GL_TRIANGLES, 0, 3);
    // The PROGRAM is deliberately left unbound, exactly as the application's own
    // gl::EasyTextureDrawer leaves it at the end of every paint: 0 is a state the
    // painter's next bind() establishes for itself.
    pass.shader.release();

    ggl.glBindVertexArray(GLuint(savedVao));
    ggl.glBindTexture(GL_TEXTURE_2D, GLuint(savedTexture[1]));
    ggl.glActiveTexture(GL_TEXTURE0);
    ggl.glBindTexture(GL_TEXTURE_2D, GLuint(savedTexture[0]));
    ggl.glActiveTexture(GLenum(savedActiveTexture));
    GL_CHECK_ERROR();
}

} // namespace outline

//-------------------------------------------------------------------------------------------------
// The coverage stroke: the panel's curve ink. gui::stroke in the header says why it is a
// pass of its own rather than ImDrawList::AddPolyline, what the coverage function is, and
// where in the frame its callback sits. What follows is that pass's machinery.
namespace {

// The per-segment program. Every vertex carries the quad's own corner AND the segment's
// two endpoints, so the fragment can measure the distance to the SEGMENT rather than to
// the quad.
//
// THE PROJECTION IS THE BACKEND'S OWN ORTHO, term for term
// (ImGui_ImplOpenGL3_SetupRenderState): it is diagonal plus a translation, so its four
// numbers are all the vertex shader needs, and using the same numbers rather than
// rebuilding a matrix here is what makes a curve land on exactly the pixel ImGui's own
// geometry would have inked.
const char* kStrokeVertexSource =
    "#version 400 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec2 aP0;\n"
    "layout(location = 2) in vec2 aP1;\n"
    "uniform vec4 uOrtho;\n"
    "out vec2 vPos;\n"
    "out vec2 vP0;\n"
    "out vec2 vP1;\n"
    "void main() {\n"
    "    vPos = aPos;\n"
    "    vP0 = aP0;\n"
    "    vP1 = aP1;\n"
    "    gl_Position = vec4(aPos.x * uOrtho.x + uOrtho.z, aPos.y * uOrtho.y + uOrtho.w, 0.0, 1.0);\n"
    "}\n";

const char* kStrokeFragmentSource =
    "#version 400 core\n"
    "in vec2 vPos;\n"
    "in vec2 vP0;\n"
    "in vec2 vP1;\n"
    "uniform float uPxScale;\n"
    "uniform float uHalfWidth;\n"
    "uniform float uFilter;\n"
    "uniform vec3 uInk;\n"
    "uniform float uAlpha;\n"
    "uniform int uMode;\n"
    "out vec4 FragColor;\n"
    "\n"
    "void main() {\n"
    "    // The distance to the SEGMENT, never to its infinite line: the clamp on h is what\n"
    "    // gives each segment its own round join and cap with no extra geometry at all.\n"
    "    vec2 ba = vP1 - vP0;\n"
    "    vec2 pa = vPos - vP0;\n"
    "    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-8), 0.0, 1.0);\n"
    "    // In DEVICE pixels, which is the currency the filter and the stroke's weight are\n"
    "    // in (the nominal width is a logical one, so a 2x screen gets a 2x stroke).\n"
    "    float d = length(pa - ba * h) * uPxScale;\n"
    "\n"
    "    // The box filter: a pixel's coverage of a half-plane edge is the length of the\n"
    "    // filter's span that lies inside the stroke over the span's own width, so the 50%\n"
    "    // crossing lands exactly on the nominal edge at any sub-pixel phase.\n"
    "    float r = 0.5 * uFilter;\n"
    "    // A stroke narrower than the filter keeps the filter's footprint (1 device px) and\n"
    "    // scales its ALPHA instead of thinning away - Rougier's sub-pixel rule.\n"
    "    float body = max(uHalfWidth, r);\n"
    "    float cov = clamp((body + r - d) / uFilter, 0.0, 1.0) * min(1.0, uHalfWidth / r);\n"
    "\n"
    "    // 0: the coverage alone, accumulated with GL_MAX, so overlapping segments take the\n"
    "    // UNION of their capsules instead of blending twice. 1: the ink straight into the\n"
    "    // frame - the path taken when the scratch target could not be created, where a\n"
    "    // join that overlaps itself beats a missing curve.\n"
    "    FragColor = (uMode == 0) ? vec4(cov) : vec4(uInk, cov * uAlpha);\n"
    "}\n";

// The composite: one full-screen triangle that turns the accumulated coverage back into
// the panel's own premultiplied ink. No attributes at all (gl_VertexID carries the
// corner), which is why it needs no vertex buffer.
const char* kStrokeCompositeVertexSource =
    "#version 400 core\n"
    "void main() {\n"
    "    vec2 corner = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
    "    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

const char* kStrokeCompositeFragmentSource =
    "#version 400 core\n"
    "uniform sampler2D uCoverage;\n"
    "uniform vec2 uSize;\n"
    "uniform vec3 uInk;\n"
    "uniform float uAlpha;\n"
    "out vec4 FragColor;\n"
    "void main() {\n"
    "    // gl_FragCoord's centre is the texel centre of the same pixel of an equally sized\n"
    "    // attachment, so this reads the coverage 1:1 with no filtering of its own.\n"
    "    float cov = texture(uCoverage, gl_FragCoord.xy / uSize).a;\n"
    "    // The backend's own source-over (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,\n"
    "    // GL_ONE_MINUS_SRC_ALPHA) makes this the PREMULTIPLIED ink the contrast outline\n"
    "    // pass reads back - the convention the overlay's framebuffer holds.\n"
    "    FragColor = vec4(uInk, cov * uAlpha);\n"
    "}\n";

// The callback data ImGui copies for ONE curve (ImDrawList::AddCallback's userdata_size
// path), so the geometry lives inside the draw list and nothing outlives the call. 24
// bytes of header keeps every blob a multiple of 8, which is what keeps the float pairs
// that follow it aligned inside ImGui's own byte buffer.
struct CurveInkBlob {
    int pointCount;   // >= 2
    float red;
    float green;
    float blue;
    float alpha;      // the ink's strength: 1.0, or kUnfocusedAlpha
    float halfWidth;  // the stroke's half width, in LOGICAL px: the curve's kCurveWidth/2,
                      // a handle stem's 0.5. Both are the same stroke otherwise, which is
                      // why there is one record and not two.
};
static_assert(sizeof(CurveInkBlob) == 24, "the blob's alignment rule");

// The pass's GL objects, held as a RAW OWNED POINTER for the same reason outline's are:
// every destructor here calls GL, and a namespace-scope object is destroyed by exit(),
// by which time this context is long gone (a measured segfault inside glDelete*). They
// are destroyed exactly once, by stroke::shutdown(), while a context is current.
struct StrokePass {
    gl::EasyShaderProgram segments;
    gl::EasyShaderProgram composite;
    std::unique_ptr<gl::VertexArrayObject> vao;

    int orthoLocation = -1;
    int pxScaleLocation = -1;
    int halfWidthLocation = -1;
    int filterLocation = -1;
    int inkLocation = -1;
    int alphaLocation = -1;
    int modeLocation = -1;

    int coverageLocation = -1;
    int sizeLocation = -1;
    int compositeInkLocation = -1;
    int compositeAlphaLocation = -1;

    // The coverage scratch: the union of one curve's capsules, at the framebuffer's own
    // pixel size. Rebuilt only when that size changes.
    QSize pixels;
    std::unique_ptr<gl::Texture> coverageTexture;
    std::unique_ptr<gl::Framebuffer> coverageFbo;

    // The vertices of the curve being drawn, kept so a frame that draws the same curves
    // does not reallocate three vectors per curve.
    std::vector<gl::Vector2> positions;
    std::vector<gl::Vector2> starts;
    std::vector<gl::Vector2> ends;

    // False when the driver refuses either object; the caller then draws the ink
    // straight into the frame rather than losing it.
    bool ensureCoverage(const QSize& aPixels) {
        if (pixels == aPixels && coverageFbo)
            return true;
        pixels = QSize();
        coverageFbo.reset();
        coverageTexture.reset();
        coverageTexture.reset(new gl::Texture());
        coverageTexture->create(aPixels);
        coverageTexture->setFilter(GL_NEAREST);
        coverageTexture->setWrap(GL_CLAMP_TO_EDGE);
        coverageFbo.reset(new gl::Framebuffer());
        coverageFbo->setColorAttachment(0, coverageTexture->id());
        if (!coverageFbo->isComplete()) {
            coverageFbo.reset();
            coverageTexture.reset();
            return false;
        }
        pixels = aPixels;
        return true;
    }
};

StrokePass* gStroke = nullptr;

// Every piece of GL state the callback changes, so it can be put back exactly as it was
// found. This is not a courtesy: the ImGui backend keeps its shader program, its vertex
// array and its ARRAY BUFFER bound across a draw list's commands, and uploads the NEXT
// draw list's vertices with whatever array buffer is bound when it gets there.
struct GuestState {
    GLint program = 0;
    GLint vao = 0;
    GLint arrayBuffer = 0;
    GLint activeTexture = 0;
    GLint texture0 = 0;
    GLint drawFbo = 0;
    GLint readFbo = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLint scissorBox[4] = {0, 0, 0, 0};
    GLint blendEquationRgb = 0;
    GLint blendEquationAlpha = 0;
    GLint blendSrcRgb = 0;
    GLint blendDstRgb = 0;
    GLint blendSrcAlpha = 0;
    GLint blendDstAlpha = 0;
    GLboolean blend = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean dither = GL_FALSE;

    void save(gl::Global::Functions& aGgl) {
        aGgl.glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        aGgl.glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        aGgl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        aGgl.glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        aGgl.glActiveTexture(GL_TEXTURE0);
        aGgl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
        aGgl.glActiveTexture(GLenum(activeTexture));
        aGgl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        aGgl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
        aGgl.glGetIntegerv(GL_VIEWPORT, viewport);
        aGgl.glGetIntegerv(GL_SCISSOR_BOX, scissorBox);
        aGgl.glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
        aGgl.glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
        aGgl.glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
        aGgl.glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
        aGgl.glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
        aGgl.glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
        blend = aGgl.glIsEnabled(GL_BLEND);
        scissor = aGgl.glIsEnabled(GL_SCISSOR_TEST);
        dither = aGgl.glIsEnabled(GL_DITHER);
    }

    void restore(gl::Global::Functions& aGgl) const {
        aGgl.glBlendEquationSeparate(GLenum(blendEquationRgb), GLenum(blendEquationAlpha));
        aGgl.glBlendFuncSeparate(
            GLenum(blendSrcRgb), GLenum(blendDstRgb), GLenum(blendSrcAlpha), GLenum(blendDstAlpha)
        );
        if (blend)
            aGgl.glEnable(GL_BLEND);
        else
            aGgl.glDisable(GL_BLEND);
        if (dither)
            aGgl.glEnable(GL_DITHER);
        else
            aGgl.glDisable(GL_DITHER);
        if (scissor)
            aGgl.glEnable(GL_SCISSOR_TEST);
        else
            aGgl.glDisable(GL_SCISSOR_TEST);
        aGgl.glScissor(scissorBox[0], scissorBox[1], scissorBox[2], scissorBox[3]);
        aGgl.glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        aGgl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(drawFbo));
        aGgl.glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(readFbo));
        aGgl.glBindVertexArray(GLuint(vao));
        aGgl.glBindBuffer(GL_ARRAY_BUFFER, GLuint(arrayBuffer));
        aGgl.glActiveTexture(GL_TEXTURE0);
        aGgl.glBindTexture(GL_TEXTURE_2D, GLuint(texture0));
        aGgl.glActiveTexture(GLenum(activeTexture));
        aGgl.glUseProgram(GLuint(program));
    }
};

// The device-pixel rect a clip rect covers, in the coordinates glScissor takes (y counted
// from the BOTTOM), computed with the backend's own arithmetic and its own truncations -
// so the pass clips on exactly the pixels ImGui clips its own commands on.
QRect scissorRect(const ImVec4& aClipRect, const ImVec2& aDisplayPos, const ImVec2& aScale, int aFramebufferHeight) {
    const ImVec2 clipMin((aClipRect.x - aDisplayPos.x) * aScale.x, (aClipRect.y - aDisplayPos.y) * aScale.y);
    const ImVec2 clipMax((aClipRect.z - aDisplayPos.x) * aScale.x, (aClipRect.w - aDisplayPos.y) * aScale.y);
    if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
        return QRect();
    return QRect(
        int(clipMin.x), int(float(aFramebufferHeight) - clipMax.y), int(clipMax.x - clipMin.x),
        int(clipMax.y - clipMin.y)
    );
}

// The device-pixel rect one curve's ink can reach (its own bounding box, grown by the
// stroke's whole footprint), in the same coordinates.
QRect curveInkRect(const CurveInkBlob* aInk, const ImDrawData* aDrawData, int aFramebufferHeight) {
    const float scale = aDrawData->FramebufferScale.x > 0.0f ? float(aDrawData->FramebufferScale.x) : 1.0f;
    // Half the stroke (its own, not the curve's: this rect is asked for both the curves and
    // the handle stems), the filter's own half width (in logical units), and a pixel of
    // slack - the same reach the quads themselves are built with.
    const float reach = aInk->halfWidth + (0.5f * stroke::kFilterPx + 1.0f) / scale;

    const float* coords = reinterpret_cast<const float*>(reinterpret_cast<const uchar*>(aInk) + sizeof(*aInk));
    float minX = FLT_MAX;
    float minY = FLT_MAX;
    float maxX = -FLT_MAX;
    float maxY = -FLT_MAX;
    for (int i = 0; i < aInk->pointCount; ++i) {
        minX = std::min(minX, coords[2 * i]);
        maxX = std::max(maxX, coords[2 * i]);
        minY = std::min(minY, coords[2 * i + 1]);
        maxY = std::max(maxY, coords[2 * i + 1]);
    }
    if (minX > maxX)
        return QRect();

    const ImVec2 offset = aDrawData->DisplayPos;
    const ImVec2 sc = aDrawData->FramebufferScale;
    const int left = int(std::floor((minX - reach - offset.x) * sc.x));
    const int right = int(std::ceil((maxX + reach - offset.x) * sc.x));
    const int top = int(std::floor((minY - reach - offset.y) * sc.y));
    const int bottom = int(std::ceil((maxY + reach - offset.y) * sc.y));
    return QRect(left, aFramebufferHeight - bottom, right - left, bottom - top);
}

// One polyline's place in the draw list: the geometry and the ink travel in the callback's
// own copied data, so this keeps no state of its own and the panel stays a pure function of
// its model. One callback PER POLYLINE is what keeps the pass in the draw list's own order -
// over the ticks and the labels, under the key markers (a curve), and between the markers
// and their own dots (a handle stem).
void addInkCallback(
    ImDrawList& aDrawList, const QVector<QPointF>& aPoints, const QColor& aColour, float aStrength,
    float aHalfWidth
) {
    if (aPoints.size() < 2)
        return;

    QVarLengthArray<uchar, 2048> blob(int(sizeof(CurveInkBlob) + aPoints.size() * 2 * sizeof(GLfloat)));
    CurveInkBlob header;
    header.pointCount = int(aPoints.size());
    header.red = GLfloat(aColour.redF());
    header.green = GLfloat(aColour.greenF());
    header.blue = GLfloat(aColour.blueF());
    header.alpha = aStrength;
    header.halfWidth = aHalfWidth;
    std::memcpy(blob.data(), &header, sizeof(header));
    GLfloat* coords = reinterpret_cast<GLfloat*>(blob.data() + sizeof(CurveInkBlob));
    for (int i = 0; i < aPoints.size(); ++i) {
        coords[2 * i] = GLfloat(aPoints.at(i).x());
        coords[2 * i + 1] = GLfloat(aPoints.at(i).y());
    }
    // ImGui COPIES this into the draw list, so nothing here has to outlive the call.
    aDrawList.AddCallback(stroke::drawListCallback, blob.data(), size_t(blob.size()));
}

} // namespace

namespace stroke {

bool init() {
    if (gStroke)
        return true;

    std::unique_ptr<StrokePass> pass(new StrokePass());
    const bool segmentsLinked = pass->segments.setVertexSource(QString::fromLatin1(kStrokeVertexSource))
        && pass->segments.setFragmentSource(QString::fromLatin1(kStrokeFragmentSource)) && pass->segments.link();
    const bool compositeLinked = pass->composite.setVertexSource(QString::fromLatin1(kStrokeCompositeVertexSource))
        && pass->composite.setFragmentSource(QString::fromLatin1(kStrokeCompositeFragmentSource))
        && pass->composite.link();
    if (!segmentsLinked || !compositeLinked) {
        // Not fatal: drawPanel strokes with ImDrawList::AddPolyline instead, i.e. exactly
        // the ink this pass was added to replace.
        XC_DEBUG_REPORT(
            "the coverage stroke pass did not build: %s", qPrintable(pass->segments.log() + pass->composite.log())
        );
        return false;
    }

    pass->vao.reset(new gl::VertexArrayObject());
    pass->orthoLocation = pass->segments.uniformLocation("uOrtho");
    pass->pxScaleLocation = pass->segments.uniformLocation("uPxScale");
    pass->halfWidthLocation = pass->segments.uniformLocation("uHalfWidth");
    pass->filterLocation = pass->segments.uniformLocation("uFilter");
    pass->inkLocation = pass->segments.uniformLocation("uInk");
    pass->alphaLocation = pass->segments.uniformLocation("uAlpha");
    pass->modeLocation = pass->segments.uniformLocation("uMode");
    pass->coverageLocation = pass->composite.uniformLocation("uCoverage");
    pass->sizeLocation = pass->composite.uniformLocation("uSize");
    pass->compositeInkLocation = pass->composite.uniformLocation("uInk");
    pass->compositeAlphaLocation = pass->composite.uniformLocation("uAlpha");

    // Released by shutdown(), never by exit(): see gStroke above.
    gStroke = pass.release();
    return true;
}

void shutdown() {
    delete gStroke;
    gStroke = nullptr;
}

bool isReady() { return gStroke != nullptr; }

void drawListCallback(const ImDrawList*, const ImDrawCmd* aCommand) {
    if (!gStroke || !aCommand || !aCommand->UserCallbackData)
        return;
    const CurveInkBlob* ink = static_cast<const CurveInkBlob*>(aCommand->UserCallbackData);
    // A callback is per curve and the data is the blob addCurveInkCallback built; the
    // size ImGui recorded beside it is what makes a corrupt or truncated blob (never one
    // of ours) a no-op rather than a wild read.
    if (ink->pointCount < 2 || aCommand->UserCallbackDataSize < int(sizeof(*ink)))
        return;

    // The frame's OWN projection, clip and scale: the draw data is what the backend
    // renders from, so a curve lands where the backend's own commands land.
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData)
        return; // driven outside a frame: there is nothing to project against
    const int fbWidth = int(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int fbHeight = int(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (fbWidth <= 0 || fbHeight <= 0)
        return;

    // Where this curve may draw: the panel's own clip (the plot), its own ink's bounds,
    // and only where the two meet.
    const QRect clip = scissorRect(aCommand->ClipRect, drawData->DisplayPos, drawData->FramebufferScale, fbHeight);
    const QRect area = clip.intersected(curveInkRect(ink, drawData, fbHeight));
    if (area.isEmpty())
        return;

    gl::Global::Functions& ggl = gl::Global::functions();
    StrokePass& pass = *gStroke;

    // The stroke's own numbers, both in DEVICE pixels: the filter is one of those by
    // definition, and the nominal width is a logical one that a scaled framebuffer
    // multiplies (so a 2x screen gets a 2x stroke and a one-device-pixel ramp).
    const float pxScale = drawData->FramebufferScale.x > 0.0f ? float(drawData->FramebufferScale.x) : 1.0f;
    const float halfWidth = ink->halfWidth * pxScale;
    const float reach = ink->halfWidth + (0.5f * kFilterPx + 1.0f) / pxScale;

    // The quads: one per segment, expanded on EVERY side to halfWidth + filter, so the
    // capsule's round cap and join are inside the geometry already. All four corners
    // carry the segment's own endpoints - that is what the fragment measures against.
    const float* coords = reinterpret_cast<const float*>(reinterpret_cast<const uchar*>(ink) + sizeof(*ink));
    std::vector<gl::Vector2>& positions = pass.positions;
    std::vector<gl::Vector2>& starts = pass.starts;
    std::vector<gl::Vector2>& ends = pass.ends;
    positions.clear();
    starts.clear();
    ends.clear();
    for (int i = 0; i + 1 < ink->pointCount; ++i) {
        const float ax = coords[2 * i];
        const float ay = coords[2 * i + 1];
        const float bx = coords[2 * i + 2];
        const float by = coords[2 * i + 3];
        const float dx = bx - ax;
        const float dy = by - ay;
        const float length = std::sqrt(dx * dx + dy * dy);
        if (!(length > 0.0f))
            continue; // a repeated sample: no direction, nothing to stroke
        const float ex = dx / length * reach;
        const float ey = dy / length * reach;
        const float fx = -dy / length * reach;
        const float fy = dx / length * reach;
        const gl::Vector2 corners[4] = {
            gl::Vector2::make(ax - ex + fx, ay - ey + fy),
            gl::Vector2::make(ax - ex - fx, ay - ey - fy),
            gl::Vector2::make(bx + ex - fx, by + ey - fy),
            gl::Vector2::make(bx + ex + fx, by + ey + fy),
        };
        // The corners alternate sides of the segment (c0/c3 on one, c1/c2 on the other), so
        // the two triangles have to share ONE diagonal: the pairing that shares the other
        // one (c2,c1,c3) crosses it instead, and the quad's middle is painted twice while a
        // wedge of it is painted not at all - measured as a hole in the ink, which is why
        // this is the order it is.
        const int order[6] = {0, 1, 2, 0, 2, 3};
        for (int v = 0; v < 6; ++v) {
            positions.push_back(corners[order[v]]);
            starts.push_back(gl::Vector2::make(ax, ay));
            ends.push_back(gl::Vector2::make(bx, by));
        }
    }
    if (positions.empty())
        return;

    GuestState guest;
    guest.save(ggl);

    // The IMGUI ORTHO, term for term (SetupRenderState): clip.x = x * sx + tx and
    // clip.y = y * sy + ty, with the matrix's own diagonal and translation.
    const float left = drawData->DisplayPos.x;
    const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float top = drawData->DisplayPos.y;
    const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
    const QVector4D ortho(
        GLfloat(2.0f / (right - left)), GLfloat(2.0f / (top - bottom)), GLfloat((right + left) / (left - right)),
        GLfloat((top + bottom) / (bottom - top))
    );

    // The scratch has to exist before anything is drawn into it; when the driver refuses
    // it (never measured, but cheap to survive) the ink goes straight into the frame.
    //
    // A SINGLE SEGMENT needs no scratch at all, and that is not a shortcut: with one segment
    // there is no second capsule to overlap, so the union IS that segment's capsule and the
    // direct path's source-over is bit-for-bit the same composite the accumulate path would
    // produce. It is worth saying because a two-point polyline is the common case: every
    // tangent handle stem, and every channel whose plot window holds one key pair.
    const bool accumulate = ink->pointCount > 2 && pass.ensureCoverage(QSize(fbWidth, fbHeight));

    // Dithering perturbs an 8-bit write by a bit, which is exactly the size of a ramp
    // step: the coverage pass writes real coverage, so it is turned off for the pass and
    // put back afterwards (the contrast outline pass does the same, for its own reason).
    ggl.glDisable(GL_DITHER);
    ggl.glDisable(GL_CULL_FACE);
    ggl.glDisable(GL_DEPTH_TEST);
    ggl.glEnable(GL_SCISSOR_TEST);
    ggl.glScissor(area.left(), area.top(), area.width(), area.height());
    ggl.glEnable(GL_BLEND);

    if (accumulate) {
        // 1. THE UNION OF THE CAPSULES, in the scratch. GL_MAX is the whole point: two
        // overlapping segments must contribute the LARGER coverage, not the sum of two
        // source-over blends (which would darken every join, and darken a dimmed curve's
        // join even where both segments are fully covered).
        pass.coverageFbo->bind();
        ggl.glViewport(0, 0, pass.pixels.width(), pass.pixels.height());
        ggl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        ggl.glClear(GL_COLOR_BUFFER_BIT);
        ggl.glBlendEquation(GL_MAX);
        ggl.glBlendFunc(GL_ONE, GL_ONE);

        pass.vao->bind();
        pass.segments.bind();
        pass.segments.setUniformValue(pass.orthoLocation, ortho);
        pass.segments.setUniformValue(pass.pxScaleLocation, pxScale);
        pass.segments.setUniformValue(pass.halfWidthLocation, GLfloat(halfWidth));
        pass.segments.setUniformValue(pass.filterLocation, GLfloat(kFilterPx));
        pass.segments.setUniformValue(pass.modeLocation, GLint(0));
        pass.segments.setAttributeArray(0, positions.data(), int(positions.size()));
        pass.segments.setAttributeArray(1, starts.data(), int(starts.size()));
        pass.segments.setAttributeArray(2, ends.data(), int(ends.size()));
        ggl.glDrawArrays(GL_TRIANGLES, 0, GLsizei(positions.size()));
        pass.segments.release();

        // 2. ONE composite of that union into the frame, with the ImGui backend's own
        // source-over, so the ink lands in the premultiplied convention the overlay's
        // framebuffer holds and the contrast outline pass reads back.
        ggl.glBindFramebuffer(GL_FRAMEBUFFER, GLuint(guest.drawFbo));
        ggl.glViewport(0, 0, fbWidth, fbHeight);
        ggl.glBlendEquation(GL_FUNC_ADD);
        ggl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        ggl.glActiveTexture(GL_TEXTURE0);
        ggl.glBindTexture(GL_TEXTURE_2D, pass.coverageTexture->id());

        pass.vao->bind();
        pass.composite.bind();
        pass.composite.setUniformValue(pass.coverageLocation, GLint(0));
        pass.composite.setUniformValue(pass.sizeLocation, QVector2D(GLfloat(fbWidth), GLfloat(fbHeight)));
        pass.composite.setUniformValue(
            pass.compositeInkLocation, QVector3D(ink->red, ink->green, ink->blue)
        );
        pass.composite.setUniformValue(pass.compositeAlphaLocation, ink->alpha);
        ggl.glDrawArrays(GL_TRIANGLES, 0, 3);
        pass.composite.release();
    } else {
        // No scratch: the ink goes straight into the frame at the coverage the fragment
        // measured. A join overlaps itself on this path - the one thing this pass exists
        // to avoid - but the curve is on screen, which is what matters here.
        ggl.glViewport(0, 0, fbWidth, fbHeight);
        ggl.glBlendEquation(GL_FUNC_ADD);
        ggl.glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

        pass.vao->bind();
        pass.segments.bind();
        pass.segments.setUniformValue(pass.orthoLocation, ortho);
        pass.segments.setUniformValue(pass.pxScaleLocation, pxScale);
        pass.segments.setUniformValue(pass.halfWidthLocation, GLfloat(halfWidth));
        pass.segments.setUniformValue(pass.filterLocation, GLfloat(kFilterPx));
        pass.segments.setUniformValue(pass.modeLocation, GLint(1));
        pass.segments.setUniformValue(pass.inkLocation, QVector3D(ink->red, ink->green, ink->blue));
        pass.segments.setUniformValue(pass.alphaLocation, ink->alpha);
        pass.segments.setAttributeArray(0, positions.data(), int(positions.size()));
        pass.segments.setAttributeArray(1, starts.data(), int(starts.size()));
        pass.segments.setAttributeArray(2, ends.data(), int(ends.size()));
        ggl.glDrawArrays(GL_TRIANGLES, 0, GLsizei(positions.size()));
        pass.segments.release();
    }

    guest.restore(ggl);
    GL_CHECK_ERROR();
}

} // namespace stroke

//-------------------------------------------------------------------------------------------------
// The one legend label geometry - declared to the gate as well (see the header),
// so a pixel gate can assert the no-room rule on the geometry the hit-test uses.
QRect legendLabelRect(const PanelLegendRow& aRow, ImFont* aFont, float aFontSize) {
    if (!aFont || aRow.label.isEmpty() || aRow.swatch.isEmpty())
        return QRect();

    const QByteArray utf8 = aRow.label.toUtf8();
    const ImVec2 size = measureText(aFont, aFontSize, utf8.constData());
    const int width = int(size.x + 0.5f);
    const int height = int(size.y + 0.5f);
    if (width <= 0 || height <= 0)
        return QRect();
    return QRect(aRow.swatch.right() + 1 + kLegendLabelGap, aRow.swatch.center().y() - height / 2, width, height);
}

//-------------------------------------------------------------------------------------------------
void drawPanel(
    ImDrawList& aDrawList, const GraphPanelModel& aModel, const theme::Graph& aTheme, const PanelFocus& aFocus
) {
    ImFont* font = drawListFont(aDrawList);
    const float fontSize = drawListFontSize(aDrawList);
    const ImU32 halo = toImU32Strength(aTheme.halo(), kHaloAlpha);
    const ImU32 axisText = toImU32(aTheme.axisText());
    const ImU32 tick = toImU32(aTheme.gridMajor());

    const QRect& plot = aModel.plot;

    if (plot.isEmpty()) {
        // No room for the plot. The model still states why, and the panel is the
        // whole viewport, so the reason goes in the middle of it.
        drawLabel(aDrawList, font, fontSize, aModel.panel, aModel.emptyState, LabelAlign::Centre, axisText, halo);
        return;
    }

    // Everything that belongs to the plot is clipped to it. This is the capability
    // the painter engine lacked (ctrl::GLCorePaintEngine has no clip), and it is
    // what keeps a curve that leaves the value window from stroking over the
    // value column or the legend.
    aDrawList.PushClipRect(rectMin(plot), rectMax(plot), true);

    // The axes: SHORT marks only, 1px with AA off (the timeline's hairline rule -
    // an AA'd 1px line straddles two columns and reads as 2px of grey). The model
    // publishes positions, never spans, so there is no grid to draw. The flags are
    // put back exactly as they were found, so a caller's own setting is not
    // clobbered.
    const ImDrawListFlags savedLineFlags = aDrawList.Flags;
    aDrawList.Flags = savedLineFlags & ~ImDrawListFlags_AntiAliasedLines;
    // ...down from the RULER BAND's top, not from the plot's: a widget floating above
    // the viewport moves the whole band (the marks and the numbers under them) below
    // itself, and the marks are its top edge (see THE OCCLUDED TOP in
    // GraphPanelModel.h). The band is inside the plot by construction.
    const float bandTop = float(aModel.rulerBandTop);
    for (int x : aModel.frameTickX) {
        const float fx = float(x);
        aDrawList.AddLine(ImVec2(fx, bandTop), ImVec2(fx, bandTop + float(kAxisTickLength)), tick, 1.0f);
    }
    for (int y : aModel.valueTickY) {
        const float fy = float(y);
        aDrawList.AddLine(
            ImVec2(float(plot.left()), fy), ImVec2(float(plot.left() + kAxisTickLength), fy), tick, 1.0f
        );
    }

    // The zero rule, only while 0 is inside the value window (the model publishes
    // zeroY exactly then).
    if (aModel.zeroY >= 0) {
        aDrawList.AddLine(
            ImVec2(float(plot.left()), float(aModel.zeroY)), ImVec2(float(plot.right() + 1), float(aModel.zeroY)),
            toImU32(aTheme.axisLine()), 1.0f
        );
    }
    aDrawList.Flags = savedLineFlags;

    // The frame numbers, centred in the boxes the model nudged inside the plot.
    for (const PanelLabel& label : aModel.frameLabels)
        drawLabel(aDrawList, font, fontSize, label.rect, label.text, LabelAlign::Centre, axisText, halo);

    // The curves. Clipped to the plot (above); the focused one at full colour,
    // every other one dimmed while a focus exists.
    //
    // THE SHIPPING STROKE IS gui::stroke's (see the header): one draw-list callback per
    // curve, carrying the polyline, the colour and the strength, so the ink is measured
    // PER FRAGMENT and its width stops depending on the angle - and so the pass sits in
    // the draw list's own order, over the ticks and under the key markers below. With the
    // pass unavailable (init() refused, or a caller that never built it) this is the
    // ImDrawList::AddPolyline path, which is exactly what the panel shipped before: a
    // panel drawn with the fringe stroke beats a panel with no curves.
    for (const PanelCurve& curve : aModel.curves) {
        if (curve.points.size() < 2)
            continue; // identity and colour but no geometry: the keys are outside the window

        const bool focused = aFocus.valid && aFocus.node == curve.node && aFocus.channel == curve.channel;
        const float strength = (aFocus.valid && !focused) ? kUnfocusedAlpha : 1.0f;

        if (stroke::isReady()) {
            addInkCallback(aDrawList, curve.points, curve.color, strength, kCurveWidth * 0.5f);
            continue;
        }

        // A stack buffer, so the common curve converts without touching the heap.
        QVarLengthArray<ImVec2, 256> points(int(curve.points.size()));
        for (int i = 0; i < curve.points.size(); ++i) {
            const QPointF& point = curve.points.at(i);
            points[i] = ImVec2(float(point.x()), float(point.y()));
        }
        aDrawList.AddPolyline(
            points.constData(), int(points.size()), toImU32Strength(curve.color, strength), ImDrawFlags_None,
            kCurveWidth
        );
    }

    // The key MARKERS, from the model's own centres (PanelCurve::keyDots): a DOT
    // of radius ctrl::graph::kKeyDotRadius - the timeline's own key-body radius
    // (ctrl::keyRadius, 3.75px) - filled with ITS CHANNEL'S OWN COLOUR
    // (PanelCurve::color, i.e. theme::Graph::keyColor(channel.type()) - the same
    // colour its curve is stroked in and its legend swatch filled with), inside the
    // palette's floor-coloured ring, which is what keeps a key visible on top of the
    // curve that carries it (a same-hue key on a same-hue stroke would otherwise
    // disappear into it). The editor's selection turns both into the selected token.
    //
    // THE MARKER IS NOT THE PICK AREA, and that is the timeline's own rule for a
    // small element ("hit testing and visuals are two separate things"): the editor
    // grabs a key within ctrl::graph::kKeyHitRadius (10px) of this centre, and the
    // model's `keys` rect is that rule's square form, so a 10px square used to be
    // drawn where a 7.5px dot belongs - 2.3x the timeline's key area. The ink here
    // is the marker; the hit stays where the editor's hit-test puts it.
    const float dotRadius = float(ctrl::graph::kKeyDotRadius);
    const ImU32 selectedKey = toImU32(aTheme.selected());
    for (const PanelCurve& curve : aModel.curves) {
        const bool focused = aFocus.valid && aFocus.node == curve.node && aFocus.channel == curve.channel;
        const float strength = (aFocus.valid && !focused) ? kUnfocusedAlpha : 1.0f;
        const ImU32 fill = toImU32Strength(curve.color, strength);
        const ImU32 edge = toImU32Strength(aTheme.keyEdge(), strength);
        for (const QPointF& dot : curve.keyDots) {
            const bool selected = isKeySelected(aFocus, curve.channel.type(), dot);
            const ImVec2 centre(float(dot.x()), float(dot.y()));
            aDrawList.AddCircleFilled(centre, dotRadius, selected ? selectedKey : fill);
            aDrawList.AddCircle(centre, dotRadius, selected ? selectedKey : edge, 0, 1.0f);
        }
    }

    // The Custom tangent handles, from the model's geometry: a stem from each
    // handle's base to its dot, then the dot itself. The dot NEAREST the pointer,
    // within ctrl::graph::kHandleHitRadius, is drawn in the selected token instead
    // - the same distance and the same `<=` the editor's hit-test grabs through, so
    // the highlight names exactly the handle a grab would take. The handles are
    // inside the plot clip (pushed above), so a stem that leaves the plot is cut at
    // the edge rather than painting over the panel's chrome.
    //
    // THEY ARE DRAWN FOR THE FOCUSED CHANNEL, and that is a COST decision with a
    // measured number behind it (the application-level performance diagnosis, on this
    // build and this machine): a key costs ~68 vertices with its tangent handles drawn
    // and ~16 without - the handles are the delta, 2 stems, 2 filled dots and 2
    // circles - at ~50ns of Debug-build CPU per vertex, which is 2.6ms/frame at 600
    // keys, 32ms at 8000 and 165ms at 40000, paid on every viewport repaint while the
    // panel is up. Drawing them for the focused channel alone measured 4.2x fewer
    // vertices and 4.4x less frame time in that diagnosis.
    //
    // WHAT THAT COSTS, stated rather than implied: a handle on a curve the user has
    // NOT focused is not on screen, so a pointer hovering it draws no highlight -
    // and a press there still GRABS it, because the editor's hit-test is its own
    // geometry and a handle press focuses that channel first (GraphEditor::
    // updateCursor), so the handle is drawn - and highlighted - from that frame on.
    // With NO focus at all every curve draws its handles, so nothing is hidden
    // before the user has said which channel they are working on (and the pixel
    // gate's own passes, which hand drawPanel an invalid focus, keep measuring the
    // full geometry).
    {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const auto drawsHandles = [&aFocus](const PanelCurve& aCurve) {
            return !aFocus.valid || (aFocus.node == aCurve.node && aFocus.channel == aCurve.channel);
        };
        int nearestCurve = -1;
        int nearestHandle = -1;
        double nearestDistance = double(ctrl::graph::kHandleHitRadius);
        for (int c = 0; c < aModel.curves.size(); ++c) {
            const PanelCurve& curve = aModel.curves.at(c);
            if (!drawsHandles(curve))
                continue;
            for (int h = 0; h < curve.handles.size(); ++h) {
                const QPointF& dot = curve.handles.at(h).dot;
                const double distance = std::hypot(double(mouse.x) - dot.x(), double(mouse.y) - dot.y());
                if (distance <= nearestDistance) { // `<=`, like the editor's own pick
                    nearestDistance = distance;
                    nearestCurve = c;
                    nearestHandle = h;
                }
            }
        }

        const ImU32 handleLine = toImU32(aTheme.handleLine());
        const ImU32 handleFill = toImU32(aTheme.handleFill());
        const ImU32 handleEdge = toImU32(aTheme.handleEdge());
        const ImU32 handleSelected = toImU32(aTheme.selected());
        for (int c = 0; c < aModel.curves.size(); ++c) {
            const PanelCurve& curve = aModel.curves.at(c);
            if (!drawsHandles(curve))
                continue;
            for (int h = 0; h < curve.handles.size(); ++h) {
                const PanelHandle& handle = curve.handles.at(h);
                const bool highlighted = c == nearestCurve && h == nearestHandle;
                const ImVec2 base(float(handle.base.x()), float(handle.base.y()));
                const ImVec2 dot(float(handle.dot.x()), float(handle.dot.y()));
                // THE STEM IS A 1px LINE WITH FRACTIONAL ENDS, and it goes through the same
                // coverage pass the curves do - measured first, because it is the one 1px
                // line in the panel whose position can be wrong: ImGui's own AddLine shifts
                // its geometry by (+0.5,+0.5) (imgui_draw.cpp, the 1px rasterization
                // convention), which for a stem at this slope moves the ink up to 0.71px
                // PERPENDICULAR to the line it is supposed to draw - measured on the gate's
                // own fractional stem at +0.68/+0.75/+0.76px along its length, against
                // -0.01/-0.00/+0.01px for this pass. The pass has
                // no such offset: its coverage is the distance to the segment the model
                // asked for. The stem is already antialiased today (the draw list's
                // AntiAliasedLines flag is not turned off here), so nothing about its
                // crispness changes; only where its ink sits.
                if (stroke::isReady()) {
                    addInkCallback(aDrawList, QVector<QPointF>{handle.base, handle.dot}, handleLine, 1.0f, 0.5f);
                } else {
                    aDrawList.AddLine(base, dot, handleLine, 1.0f);
                }
                aDrawList.AddCircleFilled(dot, float(handle.radius), highlighted ? handleSelected : handleFill);
                aDrawList.AddCircle(dot, float(handle.radius), highlighted ? handleSelected : handleEdge, 0, 1.0f);
            }
        }
    }

    // The live box-select marquee, when a drag is in flight. It is the timeline's
    // own gesture and is drawn with the timeline's own colours
    // (theme::Graph::marqueeEdge()/marqueeFill(), ctrl/time/time_Renderer.cpp's
    // renderSelectionRange): a rounded (6px) accent body at low alpha under a 1px
    // accent edge. ImGui's draw list has no dashed stroke, so the edge is solid -
    // the one thing about the timeline's box that cannot be reproduced here.
    // It is drawn INSIDE the plot clip (pushed above), and the playhead goes on top
    // of it, so a drag never paints over the axes or the value column.
    if (!aModel.marquee.isEmpty()) {
        const ImVec2 min = rectMin(aModel.marquee);
        const ImVec2 max = rectMax(aModel.marquee);
        aDrawList.AddRectFilled(min, max, toImU32(aTheme.marqueeFill()), kMarqueeRounding);
        aDrawList.AddRect(min, max, toImU32(aTheme.marqueeEdge()), kMarqueeRounding, ImDrawFlags_None, 1.0f);
    }

    // The playhead: the document's own frame, 1px, the same hairline rule as the
    // axes. It is the one mark on the panel that is not part of the curve data.
    //
    // It covers the plot's rows EXACTLY. The model's playheadX is a pixel column and
    // the panel's playhead spans plot.top()..plot.bottom(), but ImGui's non-AA 1px
    // path is half-open: it shifts the endpoints by half a pixel and loses the first
    // row, so a line passed as (top, bottom+1) inks rows top+1..bottom and leaves the
    // plot's first row empty. The half-pixel inset below is what makes the drawn
    // column cover the rows the model names - measured by the gate's playhead
    // assertion (tests/graph_panel_pixels.cpp), which walks every row of the span.
    if (aModel.playheadX >= 0) {
        const ImDrawListFlags savedPlayheadFlags = aDrawList.Flags;
        aDrawList.Flags = savedPlayheadFlags & ~ImDrawListFlags_AntiAliasedLines;
        const float px = float(aModel.playheadX);
        aDrawList.AddLine(
            ImVec2(px, float(plot.top()) - 0.5f), ImVec2(px, float(plot.bottom()) + 0.5f),
            toImU32(aTheme.selected()), 1.0f
        );
        aDrawList.Flags = savedPlayheadFlags;
    }

    // The legend: one swatch per row, at the swatch rect the model laid out, with
    // the row's own colour and its label beside it. A hidden row keeps both, dimmed.
    // A row with NO ROOM (an empty swatch: GraphPanelModel's LEGEND note) is skipped
    // whole - nothing is drawn for it, and nothing is derived from its empty rect.
    for (const PanelLegendRow& row : aModel.legend) {
        if (row.swatch.isEmpty())
            continue;

        const float strength = row.visible ? 1.0f : kHiddenAlpha;
        const ImVec2 min = rectMin(row.swatch);
        const ImVec2 max = rectMax(row.swatch);
        aDrawList.AddRectFilled(min, max, toImU32Strength(row.color, strength));
        aDrawList.AddRect(min, max, toImU32Strength(aTheme.keyEdge(), strength), 0.0f, ImDrawFlags_None, 1.0f);

        const QRect box = legendLabelRect(row, font, fontSize);
        drawLabel(
            aDrawList, font, fontSize, box, row.label, LabelAlign::Left, toImU32Strength(aTheme.axisText(), strength),
            halo
        );
    }

    // Why the panel is empty, in the middle of the plot: an empty panel has to say
    // so, and this is the only place it can.
    if (!aModel.emptyState.isEmpty())
        drawLabel(aDrawList, font, fontSize, plot, aModel.emptyState, LabelAlign::Centre, axisText, halo);

    aDrawList.PopClipRect();

    // The value numbers live in the column LEFT of the plot, so they are drawn after
    // the clip: the model already clamped each box into the column, and a label must
    // not be cut off by an edge it was deliberately kept clear of.
    //
    // TWO THINGS HAPPEN HERE, and they are different obligations:
    //   * the GUTTER (kValueLabelGap). A right-aligned number keeps that much space
    //     from the box's right edge whenever the box can spare it, so the top value
    //     label and the plot's first frame number do not read as one string - the
    //     measured defect, and the timeline's own kNumberMargin for it.
    //   * the GUARD. The whole pass is clipped to the PANEL, so a label wider than
    //     its box (the model's column is measured so that it cannot be, but a
    //     drawing font wider than the model's can make one) is cut at the panel's
    //     edge instead of painting outside the panel - the one place in this
    //     function whose ink is outside every other clip.
    aDrawList.PushClipRect(rectMin(aModel.panel), rectMax(aModel.panel), true);
    for (const PanelLabel& label : aModel.valueLabels)
        drawLabel(
            aDrawList, font, fontSize, label.rect, label.text, LabelAlign::Right, axisText, halo, kValueLabelGap
        );
    aDrawList.PopClipRect();
}

//-------------------------------------------------------------------------------------------------
struct ImGuiOverlay::State {
    // The widget the backend was initialized on, so teardown can make its context
    // current before deleting the backend's GL objects.
    QPointer<QOpenGLWidget> widget;
    // The Qt -> ImGuiIO translation. Destroyed before the context it feeds.
    QScopedPointer<ImGuiInputBridge> bridge;

    // The frame the pixels were drawn from, and the editor they were built from
    // (null while the panel is hidden). The widget's own hits - a legend swatch, a
    // legend label, a double-click on the plot - are tested against THIS geometry,
    // so the clickable control is exactly the drawn control.
    GraphPanelModel model;
    ctrl::GraphEditor* editor = nullptr;
    // The frame's font, kept so a press BETWEEN frames measures a legend label
    // exactly as the frame did. It belongs to the context's atlas and lives as
    // long as the context.
    ImFont* font = nullptr;
    float fontSize = 0.0f;

    // The contrast outline pass's own targets (see gui::outline): the backdrop copy
    // and the panel's coverage framebuffer. Built lazily on the frame that first
    // needs them, released in shutdown().
    OutlineTargets outline;

    // How much of the viewport's top a widget floating above it covers, in logical
    // pixels (setTopInset). Read when the frame builds its model.
    int topInset = 0;

    // io.DeltaTime, from the real interval between frames.
    QElapsedTimer frameTimer;
    bool frameTimerStarted = false;

    // The widget framebuffer's pixel size, CACHED. Asking for it is a chain of GL
    // queries (framebufferPixels: the bound framebuffer, the attachment's type and
    // name, then the renderbuffer's or the texture's size), measured at ~10 queries
    // and 0.10-0.13ms per frame - work whose answer changes only when the widget does.
    // The attachment is qRound(logical size * device pixel ratio) (see
    // framebufferPixels), so the two numbers below are the whole key: anything that can
    // change the answer changes one of them, and a new GL context drops the cache in
    // shutdown() (the widget's own framebuffer is recreated with it).
    QSize pixelsCache;
    QSize pixelsCacheLogical;
    double pixelsCacheDpr = 0.0;

    // The widget framebuffer's pixel size, from the cache when the widget has not
    // changed (see the fields above). aFramebuffer is only read when the cache is
    // stale, so a steady frame costs no GL query at all.
    QSize cachedFramebufferPixels(GLuint aFramebuffer, const QSize& aLogical, double aDpr) {
        if (pixelsCache.isEmpty() || pixelsCacheLogical != aLogical || pixelsCacheDpr != aDpr) {
            pixelsCache = framebufferPixels(aFramebuffer);
            pixelsCacheLogical = aLogical;
            pixelsCacheDpr = aDpr;
        }
        return pixelsCache;
    }
};

namespace {

// The frame delta ImGui animates with, clamped so a stall (a load, an attached
// debugger) cannot make an animation jump and a burst of repaints cannot make it
// crawl. The first frame has no interval to measure, so it gets 60Hz.
float nextDeltaTime(QElapsedTimer& aTimer, bool& aStarted) {
    if (!aStarted) {
        aTimer.start();
        aStarted = true;
        return 1.0f / 60.0f;
    }
    const qint64 nanoseconds = aTimer.nsecsElapsed();
    aTimer.restart();
    return float(qBound(qint64(1000000), nanoseconds, qint64(100000000))) / 1.0e9f;
}

// The editor's sticky focus plus its selection, in the currency drawPanel uses.
PanelFocus buildFocus(ctrl::GraphEditor& aEditor, const GraphPanelModel& aModel) {
    PanelFocus focus;

    if (aEditor.hasFocus() && aEditor.focusedNode()) {
        focus.node = const_cast<core::ObjectNode*>(aEditor.focusedNode());
        focus.channel = aEditor.focusedChannel();
        focus.valid = true;
    }

    core::TimeLineEvent selection;
    if (!aEditor.retrieveSelection(selection))
        return focus;

    // A selected key has to be found among the model's key MARKER centres
    // (PanelCurve::keyDots), which carry no frame. ctrl::graph::keyDot puts the
    // centre exactly on the mapped point, so the frame's pixel x is what identifies
    // it - and it is the editor's own scaler with the MODEL's panel, i.e. the same
    // mapping the model copied, so the two agree by construction rather than by
    // coincidence.
    ctrl::graph::Scaler scale = aEditor.scale();
    scale.setRect(aModel.panel);
    for (const core::TimeLineEvent::Target& target : selection.targets()) {
        if (target.pos.isNull())
            continue;
        PanelFocus::SelectedKey key;
        key.type = target.pos.type();
        key.x = scale.pixelX(core::Frame(target.pos.index()));
        focus.selectedKeys.append(key);
    }
    return focus;
}

} // namespace

//-------------------------------------------------------------------------------------------------
ImGuiOverlay::ImGuiOverlay(): mState(new State()), mReady(false) {}

ImGuiOverlay::~ImGuiOverlay() {
    // The accessible factory points at this object's callback: it goes first.
    a11y::uninstall();
    shutdown();
}

void ImGuiOverlay::shutdown() {
    if (!mState)
        return;

    // The bridge is ImGui's platform IME target and reads the context: drop it
    // before the context goes, and drop the frame state with it (the editor pointer
    // and the geometry are answers about a panel that no longer exists).
    mState->bridge.reset();
    // The framebuffer cache goes with the context: the widget's own framebuffer is
    // recreated when the context is.
    mState->pixelsCache = QSize();
    mState->pixelsCacheLogical = QSize();
    mState->pixelsCacheDpr = 0.0;
    mState->model = GraphPanelModel();
    mState->editor = nullptr;
    mState->font = nullptr;
    mState->fontSize = 0.0f;

    if (!mReady)
        return;

    // The backend's GL objects (shaders, buffers, textures) only go away on a
    // current context. Make the widget's current for the teardown while it is still
    // usable; a widget torn down after its context has gone leaves those few objects
    // to the driver rather than calling into a dead context.
    QOpenGLWidget* widget = mState->widget;
    const bool madeCurrent = widget && widget->context() && widget->context()->isValid();
    if (madeCurrent)
        widget->makeCurrent();
    // The outline pass's program, quad and two textures are context objects too, and so
    // are the curve stroke's programs and its coverage scratch.
    mState->outline.release();
    stroke::shutdown();
    outline::shutdown();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui::DestroyContext();
    if (madeCurrent)
        widget->doneCurrent();
    mReady = false;
}

bool ImGuiOverlay::init(QOpenGLWidget& aWidget) {
    if (!mState)
        return false;

    // initializeGL can run more than once, and it does so exactly when the widget
    // has a new GL context (a reparent, a screen change): every object the backend
    // created belongs to the OLD context, so the overlay is rebuilt from scratch
    // rather than kept.
    if (mReady)
        shutdown();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    // No runtime files, ever: no imgui.ini beside the application and no ImGui log.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    // The overlay never changes the cursor itself; the viewport's cursor policy
    // (MainDisplayWidget) stays the only thing that does.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // ImGui's style is left at the context's own default (ImGui::CreateContext
    // calls StyleColorsDark itself): the panel draws every colour it uses from the
    // palette into its own draw list, and the key menu is the application's QMenu,
    // so nothing this class draws reads ImGuiStyle at all.

    // The atlas through the Qt/QRawFont loader: the application font at the size Qt
    // RASTERIZES it at, plus the writing-system fallbacks Qt resolves for it, so CJK,
    // `.ttc` and variable faces rasterize with the face the rest of the application
    // draws with.
    //
    // THE SIZE IS panelFontPixelSize, NOT THE FONT'S LINE HEIGHT. The panel drew
    // every numeral ~30% larger than the timeline draws the same font, because
    // QFontMetricsF::height() is the LINE height (ascent + descent + leading:
    // 15.578125px for this application's Geist 9pt at 96dpi) while Qt rasterizes the
    // face at 12px - the size panelFontPixelSize derives, and the size the
    // timeline's own ruler numbers use. See gui/ImGuiQtFontLoader.h for the
    // measurement; 0 means the font stated no size, and the panel keeps its own
    // fallback rather than an atlas with no size at all.
    //
    // The loader's last argument is NOT the widget's device pixel ratio. It is
    // ImFontConfig::RasterizerDensity, and on the dynamic-atlas path (which the GL3
    // backend turns on through ImGuiBackendFlags_RendererHasTextures) ImGui
    // multiplies it by the bake's own density, taken from
    // io.DisplayFramebufferScale - i.e. from the widget's ratio again (imgui.cpp:4605
    // sets g.FontRasterizerDensity from the viewport's FramebufferScale, and
    // imgui_draw.cpp rasterizes at Size * baked->RasterizerDensity *
    // src->RasterizerDensity). Passing the ratio here would square it: 4x the linear
    // raster, and 16x the atlas area per glyph, for a 2x screen.
    const float statedSize = imgui_qt::panelFontPixelSize(aWidget.font(), aWidget.logicalDpiY());
    const float pixelSize = (statedSize > 0.0f) ? statedSize : kFallbackFontPixelSize;
    if (imgui_qt::installQtFontLoader(*io.Fonts, aWidget.font(), pixelSize, 1) == 0) {
        // No Qt-rasterized source: the bundled face could not be read, or Qt has no
        // face for any writing system. ImGui installs its own default font when the
        // first frame builds an empty atlas (imgui_draw.cpp, ImFontAtlasBuildMain),
        // so the panel still draws - in the wrong face for anything but Latin. A
        // panel drawn at all beats no panel, so this is not a failure.
    }

    if (!ImGui_ImplOpenGL3_Init(kGlslVersion)) {
        // Nothing to unwind beyond the context: the atlas and the loader state live
        // inside it.
        ImGui::DestroyContext();
        return false;
    }

    mState->widget = &aWidget;
    mState->bridge.reset(new ImGuiInputBridge(aWidget));
    mReady = true;

    // ImGui draws triangles into a GL framebuffer, so without this factory the panel
    // does not exist for assistive technology at all. The callback acts on the
    // current frame's editor, i.e. exactly what the pointer would act on.
    a11y::install(aWidget, [this](int aId) { onAccessibilityAction(aId); });
    return true;
}

void ImGuiOverlay::paint(
    QOpenGLWidget& aWidget, ctrl::GraphEditor* aEditor, core::Project*, bool aVisible
) {
    if (!mReady || !mState)
        return;
    State& state = *mState;

    const QSize viewport = aWidget.size();
    const double dpr = aWidget.devicePixelRatioF();
    ImGuiIO& io = ImGui::GetIO();
    // Logical (device independent) pixels, which is the space QMouseEvent::position()
    // and the model's rects are in; ImGui scales the projection by
    // ImDrawData::FramebufferScale itself.
    io.DisplaySize = ImVec2(float(viewport.width()), float(viewport.height()));
    io.DisplayFramebufferScale = ImVec2(float(dpr > 0.0 ? dpr : 1.0), float(dpr > 0.0 ? dpr : 1.0));
    io.DeltaTime = nextDeltaTime(state.frameTimer, state.frameTimerStarted);

    // The backend's own NewFrame is the CALLER's duty in 1.92: nothing in
    // ImGui::NewFrame() calls ImGuiPlatformIO::Renderer_NewFrame, and it is what
    // creates the backend's device objects (shader, VBO, EBO) on the first frame.
    // Skipping it leaves every handle 0 and the first RenderDrawData() draws with no
    // index buffer bound.
    ImGui_ImplOpenGL3_NewFrame();

    // THE PALETTE of THIS frame, which is what the panel is drawn from. It is
    // re-derived every frame rather than cached because the app changes palette at
    // runtime (a settings change, a different accent): theme::Colors::current() is
    // the live answer and the panel's colours are rebuilt from it below, so there is
    // nothing to invalidate and no signal to miss.
    const theme::Graph palette = theme::Graph::from(theme::Colors::current());

    ImGui::NewFrame();

    // The curve ink's own pass, built on the current context the first time this runs and
    // kept for the life of that context (idempotent, so the call per frame costs a null
    // check). It is called HERE because drawPanel is what decides which stroke to use, and
    // it refuses quietly: a driver that cannot compile the programs leaves the panel on its
    // AddPolyline path rather than with no curves at all (see gui::stroke).
    stroke::init();

    // The model of this frame, and nothing else: the panel's geometry never comes
    // from a rect computed here.
    GraphPanelModel model;
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const bool panelDrawn = aVisible && aEditor && drawList;
    if (panelDrawn) {
        model = buildGraphPanelModel(*aEditor, viewport, palette, state.topInset);
        drawPanel(*drawList, model, palette, buildFocus(*aEditor, model));
        state.font = drawListFont(*drawList);
        state.fontSize = drawListFontSize(*drawList);
    } else {
        // Hidden: no draw command at all, and no geometry left behind for a press
        // to hit.
        state.font = nullptr;
        state.fontSize = 0.0f;
    }

    // Published whether or not anything was drawn: the widget's own hits and the
    // accessibility layer both read THIS, so they can never disagree with the pixels.
    state.model = model;
    state.editor = (aVisible && aEditor) ? aEditor : nullptr;

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();

    // THE CONTRAST OUTLINE, THEN THE COMPOSITE (see gui::outline in the header). One
    // full-screen pass at the very end of the frame, and the two renders it needs:
    // the widget framebuffer becomes the BACKDROP - what the user would see without
    // the panel - and the panel is drawn into its own framebuffer, where alpha IS
    // the coverage and rgb the ink. The pass then rings the ink that would have been
    // lost against that backdrop and composites the panel over it.
    //
    // It is skipped ENTIRELY while the panel is hidden (an empty draw data has no
    // ink to ring and nothing to composite, which is why the hidden path is exactly
    // the cheap frame it always was) and when the driver refuses the pass or its
    // targets, in which case the panel is drawn straight into the widget
    // framebuffer, i.e. precisely the frame this pass was added to.
    const GLuint widgetFbo = GLuint(aWidget.defaultFramebufferObject());
    const QSize pixels = panelDrawn ? state.cachedFramebufferPixels(widgetFbo, viewport, dpr) : QSize();
    bool composited = false;
    if (!pixels.isEmpty() && outline::init() && state.outline.ensure(pixels)) {
        OutlineTargets& targets = state.outline;
        gl::Global::Functions& ggl = gl::Global::functions();

        // 1. the backdrop. A BLIT, never a readback: the widget's framebuffer is
        // 4x multisampled (MainWindow's QSurfaceFormat), and a blit resolves it.
        outline::copyBackdrop(widgetFbo, targets.backdropFbo->id(), pixels);

        // 2. the panel, into its own framebuffer.
        targets.overlayFbo->bind();
        // ImGui's own draw commands set a scissor box, so the clear has to be free
        // of one, and the viewport is the framebuffer's - both were last set by
        // something else.
        ggl.glDisable(GL_SCISSOR_TEST);
        ggl.glViewport(0, 0, pixels.width(), pixels.height());
        ggl.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        ggl.glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(drawData);

        // 3. ONE pass into the widget's own framebuffer: ring, then composite.
        ggl.glBindFramebuffer(GL_FRAMEBUFFER, widgetFbo);
        outline::Params outlineParams;
        outlineParams.colour = palette.outline();
        outline::composite(targets.overlayTexture->id(), targets.backdropTexture->id(), pixels, outlineParams);
        composited = true;
    }
    if (!composited)
        ImGui_ImplOpenGL3_RenderDrawData(drawData);

    // What the backend does NOT restore (measured: the viewport is left as the
    // backend set it, the scissor box is left set, and a temporary VAO is created
    // and deleted per draw). paintGL re-sets the viewport and the render state at
    // its top, so nothing here has to restore them - and the pass above leaves the
    // widget's own framebuffer bound, which is the state it was called in.

    // The QWidget side of the IME contract (see ImGuiInputBridge's class comment):
    // Qt asks the FOCUS OBJECT for input-method queries, and a plain QOpenGLWidget
    // is not an input item at all - so the attribute is set exactly while a Dear
    // ImGui text field wants text, and the widget takes focus so Qt asks it. With no
    // ImGui field on screen this is a no-op every frame.
    const bool wantsText = state.bridge && state.bridge->imeWantsTextInput();
    if (aWidget.testAttribute(Qt::WA_InputMethodEnabled) != wantsText)
        aWidget.setAttribute(Qt::WA_InputMethodEnabled, wantsText);
    if (wantsText && !aWidget.hasFocus())
        aWidget.setFocus(Qt::OtherFocusReason);

    publishAccessibility();
}

//-------------------------------------------------------------------------------------------------
bool ImGuiOverlay::mouseMove(const QMouseEvent& aEvent) {
    return mState && mState->bridge ? mState->bridge->handleMouseMove(aEvent) : false;
}

bool ImGuiOverlay::mouseButton(const QMouseEvent& aEvent) {
    const bool captured = mState && mState->bridge ? mState->bridge->handleMouseButton(aEvent) : false;

    // THE KEY MENU'S GESTURE, and the only place this class sees a right press: the
    // widget forwards EVERY button here (mousePressEvent's first step) and offers
    // press() the left one only, so a right-click on a key has to be taken here.
    // The menu itself is the application's own QMenu (see the class comment) and
    // QMenu::popup() returns immediately, so nothing here blocks the press.
    if (aEvent.type() == QEvent::MouseButtonPress && aEvent.button() == Qt::RightButton)
        openKeyMenuAt(aEvent.position().toPoint());
    return captured;
}

bool ImGuiOverlay::wheel(const QWheelEvent& aEvent) {
    return mState && mState->bridge ? mState->bridge->handleWheel(aEvent) : false;
}

bool ImGuiOverlay::key(const QKeyEvent& aEvent) {
    // The application's own key commands are untouched: with the menu a QMenu, this
    // overlay consumes no key of its own, and the bridge's answer is the whole
    // routing rule (see ImGuiInputBridge::keyboardCaptured).
    return mState && mState->bridge ? mState->bridge->handleKey(aEvent) : false;
}

bool ImGuiOverlay::inputMethod(const QInputMethodEvent& aEvent) {
    return mState && mState->bridge ? mState->bridge->handleInputMethod(aEvent) : false;
}

bool ImGuiOverlay::tablet(const QTabletEvent& aEvent) {
    return mState && mState->bridge ? mState->bridge->handleTablet(aEvent) : false;
}

bool ImGuiOverlay::touch(const QTouchEvent& aEvent) {
    return mState && mState->bridge ? mState->bridge->handleTouch(aEvent) : false;
}

void ImGuiOverlay::setTopInset(int aLogicalPixels) {
    if (!mState)
        return;

    const int inset = ImMax(0, aLogicalPixels);
    if (inset == mState->topInset)
        return; // the caller sets it every frame: a steady inset is not an event

    mState->topInset = inset;
    // A CHANGE is a frame the widget may not otherwise ask for: the call comes from
    // the widget's own paintEvent, but the widget above the viewport can appear,
    // disappear or change height without the viewport's pixels changing at all, and
    // without this the band would move only on the next unrelated repaint.
    requestRepaint();
}

int ImGuiOverlay::topInset() const {
    return mState ? mState->topInset : 0;
}

void ImGuiOverlay::focus(bool aFocused) {
    if (mState && mState->bridge)
        mState->bridge->handleFocus(aFocused);
}

void ImGuiOverlay::dprChanged(qreal aDpr) {
    if (mState && mState->bridge)
        mState->bridge->handleDprChanged(aDpr);
}

QVariant ImGuiOverlay::inputMethodQuery(Qt::InputMethodQuery aQuery) const {
    return (mState && mState->bridge) ? mState->bridge->inputMethodQuery(aQuery) : QVariant();
}

bool ImGuiOverlay::imeWantsTextInput() const {
    return mState && mState->bridge && mState->bridge->imeWantsTextInput();
}

//-------------------------------------------------------------------------------------------------
bool ImGuiOverlay::press(ctrl::GraphEditor& aEditor, const QPoint& aPos) {
    if (!mReady || !mState || !mState->editor)
        return false; // hidden: nothing is on screen to hit

    // The last frame's own geometry. A swatch takes precedence over a label, and
    // the label's box is the one legendLabelRect measured for the drawn glyphs. A row
    // with no room is skipped in both passes: it has no swatch on screen and - see
    // legendLabelRect - no label box either, so nothing of it is a control.
    for (int i = 0; i < mState->model.legend.size(); ++i) {
        const PanelLegendRow& row = mState->model.legend.at(i);
        if (!row.node || row.swatch.isEmpty() || !row.swatch.contains(aPos))
            continue;
        // The swatch is the channel's switch. Hiding one also drops it from the
        // model's curves on the next frame, and it keeps its (dimmed) row so it can
        // be switched back on.
        aEditor.setChannelVisible(*row.node, row.channel, !row.visible);
        return true;
    }

    for (int i = 0; i < mState->model.legend.size(); ++i) {
        const PanelLegendRow& row = mState->model.legend.at(i);
        if (!row.node || row.swatch.isEmpty())
            continue;
        if (legendLabelRect(row, mState->font, mState->fontSize).contains(aPos)) {
            // Focusing a channel: the one place the panel lets a click choose which
            // curve is emphasised (every other curve is dimmed while a focus exists).
            // It does NOT move the value window - focusing is not a fit; the fit
            // gestures are GraphEditor::fitAll (the plot's double-click) and the
            // editor's own axis fits.
            aEditor.setFocusedChannel(*row.node, row.channel);
            return true;
        }
    }
    return false;
}

bool ImGuiOverlay::doubleClick(ctrl::GraphEditor& aEditor, const QPoint& aPos) {
    if (!mReady || !mState || !mState->editor)
        return false;
    if (!mState->model.plot.contains(aPos))
        return false; // not the plot: the viewport's own double-click semantics apply

    // A double-click ON a legend control is that control's business, not the fit's:
    // the swatch toggles and the label focuses, and a second click must not turn
    // either into a zoom change. A row with no room has neither on screen.
    for (const PanelLegendRow& row : mState->model.legend) {
        if (row.swatch.isEmpty())
            continue;
        if (row.swatch.contains(aPos))
            return false;
        if (legendLabelRect(row, mState->font, mState->fontSize).contains(aPos))
            return false;
    }

    // Both axes to the shown data: the panel's own fit gesture.
    aEditor.fitAll();
    return true;
}

//-------------------------------------------------------------------------------------------------
bool ImGuiOverlay::openKeyMenuAt(const QPoint& aPos) {
    if (!mReady || !mState || !mState->editor)
        return false; // hidden: nothing is on screen to hit

    ctrl::GraphEditor& editor = *mState->editor;
    const GraphPanelModel& model = mState->model;

    // THE KEY UNDER THE POINTER, by the EDITOR'S OWN RULE: the nearest key whose
    // centre is within ctrl::graph::kKeyHitRadius - the same distance, and the same
    // `<=`, ctrl::GraphEditor::hitTest grabs a key with. The centre is the model's
    // PanelCurve::keyDots (ctrl::graph::keyDot through the model's own scaler), not
    // the key's HIT rect and not the drawn marker: the rect is that rule's square
    // form (so it would be both too small at the diagonals and unrelated to the ink),
    // and the marker is the ink, which is deliberately smaller than the target.
    //
    // A handle is not considered: the panel's right-click has no handle action, and
    // the editor's own handle grab is the LEFT press's, which still goes through
    // ctrl::GraphEditor. So a right-click near a handle that is also near a key opens
    // the key's menu - there is no second, quieter gesture for a handle to take.
    core::ObjectNode* node = nullptr;
    ctrl::graph::Channel channel;
    core::Frame frame;
    double nearest = double(ctrl::graph::kKeyHitRadius);
    for (const PanelCurve& curve : model.curves) {
        // keyFrames is published beside keyDots and has the same length (see
        // GraphPanelModel.h); a model that carries one without the other is not one
        // buildGraphPanelModel makes, and it is not one this class can name a key
        // from either, so it is skipped rather than guessed at.
        if (curve.keyFrames.size() != curve.keyDots.size())
            continue;
        for (int i = 0; i < curve.keyDots.size(); ++i) {
            const QPointF& dot = curve.keyDots.at(i);
            const double distance = std::hypot(double(aPos.x()) - dot.x(), double(aPos.y()) - dot.y());
            if (distance <= nearest) {
                nearest = distance;
                node = curve.node;
                channel = curve.channel;
                frame = curve.keyFrames.at(i);
            }
        }
    }
    if (!node)
        return false; // no key that close: this is not the panel's right-click

    // The right-click makes that key the panel's SELECTION, which is the timeline's
    // own behaviour with its key menu (selectKeysAt with the click point, then the
    // menu acts on the result): the menu acts on the selection, so the key it was
    // opened on has to be in it. Selecting is also what a press on a key does, so a
    // right-click here is not a second, quieter way to move the panel's current key.
    editor.selectKey(*node, channel, frame);

    // ...and the menu is the TIMELINE'S OWN, handed the selection and this widget's
    // position (see the class comment): one menu, one list of easings, one edit path
    // for both views.
    core::TimeLineEvent selection;
    if (!editor.retrieveSelection(selection) || selection.targets().isEmpty())
        return false;

    // The widget the overlay lives in owns the menu: it is the object that owns the
    // timeline, and it is the one that can map the panel's own pixels to a screen
    // position (MainDisplayWidget::showGraphKeyMenu). The cast is a static_cast and
    // not a qobject_cast ON PURPOSE: qobject_cast would need the widget class's
    // staticMetaObject at link time, and the pixel gate compiles this file with no
    // widget translation unit at all (tests/CMakeLists.txt: "there is no gui library
    // to link against"). MainDisplayWidget is this class's ONE construction site, so
    // the widget it holds is one whenever there is a widget at all; a gate that drives
    // this class owns none, and the selection above is then the whole of the gesture's
    // effect.
    if (mState->widget)
        static_cast<MainDisplayWidget*>(mState->widget.data())->showGraphKeyMenu(selection, aPos);

    // The selection just changed under the panel's ink (the key turns
    // theme::Graph::selected()), so this frame is already stale.
    requestRepaint();
    return true;
}

//-------------------------------------------------------------------------------------------------
bool ImGuiOverlay::selectedKeyDot(const core::TimeLineEvent& aTargets, QPoint& aDot) const {
    if (!mState || aTargets.targets().size() != 1)
        return false;

    // The triple the target carries IS a key's identity - (node, key type, frame) -
    // and PanelCurve publishes the other half of it (keyFrames, keyDots) beside each
    // other, so the marker is found rather than re-derived: a consumer that recomputed
    // the pixel would be a second copy of the scaler's arithmetic.
    const core::TimeLineEvent::Target& target = aTargets.targets().first();
    if (!target.node || target.pos.isNull())
        return false;
    for (const PanelCurve& curve : mState->model.curves) {
        if (curve.node != target.node || curve.channel.type() != target.pos.type())
            continue;
        for (int i = 0; i < curve.keyFrames.size() && i < curve.keyDots.size(); ++i) {
            if (curve.keyFrames.at(i).get() != target.pos.index())
                continue;
            aDot = QPoint(int(curve.keyDots.at(i).x()), int(curve.keyDots.at(i).y()));
            return true;
        }
    }
    return false;
}

//-------------------------------------------------------------------------------------------------
void ImGuiOverlay::publishAccessibility() {
    if (!mState)
        return;

    const GraphPanelModel& model = mState->model;
    ctrl::GraphEditor* editor = mState->editor;

    a11y::Model published;
    published.panel = model.panel;

    if (!editor) {
        // The panel is not on screen. The viewport stays a named pane with no
        // content, which is what it is.
        a11y::sync(published);
        return;
    }

    // One item per legend swatch: the same control the pointer toggles, with the
    // row's own label as its name and its state as the value.
    //
    // A row with NO ROOM is NOT published: its swatch is empty, so an item carrying
    // that rect would be a control at the panel's corner that is nowhere on screen
    // (and a rect of zero size is not something Qt can map to a screen position
    // either). Rows are only skipped for want of room, never for any other reason,
    // and the count of them is named in the summary below - a screen-reader user has
    // no other way to learn that the channel exists and is controllable.
    int roomlessRows = 0;
    for (int i = 0; i < model.legend.size(); ++i) {
        const PanelLegendRow& row = model.legend.at(i);
        if (row.swatch.isEmpty()) {
            ++roomlessRows;
            continue;
        }
        a11y::Item item;
        item.role = a11y::Item::CheckBox;
        item.id = i;
        item.name = row.label;
        item.value = row.visible ? trText(QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "shown"))
                                 : trText(QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "hidden"));
        item.rect = row.swatch;
        item.checked = row.visible;
        item.enabled = row.node != nullptr;
        published.items.append(item);
    }

    // The panel in one line: its own reason while it is empty (the model states it
    // in words), otherwise how much is shown - plus the focused channel, which is
    // the one piece of the panel's state with no control of its own.
    QString summary;
    if (!model.emptyState.isEmpty())
        summary = model.emptyState;
    else
        summary = trText(QT_TRANSLATE_N_NOOP("gui::ImGuiOverlay", "%n channel(s) shown"), int(model.curves.size()));
    if (editor->hasFocus() && editor->focusedNode()) {
        const QString label =
            editor->focusedNode()->name() + QStringLiteral(" : ") + editor->focusedChannel().name();
        summary = QCoreApplication::translate(kTranslationContext, QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "%1, focused: %2")).arg(summary, label);
    }

    // THE SELECTED KEY, when the graph's selection names one: which key it is and
    // which easing it carries. The key menu acts on that selection, and the menu is
    // a QMenu - it is not in this tree, like the repository's own QMenus (see the
    // class comment) - so this line is how the state the menu changes stays
    // readable: choose an easing from it and the panel says so. The easing name is
    // the timeline's own (util::Easing::getTypeName, the "EasingName" context), so
    // the two agree in every translation.
    //
    // AND THE MENU'S OWN ACTION IS PUBLISHED from the same selection, one item
    // below: a menu a screen reader cannot address still needs a route to OPEN it.
    core::TimeLineEvent selection;
    QPoint selectedDot;
    const bool oneSelectedKey =
        editor->retrieveSelection(selection) && selectedKeyDot(selection, selectedDot);
    if (oneSelectedKey) {
        const core::TimeLineEvent::Target& target = selection.targets().first();
        const core::TimeLine* line = target.pos.line();
        const core::TimeKey* key = line ? line->timeKey(target.pos.type(), target.pos.index()) : nullptr;
        const util::Easing::Param* easing =
            key ? ctrl::graph::Curve::easingOf(*key, target.pos.type()) : nullptr;
        if (easing) {
            const QString keyLabel = core::TimeLine::getTimeKeyName(target.pos.type()) + QStringLiteral(" ")
                + util::Easing::getTypeName(easing->type);
            summary = QCoreApplication::translate(
                          kTranslationContext, QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "%1, selected key: %2")
            )
                          .arg(summary, keyLabel);
        }
    }

    if (roomlessRows > 0) {
        // The channels the viewport could not lay out, stated rather than dropped in
        // silence: they keep their curves and they are NOT listed above. %n is the
        // count, so the pair is the plural form.
        summary = QCoreApplication::translate(
                      kTranslationContext,
                      QT_TRANSLATE_N_NOOP(
                          "gui::ImGuiOverlay", "%1 (%n more channel(s) not listed: no room in the panel)"
                      ),
                      nullptr, roomlessRows
        )
                      .arg(summary);
    }

    a11y::Item summaryItem;
    summaryItem.role = a11y::Item::StaticText;
    summaryItem.id = kSummaryItemId;
    summaryItem.name = summary;
    summaryItem.rect = model.plot.isEmpty() ? model.panel : model.plot;
    published.items.append(summaryItem);

    // The fit action. Its rectangle is the plot because the gesture that performs it
    // is the double-click on the plot: an assistive tool activates the action
    // directly (the interfaces call back into onAccessibilityAction) rather than
    // clicking, and a click where this item says it is lands on the plot, i.e. on
    // the same region the gesture is bound to.
    a11y::Item fitItem;
    fitItem.role = a11y::Item::PushButton;
    fitItem.id = kFitItemId;
    fitItem.name = trText(QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "Fit graph"));
    fitItem.value = trText(QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "double-click the plot"));
    fitItem.rect = model.plot.isEmpty() ? model.panel : model.plot;
    published.items.append(fitItem);

    // The key menu's action, while the selection names one key the panel draws. Its
    // rectangle is the region the POINTER's gesture works in - the key's centre
    // grown by ctrl::graph::kKeyHitRadius, the distance the right-click is tested
    // against - so a click where this item says it is does what this item does, the
    // same rule the fit action follows. Nothing is published when no key is
    // selected, or when the selected key is not in the drawn model (outside the
    // plot's window, or on a hidden channel): an item whose action had no key to
    // open a menu on would be a control that does nothing.
    if (oneSelectedKey) {
        a11y::Item menuItem;
        menuItem.role = a11y::Item::PushButton;
        menuItem.id = kKeyMenuItemId;
        menuItem.name = trText(QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "Key menu"));
        menuItem.value = trText(QT_TRANSLATE_NOOP("gui::ImGuiOverlay", "right-click the key"));
        menuItem.rect = QRect(selectedDot, selectedDot).adjusted(
            -ctrl::graph::kKeyHitRadius, -ctrl::graph::kKeyHitRadius, ctrl::graph::kKeyHitRadius,
            ctrl::graph::kKeyHitRadius
        );
        published.items.append(menuItem);
    }

    a11y::sync(published);
}

void ImGuiOverlay::onAccessibilityAction(int aId) {
    if (!mState || !mState->editor)
        return;

    ctrl::GraphEditor& editor = *mState->editor;
    if (aId == kFitItemId) {
        editor.fitAll();
        requestRepaint();
        return;
    }

    // The key menu: THE SAME ROUTE THE POINTER TAKES, at the same widget position -
    // the menu is the application's own and it is anchored where the key is drawn,
    // so an assistive activation puts it exactly where a right-click on that key
    // would (MainDisplayWidget::showGraphKeyMenu). The selection is the one
    // publishAccessibility published the item from, re-read here because a frame may
    // have passed since.
    if (aId == kKeyMenuItemId) {
        core::TimeLineEvent selection;
        QPoint dot;
        if (!editor.retrieveSelection(selection) || !selectedKeyDot(selection, dot))
            return; // the selection moved on: nothing to open the menu on
        if (mState->widget)
            static_cast<MainDisplayWidget*>(mState->widget.data())->showGraphKeyMenu(selection, dot);
        return;
    }

    // A legend swatch: the very action a pointer press on it performs.
    if (aId >= 0 && aId < mState->model.legend.size()) {
        const PanelLegendRow& row = mState->model.legend.at(aId);
        if (row.node)
            editor.setChannelVisible(*row.node, row.channel, !row.visible);
        requestRepaint();
    }
}

void ImGuiOverlay::requestRepaint() {
    if (mState && mState->widget)
        mState->widget->update();
}

} // namespace gui
