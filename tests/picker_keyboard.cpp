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
// Deliberately NOT here, because it needs a window and this file has none:
//   B12 the pointer is an I-beam over the text area (a cursor shape is a property of
//       a real window, and X11 screen grabs do not contain the pointer either - this
//       needs `ffmpeg -f x11grab -draw_mouse 1` against a visible window).
//   B11 clicking outside the field removes its focus (there is no focus without a
//       window, so there is nothing to remove).
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
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUrl>
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
        // its hover text, not the component). The manual button's explanatory
        // prose is gone instance and all.
        QObject* rowTip = nullptr;
        int rowTips = 0;
        bool manualFound = false;
        const auto all = root->findChildren<QObject*>();
        for (QObject* o : all) {
            const QByteArray cls = o->metaObject()->className();
            if (cls.contains("AeToolTip")) {
                bool inButton = false;
                for (QObject* p = o->parent(); p; p = p->parent())
                    if (QByteArray(p->metaObject()->className()).contains("AeIconButton")) { inButton = true; break; }
                if (!inButton) { ++rowTips; rowTip = o; }
            }
            if (cls.contains("AeButton")
                && o->property("text").toString() == model.property("manualText").toString()) {
                manualFound = true;
                int underManual = 0;
                for (QObject* d : o->findChildren<QObject*>())
                    if (QByteArray(d->metaObject()->className()).contains("AeToolTip")) ++underManual;
                check(underManual == 0, "E29: the manual button carries NO tooltip (its prose is dropped)");
            }
        }
        check(manualFound, "precondition: the manual button is in the scene");
        check(rowTips == 1 && rowTip, "E29: exactly one ROW tooltip feeds from the list hover");

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

    delete created;
    QTextStream(stdout) << (failures ? "=== FAILED " : "=== OK ")
                        << failures << " failures\n";
    return failures ? 1 : 0;
}
