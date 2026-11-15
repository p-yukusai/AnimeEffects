// The tooltip contract, offscreen: E29's explicit set, E27's placement, E28's
// never-clipped clamp - with E26's timing kept honest in passing.
//
// Why this file exists: the 2026-09-30 decisions made the tooltip SET and its
// PLACEMENT contract lines, and both are geometry/timing facts that a
// screenshot cannot pin (the contract's own verification table). Everything
// here asserts an observable: which controls reveal a tooltip at all (driven by
// REAL window-delivered hover, then a dwell past the 500 ms delay), where the
// bubble lands (E27: below its owner, flipping above at the bottom edge), and
// that it always fits the window (E28: shifted/clamped inside - the measured
// defect it replaces overhung a 500px window by 114 px and was simply cut).
//
// The call sites are the REAL files: AeTextField's clear button and
// AeFrameRangeRow's remove button must be in the DROPPED set (they restate
// their visible glyph's action words), AeNumberField's reset and the export
// dialog's "Reset to …" buttons the KEPT one (E29 + item 35), and the real
// ExportDialog.qml is walked button by button so the set cannot silently grow.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/tooltip_contract
#include <QApplication>
#include <QElapsedTimer>
#include <QHoverEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>

#include "ctrl/ExportTarget.h"
#include "gui/qml/ExportViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Spin the event loop for a bit: AeToolTip's delay and timeout are Timer work.
static void spin(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

// Window-delivered hover (geom's measured pattern: pointer handlers see the
// window's delivery path, never events posted at the item). A move enters the
// target and leaves whatever was hovered before.
static void hoverAt(QQuickWindow* w, QQuickItem* content, QQuickItem* target, QPointF local) {
    const QPointF p = target->mapToItem(content, local);
    QHoverEvent he(QEvent::HoverMove, p, p, p - QPointF(40, 0));
    QCoreApplication::sendEvent(w, &he);
    QCoreApplication::processEvents();
}
static void hoverAway(QQuickWindow* w) {
    QHoverEvent he(QEvent::HoverMove, QPointF(-100, -100), QPointF(-100, -100), QPointF(0, 0));
    QCoreApplication::sendEvent(w, &he);
    QCoreApplication::processEvents();
}

static QList<QObject*> tipsBelow(QObject* aRoot) {
    QList<QObject*> out;
    const auto all = aRoot->findChildren<QObject*>();
    for (QObject* o : all)
        if (QByteArray(o->metaObject()->className()).contains("AeToolTip")) out << o;
    return out;
}

static QObject* tipIn(QObject* aRoot) {
    const auto tips = tipsBelow(aRoot);
    return tips.isEmpty() ? nullptr : tips.first();
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

    // ---- the unit + real-control scene (500x400, edges reachable) -----------
    QTemporaryDir sceneDir;
    check(sceneDir.isValid(), "a scratch dir for the scene");
    const QString scenePath = sceneDir.path() + QStringLiteral("/scene.qml");
    {
        QFile f(scenePath);
        check(f.open(QIODevice::WriteOnly | QIODevice::Text), "the scene file can be written");
        QByteArray q;
        q += "import QtQuick\n";
        q += "import \"file:" AE_QML_DIR "\"\n";
        q += "Item {\n";
        q += "  width: 500; height: 400\n";
        // Placement probes: explicit owner rects at the interesting edges.
        q += "  Item { width: 40; height: 26; x: 200; y: 150\n";
        q += "    AeToolTip { objectName: \"tipMid\"; text: \"MID\"; ownerX: 0; ownerY: 0; ownerW: 40; ownerH: 26 } }\n";
        q += "  Item { width: 40; height: 26; x: 200; y: 360\n";
        q += "    AeToolTip { objectName: \"tipBottom\"; text: \"BOTTOM\"; ownerX: 0; ownerY: 0; ownerW: 40; ownerH: 26 } }\n";
        q += "  Item { width: 26; height: 26; x: 462; y: 100\n";
        q += "    AeToolTip { objectName: \"tipRight\"; text: \"Reset to native\"; ownerX: 0; ownerY: 0; ownerW: 26; ownerH: 26 } }\n";
        q += "  Item { width: 26; height: 26; x: 462; y: 372\n";
        q += "    AeToolTip { objectName: \"tipCorner\"; text: \"Reset to native\"; ownerX: 0; ownerY: 0; ownerW: 26; ownerH: 26 } }\n";
        q += "  Item { width: 40; height: 26; x: 200; y: 240\n";
        q += "    AeToolTip { objectName: \"tipMulti\"; text: \"Label line\\nfamily line\\nid line\"; ownerX: 0; ownerY: 0; ownerW: 40; ownerH: 26 } }\n";
        // The real call sites.
        q += "  AeIconButton { objectName: \"btnKeep\"; x: 30; y: 30; iconName: \"arrow-clockwise\"; label: \"Reset to auto\"; showTip: true }\n";
        q += "  AeIconButton { objectName: \"btnDrop\"; x: 30; y: 70; iconName: \"link\"; label: \"Keep aspect ratio\" }\n";
        q += "  AeTextField { objectName: \"fieldClear\"; x: 30; y: 110; width: 200; text: \"hello\"; clearButton: true; accessibleName: \"Name\" }\n";
        q += "  AeFrameRangeRow { objectName: \"rowRange\"; x: 30; y: 150; width: 320; firstFrame: 1; lastFrame: 48; firstLabel: \"First frame\"; lastLabel: \"Last frame\"; ordinal: 1 }\n";
        q += "  AeNumberField { objectName: \"numReset\"; x: 30; y: 195; width: 180; label: \"Width\"; resetLabel: \"Reset to native\"; placeholder: \"Auto\"; placeholderValue: -1; value: 10; from: -1; to: 4096 }\n";
        q += "}\n";
        f.write(q);
    }

    QQmlComponent component(&engine, QUrl::fromLocalFile(scenePath));
    if (component.isError()) {
        for (const QQmlError& e : component.errors())
            QTextStream(stderr) << "scene load error: " << e.toString() << "\n";
        return 2;
    }
    QObject* created = component.create();
    auto* root = qobject_cast<QQuickItem*>(created);
    check(root != nullptr, "the scene root builds");
    if (!root) return 1;

    QQuickWindow window;
    window.resize(500, 400);
    window.show();
    root->setParentItem(window.contentItem());
    spin(150);

    auto item = [&](const char* n) -> QQuickItem* {
        return root->findChild<QQuickItem*>(QString(n));
    };

    // ---- E26: the delay is still real (non-regression on the timing law) ----
    {
        QObject* tipMid = root->findChild<QObject*>(QStringLiteral("tipMid"));
        check(tipMid != nullptr, "the placement probes loaded");
        if (tipMid) {
            tipMid->setProperty("text", QStringLiteral("MID"));
            tipMid->setProperty("shown", true);
            spin(150);
            check(!tipMid->property("revealed").toBool(),
                  "E26: 150ms after shown=true the tooltip has NOT appeared (the 500ms delay)");
            spin(800);
            check(tipMid->property("revealed").toBool(),
                  "E26: ~950ms after shown=true it HAS appeared");
        }
    }

    // ---- E27/E28: placement geometry, revealed ------------------------------
    {
        for (const char* n : {"tipMid", "tipBottom", "tipRight", "tipCorner", "tipMulti"}) {
            QObject* t = root->findChild<QObject*>(QString(n));
            if (t) t->setProperty("shown", true);
        }
        spin(800);
        QQuickItem* content = window.contentItem();

        auto geom = [&](const char* n, qreal& X, qreal& Y, qreal& W, qreal& H) {
            auto* it = qobject_cast<QQuickItem*>(root->findChild<QObject*>(QString(n)));
            if (!it) { X = Y = W = H = -1; return; }
            const QPointF p = it->mapToItem(content, QPointF(0, 0));
            X = p.x(); Y = p.y(); W = it->width(); H = it->height();
        };
        qreal x, y, w, h;

        // E27 below-first: mid-window owner has room below -> BELOW, centred.
        geom("tipMid", x, y, w, h);
        check(y == 150 + 26 + 6, QString("E27: below-first (y=%1, want %2)").arg(y).arg(150 + 26 + 6));
        // Centred within the placement's Math.round (a half-pixel of slack).
        check(qAbs(x - (200 + 20 - w / 2)) <= 1.0,
              QString("E27: centred on the owner (x=%1, w=%2)").arg(x).arg(w));

        // E27 flip: owner at the bottom edge has no room below -> ABOVE.
        geom("tipBottom", x, y, w, h);
        check(y + h <= 360, QString("E27: flipped ABOVE the bottom-edge owner (y=%1 h=%2)").arg(y).arg(h));
        check(y >= 0, "E28: and it is inside the window");

        // E28 clamp: wide tip on a right-edge owner -> shifted inside, still below.
        geom("tipRight", x, y, w, h);
        check(x >= 0 && x + w <= 500.5, QString("E28: right edge never clips (x=%1 w=%2)").arg(x).arg(w));
        check(y == 100 + 26 + 6, QString("E27: and still below-first (y=%1)").arg(y));

        // E28 + flip together: corner owner -> fully inside the window.
        geom("tipCorner", x, y, w, h);
        check(x >= 0 && x + w <= 500.5 && y >= 0 && y + h <= 400.5,
              QString("E28: the corner tip fits the window (x=%1 y=%2 w=%3 h=%4)").arg(x).arg(y).arg(w).arg(h));
        check(y + h <= 372, QString("E27: and it flipped above (y=%1 h=%2)").arg(y).arg(h));

        // The multi-line bubble fits its text (E29 'shows it in full' + E28).
        geom("tipMulti", x, y, w, h);
        check(h > 22 && y + h <= 400.5 && y >= 240 + 26,
              QString("the bubble grows for multi-line text and still sits below (y=%1 h=%2)").arg(y).arg(h));
    }

    // ---- E29: the set, driven by REAL hover + a dwell -----------------------
    {
        QQuickItem* content = window.contentItem();
        QQuickItem* keep = item("btnKeep");
        QQuickItem* drop = item("btnDrop");
        check(keep && drop, "the kept and dropped buttons loaded");
        if (keep && drop) {
            QObject* keepTip = tipIn(keep);
            QObject* dropTip = tipIn(drop);
            check(keepTip && dropTip, "both buttons carry a tooltip ITEM (gated, not deleted)");
            hoverAt(&window, content, keep, QPointF(13, 13));
            spin(250);
            check(keepTip && !keepTip->property("revealed").toBool(),
                  "E26/E29: the KEPT tooltip still waits at 250ms");
            spin(750);
            check(keepTip && keepTip->property("revealed").toBool(),
                  "E29: the kept kind (a Reset button) DOES reveal after the dwell");
            hoverAway(&window);
            spin(80);
            hoverAt(&window, content, drop, QPointF(13, 13));
            spin(1000);
            check(dropTip && !dropTip->property("revealed").toBool() && !dropTip->property("visible").toBool(),
                  "E29: the DROPPED kind reveals NOTHING after hover+dwell");
            hoverAway(&window);
        }

        // The set at the real field call sites (configuration, so the set cannot
        // silently grow): showTip is exactly the Auto/Reset affordances.
        auto gate = [](QObject* aRoot, const char* aType) {
            QList<QObject*> out;
            const auto all = aRoot->findChildren<QObject*>();
            for (QObject* o : all)
                if (QByteArray(o->metaObject()->className()).contains(aType)) out << o;
            return out;
        };
        QQuickItem* field = item("fieldClear");
        QQuickItem* rowR = item("rowRange");
        QQuickItem* num = item("numReset");
        check(field && rowR && num, "the real AeTextField / AeFrameRangeRow / AeNumberField loaded");
        if (field) {
            const auto btns = gate(field, "AeIconButton");
            check(!btns.isEmpty(), "the clear button exists (precondition)");
            bool anyTip = false;
            for (QObject* b : btns) anyTip = anyTip || b->property("showTip").toBool();
            check(!anyTip, "E29: AeTextField's Clear keeps NO tooltip");
        }
        if (rowR) {
            const auto btns = gate(rowR, "AeIconButton");
            check(!btns.isEmpty(), "the remove button exists (precondition)");
            for (QObject* b : btns) {
                const QString label = b->property("label").toString();
                if (label.isEmpty()) continue; // hidden field-resets with no words
                check(!b->property("showTip").toBool(),
                      QString("E29: AeFrameRangeRow's \"%1\" keeps NO tooltip").arg(label));
            }
        }
        if (num) {
            const auto btns = gate(num, "AeIconButton");
            bool found = false;
            for (QObject* b : btns) {
                if (b->property("label").toString() == QLatin1String("Reset to native")) {
                    found = true;
                    check(b->property("showTip").toBool(),
                          "E29: AeNumberField's reset keeps its tooltip (item 35's words)");
                }
            }
            check(found, "the reset button exists (precondition)");
        }
    }

    // ---- E29: the set at the REAL ExportDialog.qml call sites ---------------
    {
        QQmlComponent dlg(&engine, QUrl(QStringLiteral("file:" AE_QML_DIR "/ExportDialog.qml")));
        if (dlg.isError()) {
            for (const QQmlError& e : dlg.errors())
                QTextStream(stderr) << "dialog load error: " << e.toString() << "\n";
        }
        gui::ExportViewModel::Init init;
        init.nativeSize = QSize(1000, 400);
        init.nativeFps = 24;
        init.projectFirstFrame = 0;
        init.projectLastFrame = 59;
        init.playhead = 3;
        init.ranges = {ctrl::FrameRange{0, 5}};
        init.destinationId = QStringLiteral("telegram.sticker");
        init.qualityIndex = 1;
        init.width = 1000;
        init.height = 400;
        gui::ExportViewModel model(init);
        engine.rootContext()->setContextProperty(QStringLiteral("exportModel"), &model);
        QObject* dlgRoot = dlg.create();
        auto* dlgItem = qobject_cast<QQuickItem*>(dlgRoot);
        check(dlgItem != nullptr, "the real ExportDialog.qml builds with the real view model");
        if (dlgItem) {
            dlgItem->setParentItem(window.contentItem());
            spin(120);
            struct Site { QString label; bool tip; };
            QList<Site> sites;
            const auto all = dlgItem->findChildren<QObject*>();
            for (QObject* o : all) {
                if (!QByteArray(o->metaObject()->className()).contains("AeIconButton")) continue;
                sites << Site{o->property("label").toString(), o->property("showTip").toBool()};
            }
            check(!sites.isEmpty(), "the dialog really instantiates icon buttons (precondition)");
            int kept = 0;
            bool ruleHolds = true;
            QString offender;
            bool sawAspect = false, sawReset = false;
            for (const Site& s : sites) {
                const bool isReset = s.label.startsWith(QLatin1String("Reset "));
                if (s.tip) ++kept;
                if (s.tip && !isReset) { ruleHolds = false; offender = s.label; }
                if (!s.tip && isReset) { ruleHolds = false; offender = s.label; }
                if (s.label == QLatin1String("Keep aspect ratio")) { sawAspect = true; check(!s.tip, "E29: the aspect-ratio toggle keeps NO tooltip"); }
                if (isReset && s.tip) sawReset = true;
            }
            check(sawAspect, "the aspect-ratio toggle is in the scene (precondition)");
            check(sawReset, "at least one Reset button keeps its tooltip (precondition)");
            check(ruleHolds, QString("E29: showTip is EXACTLY the Reset affordances (offender: \"%1\")").arg(offender));
            check(kept >= 2, QString("E29: the kept set is the dialog's resets (kept=%1)").arg(kept));
        }
        delete dlgRoot;
    }

    QTextStream(stdout) << (failures ? "FAILURES: " : "ALL PASS: ") << failures << "\n";
    return failures ? 1 : 0;
}
