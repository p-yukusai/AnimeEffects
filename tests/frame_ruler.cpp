// THE FRAME RULER'S CONTRACT (src/gui/FrameRuler.h): one set of rules for the strip
// above the timeline's lanes and the band above the graph panel.
//
// WHAT IS DRIVEN. A real ctrl::TimeLineEditor (no widget, no GL), the shared builder
// and the shared QPainter renderer, into an offscreen QImage - so the pixels asserted
// here are the ones the timeline's ruler paints. The graph panel consumes the SAME
// model through gui::buildGraphPanelModel; its own geometry is pinned by
// graph_panel_model, and the fact that both come from this builder is what makes the
// two rulers unable to disagree.
//
// WHAT IT PINS, and why each assertion is a regression gate:
//   1 THE TICKS. Ascending, one per column, every tier known, every column inside the
//     band. A zoomed-out window rounds several frames onto one pixel; the more
//     prominent tier must win that column (the signed-off rule) instead of whichever
//     frame happened to be drawn last.
//   2 THE BOUNDARY LABELS. The project's own first/last frame own their slots: separate
//     labels when they fit, ONE merged "a-b" label when they would touch, and NOTHING
//     when the range is a single frame (the signed-off rule - "5-5" was noise).
//   3 THE PLAYHEAD'S COLUMN. The frame-number pill carries the only number in its
//     column, so no label may sit under it.
//   4 THE PLAYBACK WINDOW. The wash is clipped to the visible columns, the two 2px
//     stems stand on the window's first and last frame, and two arms that would touch
//     collapse into one centred "a-b" pill.
//   5 THE ACTIVE RANGE. Ticks and numbers outside it are CLASSIFIED as outside (the
//     ink applies the shared 0.4), never dropped.
//   6 THE INK. A tick's column carries ink in the tick rows; a label's box carries ink
//     in the number row; and a number that leaves the band is CLIPPED, never shifted -
//     its box is still centred on its tick (the signed-off rule: a shifted number
//     would misreport the frame it names).
//   7 THE FORMATTER'S BASIS. Numbers are formatted from the PROJECT frame range, so a
//     number's padding cannot change as the view scrolls.
//
// CMake wiring is in tests/CMakeLists.txt (it needs ctrl, gl and the real
// ../src/gui/theme/TimeLine.cpp, like the other timeline tests).
#include <QApplication>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "XCAssert.h"
#include "core/TimeFormat.h"
#include "ctrl/TimeLineEditor.h"
#include "gui/FrameRuler.h"
#include "gui/theme/Colors.h"
#include "gui/theme/TimeLine.h"

// Project.cpp (in the core library) references the GUI's AudioPlaybackWidget and
// ctrl::TimeLineEditor's paste path references ObjectTreeWidget; neither is reached
// here. Stub them, the pattern the other timeline tests use.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }
#include "gui/ObjectTreeWidget.h"
bool gui::obj::Item::isTopNode() const { return false; }
std::pair<core::TimeKey*, core::LayerMesh*> gui::ObjectTreeWidget::getAreaMeshImpl(core::ObjectNode&, int) {
    return {nullptr, nullptr};
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
} // namespace
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace {

#define CHECK(cond)                                                                                    \
    do {                                                                                               \
        if (!(cond)) {                                                                                 \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                             \
            return 1;                                                                                  \
        }                                                                                              \
    } while (false)

using gui::frameRuler::Axis;
using gui::frameRuler::Inputs;
using gui::frameRuler::Model;

constexpr int kWidth = 920;
constexpr int kBandHeight = gui::frameRuler::kBandHeight;

// The timeline's own adapter: the ladder's integer positions offset by the margin the
// keys, the playhead and the range markers share.
Axis timelineAxis(const ctrl::TimeLineEditor& aEditor) {
    const ctrl::time::Scaler& scale = aEditor.timeScale();
    const int margin = ctrl::TimeLineEditor::kTimeLineMargin;
    Axis axis;
    axis.pixelOfFrame = [&scale](int aFrame) { return double(margin + scale.pixelWidth(aFrame)); };
    axis.frameOfPixel = [&scale](int aPixel) { return scale.frame(aPixel - margin); };
    axis.attributeAt = [&scale](int aFrame, double) { return scale.attribute(aFrame); };
    axis.pixelsPerFrame = scale.pixelsPerFrame();
    axis.tickStride = scale.tickStride(scale.pixelsPerFrame());
    return axis;
}

Inputs timelineInputs(const ctrl::TimeLineEditor& aEditor, int aWidth, int aOffset = 0) {
    const QFontMetrics metrics{QFont()};
    const core::TimeFormat format(aEditor.frameRange(), aEditor.fps());
    const QRect band(QPoint(-aOffset, 0), QSize(aWidth, kBandHeight));
    Inputs in;
    in.band = band;
    in.visible = band;
    in.labelTop = band.top() + gui::frameRuler::kNumberTop;
    in.frameRange = aEditor.frameRange();
    in.hasFrameRange = true;
    in.activeRange = aEditor.playbackRange();
    in.visibleFrames = util::Range(
        aEditor.timeScale().frame(-aOffset - 5), aEditor.timeScale().frame(-aOffset + aWidth + 5)
    );
    in.fps = aEditor.fps();
    in.formatType = aEditor.timeFormatType();
    const int playFrame = aEditor.currentFrame().get();
    in.hasPlayhead = true;
    in.playheadFrame = playFrame;
    in.playheadText = format.frameToString(playFrame, in.formatType);
    in.playheadTextWidth = metrics.horizontalAdvance(in.playheadText);
    const ctrl::TimeLineEditor::PlayWindow& window = aEditor.playWindow();
    if (window.active()) {
        in.windowStart = window.start;
        in.windowEnd = window.end;
        in.windowStartText = format.frameToString(window.start, in.formatType);
        in.windowEndText = format.frameToString(window.end, in.formatType);
        in.windowStartTextWidth = metrics.horizontalAdvance(in.windowStartText);
        in.windowEndTextWidth = metrics.horizontalAdvance(in.windowEndText);
        in.windowMergedTextWidth =
            metrics.horizontalAdvance(in.windowStartText + QString::fromUtf8("\u2013") + in.windowEndText);
    }
    return in;
}

QImage paint(const Model& aModel, theme::TimeLine& aTheme) {
    QImage image(aModel.band.width(), kBandHeight, QImage::Format_RGB32);
    image.fill(theme::Colors::current().base);
    QPainter painter(&image);
    gui::frameRuler::PaintInk ink;
    ink.content = aTheme.headerContentColor();
    const theme::Colors& colors = theme::Colors::current();
    ink.windowBand = colors.windowBand;
    ink.windowEdge = colors.windowEdge;
    ink.windowText = colors.windowText;
    gui::frameRuler::paintRuler(painter, aModel, ink);
    painter.end();
    return image;
}

// Ink in a box: any pixel differing from the band's floor.
bool hasInk(const QImage& aImage, const QRect& aBox) {
    const QRgb floor = theme::Colors::current().base.rgb();
    for (int y = aBox.top(); y <= aBox.bottom(); ++y) {
        for (int x = aBox.left(); x <= aBox.right(); ++x) {
            if (x < 0 || y < 0 || x >= aImage.width() || y >= aImage.height())
                continue;
            if (aImage.pixel(x, y) != floor)
                return true;
        }
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    theme::Colors::activate(true, theme::kDefaultAccent);
    theme::TimeLine theme;

    // --- 1: the ticks are ascending, one per column, classified, inside the band.
    {
        ctrl::TimeLineEditor editor;
        editor.setFrameRange(util::Range(1, 600));
        const Model model = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        CHECK(!model.ticks.isEmpty());
        for (int i = 0; i < model.ticks.size(); ++i) {
            CHECK(model.band.left() <= model.ticks.at(i).x && model.ticks.at(i).x <= model.band.right());
            CHECK(model.ticks.at(i).level != int(ctrl::time::Scaler::TickLevel::None));
            if (i > 0)
                CHECK(model.ticks.at(i - 1).x < model.ticks.at(i).x); // ascending, no duplicates
        }
        // A deep zoom-out rounds many frames onto one column; the column keeps the most
        // prominent tier, not the last one drawn.
        const_cast<ctrl::time::Scaler&>(editor.timeScale()).setIndex(-120);
        const Model zoomed = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        CHECK(!zoomed.ticks.isEmpty());
        for (int i = 0; i < zoomed.ticks.size(); ++i) {
            if (i > 0)
                CHECK(zoomed.ticks.at(i - 1).x < zoomed.ticks.at(i).x);
        }
    }

    // --- 2: the boundary labels - separate, merged, and NONE for a one-frame range.
    {
        ctrl::TimeLineEditor editor;
        // A rung whose majors are far apart, so the ends of the range are OFF-major and
        // therefore own their slots (the rule the labels exist for).
        const_cast<ctrl::time::Scaler&>(editor.timeScale()).setIndex(-20);
        editor.setFrameRange(util::Range(3, 597));
        editor.setFrame(core::Frame(300));
        const auto levelOf = [&editor](int aFrame) { return editor.timeScale().attribute(aFrame).level; };
        CHECK(levelOf(3) != ctrl::time::Scaler::TickLevel::Major);
        CHECK(levelOf(597) != ctrl::time::Scaler::TickLevel::Major);
        const Model wide = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        int boundary = 0;
        for (const auto& label : wide.labels)
            if (label.boundary)
                ++boundary;
        CHECK(boundary == 2);

        editor.setFrameRange(util::Range(3, 9)); // too narrow for both labels
        editor.setFrame(core::Frame(500)); // the pill must not own the window's column
        const Model narrow = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        boundary = 0;
        QString merged;
        for (const auto& label : narrow.labels) {
            if (label.boundary) {
                ++boundary;
                merged = label.text;
            }
        }
        CHECK(boundary == 1);
        CHECK(merged.contains(QString::fromUtf8("\u2013")));

        editor.setFrameRange(util::Range(7, 7)); // a single frame: no ends to mark
        editor.setFrame(core::Frame(7));
        const Model single = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        for (const auto& label : single.labels)
            CHECK(!label.boundary);
        // ...but the frame itself is still MARKED: the ladder may classify it as None, so
        // the boundary block is the only thing that draws it. Without this the project's
        // only frame had no tick at all (the defect a review caught in the extraction).
        {
            const int x = int(std::lround(timelineAxis(editor).pixelOfFrame(7)));
            bool marked = false;
            for (const auto& tick : single.ticks)
                marked = marked || (tick.x == x && tick.level == int(ctrl::time::Scaler::TickLevel::Major) &&
                                    tick.boundary);
            CHECK(marked);
        }
    }

    // --- 2b: a boundary tick WINS its column. Several frames round onto one pixel at a
    // zoomed-out rung; the project's edge must be the mark that survives there, not a
    // ladder tick that happened to sort first.
    {
        ctrl::TimeLineEditor editor;
        const_cast<ctrl::time::Scaler&>(editor.timeScale()).setIndex(-60);
        editor.setFrameRange(util::Range(1, 600));
        editor.setFrame(core::Frame(300));
        const Model model = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        const int edgeX = int(std::lround(timelineAxis(editor).pixelOfFrame(1)));
        int atEdge = 0;
        int boundaryAtEdge = 0;
        for (const auto& tick : model.ticks) {
            if (tick.x != edgeX)
                continue;
            ++atEdge;
            boundaryAtEdge += tick.boundary ? 1 : 0;
        }
        CHECK(atEdge == 1);
        CHECK(boundaryAtEdge == 1);
    }

    // --- 3: the playhead's pill owns its column.
    {
        ctrl::TimeLineEditor editor;
        editor.setFrameRange(util::Range(1, 600));
        // Park the playhead on a numbered major tick so the suppression is observable.
        int numbered = -1;
        {
            const Model probe = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
            for (const auto& label : probe.labels)
                if (!label.boundary && label.frame > 100) {
                    numbered = label.frame;
                    break;
                }
        }
        CHECK(numbered > 0);
        editor.setFrame(core::Frame(numbered));
        const Model model = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        CHECK(model.playhead.isValid());
        const int half = model.playhead.rect.width() / 2;
        for (const auto& label : model.labels) {
            const int centre = label.left + label.width / 2;
            CHECK(qAbs(centre - model.playhead.rect.center().x()) >= half);
        }
        for (const auto& label : model.labels)
            CHECK(!(label.frame == numbered && !label.boundary));
    }

    // --- 4: the playback window - stems on its frames, merged arms when they touch,
    // the wash clipped to the visible columns.
    {
        ctrl::TimeLineEditor editor;
        editor.setFrameRange(util::Range(1, 600));
        editor.setFrame(core::Frame(300));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Begin, core::Frame(100));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Drag, core::Frame(200));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::End, core::Frame(200));
        const Model wide = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        CHECK(wide.window.active);
        CHECK(!wide.window.band.isEmpty());
        CHECK(wide.window.startStem.left() == wide.window.band.left());
        CHECK(wide.window.startStem.width() == 2);
        CHECK(wide.window.endStem.left() + 1 == wide.window.band.right());
        CHECK(wide.window.merged.rect.isEmpty()); // the arms fit
        CHECK(wide.window.start.isValid() && wide.window.end.isValid());
        CHECK(wide.window.start.rect.left() == wide.window.startStem.left() + 2);
        CHECK(wide.window.start.rect.top() == wide.band.top() + gui::frameRuler::kPillTop);

        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Begin, core::Frame(100));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Drag, core::Frame(101));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::End, core::Frame(101));
        const Model merged = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        CHECK(merged.window.active);
        CHECK(merged.window.merged.isValid());
        CHECK(merged.window.start.rect.isEmpty() && merged.window.end.rect.isEmpty());
        CHECK(merged.window.merged.text.contains(QString::fromUtf8("\u2013")));
    }

    // --- 5: the active range is a CLASSIFICATION, not a filter.
    {
        ctrl::TimeLineEditor editor;
        editor.setFrameRange(util::Range(1, 600));
        editor.setFrame(core::Frame(300));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Begin, core::Frame(200));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::Drag, core::Frame(300));
        editor.updatePlayWindow(ctrl::TimeLineEditor::PlayWindowStep::End, core::Frame(300));
        const Model model = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        bool sawOutside = false;
        bool sawInside = false;
        for (const auto& tick : model.ticks) {
            sawOutside = sawOutside || !tick.inActiveRange;
            sawInside = sawInside || tick.inActiveRange;
        }
        CHECK(sawOutside && sawInside);
    }

    // --- 6: the ink - ticks in the tick rows, numbers in the number row, and a number
    // that leaves the band is CLIPPED, not shifted.
    {
        ctrl::TimeLineEditor editor;
        editor.setFrameRange(util::Range(1, 600));
        editor.setFrame(core::Frame(300));
        const Model model = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        const QImage image = paint(model, theme);
        const auto& tick = model.ticks.at(model.ticks.size() / 2);
        const QRect tickBox(tick.x, model.band.bottom() - gui::frameRuler::kTickLength, 1,
                            gui::frameRuler::kTickLength);
        CHECK(hasInk(image, tickBox));
        const auto& label = model.labels.at(model.labels.size() / 2);
        CHECK(hasInk(image, QRect(label.left, label.top, label.width, gui::frameRuler::kNumberHeight)));
        // Never shifted: every published box is centred on a column, never nudged inside
        // the band (the signed-off rule - a shifted number would name the wrong frame).
        for (const auto& each : model.labels) {
            const int centre = each.left + each.width / 2;
            bool onATick = false;
            for (const auto& t : model.ticks)
                onATick = onATick || qAbs(t.x - centre) <= 1;
            CHECK(onATick);
        }
    }

    // --- 7: the formatter's basis is the PROJECT range (padding cannot follow a scroll).
    {
        ctrl::TimeLineEditor editor;
        editor.setFrameRange(util::Range(1, 600));
        editor.setFrame(core::Frame(300));
        editor.setTimeFormatType(core::TimeFormatType_Frames_From1);
        const core::TimeFormat project(editor.frameRange(), editor.fps());
        const Model model = gui::frameRuler::build(timelineInputs(editor, kWidth), timelineAxis(editor));
        CHECK(!model.labels.isEmpty());
        for (const auto& label : model.labels) {
            if (label.frame < 0)
                continue;
            CHECK(label.text == project.frameToString(label.frame, editor.timeFormatType()));
        }
    }

    std::printf("PASS frame_ruler\n");
    return 0;
}
