// picker_probe.cpp — exercise the EasingPickWidget's interactive behaviors
// (mode switch, slider drag-outside, knob clip, bezier spin input) and print
// the results. Compiled directly against the widget sources like the layout
// gate, so it links only util + Qt Widgets.
#include <QApplication>
#include <QWidget>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QDebug>
#include <cstdio>
#include "gui/prop/EasingPickWidget.h"
#include "gui/theme/Colors.h"
#include "util/Easing.h"

using namespace gui::prop;

static int hfw_h(EasingPickWidget& w) {
    const int f = w.heightForWidth(300);
    return f > 0 ? f : w.sizeHint().height();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    theme::Colors::activate(true, theme::kDefaultAccent);

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
    const qreal cx = sliderImg.width() / 2.0;
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
                strip->width() - 1 - dotR, dotT, ok ? "OK" : "MISMATCH");
    }

    // ---- #8 close X: phosphor stroke is thin (16/256 of the 14px box) ----
    QWidget* closeBtn = w.findChild<QWidget*>("popover-close");
    const QImage xi = closeBtn->grab().toImage();
    int xw = 0;
    const int midY = xi.height() / 2;
    for (int x = 0; x < xi.width(); ++x) {
        const QRgb p = xi.pixel(x, midY);
        if (qRed(p) > 90 && qGreen(p) > 90 && qBlue(p) > 90) { ++xw; }
    }
    // the diagonal crosses the mid row in ~strokeWidth/sin(45deg) px:
    // phosphor 0.875px stroke -> ~1-2 painted px; the old 2.5px stroke -> ~4.
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
    const bool ok = fi && fiH == 20 && hasHover && hasFocus && hasPressed && hasBorder;
    fprintf(stderr, "#9 fi: h=%d hover=%d focus=%d pressed=%d borderRule=%d -> %s\n",
            fiH, (int)hasHover, (int)hasFocus, (int)hasPressed, (int)hasBorder,
            ok ? "OK" : "MISMATCH");

    return 0;
}
