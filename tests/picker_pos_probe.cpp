// picker_pos_probe.cpp — regression for the Easing picker popover position:
// toggling between the normal (non-Custom) view and Custom must NOT nudge the
// popup to the right, and the tall Custom view must not clip off the bottom of
// the screen.
//
// It opens the popover through the SHIPPING placement — EasingPickWidget::popUpAt,
// which is the right-edge clamp, the 0-opacity realize-the-tall-Custom dance, the
// transient parent and the anchor re-assert on refit — then toggles Custom<->normal
// and prints geometry().x() and whether the Custom bottom stays on-screen, so what
// is measured is the popup the application pops and not a copy of its maths.
//
// Same source set as the layout gate / behavior probe.
#include <QApplication>
#include <QWidget>
#include <QScreen>
#include <QTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <cstdio>
#include "gui/prop/EasingPickWidget.h"
#include "gui/theme/Colors.h"
#include "util/Easing.h"

using namespace gui::prop;

static void settle(QApplication& app, int ms = 60) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
    app.processEvents();
}

static void report(const char* label, EasingPickWidget& w) {
    fprintf(stderr, "%-20s x=%d y=%d w=%d h=%d\n",
            label, w.geometry().x(), w.geometry().y(), w.width(), w.height());
}

// Opens the popover the way the application does. The placement itself is the
// picker's (EasingPickWidget::popUpAt): it needs the anchor, the widget the anchor
// belongs to (for the screen and the transient parent) and the value to show, and it
// runs its own realize/clamp/re-assert dance asynchronously - so settle() here just
// lets those zero-timers fire before anything is measured.
static void openPickerAt(QApplication& app, EasingPickWidget& w,
                         QWidget& chip, const QPoint& anchor) {
    w.popUpAt(anchor, &chip, w.value(), util::Easing::AutoContext());
    settle(app, 50);
}

static void assertCustomFits(QApplication& app, EasingPickWidget& w,
                             const QScreen* scr, const char* tag) {
    w.setCustom();
    // Settle the deferred re-fit (singleShot 0) and the 16ms motion timer so
    // the popup reaches its final mode height before we measure it.
    for (int i = 0; i < 4; ++i) { settle(app, 80); }
    const QRect avail = scr->availableGeometry();
    const int top = w.geometry().y(), hh = w.height();
    const int bottom = top + hh - 1;
    report(tag, w);
    fprintf(stderr, "   [%s] top=%d h=%d custom bottom=%d avail.bottom=%d -> %s\n",
            tag, top, hh, bottom, avail.bottom(),
            bottom <= avail.bottom() ? "FITS" : "CLIPS-BOTTOM");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    theme::Colors::activate(true, theme::kDefaultAccent);

    util::Easing::Param initial;
    initial.type = util::Easing::Type_Cubic;
    initial.range = util::Easing::Range_InOut;
    initial.weight = 1.0f;

    const QScreen* scr = QGuiApplication::primaryScreen();
    const QRect avail = scr->availableGeometry();

    // A real shown chip so mapToGlobal/transient-parent work like the app.
    QWidget chip;
    chip.resize(300, 24);
    const QPoint anchor(avail.left() + 700, avail.top() + 200);
    chip.move(anchor); chip.show(); app.processEvents();

    EasingPickWidget w(initial);
    w.setWindowFlags(w.windowFlags() | Qt::Popup | Qt::FramelessWindowHint);
    w.setAttribute(Qt::WA_TranslucentBackground, true);

    openPickerAt(app, w, chip, anchor);
    report("open(normal)", w);
    const int x0 = w.geometry().x();
    const int top0 = w.geometry().y();
    int maxTopDrift = 0;
    for (int i = 0; i < 3; ++i) {
        w.setCustom(); settle(app, 60); report("custom", w);
        maxTopDrift = qMax(maxTopDrift, qAbs(w.geometry().y() - top0));
        w.setRange(util::Easing::Range_InOut); settle(app, 60); report("normal", w);
        maxTopDrift = qMax(maxTopDrift, qAbs(w.geometry().y() - top0));
    }
    const int drift = w.geometry().x() - x0;
    fprintf(stderr, "RESULT: x0=%d xFinal=%d horizontal-drift=%d top0=%d maxTopDrift=%d -> %s\n",
            x0, w.geometry().x(), drift, top0, maxTopDrift,
            (drift == 0 && maxTopDrift == 0) ? "NO DRIFT" : "DRIFTED");
    fprintf(stderr, "anchor=%s reasserted?(%d)\n",
            qPrintable(QString("(%1,%2)").arg(w.anchorPoint().x()).arg(w.anchorPoint().y())),
            (int)(w.anchorPoint() == QPoint(x0, w.geometry().y())));

    // Bottom-clip regression: mid-screen (no vertical room issue) and near the
    // bottom edge, Custom must stay fully on-screen.
    assertCustomFits(app, w, scr, "custom-mid");
    w.hide(); w.setValue(initial);
    const QPoint anchorBR(avail.right() - 30, avail.bottom() - 40);
    chip.move(anchorBR); chip.show(); app.processEvents();
    openPickerAt(app, w, chip, anchorBR);
    report("open(bottom-right)", w);
    assertCustomFits(app, w, scr, "custom-bottomright");

    return 0;
}
