// The export form's overflow contract, offscreen: D25 + Q9 (decided 2026-09-30).
//
//   "The export form never exceeds the window without a scroll: when the content
//    does not fit (minimum size, small screen, or the Advanced section open), the
//    content scrolls by wheel, keys and drag, and the Cancel/Export row stays
//    fixed at the bottom while the content scrolls - reachable without scrolling
//    to the end."
//
// Every assertion is a geometry fact of the REAL ExportDialog.qml with the real
// view model - the measured before-column (overflow-spike) is what the suite
// locks in: 630px of content in 558px of page at 560x640 with Advanced open and
// four frame ranges was CUT with no recourse, and 316px at a 300px-tall window.
// (The 630px is the HISTORICAL before-column the regression was measured against,
// not today's number: measured 2026-10-03, after the transparency toggle moved
// onto the regular surface, the same worst case printed 702px. H41 moved the
// frame-range rows INTO Advanced on 2026-10-09; re-measured that day the same
// worst case prints
//   [measured] D25 worst case 560x640, four ranges, Advanced open:
//              contentHeight=700 pageHeight=558
// and a GIF-row form (twitch.emote) prints 493 closed -> 749 open. The OPEN case
// is the same height it was (2px less: the row moved from the OUTPUT grid's row
// spacing into the Advanced column's); the CLOSED case is 164px shorter, because
// the frame ranges no longer sit on the default surface - which is the whole
// point of H41. The page is still 558px; the content grew, which is exactly why
// these asserts compare against the form's own measurements instead of a pinned
// constant.)
//
// The window-route Escape block below targets the FRAME field's editor through the
// row that owns it rather than through a tree-wide "scrubEdit" lookup, because the
// frames rows moved from the OUTPUT grid (where they were the first in the tree) to
// the Advanced section, and a tree-wide lookup now finds the Width box instead.
// The row was always anchored (it does not move - the Pinned assert keeps it
// that way); what was missing is everything else in this file:
//
//   * the content scrolls (wheel, drag, PageUp/PageDown/Home/End);
//   * focus movement scrolls the target's whole ROW into view (reachability);
//   * the scroll bar appears only when the content overflows (D23's rule);
//   * a tooltip inside the scrolling content is clipped by NOTHING (E28) -
//     the clamp now narrows to the visible part of every clipping ancestor;
//   * the combo's in-scene popup lives outside the clip (G37 survives D25).
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/form_layout
#include <QApplication>
#include <QElapsedTimer>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QWheelEvent>
#include <QGuiApplication>
#include <QTest>

#include <cstdlib>

#include "ctrl/ExportTarget.h"
#include "gui/qml/ExportViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

static int failures = 0;

// K46: a QML binding that cannot evaluate writes NO value at all - the property keeps
// its default and the form still looks plausible, which is how the Quality row stayed
// drawn in both modes through four TypeError lines nobody read. The message handler is
// the only place that fact is observable, so the run fails on the first one; anything
// that is not a throw goes to stderr exactly as Qt would have written it.
static int qmlThrows = 0;
static QString qmlThrowFirst;

static void collectQmlThrows(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (msg.contains(QLatin1String("TypeError")) || msg.contains(QLatin1String("is not a function"))) {
        if (qmlThrows == 0) qmlThrowFirst = msg;
        ++qmlThrows;
    }
    QTextStream(stderr) << msg << "\n";
    if (type == QtFatalMsg) std::abort();
}

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

static void spin(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

// Window-delivered hover (tooltip_contract's measured pattern: pointer handlers
// see the window's delivery path, never events posted at the item).
static void hoverAt(QQuickWindow* w, QQuickItem* content, QQuickItem* target, QPointF local) {
    const QPointF p = target->mapToItem(content, local);
    QHoverEvent he(QEvent::HoverMove, p, p, p - QPointF(40, 0));
    QCoreApplication::sendEvent(w, &he);
    QCoreApplication::processEvents();
}

static bool rectFits(const QRectF& inner, const QRectF& outer, qreal slack = 1.5) {
    return inner.left() >= outer.left() - slack && inner.top() >= outer.top() - slack
        && inner.right() <= outer.right() + slack && inner.bottom() <= outer.bottom() + slack;
}

static QString rs(const QRectF& r) {
    return QString("(%1,%2 %3x%4)").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
}

// K46 (ruled 2026-10-01): which of the form's two quality rows belongs to which
// artifact. The rows are drawn as a label and a control that are SIBLINGS in one
// layout, and only the control carries the objectName the probes address - so a
// row is found as the pair and the label is looked up next to its control.
static void qualityRow(QQuickItem* root, QQuickItem** label, QQuickItem** combo) {
    *label = nullptr;
    *combo = root ? root->findChild<QQuickItem*>(QStringLiteral("Quality")) : nullptr;
    if (!*combo || !(*combo)->parentItem()) return;
    // Among the combo's own siblings, never by a tree-wide search for the word: the
    // Advanced section carries a second control whose label reads "Quality override",
    // and a whole-tree match is one substring away from reporting the wrong row.
    const QList<QQuickItem*> siblings = (*combo)->parentItem()->childItems();
    for (QQuickItem* c : siblings) {
        if (c != *combo && c->property("text").toString() == QLatin1String("Quality")) {
            *label = c;
            break;
        }
    }
}

// The "Image quality" row. AeNumberField draws no label of its own - the form puts
// an AeFieldLabel beside it - so the words match exactly one Text, and the control is
// the field that carries the same words as the accessible name a probe reads.
static void imageQualityRow(QQuickItem* root, QQuickItem** label, QQuickItem** field) {
    *label = nullptr;
    *field = nullptr;
    if (!root) return;
    for (QQuickItem* c : root->findChildren<QQuickItem*>()) {
        if (!*label && c->property("text").toString() == QLatin1String("Image quality"))
            *label = c;
        if (!*field && c->property("label").toString() == QLatin1String("Image quality"))
            *field = c;
    }
}

// Repeater delegates are NOT reachable with findChildren<QQuickItem*>(): the
// QObject parent of a delegate is the delegate model, not the item tree, so a
// lookup inside one finds nothing even while the rows are laid out and visible.
// Walk the VISUAL tree (childItems()) instead - that is the tree the layouts use.
// ONE statement of the rule, at file scope: the frame-range rows, the fact rows
// and the picker rows are all delegates, and a lookup that forgets this returns
// nullptr while the control is on screen.
static QList<QQuickItem*> visualTree(QQuickItem* root) {
    QList<QQuickItem*> out;
    QList<QQuickItem*> stack;
    stack.append(root);
    while (!stack.isEmpty()) {
        QQuickItem* it = stack.takeLast();
        for (QQuickItem* child : it->childItems()) {
            out.append(child);
            stack.append(child);
        }
    }
    return out;
}

struct Form {
    QQuickWindow* win = nullptr;
    QQuickItem* root = nullptr;
    QQuickItem* scroller = nullptr;
    QQuickItem* page = nullptr;
    QQuickItem* footer = nullptr;
    QQuickItem* vbar = nullptr;
    gui::ExportViewModel* model = nullptr;

    QQuickItem* byName(const char* n) {
        return root ? root->findChild<QQuickItem*>(QString(n)) : nullptr;
    }
    qreal contentY() const { return scroller ? scroller->property("contentY").toReal() : -1; }
    qreal contentX() const { return scroller ? scroller->property("contentX").toReal() : -1; }
    qreal contentH() const { return scroller ? scroller->property("contentHeight").toReal() : -1; }
    qreal contentW() const { return scroller ? scroller->property("contentWidth").toReal() : -1; }
    QRectF viewportInPage() const {
        return QRectF(contentX(), contentY(), scroller->width(), scroller->height());
    }
    QRectF rectInPage(QQuickItem* it) const {
        const QPointF p = it->mapToItem(page, 0, 0);
        return QRectF(p.x(), p.y(), it->width(), it->height());
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    qInstallMessageHandler(collectQmlThrows);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QQmlComponent dlg(&engine, QUrl(QStringLiteral("file:" AE_QML_DIR "/ExportDialog.qml")));
    if (dlg.isError()) {
        for (const auto& e : dlg.errors())
            QTextStream(stderr) << "dialog load error: " << e.toString() << "\n";
        return 2;
    }

    // One form per scenario: the scene keeps imperative state (contentY), and a
    // reused instance would let one scenario scroll the next one's assumptions.
    // `dest` defaults to the group id every earlier scenario uses; the transparency
    // block below needs destinations whose own alpha rule differs.
    // `native` is the PROJECT's size: it seeds the requested size as well, so the form
    // opens on a project whose boxes start at that resolution - the rule the resolution
    // block below drives through the real fields.
    auto makeForm = [&](int w, int h, bool adv, int ranges, Form* f,
                        const QString& dest = QStringLiteral("discord"),
                        QSize native = QSize(1000, 400)) {
        gui::ExportViewModel::Init init;
        init.nativeSize = native;
        init.nativeFps = 24;
        init.projectFirstFrame = 0;
        init.projectLastFrame = 59;
        init.playhead = 3;
        QList<ctrl::FrameRange> rs;
        for (int i = 0; i < ranges; ++i) rs << ctrl::FrameRange{i * 10, i * 10 + 5};
        init.ranges = rs;
        init.destinationId = dest;
        init.qualityIndex = 1;
        init.width = native.width();
        init.height = native.height();
        f->model = new gui::ExportViewModel(init);
        engine.rootContext()->setContextProperty(QStringLiteral("exportModel"), f->model);
        QObject* o = dlg.create();
        f->root = qobject_cast<QQuickItem*>(o);
        f->win = new QQuickWindow();
        f->win->resize(w, h);
        f->win->show();
        f->root->setParentItem(f->win->contentItem());
        f->root->setWidth(w);
        f->root->setHeight(h);
        f->model->setAdvancedOpen(adv);
        spin(150);
        f->scroller = f->byName("formScroller");
        f->page = f->byName("formPage");
        f->footer = f->byName("formFooter");
        f->vbar = f->byName("formVBar");
    };
    auto dispose = [](Form* f) {
        delete f->root;
        delete f->win;
        delete f->model;
    };

    // =====================================================================
    // The overflow scenario: 560x640 (the shell's floor), Advanced open, four
    // frame ranges - measured to overflow by 72px before this change.
    // =====================================================================
    Form f;
    makeForm(560, 640, true, 4, &f);
    check(f.scroller && f.page && f.footer && f.vbar,
          "the form builds with its scroll host, page, footer and scroll bar");
    if (!f.scroller) return 1;

    // The figure the dialog's own comment and this file's header both quote. Printed,
    // not just asserted: a layout change that keeps the overflow premise true while
    // moving the number cannot leave the two copies of it quietly wrong.
    QTextStream(stdout) << QStringLiteral("[measured] D25 worst case 560x640, four ranges, "
                                          "Advanced open: contentHeight=%1 pageHeight=%2")
                               .arg(f.contentH())
                               .arg(f.scroller->height())
                        << "\n";
    check(f.contentH() > f.scroller->height(),
          QString("precondition: the content actually overflows (%1 > %2) or nothing below can be observed")
              .arg(f.contentH()).arg(f.scroller->height()).toUtf8().constData());
    check(f.footer->height() == 52 && f.footer->mapToItem(f.root, 0, 0).y() == 640 - 52,
          "precondition: the action row starts pinned at the window's bottom band");

    // ---- Q9: the row is FIXED while the content scrolls ----------------------
    {
        const qreal y0 = f.footer->mapToItem(f.root, 0, 0).y();
        const qreal cy0 = f.contentY();
        QMetaObject::invokeMethod(f.scroller, "pageBy", Q_ARG(QVariant, 60));
        spin(60);
        check(f.contentY() > cy0, "the content scrolled (pageBy moved contentY)");
        check(f.footer->mapToItem(f.root, 0, 0).y() == y0,
              "Q9: the action row did not move while the content scrolled");
        // And the content really moved under the fixed row: the page shifted in
        // the window by exactly the scroll delta.
        QMetaObject::invokeMethod(f.scroller, "pageBy", Q_ARG(QVariant, -60));
        spin(60);
        check(f.footer->mapToItem(f.root, 0, 0).y() == y0,
              "Q9: and it is still pinned after scrolling back");
    }

    // ---- D25: wheel, drag and keys all scroll -------------------------------
    {
        const qreal cy0 = f.contentY();
        QWheelEvent we(QPointF(280, 300), QPointF(280, 300), QPoint(0, 0), QPoint(0, -240),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(f.win, &we);
        spin(80);
        check(f.contentY() != cy0,
              "D25: the wheel scrolls the content");

        // Drag (flick) on empty content: the Output section header is a plain
        // Text - the press falls through to the Flickable and drags the content.
        QQuickItem* header = nullptr;
        for (QQuickItem* c : f.root->findChildren<QQuickItem*>())
            if (c->property("text").toString() == QLatin1String("Output")) { header = c; break; }
        check(header != nullptr, "precondition: a plain (non-interactive) content area to drag from");
        if (header) {
            const QPointF hp = header->mapToItem(f.win->contentItem(), 2, 2);
            const qreal before = f.contentY();
            QMouseEvent press(QEvent::MouseButtonPress, hp, hp, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(f.win, &press);
            for (int i = 1; i <= 5; ++i) {
                QMouseEvent move(QEvent::MouseMove, hp + QPointF(0, -12.0 * i),
                                 hp + QPointF(0, -12.0 * i), Qt::NoButton,
                                 Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(f.win, &move);
                spin(16);
            }
            QMouseEvent rel(QEvent::MouseButtonRelease, hp + QPointF(0, -60),
                            hp + QPointF(0, -60), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(f.win, &rel);
            spin(80);
            check(f.contentY() != before, "D25: a drag scrolls the content");
        }

        // Keys: PageUp/PageDown/Home/End at the scene root (the fields do not
        // consume these four, so they bubble - the second kind of "keys").
        f.footer->forceActiveFocus();
        spin(30);
        QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
        QCoreApplication::sendEvent(f.win, &end);
        spin(60);
        check(qAbs(f.contentY() - (f.contentH() - f.scroller->height())) < 2,
              "D25: End scrolls to the bottom of the content");
        QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
        QCoreApplication::sendEvent(f.win, &home);
        spin(60);
        check(f.contentY() == 0, "D25: Home scrolls back to the top");
        QKeyEvent pgdn(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier);
        QCoreApplication::sendEvent(f.win, &pgdn);
        spin(60);
            check(f.contentY() > 0, "D25: PageDown scrolls by a page");
    }

    // A drag on a scrubbing field must scrub and NOT scroll (item 32). That contract
    // was first written here, offscreen, and this harness CANNOT decide it: with the
    // fix removed the synthetic press-drag still scrubbed (1000 -> 1008) with contentY
    // at 0, and the one "steal" signal it appeared to show - ScrubSpinBox's `scrubbing`
    // going false - was this file reading the AeNumberField WRAPPER, which has no such
    // property (a missing property reads as an invalid QVariant, i.e. false). A
    // Flickable's steal needs the real pointer path, so the gate for this line is
    // scripts/gui/probe_scrub.sh, which drags with xdotool and watches the scroll bar.
    // This note stays so nobody re-invents a version that is green by construction.

    // ---- D25: every control is REACHABLE (focus movement scrolls it in) ------
    {
        int checked = 0, walkFails = 0;
        // Start scrolled to the TOP: the controls deep in the form then NEED the
        // focus-following scroll (a walk that never leaves the viewport proves
        // nothing - measured lesson from the first draft of this block, which
        // passed with ensureVisible deliberately disabled).
        f.scroller->setProperty("contentY", 0.0);
        spin(40);
        const auto controls = f.root->findChildren<QQuickItem*>();
        for (QQuickItem* c : controls) {
            const QByteArray cn(c->metaObject()->className());
            const bool isControl = cn.contains("AeButton") || cn.contains("AeComboBox")
                || cn.contains("AeNumberField") || cn.contains("AeTextField")
                || cn.contains("AeCheckBox") || cn.contains("AeIconButton")
                || cn.contains("AeFrameRangeRow") || cn.contains("AeDisclosure")
                || c->objectName() == QLatin1String("destinationRow");
            if (!isControl || !c->isVisible()) continue;
            // The disclosure's own `open` is bound to the model, and the model can
            // close the section by itself when the artifact stops having Advanced
            // content (applyArtifactConstraints). The two must never disagree: a
            // reset that is not announced leaves the caret - and what the screen
            // reader is told - saying "Expanded" over a shut section.
            if (cn.contains("AeDisclosure")
                && c->property("text").toString() == QStringLiteral("Advanced")) {
                check(c->property("open").toBool() == f.model->advancedOpen(),
                      "D25: the Advanced disclosure never contradicts the model's advancedOpen");
            }
            // Controls outside the scrolling page (the pinned footer's own) are
            // reachable BY CONSTRUCTION - their reachability is Q9's assert
            // below, and they are not scroll targets.
            bool inPage = false;
            for (QQuickItem* p = c; p; p = p->parentItem())
                if (p == f.page) { inPage = true; break; }
            if (!inPage) continue;
            // Land focus the way a keyboard would land it, then assert the whole
            // control is in the visible viewport (its row scrolled in with it).
            c->forceActiveFocus();
            spin(40);
            const QRectF r = f.rectInPage(c);
            const QRectF v = f.viewportInPage();
            if (!rectFits(r, v)) {
                ++walkFails;
                check(false, QString("D25: control '%1' is fully reachable (%2 not in %3)")
                                 .arg(c->property("text").toString().isEmpty()
                                      ? c->property("label").toString()
                                      : c->property("text").toString())
                                 .arg(rs(r), rs(v)).toUtf8().constData());
            }
            ++checked;
        }
        check(checked >= 10,
              QString("precondition: the walk covered the form's controls (%1 checked)").arg(checked)
                  .toUtf8().constData());
        check(walkFails == 0 && checked >= 10,
              QString("D25: all %1 in-page controls reachable (fully in viewport after focus)")
                  .arg(checked).toUtf8().constData());
        // Q9's half: the footer's own buttons never scroll away.
        QQuickItem* cancelBtn = nullptr;
        for (QQuickItem* c : f.footer->findChildren<QQuickItem*>())
            if (c->property("text").toString() == QLatin1String("Cancel")) { cancelBtn = c; break; }
        check(cancelBtn != nullptr, "precondition: the footer's Cancel button exists");
        if (cancelBtn) {
            const QPointF p = cancelBtn->mapToItem(f.root, 0, 0);
            check(rectFits(QRectF(p.x(), p.y(), cancelBtn->width(), cancelBtn->height()),
                           QRectF(0, 0, f.root->width(), f.root->height())),
                  "Q9: the action row's buttons are in the window without any scrolling");
        }
    }

    // ---- E28: a tooltip inside the scrolling content is clipped by NOTHING ---
    {
        // The BOTTOM-MOST kept-tip button, scrolled to sit 10px above the
        // viewport's bottom edge - where a window-clamped bubble would be cut by
        // the Flickable's clip and only a clip-aware clamp survives (the
        // measured defect class). It also sits at its row's right end, so the
        // horizontal clamp is exercised in the same case.
        QQuickItem* tipOwner = nullptr;
        qreal best = -1;
        for (QQuickItem* c : f.root->findChildren<QQuickItem*>()) {
            if (!QByteArray(c->metaObject()->className()).contains("AeIconButton")) continue;
            if (!c->property("showTip").toBool()) continue;
            const qreal b = f.rectInPage(c).bottom();
            if (b > best) { best = b; tipOwner = c; }
        }
        check(tipOwner != nullptr, "precondition: a kept-tip reset button exists");
        if (tipOwner) {
            const QRectF r = f.rectInPage(tipOwner);
            qreal cy = r.bottom() + 10 - f.scroller->height();
            cy = qMax(0.0, qMin(f.contentH() - f.scroller->height(), cy));
            check(cy > 0, "precondition: the edge case really scrolls (the button is deep in the form)");
            f.scroller->setProperty("contentY", cy);
            spin(60);
            QQuickItem* tip = nullptr;
            for (QQuickItem* c : tipOwner->findChildren<QQuickItem*>())
                if (QByteArray(c->metaObject()->className()).contains("AeToolTip")) {
                    tip = c; break;
                }
            check(tip != nullptr, "precondition: the button's AeToolTip is there");
            if (tip) {
                hoverAt(f.win, f.win->contentItem(), tipOwner,
                        QPointF(tipOwner->width() / 2, tipOwner->height() / 2));
                spin(800); // past the 500ms E26 delay
                check(tip->property("revealed").toBool(),
                      "precondition: the tooltip revealed after its delay");
                const QPointF tp = tip->mapToItem(f.page, 0, 0);
                const QRectF tipRect(tp.x(), tp.y(), tip->width(), tip->height());
                check(rectFits(tipRect, f.viewportInPage()),
                      QString("E28: the bubble fits the VISIBLE viewport, not just the window (%1 in %2)")
                          .arg(rs(tipRect), rs(f.viewportInPage())).toUtf8().constData());
                // And the edge case is real: with the owner 10px above the
                // bottom, a below-first bubble would end past the viewport - so
                // E27 must have flipped it above (or clamped) rather than cut.
                check(tipRect.bottom() <= f.viewportInPage().bottom()
                          && tipRect.top() <= r.top(),
                      "E27/E28: at the bottom edge the bubble flipped or clamped, never past the view");
            }
        }
    }

    // ---- G37 survives D25: the in-scene combo popup lives outside the clip ---
    {
        QQuickItem* combo = f.byName("Format");
        check(combo != nullptr, "precondition: the Format combo is addressable");
        QQuickItem* popupItem = nullptr;
        QObject* popupObj = nullptr;
        if (combo) {
            for (QQuickItem* c : combo->findChildren<QQuickItem*>())
                if (QByteArray(c->metaObject()->className()).contains("QQuickPopupItem")) {
                    popupItem = c;
                    popupObj = c->parent();
                    break;
                }
        }
        check(popupItem != nullptr, "precondition: the combo's popup item exists");
        if (popupItem) {
            // Structural: the popup must not be inside the scrolling page (or
            // the clip would cut it wherever it lands).
            bool inPage = false;
            for (QQuickItem* p = popupItem->parentItem(); p; p = p->parentItem()) {
                if (p == f.page) { inPage = true; break; }
            }
            check(!inPage, "G37: the popup is outside the scrolling content (clip cannot reach it)");
            QMetaObject::invokeMethod(popupObj, "open");
            spin(120);
            const QPointF pp = popupItem->mapToItem(f.root, 0, 0);
            const QRectF pr(pp.x(), pp.y(), popupItem->width(), popupItem->height());
            check(rectFits(pr, QRectF(0, 0, f.root->width(), f.root->height())),
                  QString("G37/D25: the opened popup sits inside the window (%1)").arg(rs(pr))
                      .toUtf8().constData());
        }
    }
    dispose(&f);

    // =====================================================================
    // The no-overflow scenario: geometry must be EXACTLY the designed look -
    // no scroll, no bar - so a fitting form changed nothing (the pixel proof
    // is overflow-spike's before/after AE on min_closed).
    // =====================================================================
    {
        Form g;
        makeForm(620, 720, false, 1, &g);
        check(g.contentY() == 0 && g.contentX() == 0, "no overflow: the content sits at rest");
        check(g.contentH() <= g.scroller->height(),
              "precondition: the content fits at this size");
        check(!g.vbar->isVisible(), "D23: the scroll bar is hidden when nothing overflows");
        const qreal y0 = g.footer->mapToItem(g.root, 0, 0).y();
        QWheelEvent we(QPointF(280, 300), QPointF(280, 300), QPoint(0, 0), QPoint(0, -240),
                       Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(g.win, &we);
        spin(60);
        check(g.contentY() == 0, "no overflow: the wheel cannot scroll the content away");
        check(g.footer->mapToItem(g.root, 0, 0).y() == y0, "no overflow: the row is where it was");
        dispose(&g);
    }

    // =====================================================================
    // K46: which quality row belongs to which artifact.
    //
    // The Quality row belongs to the video encoder that writes the file, and
    // "Image quality" to QImage::save(); the two are complements by construction
    // (`qualityVisible = !imageQualityVisible`), so every case asserts BOTH rows -
    // one shown, the other hidden. Asserting only the row that ought to be there
    // is the test that missed the defect: `visible: !exportModel.manualMode()` was
    // the CALL form of a Q_PROPERTY, which registers no binding dependency, threw
    // a TypeError and wrote no value at all - so the row kept its default `true`
    // and was drawn in every mode, doubling with "Image quality" wherever Qt wrote
    // the file. A binding that cannot evaluate is invisible to a test that never
    // looks at the other half.
    //
    // Each case states its own precondition - the mode, the resolved format id,
    // and that the row's section is on screen - so a screen that could not be
    // reached FAILs loudly instead of passing quietly.
    // =====================================================================
    {
        auto says = [](QQuickItem* it) {
            if (!it) return QStringLiteral("absent from the tree");
            return it->isVisible() ? QStringLiteral("shown") : QStringLiteral("hidden");
        };
        auto formatIndexOf = [](gui::ExportViewModel* m, const char* id) {
            const QVariantList options = m->formatOptions();
            for (int i = 0; i < options.size(); ++i) {
                if (options.at(i).toMap().value(QStringLiteral("value")).toString()
                    == QLatin1String(id)) {
                    return i;
                }
            }
            return -1;
        };
        auto assertRows = [&](Form* fm, const QString& what, bool qualityShown) {
            QQuickItem* qLabel = nullptr;
            QQuickItem* qCombo = nullptr;
            qualityRow(fm->root, &qLabel, &qCombo);
            QQuickItem* iLabel = nullptr;
            QQuickItem* iField = nullptr;
            imageQualityRow(fm->root, &iLabel, &iField);
            check(qLabel && qCombo && iLabel && iField,
                  QStringLiteral("K46 %1: both rows are whole in the tree (Quality label %2, "
                                 "combo %3, Image quality label %4, field %5)")
                      .arg(what, says(qLabel), says(qCombo), says(iLabel), says(iField)));
            if (!qCombo || !iField) return;
            check(qCombo->parentItem() && qCombo->parentItem()->isVisible(),
                  QStringLiteral("K46 %1: the row's section is on screen, so \"hidden\" below "
                                 "means hidden by its own binding").arg(what));
            const bool qShown = qLabel && qLabel->isVisible() && qCombo->isVisible();
            const bool iShown = iLabel && iLabel->isVisible() && iField->isVisible();
            check(qShown == qualityShown,
                  QStringLiteral("K46 %1: the encoder's Quality row is %2, and %3 is what this "
                                 "artifact should give it")
                      .arg(what, says(qCombo),
                           qualityShown ? QStringLiteral("shown") : QStringLiteral("hidden")));
            check(iShown == !qualityShown,
                  QStringLiteral("K46 %1: QImage's Image quality row is %2 - the complement, "
                                 "never both and never neither")
                      .arg(what, says(iField)));
        };

        // Manual mode, a video container: no destination describes the artifact, so
        // the Format row IS the artifact and a video encoder writes it.
        {
            Form fm;
            makeForm(620, 720, false, 1, &fm);
            fm.model->setDestinationId(QString());
            spin(60);
            const int mp4 = formatIndexOf(fm.model, "mp4");
            check(fm.model->manualMode() && mp4 >= 0,
                  QStringLiteral("K46 precondition: manual mode offers MP4 in the Format row "
                                 "(mode %1, format %2)")
                      .arg(fm.model->manualMode() ? QStringLiteral("manual") : QStringLiteral("not manual"),
                           fm.model->formatId()));
            if (mp4 >= 0) fm.model->setFormatIndex(mp4);
            spin(60);
            check(!fm.model->formatId().startsWith(QLatin1String("image/")),
                  QStringLiteral("K46 precondition: a video encoder writes this artifact (%1)")
                      .arg(fm.model->formatId()));
            assertRows(&fm, QStringLiteral("manual + MP4"), true);
            dispose(&fm);
        }

        // Manual mode, one of the two image/* formats: QImage::save owns the file,
        // so the row that matters is its own number. This is the case the defect
        // showed the wrong control for, and it needs no destination to reach.
        {
            Form fm;
            makeForm(620, 720, false, 1, &fm);
            fm.model->setDestinationId(QString());
            spin(60);
            const int png = formatIndexOf(fm.model, "image/png");
            check(png >= 0, "K46 precondition: PNG sequence is on the manual format list");
            if (png >= 0) fm.model->setFormatIndex(png);
            spin(60);
            check(fm.model->formatId() == QLatin1String("image/png"),
                  QStringLiteral("K46 precondition: the artifact is the sequence QImage writes (%1)")
                      .arg(fm.model->formatId()));
            assertRows(&fm, QStringLiteral("manual + PNG sequence"), false);
            dispose(&fm);
        }

        // A destination whose artifact is a single still.
        {
            Form fm;
            makeForm(620, 720, false, 1, &fm);
            fm.model->setDestinationId(QStringLiteral("still.pixiv.illustration"));
            spin(60);
            check(!fm.model->manualMode() && fm.model->imageQualityVisible(),
                  "K46 precondition: pixiv's illustration row is a still, so QImage writes it");
            assertRows(&fm, QStringLiteral("still.pixiv.illustration"), false);
            dispose(&fm);
        }

        // A destination whose artifact is a sequence of stills.
        {
            Form fm;
            makeForm(620, 720, false, 1, &fm);
            fm.model->setDestinationId(QStringLiteral("post.pixiv.ugoira"));
            spin(60);
            check(fm.model->imageQualityVisible(),
                  "K46 precondition: ugoira is a folder of numbered stills, so QImage writes it");
            assertRows(&fm, QStringLiteral("post.pixiv.ugoira"), false);
            dispose(&fm);
        }

        // A destination whose artifact is a video: the encoder's own number is the
        // one that reaches the file, and Qt's image writer never touches it.
        {
            Form fm;
            makeForm(620, 720, false, 1, &fm);
            fm.model->setDestinationId(QStringLiteral("chat.discord.file"));
            spin(60);
            check(!fm.model->imageQualityVisible(),
                  "K46 precondition: Discord's chat row is written by a video encoder");
            assertRows(&fm, QStringLiteral("chat.discord.file"), true);
            dispose(&fm);
        }
    }

    // =====================================================================
    // The resolution row: the two boxes hold the USER's request, the facts hold
    // the FILE (ruled 2026-10-07). A destination's <size mode="max"> is a CEILING
    // on the output - "no bigger than 1280x720" - not a decision about either of
    // the two numbers the user types, so a 1080x1080 project over
    // chat.whatsapp.video's 1280x720 ceiling must OPEN at 1080x1080, RESET to
    // 1080x1080, and, with the link on, derive Height from the PROJECT's ratio.
    // The superseded code wrote the plan's fitted size into the boxes and carried
    // the ceiling as their maximum: it opened 720x720, "Reset to native" landed
    // 1080x720 (the height clamped by the ceiling), and an aspect edit clamped the
    // derived partner to 720 - the "aspect ratio linking is broken" the user
    // reported. tests/export_form pins this on the MODEL; this block drives the
    // REAL AeNumberFields and AeIconButtons the user actually edits.
    // =====================================================================
    {
        const QString dest = QStringLiteral("chat.whatsapp.video");
        const QSize native(1080, 1080);
        const QList<ctrl::ExportTarget> targets = ctrl::ExportTargets::load();
        const ctrl::ExportTarget ceiling = ctrl::ExportTargets::find(targets, dest);
        check(ceiling.size.mode == ctrl::SizeMode::Max && ceiling.size.w == 1280
                  && ceiling.size.h == 720,
              QStringLiteral("resolution precondition: %1 is the 1280x720 CEILING this rule is "
                             "about (%2x%3)").arg(dest).arg(ceiling.size.w).arg(ceiling.size.h));
        check(native.height() > ceiling.size.h,
              QStringLiteral("resolution precondition: the project's %1 height exceeds the ceiling's "
                             "%2, so the fit and the request are distinguishable")
                  .arg(native.height()).arg(ceiling.size.h));

        Form f;
        makeForm(620, 720, false, 1, &f, dest, native);
        check(f.root && f.model, "resolution precondition: the dialog built with its view model");

        // Find the resolution row's controls the way this file finds controls: the
        // fields by the label AeNumberField carries, the shared reset and the link
        // toggle among the SAME row's children. The row's own parent is what tells the
        // resolution reset from the fps field's own AeIconButton, which carries the
        // very same "Reset to native" label.
        auto controlByLabel = [](QQuickItem* root, const char* classNeedle,
                                 const QString& label) -> QQuickItem* {
            if (!root) return nullptr;
            for (QQuickItem* c : root->findChildren<QQuickItem*>())
                if (QByteArray(c->metaObject()->className()).contains(classNeedle)
                    && c->property("label").toString() == label)
                    return c;
            return nullptr;
        };
        QQuickItem* widthField = controlByLabel(f.root, "AeNumberField", QStringLiteral("Width"));
        QQuickItem* heightField = controlByLabel(f.root, "AeNumberField", QStringLiteral("Height"));
        QQuickItem* resetBtn = nullptr;
        QQuickItem* aspectBtn = nullptr;
        if (widthField && widthField->parentItem()) {
            for (QQuickItem* c : widthField->parentItem()->childItems()) {
                if (!QByteArray(c->metaObject()->className()).contains("AeIconButton")) continue;
                const QString label = c->property("label").toString();
                if (label == QStringLiteral("Reset to native")) resetBtn = c;
                else if (label == QStringLiteral("Keep aspect ratio")) aspectBtn = c;
            }
        }
        check(widthField && heightField && resetBtn && aspectBtn,
              QStringLiteral("resolution precondition: the Width/Height fields, the shared "
                             "Reset to native and the Keep aspect ratio toggle are whole in the row"));
        check(f.model->destinationId() == dest && f.model->sizeEditable(),
              QStringLiteral("resolution precondition: %1 only BOUNDS the size, so the row is live "
                             "(%2)").arg(dest).arg(f.model->sizeEditable()));
        check(widthField && widthField->isVisible() && heightField && heightField->isVisible(),
              "resolution precondition: the resolution row is on screen, so what it shows is real");

        if (!widthField || !heightField || !resetBtn || !aspectBtn) {
            dispose(&f);
        } else {
            // ---- 1. the boxes open at the PROJECT's size ----------------------
            const qreal openWidth = widthField->property("value").toReal();
            const qreal openHeight = heightField->property("value").toReal();
            check(openWidth == native.width() && openHeight == native.height(),
                  QStringLiteral("the Width and Height fields show the PROJECT's 1080x1080, not the "
                                 "ceiling's fitted 720x720: the boxes are the request (%1x%2) - the "
                                 "superseded code wrote the plan's fit here")
                      .arg(openWidth).arg(openHeight));

            // ---- 2. the facts still state the FILE (the invariant, not the fix) ----
            const ctrl::ExportPlan plan = f.model->lastPlan();
            check(plan.size.width() == 720 && plan.size.height() == 720,
                  QStringLiteral("resolution precondition: the plan really fits the 1280x720 "
                                 "ceiling (%1x%2)").arg(plan.size.width()).arg(plan.size.height()));
            check(f.model->factsText().contains(QStringLiteral("720 x 720")),
                  QStringLiteral("the facts still state the ceiling-fitted FILE (720 x 720) while "
                                 "the boxes hold the project's request: the boxes are the request, "
                                 "the facts are the file, and a fix must not make them the same"));

            // ---- 3. Reset to native lands BOTH fields at the project's size ----
            check(QMetaObject::invokeMethod(resetBtn, "clicked"),
                  "resolution: the Reset to native button's own action ran (clicked)");
            spin(60);
            const qreal resetWidth = widthField->property("value").toReal();
            const qreal resetHeight = heightField->property("value").toReal();
            check(resetWidth == native.width() && resetHeight == native.height(),
                  QStringLiteral("Reset to native puts BOTH fields at the project's 1080x1080 "
                                 "(%1x%2) - the superseded code's ceiling was also the field's "
                                 "maximum, so the height clamped back to 720")
                      .arg(resetWidth).arg(resetHeight));

            // ---- 4. the link derives Height from the PROJECT's ratio -----------
            check(aspectBtn->property("checked").toBool() && f.model->keepAspectRatio(),
                  "aspect precondition: Keep aspect ratio is ON (its default) before the edit");
            // The field's OWN edit path, not a model write: an accessibility write takes
            // the same route as a typed edit (AeNumberField's valueNode) - valueEdited,
            // which is exactly what the dialog's onValueEdited handles.
            QQuickItem* widthValueNode =
                widthField->findChild<QQuickItem*>(QStringLiteral("aeAccessibleValue"));
            check(widthValueNode != nullptr,
                  "aspect precondition: the Width field's own value node is there to take the edit");
            check(widthValueNode
                      && QMetaObject::invokeMethod(widthValueNode, "accessibleSetValue",
                                                   Q_ARG(QVariant, 1000)),
                  "aspect: the Width field's own edit path ran (accessibleSetValue)");
            spin(60);
            const qreal linkedHeight = heightField->property("value").toReal();
            check(widthField->property("value").toReal() == 1000 && linkedHeight == 1000,
                  QStringLiteral("with the link on, Width 1000 derives Height 1000 from the "
                                 "PROJECT's 1:1 ratio (%1), not the ceiling's 720 - a ceiling "
                                 "bounds the file, not the box")
                      .arg(linkedHeight));
            dispose(&f);
        }
    }

    // =====================================================================
    // Auto is a STATE, not a rounded comparison (ruled 2026-10-08: "the auto value
    // handling for CRF and Size cap is terrible ... Auto needs to be a special value,
    // not just a visual thing").
    //
    // Two claims, both read off the REAL dialog and its own controls:
    //
    //   THE WORD - the size cap field draws "Auto" exactly when the model says the cap
    //   IS Auto. The superseded field compared Math.round(value) with a sentinel, so a
    //   0.25 MB cap - a real cap, 256 KiB, the value scripts/gui/probe_capped.sh sets -
    //   painted the word "Auto" over itself while the export really was fitted under
    //   it. The observation that cannot be faked is the word's own visibility.
    //
    //   CUSTOM - while the user has named the codec's own number, the Quality combo
    //   reads "Custom" and stays USABLE rather than going grey ("when CRF is used
    //   Quality should change to Custom instead of disabling, this is established UI
    //   convention"), and choosing a bucket from it hands the decision back, so Custom
    //   cannot outlive the choice that ended it.
    // =====================================================================
    {
        Form f;
        makeForm(560, 760, true, 1, &f, QStringLiteral("discord"), QSize(1080, 1080));
        QQuickItem* cap = f.byName("sizeCapField");
        QQuickItem* capWord =
            cap ? cap->findChild<QQuickItem*>(QStringLiteral("aeAutoWord")) : nullptr;
        check(cap && capWord && cap->isVisible(),
              "auto precondition: the size cap field and its Auto word are whole and on screen");
        if (!cap || !capWord || !cap->isVisible()) {
            dispose(&f);
        } else {
            check(f.model->sizeCapAuto() && capWord->isVisible(),
                  "auto: a cap nobody set draws the word (the word IS the state)");
            QQuickItem* capNode =
                cap->findChild<QQuickItem*>(QStringLiteral("aeAccessibleValue"));
            check(capNode != nullptr, "auto precondition: the cap field's own value node is there");
            check(capNode
                      && QMetaObject::invokeMethod(capNode, "accessibleSetValue",
                                                   Q_ARG(QVariant, 0.25)),
                  "auto: the cap field's own edit path ran (0.25 MB)");
            spin(60);
            check(!f.model->sizeCapAuto() && f.model->sizeCapValue() == 0.25,
                  QStringLiteral("auto: 0.25 MB is a REAL cap, not the word (%1)")
                      .arg(f.model->sizeCapValue()));
            check(!capWord->isVisible(),
                  "auto: so the field must not paint Auto over it - the superseded comparison "
                  "rounded 0.25 to the sentinel and did exactly that");
            check(capNode->property("accessibleValue").toReal() == 0.25,
                  "auto: the value a probe and a screen reader read is the number it shows");
            f.model->setSizeCapToAuto();
            spin(30);
            check(f.model->sizeCapAuto() && capWord->isVisible(),
                  "auto: Reset to auto brings the state - and its word - back");

            // ---- the Quality combo's fourth answer --------------------------
            QQuickItem* combo = f.byName("Quality");
            QQuickItem* overrideField = f.byName("qualityOverrideField");
            const int buckets = f.model->qualityLabels().size();
            check(combo && overrideField && combo->isVisible() && overrideField->isVisible(),
                  "custom precondition: the Quality combo and the codec's own field are on screen");
            check(f.model->qualityOverrideVisible(),
                  "custom precondition: this destination's codec has a knob to name");
            if (combo && overrideField && f.model->qualityOverrideVisible()) {
                check(combo->property("enabled").toBool()
                          && combo->property("index").toInt() == f.model->qualityIndex(),
                      "custom precondition: with the intent in charge it is the plain bucket list");
                QQuickItem* overrideNode =
                    overrideField->findChild<QQuickItem*>(QStringLiteral("aeAccessibleValue"));
                check(overrideNode != nullptr,
                      "custom precondition: the codec field's own value node is there");
                const int named = f.model->qualityOverrideMinimum();
                check(overrideNode
                          && QMetaObject::invokeMethod(overrideNode, "accessibleSetValue",
                                                       Q_ARG(QVariant, named)),
                      QStringLiteral("custom: the user names the codec's own number (%1)").arg(named));
                spin(60);
                check(!f.model->qualityOverrideAuto(),
                      QStringLiteral("custom precondition: naming it left Auto (%1)")
                          .arg(f.model->qualityOverrideValue()));
                check(combo->property("enabled").toBool(),
                      "custom: the Quality combo stays USABLE (the report was that it went grey)");
                const QString shown = combo->property("text").toString();
                check(combo->property("index").toInt() == buckets
                          && shown == f.model->qualityCustomLabel(),
                      QStringLiteral("custom: so it READS Custom (%1) beside the three buckets")
                          .arg(shown));
                check(combo->property("options").toList().size() == buckets + 1,
                      "custom: and the Custom entry exists only while it IS the state");
                // The combo's OWN activation path (what its popup emits), not the model's
                // setter: this is the wiring the user's click goes through.
                check(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 1)),
                      "custom: a bucket is chosen from the combo's own activation path");
                spin(60);
                check(!f.model->qualityCustom() && f.model->qualityOverrideAuto(),
                      "custom: choosing a bucket hands the decision back (Custom ends with it)");
                const QString back = combo->property("text").toString();
                check(combo->property("index").toInt() == 1
                          && back == f.model->qualityLabels().at(1),
                      QStringLiteral("custom: and the combo shows the bucket again (%1)").arg(back));
            }
            dispose(&f);
        }
    }

    // =====================================================================
    // Transparency is a REGULAR option and a default-OFF one (ruled 2026-10-03:
    // "why is transparency under Advanced? since it pushes the codec choice onto
    // a suboptimal codec it belongs in the regular options, and off by default").
    //
    // Two claims, each with its own catchable observation:
    //
    //   PLACEMENT - the row is not part of the Advanced subtree, and opening or
    //   closing Advanced neither moves it nor stops drawing it. The observation
    //   that cannot be faked is effective visibility: an item under an invisible
    //   parent is not visible, so while Advanced is SHUT the box must still be
    //   visible - inside that subtree it could only be false. The Advanced
    //   container is identified by its own residents (the loop and audio rows
    //   share it), and that identification is asserted, not assumed. The one form
    //   is then toggled both ways at runtime, so "the row did not move" is read
    //   off a form that really did relayout in between.
    //
    //   DEFAULT - an untouched Init (no saved preference) leaves the row
    //   unchecked on a destination that offers the row and states no default of
    //   its own (a GIF row: alpha="choice"). A destination whose own rule is
    //   alpha="default-on" must still come up checked, and one whose codec cannot
    //   carry alpha must still hide the row.
    //
    // Every form here is created through setContextProperty("exportModel", ...) on
    // the shared root context, so only ONE may be alive at a time: while a second
    // form exists, the first one's bindings follow the NEW model. Measured: building
    // the second form with Advanced open made the first (built shut) form lay itself
    // out open too - both reported contentHeight 751 and the Advanced container was
    // visible in both. Disposing each form before the next is built keeps the
    // comparison honest, and it is also what keeps a torn-down form's bindings from
    // being re-evaluated against a null context property.
    // =====================================================================
    {
        auto sameRect = [](const QRectF& a, const QRectF& b) {
            return qAbs(a.x() - b.x()) <= 0.5 && qAbs(a.y() - b.y()) <= 0.5
                && qAbs(a.width() - b.width()) <= 0.5 && qAbs(a.height() - b.height()) <= 0.5;
        };
        // The checkbox carries no objectName of its own (the probes address the row
        // by its words), so it is looked up by the text the model hands it - the
        // same string a probe reads, and a lookup that finds nothing is asserted
        // below rather than passing quietly.
        auto boxByText = [](Form* f, const QString& text) -> QQuickItem* {
            if (!f->root || text.isEmpty()) return nullptr;
            for (QQuickItem* c : f->root->findChildren<QQuickItem*>())
                if (c->property("text").toString() == text) return c;
            return nullptr;
        };
        auto transparencyBox = [&](Form* f) {
            return boxByText(f, f->model->transparency()->label());
        };
        // An item is inside `anc` when the walk up the item tree reaches it. The
        // walk uses the C++ parent pointer, which is the same relation the dialog's
        // own QML walks use (`.parent` there, `parentItem` being undefined in QML).
        auto isInside = [](QQuickItem* it, QQuickItem* anc) {
            for (QQuickItem* p = it; p; p = p->parentItem())
                if (p == anc) return true;
            return false;
        };

        // A GIF row: its table entry is alpha="choice", so the row is a live choice
        // and the value it shows can only have come from Init's default.
        const QString dest = QStringLiteral("twitch.emote");
        Form shut; // built with Advanced SHUT, opened and closed again at runtime
        makeForm(560, 640, false, 4, &shut, dest);
        QQuickItem* shutBox = transparencyBox(&shut);
        check(shutBox, "transparency: the row was found by the label the model hands it");
        check(shut.model->transparency()->visible(),
              "transparency precondition: this destination offers the row or nothing below can be "
              "observed");
        check(shut.model->advancedVisible() && !shut.model->advancedOpen(),
              "advanced precondition: the form was built offered, but SHUT");
        check(shutBox && shutBox->isVisible(),
              "transparency: drawn while Advanced is SHUT (an item under an invisible parent is not "
              "visible, so this fails if the row is back inside the subtree)");

        // ---- default: OFF unless a saved preference or the destination says otherwise
        check(shut.model->transparency()->enabled(),
              "transparency precondition: the row is a real choice here (not frozen by the "
              "destination)");
        check(!shut.model->transparency()->value(),
              "transparency default: an untouched Init leaves the row UNCHECKED");
        if (shutBox)
            check(!shutBox->property("checked").toBool(),
                  "transparency default: the checkbox the user sees is UNCHECKED too");

        // The Advanced container, identified by its residents: loop and audio are
        // both rows of it (audio stays hidden for a GIF, and a hidden resident is
        // still a child of the container).
        QQuickItem* shutLoop = boxByText(&shut, shut.model->loop()->label());
        QQuickItem* shutAudio = boxByText(&shut, shut.model->audio()->label());
        check(shutLoop && shutAudio, "advanced precondition: the loop and audio rows exist");
        QQuickItem* advCol = shutLoop ? shutLoop->parentItem() : nullptr;
        if (shutLoop && shutAudio && shutBox && advCol) {
            check(advCol == shutAudio->parentItem(),
                  "advanced precondition: the loop and audio rows share one container (the Advanced "
                  "column), so that container can be addressed");
            check(!advCol->property("visible").toBool()
                      && advCol->property("visible").toBool() == shut.model->advancedOpen(),
                  "advanced precondition: that container is the Advanced subtree itself (hidden "
                  "while Advanced is shut)");
            check(!advCol->isVisible() && !shutLoop->isVisible(),
                  "advanced precondition: the container and its residents agree that it is hidden");
            check(isInside(shutLoop, advCol) && isInside(shutAudio, advCol),
                  "advanced precondition: the walk finds residents of it");
            check(!isInside(shutBox, advCol),
                  "transparency: the row is NOT a descendant of the Advanced subtree");
        } else {
            check(false,
                  "transparency: the loop/audio rows the placement claim is read from exist");
        }

        // Toggle the disclosure both ways on this one form. Opening it must relayout
        // (its residents become real rows), and the transparency row must neither
        // move nor stop being drawn while that happens.
        if (shutBox && shutBox->isVisible()) {
            const QRectF before = shut.rectInPage(shutBox);
            const qreal heightBefore = shut.contentH();
            shut.model->setAdvancedOpen(true);
            spin(150);
            const QRectF opened = shut.rectInPage(shutBox);
            const qreal heightOpen = shut.contentH();
            QTextStream(stdout) << QStringLiteral("[measured] 560x640, four ranges, GIF row, "
                                                  "Advanced shut -> open: contentHeight %1 -> %2")
                                       .arg(heightBefore)
                                       .arg(heightOpen)
                                << "\n";
            check(shut.model->advancedOpen(), "advanced precondition: the toggle reached the model");
            check(advCol && advCol->isVisible() && shutLoop && shutLoop->isVisible(),
                  "advanced precondition: opening it really drew its subtree");
            check(heightOpen > heightBefore + 5.0,
                  QStringLiteral("advanced precondition: opening it grew the content (%1 -> %2 "
                                 "content px), so \"the row did not move\" is not vacuous")
                      .arg(heightBefore)
                      .arg(heightOpen));
            check(shutBox->isVisible(), "transparency: still drawn after Advanced is opened");
            check(sameRect(before, opened),
                  QStringLiteral("transparency: opening Advanced does not move or reshape the row "
                                 "(%1 vs %2)")
                      .arg(rs(before), rs(opened)));
            shut.model->setAdvancedOpen(false);
            spin(150);
            check(advCol && !advCol->isVisible() && shutLoop && !shutLoop->isVisible(),
                  "advanced precondition: closing it hid the subtree again");
            check(shut.contentH() < heightOpen - 5.0,
                  QStringLiteral("advanced precondition: closing it shrank the content back "
                                 "(%1 -> %2 content px)")
                      .arg(heightOpen)
                      .arg(shut.contentH()));
            check(shutBox->isVisible(), "transparency: still drawn after Advanced is closed again");
            check(sameRect(before, shut.rectInPage(shutBox)),
                  "transparency: closing Advanced does not move or reshape the row either");
        }
        dispose(&shut);

        // A destination whose own rule asks for alpha still comes up checked: that is
        // the per-destination default winning over the global default, not a byte the
        // global default leaked.
        {
            Form on;
            makeForm(560, 640, false, 4, &on, QStringLiteral("discord.animation"));
            check(on.model->transparency()->visible() && on.model->transparency()->value(),
                  QStringLiteral("transparency default: a destination whose own rule is "
                                 "alpha=\"default-on\" still comes up checked (visible=%1 value=%2)")
                      .arg(on.model->transparency()->visible())
                      .arg(on.model->transparency()->value()));
            QQuickItem* onBox = transparencyBox(&on);
            check(onBox && onBox->property("checked").toBool(),
                  "transparency default: and the checkbox the user sees is checked");
            dispose(&on);
        }
        // And a codec that cannot carry alpha still hides the row, wherever it now
        // sits: the destination's own rule is alpha="forbidden" and the artifact is
        // an MP4 video.
        {
            Form opaque;
            makeForm(560, 640, false, 4, &opaque, QStringLiteral("chat.discord.file"));
            check(!opaque.model->transparency()->visible(),
                  "transparency: a destination that cannot carry alpha still hides the row");
            QQuickItem* opaqueBox = transparencyBox(&opaque);
            check(!opaqueBox || !opaqueBox->isVisible(),
                  "transparency: and the row is not drawn");
            dispose(&opaque);
        }

        // ---- (a) a section the MODEL closes by itself must not stay drawn open ----
        // applyArtifactConstraints() resets mAdvancedOpen when the incoming artifact
        // has no Advanced content at all. It used to announce only constraintsChanged();
        // the QML `open:` binding follows advancedOpenChanged, so the reset left the
        // caret - and the state the screen reader is told - saying "Expanded" over a
        // section that is shut. This walk is what caught it, and it is reachable from
        // the dialog itself (pick a destination with nothing to put in Advanced), so it
        // was a live path, not a hypothetical one; the reset now emits the change.
        // Walk open -> a destination with nothing to put in Advanced -> back, and read
        // the control, not the model: the model's own value was correct either way,
        // which is exactly why it went unnoticed.
        {
            Form f;
            makeForm(560, 640, false, 4, &f, dest); // twitch.emote: Advanced is offered
            QQuickItem* toggle = nullptr;
            // Two AeDisclosure instances live in this dialog ("Details" and "Advanced"),
            // so the class name alone is not enough: pick the one that says Advanced.
            for (QQuickItem* c : f.root->findChildren<QQuickItem*>())
                if (QByteArray(c->metaObject()->className()).contains("AeDisclosure")
                    && c->property("text").toString() == QStringLiteral("Advanced")) {
                    toggle = c;
                    break;
                }
            check(toggle, "advanced open state: the disclosure control was found");
            check(f.model->advancedVisible(),
                  "advanced open state precondition: this destination offers Advanced");
            f.model->setAdvancedOpen(true);
            spin(150);
            check(toggle && toggle->property("open").toBool(),
                  "advanced open state precondition: the control shows the section open");
            // A JPG still: one codec, no audio, no GIF, no knob - nothing to put in
            // Advanced, so advancedVisible goes false and the model closes the section
            // itself, without the user touching the disclosure.
            f.model->setDestinationId(QStringLiteral("still.instagram.feed"));
            spin(150);
            check(!f.model->advancedVisible() && !f.model->advancedOpen(),
                  "advanced open state precondition: the new destination has no Advanced content, "
                  "so the model closed the section by itself");
            check(toggle && !toggle->property("open").toBool(),
                  "advanced open state: a section the model closed by itself is not still drawn as "
                  "open (the caret and the accessible state follow advancedOpenChanged)");
            f.model->setDestinationId(dest);
            spin(150);
            check(f.model->advancedVisible() && !f.model->advancedOpen(),
                  "advanced open state precondition: back at a destination that offers Advanced, "
                  "and it is still shut");
            check(toggle && !toggle->property("open").toBool(),
                  "advanced open state: and the control does not come back open");
            dispose(&f);
        }
    }

    // =====================================================================
    // Defect 6 (reported 2026-10-03, its own workstream): "the Details text
    // doesn't wrap and instead introduces horizontal scroll."
    //
    // PHASE 1 is measurement. No edit to ExportDialog.qml has been made for this
    // block yet, so what it prints is the BEFORE column, and every assert below is
    // a PRECONDITION (the note really was long, the disclosure really was open,
    // the narrow window really had something to pan) - never the fixed shape. A
    // short note can therefore not make the block pass vacuously, and the same run
    // stays meaningful on both sides of the fix.
    //
    // The destination is chosen BY MEASUREMENT, not by name: every destination in
    // the registry is offered to a throwaway view model (no QML, no window) and the
    // one whose note rows carry the most characters wins. Its id, row count and
    // character count are printed next to the runner-up, so a registry edit cannot
    // silently turn this into a one-line note.
    // =====================================================================
    {
        const int ranges = 4;
        // The same Init makeForm() below builds, so the notes measured here are the
        // notes the forms below really show.
        // `ranges` is a constant expression, so it needs no capture.
        auto initFor = [](const QString& dest) {
            gui::ExportViewModel::Init init;
            init.nativeSize = QSize(1000, 400);
            init.nativeFps = 24;
            init.projectFirstFrame = 0;
            init.projectLastFrame = 59;
            init.playhead = 3;
            QList<ctrl::FrameRange> rs;
            for (int i = 0; i < ranges; ++i) rs << ctrl::FrameRange{i * 10, i * 10 + 5};
            init.ranges = rs;
            init.destinationId = dest;
            init.qualityIndex = 1;
            init.width = 1000;
            init.height = 400;
            return init;
        };

        const QList<ctrl::ExportTarget> targets = ctrl::ExportTargets::load();
        check(!targets.isEmpty(),
              QStringLiteral("defect 6 precondition: the destination registry loaded (%1 destinations)")
                  .arg(targets.size()));

        QString bestDest, secondDest;
        int bestLen = -1, bestRows = 0, secondLen = -1;
        for (const ctrl::ExportTarget& t : targets) {
            gui::ExportViewModel probe(initFor(t.id));
            QString notes;
            int rows = 0;
            for (const gui::ExportFact& fact : probe.facts()->facts()) {
                if (fact.id == QLatin1String("note")) {
                    notes += fact.value;
                    ++rows;
                }
            }
            if (notes.size() > bestLen) {
                secondDest = bestDest;
                secondLen = bestLen;
                bestDest = t.id;
                bestLen = notes.size();
                bestRows = rows;
            } else if (notes.size() > secondLen) {
                secondDest = t.id;
                secondLen = notes.size();
            }
        }
        QTextStream(stdout)
            << QStringLiteral("[measured] defect 6: longest Details text is %1 (%2 chars in %3 note "
                              "rows); runner-up %4 (%5 chars)")
                   .arg(bestDest).arg(bestLen).arg(bestRows).arg(secondDest).arg(secondLen)
            << "\n";
        check(bestLen >= 200 && bestRows >= 1,
              QStringLiteral("defect 6 precondition: the chosen destination states a note long enough "
                             "to wrap (%1 chars in %2 rows)")
                  .arg(bestLen).arg(bestRows));

        auto detailToggle = [](Form* f) -> QQuickItem* {
            for (QQuickItem* c : f->root->findChildren<QQuickItem*>()) {
                if (QByteArray(c->metaObject()->className()).contains("AeDisclosure")
                    && c->property("text").toString() == QLatin1String("Details"))
                    return c;
            }
            return nullptr;
        };
        // The note rows are Text items the Repeater builds from the facts model, and
        // they are the only texts in the dialog that carry the bullet prefix. Found
        // through the visual-tree walk above (a Repeater delegate has no QObject
        // parent in the item tree, so findChildren cannot see it).
        auto longestNote = [](Form* f) -> QQuickItem* {
            QQuickItem* best = nullptr;
            for (QQuickItem* c : visualTree(f->root)) {
                if (!c->property("text").toString().startsWith(QStringLiteral("• "))) continue;
                if (!best
                    || c->property("text").toString().size()
                           > best->property("text").toString().size())
                    best = c;
            }
            return best;
        };
        auto reportNote = [](const QString& when, QQuickItem* note) {
            if (!note) {
                QTextStream(stdout) << QStringLiteral("[measured] defect 6 %1 note Text: (none found)").arg(when)
                                    << "\n";
                return;
            }
            QTextStream(stdout)
                << QStringLiteral("[measured] defect 6 %1 note Text: width=%2 implicitWidth=%3 "
                                  "contentWidth=%4 lineCount=%5 height=%6 implicitHeight=%7 "
                                  "contentHeight=%8")
                       .arg(when)
                       .arg(note->width())
                       .arg(note->property("implicitWidth").toReal())
                       .arg(note->property("contentWidth").toReal())
                       .arg(note->property("lineCount").toInt())
                       .arg(note->height())
                       .arg(note->property("implicitHeight").toReal())
                       .arg(note->property("contentHeight").toReal())
                << "\n";
        };
        auto reportPage = [](const QString& when, Form* f) {
            QTextStream(stdout)
                << QStringLiteral("[measured] defect 6 %1 page: implicitWidth=%2 width=%3 "
                                  "scroller.width=%4 contentWidth=%5 contentHeight=%6")
                       .arg(when)
                       .arg(f->page->property("implicitWidth").toReal())
                       .arg(f->page->width())
                       .arg(f->scroller->width())
                       .arg(f->contentW())
                       .arg(f->contentH())
                << "\n";
        };

        // ---- the normal window: Details SHUT is the control -----------------
        Form g;
        makeForm(560, 640, false, ranges, &g, bestDest);
        check(g.scroller && g.page, "defect 6: the long-note form builds");
        QQuickItem* toggle = detailToggle(&g);
        check(g.model->facts()->noteCount() > 0 && toggle && toggle->isVisible(),
              QStringLiteral("defect 6 precondition: %1 shows a Details disclosure with %2 note "
                             "rows")
                  .arg(bestDest).arg(g.model ? g.model->facts()->noteCount() : -1));
        QQuickItem* shutNote = longestNote(&g);
        check(shutNote && !shutNote->isVisible(),
              "defect 6 precondition: the note delegates exist but are hidden while Details is shut");
        const qreal shutHeight = g.contentH();
        reportNote(QStringLiteral("shut"), shutNote);
        reportPage(QStringLiteral("shut"), &g);

        if (toggle) {
            toggle->setProperty("notesOpen", true);
            spin(200);
        }
        QQuickItem* note = longestNote(&g);
        check(note && note->isVisible(),
              "defect 6 precondition: the note rows are laid out once Details is open");
        check(note && note->property("text").toString().size() >= 200,
              QStringLiteral("defect 6 precondition: the visible note is the long one (%1 chars)")
                  .arg(note ? note->property("text").toString().size() : -1));
        reportNote(QStringLiteral("open"), note);
        reportPage(QStringLiteral("open"), &g);

        // The defect: the note is handed its unwrapped natural width, so it never
        // wraps and the page grows to that width - which turns into a horizontal
        // scrollbar. A wrapping note must be narrower than the page it lives on.
        const int lines = note ? note->property("lineCount").toInt() : 0;
        check(note && lines > 1,
              QStringLiteral("defect 6: the note wraps instead of running off the page "
                             "(lineCount=%1 note.width=%2)")
                  .arg(lines)
                  .arg(note ? note->width() : -1));
        check(g.contentW() <= g.scroller->width() + 0.5,
              QStringLiteral("defect 6: a note does not widen the page past the viewport "
                             "(contentWidth=%1 viewport=%2)")
                  .arg(g.contentW())
                  .arg(g.scroller->width()));
        check(g.contentH() > shutHeight,
              QStringLiteral("defect 6: opening Details makes the page taller, not wider "
                             "(%1 -> %2)")
                  .arg(shutHeight)
                  .arg(g.contentH()));
        dispose(&g);

        // ---- a narrow window: below the form's own minimum width, so the
        // horizontal axis is LEGITIMATE here (the form cannot be narrower) ------
        Form h;
        // 300px, not the 380px this block used before H41: the frame-range rows
        // were the form's widest row (AeFrameRangeRow is an implicit 320 wide, and
        // with the 104px label column beside it the page's minimum measured 552),
        // and moving them into the Advanced section brought the page's own minimum
        // down to a MEASURED 343. A 380px window is therefore no longer narrow
        // enough to overflow - 380 less the scroller's 32px of margins is 348,
        // which is more than the page wants - and the pan assertion below would fail
        // with no defect to find. The window is chosen against the measured minimum
        // so the axis really is the form's own.
        makeForm(300, 640, false, ranges, &h, bestDest);
        check(h.scroller && h.page, "defect 6: the long-note form builds narrow too");
        QQuickItem* narrowToggle = detailToggle(&h);
        if (narrowToggle) {
            narrowToggle->setProperty("notesOpen", true);
            spin(200);
        }
        reportNote(QStringLiteral("narrow"), longestNote(&h));
        reportPage(QStringLiteral("narrow"), &h);
        // The horizontal axis here comes from the form's own minimum width; the note's
        // length must not add to it. 1000px is well below the note's unwrapped width
        // (measured at ~4000) and well above the form's minimum (measured at 343).
        check(h.contentW() < 1000,
              QStringLiteral("defect 6: in a narrow window the horizontal axis is the form's own "
                             "minimum, not the note's unwrapped width (contentWidth=%1)")
                  .arg(h.contentW()));
        check(h.contentW() - h.scroller->width() >= 20,
              QStringLiteral("defect 6 precondition: a 300px window is narrower than the form's own "
                             "minimum (%1), so there is horizontal room to pan (contentWidth %2 vs "
                             "the viewport's %3)")
                  .arg(h.page->property("implicitWidth").toInt())
                  .arg(h.contentW()).arg(h.scroller->width()));
        h.scroller->setProperty("contentX", 20.0);
        spin(40);
        check(h.contentX() > 0,
              QStringLiteral("defect 6: a genuinely narrow window still pans horizontally (%1)")
                  .arg(h.contentX()));
        dispose(&h);
    }

    // ---- The window-route Escape (the CRITICAL of 2026-10-09). The
    // dialog's Escape Shortcut (ExportDialog.qml:77) is matched by
    // the shortcut map BEFORE the key reaches the scene, and only an
    // accepted ShortcutOverride can claim a key from a Shortcut.
    // scrub_escape.cpp sends keys straight at the editor item - a
    // route that bypasses the shortcut map entirely - so this
    // window-route case is the only instrument that can see the
    // defect. Escape in a scrub editor must end the EDIT and leave
    // the dialog open (F33/F34); Escape with no editor focused must
    // still cancel it (A2). The observable is the cancelRequested
    // SIGNAL, not operationCancelled: that property defaults to true
    // ("no accept yet"), so it cannot tell "cancelled" from "undecided".
    {
        Form g;
        // The base frame row: its First and Last fields are the scene's
        // scrub editors. Advanced is OPEN, because H41 (2026-10-09) put the frame
        // ranges behind that disclosure - an editor under an invisible parent takes
        // no focus, so a shut section would make every check below pass by finding
        // nothing. The editor is addressed THROUGH THE ROW that owns it rather than
        // by a tree-wide "scrubEdit" lookup: the rows moved from the first row of
        // the OUTPUT grid to the first row of Advanced, so a tree-wide lookup now
        // finds the Width box - the same objectName, a different field, and the
        // discarded-edit check below would stop proving anything about frames.
        makeForm(560, 640, true, 0, &g);
        QQuickItem* rangeRow = nullptr;
        for (QQuickItem* c : visualTree(g.root))
            if (QByteArray(c->metaObject()->className()).contains("AeFrameRangeRow")) {
                rangeRow = c;
                break;
            }
        check(rangeRow != nullptr,
              QStringLiteral("escape window route: a frame range row is in the scene"));
        QQuickItem* edit = rangeRow ? rangeRow->findChild<QQuickItem*>(QStringLiteral("scrubEdit"))
                                   : nullptr;
        // The editor itself is `visible: activeFocus` (ScrubSpinBox draws its value
        // display until an edit begins), so visibility is asserted on the ROW that
        // owns it, never on the editor: a focusless editor is invisible by design.
        check(edit != nullptr && rangeRow->isVisible(),
              QStringLiteral("escape window route: the frame editor is in the scene, on a "
                             "visible row"));
        if (edit) {
            g.win->requestActivate();
            spin(100);
            check(QGuiApplication::focusWindow() == g.win,
                  QStringLiteral("escape window route: the dialog window is the focus window"));
            edit->setFocus(true);
            spin(100);
            check(edit->hasActiveFocus(),
                  QStringLiteral("escape window route: the editor holds focus"));
            // An uncommitted edit: the text says 17, the document still says 0.
            edit->setProperty("text", QStringLiteral("17"));
            bool cancelRequested = false;
            QObject::connect(g.model, &gui::ExportViewModel::cancelRequested,
                             [&cancelRequested]() { cancelRequested = true; });
            QTest::keyPress(g.win, Qt::Key_Escape);
            spin(150);
            check(!cancelRequested,
                  QStringLiteral("escape window route: Escape in an editor ends the edit, "
                                 "the dialog stays open"));
            check(!edit->hasActiveFocus(),
                  QStringLiteral("escape window route: the edit ended (the editor lost focus)"));
            check(g.model->ranges()->ranges().first().first == 0,
                  QStringLiteral("escape window route: the discarded edit did not reach the "
                                 "document (first frame=%1)")
                      .arg(g.model->ranges()->ranges().first().first));
            // A2's other half: with no editor focused, Escape still cancels.
            cancelRequested = false;
            QTest::keyClick(g.win, Qt::Key_Escape);
            spin(150);
            check(cancelRequested,
                  QStringLiteral("escape window route: Escape with no editor focused still "
                                 "cancels the dialog"));
        }
        dispose(&g);
    }


    // =====================================================================
    // H41 (contract item 41, [R12]): "the frame range lives under the
    // Custom/Advanced section; the default surface stays uncluttered for the
    // common case." The Frames row used to be the FIRST row of the OUTPUT grid,
    // gated by `rangeVisible` - true for video, animated image and image
    // sequence, i.e. the app's primary use - so the default surface carried it
    // for exactly the common case the line names. Two observations, both read
    // off the real scene, neither of which an old placement could fake:
    //
    //   PLACEMENT - the row is a descendant of the Advanced column and not of
    //   the OUTPUT grid, and with the section SHUT it is effectively invisible
    //   (an item under an invisible parent is not visible). It is also the
    //   topmost row of the section, so the reading order it always had - which
    //   frames, then how big, then how fast - is the one inside the section too.
    //
    //   D25 - moving the row INTO the section leaves the worst case (Advanced
    //   open, four ranges) exactly as it was, because the rows were already
    //   inside the open section's height, and makes the SHUT surface shorter by
    //   them: opening the section now grows the content by at least the frames
    //   block's own height. Under the old placement the growth was the section's
    //   OTHER residents alone.
    //
    // The Advanced column is identified by its residents (loop and audio), and
    // that identification is asserted rather than assumed.
    // =====================================================================
    {
        auto itemByText = [](QQuickItem* root, const QString& text) -> QQuickItem* {
            if (!root) return nullptr;
            for (QQuickItem* c : root->findChildren<QQuickItem*>())
                if (c->property("text").toString() == text) return c;
            return nullptr;
        };
        auto isInside = [](QQuickItem* it, QQuickItem* anc) {
            for (QQuickItem* p = it; p; p = p->parentItem())
                if (p == anc) return true;
            return false;
        };

        Form f;
        // Four ranges, Advanced SHUT: the state the review measured the violation
        // in, on a video destination.
        makeForm(560, 640, false, 4, &f);
        QQuickItem* framesLabel = itemByText(f.root, QStringLiteral("Frames"));
        QQuickItem* resolutionLabel = itemByText(f.root, QStringLiteral("Resolution"));
        QQuickItem* loopBox = itemByText(f.root, f.model->loop()->label());
        QQuickItem* audioBox = itemByText(f.root, f.model->audio()->label());
        check(framesLabel && resolutionLabel && loopBox && audioBox,
              QStringLiteral("H41 precondition: the Frames, Resolution, loop and audio rows are all "
                             "in the scene (frames %1, resolution %2, loop %3, audio %4)")
                  .arg(framesLabel ? QStringLiteral("there") : QStringLiteral("absent"),
                       resolutionLabel ? QStringLiteral("there") : QStringLiteral("absent"),
                       loopBox ? QStringLiteral("there") : QStringLiteral("absent"),
                       audioBox ? QStringLiteral("there") : QStringLiteral("absent")));
        QQuickItem* advCol = loopBox ? loopBox->parentItem() : nullptr;
        check(advCol && advCol == audioBox->parentItem(),
              "H41 precondition: the loop and audio rows really share one container (the Advanced "
              "column), so that container can be addressed");

        QQuickItem* outputGrid = resolutionLabel ? resolutionLabel->parentItem() : nullptr;
        QQuickItem* framesGrid = framesLabel ? framesLabel->parentItem() : nullptr;
        if (framesLabel && framesGrid && advCol && outputGrid) {
            check(isInside(framesGrid, advCol),
                  "H41: the Frames row is INSIDE the Advanced column - item 41's \"the frame range "
                  "lives under the Custom/Advanced section\"");
            check(!isInside(framesLabel, outputGrid),
                  "H41: and it is no longer a row of the OUTPUT grid that opened the default "
                  "surface");
            check(!framesLabel->isVisible(),
                  "H41: with Advanced SHUT the Frames row is not drawn - an item under an invisible "
                  "parent is not visible, so the default surface is uncluttered for the common case");
            const qreal shutHeight = f.contentH();
            check(shutHeight > 0, "H41 precondition: the shut form has real content to compare");

            f.model->setAdvancedOpen(true);
            spin(150);
            check(framesLabel->isVisible(), "H41: opening Advanced draws the row again");
            const qreal openHeight = f.contentH();
            QTextStream(stdout)
                << QStringLiteral("[measured] H41 560x640, four ranges, Advanced shut -> open: "
                                  "contentHeight %1 -> %2, frames block %3")
                       .arg(shutHeight).arg(openHeight).arg(framesGrid->height())
                << "\n";
            check(openHeight - shutHeight >= framesGrid->height() - 1.0,
                  QStringLiteral("H41/D25: opening Advanced now adds the frame ranges themselves "
                                 "(%1 -> %2 content px, frames block %3 px) - the default surface "
                                 "carried them before the move")
                      .arg(shutHeight).arg(openHeight).arg(framesGrid->height()));

            // The FIRST row of the section: its top is the section's top, and every
            // other visible row of the column starts below its bottom.
            qreal nextTop = 1e9;
            int visibleRows = 0;
            for (QQuickItem* c : advCol->childItems()) {
                if (!c->isVisible()) continue;
                ++visibleRows;
                if (c == framesGrid) continue;
                nextTop = qMin(nextTop, c->y());
            }
            check(framesGrid->height() > 100 && visibleRows >= 2
                      && nextTop >= framesGrid->y() + framesGrid->height(),
                  QStringLiteral("H41: the frames block is the FIRST row of the Advanced section, "
                                 "with the section's other rows below it (block %1 px at y=%2, the "
                                 "next row at y=%3, %4 visible rows)")
                      .arg(framesGrid->height()).arg(framesGrid->y()).arg(nextTop).arg(visibleRows));

            // And the manual-mode half of the gating: a Still destination never
            // offers the range, wherever it now lives.
            f.model->setDestinationId(QStringLiteral("still.pixiv.illustration"));
            spin(150);
            check(!f.model->rangeVisible() && !framesLabel->isVisible(),
                  "H41: a Still artifact still has no Frames row (rangeVisible false), so only the "
                  "placement changed");
        }
        dispose(&f);
    }

    // =====================================================================
    // H41's other half: a row that lives INSIDE a section needs that section
    // OFFERED. `post.pixiv.ugoira` is an image SEQUENCE - `rangeVisible` is true,
    // because a sequence does take a range - and it has NO other resident of the
    // Advanced section: audio is forbidden, a directory does not loop, there is no
    // palette, one PNG codec offers no codec row, and neither the quality knob nor
    // the bitrate applies to an image artifact. So `mAdvancedVisible` came out
    // false for it, the disclosure was not offered at all, and the frame-range
    // rows item 41 had just moved in there were unreachable - measured:
    // `scripts/gui/probe_still.sh` on this row failed "a frame sequence keeps the
    // frame range editable" (and the `ae_activate "Advanced"` that follows) while
    // the same probe's set-by-name of the range passed.
    //
    // The fix keeps the section alive with `rangeVisible` as a term, exactly the way
    // the offered codec row already does - and it is NOT a weakening of the "an empty
    // Advanced section is a dead end" rule, which the Still case at the end of this
    // block re-pins: a Still artifact has no range either, so with nothing else
    // inside the section still hides.
    // =====================================================================
    {
        Form f;
        makeForm(560, 640, false, 1, &f, QStringLiteral("post.pixiv.ugoira"));
        spin(150);
        check(f.model->rangeVisible(),
              "H41 precondition: an image sequence does offer its frame range "
              "(rangeVisible true)");

        check(f.model->advancedVisible(),
              "H41: an image sequence OFFERS the Advanced section - its frame-range rows are "
              "the residents it has, and a resident the section does not show is a control "
              "the dialog hides from the user");

        QQuickItem* toggle = nullptr;
        for (QQuickItem* c : f.root->findChildren<QQuickItem*>())
            if (QByteArray(c->metaObject()->className()).contains("AeDisclosure")
                && c->property("text").toString() == QStringLiteral("Advanced")) {
                toggle = c;
                break;
            }
        check(toggle && toggle->isVisible(),
              "H41: and the disclosure itself is drawn, so the section can actually be opened");

        QQuickItem* framesLabel = nullptr;
        for (QQuickItem* c : f.root->findChildren<QQuickItem*>())
            if (c->property("text").toString() == QStringLiteral("Frames")) {
                framesLabel = c;
                break;
            }
        check(framesLabel != nullptr && !framesLabel->isVisible(),
              "H41 precondition: the Frames row is in the scene, shut with the section");
        QQuickItem* framesGrid = framesLabel ? framesLabel->parentItem() : nullptr;
        check(framesGrid != nullptr, "H41 precondition: the Frames label sits in the section's own grid");
        if (toggle && framesLabel && framesGrid) {
            f.model->setAdvancedOpen(true);
            spin(150);
            check(toggle->property("open").toBool(),
                  "H41 precondition: the disclosure follows the model open");
            check(framesLabel->isVisible(),
                  "H41: opening the section draws the frame-range rows for an image sequence - "
                  "the destination's ranges are reachable");
            // The two fields of the row are the ones the user would edit, and they are
            // the reachable end of the same claim. The ROW is a Repeater DELEGATE, so it
            // is found through the VISUAL tree - findChildren cannot reach one (the rule
            // visualTree states, and the F36 block below lives by) - and its fields are
            // then the row's own children, as they are for any QQuickItem.
            QQuickItem* rangeRow = nullptr;
            for (QQuickItem* c : visualTree(f.root)) {
                if (!QByteArray(c->metaObject()->className()).contains("AeFrameRangeRow"))
                    continue;
                for (QQuickItem* p = c; p; p = p->parentItem())
                    if (p == framesGrid) {
                        rangeRow = c;
                        break;
                    }
                if (rangeRow)
                    break;
            }
            int rangeFields = 0;
            if (rangeRow)
                for (QQuickItem* c : rangeRow->findChildren<QQuickItem*>())
                    if (QByteArray(c->metaObject()->className()).contains("AeNumberField"))
                        ++rangeFields;
            check(rangeRow != nullptr && rangeFields == 2,
                  QStringLiteral("H41: and the row carries its two frame-range editors (%1 fields, "
                                 "the row %2)")
                      .arg(rangeFields)
                      .arg(rangeRow ? QStringLiteral("there") : QStringLiteral("absent")));
        }

        // The dead-end rule stands: a Still artifact offers no range, so with nothing
        // else inside the section stays hidden. This is the guard against "offer the
        // section for everything".
        f.model->setDestinationId(QStringLiteral("still.instagram.feed"));
        spin(150);
        check(!f.model->rangeVisible() && !f.model->advancedVisible(),
              "H41: a Still artifact still offers no Advanced section (no range, nothing else "
              "inside) - the section is only offered when something inside it applies");
        dispose(&f);
    }

    // =====================================================================
    // F36: "the frame-range fields clamp against each other so a range can never
    // invert." The RULE lives in the model (ExportRangeModel::setRangeFirst/
    // setRangeLast refuse a value that passes the partner); the ROW owns only
    // what it offers. Read off the real row's two fields:
    //
    //   * First stops at Last and Last starts at First - through the field's own
    //     range, which is also the range an accessibility client reads
    //     (aeAccessibleValue's minimum/maximum), so a probe sees the same bound;
    //   * an edit that would invert is therefore not OFFERED: typed into a field
    //     it commits the clamped value, so the pair can never end up first > last;
    //   * and the bounds follow the model when EITHER end changes, so the two stay
    //     consistent after an edit to the other one.
    // =====================================================================
    {
        Form f;
        makeForm(560, 640, true, 1, &f);
        QQuickItem* rangeRow = nullptr;
        for (QQuickItem* c : visualTree(f.root))
            if (QByteArray(c->metaObject()->className()).contains("AeFrameRangeRow")) {
                rangeRow = c;
                break;
            }
        check(rangeRow != nullptr, "F36 precondition: a frame range row is in the scene");
        QList<QQuickItem*> numberFields;
        if (rangeRow)
            for (QQuickItem* c : rangeRow->findChildren<QQuickItem*>())
                if (QByteArray(c->metaObject()->className()).contains("AeNumberField"))
                    numberFields << c;
        check(numberFields.size() == 2,
              QStringLiteral("F36 precondition: the row's two number fields are in the scene (%1 "
                             "found)").arg(numberFields.size()));
        // Order them by geometry rather than by tree order, so a future edit that
        // swaps the declarations cannot silently measure the wrong field.
        QQuickItem* firstField = numberFields.size() == 2
            ? (numberFields.at(0)->x() <= numberFields.at(1)->x() ? numberFields.at(0) : numberFields.at(1))
            : nullptr;
        QQuickItem* lastField = numberFields.size() == 2
            ? (numberFields.at(0)->x() <= numberFields.at(1)->x() ? numberFields.at(1) : numberFields.at(0))
            : nullptr;
        const ctrl::FrameRange open = f.model->ranges()->ranges().at(0);
        check(open.first == 0 && open.last == 5,
              QStringLiteral("F36 precondition: the form opens on the range %1..%2 it was seeded "
                             "with").arg(open.first).arg(open.last));
        if (firstField && lastField) {
            check(firstField->property("to").toInt() == open.last,
                  QStringLiteral("F36: the First field stops at Last (%1), so the edit that would "
                                 "invert is not offered")
                      .arg(firstField->property("to").toInt()));
            check(lastField->property("from").toInt() == open.first,
                  QStringLiteral("F36: and the Last field starts at First (%1)")
                      .arg(lastField->property("from").toInt()));
            QQuickItem* firstNode = firstField->findChild<QQuickItem*>(QStringLiteral("aeAccessibleValue"));
            QQuickItem* lastNode = lastField->findChild<QQuickItem*>(QStringLiteral("aeAccessibleValue"));
            check(firstNode && firstNode->property("accessibleMaximum").toInt() == open.last,
                  "F36: the bound an accessibility client reads is the clamped one (First's maximum "
                  "is Last)");
            check(lastNode && lastNode->property("accessibleMinimum").toInt() == open.first,
                  "F36: and Last's minimum is First, both directions of the same clamp");

            // ---- an edit upward: the pair stays consistent ------------------
            QQuickItem* firstEdit = firstField->findChild<QQuickItem*>(QStringLiteral("scrubEdit"));
            check(firstEdit != nullptr && rangeRow->isVisible(),
                  "F36 precondition: the First field's editor is there to take the edit");
            if (firstEdit) {
                f.win->requestActivate();
                spin(80);
                firstEdit->setFocus(true);
                spin(40);
                check(firstEdit->hasActiveFocus(), "F36 precondition: and it took focus");
                firstEdit->setProperty("text", QStringLiteral("3"));
                firstEdit->setFocus(false); // focus-out is one of the two commit routes
                spin(60);
                const ctrl::FrameRange moved = f.model->ranges()->ranges().at(0);
                check(moved.first == 3 && moved.last == 5,
                      QStringLiteral("F36: First moves up inside the range (%1..%2)")
                          .arg(moved.first).arg(moved.last));
                check(lastField->property("from").toInt() == 3,
                      "F36: and Last's own floor followed it - the pair stays consistent when "
                      "either end changes");
                check(lastNode && lastNode->property("accessibleMinimum").toInt() == 3,
                      "F36: the floor an accessibility client reads followed it too (Last's "
                      "minimum is First again)");

                // ---- an edit that would invert ------------------------------
                QQuickItem* lastEdit = lastField->findChild<QQuickItem*>(QStringLiteral("scrubEdit"));
                check(lastEdit != nullptr && rangeRow->isVisible(),
                      "F36 precondition: the Last field's editor is there to take the edit");
                if (lastEdit) {
                    lastEdit->setFocus(true);
                    spin(40);
                    lastEdit->setProperty("text", QStringLiteral("-3"));
                    lastEdit->setFocus(false);
                    spin(60);
                    const ctrl::FrameRange after = f.model->ranges()->ranges().at(0);
                    check(after.first == 3 && after.last == 3,
                          QStringLiteral("F36: the edit that would invert is not offered - typed -3 "
                                         "on %1..%2, the field offers [first..last] and commits the "
                                         "clamped value (%3..%4), never an inverted range")
                              .arg(moved.first).arg(moved.last).arg(after.first).arg(after.last));
                    check(!(after.first > after.last),
                          "F36: the range on the form is never inverted, whatever was typed");
                }
            }
        }
        dispose(&f);
    }

    // =====================================================================
    // A3 (item 3) / B11 (item 11): a click outside a focused text field removes
    // its focus, and the field's text and the list's selection are unchanged by
    // that click. The shell's background MouseArea is the only mechanism this
    // scene has for it, and until 2026-10-09 it blurred ONLY the scrub editors:
    // the bitrate field buried in Advanced kept the caret after a background
    // click. Both kinds of editor are checked on one form, through the window's
    // REAL active focus item, which is the thing a click actually changes.
    // =====================================================================
    {
        Form f;
        // archive.master at Lossless resolves the archive ladder to FFV1, which
        // has no quality knob in the table, so the Bitrate row is the one this
        // destination offers - and it is the row that kept focus before.
        makeForm(560, 760, true, 1, &f, QStringLiteral("archive.master"), QSize(1000, 400));
        f.model->setQualityIndex(3); // Lossless
        spin(150);
        // The model's own rule: applyPlan() ends with setAdvancedOpen(false) - the
        // plan reports and the section shuts, so a quality change that re-plans
        // closes it again. Re-open it, the way the form's own disclosure does, or
        // both rows below sit behind a shut section and the check is vacuous.
        f.model->setAdvancedOpen(true);
        spin(150);
        check(f.model->advancedOpen() && f.model->bitrateVisible(),
              "A3 precondition: the Bitrate row is on screen (Advanced open, this destination "
              "offers it)");
        QQuickItem* bitrate = f.byName("bitrateEdit");
        QQuickItem* rangeRow = nullptr;
        for (QQuickItem* c : visualTree(f.root))
            if (QByteArray(c->metaObject()->className()).contains("AeFrameRangeRow")) {
                rangeRow = c;
                break;
            }
        QQuickItem* frameEdit =
            rangeRow ? rangeRow->findChild<QQuickItem*>(QStringLiteral("scrubEdit")) : nullptr;
        check(bitrate != nullptr && bitrate->isVisible(),
              QStringLiteral("A3 precondition: the Bitrate field is in the scene and visible (the "
                             "row that was never blurred; found=%1 visible=%2 advancedOpen=%3 "
                             "bitrateVisible=%4)")
                  .arg(bitrate ? QStringLiteral("yes") : QStringLiteral("no"),
                       bitrate && bitrate->isVisible() ? QStringLiteral("yes") : QStringLiteral("no"))
                  .arg(f.model->advancedOpen())
                  .arg(f.model->bitrateVisible()));
        check(frameEdit != nullptr && rangeRow->isVisible(),
              "A3 precondition: a frame field's editor is in the scene, on a visible row (the case "
              "the handler already covered)");
        // A press in the window's own margin: the scroller starts at 16, so this
        // lands on the shell's background MouseArea and on nothing else.
        const QPointF outside(4, 4);
        auto clickOutside = [&]() {
            QMouseEvent press(QEvent::MouseButtonPress, outside, outside,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(f.win, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, outside, outside,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(f.win, &release);
            spin(40);
        };
        const QString held = f.model->bitrate();
        f.win->requestActivate();
        spin(80);
        if (bitrate && bitrate->isVisible()) {
            bitrate->forceActiveFocus();
            spin(40);
            check(f.win->activeFocusItem() == bitrate,
                  "A3 precondition: the Bitrate field holds the window's focus");
            // A value typed but not yet reported: a TextField has no commit on
            // focus-out (its edits arrive on textEdited), so the only thing that
            // can change what the field holds is the click itself.
            bitrate->setProperty("text", QStringLiteral("1500k"));
            clickOutside();
            check(f.win->activeFocusItem() != bitrate && !bitrate->hasActiveFocus(),
                  "A3: a click on the dialog's empty space removes the Bitrate field's focus");
            check(bitrate->property("text").toString() == QLatin1String("1500k"),
                  "B11: the field's own text is unchanged by that click");
            check(f.model->bitrate() == held,
                  "B11: and neither is the value behind it - the click removes focus and "
                  "nothing else");
        }
        if (frameEdit && frameEdit->isVisible()) {
            frameEdit->setFocus(true);
            spin(40);
            check(f.win->activeFocusItem() == frameEdit,
                  "A3 precondition: the frame editor holds the window's focus");
            // The editor holds its own value, so the commit a focus-out makes is a
            // no-op: what is asserted below is that the CLICK itself changed
            // nothing, which is the other half of item 11.
            const int firstBefore = f.model->ranges()->ranges().at(0).first;
            frameEdit->setProperty("text", QString::number(firstBefore));
            clickOutside();
            check(f.win->activeFocusItem() != frameEdit && !frameEdit->hasActiveFocus(),
                  "A3: the same click removes a scrub editor's focus");
            check(f.model->ranges()->ranges().at(0).first == firstBefore,
                  "B11: and the field's value is unchanged by it");
        }
        dispose(&f);
    }

    // Every form built above is the same scene; a binding that threw did so on
    // every one of them and still left a form that looks almost right.
    {
        QString throwInfo = QStringLiteral("%1 TypeError%2")
                                .arg(qmlThrows)
                                .arg(qmlThrows == 1 ? QString() : QStringLiteral("s"));
        if (!qmlThrowFirst.isEmpty()) throwInfo += QStringLiteral("; first: %1").arg(qmlThrowFirst);
        check(qmlThrows == 0,
              QStringLiteral("K46: no binding in the dialog threw while these screens were "
                             "built (%1)").arg(throwInfo));
    }

    QTextStream(stdout) << (failures ? "FAILURES: " : "ALL PASS ") << failures << "\n";
    return failures ? 1 : 0;
}
