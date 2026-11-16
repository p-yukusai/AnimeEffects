// AeButton's hover fade and C21: "No row paints a wrong intermediate colour".
//
// The defect this gates (measured 2026-09-30, hover-spike/): the ghost variant's
// rest fill was "transparent" = rgba(0,0,0,0), and `Behavior on color` lerps
// straight RGBA - so the 80 ms fade to theme.raised passed through dark
// semi-transparent frames ("Add range hover flashes black"). The fill's own
// colour climbed #000000 -> #020202 -> #0a0a0a -> #171717 -> #292929 -> #404040,
// and the leave fade dipped below the base ON SCREEN (video frame series,
// hover-spike/out/rec_before.mp4, the (20,24,37) and (30,30,30) transition dips).
//
// What is asserted here is the ANIMATED PROPERTY (background.color), which is
// exactly what paints - NOT a grab: measured, QQuickItemGrabResult composites the
// item onto opaque black, which hides alpha and turns every fade into a climb
// from 0, making the instrument blind to this class of defect.
//
// The rules (C21):
//   * a fade between a transparent and an opaque fill must keep the fill's RGB
//     CONSTANT (the transparent endpoint is the opaque colour at alpha 0) -
//     any frame whose RGB falls toward black is the flash;
//   * a fade between two opaque fills stays inside the endpoints' RGB range;
//   * the hover state on first entry equals the hover state on the fifth;
//   * settled looks are unchanged (rest = nothing painted, hover = the token).
//
// Deliberate-break proof (run, reverted): restoring `"transparent"` makes the
// RGB-constancy and black-detector asserts fail on entry and leave.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/hover_parity
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
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

#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

static int failures = 0;
static QTextStream out(stdout);

static void check(bool ok, const QString& what) {
    out << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

static void spin(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(2);
    }
}

static void hoverAt(QQuickWindow* win, QPointF scenePt) {
    QHoverEvent he(QEvent::HoverMove, scenePt, scenePt, scenePt);
    QCoreApplication::sendEvent(win, &he);
}

static QColor bgColor(QQuickItem* btn) {
    QQuickItem* bg = btn->property("background").value<QQuickItem*>();
    return bg ? bg->property("color").value<QColor>() : QColor();
}

struct Sample { QColor c; int t; };

// Flip hover and sample the animated fill every ~10 ms for `dur` ms.
static QList<Sample> capture(QQuickWindow* win, QQuickItem* btn, QPointF on, QPointF off,
                             bool entering, int dur) {
    hoverAt(win, entering ? on : off);
    QElapsedTimer t;
    t.start();
    QList<Sample> s;
    while (t.elapsed() <= dur) {
        s.append({ bgColor(btn), (int)t.elapsed() });
        spin(8);
    }
    return s;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    const QColor raised = theme.raised();
    const QColor raisedHover = theme.raisedHover();

    QTemporaryDir sceneDir;
    const QString scenePath = sceneDir.path() + QStringLiteral("/scene.qml");
    {
        QFile f(scenePath);
        check(f.open(QIODevice::WriteOnly | QIODevice::Text), "the scene file can be written");
        f.write(
            "import QtQuick\n"
            "import \"file:" AE_QML_DIR "\"\n"
            "Item {\n"
            "    width: 320; height: 200\n"
            "    AeButton { objectName: \"ghost\"; kind: \"ghost\"; text: \"Add range\"; x: 20; y: 20 }\n"
            "    AeButton { objectName: \"secondary\"; kind: \"secondary\"; text: \"Cancel\"; x: 20; y: 120 }\n"
            "}\n");
    }

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QQmlComponent comp(&engine, QUrl::fromLocalFile(scenePath));
    if (comp.isError()) {
        for (const auto& e : comp.errors()) out << "scene error: " << e.toString() << "\n";
        return 2;
    }
    QQuickWindow win;
    win.resize(320, 200);
    win.show();
    auto* root = qobject_cast<QQuickItem*>(comp.create());
    check(root != nullptr, "the scene builds with the real AeButton");
    if (!root) return 1;
    root->setParentItem(win.contentItem());

    QQuickItem* ghost = root->findChild<QQuickItem*>(QStringLiteral("ghost"));
    QQuickItem* second = root->findChild<QQuickItem*>(QStringLiteral("secondary"));
    check(ghost && ghost->property("background").value<QQuickItem*>(),
          "precondition: the ghost button and its background exist");
    check(second && second->property("background").value<QQuickItem*>(),
          "precondition: the secondary button and its background exist");
    if (!ghost || !second) return 1;

    const QPointF gOn = ghost->mapToItem(win.contentItem(), QPointF(ghost->width() / 2, ghost->height() / 2));
    const QPointF gOff(gOn.x(), gOn.y() - 70);
    const QPointF sOn = second->mapToItem(win.contentItem(), QPointF(second->width() / 2, second->height() / 2));
    const QPointF sOff(sOn.x(), sOn.y() - 70);

    // ---- the fade rules, ghost: entry and leave -----------------------------
    for (int pass = 0; pass < 2; ++pass) {
        const bool entering = pass == 0;
        // Park on the START state of the transition being captured: OFF before an
        // entry, ON before a leave - else the fade runs during this settle and
        // the capture samples a state that is not changing.
        hoverAt(&win, entering ? gOff : gOn);
        spin(150);
        const QList<Sample> s = capture(&win, ghost, gOn, gOff, entering, 200);
        const QString dir = entering ? QStringLiteral("entry") : QStringLiteral("leave");
        bool rgbFlat = true, blackFrame = false, rangeOk = true;
        int aMin = 255, aMax = 0;
        for (const Sample& sm : s) {
            const int d = qMax(qAbs(sm.c.red() - raised.red()),
                               qMax(qAbs(sm.c.green() - raised.green()), qAbs(sm.c.blue() - raised.blue())));
            if (d > 3) rgbFlat = false;
            // C21's detector: an in-between frame (alpha mid-fade) whose fill
            // has gone dark is the flash.
            if (sm.c.alpha() > 20 && sm.c.alpha() < 235 && sm.c.red() < 16) blackFrame = true;
            if (sm.c.alpha() > 20 && sm.c.alpha() < 235 && sm.c.green() < 16) blackFrame = true;
            if (sm.c.alpha() > 20 && sm.c.alpha() < 235 && sm.c.blue() < 16) blackFrame = true;
            aMin = qMin(aMin, sm.c.alpha());
            aMax = qMax(aMax, sm.c.alpha());
            Q_UNUSED(rangeOk);
        }
        check(rgbFlat, "C21 ghost " + dir + ": the fade keeps the fill RGB at theme.raised (no dark RGB)");
        check(!blackFrame, "C21 ghost " + dir + ": no mid-fade frame has a black-ish fill");
        if (entering) check(aMax - aMin > 100, "precondition: the entry fade really animated (alpha moved)");
        else check(aMax - aMin > 100, "precondition: the leave fade really animated (alpha moved)");
    }

    // ---- C21 parity: first entry == fifth entry ----------------------------
    QColor settled1, settled5;
    for (int k = 1; k <= 5; ++k) {
        hoverAt(&win, gOff);
        spin(120);
        capture(&win, ghost, gOn, gOff, true, 160);
        spin(120);
        const QColor settled = bgColor(ghost);
        if (k == 1) settled1 = settled;
        if (k == 5) settled5 = settled;
    }
    check(settled1 == settled5, "C21: the hover state on first entry equals the hover state on the fifth");
    check(settled1.rgba() == QColor(raised.red(), raised.green(), raised.blue(), 255).rgba(),
          "settled hover is theme.raised, fully opaque");
    hoverAt(&win, gOff);
    spin(200);
    const QColor rest = bgColor(ghost);
    check(rest.alpha() == 0 && rest.red() == raised.red() && rest.green() == raised.green()
              && rest.blue() == raised.blue(),
          "settled rest is the raised RGB at alpha 0 - paints nothing, same look as 'transparent'");

    // ---- secondary: the opaque-to-opaque rule ------------------------------
    hoverAt(&win, sOff);
    spin(150);
    {
        const QList<Sample> s = capture(&win, second, sOn, sOff, true, 200);
        bool inRange = true;
        for (const Sample& sm : s) {
            const int lo = qMin(raised.red(), raisedHover.red()), hi = qMax(raised.red(), raisedHover.red());
            if (sm.c.red() < lo - 3 || sm.c.red() > hi + 3) inRange = false;
        }
        check(inRange, "C21 secondary: the opaque-to-opaque fade stays inside the endpoints' RGB range");
        spin(150);
        check(bgColor(second) == QColor(raisedHover.red(), raisedHover.green(), raisedHover.blue(), 255),
              "secondary settled hover is theme.raisedHover");
    }

    out << (failures ? "FAILURES=" : "ALL PASS, failures=") << failures << "\n";
    return failures ? 1 : 0;
}
