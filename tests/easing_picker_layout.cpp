// easing_picker_layout.cpp — code-based layout verification for the locked
// EasingPickWidget.
//
// Renders the picker offscreen (QT_QPA_PLATFORM=offscreen), measures the
// geometry of every named sub-widget, and asserts each box is within the
// reference tolerance recorded in tests/golden/easing_picker_geometry.json
// (measured from the browser mock in design/easing-redesign/pick-ref.html).
//
// This is the layout gate: any drift in the segmented control, the tile
// catalog wrap, the blend row, or the footer shows up as a failed assertion
// here — no eyeballing. It also writes the rendered widget to
// easing_picker_layout.png for the record.
//
// Beyond the golden's boxes it asserts what a box list cannot see: the tile
// catalog IS the display table's non-Custom entries position by position (read
// back by clicking each tile), the hairline separator sits between tile-3 and
// tile-4 - i.e. right after Auto, closing the four derived entries - and the
// Auto tile's glyph is NOT the identity curve the Linear tile draws. Those three
// are computed from live geometry/pixels, so a pure REORDER of the catalog
// (which leaves every box where it was) is still caught without regenerating the
// golden.
//
// Usage: easing_picker_layout [reference.json] [output.png]
#include <QApplication>
#include <QWidget>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDebug>
#include <QLabel>
#include <QToolButton>
#include <cmath>
#include <cstdio>
#include "gui/prop/EasingPickWidget.h"
#include "gui/theme/Colors.h"

static QString findFixture(const QString& aArg) {
    if (!aArg.isEmpty() && QFile::exists(aArg)) return aArg;
    QStringList cands = {
        "tests/golden/easing_picker_geometry.json",
        "../tests/golden/easing_picker_geometry.json",
        "../../../tests/golden/easing_picker_geometry.json",
    };
    for (const QString& c : cands) if (QFile::exists(c)) return c;
    return QString();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    theme::Colors::activate(true, theme::kDefaultAccent);

    util::Easing::Param initial;
    initial.type = util::Easing::Type_Cubic;
    initial.range = util::Easing::Range_InOut;
    initial.weight = 1.0f;

    gui::prop::EasingPickWidget w(initial);
    w.setObjectName("popover");
    w.show();
    app.processEvents();
    // Height-for-width widget: size via heightForWidth (sizeHint is ambiguous
    // for an hfw layout), matching how the popover is sized in the app.
    const int hfw = w.heightForWidth(300);
    w.resize(300, hfw > 0 ? hfw : w.sizeHint().height());
    app.processEvents();

    fprintf(stderr, "widget size=%dx%d\n", w.width(), w.height());

    // ---- write-golden: snapshot the current measured geometry (same element
    // set + tolerances as the existing reference) into the golden so the layout
    // gate tracks the approved layout. Run: easing_picker_layout --write-golden
    // <path>. This is how the golden is kept in sync after an intentional
    // layout change; it does not re-derive values from the browser mock.
    if (argc > 2 && QString::fromLocal8Bit(argv[1]) == "--write-golden") {
        const QString outPath = QString::fromLocal8Bit(argv[2]);
        const QString srcRef = findFixture(QString()); // name/tol template
        QJsonArray arr;
        int missing = 0;
        if (!srcRef.isEmpty()) {
            QFile rf2(srcRef);
            if (rf2.open(QIODevice::ReadOnly)) {
                for (const QJsonValue& v : QJsonDocument::fromJson(rf2.readAll()).array()) {
                    const QJsonObject o = v.toObject();
                    const QString name = o["name"].toString();
                    QWidget* child = (name == "popover") ? &w : w.findChild<QWidget*>(name);
                    if (!child) { fprintf(stderr, "write-golden: missing %s\n", qPrintable(name)); ++missing; continue; }
                    const QPoint tl = child->mapTo(&w, QPoint(0, 0));
                    arr.append(QJsonObject{
                        {"name", name}, {"x", tl.x()}, {"y", tl.y()},
                        {"w", child->width()}, {"h", child->height()},
                        {"tol", o.contains("tol") ? o["tol"].toInt() : 1}});
                }
            }
        }
        if (arr.isEmpty()) { fprintf(stderr, "write-golden: no elements (missing %d)\n", missing); return 2; }
        QFile of(outPath);
        if (!of.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fprintf(stderr, "write-golden: cannot open %s\n", qPrintable(outPath)); return 2;
        }
        of.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
        fprintf(stderr, "write-golden: wrote %d elements -> %s (missing %d)\n",
                int(arr.size()), qPrintable(outPath), missing);
        return missing == 0 ? 0 : 2;
    }

    // ---- reference ----
    const QString refPath = findFixture(argv[1]);
    if (refPath.isEmpty()) {
        fprintf(stderr, "FAIL: reference geometry not found\n");
        return 2;
    }
    QFile rf(refPath);
    if (!rf.open(QIODevice::ReadOnly)) {
        fprintf(stderr, "FAIL: cannot open %s\n", qPrintable(refPath));
        return 2;
    }
    const QJsonArray refs = QJsonDocument::fromJson(rf.readAll()).array();

    // ---- compare ----
    int failed = 0, passed = 0;
    for (const QJsonValue& v : refs) {
        const QJsonObject o = v.toObject();
        const QString name = o["name"].toString();
        const double rx = o["x"].toDouble(), ry = o["y"].toDouble();
        const double rw = o["w"].toDouble(), rh = o["h"].toDouble();
        const double tol = o.contains("tol") ? o["tol"].toDouble() : 2.0;

        QWidget* child = (name == "popover") ? &w : w.findChild<QWidget*>(name);
        if (!child) {
            fprintf(stderr, "FAIL  %-18s missing element\n", qPrintable(name));
            ++failed;
            continue;
        }
        const QPoint tl = child->mapTo(&w, QPoint(0, 0));
        const int ax = tl.x(), ay = tl.y(), aw = child->width(), ah = child->height();

        const bool okX = std::fabs(ax - rx) <= tol;
        const bool okY = std::fabs(ay - ry) <= tol;
        const bool okW = std::fabs(aw - rw) <= tol;
        const bool okH = std::fabs(ah - rh) <= tol;
        const bool ok = okX && okY && okW && okH;

        if (ok) {
            ++passed;
        } else {
            ++failed;
            fprintf(stderr,
                    "FAIL  %-18s got(%4d,%4d,%4d,%4d) ref(%4.1f,%4.1f,%4.1f,%4.1f) tol %.0f   %s%s%s%s\n",
                    qPrintable(name), ax, ay, aw, ah, rx, ry, rw, rh, tol,
                    okX ? "" : " X", okY ? "" : " Y", okW ? "" : " W", okH ? "" : " H");
        }
    }

    // ---- tile catalog: the order, the separator, and the Auto tile's own curve ----
    // Three things the golden cannot see: the tiles' IDENTITY (it records a box at
    // "tile-i", not which easing tile-i is), the separator (unnamed), and what the Auto
    // glyph draws. All three matter after the reorder: the picker's catalog is the
    // display table's non-Custom entries position by position - Linear, Floor, Round,
    // Auto first - so the hairline closes the four non-curve entries and lands after the
    // AUTO tile, not after Cubic, and the Auto tile draws the canonical automatic
    // handle pair instead of collapsing to the identity (Linear) it shows when it is
    // handed the edited key's empty neighbour context.
    {
        // (a) Order: a tile's click writes its easing into the draft, so walking
        // the catalog reads its order back. The head is the three non-curve types;
        // tile 3 is the AUTO PRESET (Custom with both handle sides Auto), so the
        // curve family after it is the display table shifted by one. Restored
        // afterwards so the screenshot below renders the same draft this gate
        // started on.
        bool order = true;
        int catalog = 0;
        for (int i = 0;; ++i) {
            QToolButton* tile = w.findChild<QToolButton*>(QStringLiteral("tile-%1").arg(i));
            if (!tile) break;
            ++catalog;
            tile->click();
            app.processEvents();
            const util::Easing::Param value = w.value();
            const bool preset = value.type == util::Easing::Type_Custom
                && value.handleIn == util::Easing::kAutoPresetHandleMode
                && value.handleOut == util::Easing::kAutoPresetHandleMode;
            if (i < 3)
                order = order && !preset && int(value.type) == int(util::Easing::typeAtDisplayIndex(i));
            else if (i == 3)
                order = order && preset;
            else
                order = order && !preset
                    && int(value.type) == int(util::Easing::typeAtDisplayIndex(i - 1));
        }
        order = order && catalog == util::Easing::displayCount();

        // (a2) The Auto preset's CHECKED STATE and its label both come from the
        // Param it writes: clicking the tile must check that tile and no other,
        // and the button's name must be the Param's user-facing name (the two
        // handle modes), never the interpolation type "Custom".
        QToolButton* autoTile = w.findChild<QToolButton*>(QStringLiteral("tile-3"));
        bool checkedOnlyAuto = autoTile != nullptr;
        if (autoTile) {
            autoTile->click();
            app.processEvents();
            for (int i = 0; i < catalog; ++i) {
                QToolButton* tile = w.findChild<QToolButton*>(QStringLiteral("tile-%1").arg(i));
                checkedOnlyAuto = checkedOnlyAuto && tile && tile->isChecked() == (i == 3);
            }
            // The preset is the CLAMPED automatic pair (the retired Auto type's
            // behaviour), so its name is "Auto Clamped" - and the picker's own
            // header must say that name, never the interpolation type "Custom".
            const QString shown = util::Easing::getParamName(w.value());
            QLabel* title = w.findChild<QLabel*>(QStringLiteral("popover-title"));
            // The glyph-only tile's accessible text is the ACTION (automatic
            // handles apply to the segment after the key) plus the pair it
            // writes; the picker's TITLE still names only the parameter.
            const QString accessible = autoTile->accessibleName();
            checkedOnlyAuto = checkedOnlyAuto
                && shown == util::Easing::getHandleModeName(util::Easing::kAutoPresetHandleMode)
                && accessible.contains(shown)
                && accessible.contains(QLatin1String("segment after the selected key"))
                && title && title->text() == shown;
            fprintf(stderr, "Auto preset: checked only tile-3=%d, name='%s', title='%s' -> %s\n",
                    int(checkedOnlyAuto), qPrintable(shown),
                    title ? qPrintable(title->text()) : "<none>", checkedOnlyAuto ? "OK" : "FAIL");
        }
        if (!checkedOnlyAuto) {
            fprintf(stderr, "FAIL  the Auto preset's checked state/name is not the pair it writes\n");
            ++failed;
        }
        w.setValue(initial); // back to the initial draft (and the initial glyph colours)
        app.processEvents();

        // (b) The separator: every same-row step is one tile plus one gap (36px); the
        // step from tile-3 to tile-4 carries the 9px hairline too (49px). Nothing names
        // the separator, so the step it leaves is the only handle on it.
        int badSteps = 0;
        for (int i = 1; i < catalog; ++i) {
            QToolButton* a = w.findChild<QToolButton*>(QStringLiteral("tile-%1").arg(i - 1));
            QToolButton* b = w.findChild<QToolButton*>(QStringLiteral("tile-%1").arg(i));
            if (!a || !b) { ++badSteps; continue; }
            const QPoint pa = a->mapTo(&w, QPoint(0, 0)), pb = b->mapTo(&w, QPoint(0, 0));
            if (pa.y() != pb.y()) continue; // tile-i wrapped onto the next row
            const int want = (i == 4) ? 49 : 36; // 32px tile + 4px gap (+ 9px separator)
            if (pb.x() - pa.x() != want) ++badSteps;
        }

        // (c) The Auto glyph: neither tile is checked here, so both glyphs are painted
        // the same muted colour and any pixel difference IS the Auto tile's image
        // (its canonical curve plus the two handle dots/stems it now draws) rather
        // than the identity curve the Linear tile paints.
        auto grabGlyph = [&w](const char* aTile) {
            QToolButton* tile = w.findChild<QToolButton*>(QString::fromLatin1(aTile));
            gui::prop::CurveGlyph* g = tile ? tile->findChild<gui::prop::CurveGlyph*>() : nullptr;
            return g ? g->grab().toImage() : QImage();
        };
        const QImage gLinear = grabGlyph("tile-0");
        const QImage gAuto = grabGlyph("tile-3");
        const bool sameSize = !gLinear.isNull() && gLinear.size() == gAuto.size();
        int glyphDiff = -1;
        if (sameSize) {
            glyphDiff = 0;
            for (int y = 0; y < gLinear.height(); ++y)
                for (int x = 0; x < gLinear.width(); ++x)
                    if (gLinear.pixel(x, y) != gAuto.pixel(x, y)) ++glyphDiff;
        }

        fprintf(stderr,
                "tile catalog: %d tile(s) for %d entries (non-Custom + the Auto preset), order=%d, bad column steps=%d "
                "(want tile+gap, +separator between tile-3 and tile-4), "
                "Auto-vs-Linear differing glyph pixels=%d\n",
                catalog, util::Easing::displayCount(), int(order), badSteps, glyphDiff);
        if (!order) {
            fprintf(stderr, "FAIL  tile catalog order is not the display table's\n");
            ++failed;
        }
        if (badSteps != 0) {
            fprintf(stderr, "FAIL  the separator does not sit between tile-3 and tile-4\n");
            ++failed;
        }
        if (!sameSize || glyphDiff <= 0) {
            fprintf(stderr, "FAIL  the Auto tile's glyph is missing or identical to Linear's\n");
            ++failed;
        }

        // (d) THE AUTO GLYPH'S SHAPE, from the glyph's OWN param and context: a
        // "not equal to Linear" gate accepts any non-straight curve, including the
        // retired clamped pair's (which dived below the start and overshot the end).
        // The canonical automatic pair derives (1/3, 0) and (2/3, 1); assert those.
        {
            gui::prop::CurveGlyph* g = w.findChild<QToolButton*>(QStringLiteral("tile-3"))
                ? w.findChild<QToolButton*>(QStringLiteral("tile-3"))
                      ->findChild<gui::prop::CurveGlyph*>()
                : nullptr;
            bool canonical = g != nullptr;
            if (g) {
                const util::Easing::Param& p = g->param();
                const util::Easing::AutoContext& ctx = g->autoContext();
                canonical = p.type == util::Easing::Type_Custom
                    && p.handleIn == util::Easing::kAutoPresetHandleMode
                    && p.handleOut == util::Easing::kAutoPresetHandleMode
                    && ctx.inMode == util::Easing::kAutoPresetHandleMode;
                const QPointF out = util::Easing::controlPoint(p, ctx.inMode, ctx, 1.0f, 0);
                const QPointF in = util::Easing::controlPoint(p, ctx.inMode, ctx, 1.0f, 1);
                canonical = canonical && std::fabs(out.x() - 1.0 / 3.0) < 1e-6
                    && std::fabs(out.y() - 0.0) < 1e-6 && std::fabs(in.x() - 2.0 / 3.0) < 1e-6
                    && std::fabs(in.y() - 1.0) < 1e-6;
            }
            fprintf(stderr, "Auto glyph control points are the canonical (1/3,0)/(2/3,1): %d\n",
                    int(canonical));
            if (!canonical) {
                fprintf(stderr, "FAIL  the Auto tile is not drawing the canonical automatic pair\n");
                ++failed;
            }
        }
    }

    // ---- record screenshot ----
    const QString outPng = (argc > 2) ? QString::fromLocal8Bit(argv[2])
                                      : QStringLiteral("easing_picker_layout.png");
    // Optional: render the Custom (bezier) view when argv[3]=="custom".
    if (argc > 3 && QString::fromLocal8Bit(argv[3]) == "custom") {
        w.setCustom();
        app.processEvents();
        const int hfw = w.heightForWidth(300);
        w.resize(300, hfw > 0 ? hfw : w.sizeHint().height());
        app.processEvents();
        w.grab().save(outPng);
        return 0;
    }
    w.grab().save(outPng);

    fprintf(stderr, "\n%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
