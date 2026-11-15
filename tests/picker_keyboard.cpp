// The destination picker's keyboard contract, offscreen and windowless.
//
// This is the coverage hole the review named: the picker's search field could not
// keep what it was given. B7/B8/B9 say which key belongs to whom while the field has
// focus, and nothing tested it, because the GUI probes cannot: driving a keystroke
// needs a window manager, and the whole point of these lines is the ROUTING of a key
// between the field and the list, which is a question about a scene.
//
// So the scene is built and the keys are injected in-process, which is what Qt's own
// autotests do. Measured before relying on it (and worth knowing): a key sent
// straight to a Qt Quick item is delivered to the item's Keys filter with NO window
// and NO active focus - the filter consumes it, and everything it does not consume
// still reaches the editor (a character key really does grow the text and move the
// caret). That is why this needs neither a display nor a GL context, and why it can
// tell "the filter ate it" apart from "the editor ate it": Left would otherwise
// move the caret.
//
// The observable is the MODEL, not the widgets: what a person is affected by is the
// query, the highlighted row and which rows are on screen, and those are the same
// objects the scene binds to. The scene is the real DestinationPicker.qml, so the
// wiring between the field and the model is under test and not a reimplementation
// of it.
//
// Deliberately NOT here, because it needs a visible window and a screen grab:
//   B12 the pointer is an I-beam over the text area (a cursor shape is a property of
//       a real window, and X11 screen grabs do not contain the pointer either - this
//       needs `ffmpeg -f x11grab -draw_mouse 1` against a visible window).
// F31's I-beam is the same story. A click outside a focused field once sat in this
// note too ("no focus without a window"); the blocks below now host the scene in a
// real offscreen window (the B10 route and the same measurement that let the
// window-route Escape pass), so B11 is covered below rather than deferred.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/picker_keyboard
#include <QApplication>
#include <QElapsedTimer>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QWheelEvent>
#include <QColor>

#include "ctrl/ExportTarget.h"
#include "gui/qml/DestinationPickerModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

using Model = gui::DestinationPickerModel;

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Send one key straight to the item. Returns whether it was accepted, which is a
// useful extra fact but never the assertion by itself: "accepted" cannot tell the
// filter apart from the editor, and that distinction is the whole contract.
// Takes QObject* so the SAME helper can also target a window: measured, the two
// routes differ (scratch: escape-spike/) - a direct send reaches the item's own
// Keys filters and editor and does NOT bubble to ancestors, while a window send is
// delivered to the activeFocusItem and DOES bubble up the item hierarchy. B10's
// Escape is a routing question as much as a policy one, so it is driven the honest
// way, through the window.
static bool sendKey(QObject* aItem, int aKey, const QString& aText = QString()) {
    QKeyEvent event(QEvent::KeyPress, aKey, Qt::NoModifier, aText);
    QCoreApplication::sendEvent(aItem, &event);
    return event.isAccepted();
}

// Which rows are on screen, in order. A group that closes loses its children from
// this list, so comparing two of these is how "an arrow closed a group" is detected
// without trusting any single flag.
static QStringList onScreenRows(const Model& aModel) {
    QStringList out;
    for (int row = 0; row < aModel.rowCount(); ++row) {
        if (aModel.isOnScreen(row)) {
            out << QString::number(row) + QLatin1Char(':') + aModel.labelOf(row);
        }
    }
    return out;
}

// Every open group, so "Left did not expand or collapse ANY group" can be asserted
// even when the current row is not inside one.
static QStringList openSections(const Model& aModel) {
    QStringList out;
    for (int row = 0; row < aModel.rowCount(); ++row) {
        if (aModel.isSection(row) && aModel.isExpanded(row)) out << QString::number(row);
    }
    return out;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // The application's style, so the scene builds the same controls it ships.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QList<ctrl::ExportTarget> targets =
        ctrl::ExportTargets::load(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    const QList<ctrl::ExportGroup> groups =
        ctrl::ExportTargets::loadGroups(QStringLiteral(AE_EXPORT_TARGETS_PATH));
    check(targets.size() > 40, QStringLiteral("the destination table loads (%1 rows)").arg(targets.size()));
    if (targets.isEmpty()) return 1;

    // The remembered destination is what the application passes in (QSettings
    // "export_destination"), and it is also what makes the precondition of the group
    // assertions hold: its platform is opened so the preselection is on screen.
    Model model(targets, groups, QStringLiteral("telegram.sticker"));
    gui::QmlTheme theme;

    // The scene's glyphs are read from a scratch dir the application tints on
    // startup (theme::tintIcons -> setIconDir). Doing the same here keeps the log
    // readable: without it every Image in the scene reports "Cannot open:
    // file:///caret-right.svg", and a run full of warnings is a run nobody reads.
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    // What "the picker answered" means to the shell.
    int acceptedCount = 0;
    QString chosen;
    QObject::connect(&model, &Model::accepted, [&model, &acceptedCount, &chosen]() {
        ++acceptedCount;
        chosen = model.chosenId();
    });
    // What "the picker CLOSED" means to the shell (DestinationPicker.cpp wires this
    // to QDialog::reject): the observable behind B10's second Escape.
    int rejectedCount = 0;
    QObject::connect(&model, &Model::rejected, [&rejectedCount]() { ++rejectedCount; });

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("picker"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

    QQmlComponent component(&engine,
                            QUrl::fromLocalFile(QStringLiteral(AE_QML_DIR "/DestinationPicker.qml")));
    if (component.isError()) {
        QTextStream(stderr) << "the picker's QML does not load:\n";
        for (const QQmlError& error : component.errors()) {
            QTextStream(stderr) << "  " << error.toString() << "\n";
        }
        return 2;
    }
    QObject* created = component.create();
    auto* root = qobject_cast<QQuickItem*>(created);
    if (!root) {
        QTextStream(stderr) << "the picker's root object is not an item\n";
        return 2;
    }

    // Host the scene in a real (offscreen) window. The blocks above send keys
    // straight at an item and measure SAME-ITEM routing; B10 sends at the window,
    // because "does Escape typed in the field reach the picker's policy?" is a
    // delivery-chain question (measured: direct send never bubbles to ancestors,
    // window delivery does - scratch: escape-spike/). The window costs the
    // direct-send assertions nothing: their route is unchanged.
    QQuickWindow window;
    window.resize(640, 480);
    window.show();
    root->setParentItem(window.contentItem());

    // The search field, addressed the way the probes address it: by the accessible
    // name the model gives it. A field that is merely "the first TextField" would
    // keep passing if a second one were added above it.
    QQuickItem* search = nullptr;
    const QList<QQuickItem*> items = root->findChildren<QQuickItem*>();
    for (QQuickItem* item : items) {
        if (item->property("accessibleName").toString() == model.searchAccessibleName()) {
            search = item;
            break;
        }
    }
    check(search != nullptr, "the search field is in the scene, by its accessible name");
    if (!search) return 1;

    // B15 (decided): focusing the field selects nothing, so typing refines the query
    // instead of replacing it. The focus itself needs a window; what can be asserted
    // here is that nothing selects the text on its own - which is what would break
    // this if someone added a selectAll() on focus.
    check(search->property("selectedText").toString().isEmpty(),
          "B15: the field holds no selection of its own, so focusing it cannot steal the query");

    // B13: the two selection colours are the design's tokens, not the style's
    // palette. Asserted through the real QmlTheme, so a token rename fails here.
    check(search->property("selectionColor").value<QColor>() == theme.selection(),
          "B13: the selection highlight is theme.selection");
    check(search->property("selectedTextColor").value<QColor>() == theme.selectionText(),
          "B13: the selected text is theme.selectionText");

    // B6: typing reaches the editor AND filters the list. This is the guard against
    // the cheap way to pass B7 - a field that swallows every key would move no caret
    // and alter no text either.
    {
        sendKey(search, Qt::Key_X, QStringLiteral("x"));
        check(search->property("text").toString() == QLatin1String("x"),
              "B6: a typed character reaches the editor");
        check(search->property("cursorPosition").toInt() == 1,
              "B6: and the caret follows it");
        check(model.query() == QLatin1String("x"),
              "B6: and the list is filtered by what was typed");
        // Back to no query, which is the state the group preconditions describe.
        search->setProperty("text", QString());
        check(model.query().isEmpty(), "clearing the field clears the query too");
    }

    // The preconditions the group assertions below need, IN THE TEST rather than
    // assumed: a run that cannot observe a collapse must not report that none
    // happened. Measured, not hoped for.
    check(model.visibleRowCount() >= 3,
          QStringLiteral("precondition: several destinations are on screen (%1)")
              .arg(model.visibleRowCount()));
    check(!openSections(model).isEmpty(),
          "precondition: at least one group is open, so an arrow COULD close it");
    check(!model.idOf(model.currentRow()).isEmpty(),
          "precondition: the remembered destination is the highlighted row");

    // B7: Up/Down move the LIST's highlight while the field keeps everything else.
    {
        const int rowBefore = model.currentRow();
        const QString textBefore = search->property("text").toString();
        const int caretBefore = search->property("cursorPosition").toInt();
        // NOT visibleRowCount(): that counts DESTINATIONS on screen, while a row
        // index also counts the sections between them. The precondition needs an
        // on-screen destination below the current row.
        int below = 0;
        for (int row = rowBefore + 1; row < model.rowCount(); ++row) {
            if (!model.isSection(row) && model.isOnScreen(row)) ++below;
        }
        check(below >= 1, "precondition: an on-screen destination sits below the highlighted row");

        sendKey(search, Qt::Key_Down);
        check(model.currentRow() == rowBefore + 1,
              "B7: Down moves the list's highlight one row");
        check(search->property("text").toString() == textBefore,
              "B7: Down does not alter the query");
        check(search->property("cursorPosition").toInt() == caretBefore,
              "B7: Down does not move the caret");

        sendKey(search, Qt::Key_Up);
        check(model.currentRow() == rowBefore,
              "B7: Up moves it back");
    }

    // B9/B14: Left and Right belong to the CARET. The field must not forward them, so
    // no group may open or close - which is the reported defect, and the reason this
    // assertion tests the groups and not only the caret.
    {
        // A query with a caret in it, so "the caret moved" is observable at all.
        search->setProperty("text", QStringLiteral("telegram"));
        check(model.query() == QLatin1String("telegram"),
              "precondition: the typed query reached the model");
        const int caret = search->property("cursorPosition").toInt();
        check(caret > 0, "precondition: the caret has somewhere to move back to");

        const QStringList rowsBefore = onScreenRows(model);
        const QStringList openBefore = openSections(model);
        check(!openBefore.isEmpty(),
              "precondition: a group is open, so forwarding Left WOULD have collapsed it");

        sendKey(search, Qt::Key_Left);
        check(search->property("cursorPosition").toInt() == caret - 1,
              "B9: Left moves the caret");
        check(openSections(model) == openBefore,
              "B9: Left does not open or close any group");
        check(onScreenRows(model) == rowsBefore,
              "B9: Left does not change what is on screen");
        check(model.query() == QLatin1String("telegram"),
              "B9: Left does not alter the query");

        sendKey(search, Qt::Key_Right);
        check(search->property("cursorPosition").toInt() == caret,
              "B9: Right moves the caret back");
        check(openSections(model) == openBefore,
              "B9: Right does not open or close any group");
        check(onScreenRows(model) == rowsBefore,
              "B9: Right does not change what is on screen");
    }

    // B8: Enter activates the highlighted row, exactly as clicking it would. The
    // query is an id, so "the highlighted row" is not a guess about ordering.
    {
        search->setProperty("text", QStringLiteral("telegram.sticker"));
        check(model.idOf(model.currentRow()) == QLatin1String("telegram.sticker"),
              "precondition: the highlighted row is the destination that was searched for");

        check(acceptedCount == 0, "precondition: nothing has been accepted yet");
        sendKey(search, Qt::Key_Return);
        check(acceptedCount == 1, "B8: Return answers with the highlighted row");
        check(chosen == QLatin1String("telegram.sticker"),
              QStringLiteral("B8: and the answer is that row (%1)").arg(chosen));
    }

    // B10 (decided 2026-09-30): Escape empties the search field FIRST and closes
    // the picker only when the field is already empty - a half-typed query is never
    // a reason to throw the dialog away. Driven through the WINDOW (see sendKey):
    // the field and the list must both CARRY Escape to the picker's policy, so a
    // test that poked the policy function directly would prove neither half.
    {
        // --- with text: the FIRST Escape clears, and must not close. ---
        const QStringList rowsAll = onScreenRows(model);
        search->setProperty("text", QStringLiteral("telegram"));
        check(model.query() == QLatin1String("telegram"),
              "precondition: the typed query reached the model");
        check(onScreenRows(model) != rowsAll,
              "precondition: the query actually filtered the list, so clearing is observable");

        search->forceActiveFocus();
        check(window.activeFocusItem() == search,
              "precondition: the field holds the window's focus, the state the picker opens in");

        sendKey(&window, Qt::Key_Escape);
        check(search->property("text").toString().isEmpty(),
              "B10: Escape empties the search field");
        check(model.query().isEmpty(),
              "B10: and the query goes with it");
        {
            // Every row the filter hid is back. NOT "the lists are equal": the
            // model never closes what it or a search opened (setQuery's own
            // contract), so a revealed group legitimately STAYS open and the
            // cleared list can be a SUPERSET of the captured one.
            const QStringList rowsBack = onScreenRows(model);
            QStringList missing;
            for (const QString& row : rowsAll)
                if (!rowsBack.contains(row)) missing << row;
            check(missing.isEmpty(),
                  "B10: and every row the filter had hidden comes back (missing: "
                      + (missing.isEmpty() ? QStringLiteral("none") : missing.join(QLatin1String(", "))) + ")");
        }
        check(rejectedCount == 0,
              "B10: the FIRST Escape does not close the picker");

        // --- the SECOND Escape, field now empty, closes. ---
        sendKey(&window, Qt::Key_Escape);
        check(rejectedCount == 1,
              "B10: the second Escape closes the picker (model.rejected -> the shell's QDialog::reject)");
        check(search->property("text").toString().isEmpty(),
              "B10: and there was nothing left to clear");

        // --- the same two-step from the LIST's focus: Escape belongs to the
        // picker whatever holds it, and neither a row nor the list's own key
        // handler may eat it. Where inside the list's subtree the window puts the
        // focus item is a Qt detail (measured: it varies between the view and a
        // delegate row), so the precondition is "somewhere in the list's subtree,
        // and not the field" - and the behaviour below is the real assertion. ---
        QQuickItem* list = root->findChild<QQuickItem*>(QStringLiteral("pickerList"));
        check(list != nullptr, "precondition: the list is addressable");
        if (list) {
            search->setProperty("text", QStringLiteral("telegram"));
            list->forceActiveFocus();
            QQuickItem* focused = window.activeFocusItem();
            bool inList = false;
            for (QQuickItem* p = focused; p; p = p->parentItem())
                if (p == list) { inList = true; break; }
            check(focused != nullptr && focused != search && inList,
                  "precondition: the window's focus is in the list's subtree, not the field");
            sendKey(&window, Qt::Key_Escape);
            check(search->property("text").toString().isEmpty(),
                  "B10: Escape clears the field even when the LIST has focus");
            check(rejectedCount == 1,
                  "B10: and still does not close while there was text");
            sendKey(&window, Qt::Key_Escape);
            check(rejectedCount == 2,
                  "B10: the second one, from the list, closes");
        }

        // Routing guard (a supplementary fact, never the assertion by itself): the
        // field must leave Escape alone. If a future edit makes it CONSUME the key,
        // the window routes above stop working - and this names the culprit.
        search->setProperty("text", QStringLiteral("x"));
        check(!sendKey(search, Qt::Key_Escape),
              "B10 routing: the search field itself does not consume Escape");
    }

    // ---- E29: the row tooltip is only for a TRUNCATED label, and carries it in
    // full (decided 2026-09-30). Hover is REAL window-delivered hover (geom's
    // measured pattern - pointer handlers see the window's delivery path, never
    // events posted at the item), and ONE row is checked at two widths so
    // neither side of the gate can pass vacuously.
    {
        auto spin = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
        };
        auto hoverAt = [&](QQuickItem* target, QPointF local) {
            QQuickItem* content = window.contentItem();
            const QPointF p = target->mapToItem(content, local);
            // A real pointer's hover arrives as a MOUSE MOVE (the delivery agent
            // derives item/handler hover state from it). A bare HoverMove reaches
            // only the topmost accepting item: measured, it left the delegate's
            // own `hovered` at 0 when the row's label sat on top. Send both, as
            // a real move would produce.
            QMouseEvent me(QEvent::MouseMove, p, p, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &me);
            QHoverEvent he(QEvent::HoverMove, p, p, p - QPointF(40, 0));
            QCoreApplication::sendEvent(&window, &he);
            QCoreApplication::processEvents();
        };
        auto hoverAway = [&]() {
            const QPointF p(-100, -100);
            QMouseEvent me(QEvent::MouseMove, p, p, Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &me);
            QHoverEvent he(QEvent::HoverMove, p, p, QPointF(0, 0));
            QCoreApplication::sendEvent(&window, &he);
            QCoreApplication::processEvents();
        };

        // The ROW tooltip is the only tooltip in the picker that is not inside an
        // icon button (the search field's Clear keeps a gated instance - E29 drops
        // its hover text, not the component). The manual button is GONE (decided
        // 2026-10-08), so the scan below proves its ABSENCE; manual mode survives in
        // the model, and is asserted just after.
        QObject* rowTip = nullptr;
        int rowTips = 0;
        int extraButtons = 0;
        const auto all = root->findChildren<QObject*>();
        for (QObject* o : all) {
            const QByteArray cls = o->metaObject()->className();
            if (cls.contains("AeToolTip")) {
                bool inButton = false;
                for (QObject* p = o->parent(); p; p = p->parent())
                    if (QByteArray(p->metaObject()->className()).contains("AeIconButton")) { inButton = true; break; }
                if (!inButton) { ++rowTips; rowTip = o; }
            }
            // The rule is now the OPPOSITE of E29's old one: the footer carries the
            // two conventional buttons and NOTHING else. The escape hatch's sentence
            // went with its button, so the check is "no button whose words are
            // neither the confirm nor the cancel label" - a third button cannot come
            // back unnoticed.
            if (cls.contains("AeButton")) {
                const QString text = o->property("text").toString();
                const QString confirm = model.okText();
                const QString cancel = model.cancelText();
                if (text != confirm && text != cancel) ++extraButtons;
            }
        }
        check(extraButtons == 0,
              QStringLiteral("E29: the footer carries the two conventional buttons and no third one "
                             "(%1 button(s) with other words; the escape hatch's row was removed "
                             "2026-10-08)").arg(extraButtons));
        check(rowTips == 1 && rowTip, "E29: exactly one ROW tooltip feeds from the list hover");

        // ...and the escape hatch did not go with the button: it lives in the MODEL
        // now, as the fallback activate() takes on a row that matched nothing. Driven on
        // the scene's own model, and non-vacuously - a real match first answers with its
        // id, so the empty id below is the fallback and not the model's untouched state.
        // The query is restored afterwards so the hover checks below and C18 see the
        // table they expect.
        {
            const QString previous = model.query();
            model.setQuery(QStringLiteral("generic.video"));
            model.activateCurrent();
            check(!model.chosenId().isEmpty(),
                  "E29: precondition: a query that matches answers with its id");
            const int before = acceptedCount;
            model.setQuery(QStringLiteral("zzqqjj"));
            model.activateCurrent();
            check(acceptedCount == before + 1 && model.chosenId().isEmpty(),
                  QStringLiteral("E29: with the button gone, the model's fallback is the only "
                                 "route into manual mode - a query that matches nothing and "
                                 "Enter accepts once with an EMPTY id (got '%1')")
                      .arg(model.chosenId()));
            model.setQuery(previous);
        }

        QQuickItem* list = root->findChild<QQuickItem*>(QStringLiteral("pickerList"));
        check(list != nullptr, "precondition: the list is addressable");

        // Narrow: some label elide. Take the first visible row that does.
        // itemAtIndex is the view's invokable (the item is found as QQuickItem),
        // so it is called reflectively - the QML call site does the same.
        // The picker sizes from its ROOT item (ColumnLayout anchors.fill), and
        // the window must match so the E28 clamp boundary IS the dialog edge.
        auto itemAt = [&](int row) -> QQuickItem* {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, row));
            return it;
        };
        auto sizeTo = [&](int w) {
            window.resize(w, 480);
            root->setWidth(w);
            root->setHeight(480);
            spin(250);
        };
        // Measured: the picker's list keeps its CONTENT's natural width - the
        // ColumnLayout's minimums mean window-driven narrowing clips labels at
        // the viewport instead of ELIDING them (rootW=180 measured with
        // listW=211 = the widest row's natural width, at every window width).
        // So the elide is exercised where the geometry allows: an imperative
        // write on the delegate's `width: list.width` binding, after which the
        // label re-anchors narrower and Text.elide/truncated engage for real.
        sizeTo(640);
        QQuickItem* rowItem = nullptr;
        QQuickItem* rowLabelText = nullptr;
        QString fullLabel;
        int rowIdx = -1;
        for (int row = 0; row < model.rowCount(); ++row) {
            QQuickItem* it = itemAt(row);
            // onScreen=false rows are the load-bearing 0-height ones; recycled
            // OFF-VIEWPORT rows take no pointer. (`hoverEnabled: false` on the
            // delegate is deliberate - hover is rowHover's job - so the
            // delegate's own `hovered` is always 0 and says nothing.)
            if (!it || it->height() < 5) continue;
            const QPointF centre = it->mapToItem(window.contentItem(),
                                                 QPointF(it->width() / 2, it->height() / 2));
            if (centre.x() < 0 || centre.x() > 640 || centre.y() < 0 || centre.y() > 480) continue;
            QQuickItem* lbl = it->findChild<QQuickItem*>(QStringLiteral("rowLabelText"));
            const QString label = model.labelOf(row);
            if (!lbl || label.isEmpty()) continue;
            // The LONGEST on-screen label: at 90px any real label elides (a short
            // one like "pixiv" fits even a 32px lane - measured), and the widest
            // makes the composed tooltip's three lines meaningful.
            if (rowItem && label.length() <= fullLabel.length()) continue;
            rowItem = it;
            rowLabelText = lbl;
            rowIdx = row;
            fullLabel = label;
        }
        check(rowItem != nullptr, "precondition: an on-screen row with a label exists");
        if (rowItem) {
            const QString tipRole =
                model.data(model.index(rowIdx, 0), gui::DestinationPickerModel::TooltipRole).toString();
            const QString expected =
                tipRole.isEmpty() ? fullLabel : fullLabel + QLatin1Char('\n') + tipRole;

            // --- the truncated side: the row narrowed until its label elides ---
            rowItem->setWidth(90); // the label lane = 90 - 62 - 10 = 18px
            spin(150);
            check(rowLabelText->property("truncated").toBool(),
                  "precondition: the narrowed row's label actually elides");
            hoverAt(rowItem, QPointF(30, 15));
            spin(250);
            check(!rowTip->property("revealed").toBool(),
                  "E26: the row tooltip still waits at 250ms");
            spin(600);
            check(rowTip->property("text").toString() == expected,
                  QString("E29: a truncated row's tooltip = the label IN FULL + the identification (\"%1\")")
                      .arg(rowTip->property("text").toString().replace('\n', '|')));
            check(rowTip->property("revealed").toBool(), "E29: and it revealed after the dwell");
            // E27/E28 on the real row rect: below the row, inside the window.
            QQuickItem* content = window.contentItem();
            const QPointF rp = rowItem->mapToItem(content, QPointF(0, 0));
            auto* tip = qobject_cast<QQuickItem*>(rowTip);
            const QPointF tp = tip->mapToItem(content, QPointF(0, 0));
            check(tp.y() >= rp.y() + rowItem->height(),
                  "E27: the row tooltip sits BELOW its row (flip applies only at the edge)");
            check(tp.x() >= 0 && tp.x() + tip->width() <= 640.5 && tp.y() + tip->height() <= 480.5,
                  "E28: and fully inside the window (never clipped)");

            // --- the untruncated side: the same row widened past its label ---
            hoverAway();
            spin(120);
            rowItem->setWidth(600); // the label lane = 528px, the label fits
            spin(150);
            check(!rowLabelText->property("truncated").toBool(),
                  "precondition: widened, the same row's label fits");
            hoverAt(rowItem, QPointF(30, 15));
            spin(900);
            check(rowTip->property("text").toString().isEmpty(),
                  "E29: an untruncated row gets NO tooltip (its label already states it)");
            hoverAway();
        }
    }

    // ---- C18 (contract line 97): "A destination with no children is not indented as
    // though it had a parent." This is a GEOMETRY assertion on the real delegate, and
    // it is deliberately not satisfied by a flag: what was wrong is where the row was
    // drawn. Before the fix the only horizontal discriminator was `isSection`, so a
    // childless top-level destination - which the model really does produce, for every
    // platform with a single destination and for a group whose rows are not variants of
    // one platform - took the CHILD lanes (badge x=38, label margin 62) while a section
    // took (24/48). Now both draw 0/24/48 and only a real child sits one lane in.
    //
    // The role is read by the NAME the scene binds ("level"), so this test compiles
    // and runs against the pre-fix tree too, where the honest failure is "no such role"
    // and the measured x is 62.
    {
        auto spin = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
        };
        auto near = [](double a, double b) { return a > b - 0.5 && a < b + 0.5; };

        // The preceding block leaves a live filter in the field ("x"), and a filter
        // is a different table: it drops most rows, including every childless
        // top-level one. C18 is about the unfiltered listing, so clear it through
        // the field (the same route B10 uses) and prove the table is whole again.
        search->setProperty("text", QString());
        spin(250);
        check(model.query().isEmpty(),
              "precondition (C18): the search filter is cleared, so the whole table is listed");

        const int levelRole = [&model] {
            const QHash<int, QByteArray> names = model.roleNames();
            for (auto it = names.cbegin(); it != names.cend(); ++it) {
                if (it.value() == "level") return it.key();
            }
            return -1;
        }();
        check(levelRole >= 0, "C18: the model exposes a 'level' role for the scene to bind");
        auto levelOf = [&](int row) {
            return levelRole < 0 ? -1 : model.data(model.index(row, 0), levelRole).toInt();
        };

        QQuickItem* list = root->findChild<QQuickItem*>(QStringLiteral("pickerList"));
        check(list != nullptr, "precondition (C18): the list is addressable");
        auto itemAt = [&](int row) -> QQuickItem* {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, row));
            return it;
        };
        // The first row of each class the MODEL offers. The scroll position at this
        // moment is not consulted: the picker opens scrolled to the current destination
        // (Telegram in this fixture) and the window is 640x480, so only a handful of
        // rows have delegates at all. Each row is brought into the viewport just before
        // it is measured - that is the only thing scrolling is used for here, since a
        // row that was never instantiated has no delegate. Where a row sits in the
        // viewport is not what C18 is about.
        int sectionRow = -1, flatRow = -1, childRow = -1;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (!model.isOnScreen(row) || model.idOf(row).isEmpty()) continue;  // skips collapsed rows
            if (model.isSection(row)) {
                if (sectionRow < 0) sectionRow = row;
                continue;
            }
            if (levelOf(row) == 0) {
                if (flatRow < 0) flatRow = row;    // childless TOP-LEVEL destination
            } else if (levelOf(row) == 1) {
                if (childRow < 0) childRow = row;  // destination under a section
            }
        }
        check(sectionRow >= 0, "precondition (C18): the model has an expanded section to measure");
        check(flatRow >= 0,
              "precondition (C18): the model has a childless top-level destination to measure");
        check(childRow >= 0, "precondition (C18): the model has a destination under a section");
        check(flatRow >= 0 && !model.isSection(flatRow) && model.sectionOf(flatRow) < 0
                  && model.childCount(flatRow) == 0,
              "precondition (C18): the row measured as top-level really is childless and sectionless");
        // Scrolling is a measurement aid, not the claim: it makes the delegate exist.
        auto reveal = [&](int row) {
            QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 0));
            spin(250);
        };

        if (sectionRow >= 0 && flatRow >= 0 && childRow >= 0) {
            // The model's own discrimination, for the three rows being measured.
            check(levelOf(sectionRow) == 0, "C18: a section row is level 0");
            check(levelOf(flatRow) == 0,
                  QString("C18: a CHILDLESS top-level destination is level 0, not 1 (%1)")
                      .arg(levelOf(flatRow)));
            check(levelOf(childRow) == 1,
                  QString("C18: a destination under a section is level 1 (%1)").arg(levelOf(childRow)));
            check(model.sectionOf(flatRow) < 0 && model.sectionOf(childRow) >= 0,
                  "C18: the rows measured as top-level and nested really are (sectionOf)");

            // The lanes, read off the delegate's own items. The label is the anchored
            // Text ("rowLabelText"), so its x IS its left margin inside the row; the
            // badge and the disclosure caret are the row's two Images, told apart by
            // their size (18 and 12).
            struct Lanes {
                double badge = -1;
                double label = -1;
                double caret = -1;
                bool caretVisible = false;
                bool found = false;
            };
            auto lanesOf = [&](int row) {
                Lanes l;
                QQuickItem* it = itemAt(row);
                if (!it) return l;
                l.found = true;
                if (QQuickItem* lbl = it->findChild<QQuickItem*>(QStringLiteral("rowLabelText")))
                    l.label = lbl->x();
                const auto kids = it->findChildren<QQuickItem*>();
                for (QQuickItem* c : kids) {
                    if (!c->inherits("QQuickImage")) continue;
                    if (near(c->width(), 18.0) && near(c->height(), 18.0)) l.badge = c->x();
                    if (near(c->width(), 12.0) && near(c->height(), 12.0)) {
                        l.caret = c->x();
                        l.caretVisible = c->isVisible();
                    }
                }
                return l;
            };
            reveal(sectionRow);
            const Lanes sec = lanesOf(sectionRow);
            reveal(flatRow);
            const Lanes flat = lanesOf(flatRow);
            reveal(childRow);
            const Lanes child = lanesOf(childRow);
            check(sec.found && flat.found && child.found,
                  "precondition (C18): all three measured rows have a delegate to read");
            check(sec.label > 0 && flat.label > 0 && child.label > 0,
                  "precondition (C18): all three labels have a real x (not NaN)");

            // The label lane: the thing a person reads as "this is indented".
            check(near(sec.label, 48.0),
                  QString("C18: a section's label sits at x=48 (measured %1)").arg(sec.label));
            check(near(flat.label, 48.0),
                  QString("C18: a CHILDLESS top-level destination's label sits at x=48, level with "
                          "a section - NOT at the child indent (measured %1)").arg(flat.label));
            check(near(child.label, 62.0),
                  QString("C18: a destination really inside a section still sits at x=62 (measured %1)")
                      .arg(child.label));
            check(!near(flat.label, child.label),
                  "C18: the childless top-level row is not drawn at the child indent");
            check(near(flat.label, sec.label),
                  "C18: it is drawn at exactly the section lane");

            // The badge lane: the other visible expression that used `isSection`.
            check(near(sec.badge, 24.0),
                  QString("C18: a section's icon badge sits at x=24 (measured %1)").arg(sec.badge));
            check(near(flat.badge, 24.0),
                  QString("C18: a childless top-level destination's badge sits at x=24 (measured %1)")
                      .arg(flat.badge));
            check(near(child.badge, 38.0),
                  QString("C18: a nested destination's badge still sits at x=38 (measured %1)")
                      .arg(child.badge));

            // The caret lane. The disclosure glyph is still sections-only (visible:
            // model.isSection is untouched), so it is asserted where it is visible; its
            // x follows the same level statement, which is checked on the invisible
            // instance that a destination really does carry.
            check(sec.caretVisible, "C18: a section still discloses (its caret is visible)");
            check(!flat.caretVisible && !child.caretVisible,
                  "C18: a DESTINATION still has no disclosure caret, childless or not (isSection unchanged)");
            check(near(sec.caret, 6.0),
                  QString("C18: a section's caret sits at x=6 (measured %1)").arg(sec.caret));
            check(near(child.caret, 20.0),
                  QString("C18: the caret lane follows the level for a nested row too (measured %1)")
                      .arg(child.caret));
        }
    }

    // ---- THE STRUCTURAL RULE (decided 2026-10-08): the data table asks for a
    // separator after the `general` block, and the model inserts it as a row of its
    // own kind. The MODEL half - where the row sits, what it is, and that nothing can
    // select, activate or step onto it - is asserted in tests/destination_picker.cpp.
    // What only the built scene can answer is what the DELEGATE does with it: that the
    // row's band is the separator's own height, that a 1 px hairline is drawn at the
    // theme's rule colour across the LIST's width (not the indent lane), and that the
    // row cannot be hovered or clicked.
    //
    // Read off the scene's own items rather than a screenshot - this test has no
    // display to grab, and "the rule is there" is a RECTANGLE question, which the
    // delegate answers exactly.
    {
        auto spin = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
        };
        auto near = [](double a, double b) { return a > b - 0.5 && a < b + 0.5; };

        // The block above leaves the field empty (its C18 precondition clears it), and
        // the rule is drawn only with an empty query - say so rather than assume it.
        search->setProperty("text", QString());
        spin(250);
        check(model.query().isEmpty(),
              "precondition (rule): the search filter is cleared, so the list shows its structure");

        // The scene binds the row kind by ROLE NAME (like `level`), which is the
        // contract between this model and the QML: a role the scene cannot see is a
        // delegate that falls back to guessing from an empty label.
        bool hasSeparatorRole = false;
        const QHash<int, QByteArray> names = model.roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it) {
            if (it.value() == "isSeparator") hasSeparatorRole = true;
        }
        check(hasSeparatorRole, "the model exposes an 'isSeparator' role for the delegate to branch on");

        QList<int> separatorRows;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (model.isSeparator(row)) separatorRows << row;
        }
        check(separatorRows.size() == 1,
              QStringLiteral("precondition (rule): the model has exactly one separator (%1)")
                  .arg(separatorRows.size()));

        QQuickItem* list = root->findChild<QQuickItem*>(QStringLiteral("pickerList"));
        check(list != nullptr, "precondition (rule): the list is addressable");
        auto itemAt = [&](int row) -> QQuickItem* {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, row));
            return it;
        };

        if (list && separatorRows.size() == 1) {
            const int sep = separatorRows.first();
            const int above = sep - 1;  // the last general row
            const int below = sep + 1;  // the flat destination listed right under the rule
            // Put the row ABOVE the rule at the top of the viewport, so all three rows
            // around it are inside it: the scene has scrolled deep into the list (the
            // remembered destination is the current row), and a delegate that was never
            // instantiated has no rectangle to measure.
            QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, above), Q_ARG(int, 0));
            spin(250);
            QQuickItem* aboveItem = itemAt(above);
            QQuickItem* sepItem = itemAt(sep);
            QQuickItem* belowItem = itemAt(below);
            check(aboveItem && sepItem && belowItem,
                  "precondition (rule): the three rows around the separator have delegates");

            if (aboveItem && sepItem && belowItem) {
                // The band IS the separator's height, in the real layout: the row above
                // is the usual 30 px rhythm, then the rule's own band, then the next row.
                check(near(aboveItem->height(), 30.0),
                      QStringLiteral("the row above the rule is a normal 30 px row (%1)")
                          .arg(aboveItem->height()));
                check(near(sepItem->height(), 12.0),
                      QStringLiteral("the separator's delegate is the 12 px band (%1)")
                          .arg(sepItem->height()));
                check(near(sepItem->y() - aboveItem->y(), 30.0),
                      QStringLiteral("and it starts where the 30 px row above it ends (%1)")
                          .arg(sepItem->y() - aboveItem->y()));
                check(near(belowItem->y() - sepItem->y(), 12.0),
                      QStringLiteral("with the next row starting 12 px lower (%1)")
                          .arg(belowItem->y() - sepItem->y()));

                // The rule itself: one hairline, the full width of the LIST (NOT the
                // indent lane - the statement is about the list's structure, so it must
                // not take the child lane or move the lanes the rows draw in), in the
                // token every other rule in this design uses.
                QQuickItem* rule =
                    sepItem->findChild<QQuickItem*>(QStringLiteral("rowSeparatorRule"));
                check(rule != nullptr, "the separator delegate draws a rule item");
                if (rule) {
                    check(rule->isVisible(), "the rule is drawn (not merely present)");
                    check(near(rule->height(), 1.0),
                          QStringLiteral("it is one pixel high (%1)").arg(rule->height()));
                    check(near(rule->width(), list->width()),
                          QStringLiteral("and spans the whole list width, not the indent lane "
                                         "(%1 vs the list's %2)")
                              .arg(rule->width()).arg(list->width()));
                    check(near(rule->x(), 0.0),
                          QStringLiteral("starting at the list's left edge (%1)").arg(rule->x()));
                    // The band exists FOR the margin: a rule flush against the row above
                    // would read as that row's underline rather than as a boundary after
                    // the block, so the rule is centered (and pixel-snapped) inside it.
                    check(rule->y() >= 1.0 && rule->y() + rule->height() <= sepItem->height() - 1.0,
                          QStringLiteral("with air above and below it inside its band (y %1 of %2)")
                              .arg(rule->y()).arg(sepItem->height()));
                    check(rule->property("color").value<QColor>() == theme.hairline(),
                          "and it uses the theme's own rule colour, theme.hairline "
                          "(so it agrees with the form's rules)");
                }

                // No words and no interaction: the row is disabled, so it can take
                // neither the hover highlight nor a click, and its label carries
                // nothing a tooltip could be about.
                check(!sepItem->isEnabled(),
                      "the separator row is not hit-testable (a disabled delegate emits no "
                      "clicked and lights no HoverHandler)");
                QQuickItem* label = sepItem->findChild<QQuickItem*>(QStringLiteral("rowLabelText"));
                check(label && label->property("text").toString().isEmpty(),
                      "and it carries no label text");
            }
        }
    }

    // ---- A3/B11 (contract items 3 and 11): a click on the picker's own empty
    // space removes the search field's focus, and its TEXT and the list's
    // SELECTION are unchanged by that click. The export form's shell has had a
    // background MouseArea since the start; this scene had none until
    // 2026-10-09, so a click beside the list left the field holding a caret
    // while the pointer was somewhere else entirely.
    //
    // The observables are the window's REAL active focus item (what a click
    // actually changes) plus the two things item 11 says the click must not
    // disturb - the field's text and the model's selection - so a handler that
    // "cleared focus" by wiping the query would fail here.
    {
        auto spin = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
        };
        // The scene sizes from its ROOT item, and the ColumnLayout's 14px margin
        // is where "outside the controls" actually is.
        window.resize(640, 480);
        root->setWidth(640);
        root->setHeight(480);
        spin(250);
        search->forceActiveFocus();
        spin(60);
        check(window.activeFocusItem() == search,
              "B11 precondition: the field holds the window's focus, the state the picker opens in");
        search->setProperty("text", QStringLiteral("telegram"));
        spin(200);
        check(model.query() == QLatin1String("telegram"),
              "B11 precondition: the field holds a query, so the text is observable");
        const QStringList rowsBefore = onScreenRows(model);
        const int rowBefore = model.currentRow();
        check(!rowsBefore.isEmpty(),
              "B11 precondition: the filtered list really is filtered, so a row change would show");

        const QPointF outside(4, 4); // inside the root, outside every control
        QMouseEvent press(QEvent::MouseButtonPress, outside, outside,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, outside, outside,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&window, &release);
        spin(60);
        check(window.activeFocusItem() != search && !search->hasActiveFocus(),
              "B11: a click on the picker's empty space removes the search field's focus");
        check(search->property("text").toString() == QLatin1String("telegram"),
              "B11: and the field's text is unchanged by that click");
        check(model.query() == QLatin1String("telegram"),
              "B11: so the filter it drives is unchanged too");
        check(model.currentRow() == rowBefore && onScreenRows(model) == rowsBefore,
              "B11: and the list's selection and its contents are unchanged");
        // Back to the unfiltered table, which is the state the blocks below expect.
        search->setProperty("text", QString());
        spin(200);
        check(model.query().isEmpty(),
              "precondition: the query is cleared again for the blocks that follow");
    }

    // ---- E27, the wheel case: the row tooltip must follow its row when the list
    // scrolls under a stationary hovered pointer. The owner rect the tooltip is
    // positioned from was read as the delegate's own x/y, which are coordinates
    // in the view's CONTENT - and a ListView scroll moves the content under a clip
    // window that does not move. So the two numbers were identical before and
    // after a scroll: the tooltip stayed anchored where the row USED to be, E27
    // was violated in that one case, and E28's clamp still held (it re-reads every
    // ancestor's contentY), which is why the clamp's own tests saw nothing wrong.
    //
    // The observation is the one a person sees: the bubble's distance from its row,
    // in window coordinates, must not change when the scroll moves both of them.
    // The scroll is written directly on contentY - the same property every wheel
    // notch, page and flick writes - so the fix is measured on the number that
    // decides the position, with no animation or recycling to obscure it.
    {
        auto near = [](double a, double b) { return a > b - 0.5 && a < b + 0.5; };
        auto spin = [](int ms) {
            QElapsedTimer t;
            t.start();
            while (t.elapsed() < ms) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(5);
            }
        };
        window.resize(640, 480);
        root->setWidth(640);
        root->setHeight(480);
        spin(250);

        QQuickItem* list = root->findChild<QQuickItem*>(QStringLiteral("pickerList"));
        check(list != nullptr, "precondition (E27/scroll): the list is addressable");
        auto itemAt = [&](int row) -> QQuickItem* {
            QQuickItem* it = nullptr;
            QMetaObject::invokeMethod(list, "itemAtIndex",
                                      Q_RETURN_ARG(QQuickItem*, it), Q_ARG(int, row));
            return it;
        };
        // The row is chosen by INDEX and then brought into view on purpose: a
        // delegate only exists for a row near the viewport, so picking one that
        // already has a delegate ties the test to wherever the previous block left
        // the scroll - and that is how the first version of this block ended up
        // measuring a row pressed against the end of the content, where the step it
        // asked for was clamped away and the tooltip legitimately flipped.
        int rowIdx = -1;
        for (int row = 0; row < model.rowCount(); ++row) {
            if (!model.isOnScreen(row) || model.isSection(row)) continue;
            if (model.labelOf(row).length() < 10) continue;
            rowIdx = row;
            break;
        }
        check(rowIdx >= 0,
              "precondition (E27/scroll): an on-screen row with a long label exists");
        if (rowIdx >= 0) {
            QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, rowIdx), Q_ARG(int, 0));
            spin(250);
        }
        QQuickItem* rowItem = rowIdx >= 0 ? itemAt(rowIdx) : nullptr;
        QQuickItem* rowLabel =
            rowItem ? rowItem->findChild<QQuickItem*>(QStringLiteral("rowLabelText")) : nullptr;
        check(rowItem && rowLabel != nullptr,
              "precondition (E27/scroll): that row has a delegate and a label to elide");
        if (rowItem && rowLabel) {
            rowItem->setWidth(90); // the label lane narrows to 18px and the label elides
            spin(150);
            check(rowLabel->property("truncated").toBool(),
                  "precondition (E27/scroll): the narrowed row's label elides, so it keeps a tooltip");
            // CENTRE the row in the viewport, clamped to the view's own bounds: a row
            // near an edge has no room to scroll in one direction, and E27 turns the
            // bubble above at the bottom edge - so the gap would change for a reason
            // that has nothing to do with the binding under test.
            const qreal viewportH = list->height();
            const qreal contentH = list->property("contentHeight").toReal();
            const qreal maxY = qMax(0.0, contentH - viewportH);
            list->setProperty("contentY", qMin(maxY, qMax(0.0,
                rowItem->y() - (viewportH - rowItem->height()) / 2)));
            spin(250);
            const QPointF hoverPoint =
                rowItem->mapToItem(window.contentItem(), QPointF(30, 15));
            QMouseEvent move(QEvent::MouseMove, hoverPoint, hoverPoint,
                             Qt::NoButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(&window, &move);
            QHoverEvent hover(QEvent::HoverMove, hoverPoint, hoverPoint, QPointF(-10, 15));
            QCoreApplication::sendEvent(&window, &hover);
            QCoreApplication::processEvents();
            spin(900); // past the 500ms E26 wake-up delay

            // The ONE row tooltip: the same scan E29's block performs, so the row
            // tip cannot be confused with an icon button's own instance.
            QObject* rowTip = nullptr;
            for (QObject* o : root->findChildren<QObject*>()) {
                if (!QByteArray(o->metaObject()->className()).contains("AeToolTip")) continue;
                bool inButton = false;
                for (QObject* p = o->parent(); p; p = p->parent())
                    if (QByteArray(p->metaObject()->className()).contains("AeIconButton")) {
                        inButton = true;
                        break;
                    }
                if (!inButton) rowTip = o;
            }
            auto* tip = qobject_cast<QQuickItem*>(rowTip);
            check(tip && tip->property("revealed").toBool(),
                  "precondition (E27/scroll): the row tooltip is revealed before the scroll");
            if (tip && tip->property("revealed").toBool()) {
                const qreal rowY0 = rowItem->mapToItem(window.contentItem(), 0, 0).y();
                const qreal tipY0 = tip->mapToItem(window.contentItem(), 0, 0).y();
                const qreal gap0 = tipY0 - rowY0;
                check(tipY0 >= rowY0 + rowItem->height(),
                      QStringLiteral("precondition (E27/scroll): it sits BELOW its row (gap %1 px), "
                                     "so the gap is the one E27 chose").arg(gap0));

                // The step the view can really take. Scrolling DOWN cannot flip the
                // bubble (it moves the row away from the bottom edge, where E27
                // legitimately turns the tip above), so down is preferred; but a row
                // near the end of the content has no room down at all, so UP is the
                // fallback, its step capped by the room the bubble still needs below
                // the row. Either way the step is the room that is actually there:
                // the Flickable clamps a bigger one, and a measurement of the clamp
                // is a measurement of nothing.
                const qreal cy = list->property("contentY").toReal();
                const qreal rowTop = rowY0 - list->mapToItem(window.contentItem(), 0, 0).y();
                const qreal rowBottom = rowTop + rowItem->height();
                const qreal roomDown = qMin(maxY - cy, rowTop);
                const qreal roomUp = qMin(cy, qMax(0.0, viewportH - rowBottom - 6.0 - tip->height()));
                check(roomDown >= 25 || roomUp >= 25,
                      QStringLiteral("precondition (E27/scroll): the view has room to scroll either "
                                     "way with the row staying on screen and the bubble un-flipped "
                                     "(down %1 px, up %2 px at contentY %3 of %4)")
                          .arg(roomDown).arg(roomUp).arg(cy).arg(maxY));
                const qreal step = roomDown >= 25 ? qMin(60.0, roomDown)
                                                  : -qMin(60.0, roomUp);
                list->setProperty("contentY", cy + step);
                spin(200);
                const qreal after = list->property("contentY").toReal();
                check(qAbs((after - cy) - step) < 0.5,
                      QStringLiteral("precondition (E27/scroll): the list scrolled by exactly the "
                                     "step asked for (%1px: %2 -> %3, no clamp in the way)")
                          .arg(step).arg(cy).arg(after));
                check(after != cy,
                      QStringLiteral("precondition (E27/scroll): the scroll really happened (%1 -> "
                                     "%2), so the checks below are not vacuous").arg(cy).arg(after));
                const qreal rowY1 = rowItem->mapToItem(window.contentItem(), 0, 0).y();
                check(near(rowY0 - rowY1, after - cy),
                      QStringLiteral("the row moved with the scroll (row y %1 -> %2, contentY %3 -> "
                                     "%4)").arg(rowY0).arg(rowY1).arg(cy).arg(after));
                // A stationary pointer over the scrolled list RE-HOVERS: the window
                // updates the item under the cursor when the content moves under it,
                // which is how the tooltip's subject changes while a person scrolls.
                // The move below is that delivery (measured: without it the hover is
                // simply lost and the tooltip has no owner to be measured against),
                // and the row it lands on is the one moved under the pointer.
                const QPointF reHover =
                    rowItem->mapToItem(window.contentItem(), QPointF(30, 15));
                QMouseEvent move2(QEvent::MouseMove, reHover, reHover,
                                  Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(&window, &move2);
                QHoverEvent hover2(QEvent::HoverMove, reHover, reHover, hoverPoint);
                QCoreApplication::sendEvent(&window, &hover2);
                QCoreApplication::processEvents();
                spin(900); // the new owner is a new tooltip and gets its own delay
                const qreal rowY2 = rowItem->mapToItem(window.contentItem(), 0, 0).y();
                const qreal tipY2 = tip->mapToItem(window.contentItem(), 0, 0).y();
                check(tip->property("revealed").toBool()
                          && tip->parentItem()->property("hoveredRow").toInt() == rowIdx,
                      "precondition (E27/scroll): the tooltip is revealed again on the same row "
                      "after the scroll");
                // THE FIX, measured two ways:
                //  (1) the owner rect the tooltip is positioned from is the row's
                //      DRAWN position - the delegate's own y inside the view's
                //      CONTENT is not where the row is drawn; the contentY read in
                //      the binding is what makes the difference, and it is 982px of
                //      scroll here, so a stale binding is not a subtle failure.
                check(near(tip->property("ownerY").toReal(),
                           rowItem->mapToItem(tip->parentItem(), 0, 0).y()),
                      QStringLiteral("E27: the tooltip's owner is the row's DRAWN position "
                                     "(ownerY %1 vs the row's %2 in the list area, at contentY %3)")
                          .arg(tip->property("ownerY").toReal())
                          .arg(rowItem->mapToItem(tip->parentItem(), 0, 0).y())
                          .arg(after));
                //  (2) what a person sees: the bubble follows its row, in window
                //      coordinates, over the scroll - same side, same gap.
                check(qAbs((tipY2 - rowY2) - gap0) < 2.0,
                      QStringLiteral("E27: the tooltip kept its place under the row through the "
                                     "scroll (gap %1 -> %2 px over a %3px scroll; before the "
                                     "contentY dependency the bubble stayed put and the gap read %4)")
                          .arg(gap0).arg(tipY2 - rowY2).arg(after - cy).arg(gap0 + (after - cy)));
                // Restore the view for the block that follows, which measures the
                // wheel from rest at the top.
                list->setProperty("contentY", cy);
                spin(150);
            }
        }
    }

    // ---- D23 (contract line 117-118): the list "responds to the wheel". The export
    // FORM's scroller has had a wheel assertion since D25 (form_layout.cpp); the
    // picker's own list never did - the GUI probe drives its scroll BAR with xdotool
    // drags (scripts/gui/probe_scroll.sh) and never the wheel - so D23's "responds
    // to the wheel" had no test on this list at all, and this list is what the
    // "scrolling feels sluggish" report is about.
    //
    // WHAT IT MEASURES, and why that is the number the report needs: one 120-unit
    // notch moves the list by wheelScrollLines * 24 px - QQuickFlickable's own
    // arithmetic, read in qquickflickable.cpp, where the no-acceleration path does
    // "wheelScroll = -qApp->styleHints()->wheelScrollLines() * 24" and then
    // "scrollPixel = -yDelta / 120 * wheelScroll" - which is 72 px on a platform
    // whose wheelScrollLines() is 3.
    //
    // Against the 30 px delegate that is 2.4 rows of travel for one notch, so the
    // [measured] line below states the step in both units. The readings, each from rest:
    // the notch that immediately follows a DIRECT contentY write jumps far - measured
    // 1125 px whatever the wheel delta is, i.e. the wheel's own timeline resuming a
    // target it was left holding - so that one is a WARM-UP whose travel is reported and
    // not asserted; then one notch moves DOWN, the next moves further down, the opposite
    // notch undoes exactly the last step, and a notch of an eighth the delta moves an
    // eighth as far.
    //
    // The step is EASED, and that is Qt's, not the scene's: DestinationPicker.qml has
    // no Behavior, no wheel step and no flick properties, and the notch is played by
    // QQuickFlickable through its own timeline - moveBy(..., OutExpo,
    // 3*fixupDuration/4) = 300 ms, read in the same file. So every reading here is a
    // SETTLED one: a contentY read the instant the event lands is a snapshot of a
    // running animation, not the step the wheel asked for.
    //
    // WHAT THIS BLOCK DOES NOT MEASURE: frame time, GPU cost, or how that ease looks
    // while it runs - an offscreen run has no frames to time. It pins the STEP, which
    // is what D23 says and all an offscreen scene can see.
    {
        QQuickItem* list = root->findChild<QQuickItem*>(QStringLiteral("pickerList"));
        check(list != nullptr, "precondition (D23): the list is addressable");
        if (list) {
            auto contentY = [list]() { return list->property("contentY").toReal(); };
            // Read from rest only: the ease above runs for 300 ms, so a step is taken
            // once contentY has stopped moving (8 quiet polls, ~40-160 ms), with a 2 s
            // cap so a step that never settles fails here rather than reading a number
            // that is still moving.
            auto settle = [&]() {
                QElapsedTimer t;
                t.start();
                qreal last = contentY();
                int quiet = 0;
                while (t.elapsed() < 2000 && quiet < 8) {
                    const qreal now = contentY();
                    if (now == last) ++quiet;
                    else { quiet = 0; last = now; }
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                    QThread::msleep(5);
                }
                return contentY();
            };

            // The start state has to be one the SCENE agrees with, or the scene moves
            // the view after this block has started measuring "from rest". The blocks
            // above (and C18's reveal) left the model's current row DEEP in the list,
            // and DestinationPicker.qml keeps the selection on screen with
            // `onCurrentRowChanged -> positionViewAtIndex(currentRow, Contain)`; a view
            // whose current row is deep still owes a scroll to that row, and that
            // scroll is the 1110 px the first notch used to swallow. Writing contentY
            // directly cannot beat it because the scene's scroll arrives afterwards.
            //
            // So bring the selector itself to the top through the MODEL, not the view:
            // the FIRST destination row (row 0, generic.video, a level-0 destination -
            // select() refuses sections, which is exactly the behaviour wanted here).
            // Moving the model's current row to the top makes the scene's own handler
            // put the view at the top too, so nothing is left queued to move it.
            // Writing contentY is kept as a belt-and-braces start, and the precondition
            // below asserts the settled value really is the top, so a future change
            // that makes the scene scroll elsewhere fails loudly instead of producing a
            // 1110 px "notch".
            //
            // This is the LAST block in main(), so the selection it moves is not
            // restored; if a block is ever added after it, restore the previous
            // current row here.
            int firstDestination = -1;
            for (int row = 0; row < model.rowCount(); ++row) {
                if (!model.isSection(row)) { firstDestination = row; break; }
            }
            check(firstDestination >= 0,
                  "precondition (D23): the model has a first destination row to select");
            if (firstDestination >= 0) model.select(firstDestination);
            list->setProperty("contentY", 0.0);
            const qreal restY = settle();
            check(qAbs(restY) <= 1.0,
                  QStringLiteral("precondition (D23): after selecting the first destination the view "
                                 "rests at the top (contentY %1), so no scene scroll is pending to "
                                 "fight the wheel").arg(restY));
            // STABLE, not merely quiet for one settle: a second rest has to read the
            // same value, or something (a deferred scene scroll) is still coming.
            check(qAbs(settle() - restY) <= 1.0,
                  "precondition (D23): and it stays at the top from rest to rest");

            const qreal viewportH = list->height();
            const qreal contentH = list->property("contentHeight").toReal();
            check(contentH > viewportH,
                  QStringLiteral("precondition (D23): the content is taller than the viewport (%1 > %2), "
                                 "so a notch has somewhere to go").arg(contentH).arg(viewportH));
            int collapsed = 0;
            for (int row = 0; row < model.rowCount(); ++row)
                if (model.isSection(row) && !model.isExpanded(row)) ++collapsed;
            check(collapsed >= 1,
                  QStringLiteral("precondition (D23): at least one section is collapsed, so the 0-height "
                                 "delegates are really part of what a notch crosses (%1 collapsed)")
                      .arg(collapsed));
            // The delegate's row height, read off a live on-screen row rather than
            // restated: `height: model.onScreen ? 30 : 0` is the QML's own statement of
            // it, and a change there must not leave rows-per-notch quietly wrong.
            qreal rowH = -1;
            for (int row = 0; row < model.rowCount() && rowH < 0; ++row) {
                QQuickItem* rowDelegate = nullptr;
                QMetaObject::invokeMethod(list, "itemAtIndex",
                                          Q_RETURN_ARG(QQuickItem*, rowDelegate), Q_ARG(int, row));
                if (rowDelegate && rowDelegate->height() > 0) rowH = rowDelegate->height();
            }
            check(rowH == 30,
                  QStringLiteral("precondition (D23): an on-screen delegate row is the QML's 30 px "
                                 "(measured %1)").arg(rowH));

            // One notch, built and delivered the way D25's wheel is (form_layout.cpp):
            // a QWheelEvent sent to the WINDOW at a point over the list - its centre,
            // in window coordinates - which is what a real wheel does; the window hands
            // it to the item under the pointer and then up that item's ancestors to the
            // Flickable. The timestamp is the one thing a hand-built event must be
            // given: a real wheel event always carries one, and QQuickFlickable (read in
            // qquickflickable.cpp) drops a notch whose elapsed time against the previous
            // one is not positive - so two timestamp-0 events in a row would silently
            // measure a step that never happened.
            quint64 stamp = 0;
            auto notch = [&](int aAngle) {
                const QPointF at = list->mapToItem(window.contentItem(),
                                                   QPointF(list->width() / 2, list->height() / 2));
                QWheelEvent we(at, at, QPoint(0, 0), QPoint(0, aAngle),
                               Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                we.setTimestamp(stamp += 200);
                QCoreApplication::sendEvent(&window, &we);
                return settle();
            };

            // WARM-UP, AND WHY IT IS NOT ASSERTED. Measured: the notch that follows a
            // DIRECT contentY write travels far - 1125 px - and the same 1125 px whatever
            // the wheel delta is (120 units and 15 units both read 1125), so it is not a
            // step at all: it is QQuickFlickable's wheel timeline resuming a target it was
            // left holding when the view was moved behind its back. That matters beyond
            // the harness - the SCENE writes the view the same way, with
            // `positionViewAtIndex(currentRow, Contain)` whenever the current row changes
            // (typing a query, stepping with the arrows) - but the harness cannot say
            // whether a REAL wheel event through the compositor hits it, and offscreen
            // there is no compositor to ask. So the travel is REPORTED below and the
            // assertions start from the stream that follows.
            const qreal warmUp = notch(-120);
            // Back to the top for the measured pair: contentY is written directly, and
            // the model's current row is already the top row, so the scene has no scroll
            // of its own to add (it re-positions the view on a current-row CHANGE only,
            // and a wheel notch never changes the current row).
            list->setProperty("contentY", 0.0);
            const qreal restart = settle();

            // The reading `before` is the ACTUAL contentY, taken from rest immediately
            // before the notch is sent - never an assumed 0 - so a scene that moves the
            // view again (a resize settling, a row change) cannot silently turn the
            // delta below into something other than the wheel's own step.
            const qreal before = contentY();
            // One 120-unit notch is wheelScrollLines() * 24 px, QQuickFlickable's own
            // no-acceleration arithmetic (read in qquickflickable.cpp; the block header
            // above spells it out). The precondition is only that the notch has that
            // much room before the bottom edge: a step clamped by the edge would be a
            // SHORTER one, and the assertions below would then fail for the clamp
            // rather than for the wheel.
            const qreal oneNotch = QGuiApplication::styleHints()->wheelScrollLines() * 24.0;
            check(qAbs(restart - before) <= 1.0 && before + oneNotch <= contentH - viewportH,
                  QStringLiteral("precondition (D23): the warm-up left the view where it can be "
                                 "measured from and there is room to move down from the reading "
                                 "(contentY %1 + one notch %2 px <= contentHeight %3 - viewport %4 "
                                 "= %5)")
                      .arg(before).arg(oneNotch).arg(contentH).arg(viewportH)
                      .arg(contentH - viewportH));

            const qreal y1 = notch(-120);
            check(y1 > before,
                  QStringLiteral("D23: one wheel notch scrolls the list down (contentY %1 -> %2)")
                      .arg(before).arg(y1));

            const qreal y2 = notch(-120);
            check(y2 > y1,
                  QStringLiteral("D23: a second notch scrolls it further down (%1 -> %2)")
                      .arg(y1).arg(y2));

            const qreal y3 = notch(120);
            check(qAbs(y3 - y1) < 0.5,
                  QStringLiteral("D23: a notch the other way undoes the last step exactly "
                                 "(%1 vs %2)").arg(y3).arg(y1));

            // THE STEP IS PROPORTIONAL TO THE DELTA, which is what rules out a page scroll
            // or a flick: QQuickFlickable's arithmetic is angleDelta / 120 *
            // wheelScrollLines() * 24, so a notch of 15 units must move exactly an eighth
            // of what a notch of 120 units moves (72 px vs 9 px at wheelScrollLines()==3).
            // The ratio is asserted rather than the pixels, because the pixels follow the
            // platform's wheelScrollLines(); the ratio does not.
            const qreal step120 = y2 - y1;
            const qreal y4 = notch(-15);
            const qreal step15 = y4 - y3;
            check(step15 > 0.0 && qAbs(step120 / step15 - 8.0) < 0.2,
                  QStringLiteral("D23: the step follows the wheel delta, not a page or a flick "
                                 "(120 units -> %1 px, 15 units -> %2 px, ratio %3, expected 8)")
                      .arg(step120).arg(step15).arg(step15 > 0.0 ? step120 / step15 : 0.0));

            // The reading the report needs, in the test's own output: what one notch is
            // worth in pixels and in rows, and what the platform calls a notch
            // (wheelScrollLines() is the style hint QQuickFlickable itself sizes the
            // step from).
            QTextStream(stdout) << QStringLiteral(
                "[measured] picker wheel (D23): per-notch delta=%1px contentHeight=%2 viewportHeight=%3 "
                "delegateRowHeight=%4 rows-per-notch=%5 wheelScrollLines=%6 collapsedSections=%7 "
                "warmUpNotchTravel=%8px stepAt15Units=%9px")
                .arg(step120).arg(contentH).arg(viewportH).arg(rowH)
                .arg(step120 / rowH).arg(QGuiApplication::styleHints()->wheelScrollLines()).arg(collapsed)
                .arg(warmUp).arg(step15)
                << "\n";
        }
    }

    delete created;
    QTextStream(stdout) << (failures ? "=== FAILED " : "=== OK ")
                        << failures << " failures\n";
    return failures ? 1 : 0;
}
