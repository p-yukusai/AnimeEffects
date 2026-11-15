// The timeline's PIXEL CONTRACT for the row/selection/label ink (design doc
// dev/graph-timeline-redesign, sections 1 and 3).
//
// WHAT IS DRIVEN. Not a widget and not the application: a real
// ctrl::TimeLineEditor with manually pushed rows, rendered through its own
// public ctrl::TimeLineEditor::render() into an offscreen QImage (no GL, no
// window, no paint event). That is the shipping paint path - renderLines
// (lane fills, separators, lane labels, seams), renderRangeMarkers and
// renderKeys - so the pixels asserted here are the timeline's own.
//
// WHAT IT PINS, and why each assertion is a regression gate:
//   1 THE ROW TINT. A selected object's lane band is the accent at 0.06
//     ("a whisper, not a flood"), NOT the old @selection@ flood (accent at
//     0.40). Both composites are computed from the live palette - the answer
//     comes from theme::TimeLine::trackSelectColor(), which is the brush the
//     renderer fills the band with - and the band must be the new tint and
//     must NOT be the old fill.
//     The rail AND the tree's own row background belong to the object tree
//     (ObjectTreeWidget), not to this pass: the row fill is painted by
//     QTreeView::drawRow through the QSS item:selected rule the tree
//     overrides, and neither that rule nor the widget's private paint is
//     reachable from this harness, which drives ctrl::TimeLineEditor::render()
//     into a QImage and deliberately does not link the gui library (it stubs
//     the two ObjectTreeWidget symbols it needs). What IS asserted here is the
//     value the tree's rule carries: the tint base (accentBright) and the
//     flood token (selection, the accent at 0.40) are distinct, the pair is
//     told apart by the same comparison the band uses, and the band's own
//     brush - the tint the tree restates - is pinned in section 1.
//   2 THE KEY FILL AND THE RING. An unfocused key is drawn in its own channel
//     hue; a focused one brightens that fill toward the keyframe highlight
//     (the mock's mixWhite at 0.45 - white, the value of --key-highlight) and
//     gains a 1px ring clear of the marker. A key belongs to the TYPE, so its
//     channel hue is the type's FIRST component through THE CHANNEL HUE TABLE
//     (theme::Colors): the X axis colour for Move/Scale/Image/Blur, the type's
//     own hue for a channel with no axis. The axis rule itself is asserted here
//     too: two components of one type differ, and a no-axis type keeps its hue.
//     No size change: the ring does not touch the marker's centre pixel, which
//     is only the fill. The ring is asserted as a pair: its arc pixels exist
//     around the focused key and the identical window around the unfocused key
//     is clean band. The states are told apart by their centres: the mock's mix
//     replaces the old .lighter(150) fill outright.
//     THE RING'S OWN TOKEN IS PINNED TOO: its ink is the channel's raw hue at
//     ctrl::time::kSelectRingAlpha (217/255) on the 1px band centred at
//     keyRadius + ctrl::time::kSelectRingOffset, read off the pixel profile
//     around the marker - the same two cells the graph panel's marker pass reads
//     (tests/graph_panel_pixels.cpp measures the same ink at the same offset), so
//     one selection cannot be inked two ways in the two views.
//   3 THE LABEL INK. A lane's name is drawn in its channel's hue at 0.42, one
//     step quieter than the keys: the label's ink is closer to the hue's 0.42
//     composite than to the old neutral text composite, and it is still nearer
//     its own band than the full-strength hue (the "quieter" half).
//   4 THE LABEL DOES NOT YIELD. The playhead's pill carries the frame in its
//     column, and the RULER blanks its number under it because the pill
//     carries that same number there. Nothing carries a lane's name, so the
//     lane label is drawn whatever the playhead does: with the playhead parked
//     on the label's own centre the box carries the same label ink as with
//     the playhead away, and the playhead line simply crosses the text.
//   5 THE FIT IS SYMMETRIC. ctrl::TimeLineEditor::fitFrameRange (the header's
//     fit button and the new-project fit) leaves the SAME content-pixel margin
//     at both ends of the frame range, within the one pixel the whole-pixel
//     view transform can express, and its gutter is part of the RUNG choice.
//     The new-project policy (fits on attach, not on a tab switch) is asserted
//     through the same call.
//   7 THE PROPORTIONAL KEY IS A RING. A proportional key is drawn in its channel's
//     hue like any other key, but its body is hollow: the hue is a band at
//     ctrl::time::kProportionalRingOuterInset / kProportionalHoleInset and the centre
//     shows the lane. The absolute key beside it is a solid hue disc, and a
//     FOCUSED proportional key still gains the full selection ring: the hole, the
//     band and the selection ring are three separate rules.
//   8 THE FOLDER SUMMARY IS A LANE. A collapsed folder with hidden keys owns a
//     dedicated summary band (below its own lanes, or the whole row when it has
//     none), with one rounded triangle per frame; the band is a distinct recessed
//     fill, the hit test and the marquee address that band's centre, and an
//     expanded folder has no summary at all.
//
// The geometry is read off the implementation's own public mapping
// (TimeLineEditor::timeScale(), kTimeLineMargin) rather than restated, so a
// change to the scale cannot silently move a probe.
//
// CMake wiring (tests/CMakeLists.txt, another unit's file - NOT added here):
// this target needs the ctrl library and - like the other timeline tests - the
// real ../src/gui/theme/TimeLine.cpp, because ctrl's renderer names that class
// and the ctrl library leaves its symbols unresolved. Add:
//   add_executable(timeline_visuals timeline_visuals.cpp ../src/gui/theme/TimeLine.cpp)
//   target_include_directories(timeline_visuals PRIVATE .. ../src)
//   target_compile_definitions(timeline_visuals PRIVATE
//       AE_MAJOR_VERSION=1 AE_MINOR_VERSION=7 AE_MICRO_VERSION=1 USE_GL_CORE_PROFILE)
//   target_link_libraries(timeline_visuals PRIVATE
//       Qt6::Core Qt6::Core5Compat Qt6::Widgets Qt6::Gui Qt6::OpenGL
//       Qt6::OpenGLWidgets Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
//       cmnd core ctrl gl img thr util pugixml)
// Headless: QApplication for the app's own internals (QSettings, palettes), no
// GL context and no .anie file.
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "XCAssert.h"
#include "core/BoneKey.h"
#include "core/MoveKey.h"
#include "core/ObjectNode.h"
#include "core/OpaKey.h"
#include "core/TimeLine.h"
#include "ctrl/TimeLineEditor.h"
// The selected key's ring tokens (alpha/offset) - the same cells the graph panel's
// marker pass reads, which is what this file and graph_panel_pixels.cpp pin together.
#include "ctrl/time/time_Renderer.h"
#include "ctrl/time/time_BodyModel.h"
#include "ctrl/time/time_Marquee.h"
#include "cmnd/Stack.h"
#include "gui/PillInk.h"
#include "gui/theme/Colors.h"
#include "gui/theme/TimeLine.h"

// Project.cpp (in the core library) references the GUI's AudioPlaybackWidget
// only to load a .aemus sidecar, which this test never has. Stub it so the link
// succeeds - same as rightclick_transient_selection and bonekey_regression.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

// ctrl::TimeLineEditor's clipboard-paste helpers reference two GUI symbols that
// only matter on the paste path this test never exercises. Stub them (same
// pattern) instead of linking the whole gui library.
#include "gui/ObjectTreeWidget.h"
bool gui::obj::Item::isTopNode() const { return false; }
std::pair<core::TimeKey*, core::LayerMesh*> gui::ObjectTreeWidget::getAreaMeshImpl(core::ObjectNode&, int) {
    return { nullptr, nullptr };
}

namespace {

class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                             \
            return 1;                                                                                  \
        }                                                                                              \
    } while (false)

// A pixel as plain components: the assertions are stated in the palette's own
// numbers, so nothing has to round-trip through QColor's colour spaces.
struct Rgb {
    int r = 0;
    int g = 0;
    int b = 0;
};

Rgb rgbOf(const QColor& aColor) { return {aColor.red(), aColor.green(), aColor.blue()}; }

Rgb pixelAt(const QImage& aImage, int aX, int aY) { return rgbOf(aImage.pixelColor(aX, aY)); }

// The distance the assertions care about: the largest single-channel gap. A hue
// tint and a neutral ink of the same strength are told apart by this on a
// neutral band, and a composite that differs in one channel only still counts.
int chebyshev(const Rgb& a, const Rgb& b) {
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

// Source-over of a translucent source on an opaque destination: what a lane
// band does over the floor, and how the old and the new fill tokens are
// compared with the pixel that is actually painted.
Rgb over(const QColor& aSrc, double aAlpha, const Rgb& aDst) {
    auto channel = [&](int aS, int aD) {
        return static_cast<int>(std::lround(aAlpha * aS + (1.0 - aAlpha) * aD));
    };
    return {channel(aSrc.red(), aDst.r), channel(aSrc.green(), aDst.g), channel(aSrc.blue(), aDst.b)};
}

// The inverse of over(): how much of aInk the pixel carries over aBand, projected
// on the channel with the widest spread. A 1px stroke sampled inside its band is a
// PARTIAL-COVERAGE blend, so "is this pixel the token" is a question about that
// line rather than about one full-coverage sample; the projection recovers the
// combined fraction (the token's alpha times the AA coverage) whatever it is.
double inkFraction(const Rgb& aPixel, const Rgb& aInk, const Rgb& aBand) {
    const int ink[3] = {aInk.r, aInk.g, aInk.b};
    const int band[3] = {aBand.r, aBand.g, aBand.b};
    const int pixel[3] = {aPixel.r, aPixel.g, aPixel.b};
    double num = 0.0;
    double den = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double e = double(ink[i] - band[i]);
        num += double(pixel[i] - band[i]) * e;
        den += e * e;
    }
    return den > 0.0 ? num / den : 0.0;
}

// The focused key's fill (the user's ruling for both views): the channel hue
// mixed toward the theme's TEXT colour by 0.45 - the app's readable ink in
// either palette, in place of the mock's --key-highlight white. The renderer
// performs the same mix with the same token and amount (time_Renderer.cpp,
// mixTowardText), so the expectation is the rule rather than a white stand-in.
constexpr double kSelectFillMix = 0.45;

Rgb towardText(const Rgb& aColor, double aT) {
    const QColor text = theme::Colors::current().text;
    auto mix = [&](int aChannel, int aTarget) {
        return static_cast<int>(std::lround(aChannel + (aTarget - aChannel) * aT));
    };
    return {mix(aColor.r, text.red()), mix(aColor.g, text.green()), mix(aColor.b, text.blue())};
}

// The canvas: lane bands span the whole visible width, so the probes need a
// wide, short surface. The width is what puts the centred lane label
// (cameraRect.center().x() - 50) far from the keys.
constexpr int kWidth = 800;
constexpr int kHeight = 200;
// The row bands: 22px-aligned lanes below the ruler strip, like the rows a real
// view pushes from the tree.
constexpr int kRowATop = 22;
constexpr int kRowABottom = 82;
constexpr int kRowBTop = 82;
constexpr int kRowBBottom = 142;
// The frames the keys live on: far from the frame range's ends, so the range
// marker hairlines (drawn at the first/last frame) never touch a probe.
constexpr int kKeyFrameA = 100;
constexpr int kKeyFrameFocused = 120;
// A probe column inside every band and clear of every key, of the label boxes
// and of the range markers (the label box starts around x=349, the markers sit
// at x=24 and x=244).
constexpr int kBandProbeX = 300;
// The most ink a band may carry over itself: Qt's 8-bit compositing rounds by
// at most a unit per channel, so this is "nothing is painted here".
constexpr int kClean = 2;

QImage renderTimeline(const ctrl::TimeLineEditor& aEditor, theme::TimeLine& aTheme) {
    // The floor the widget's own background paints (the QSS @base@): the lane
    // fills composite onto it, so the probes compare against the same dst.
    QImage image(kWidth, kHeight, QImage::Format_RGB32);
    image.fill(theme::Colors::current().base);

    core::CameraInfo camera;
    camera.setScreenSize(QSize(kWidth, kHeight));

    QPainter painter(&image);
    const QPoint origin = camera.leftTopPos().toPoint();
    painter.translate(origin);
    aEditor.render(painter, camera, aTheme, QRect(QPoint(0, 0), QSize(kWidth, kHeight)).translated(-origin));
    painter.end();
    return image;
}

// The label box: the renderer's own placement (100px wide, centred on the
// camera rect, the row's whole height for a one-type row), inset a few pixels
// so the row seams are never sampled.
QRect labelBox(int aRowTop, int aRowBottom) {
    const int center = QRect(0, 0, kWidth, kHeight).center().x();
    return QRect(QPoint(center - 50 + 3, aRowTop + 4), QPoint(center + 50 - 3, aRowBottom - 4));
}

// The pixel of aBox that carries the most ink against the band: the glyph's
// core, and the only sample a partial-coverage probe can state a rule about.
Rgb strongestInk(const QImage& aImage, const QRect& aBox, const Rgb& aBand) {
    Rgb found = aBand;
    int best = -1;
    for (int y = aBox.top(); y <= aBox.bottom(); ++y) {
        for (int x = aBox.left(); x <= aBox.right(); ++x) {
            const Rgb p = pixelAt(aImage, x, y);
            const int ink = chebyshev(p, aBand);
            if (ink > best) {
                best = ink;
                found = p;
            }
        }
    }
    return found;
}

// The most ink over the band in a horizontal window at the marker's own y: the
// row a key's ring arc crosses. A window with no ring is band, i.e. no ink.
int maxInkIn(const QImage& aImage, int aX0, int aX1, int aY, const Rgb& aBand) {
    int best = 0;
    for (int x = aX0; x <= aX1; ++x) {
        best = std::max(best, chebyshev(pixelAt(aImage, x, aY), aBand));
    }
    return best;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace core {
// Minimal ObjectNode subclass owning a real TimeLine (ObjectNode::timeLine()
// is virtual and null by default). No meshes, no resources -- just key maps.
class TestNode: public ObjectNode {
    TimeLine mTimeLine;

public:
    ObjectType type() const override { return ObjectType_Layer; }
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
    bool serialize(Serializer&) const override { return true; }
    bool deserialize(Deserializer&) override { return true; }
    TimeLine* timeLine() override { return &mTimeLine; }
    const TimeLine* timeLine() const override { return &mTimeLine; }
};

// A folder row with one child layer: the smallest shape a folder summary needs.
class TestFolder: public TestNode {
    TestNode mChild;

public:
    TestFolder() { children().pushBack(&mChild); }
    ObjectType type() const override { return ObjectType_Folder; }
    bool canHoldChild() const override { return true; }
    TestNode& child() { return mChild; }
};
} // namespace core

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    theme::Colors::activate(true, theme::kDefaultAccent);
    theme::TimeLine theme;

    const theme::Colors& colors = theme::Colors::current();
    const Rgb floor = rgbOf(colors.base);

    core::TestNode nodeA; // the selected row: Move keys, one of them focused
    core::TestNode nodeB; // an unselected row: one Opa key (a strongly hued label)
    cmnd::Stack stack;

    auto* keyA = new core::MoveKey();
    auto* keyFocused = new core::MoveKey();
    stack.push(nodeA.timeLine()->createPusher(core::TimeKeyType_Move, kKeyFrameA, keyA));
    stack.push(nodeA.timeLine()->createPusher(core::TimeKeyType_Move, kKeyFrameFocused, keyFocused));
    stack.push(nodeB.timeLine()->createPusher(core::TimeKeyType_Opa, kKeyFrameA, new core::OpaKey()));

    // The focus the renderer reads is the key's own life link (the same live
    // link the editor's Selection holds); it must outlive the renders below.
    util::LifeLink focusLink;
    keyFocused->setFocus(focusLink);
    CHECK(keyFocused->isFocused());
    CHECK(!keyA->isFocused());

    ctrl::TimeLineEditor editor;
    // The frame range is placed so its ends (and so the range markers) are far
    // from every probe column.
    editor.setFrameRange(util::Range(0, 200));
    editor.pushRow(&nodeA, util::Range(kRowATop, kRowABottom), false);
    editor.pushRow(&nodeB, util::Range(kRowBTop, kRowBBottom), false);
    editor.updateRowSelection(&nodeA);

    const ctrl::time::Scaler& scale = editor.timeScale();
    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;
    const int keyAx = margin + scale.pixelWidth(kKeyFrameA);
    const int keyFocusedX = margin + scale.pixelWidth(kKeyFrameFocused);
    // Both lanes hold one key type, so the key row is the lane's own middle row
    // (TimeLineRow::keyHeight for a single valid type).
    const int keyRowAY = kRowATop + (kRowABottom - kRowATop + 1) / 2;
    const QRect labelA = labelBox(kRowATop, kRowABottom);
    const QRect labelB = labelBox(kRowBTop, kRowBBottom);

    // The probes only mean anything if they are where they claim to be: the
    // bands are wide, the label boxes are far to the right of every key, and
    // the key columns are distinct.
    CHECK(labelA.left() > kBandProbeX);
    CHECK(labelB.left() > kBandProbeX);
    CHECK(keyAx != keyFocusedX);
    CHECK(keyFocusedX + 9 < labelA.left());

    // --- 1: the selected row's band is the 0.06 accent, and the old 0.40 fill
    // is gone.
    {
        const QImage image = renderTimeline(editor, theme);

        // The band's brush is the token the renderer reads, so its value is
        // part of the contract: accentBright at 0.06.
        CHECK(theme.trackSelectColor().name() == colors.accentBright.name());
        CHECK(theme.trackSelectColor().alphaF() > 0.05 && theme.trackSelectColor().alphaF() < 0.07);

        const Rgb selected = pixelAt(image, kBandProbeX, kRowATop + 38);
        const Rgb unselected = pixelAt(image, kBandProbeX, kRowBTop + 30);

        const Rgb tint = over(colors.accentBright, 0.06, floor);
        const Rgb oldFill = over(colors.selection, 0.40, floor);
        CHECK(chebyshev(tint, oldFill) > 20); // the two states are far apart

        CHECK(chebyshev(selected, tint) <= 2);    // the whisper
        CHECK(chebyshev(selected, oldFill) > 20); // not the flood
        CHECK(chebyshev(unselected, floor) <= 2); // untouched rows keep the floor

        // The object tree's selected ROW carries this same tint (see the
        // harness note at the top: the row fill itself is QTreeView's QSS
        // paint and is out of reach here). The facts this harness CAN state
        // about the rule the tree appends: its fill is built from the band's
        // token - checked above - and the token the app-wide rule paints
        // instead, the selection flood, is a different colour at a different
        // alpha, so the override changes what a selected row shows.
        CHECK(colors.selection.name() != colors.accentBright.name());
        CHECK(colors.selection.alphaF() > 0.39 && colors.selection.alphaF() < 0.41); // the 0.40 flood, not 0.06
    }

    // --- 2: the focused key's fill brightens toward the highlight, the
    // unfocused key keeps its type colour, and the focused key gains the ring.
    {
        const QImage image = renderTimeline(editor, theme);
        // The channel hue table (design section 7) on the TIMELINE: a Move key
        // belongs to the type, so it takes the type's first component's hue -
        // the X axis colour - not the old per-type indigo moveKey.
        const Rgb moveKey = rgbOf(theme::channelColor(colors, core::TimeKeyType_Move, 0));
        CHECK(chebyshev(moveKey, rgbOf(colors.axisX)) == 0);
        CHECK(chebyshev(moveKey, rgbOf(colors.moveKey)) > 0); // the old type hue is gone from this key
        const Rgb brightened = towardText(moveKey, kSelectFillMix);

        // The axis rule itself, read off the same table both views read: the two
        // components of one type DIFFER (X vs Y), and a channel with no axis keeps
        // its type's hue.
        const Rgb moveX = rgbOf(theme::channelColor(colors, core::TimeKeyType_Move, 0));
        const Rgb moveY = rgbOf(theme::channelColor(colors, core::TimeKeyType_Move, 1));
        CHECK(chebyshev(moveX, moveY) > 0);
        CHECK(chebyshev(moveY, rgbOf(colors.axisY)) == 0);
        CHECK(chebyshev(rgbOf(theme::channelColor(colors, core::TimeKeyType_Scale, 0)), moveX) == 0);
        CHECK(chebyshev(rgbOf(theme::channelColor(colors, core::TimeKeyType_Opa, 0)), rgbOf(colors.opaKey)) == 0);
        CHECK(chebyshev(rgbOf(theme::channelColor(colors, core::TimeKeyType_Rotate, 0)), rgbOf(colors.rotateKey)) == 0);
        // The band the ring is drawn on (row A is the selected row).
        const Rgb bandA = pixelAt(image, kBandProbeX, keyRowAY);

        // The focused marker's centre is the brightened composite: the channel
        // hue mixed toward the keyframe highlight at the mock's own 0.45.
        // Equality to that mix is what pins the mix TARGET and its step - the
        // first wave's "the centre is the plain hue" is exactly what this
        // replaces.
        CHECK(chebyshev(pixelAt(image, keyFocusedX, keyRowAY), brightened) <= 2);
        CHECK(chebyshev(pixelAt(image, keyFocusedX, keyRowAY), moveKey) > 20); // the two states are far apart
        // No size change at the centre: the ring sits keyRadius+2.6 out and
        // never covers the marker's own fill.
        // The unfocused marker is the plain channel hue, un-brightened.
        CHECK(chebyshev(pixelAt(image, keyAx, keyRowAY), moveKey) <= 1);
        CHECK(chebyshev(pixelAt(image, keyAx, keyRowAY), brightened) > 20);

        // The ring's arc crosses the marker's y a few pixels out (the window
        // starts past the marker's own edge): an accent arc carries a large
        // channel deviation from the band.
        const int ringInk = std::max(
            maxInkIn(image, keyFocusedX + 5, keyFocusedX + 9, keyRowAY, bandA),
            maxInkIn(image, keyFocusedX - 9, keyFocusedX - 5, keyRowAY, bandA)
        );
        CHECK(ringInk > 40);

        // ...AND THE RING IS THE SHARED TOKEN, at the shared offset: the key's own
        // channel hue at ctrl::time::kSelectRingAlpha (217/255), on the 1px band
        // centred at keyRadius + ctrl::time::kSelectRingOffset. These two cells live
        // in time_Renderer.h because the GRAPH panel's marker pass reads the same
        // ones - tests/graph_panel_pixels.cpp measures the same ink at the same
        // offset there - so one selection cannot be inked two ways.
        //
        // THE GEOMETRY IS READ AS A PROFILE. The renderer centres the key on a half
        // pixel, so the pixel column +6 samples 6.0px from the centre and the column
        // +7 samples 7.0px; the band [radius + offset - 0.5, + 0.5] = [5.85, 6.85]
        // therefore puts the BODY on +6 (0.65 of the pixel) and its fringe on +7
        // (0.35), while +5 (5.0px) is still the clear gap past the marker. A ring at
        // any other offset moves that maximum and fails the profile comparison.
        const QColor ringToken = theme::channelColor(colors, core::TimeKeyType_Move, 0);
        const double inkAlpha = double(ctrl::time::kSelectRingAlpha) / 255.0;
        const double offset = ctrl::time::kSelectRingOffset;
        CHECK(std::abs(offset - 2.6) < 1e-9); // the token the graph reads is the same 2.6
        const Rgb fullRing = over(ringToken, inkAlpha, bandA);
        for (const int sign : { 1, -1 }) {
            const Rgb body = pixelAt(image, keyFocusedX + sign * 6, keyRowAY);
            const double fraction = inkFraction(body, fullRing, bandA);
            CHECK(fraction > 0.3 && fraction <= 1.0);
            // The sample is exactly the token at that partial coverage - not another
            // ink, and not the fill's mixed highlight. (The tolerance covers Qt's own
            // rounding of the blend, which the projection cannot see.)
            CHECK(chebyshev(body, over(ringToken, inkAlpha * fraction, bandA)) <= 3);
            CHECK(chebyshev(fullRing, bandA) > 20); // the token is distinguishable from the band
            // The profile: the body is stronger than the fringe outside it and than
            // the gap inside it.
            const int bodyInk = chebyshev(body, bandA);
            const int fringeInk = chebyshev(pixelAt(image, keyFocusedX + sign * 7, keyRowAY), bandA);
            const int gapInk = chebyshev(pixelAt(image, keyFocusedX + sign * 5, keyRowAY), bandA);
            CHECK(bodyInk > 0);
            CHECK(fringeInk < bodyInk);
            CHECK(gapInk < bodyInk);
        }

        // The control: the identical window around the UNFOCUSED key (its
        // marker's edge is at +3.75, the window starts at +5) is bare band.
        const int plainInk = std::max(
            maxInkIn(image, keyAx + 5, keyAx + 9, keyRowAY, bandA),
            maxInkIn(image, keyAx - 9, keyAx - 5, keyRowAY, bandA)
        );
        CHECK(plainInk <= kClean);
    }

    // --- 3: the lane label's ink is the channel's hue at 0.42.
    {
        const QImage image = renderTimeline(editor, theme);
        // Row B is unselected, so its band is the floor: a clean dst for an ink
        // measurement. Its single type is Opa, whose hue is strongly non-neutral.
        const Rgb bandB = pixelAt(image, kBandProbeX, kRowBTop + 30);
        CHECK(chebyshev(bandB, floor) <= kClean);

        // Display names are not serialization tokens: the lane says "Opacity",
        // while the project format and clipboard keep "Opa".
        CHECK(core::TimeLine::getTimeKeyDisplayName(core::TimeKeyType_Opa) == QStringLiteral("Opacity"));
        CHECK(core::TimeLine::getTimeKeyName(core::TimeKeyType_Opa) == QStringLiteral("Opa"));

        const Rgb ink = strongestInk(image, labelB, bandB);
        CHECK(chebyshev(ink, bandB) > 15); // there is ink at all

        const Rgb hueTint = over(colors.opaKey, 0.42, bandB);
        const Rgb neutralInk = over(colors.text, 180.0 / 255.0, bandB);

        // The ink carries the channel's HUE: the old neutral text ink was a
        // grey and had no channel spread at all.
        CHECK(std::max({ink.r, ink.g, ink.b}) - std::min({ink.r, ink.g, ink.b}) > 15);
        // It is the hue at 0.42 rather than a neutral ink of the same strength:
        // much closer to the tint than to the old text composite.
        CHECK(chebyshev(ink, hueTint) + 20 < chebyshev(ink, neutralInk));
        // And it is quiet - still nearer its own band than the full-strength
        // hue, which no full-brightness ink is.
        CHECK(chebyshev(ink, bandB) < chebyshev(ink, rgbOf(colors.opaKey)));
    }

    // --- 4: the lane label does NOT yield its column to the playhead.
    {
        // The pill's centre is margin + pixelWidth(frame) - the same mapping the
        // keys and the ruler use - so the label can be put exactly under it: the
        // label is centred on the camera rect.
        const int labelCenterX = QRect(0, 0, kWidth, kHeight).center().x();
        int playFrame = 0;
        while (margin + scale.pixelWidth(playFrame + 1) <= labelCenterX)
            ++playFrame;
        const int playX = margin + scale.pixelWidth(playFrame);
        CHECK(std::abs(playX - labelCenterX) <= 1); // the playhead really is on the label

        // Every state is rendered through the same paint path; only the frame
        // differs, so the playhead's own column is the only thing that moves.
        const QImage awayFromPill = renderTimeline(editor, theme);
        editor.setFrame(core::Frame(playFrame));
        const QImage underPill = renderTimeline(editor, theme);

        // The label box is bare band except for the label itself (the keys and
        // the range markers are columns away), so ink over the band is the
        // lane name. The playhead's own column is skipped - were a playhead
        // line painted into this pass, it must not be mistaken for the label.
        auto labelInk = [&](const QImage& aImage, const QRect& aBox, const Rgb& aBand) {
            int ink = 0;
            for (int y = aBox.top(); y <= aBox.bottom(); ++y) {
                for (int x = aBox.left(); x <= aBox.right(); ++x) {
                    if (std::abs(x - playX) <= 2)
                        continue;
                    if (chebyshev(pixelAt(aImage, x, y), aBand) > 15)
                        ++ink;
                }
            }
            return ink;
        };

        const Rgb bandB = pixelAt(underPill, kBandProbeX, kRowBTop + 30);
        CHECK(chebyshev(bandB, pixelAt(awayFromPill, kBandProbeX, kRowBTop + 30)) <= kClean);
        const int awayInk = labelInk(awayFromPill, labelB, bandB);
        const int underInk = labelInk(underPill, labelB, bandB);
        // A lane's name is not the ruler's number: the pill carries the frame,
        // nothing carries the lane's name, so the label stays put and stays
        // readable in BOTH states.
        CHECK(awayInk > 15);
        CHECK(underInk > 15);
        CHECK(std::abs(underInk - awayInk) <= 4); // the playhead does not add or remove label ink

        // Row A is the selected row, so its band is the tint: compare against
        // its own band, not row B's. Its label obeys the same rule.
        const Rgb bandAwayRowA = pixelAt(awayFromPill, kBandProbeX, keyRowAY);
        const Rgb bandUnderPillRowA = pixelAt(underPill, kBandProbeX, keyRowAY);
        CHECK(labelInk(awayFromPill, labelA, bandAwayRowA) > 15);
        CHECK(std::abs(
            labelInk(underPill, labelA, bandUnderPillRowA) - labelInk(awayFromPill, labelA, bandAwayRowA)
        ) <= 4);

        // The control that makes this a statement about the LABEL and not
        // about an unchanged picture: the same ink is the hue at 0.42 over the
        // band, read outside the playhead's column in the parked state too.
        int best = -1;
        Rgb strongest = bandB;
        for (int y = labelB.top(); y <= labelB.bottom(); ++y) {
            for (int x = labelB.left(); x <= labelB.right(); ++x) {
                if (std::abs(x - playX) <= 2)
                    continue;
                const Rgb p = pixelAt(underPill, x, y);
                const int ink = chebyshev(p, bandB);
                if (ink > best) {
                    best = ink;
                    strongest = p;
                }
            }
        }
        const Rgb hueTint = over(colors.opaKey, 0.42, bandB);
        CHECK(chebyshev(strongest, hueTint) <= 25);
    }

    // --- 5: THE FIT LEAVES THE SAME MARGIN AT BOTH ENDS, AND A NEW PROJECT OPENS
    // FITTED. This is the timeline header's fit button and the new-project fit in one
    // implementation: ctrl::TimeLineEditor::fitFrameRange picks the rung and the
    // view's left content pixel, and gui::TimeLineWidget puts that pixel under the
    // viewport's left edge (TimeLineWidget::fitFrameRange). The viewport is simulated
    // here at the content pixel grid the view transform really uses, so the two
    // margins are measured the way the widget produces them.
    //
    // The regression this pins: fitting to the range's span alone (without the two
    // margins in the rate) and then pushing the range flush to the left edge left
    // ZERO pixels before frame min and the whole ladder remainder after frame max.
    {
        const int fitMargin = ctrl::TimeLineEditor::kTimeLineMargin;
        // Widths the timeline really gets: its minimum size hint is 280, and the
        // docked/undocked panes go well past 1920.
        const int widths[] = { 280, 320, 517, 640, 800, 1000, 1234, 1920 };
        // Ranges whose spans land between ladder rungs (the interesting case: the
        // rung barely fits, so the leftover is large), a range not starting at 0,
        // and one entirely LEFT of frame 0 - the camera's overlap bound caps that
        // one's rung, and the fit must still come out symmetric.
        const util::Range ranges[] = {
            util::Range(0, 200),
            util::Range(1, 240),
            util::Range(-10, 30),
            util::Range(24, 31),
            util::Range(-2000, -1000),
        };
        for (const util::Range& range : ranges) {
            editor.setFrameRange(range);
            const double span = std::max(1.0, static_cast<double>(range.diff()));
            for (const int viewport : widths) {
                const ctrl::TimeLineEditor::FrameFit fit = editor.fitFrameRange(viewport);
                CHECK(fit.valid);
                // Apply the fit the way the widget does: the ladder moves by whole
                // notches (the wheel's unit), and the view's left edge lands on the
                // fit's content pixel.
                editor.updateWheel((editor.timeScale().index() - fit.scaleIndex) * 120, false);
                CHECK(editor.timeScale().index() == fit.scaleIndex);

                const double ppf = editor.pixelsPerFrame();
                const double leftTick = fitMargin + ppf * static_cast<double>(range.min());
                const double rightTick = fitMargin + ppf * static_cast<double>(range.max());
                const double leftMargin = leftTick - fit.viewLeftPixel;
                const double rightMargin = (fit.viewLeftPixel + viewport) - rightTick;

                // The range is wholly inside the viewport...
                CHECK(rightTick - leftTick <= viewport + 1e-9);
                // ...with a real margin on each side...
                CHECK(leftMargin > 0.0);
                CHECK(rightMargin > 0.0);
                // ...and THE SAME margin at both ends, to the one content pixel the
                // view transform can express. (2 * half a pixel of rounding.)
                CHECK(std::fabs(leftMargin - rightMargin) <= 1.0);
                // The gutter was part of the RUNG choice: for a range centred on or
                // right of the content origin, the next rung up overflows the
                // two-gutter budget the fit reserves, which is exactly the asymmetry
                // the old span-only rate could produce. A negative-centred range is
                // capped earlier by the camera's bound instead, so its next rung may
                // still fit the gutters and simply sit outside the bound.
                const double centre = 0.5 * (range.min() + range.max());
                if (centre >= 0.0 && fit.scaleIndex < 120) {
                    ctrl::time::Scaler next = editor.timeScale();
                    next.setIndex(fit.scaleIndex + 1);
                    CHECK(next.pixelsPerFrame() * span + 2.0 * fitMargin > viewport + 1e-9);
                }
                // ...and the view position the fit asks for is one updateCamera's own
                // overlap bound accepts (it clamps the view offset into
                // [-contentSpan, viewport], so a fitted lead would otherwise be eaten
                // by the camera rather than by the fit).
                const int contentSpan = editor.frameEndPixel() + fitMargin;
                CHECK(fit.viewLeftPixel >= -viewport);
                CHECK(fit.viewLeftPixel <= contentSpan);
                std::printf(
                    "[F] fit %d..%d into %4dpx: index %d (%.3f px/frame), margins %.2f / %.2f px\n",
                    range.min(), range.max(), viewport, fit.scaleIndex, ppf, leftMargin, rightMargin
                );
            }
        }

        // THE NEW-PROJECT POLICY. setProject fits a project that arrives from NO
        // project (newly opened / newly created, both of which clear the timeline
        // first); a tab switch hands over a project this widget is already showing,
        // so the live view is kept.
        CHECK(ctrl::TimeLineEditor::fitsOnProjectAttach(false, true));
        CHECK(!ctrl::TimeLineEditor::fitsOnProjectAttach(true, true));
        CHECK(!ctrl::TimeLineEditor::fitsOnProjectAttach(true, false));
        CHECK(!ctrl::TimeLineEditor::fitsOnProjectAttach(false, false));

        // A NEW PROJECT REALLY OPENS FITTED, not at the default zoom: after the frame
        // range arrives, the default rung is what setFrameRange leaves behind, and
        // the fit is what moves it - it zooms IN so the range fills the pane instead
        // of occupying a fraction of it.
        {
            ctrl::TimeLineEditor fresh;
            fresh.setFrameRange(util::Range(0, 200));
            const int defaultIndex = fresh.timeScale().index();
            const double defaultPpf = fresh.timeScale().pixelsPerFrame();
            const ctrl::TimeLineEditor::FrameFit fit = fresh.fitFrameRange(800);
            CHECK(fit.scaleIndex != defaultIndex);
            CHECK(fit.scaleIndex > defaultIndex); // fitted means zoomed in here
            // The default leaves the range in a fraction of the pane...
            CHECK(defaultPpf * 200.0 < 800.0 - 2.0 * fitMargin);
            // ...and the fit's rung fills it, gutters included.
            ctrl::time::Scaler probe = fresh.timeScale();
            probe.setIndex(fit.scaleIndex);
            CHECK(probe.pixelsPerFrame() * 200.0 + 2.0 * fitMargin <= 800.0 + 1e-9);
        }
    }

    // --- 6: THE PILL'S ROUNDED CORNER IS ANTIALIASED, IN BOTH TIMELINE PILLS.
    //
    // The playhead's frame pill (gui::TimeCursor) and the temporary playback
    // window's label arms (gui::TimeRuler::renderPlayWindow) both end on a
    // kPillRadius corner. They used to paint it differently: the window arms set
    // QPainter::Antialiasing, the playhead did NOT (and a QWidget's painter starts
    // with the hint off), so the playhead's corner was a hard staircase. Both now
    // go through gui::paintPill, the one rounded-pill rule, and this is the pixel
    // statement of it:
    //
    //   * an antialiased RADIUS leaves pixels in the corner box that are neither
    //     the pill's ink nor the surface under it - the curve's partial coverage;
    //   * a HARD corner (the same box filled as a rectangle) leaves none.
    //
    // The contrast case is drawn here too, so the metric itself is shown to
    // distinguish the two and a regression to a bare drawRoundedRect/fillRect in
    // either pill fails on the first count.
    {
        const QRect pill(6, 5, 30, 14);
        const QColor pillInk = colors.accentBright;
        const QColor surface = colors.base;
        const int radius = ctrl::TimeLineEditor::kPillRadius;

        QImage rounded(kWidth, 40, QImage::Format_RGB32);
        rounded.fill(surface);
        {
            QPainter painter(&rounded);
            gui::paintPill(painter, pill, pillInk, radius);
        }

        QImage hard(kWidth, 40, QImage::Format_RGB32);
        hard.fill(surface);
        {
            QPainter painter(&hard);
            painter.fillRect(pill, pillInk);
        }

        const auto blendPixels = [&](const QImage& aImage) {
            int blends = 0;
            const QRect corner(pill.left(), pill.top(), radius, radius);
            for (int y = corner.top(); y <= corner.bottom(); ++y) {
                for (int x = corner.left(); x <= corner.right(); ++x) {
                    const Rgb c = pixelAt(aImage, x, y);
                    if (chebyshev(c, rgbOf(pillInk)) > 2 && chebyshev(c, rgbOf(surface)) > 2)
                        ++blends;
                }
            }
            return blends;
        };
        const int roundedBlends = blendPixels(rounded);
        const int hardBlends = blendPixels(hard);
        std::printf(
            "[6] pill corner: radius %d leaves %d blend pixel(s) of %d; a hard corner leaves %d\n", radius,
            roundedBlends, radius * radius, hardBlends
        );
        CHECK(hardBlends == 0);   // the control: a hard corner has no partial coverage
        CHECK(roundedBlends > 0); // the rule draws an antialiased radius
    }

    // --- 7: A PROPORTIONAL KEY IS A RING, NOT A SOFTER FILL.
    //
    // The key is drawn in its channel hue at the same radius and with the same edge
    // as an absolute one, but its body is HOLLOW: the hue is a 2px band inside the
    // edge and the centre shows the lane. The two keys sit in one unselected lane, so
    // the hole is a clean read of the band beneath it. The focused pass then proves
    // the selection ring is untouched: a proportional key that is focused keeps the
    // full-strength ring at kSelectRingOffset, in the same raw hue.
    {
        core::TestNode nodeP;
        cmnd::Stack stackP;
        auto* absoluteKey = new core::MoveKey();
        auto* proportionalKey = new core::MoveKey();
        stackP.push(nodeP.timeLine()->createPusher(core::TimeKeyType_Move, kKeyFrameA, absoluteKey));
        stackP.push(nodeP.timeLine()->createPusher(core::TimeKeyType_Move, kKeyFrameFocused, proportionalKey));
        // The document's own flag: the renderer reads nothing else to decide the ring.
        proportionalKey->setProportional(true);
        CHECK(proportionalKey->isProportional());
        CHECK(!absoluteKey->isProportional());

        ctrl::TimeLineEditor proportionEditor;
        proportionEditor.setFrameRange(util::Range(0, 200));
        proportionEditor.pushRow(&nodeP, util::Range(kRowATop, kRowABottom), false);

        const ctrl::time::Scaler& proportionScale = proportionEditor.timeScale();
        const int absoluteX = margin + proportionScale.pixelWidth(kKeyFrameA);
        const int proportionalX = margin + proportionScale.pixelWidth(kKeyFrameFocused);
        const int keyRowPY = kRowATop + (kRowABottom - kRowATop + 1) / 2;

        const QImage unfocused = renderTimeline(proportionEditor, theme);
        const Rgb band = pixelAt(unfocused, kBandProbeX, keyRowPY);
        CHECK(chebyshev(band, floor) <= kClean);
        CHECK(absoluteX != proportionalX);
        CHECK(proportionalX + 9 < labelBox(kRowATop, kRowABottom).left());

        const QColor hue = theme::channelColor(colors, core::TimeKeyType_Move, 0);
        const Rgb plainHue = rgbOf(hue);

        // The absolute key: the full channel hue, solid.
        CHECK(chebyshev(pixelAt(unfocused, absoluteX, keyRowPY), plainHue) <= 1);
        // The proportional key: the centre is the LANE (the hole), and the hue is a
        // ring around it. The ring is sampled at 2px from the centre, inside the
        // 2px band the model publishes (inset 1.5, width 2 -> [1.25, 3.25]).
        const Rgb proportionalHole = pixelAt(unfocused, proportionalX, keyRowPY);
        CHECK(chebyshev(proportionalHole, band) <= 2);
        CHECK(chebyshev(proportionalHole, plainHue) > 20);
        CHECK(chebyshev(pixelAt(unfocused, proportionalX + 2, keyRowPY), plainHue) <= 3);
        CHECK(chebyshev(pixelAt(unfocused, proportionalX - 2, keyRowPY), plainHue) <= 3);
        // Neither carries a ring while nothing is focused.
        CHECK(maxInkIn(unfocused, absoluteX + 5, absoluteX + 9, keyRowPY, band) <= kClean);
        CHECK(maxInkIn(unfocused, proportionalX + 5, proportionalX + 9, keyRowPY, band) <= kClean);

        // THE FOCUSED PROPORTIONAL KEY. Focus changes the RING's ink (the channel hue
        // mixed toward the text colour, the same mix section 2 asserts); the hole and
        // the selection ring keep their own rules.
        util::LifeLink proportionFocus;
        proportionalKey->setFocus(proportionFocus);
        CHECK(proportionalKey->isFocused());
        const QImage focused = renderTimeline(proportionEditor, theme);
        const Rgb mixedRing = towardText(plainHue, kSelectFillMix);
        CHECK(chebyshev(pixelAt(focused, proportionalX, keyRowPY), band) <= 2);
        CHECK(chebyshev(pixelAt(focused, proportionalX + 2, keyRowPY), mixedRing) <= 3);
        // The ring's token and offset, read exactly as section 2 reads them.
        const double ringAlpha = double(ctrl::time::kSelectRingAlpha) / 255.0;
        const Rgb fullRing = over(hue, ringAlpha, band);
        const int ringInk = std::max(
            maxInkIn(focused, proportionalX + 5, proportionalX + 9, keyRowPY, band),
            maxInkIn(focused, proportionalX - 9, proportionalX - 5, keyRowPY, band)
        );
        CHECK(ringInk > 40);
        for (const int sign : { 1, -1 }) {
            const Rgb body = pixelAt(focused, proportionalX + sign * 6, keyRowPY);
            const double fraction = inkFraction(body, fullRing, band);
            CHECK(fraction > 0.3 && fraction <= 1.0);
            CHECK(chebyshev(body, over(hue, ringAlpha * fraction, band)) <= 3);
        }
        // The absolute key beside it is still ringless - the focus did not spread.
        CHECK(maxInkIn(focused, absoluteX + 5, absoluteX + 9, keyRowPY, band) <= kClean);
    }

    // --- 8: A COLLAPSED FOLDER OWNS A DEDICATED SUMMARY LANE.
    // The folder has its own keys AND a child with keys: the summary is a second
    // lane under the folder's own one, never an overlay on a child's lane. The
    // marker is one rounded polygon per frame, and the marquee/hit test use the
    // summary band's centre.
    {
        core::TestFolder folder;
        stack.push(folder.timeLine()->createPusher(core::TimeKeyType_Move, kKeyFrameA, new core::MoveKey()));
        stack.push(folder.child().timeLine()->createPusher(core::TimeKeyType_Move, kKeyFrameA, new core::MoveKey()));
        stack.push(folder.child().timeLine()->createPusher(core::TimeKeyType_Opa, kKeyFrameA, new core::OpaKey()));

        const QRect rowRect(ctrl::TimeLineEditor::kTimeLineMargin, kRowATop, 1,
                            ctrl::TimeLineRow::kHeight + ctrl::TimeLineRow::kFolderSummaryHeight);
        const ctrl::TimeLineRow row(&folder, rowRect, true, false);
        CHECK(row.summary);
        CHECK(row.summaryHeight() == ctrl::TimeLineRow::kFolderSummaryHeight);
        CHECK(row.ownRect().height() == ctrl::TimeLineRow::kHeight);
        CHECK(row.summaryRect().top() == rowRect.top() + ctrl::TimeLineRow::kHeight);
        CHECK(row.summaryRect().bottom() == rowRect.bottom());

        ctrl::time::Inks inks;
        inks.body = theme.trackColor();
        inks.bodySelected = theme.trackSelectColor();
        inks.edge = theme.trackEdgeColor();
        inks.separator = theme.trackSeperatorColor();

        const QRect camera(0, 0, kWidth, kHeight);
        const ctrl::time::Lines lines =
            ctrl::time::buildLines(QVector<ctrl::TimeLineRow>{ row }, camera, camera, inks);
        CHECK(lines.lanes.size() == 2);
        CHECK(!lines.lanes[0].summary);
        CHECK(lines.lanes[1].summary);
        CHECK(lines.lanes[1].rect.top() == row.summaryRect().top());
        CHECK(lines.lanes[1].rect.bottom() == row.summaryRect().bottom());
        // The band names what it is; the tree names the folder.
        CHECK(lines.lanes[1].labels.size() == 1);
        CHECK(lines.lanes[1].labels[0].text == QStringLiteral("Children"));
        CHECK(lines.lanes[1].labels[0].box == QRect(
            camera.center().x() - 50, row.summaryRect().top(), 100, row.summaryRect().height()
        ));

        ctrl::time::Scaler summaryScale;
        summaryScale.setFrameEnd(200);
        const int keyX = ctrl::TimeLineEditor::kTimeLineMargin + summaryScale.pixelWidth(kKeyFrameA);
        const ctrl::time::KeyPass summary =
            ctrl::time::buildChildKeys(&folder, row.summaryRect(), ctrl::TimeLineEditor::kTimeLineMargin,
                                       summaryScale, util::Range(0, 200));
        // Two hidden keys on ONE frame: one marker, not two overdrawing triangles.
        CHECK(summary.marks.size() == 1);
        CHECK(summary.marks[0].shape == ctrl::time::KeyShape::Polygon);
        CHECK(summary.marks[0].polygon.size() > 3); // rounded corners, not a 3-point triangle
        CHECK(!summary.stroke); // fill-only: no rim on a 5px neutral marker
        CHECK(std::abs(summary.marks[0].centre.x() - keyX) <= 1);
        CHECK(std::abs(summary.marks[0].centre.y() - row.summaryCenterY()) <= 1);

        const QVector<ctrl::TimeLineRow> rows{ row };
        ctrl::time::Marquee marquee(rows, summaryScale, ctrl::TimeLineEditor::kTimeLineMargin);
        const ctrl::time::Hit hit = marquee.hitTest(
            QPoint(keyX, int(row.summaryCenterY())), 1.0f);
        CHECK(hit.isValid());
        CHECK(hit.node == &folder.child());
        CHECK(hit.pos.type() == core::TimeKeyType_Move);

        // THE HOLDER DIAMOND USES THE SAME RADIUS. A Bone key can hold children, so
        // its marker is the diamond; a rounded corner flattens the four-point shape
        // into more than four polygon vertices (the pre-rounding marker had exactly 4).
        core::TestNode holderNode;
        stack.push(holderNode.timeLine()->createPusher(
            core::TimeKeyType_Bone, kKeyFrameA, new core::BoneKey()));
        const ctrl::TimeLineRow holderRow(
            &holderNode, QRect(ctrl::TimeLineEditor::kTimeLineMargin, kRowATop, 1, ctrl::TimeLineRow::kHeight),
            false, false);
        const ctrl::time::KeyPass holder = ctrl::time::buildKeys(
            &holderNode, holderRow, summaryScale, util::Range(0, 200), ctrl::TimeLineEditor::kTimeLineMargin,
            inks.body);
        CHECK(holder.marks.size() == 1);
        CHECK(holder.marks[0].shape == ctrl::time::KeyShape::Polygon);
        CHECK(holder.marks[0].polygon.size() > 4);

        // A folder with no own keys uses the whole row for the summary: no empty
        // lane above it. The tree height and TimeLineRow agree on the same 22px.
        core::TestFolder summaryOnly;
        stack.push(summaryOnly.child().timeLine()->createPusher(
            core::TimeKeyType_Move, kKeyFrameA, new core::MoveKey()));
        const ctrl::TimeLineRow summaryRow(
            &summaryOnly, QRect(ctrl::TimeLineEditor::kTimeLineMargin, kRowATop, 1,
                                ctrl::TimeLineRow::kFolderSummaryHeight),
            true, false);
        CHECK(summaryRow.summary);
        CHECK(summaryRow.ownRect().height() == 0);
        CHECK(summaryRow.summaryRect().height() == ctrl::TimeLineRow::kFolderSummaryHeight);
        const ctrl::time::Lines summaryLines =
            ctrl::time::buildLines(QVector<ctrl::TimeLineRow>{ summaryRow }, camera, camera, inks);
        CHECK(summaryLines.lanes.size() == 1);
        CHECK(summaryLines.lanes[0].summary);

        // An expanded folder has no summary lane: its children are rows of their own.
        const ctrl::TimeLineRow openRow(
            &folder, QRect(ctrl::TimeLineEditor::kTimeLineMargin, kRowATop, 1, ctrl::TimeLineRow::kHeight),
            false, false);
        CHECK(!openRow.summary);
        const ctrl::time::Lines openLines =
            ctrl::time::buildLines(QVector<ctrl::TimeLineRow>{ openRow }, camera, camera, inks);
        CHECK(openLines.lanes.size() == 1);
        CHECK(!openLines.lanes[0].summary);
    }

    std::printf("timeline_visuals: all checks passed\n");
    return 0;
}
