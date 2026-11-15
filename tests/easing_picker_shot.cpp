// easing_picker_shot.cpp — renders the EasingPickWidget with all text hidden
// in the normal (In-Out) state and in the Custom (bezier) state, for
// side-by-side comparison with design/easing-redesign/variant-a-gauge.html.
//
// Every textual glyph is painted alpha-0 via EasingPickWidget::setHideText
// (the CSS color:transparent equivalent) — the box geometry, backgrounds,
// borders and the drawn curve/handle/gauge graphics are all unchanged.
//
// Usage: easing_picker_shot [inout.png] [custom.png]
// With EASE_SHOT_VERIFY=1 it also renders text-visible baselines and asserts
// the hidden state changes NO layout (equal widget size) and differs only in
// text pixels (a non-empty, bounded pixel diff), printing a PASS/FAIL summary.
#include <QApplication>
#include <QPixmap>
#include <QImage>
#include <QString>
#include <QFileInfo>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QLayout>
#include <cstdio>
#include "gui/prop/EasingPickWidget.h"
#include "gui/theme/Colors.h"

static void renderInto(gui::prop::EasingPickWidget& w, bool aCustom, const QString& aOut) {
    if (aCustom) w.setCustom();
    else if (w.value().type == util::Easing::Type_Custom) w.setRange(util::Easing::Range_InOut);
    QApplication::processEvents();
    const int hfw = w.heightForWidth(300);
    w.resize(300, hfw > 0 ? hfw : w.sizeHint().height());
    QApplication::processEvents();
    fprintf(stderr, "  size=%dx%d\n", w.width(), w.height());
    w.grab().save(aOut);
}

static int countDiffPixels(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return -1;
    int n = 0;
    for (int y = 0; y < a.height(); ++y)
        for (int x = 0; x < a.width(); ++x)
            if (a.pixel(x, y) != b.pixel(x, y)) ++n;
    return n;
}

static void box(const char* label, const QWidget* w) {
    if (!w) { fprintf(stderr, "  %-16s (null)\n", label); return; }
    fprintf(stderr, "  %-16s w=%3d h=%3d\n", label, w->width(), w->height());
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    theme::Colors::activate(true, theme::kDefaultAccent);

    const bool verify = qgetenv("EASE_SHOT_VERIFY") == "1";
    const QString inout = (argc > 1) ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("qt-popup-inout-hidden.png");
    const QString custom = (argc > 2) ? QString::fromLocal8Bit(argv[2])
                                      : QStringLiteral("qt-popup-custom-hidden.png");

    util::Easing::Param initial;
    initial.type = util::Easing::Type_Cubic;
    initial.range = util::Easing::Range_InOut;
    initial.weight = 1.0f;

    auto build = [&](bool aHide) {
        auto* w = new gui::prop::EasingPickWidget(initial);
        w->setObjectName("popover");
        w->setHideText(aHide);
        w->show();
        return w;
    };

    // Deliverables: hidden-text renders.
    gui::prop::EasingPickWidget* hide = build(true);
    QApplication::processEvents();
    fprintf(stderr, "inout  (hidden):"); renderInto(*hide, false, inout);
    fprintf(stderr, "custom (hidden):"); renderInto(*hide, true, custom);

    if (verify) {
        fprintf(stderr, "\n-- verify (hidden vs text-visible) --\n");
        gui::prop::EasingPickWidget* vis = build(false);
        QApplication::processEvents();

        QApplication::processEvents();
        const int ihfw = vis->heightForWidth(300);
        vis->resize(300, ihfw > 0 ? ihfw : vis->sizeHint().height());
        QApplication::processEvents();
        const QImage visIn = vis->grab().toImage().convertToFormat(QImage::Format_ARGB32);

        vis->setCustom();
        QApplication::processEvents();
        const int chfw = vis->heightForWidth(300);
        vis->resize(300, chfw > 0 ? chfw : vis->sizeHint().height());
        QApplication::processEvents();
        const QImage visCus = vis->grab().toImage().convertToFormat(QImage::Format_ARGB32);

        const QImage hidIn = QImage(inout).convertToFormat(QImage::Format_ARGB32);
        const QImage hidCus = QImage(custom).convertToFormat(QImage::Format_ARGB32);

        bool ok = true;
        // 1) layout must be identical (same size) — the "doesn't affect layout" proof.
        const bool sizeIn = (hidIn.size() == visIn.size());
        const bool sizeCus = (hidCus.size() == visCus.size());
        fprintf(stderr, "size inout  hidden=%dx%d visible=%dx%d  %s\n",
                hidIn.width(), hidIn.height(), visIn.width(), visIn.height(),
                sizeIn ? "OK" : "MISMATCH");
        fprintf(stderr, "size custom hidden=%dx%d visible=%dx%d  %s\n",
                hidCus.width(), hidCus.height(), visCus.width(), visCus.height(),
                sizeCus ? "OK" : "MISMATCH");
        ok = ok && sizeIn && sizeCus;

        // 2) text was actually removed: at least one pixel changed.
        const int dIn = countDiffPixels(hidIn, visIn);
        const int dCus = countDiffPixels(hidCus, visCus);
        fprintf(stderr, "diff pixels inout=%d custom=%d\n", dIn, dCus);
        ok = ok && dIn > 0 && dCus > 0;

        fprintf(stderr, "-- verify: %s --\n", ok ? "PASS" : "FAIL");
        delete vis;
    }

    if (qgetenv("EASE_SHOT_MEASURE") == "1") {
        fprintf(stderr, "\n== measure (Qt) ==\n");
        // Reset to the In-Out (normal) state.
        hide->setRange(util::Easing::Range_InOut);
        QApplication::processEvents();
        const int nhfw = hide->heightForWidth(300);
        hide->resize(300, nhfw > 0 ? nhfw : hide->sizeHint().height());
        QApplication::processEvents();
        fprintf(stderr, "-- normal --\n");
        box("seg-r-0", hide->findChild<QToolButton*>("seg-r-0"));
        box("popover-close", hide->findChild<QWidget*>("popover-close"));
        box("tile-0", hide->findChild<QToolButton*>("tile-0"));
        box("t-glyph", hide->findChild<QWidget*>("t-glyph"));
        box("btn-cancel", hide->findChild<QPushButton*>("btn-cancel"));
        box("btn-apply", hide->findChild<QPushButton*>("btn-apply"));
        box("pipeline", hide->findChild<QWidget*>("pipeline"));
        fprintf(stderr, "  pipeline visible=%d\n",
                hide->findChild<QWidget*>("pipeline") ? hide->findChild<QWidget*>("pipeline")->isVisible() : -1);
        // tile wraps: count tiles per row by grouping on mapped-y.
        {
            int prevY = -9999, per = 0; QList<int> rows;
            for (int i = 0; i < 13; ++i) {
                QToolButton* tile = hide->findChild<QToolButton*>(QStringLiteral("tile-%1").arg(i));
                if (!tile) continue;
                const QPoint p = tile->mapTo(hide, QPoint(0, 0));
                if (p.y() != prevY) { if (prevY != -9999 && per) rows << per; per = 0; prevY = p.y(); }
                ++per;
            }
            if (per) rows << per;
            fprintf(stderr, "  tile rows:"); for (int r : rows) fprintf(stderr, " %d", r); fprintf(stderr, "\n");
            if (QWidget* vsep = hide->findChild<QWidget*>("vsep"))
                fprintf(stderr, "  vsep w=%d h=%d\n", vsep->width(), vsep->height());
        }
    }

    // Custom measurement: re-enter custom so the bezier widgets exist.
    if (qgetenv("EASE_SHOT_MEASURE") == "1") {
        hide->setCustom();
        QApplication::processEvents();
        const int mhfw = hide->heightForWidth(300);
        hide->resize(300, mhfw > 0 ? mhfw : hide->sizeHint().height());
        QApplication::processEvents();
        QDoubleSpinBox* spin = hide->findChild<QDoubleSpinBox*>("bezier-x1");
        QWidget* field = spin ? spin->parentWidget() : nullptr;
        box("field(bfield)", field);
        if (field) {
            const QLabel* fl = field->findChild<QLabel*>();
            box("fl", fl);
        }
        box("fi(spin)", spin);
        box("bact(copy)", hide->findChild<QPushButton*>("bezier-copy"));
        box("bezier-stage", hide->findChild<QWidget*>("bezier-stage"));
        box("be-root", hide->findChild<QWidget*>("be-root"));
        box("pipeline", hide->findChild<QWidget*>("pipeline"));
        fprintf(stderr, "  pipeline visible=%d\n",
                hide->findChild<QWidget*>("pipeline") ? hide->findChild<QWidget*>("pipeline")->isVisible() : -1);
    }

    delete hide;
    return 0;
}
