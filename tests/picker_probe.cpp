// picker_probe.cpp — exercise the EasingPickWidget's interactive behaviors
// (mode switch, slider drag-outside, knob clip, bezier spin input, the per-step
// bezier-handle drag stream) and print the results. Compiled directly against the
// widget sources like the layout gate, so it links only util + Qt Widgets.
#include <QApplication>
#include <QWidget>
#include <QSlider>
#include <QComboBox>
#include <QSet>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QDebug>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include "gui/prop/EasingPickWidget.h"
#include "gui/prop/bezierCurveEditor.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Icons.h"
#include "util/Easing.h"

using namespace gui::prop;

static int hfw_h(EasingPickWidget& w) {
    const int f = w.heightForWidth(300);
    return f > 0 ? f : w.sizeHint().height();
}

// This probe PRINTS measurements, but #7/#8/#9 encode REQUIREMENTS (the mock's
// gauge geometry, the phosphor stroke width, the field's state rules and its fixed
// height). It used to print MISMATCH and exit 0 regardless, so a stale expectation
// sat there reporting a problem nobody could see: it said h=20 for a field the
// widget sets to 24, and nothing failed. Mismatches are counted now and the exit
// code carries them.
static int gMismatches = 0;

static const char* verdict(bool aOk) {
    if (!aOk) ++gMismatches;
    return aOk ? "OK" : "MISMATCH";
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    theme::Colors::activate(true, theme::kDefaultAccent);
    // Glyphs are SVGs tinted at runtime into a scratch dir (in the app,
    // GUIResources::applyAppearance). Without this the icon dir is empty and
    // every QIcon(...) here loads nothing, so any icon measurement silently
    // measures a blank widget.
    QTemporaryDir iconScratch;
    theme::tintIcons(QStringLiteral(AE_ICONS_DIR), iconScratch.path(), theme::Colors::current());
    theme::setIconDir(iconScratch.path());

    util::Easing::Param initial;
    initial.type = util::Easing::Type_Cubic;
    initial.range = util::Easing::Range_InOut;
    initial.weight = 1.0f;

    EasingPickWidget w(initial);
    w.resize(300, hfw_h(w));
    w.show();
    app.processEvents();

    // ---- #1 mode-switch layout: default -> custom -> back ----
    const int hDefault = w.height();
    QWidget* seg = w.findChild<QWidget*>("seg");
    const int segHDefault = seg->height();

    w.setCustom();
    app.processEvents();
    w.resize(300, hfw_h(w));
    app.processEvents();
    const int hCustom = w.height();
    const int segHCustom = seg->height();

    w.setRange(util::Easing::Range_InOut);
    app.processEvents();
    w.resize(300, hfw_h(w));
    app.processEvents();
    const int hBack = w.height();
    const int segHBack = seg->height();

    fprintf(stderr, "#1 mode-switch: default h=%d segH=%d | custom h=%d segH=%d | back h=%d segH=%d\n",
            hDefault, segHDefault, hCustom, segHCustom, hBack, segHBack);
    bool stretch = (hBack != hDefault) || (segHBack != segHDefault);
    fprintf(stderr, "#1 %s\n", stretch ? "STRETCH" : "ok");

    // app-like: DON'T resize after switching back (popover may keep custom h)
    w.setCustom(); app.processEvents();
    const int hTall = w.height();
    w.setRange(util::Easing::Range_InOut);
    // let the deferred single-shot re-fit fire (simulate the app's event loop)
    QEventLoop loop; QTimer::singleShot(150, &loop, &QEventLoop::quit); loop.exec();
    app.processEvents();
    const int segHNoResize = seg->height();
    fprintf(stderr, "#1b no-resize-back: popover h=%d (custom=%d) segH=%d  -> %s\n",
            w.height(), hTall, segHNoResize,
            segHNoResize > segHDefault ? "SEG STRETCHES" : "seg ok");

    // ---- #4 slider knob clip: measure the KNOB's painted top margin ----
    // The blend control is a plain self-driving QWidget (BlendControl), not a
    // QSlider, so find it as a QWidget and read its value via the popup draft.
    QWidget* slider = w.findChild<QWidget*>("blend-input");
    auto blendVal = [&] { return qRound(w.value().weight * 100.0f); };
    // Render the actual slider widget and measure the knob's vertical extent
    // vs the widget bounds — the naive sH<14 check missed that the knob sits
    // flush at the widget top (no margin) and its top edge gets cut.
    const QImage sliderImg = slider->grab().toImage();
    int knobTop = -1, knobBottom = -1;
    for (int y = 0; y < sliderImg.height(); ++y) {
        bool knobPix = false;
        for (int x = 0; x < sliderImg.width(); ++x) {
            const QRgb p = sliderImg.pixel(x, y);
            const int r = qRed(p), g = qGreen(p), b = qBlue(p);
            if (b > 200 && r > 120 && r < 230 && g < 175) { knobPix = true; break; }
        }
        if (knobPix) { if (knobTop < 0) knobTop = y; knobBottom = y; }
    }
    const int sH = slider->height();
    if (knobTop > 0) {
        // Clipped == the knob's painted extent touches or crosses the widget
        // bounds. knobTop==0 means the outer AA row would be cut off above;
        // knobBottom==sH-1 is the last row, still fully painted.
        const bool clipped = knobTop < 1 || knobBottom > sH - 1;
        fprintf(stderr, "#4 knob: widgetH=%d knobTop=%d knobBottom=%d margin=%d  -> %s\n",
                sH, knobTop, knobBottom, knobTop,
                clipped ? "CLIPPED" : "OK (fully painted, inside bounds)");
    } else {
        fprintf(stderr, "#4 knob: widgetH=%d knob NOT FOUND (value=%d)  -> %s\n",
                sH, blendVal(), sH < 15 ? "CLIPS" : "fits");
    }

    // ---- #5 drag outside: press the handle, move outside the widget ----
    QPoint center = slider->rect().center();
    const int oldVal = blendVal();
    // press at the current handle, then drag far above the widget.
    QMouseEvent press(QEvent::MouseButtonPress, center, slider->mapToGlobal(center),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(slider, &press);
    QPoint outside(center.x(), center.y() - slider->height() - 30);
    QMouseEvent move(QEvent::MouseMove, outside, slider->mapToGlobal(outside),
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(slider, &move);
    QMouseEvent rel(QEvent::MouseButtonRelease, outside, slider->mapToGlobal(outside),
                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(slider, &rel);
    const int afterVal = blendVal();
    fprintf(stderr, "#5 drag-outside: old=%d after=%d -> %s\n", oldVal, afterVal,
            afterVal != oldVal ? "TRACKS" : "STOPS");

    // #5b: the app wraps the picker as a Qt::Popup — drag the slider and move
    // the mouse OUTSIDE the popup while the button is held. The blend control
    // maps value purely from the x pixel, so the outside move is HORIZONTAL
    // (well past the popup's right edge) to actually change the value. Note
    // sendEvent bypasses QWidgetWindow::handleMouseEvent, so this verifies the
    // control's own drag handling; the real popup-path re-target is covered by
    // picker_qpa_probe (xcb).
    w.setWindowFlag(Qt::Popup, true);
    w.hide(); w.show(); app.processEvents();
    QWidget* s2 = w.findChild<QWidget*>("blend-input");
    const QPoint c2 = s2->rect().center();
    const int old2 = blendVal();
    QMouseEvent p2(QEvent::MouseButtonPress, c2, s2->mapToGlobal(c2),
                   Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s2, &p2);
    QPoint out2(c2.x() + s2->width() + 40, c2.y());
    QMouseEvent m2(QEvent::MouseMove, out2, s2->mapToGlobal(out2),
                   Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(s2, &m2);
    QMouseEvent r2(QEvent::MouseButtonRelease, out2, s2->mapToGlobal(out2),
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(s2, &r2);
    fprintf(stderr, "#5b popup-drag-outside: old=%d after=%d -> %s\n", old2, blendVal(),
            blendVal() != old2 ? "TRACKS" : "LOST");

    // ---- #6 bezier spin input: set the spin value, check it lands in bezier ----
    w.setCustom();
    app.processEvents();
    QDoubleSpinBox* x1 = w.findChild<QDoubleSpinBox*>("bezier-x1");
    // connect a probe to see valueChanged fire
    int fired = 0;
    QObject::connect(x1, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                     [&fired](double){ ++fired; });
    const double target = 0.42;
    x1->setValue(target);
    const double spinNow = x1->value();
    app.processEvents();
    const double got = w.value().cubicBezier.x1;
    fprintf(stderr, "#6 spin: set=%f spinAfterSet=%f valueChangedFired=%d got_in_bezier=%f -> %s\n",
            target, spinNow, fired, got,
            std::fabs(got - target) < 1e-4 ? "TAKES" : "REJECTED");
    // ---- #7 gauge dot: 8px --text dot, right-flush, on the value axis ----
    w.setCustom();
    if (!w.isVisible()) { w.show(); } // a Qt::Popup can auto-hide during the earlier tests
    w.resize(300, hfw_h(w)); // realize the custom layout (the editor is 274 tall)
    app.processEvents();
    QWidget* strip = w.findChild<QWidget*>("be-strip");
    // The gauge paints no background; give the grab a dark surface so the
    // near-white dot is detectable (the app's QSS supplies this surface).
    QPalette pal = strip->palette();
    pal.setColor(QPalette::Window, QColor(31, 31, 31));
    strip->setPalette(pal);
    strip->setAutoFillBackground(true);
    { // let the widget's deferred re-fit single-shot fire before measuring
      QEventLoop settle; QTimer::singleShot(200, &settle, &QEventLoop::quit); settle.exec();
    }
    app.processEvents();
    app.processEvents();
    strip->repaint();
    const QImage gi = strip->grab().toImage();
    fprintf(stderr, "#7 grab: gi=%dx%d strip=%dx%d visible=%d\n",
            gi.width(), gi.height(), strip->width(), strip->height(), (int)strip->isVisible());
    fprintf(stderr, "#7 debug: w=%dx%d hfw=%d strip=%dx%d parent=%dx%d\n",
            w.width(), w.height(), hfw_h(w), strip->width(), strip->height(),
            strip->parentWidget()->width(), strip->parentWidget()->height());
    int dotL = -1, dotR = -1, dotT = -1, dotB = -1;
    for (int y = 0; y < gi.height(); ++y) {
        for (int x = 0; x < gi.width(); ++x) {
            const QRgb p = gi.pixel(x, y);
            if (qRed(p) > 200 && qGreen(p) > 200 && qBlue(p) > 200) {
                if (dotL < 0 || x < dotL) dotL = x;
                if (x > dotR) dotR = x;
                if (dotT < 0) dotT = y;
                dotB = y;
            }
    }
    }
    if (dotL >= 0) {
        // The value is animation-driven, so verify the mock-declared
        // invariants: ~8px diameter, flush to the strip's right edge, and
        // fully inside the strip (the (1.5-v)/2*h axis is code-identical
        // to the mock's beYdot formula).
        const int dia = dotR - dotL + 1;
        const bool ok = dia >= 7 && dia <= 10
                        && (strip->width() - 1 - dotR) <= 1
                        && dotT >= 0 && dotB <= strip->height() - 1;
        fprintf(stderr, "#7 gauge-dot: strip=%dx%d img=%dx%d dot %dx%d rightGap=%d top=%d -> %s\n",
                strip->width(), strip->height(), gi.width(), gi.height(), dia, dotB - dotT + 1,
                strip->width() - 1 - dotR, dotT, verdict(ok));
    }

    // ---- #8 close X: phosphor stroke is thin (16/256 of the 14px box) ----
    // The glyph's diagonal crosses its middle row in about stroke/sin(45deg) px,
    // so the row's painted-pixel count reads the stroke directly: the phosphor
    // 0.875px stroke lands at ~1-2 px, the old 2.5px stroke at ~4. This used to
    // compute that count and throw it away — no print, no verdict — so the
    // scenario was silently absent while looking present. It now reports in the
    // same measure-then-verdict shape as #7 and #9, and reports NOT FOUND rather
    // than skipping, because a silent skip is the bug it is here to avoid.
    {
        // Measure the GLYPH, not the button: a QToolButton with autoRaise and a
        // transparent QSS background does not paint into grab() in this harness
        // (its icon is a valid pixmap with content while its grab is fully
        // transparent), so measuring the button reported 0 forever. The stroke
        // the design specifies lives in the glyph anyway.
        auto* closeBtn = w.findChild<QToolButton*>("popover-close");
        if (closeBtn && !closeBtn->icon().isNull()) {
            const QImage gi = closeBtn->icon().pixmap(closeBtn->iconSize()).toImage();
            const int midY = gi.height() / 2;
            // Count pixels the stroke actually COVERS, not every non-zero
            // alpha: an antialiased 0.875px diagonal always spreads over ~2
            // fringe pixels, so counting any-alpha cannot distinguish a thin
            // stroke from a thick one. (Measured profile at 14px:
            // 0,0,0,0,0,50,220,220,50,0,... — two covered, two fringe.)
            int covered = 0, fringe = 0;
            for (int x = 0; x < gi.width(); ++x) {
                const int a = qAlpha(gi.pixel(x, midY));
                if (a > 128) ++covered; else if (a > 0) ++fringe;
            }
            const bool ok = covered >= 1 && covered <= 2;
            fprintf(stderr, "#8 close-x: glyph=%dx%d midRow=%d covered=%d fringe=%d -> %s\n",
                    gi.width(), gi.height(), midY, covered, fringe, verdict(ok));
        } else {
            fprintf(stderr, "#8 close-x: popover-close glyph NOT FOUND -> %s\n", verdict(false));
        }
    }
    // ---- #9 fi fields: geometry stable + hover/focus/pressed states present ----
    // The custom bezier inputs are QDoubleSpinBox[role="fi"]. The bug was
    // `border: 0` in the popover's own stylesheet, which also killed the
    // app's inherited spinbox hover feedback. Verify (a) the field keeps its
    // 20px height (the 1px geometry-reserving border must not grow it) and
    // (b) the popover stylesheet actually carries the field's state rules.
    QDoubleSpinBox* fi = w.findChild<QDoubleSpinBox*>("bezier-x1");
    const int fiH = fi ? fi->height() : -1;
    const QString css = w.styleSheet();
    const bool hasHover = css.contains("[role=\"fi\"]:hover");
    const bool hasFocus = css.contains("[role=\"fi\"]:focus");
    const bool hasPressed = css.contains("[role=\"fi\"]:hover:pressed");
    const bool hasBorder = css.contains("[role=\"fi\"]") && css.contains("border: 1px solid");
    // 24, not 20: the field is setFixedHeight(24) in EasingPickWidget.cpp. This
    // expectation said 20 (an earlier design height) and the probe reported
    // MISMATCH forever without failing, which is worse than a missing check.
    const bool ok = fi && fiH == 24 && hasHover && hasFocus && hasPressed && hasBorder;
    fprintf(stderr, "#9 fi: h=%d hover=%d focus=%d pressed=%d borderRule=%d -> %s\n",
            fiH, (int)hasHover, (int)hasFocus, (int)hasPressed, (int)hasBorder,
            verdict(ok));

    // ---- #10 tile catalog: every easing the table lists is REACHABLE, at the
    // table's own position, and the Auto PRESET is the one MODE entry ----
    // The tiles are hand-listed in EasingPickWidget.cpp (kTileOrder) while the
    // offering itself is util::Easing's display table, so a new easing can be added
    // to the table and never appear here - which is invisible: the picker looks
    // complete and the option simply cannot be chosen. Assert the two sets agree
    // (every display-order type except Custom, exactly once) AND that each tile
    // writes its own entry: the three non-curve types first, then the AUTO PRESET
    // at index 3, then the curve family in the table's order (shifted by one).
    // Automatic handles are a per-side MODE now, so the preset writes Custom with
    // BOTH sides Auto rather than a retired type.
    {
        QSet<int> reachable;
        bool allTilesClick = true;
        bool allTilesAtTableIndex = true;
        int tileCount = 0;
        int autoTileIndex = -1;
        for (int i = 0;; ++i) {
            QToolButton* tile = w.findChild<QToolButton*>(QStringLiteral("tile-%1").arg(i));
            if (!tile) break;
            ++tileCount;
            tile->click();
            app.processEvents();
            const util::Easing::Param value = w.value();
            const int got = int(value.type);
            const bool preset = value.type == util::Easing::Type_Custom
                && value.handleIn == util::Easing::kAutoPresetHandleMode
                && value.handleOut == util::Easing::kAutoPresetHandleMode;
            if (preset)
                autoTileIndex = i;
            else
                reachable.insert(got);
            allTilesClick = allTilesClick && (preset || got != int(util::Easing::Type_Custom));
            if (i < 3)
                allTilesAtTableIndex = allTilesAtTableIndex && !preset
                    && got == int(util::Easing::typeAtDisplayIndex(i));
            else if (i == 3)
                allTilesAtTableIndex = allTilesAtTableIndex && preset;
            else
                allTilesAtTableIndex = allTilesAtTableIndex && !preset
                    && got == int(util::Easing::typeAtDisplayIndex(i - 1));
        }
        QSet<int> expected;
        for (int i = 0; i < util::Easing::displayCount(); ++i) {
            const auto type = util::Easing::typeAtDisplayIndex(i);
            if (type != util::Easing::Type_Custom)
                expected.insert(int(type));
        }
        const bool complete = (reachable == expected) && allTilesClick && tileCount > 0;
        fprintf(stderr,
                "#10 tile catalog: %d tile(s) reach %d of the %d offered easings "
                "(missing=%d extra=%d, none is a plain Custom tile=%d) -> %s\n",
                tileCount, int(reachable.size()), int(expected.size()),
                int((expected - reachable).size()), int((reachable - expected).size()),
                (int)allTilesClick, verdict(complete));
        const bool order = allTilesAtTableIndex && autoTileIndex == 3
            && tileCount == util::Easing::displayCount();
        fprintf(stderr,
                "#10 tile order: the three head types, the AUTO PRESET at index 3, then the table's "
                "curve family shifted by one (%d tile(s) for %d non-Custom entries + the preset), "
                "Auto preset is index %d -> %s\n",
                tileCount, util::Easing::displayCount() - 1, autoTileIndex, verdict(order));

        // The preset's checked state and label read the PAIR it writes: with
        // the automatic pair selected only tile-3 is checked, and the glyph
        // button's name is the Param's user-facing name (the handle modes),
        // not the interpolation type "Custom".
        QToolButton* autoTile = w.findChild<QToolButton*>(QStringLiteral("tile-3"));
        bool checkedOnlyAuto = autoTile != nullptr;
        QString presetName;
        if (autoTile) {
            autoTile->click();
            app.processEvents();
            presetName = util::Easing::getParamName(w.value());
            for (int i = 0; i < tileCount; ++i) {
                QToolButton* tile = w.findChild<QToolButton*>(QStringLiteral("tile-%1").arg(i));
                checkedOnlyAuto = checkedOnlyAuto && tile && tile->isChecked() == (i == 3);
            }
            const util::Easing::Param presetValue = w.value();
            // The tile is glyph-only, so its accessible text is the ACTION it
            // performs: automatic handles apply to ONE SEGMENT, so it says the
            // segment AFTER the selected key is made automatic, and still names
            // the pair it writes ("Auto Clamped").
            const QString accessible = autoTile->accessibleName();
            checkedOnlyAuto = checkedOnlyAuto && accessible.contains(presetName)
                && accessible.contains(QLatin1String("segment after the selected key"))
                && presetName == util::Easing::getHandleModeName(util::Easing::kAutoPresetHandleMode)
                && presetName != QLatin1String("Custom")
                // THE PRESET IS THE CLAMPED PAIR, not the unclamped mode that used
                // to be the default: both sides are AutoClamped and neither is Auto.
                && presetValue.handleIn == util::Easing::kAutoPresetHandleMode
                && presetValue.handleOut == util::Easing::kAutoPresetHandleMode
                && presetValue.handleIn != util::Easing::HandleMode_Auto
                && presetValue.handleOut != util::Easing::HandleMode_Auto;
        }
        fprintf(stderr, "#10b Auto preset: checked only tile-3=%d, label='%s' -> %s\n",
                int(checkedOnlyAuto), qPrintable(presetName), verdict(checkedOnlyAuto));

        // ---- #10c ONE canonical Auto image, and its SHAPE ----
        // The tile and the header's quick-easing button draw the same catalog
        // image through gui::prop::autoPreviewContext(); "not equal to Linear" is
        // not enough of a gate (the retired clamped pair's values also drew a
        // non-linear curve - one that dived below the start and overshot the end).
        // Assert the control points the pair DERIVES: the flat-ended ease-in-out
        // (1/3, 0) and (2/3, 1).
        {
            const util::Easing::AutoContext ctx = gui::prop::autoPreviewContext();
            util::Easing::Param preset;
            preset.type = util::Easing::Type_Custom;
            preset.handleIn = util::Easing::kAutoPresetHandleMode;
            preset.handleOut = util::Easing::kAutoPresetHandleMode;
            const QPointF out = util::Easing::controlPoint(preset, ctx.inMode, ctx, 1.0f, 0);
            const QPointF in = util::Easing::controlPoint(preset, ctx.inMode, ctx, 1.0f, 1);
            const bool shape = std::fabs(out.x() - 1.0 / 3.0) < 1e-6
                && std::fabs(out.y() - 0.0) < 1e-6
                && std::fabs(in.x() - 2.0 / 3.0) < 1e-6
                && std::fabs(in.y() - 1.0) < 1e-6;
            fprintf(stderr,
                    "#10c Auto glyph: control points (%g,%g) and (%g,%g), expected (1/3,0)/(2/3,1) -> %s\n",
                    out.x(), out.y(), in.x(), in.y(), verdict(shape));
        }
    }

    // ---- #12 the chip's repaint cache compares the WHOLE AutoContext ----
    // The chip's glyph depends on the TO key's arriving mode, which changes without
    // either neighbour moving; its cache compare is AutoContext::operator==, so a
    // context equal except for inMode must read as a change (and an identical one as
    // no change). Neighbours alone are not the state.
    {
        const util::Easing::AutoContext base = gui::prop::autoPreviewContext();
        util::Easing::AutoContext nextMode = base;
        nextMode.inMode = util::Easing::HandleMode_Auto; // base is AutoClamped
        util::Easing::AutoContext nextValue = base;
        nextValue.next.value = 0.25f;
        util::Easing::AutoContext prevFrame = base;
        prevFrame.prev.frame = -3.0f;
        const util::Easing::AutoContext copy = base;
        const bool differs = (nextMode != base) && (nextValue != base) && (prevFrame != base);
        const bool same = (copy == base) && !(copy != base);
        fprintf(stderr,
                "#12 chip cache: inMode-only differs=%d, next value differs=%d, prev frame differs=%d, "
                "identical equal=%d -> %s\n",
                int(nextMode != base), int(nextValue != base), int(prevFrame != base), int(same),
                verdict(differs && same));
    }

    // ---- #11 the two handle-mode combos ARE the property panel's combo ----
    // Not a field with a role of its own, and not configured by hand: their
    // construction must MATCH prop::ComboItem's combo. The probe links the picker
    // sources directly (not the gui application), so the item class itself cannot
    // be linked in; instead the reference below is ComboItem's own constructor,
    // statement for statement (src/gui/prop/prop_Items.cpp), polished by the very
    // same class-level QComboBox rule the picker uses (we put the picker's sheet
    // on its parent, which is what propertywidget.ssa does for the real ComboItem).
    // Every construction property is then compared FIELD BY FIELD - size policy
    // both axes, minimum/maximum size, font, dynamic properties, local
    // stylesheet, window flags, focus policy, editability - not against
    // hard-coded numbers. A geometry difference would be allowed by the design
    // (the picker's column is narrower); a construction difference is not.
    {
        QComboBox* in = w.findChild<QComboBox*>("handle-in");
        QComboBox* out = w.findChild<QComboBox*>("handle-out");
        const QString css = w.styleSheet();

        // prop::ComboItem::ComboItem (prop_Items.cpp), exactly:
        //   mBox = new QComboBox(aParent);
        //   mBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QWidget refParent;
        refParent.setStyleSheet(css); // the same field rule the panel's combo gets
        QComboBox* ref = new QComboBox(&refParent);
        ref->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        refParent.show();
        refParent.ensurePolished();
        ref->ensurePolished();
        app.processEvents();

        bool plain = in && out && ref;
        auto checkField = [&](const char* aWhat, bool aSame, const QString& aMine,
                              const QString& aReference) {
            if (aSame)
                return;
            plain = false;
            fprintf(stderr, "  #11 MISMATCH %s: picker='%s' ComboItem='%s'\n",
                    aWhat, qPrintable(aMine), qPrintable(aReference));
        };
        const auto compareOne = [&](const char* aWhat, const QComboBox* aMine) {
            if (!aMine) {
                plain = false;
                fprintf(stderr, "  #11 MISMATCH %s: picker combo NOT FOUND\n", aWhat);
                return;
            }
            checkField("class", QLatin1String(aMine->metaObject()->className())
                                    == QLatin1String(ref->metaObject()->className()),
                       QString::fromLatin1(aMine->metaObject()->className()),
                       QString::fromLatin1(ref->metaObject()->className()));
            checkField("sizePolicy", aMine->sizePolicy() == ref->sizePolicy(),
                       QStringLiteral("h=%1 v=%2").arg(int(aMine->sizePolicy().horizontalPolicy()))
                           .arg(int(aMine->sizePolicy().verticalPolicy())),
                       QStringLiteral("h=%1 v=%2").arg(int(ref->sizePolicy().horizontalPolicy()))
                           .arg(int(ref->sizePolicy().verticalPolicy())));
            checkField("minimumSize", aMine->minimumSize() == ref->minimumSize(),
                       QStringLiteral("%1x%2").arg(aMine->minimumWidth()).arg(aMine->minimumHeight()),
                       QStringLiteral("%1x%2").arg(ref->minimumWidth()).arg(ref->minimumHeight()));
            checkField("maximumSize", aMine->maximumSize() == ref->maximumSize(),
                       QStringLiteral("%1x%2").arg(aMine->maximumWidth()).arg(aMine->maximumHeight()),
                       QStringLiteral("%1x%2").arg(ref->maximumWidth()).arg(ref->maximumHeight()));
            checkField("font", aMine->font() == ref->font(),
                       QStringLiteral("%1 %2pt %3px w%4").arg(aMine->font().family())
                           .arg(aMine->font().pointSizeF()).arg(aMine->font().pixelSize())
                           .arg(aMine->font().weight()),
                       QStringLiteral("%1 %2pt %3px w%4").arg(ref->font().family())
                           .arg(ref->font().pointSizeF()).arg(ref->font().pixelSize())
                           .arg(ref->font().weight()));
            checkField("dynamicProperties", aMine->dynamicPropertyNames() == ref->dynamicPropertyNames(),
                       QString::number(int(aMine->dynamicPropertyNames().size())),
                       QString::number(int(ref->dynamicPropertyNames().size())));
            checkField("styleSheet", aMine->styleSheet() == ref->styleSheet(),
                       aMine->styleSheet(), ref->styleSheet());
            checkField("windowFlags", aMine->windowFlags() == ref->windowFlags(),
                       QString::number(int(aMine->windowFlags())),
                       QString::number(int(ref->windowFlags())));
            checkField("focusPolicy", aMine->focusPolicy() == ref->focusPolicy(),
                       QString::number(int(aMine->focusPolicy())),
                       QString::number(int(ref->focusPolicy())));
            checkField("isEditable", aMine->isEditable() == ref->isEditable(),
                       QString::number(int(aMine->isEditable())),
                       QString::number(int(ref->isEditable())));
            // No role attribute: the combo is identified by its CLASS, like
            // ComboItem. (The reference has none either.)
            checkField("role-property", aMine->property("role").isValid() == ref->property("role").isValid(),
                       aMine->property("role").isValid() ? QStringLiteral("set") : QStringLiteral("absent"),
                       ref->property("role").isValid() ? QStringLiteral("set") : QStringLiteral("absent"));
        };
        compareOne("handle-in", in);
        compareOne("handle-out", out);

        const bool appField = css.contains(QStringLiteral("QComboBox {"))
            && css.contains(QStringLiteral("QComboBox::down-arrow"))
            && css.contains(QStringLiteral("caret-down.svg"))
            && !css.contains(QStringLiteral("QComboBox[role="))
            && !css.contains(QStringLiteral("handle-in"))
            && !css.contains(QStringLiteral("handle-out"));
        fprintf(stderr,
                "#11 handle-mode combos vs prop::ComboItem's construction: every field equal=%d, "
                "generic property-field rule (raised/border/caret, no role/id rules)=%d -> %s\n",
                int(plain), int(appField), verdict(plain && appField));
        delete ref;
    }

    // ---- #13 A DERIVED SIDE IS FREED BY EDITING ITS CONTROL POINT ----
    // The bezier panel edits STORED control points. On a derived side (Auto/
    // Vector/AutoClamped) that stored value is never read, so a drag/spin/paste
    // there used to change nothing the curve plays - an inert editor. The rule is
    // the graph drag's own: the edited side becomes Free, the other side is
    // untouched (it keeps deriving if it was). A STORED side (Free/Aligned) keeps
    // its mode - its stored value is what it reads, so the edit lands either way.
    {
        // (a) the spin for the INCOMING side (x2/y2, this param's handleIn).
        EasingPickWidget p{util::Easing::Param()};
        util::Easing::Param draft;
        draft.type = util::Easing::Type_Custom;
        draft.handleIn = util::Easing::HandleMode_Auto;
        draft.handleOut = util::Easing::HandleMode_Aligned;
        p.setValue(draft);
        p.resize(300, p.maxContentHeight());
        p.show();
        app.processEvents();
        QDoubleSpinBox* x2 = p.findChild<QDoubleSpinBox*>("bezier-x2");
        QDoubleSpinBox* x1 = p.findChild<QDoubleSpinBox*>("bezier-x1");
        bool spinFreed = x2 && x1;
        if (spinFreed) {
            x2->setValue(0.9);
            app.processEvents();
            spinFreed = p.value().handleIn == util::Easing::HandleMode_Free
                && p.value().handleOut == util::Easing::HandleMode_Aligned
                && std::fabs(double(p.value().cubicBezier.x2) - 0.9) < 1e-4;
            // ...and a STORED side is not freed by its own edit.
            x1->setValue(0.2);
            app.processEvents();
            spinFreed = spinFreed
                && p.value().handleOut == util::Easing::HandleMode_Aligned
                && std::fabs(double(p.value().cubicBezier.x1) - 0.2) < 1e-4;
        }
        fprintf(stderr,
                "#13a derived-side spin: handleIn=Free, handleOut keeps Aligned, values stored=%d -> %s\n",
                int(spinFreed), verdict(spinFreed));
    }
    {
        // (b) a real canvas DRAG on the outgoing control point (x1/y1 -> handleOut).
        EasingPickWidget p{util::Easing::Param()};
        util::Easing::Param preset;
        preset.type = util::Easing::Type_Custom;
        preset.handleIn = util::Easing::HandleMode_Auto;
        preset.handleOut = util::Easing::HandleMode_Auto;
        p.setValue(preset);
        p.show();
        p.setCustom(); // the preset routes to the catalog; open the bezier panel
        app.processEvents();
        auto* stage = p.findChild<BezierCurveEditor*>("bezier-stage");
        bool dragFreed = stage && stage->width() > 0 && stage->height() > 0;
        if (dragFreed) {
            stage->syncFromBezier();
            stage->update();
            app.processEvents();
            const QPoint from(stage->m_points[BezierCurveEditor::ControlPoint1].toPoint());
            const QPoint to(from.x() + 8, from.y() - 12);
            QMouseEvent press(QEvent::MouseButtonPress, from, stage->mapToGlobal(from),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &press);
            QMouseEvent move(QEvent::MouseMove, to, stage->mapToGlobal(to),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &move);
            QMouseEvent release(QEvent::MouseButtonRelease, to, stage->mapToGlobal(to),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &release);
            app.processEvents();
            dragFreed = p.value().handleOut == util::Easing::HandleMode_Free
                && p.value().handleIn == util::Easing::HandleMode_Auto;
        }
        fprintf(stderr,
                "#13b derived-side drag: handleOut=Free, the OTHER (Auto) side untouched=%d -> %s\n",
                int(dragFreed), verdict(dragFreed));
    }

    // ---- #14 THE AUTO PRESET IS SHOWN AS CURRENT (catalog, tile checked) ----
    // Entering a Custom param used to switch straight to the bezier panel, where
    // the Auto tile cannot be seen - so a preset selected by the tile (or loaded
    // from a key) could not say it was current. The preset routes to the catalog;
    // an explicit Custom click still opens the bezier panel without changing it.
    {
        util::Easing::Param preset;
        preset.type = util::Easing::Type_Custom;
        preset.handleIn = util::Easing::kAutoPresetHandleMode;
        preset.handleOut = util::Easing::kAutoPresetHandleMode;
        EasingPickWidget p{util::Easing::Param()};
        p.setValue(preset);
        p.show();
        app.processEvents();
        QWidget* trow = p.findChild<QWidget*>("trow");
        QWidget* bezier = p.findChild<QWidget*>("bezier-panel");
        QToolButton* autoTile = p.findChild<QToolButton*>("tile-3");
        const bool catalog = trow && bezier && autoTile
            && trow->isVisible() && !bezier->isVisible() && autoTile->isChecked();
        // The Custom segment still reaches the controls, keeping the pair.
        p.setCustom();
        app.processEvents();
        const bool forced = bezier && bezier->isVisible()
            && p.value() == preset;
        fprintf(stderr,
                "#14 Auto preset routed to the catalog (tile checked)=%d, Custom still opens the "
                "bezier panel without changing the pair=%d -> %s\n",
                int(catalog), int(forced), verdict(catalog && forced));
    }

    // ---- #15 A DISCRETE MODE CHANGE APPLIES WHILE THE POPUP IS OPEN ----
    // A tile / segment / mode-combo / typed-bezier / paste edit used to be held
    // until the popup closed, so the document (and the graph behind it) did not
    // move while the menu was up. Every discrete edit now emits applied at once;
    // a continuous drag still folds into ONE commit when the popup closes.
    {
        util::Easing::Param draft;
        draft.type = util::Easing::Type_Custom;
        draft.handleIn = util::Easing::HandleMode_Aligned;
        draft.handleOut = util::Easing::HandleMode_Aligned;

        EasingPickWidget p{draft};
        p.setValue(draft);
        int applied = 0;
        util::Easing::Param last;
        QObject::connect(&p, &EasingPickWidget::applied,
                         [&applied, &last](util::Easing::Param aParam) { ++applied; last = aParam; });
        p.show();
        app.processEvents();

        p.setHandleMode(true, util::Easing::HandleMode_Auto);
        app.processEvents();
        const bool modeLive = applied == 1 && last.handleIn == util::Easing::HandleMode_Auto
            && p.isVisible();
        p.setHandleMode(false, util::Easing::HandleMode_AutoClamped);
        app.processEvents();
        const bool secondLive = applied == 2 && last.handleOut == util::Easing::HandleMode_AutoClamped;
        p.hide();
        app.processEvents();
        const bool noDoubleCommit = applied == 2; // close does not re-send what was committed

        // A DRAG is different: it reports live (changed) but commits once, on close.
        EasingPickWidget drag{draft};
        drag.setValue(draft);
        int dragApplied = 0;
        float dragWeight = -1.0f;
        QObject::connect(&drag, &EasingPickWidget::applied,
                         [&dragApplied, &dragWeight](util::Easing::Param aParam) {
                             ++dragApplied;
                             dragWeight = aParam.weight;
                         });
        drag.show();
        app.processEvents();
        drag.setWeight(0.5f);
        app.processEvents();
        const bool dragDeferred = dragApplied == 0;
        drag.hide();
        app.processEvents();
        const bool dragOnClose = dragApplied == 1 && dragWeight == 0.5f;

        const bool ok = modeLive && secondLive && noDoubleCommit && dragDeferred && dragOnClose;
        fprintf(stderr,
                "#15 live apply: mode while open=%d, second mode=%d, no re-commit on close=%d, "
                "drag deferred=%d, drag one commit on close=%d -> %s\n",
                int(modeLive), int(secondLive), int(noDoubleCommit), int(dragDeferred),
                int(dragOnClose), verdict(ok));
    }

    // ---- #16 THE HEADER NAMES THE PARAM, NOT THE TYPE ----
    // The picker's own title used to be the generic "Easing" (and the top of the
    // chip area read the TYPE elsewhere); a Custom curve must be named by its two
    // handle modes, exactly like the chip and every tile. Both-Auto reads "Auto",
    // the preset reads "Auto Clamped" - never "Custom".
    {
        util::Easing::Param bothAuto;
        bothAuto.type = util::Easing::Type_Custom;
        bothAuto.handleIn = util::Easing::HandleMode_Auto;
        bothAuto.handleOut = util::Easing::HandleMode_Auto;
        EasingPickWidget p{util::Easing::Param()};
        p.setValue(bothAuto);
        p.show();
        app.processEvents();
        QLabel* title = p.findChild<QLabel*>(QStringLiteral("popover-title"));
        const bool autoName = title
            && title->text() == util::Easing::getParamName(bothAuto)
            && title->text() == QLatin1String("Auto")
            && title->text() != util::Easing::getTypeName(util::Easing::Type_Custom);

        p.setAutoHandlesPreset();
        app.processEvents();
        QLabel* titlePreset = p.findChild<QLabel*>(QStringLiteral("popover-title"));
        const bool presetName = titlePreset
            && titlePreset->text() == util::Easing::getParamName(p.value())
            && titlePreset->text() == util::Easing::getHandleModeName(util::Easing::kAutoPresetHandleMode)
            && titlePreset->text() != util::Easing::getTypeName(util::Easing::Type_Custom);

        fprintf(stderr,
                "#16 picker title: both-Auto reads '%s' (not Custom)=%d, preset reads '%s'=%d -> %s\n",
                title ? qPrintable(title->text()) : "<none>", int(autoName),
                titlePreset ? qPrintable(titlePreset->text()) : "<none>", int(presetName),
                verdict(autoName && presetName));
    }

    // ---- #17 A BEZIER HANDLE DRAG REPORTS AND APPLIES EVERY STEP ----
    // The complaint was "in Custom, changing the bezier handles only updates once
    // I close the easing menu": the canvas wrote the draft on every move but told
    // nobody, so the chip/document/graph waited for hideEvent. A drag now streams
    // each pointer move (liveEdited, with changed alongside) while the owner keeps
    // ONE command open across the gesture - so no `applied` fires per step and the
    // close cannot commit a second time.
    {
        util::Easing::Param draft;
        draft.type = util::Easing::Type_Custom;
        draft.handleIn = util::Easing::HandleMode_Aligned;
        draft.handleOut = util::Easing::HandleMode_Aligned;

        EasingPickWidget p{draft};
        p.setValue(draft);
        p.show();
        p.setCustom(); // Custom non-preset already routes there; force the layout
        app.processEvents();
        int started = 0, steps = 0, ended = 0, changedFires = 0, appliedFires = 0;
        float stepX = -99.0f;
        QObject::connect(&p, &EasingPickWidget::liveEditStarted, [&] { ++started; });
        QObject::connect(&p, &EasingPickWidget::liveEdited,
                         [&](util::Easing::Param aParam) { ++steps; stepX = aParam.cubicBezier.x1; });
        QObject::connect(&p, &EasingPickWidget::liveEditEnded, [&] { ++ended; });
        QObject::connect(&p, &EasingPickWidget::changed,
                         [&](util::Easing::Param) { ++changedFires; });
        QObject::connect(&p, &EasingPickWidget::applied,
                         [&](util::Easing::Param) { ++appliedFires; });

        auto* stage = p.findChild<BezierCurveEditor*>("bezier-stage");
        bool ok = stage && stage->width() > 0 && stage->height() > 0;
        bool openedAtPress = false, firstStep = false, secondStep = false;
        bool closedAtRelease = false, draftFollowed = false, nothingReApplied = false;
        if (ok) {
            stage->syncFromBezier();
            stage->update();
            app.processEvents();
            const QPoint from(stage->m_points[BezierCurveEditor::ControlPoint1].toPoint());
            const QPoint mid(from.x() + 6, from.y() - 8);
            const QPoint end(from.x() + 12, from.y() - 18);
            QMouseEvent press(QEvent::MouseButtonPress, from, stage->mapToGlobal(from),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &press);
            openedAtPress = started == 1 && steps == 0 && changedFires == 0;

            QMouseEvent move1(QEvent::MouseMove, mid, stage->mapToGlobal(mid),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &move1);
            firstStep = steps == 1 && changedFires >= 1 && appliedFires == 0 && started == 1;

            QMouseEvent move2(QEvent::MouseMove, end, stage->mapToGlobal(end),
                              Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &move2);
            secondStep = steps == 2 && appliedFires == 0;
            // The draft the owner was handed IS the draft the widget holds - the
            // model is written per step, not deferred to the release/close.
            draftFollowed = std::fabs(double(stepX - p.value().cubicBezier.x1)) < 1e-6;

            QMouseEvent release(QEvent::MouseButtonRelease, end, stage->mapToGlobal(end),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &release);
            // The session stays OPEN across the gesture (it spans the popup), so the
            // release closes the gesture, not the command.
            closedAtRelease = ended == 0 && steps == 2 && appliedFires == 0;

            p.hide();
            app.processEvents();
            // Every step was already written by the open command, which closes with
            // the popup: one undo entry, and no `applied` re-commit on the way out.
            nothingReApplied = ended == 1 && appliedFires == 0;
        }
        ok = ok && openedAtPress && firstStep && secondStep && draftFollowed
            && closedAtRelease && nothingReApplied;
        fprintf(stderr,
                "#17 bezier drag: opens at press=%d, step1 live=%d, step2 live=%d, "
                "draft followed=%d, session open after release=%d, closed once on hide=%d -> %s\n",
                int(openedAtPress), int(firstStep), int(secondStep), int(draftFollowed),
                int(closedAtRelease), int(nothingReApplied), verdict(ok));
    }

    // ---- #18 ONE TYPED SPIN VALUE IS ONE GESTURE (AND ONE UNDO ENTRY) ----
    // A spin's valueChanged fires per keystroke/step. Streaming each one is what
    // makes the graph follow the typing; beginning/ending ONE gesture around the
    // edit is what stops five digits leaving five undo entries.
    {
        util::Easing::Param draft;
        draft.type = util::Easing::Type_Custom;
        draft.handleIn = util::Easing::HandleMode_Aligned;
        draft.handleOut = util::Easing::HandleMode_Aligned;
        draft.cubicBezier = { 0.25f, 0.1f, 0.75f, 0.9f };

        EasingPickWidget p{draft};
        p.setValue(draft);
        p.resize(300, p.maxContentHeight());
        p.show();
        app.processEvents();
        QDoubleSpinBox* x1 = p.findChild<QDoubleSpinBox*>("bezier-x1");
        int started = 0, steps = 0, ended = 0, appliedFires = 0;
        QObject::connect(&p, &EasingPickWidget::liveEditStarted, [&] { ++started; });
        QObject::connect(&p, &EasingPickWidget::liveEdited,
                         [&](util::Easing::Param) { ++steps; });
        QObject::connect(&p, &EasingPickWidget::liveEditEnded, [&] { ++ended; });
        QObject::connect(&p, &EasingPickWidget::applied,
                         [&](util::Easing::Param) { ++appliedFires; });

        bool ok = x1 != nullptr;
        bool beganOnce = false, bothSteps = false, finishedOnce = false, oneUndo = false;
        if (ok) {
            // The editor is authoritative: once the panel has painted, the draft's
            // curve is the one the canvas clamped. Read that as the pre-typing
            // value, so the test never depends on the size the panel was built at.
            const float before = p.value().cubicBezier.x1;
            // Two keystrokes of ONE typed value: the gesture opens on the first,
            // the second is a step of the same gesture (no new session).
            x1->setValue(0.31);
            app.processEvents();
            beganOnce = started == 1 && steps == 1 && appliedFires == 0;
            x1->setValue(0.37);
            app.processEvents();
            bothSteps = started == 1 && steps == 2 && appliedFires == 0;

            // Finishing the edit folds the popup's own history into ONE entry, so a
            // single undo returns to the pre-typing value. The streamed command
            // stays open across the popup (closed by the hide below), so no
            // `applied` fires at either point.
            QMetaObject::invokeMethod(x1, "editingFinished");
            app.processEvents();
            finishedOnce = ended == 0 && appliedFires == 0;
            p.undo();
            app.processEvents();
            oneUndo = std::fabs(double(p.value().cubicBezier.x1 - before)) < 1e-4;
            p.hide();
            app.processEvents();
            finishedOnce = finishedOnce && ended == 1 && appliedFires == 0;
        }
        ok = ok && beganOnce && bothSteps && finishedOnce && oneUndo;
        fprintf(stderr,
                "#18 typed spin: one gesture opened=%d, both steps streamed=%d, "
                "editing-finished closes=%d, one undo restores the pre-typing value=%d -> %s\n",
                int(beganOnce), int(bothSteps), int(finishedOnce), int(oneUndo), verdict(ok));
    }

    // ---- #19 THE POPUP FOLLOWS THE DOCUMENT WHILE IT IS OPEN ----
    // The user's report: "the sync between the graph editor and the easing menu's
    // Custom tab is only one way". The popup read the selection when it opened and
    // never again, so a handle drag / mode change / key selection change made in the
    // graph while it was open never reached it. The owner now pushes the document's
    // state (setShownValue) on every selection/document change, and a gesture in the
    // POPUP wins until it ends. This drives the widget alone - the host's push is the
    // same call EasingItem::refreshShown makes.
    {
        util::Easing::Param draft;
        draft.type = util::Easing::Type_Custom;
        draft.handleIn = util::Easing::HandleMode_Aligned;
        draft.handleOut = util::Easing::HandleMode_Aligned;
        draft.cubicBezier = { 0.25f, 0.1f, 0.75f, 0.9f };

        EasingPickWidget p{draft};
        p.resize(300, p.maxContentHeight());
        p.show();
        p.setCustom(); // the Custom tab is the one that follows the graph
        app.processEvents();

        int changedFires = 0, appliedFires = 0;
        QObject::connect(&p, &EasingPickWidget::changed, [&](util::Easing::Param) { ++changedFires; });
        QObject::connect(&p, &EasingPickWidget::applied, [&](util::Easing::Param) { ++appliedFires; });

        const util::Easing::AutoContext ctx;
        auto* stage = p.findChild<BezierCurveEditor*>("bezier-stage");
        bool uiFollowed = false;

        // (a) A GRAPH EDIT WHILE THE POPUP IS IDLE IS ADOPTED, and the panel shows it.
        util::Easing::Param fromGraph = draft;
        fromGraph.cubicBezier.x1 = 0.55f;
        fromGraph.cubicBezier.y1 = 0.42f;
        const bool adopted = p.setShownValue(fromGraph, ctx) && p.value() == fromGraph;
        app.processEvents();
        if (stage) {
            stage->syncFromBezier();
            const QPointF dot = stage->m_points[BezierCurveEditor::ControlPoint1];
            // The panel's own canvas draws the adopted control point: x is a
            // fraction of the [0,1] box, so it lies past 0.5 of the width.
            uiFollowed = stage->width() > 0
                && std::fabs(dot.x() / stage->width() - 0.55) < 0.06;
        }
        // A re-read is NOT an edit: nothing emitted, nothing committed...
        const bool emittedNothing = changedFires == 0 && appliedFires == 0;
        // ...and the same state again is refused (idempotent).
        const bool idempotent = !p.setShownValue(fromGraph, ctx);

        // (b) A GESTURE IN THE POPUP WINS UNTIL IT ENDS. While a bezier handle is
        // being dragged, a push is refused and the draft stays the pointer's value;
        // once the gesture is over a push is taken again (an undo from another view,
        // for instance, must reach it).
        bool refusedMidDrag = false, dragValueKept = false, adoptedAfterDrag = false;
        if (stage && stage->width() > 0 && stage->height() > 0) {
            const QPoint from(stage->m_points[BezierCurveEditor::ControlPoint1].toPoint());
            const QPoint to(from.x() + 10, from.y() - 12);
            QMouseEvent press(QEvent::MouseButtonPress, from, stage->mapToGlobal(from),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &press);
            QMouseEvent move(QEvent::MouseMove, to, stage->mapToGlobal(to),
                             Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &move);
            app.processEvents();

            util::Easing::Param midDrag = draft;
            midDrag.cubicBezier.x1 = 0.05f;
            midDrag.cubicBezier.y1 = 0.85f;
            const util::Easing::Param held = p.value();
            refusedMidDrag = p.isEditing() && !p.setShownValue(midDrag, ctx);
            dragValueKept = p.value() == held && std::fabs(held.cubicBezier.x1 - 0.05f) > 1e-6;

            QMouseEvent release(QEvent::MouseButtonRelease, to, stage->mapToGlobal(to),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(stage, &release);
            app.processEvents();

            util::Easing::Param afterDrag = p.value(); // the document holds the drag's value
            afterDrag.cubicBezier.x2 = 0.33f;           // another view edited the other side
            adoptedAfterDrag = !p.isEditing() && p.setShownValue(afterDrag, ctx)
                && p.value() == afterDrag;
        }

        p.hide(); // closes the session the drag opened
        app.processEvents();

        // (c) A CLOSED POPUP IS NEVER PUSHED INTO: the next open loads its own state.
        const bool closedRefuses = !p.setShownValue(draft, ctx);

        const bool ok = adopted && uiFollowed && emittedNothing && idempotent
            && refusedMidDrag && dragValueKept && adoptedAfterDrag && closedRefuses;
        fprintf(stderr,
                "#19 two-way picker: graph value adopted=%d, panel showed it=%d, re-read is not an "
                "edit=%d, idempotent=%d, refused mid-drag=%d (draft kept=%d), adopted after the "
                "drag=%d, closed refused=%d -> %s\n",
                int(adopted), int(uiFollowed), int(emittedNothing), int(idempotent),
                int(refusedMidDrag), int(dragValueKept), int(adoptedAfterDrag),
                int(closedRefuses), verdict(ok));
    }

    fprintf(stderr, "picker_probe: %d mismatch%s\n", gMismatches, gMismatches == 1 ? "" : "es");
    return gMismatches == 0 ? 0 : 1;
}
