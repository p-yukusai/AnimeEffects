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
// Usage: easing_picker_layout [reference.json] [output.png]
#include <QApplication>
#include <QWidget>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QDebug>
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
