// THE CANVAS PAINTER RULER'S PARITY GATE (needs Qt >= 6.12: AE_CANVAS_PAINTER).
//
// One gui::frameRuler::Model, painted twice - by gui::frameRuler::paintRuler (QPainter,
// the shipped renderer) and by gui::frameRuler::paintRulerCanvas (Canvas Painter, the
// migration target) - and the two images compared pixel by pixel. This is the first
// surface of the migration and therefore the first proof that the two ink paths can
// read one model.
//
// WHAT IS AND IS NOT EXPECTED TO MATCH. The GEOMETRY (which columns are ticked, where a
// number's box sits, where the window's stems and pills are) comes from the model, so a
// difference there would be a bug in the Canvas renderer. The GLYPHS are rasterized by
// two different engines (Qt's raster font engine vs Canvas Painter's atlas), so text
// coverage is expected to differ by antialiasing even when the placement is identical -
// the gate reports text and non-text differences SEPARATELY for that reason, and only
// the non-text ones are asserted to be zero.
#include <QApplication>
#include <QFontMetrics>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QCanvasPainter>
#include <QCanvasPainterWidget>
#include <QMap>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ctrl/TimeLineEditor.h"
#include "gui/FrameRuler.h"
#include "gui/FrameRulerCanvas.h"
#include "gui/theme/Colors.h"
#include "gui/theme/TimeLine.h"
#include "XCAssert.h"
#include "gui/AudioPlaybackWidget.h"
#include "gui/ObjectTreeWidget.h"

bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }
bool gui::obj::Item::isTopNode() const { return false; }
std::pair<core::TimeKey*, core::LayerMesh*> gui::ObjectTreeWidget::getAreaMeshImpl(core::ObjectNode&, int) {
    return {nullptr, nullptr};
}
namespace {
class H1: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class H2: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
H1 g1;
H2 g2;
} // namespace
XCAssertHandler* gXCAssertHandler = &g1;
XCErrorHandler* gXCErrorHandler = &g2;

namespace {

constexpr int kWidth = 920;
constexpr int kHeight = gui::frameRuler::kBandHeight;

struct Fixture {
    gui::frameRuler::Model model;
    gui::frameRuler::PaintInk ink;
};

Fixture fixtureFor(ctrl::TimeLineEditor& aEditor, const QFontMetrics& aMetrics, int aOffset) {
    const core::TimeFormat format(aEditor.frameRange(), aEditor.fps());
    const QRect band(QPoint(-aOffset, 0), QSize(kWidth, kHeight));
    gui::frameRuler::Inputs in;
    in.band = band;
    in.visible = band;
    in.labelTop = band.top() + gui::frameRuler::kNumberTop;
    in.frameRange = aEditor.frameRange();
    in.hasFrameRange = true;
    in.activeRange = aEditor.playbackRange();
    in.visibleFrames = util::Range(
        aEditor.timeScale().frame(-aOffset - ctrl::TimeLineEditor::kTimeLineMargin - 5),
        aEditor.timeScale().frame(-aOffset + kWidth - ctrl::TimeLineEditor::kTimeLineMargin + 5)
    );
    in.fps = aEditor.fps();
    in.formatType = aEditor.timeFormatType();
    in.hasPlayhead = true;
    in.playheadFrame = aEditor.currentFrame().get();
    in.playheadText = format.frameToString(in.playheadFrame, in.formatType);
    in.playheadTextWidth = aMetrics.horizontalAdvance(in.playheadText);
    const ctrl::TimeLineEditor::PlayWindow& w = aEditor.playWindow();
    if (w.active()) {
        in.windowStart = w.start;
        in.windowEnd = w.end;
        in.windowStartText = format.frameToString(w.start, in.formatType);
        in.windowEndText = format.frameToString(w.end, in.formatType);
        in.windowStartTextWidth = aMetrics.horizontalAdvance(in.windowStartText);
        in.windowEndTextWidth = aMetrics.horizontalAdvance(in.windowEndText);
        in.windowMergedTextWidth =
            aMetrics.horizontalAdvance(in.windowStartText + QString::fromUtf8("\u2013") + in.windowEndText);
    }
    const ctrl::time::Scaler& sc = aEditor.timeScale();
    const int mg = ctrl::TimeLineEditor::kTimeLineMargin;
    gui::frameRuler::Axis ax;
    ax.pixelOfFrame = [&sc](int fr) { return double(mg + sc.pixelWidth(fr)); };
    ax.frameOfPixel = [&sc](int px) { return sc.frame(px - mg); };
    ax.attributeAt = [&sc](int fr, double) { return sc.attribute(fr); };
    ax.pixelsPerFrame = sc.pixelsPerFrame();
    ax.tickStride = sc.tickStride(sc.pixelsPerFrame());

    Fixture f;
    f.model = gui::frameRuler::build(in, ax);
    const theme::Colors& c = theme::Colors::current();
    f.ink.content = theme::TimeLine().headerContentColor();
    f.ink.windowBand = c.windowBand;
    f.ink.windowEdge = c.windowEdge;
    f.ink.windowText = c.windowText;
    // The playhead's pill is part of the strip now (it used to be the cursor widget's), so
    // the fixture states its ink too - otherwise both hosts would paint an invalid colour
    // and the gate would compare two blanks.
    f.ink.playheadFill = c.accentBright;
    f.ink.playheadText = c.accentBrightText;
    return f;
}

// The Canvas Painter host: an offscreen QCanvasPainterWidget that paints the fixture.
class CanvasRuler: public QCanvasPainterWidget {
public:
    Fixture fixture;
    // The host applies the view translation, exactly as TimeRuler::paintEvent does
    // (`painter.translate(origin)`): the model states ABSOLUTE columns, the surface shows
    // them shifted by the view. Without this a scrolled view paints off-canvas.
    QPoint origin;

protected:
    void paint(QCanvasPainter* aPainter) override {
        aPainter->translate(float(origin.x()), float(origin.y()));
        gui::frameRuler::paintRulerCanvas(*aPainter, fixture.model, fixture.ink);
    }
};

struct Verdict {
    int nonTextDiff = 0;
    int textDiff = 0;
    int blendRounding = 0;
};

// Is (x, y) a place the two rasterizers may legitimately differ? Two such places:
//
//  * a LABEL's box - two glyph rasterizers cannot agree per pixel;
//  * the ONE-PIXEL OUTLINE of a published pill (the playhead's and the playback window's
//    arms). A rounded corner's antialiasing is a kernel, and the measured difference is
//    exactly one unit of coverage on the rows and columns outside the pill (Canvas:
//    #262627 where the raster has #262626, and mirrored inside). The pill's BODY is not
//    excused: the per-pill ink assert below requires both renderings to fill it.
//
// Everything else - a tick, a rule, a band's edge - must match to the pixel.
bool isExcused(int aX, int aY, const gui::frameRuler::Model& aModel) {
    for (const auto& label : aModel.labels) {
        // The raster renderer draws into QRect(QPoint(left, top), QPoint(left + width + 1,
        // top + kNumberHeight)) - an INCLUSIVE rect two columns wider than the model's
        // reserve - so its glyph ink can lie just outside the published box.
        // ...and glyph ANTIALIASING fringes reach a couple of pixels beyond the glyphs
        // themselves, so the text region is the box grown by 2px - the same band the two
        // rasterizers disagree in by construction.
        constexpr int kFringe = 2;
        if (aX >= label.left - kFringe && aX <= label.left + label.width + 1 + kFringe &&
            aY >= label.top - kFringe && aY <= label.top + gui::frameRuler::kNumberHeight + kFringe)
            return true;
    }
    const auto onPillOutline = [&aX, &aY](const QRect& aRect) {
        const QRect outer = aRect.adjusted(-1, -1, 1, 1);
        const QRect inner = aRect.adjusted(1, 1, -1, -1);
        return outer.contains(aX, aY) && !inner.contains(aX, aY);
    };
    if (aModel.playhead.isValid()) {
        if (onPillOutline(aModel.playhead.rect))
            return true;
        // The pill's NUMBER is text (a glyph rasterizer difference), so its own box is
        // excused exactly as a ruler number's is - and its fill, which is not text, stays
        // compared. The per-pill ink assert below is what proves the pill is drawn.
        constexpr int kFringe = 2;
        const int textWidth = QFontMetrics(QFont()).horizontalAdvance(aModel.playhead.text);
        const QRect textBox(
            aModel.playhead.rect.left() + gui::frameRuler::kPillPadding - kFringe,
            aModel.band.top() + gui::frameRuler::kNumberTop - kFringe,
            textWidth + 2 * kFringe, gui::frameRuler::kNumberHeight + 2 * kFringe
        );
        if (textBox.contains(aX, aY))
            return true;
    }
    if (aModel.window.active) {
        for (const auto& pill : {aModel.window.merged, aModel.window.start, aModel.window.end}) {
            if (!pill.isValid())
                continue;
            // The window arms' BODIES are excused too: their faces are squared against the
            // bracket stems by overdrawing slivers, which the two engines composite
            // differently - a rule older than this gate, measured when the arms were added.
            if (pill.rect.contains(aX, aY) || onPillOutline(pill.rect))
                return true;
        }
    }
    return false;
}

Verdict compare(const QImage& aRaster, const QImage& aCanvas, const gui::frameRuler::Model& aModel,
                QPoint aOrigin, bool aDetail = false) {
    Verdict v;
    int shown = 0;
    QMap<int, int> rows;
    for (int y = 0; y < aRaster.height(); ++y) {
        for (int x = 0; x < aRaster.width(); ++x) {
            const QRgb a = aRaster.pixel(x, y);
            const QRgb b = aCanvas.pixel(x, y);
            if (a == b)
                continue;
            // The model's boxes are ABSOLUTE; the images are the view's columns shifted by
            // the host's translation. Classify in the MODEL's space.
            const QColor ca(a);
            const QColor cb(b);
            if (std::abs(ca.red() - cb.red()) <= 1 && std::abs(ca.green() - cb.green()) <= 1 &&
                std::abs(ca.blue() - cb.blue()) <= 1) {
                ++v.blendRounding; // translucent ink: one LSB is the renderer's, not ours
                continue;
            }
            if (isExcused(x - aOrigin.x(), y - aOrigin.y(), aModel))
                ++v.textDiff;
            else {
                ++v.nonTextDiff;
                rows[y] += 1;
                if (shown < 8) {
                    std::printf("    nontext @(%d,%d) raster=%s canvas=%s\n", x, y,
                                qPrintable(QColor(a).name()), qPrintable(QColor(b).name()));
                    ++shown;
                }
            }
        }
    }
    if (aDetail) {
        std::printf("    nontext rows:");
        for (auto it = rows.begin(); it != rows.end(); ++it)
            std::printf(" %d:%d", it.key(), it.value());
        std::printf("\n");
    }
    return v;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    int failures = 0;
    int cases = 0;

    struct State {
        const char* name;
        int zoom, offset, rangeMax, frame, windowStart, windowEnd;
        core::TimeFormatType format;
        bool dark;
    };
    const State states[] = {
        {"default", 0, 0, 600, 300, -1, -1, core::TimeFormatType_Frames_From0, true},
        {"zoomed-out", -40, 0, 600, 300, -1, -1, core::TimeFormatType_Frames_From0, true},
        {"zoomed-in", 30, 0, 600, 300, -1, -1, core::TimeFormatType_Frames_From0, true},
        {"scrolled", 6, 240, 600, 300, -1, -1, core::TimeFormatType_Frames_From0, true},
        {"window", 0, 0, 600, 300, 100, 200, core::TimeFormatType_Frames_From0, true},
        {"window-merged", 0, 0, 600, 300, 100, 101, core::TimeFormatType_Frames_From0, true},
        {"one-frame", 0, 0, 7, 7, -1, -1, core::TimeFormatType_Frames_From0, true},
        {"timecode", 0, 0, 600, 300, 100, 200, core::TimeFormatType_Timecode_SMPTE, true},
        {"light", 0, 0, 600, 300, 100, 200, core::TimeFormatType_Frames_From0, false},
    };

    for (const State& state : states) {
        theme::Colors::activate(state.dark, theme::kDefaultAccent);
        ctrl::TimeLineEditor editor;
        editor.setFrameRange(util::Range(1, state.rangeMax));
        editor.setFrame(core::Frame(state.frame));
        editor.setTimeFormatType(state.format);
        const_cast<ctrl::time::Scaler&>(editor.timeScale()).setIndex(state.zoom);
        if (state.windowStart >= 0) {
            editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Begin, core::Frame(state.windowStart));
            editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Drag, core::Frame(state.windowEnd));
            editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::End, core::Frame(state.windowEnd));
        }

        const QImage rasterRef(kWidth, kHeight, QImage::Format_RGB32);
        QFontMetrics metrics{QFont()};
        QImage raster = rasterRef;
        raster.fill(theme::Colors::current().base);
        Fixture fixture;
        {
            QPainter p(&raster);
            p.setRenderHint(QPainter::Antialiasing, false);
            fixture = fixtureFor(editor, metrics, state.offset);
            p.translate(-state.offset, 0);
            gui::frameRuler::paintRuler(p, fixture.model, fixture.ink);
        }

        CanvasRuler widget;
        widget.fixture = fixture;
        widget.origin = QPoint(-state.offset, 0);
        widget.setFillColor(theme::Colors::current().base);
        widget.resize(kWidth, kHeight);
        const QImage canvas = widget.grabFramebuffer();
        if (canvas.isNull()) {
            std::printf("FAIL %-14s no Canvas Painter frame\n", state.name);
            ++failures;
            continue;
        }
        const QImage canvasAt = canvas.scaled(raster.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const Verdict verdict =
            compare(raster, canvasAt, fixture.model, QPoint(-state.offset, 0), state.offset != 0);
        ++cases;
        std::printf(
            "%-14s canvas=%dx%d  textDiff=%5d  blendLSB=%5d  NON-TEXT=%4d  ticks=%3d labels=%2d window=%d\n", state.name,
            canvas.width(), canvas.height(), verdict.textDiff, verdict.blendRounding, verdict.nonTextDiff, int(fixture.model.ticks.size()),
            int(fixture.model.labels.size()), int(fixture.model.window.active)
        );
        if (verdict.nonTextDiff != 0)
            ++failures;
        // A NUMBER'S BOX IS "EXPECTED" TERRITORY (compare() excuses it, two text engines
        // cannot agree per pixel), so a MISSING number would pass this gate - the same blind
        // spot that let a lane's label vanish from the Canvas body. So every number the model
        // publishes is checked for INK here, in both renderings.
        // The model's boxes are ABSOLUTE; the two images are the view's columns shifted by
        // the host's translation (compare() gets the same origin), so map before counting.
        const QPoint origin(-state.offset, 0);
        // Ink inside a box, measured against the box's own modal colour (so it works on
        // either palette): a region one engine paints and the other leaves empty is the
        // failure this counts.
    const auto inkCount = [](const QImage& aImage, const QRect& aBox) {
        const QRect clip = aBox.intersected(aImage.rect());
        if (clip.isEmpty())
            return 0;
        QHash<QRgb, int> hist;
        for (int y = clip.top(); y <= clip.bottom(); ++y)
            for (int x = clip.left(); x <= clip.right(); ++x)
                ++hist[aImage.pixel(x, y)];
        QRgb bg = 0;
        int best = -1;
        for (auto it = hist.cbegin(); it != hist.cend(); ++it)
            if (it.value() > best) { best = it.value(); bg = it.key(); }
        int ink = 0;
        for (int y = clip.top(); y <= clip.bottom(); ++y)
            for (int x = clip.left(); x <= clip.right(); ++x)
                if (aImage.pixel(x, y) != bg)
                    ++ink;
        return ink;
    };
        for (const auto& eachLabel : fixture.model.labels) {
            const QRect box = QRect(eachLabel.left - 2, eachLabel.top - 2, eachLabel.width + 6,
                                    gui::frameRuler::kNumberHeight + 5).translated(-origin.x(), -origin.y());
            const int a = inkCount(raster, box);
            const int b = inkCount(canvasAt, box);
            // The rule is PARITY BETWEEN THE TWO RENDERINGS, not "the model says draw it": the
            // ruler legitimately blanks a number under the playhead's pill, and a box
            // can fall outside the image. What must never happen is one engine drawing
            // ink the other does not.
            const bool ok = (a == 0) == (b == 0) && (a == 0 || std::abs(a - b) * 2 <= std::max(a, b));
            std::printf("    number %-5s x=%d raster=%3d px canvas=%3d px %s\n",
                        qPrintable(eachLabel.text), eachLabel.left, a, b, ok ? "ok" : "MISSING/DIFFERS");
            if (!ok)
                ++failures;
        }

        // THE PLAYHEAD'S PILL IS EXCUSED FROM PIXEL COMPARISON (its outline is one
        // antialiasing kernel against another), so - exactly as with a number - its BODY
        // must be shown to carry ink in BOTH renderings, or a missing pill would pass.
        //
        // "CARRIES INK" MEANS CARRIES ITS FILL, not "has some non-modal pixel": a box whose
        // fill vanished still holds its LABEL text, so a modal-colour count would agree on
        // both sides while one had no pill at all. Counting the fill TOKEN is what makes the
        // assertion about the body. (Measured 2026-10-23: the Canvas ruler inked none of a
        // window pill's body - fill() left the ticks' subpaths current, so the roundRect was
        // appended to them - and this gate stayed green because no window pill was checked.)
        const auto fillCount = [](const QImage& aImage, const QRect& aBox, const QColor& aFill) {
            const QRect clip = aBox.intersected(aImage.rect());
            if (clip.isEmpty() || !aFill.isValid())
                return 0;
            int n = 0;
            for (int y = clip.top(); y <= clip.bottom(); ++y)
                for (int x = clip.left(); x <= clip.right(); ++x) {
                    const QColor px = aImage.pixelColor(x, y);
                    if (std::abs(px.red() - aFill.red()) <= 2 && std::abs(px.green() - aFill.green()) <= 2 &&
                        std::abs(px.blue() - aFill.blue()) <= 2)
                        ++n;
                }
            return n;
        };
        if (fixture.model.playhead.isValid()) {
            const QRect box =
                fixture.model.playhead.rect.adjusted(2, 2, -2, -2).translated(-origin.x(), -origin.y());
            const int a = fillCount(raster, box, fixture.ink.playheadFill);
            const int b = fillCount(canvasAt, box, fixture.ink.playheadFill);
            const bool ok = (a == 0) == (b == 0) && (a == 0 || std::abs(a - b) * 2 <= std::max(a, b));
            std::printf("    playhead pill  raster=%3d px canvas=%3d px %s\n", a, b, ok ? "ok" : "MISSING/DIFFERS");
            if (!ok)
                ++failures;
        }

        // AND THE TEMPORARY PLAYBACK WINDOW'S PILLS, the same way. Their bodies are excused
        // from pixel comparison (rounded corners are two AA kernels), so each one's FILL is
        // asserted to be present in both renderings.
        for (const gui::frameRuler::Pill* eachPill :
             {&fixture.model.window.start, &fixture.model.window.end, &fixture.model.window.merged}) {
            if (!eachPill->isValid())
                continue;
            const QRect box = eachPill->rect.adjusted(2, 2, -2, -2).translated(-origin.x(), -origin.y());
            const int a = fillCount(raster, box, fixture.ink.windowEdge);
            const int b = fillCount(canvasAt, box, fixture.ink.windowEdge);
            const bool ok = (a == 0) == (b == 0) && (a == 0 || std::abs(a - b) * 2 <= std::max(a, b));
            std::printf("    window pill %-5s raster=%3d px canvas=%3d px %s\n", qPrintable(eachPill->text), a, b,
                        ok ? "ok" : "MISSING/DIFFERS");
            if (!ok)
                ++failures;
        }
    }

    std::printf("cases=%d failures=%d (only NON-TEXT differences fail the gate)\n", cases, failures);
    return failures == 0 ? 0 : 1;
}
