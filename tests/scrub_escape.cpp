// Numeric fields and Escape: the F34 contract, offscreen.
//
// Decided 2026-09-30 (docs/export-dialog-behavior.md item 34): Escape in a numeric
// field's text-entry mode restores the value FROM WHEN THIS EDIT BEGAN - the
// field's focused moment - not the value from when the dialog opened, and the edit
// exits. Item 33 keeps the existing blur-on-Escape design.
//
// Why the obvious reading is not enough, and why the middle scenario below exists:
// ScrubSpinBox's cancel path used to "keep the committed value", which is USUALLY
// the edit-start value - but the owner can write `value` while an edit is open (the
// dialog re-syncs on aspect locks and range clamps; the accessibility setter writes
// through the same path), and "keep whatever the property holds now" then restores
// a value the user's edit never saw. The decided semantics name the moment - edit
// start - so the control must REMEMBER it.
//
// The sentinel ("Auto") is part of the same line: AeNumberField shows a word when
// the value is the sentinel (placeholderValue), and an undone edit must come back
// to the sentinel state, not to some number underneath the word.
//
// Routing facts measured before writing this (scratch: escape-spike/):
//   * a key sent straight to an item reaches THAT item's Keys filters and its
//     editor, and does NOT bubble to ancestors - direct send is a same-item probe;
//   * a key sent to a QQuickWindow under QT_QPA_PLATFORM=offscreen is delivered to
//     the activeFocusItem and DOES bubble up the parent chain - so the window route
//     is the honest end-to-end route, and it works headless (active focus included).
// Escape is sent to the EDITOR (editInput, objectName "scrubEdit") here because the
// control's Keys.onEscapePressed lives ON the editor - same-item delivery is the
// exact path a real keystroke takes through it.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/scrub_escape
#include <QApplication>
#include <QKeyEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Send one key straight to the item (same-item delivery). The return value - the
// item accepted it - is a useful extra fact, never the assertion by itself.
static bool sendKey(QQuickItem* aItem, int aKey, const QString& aText = QString()) {
    QKeyEvent event(QEvent::KeyPress, aKey, Qt::NoModifier, aText);
    QCoreApplication::sendEvent(aItem, &event);
    return event.isAccepted();
}

// Type a string into whatever holds the editor's selection/caret, key by key.
static void typeText(QQuickItem* aItem, const QString& aText) {
    for (const QChar& ch : aText) sendKey(aItem, ch.unicode(), QString(ch));
}

// One scene instance per scenario: the control's restore writes `value`
// imperatively (which is correct and mirrors the scrub path), and an imperative
// write destroys the QML binding that feeds the property - reusing one instance
// across scenarios would have the SECOND scenario quietly lose its owner link and
// measure nothing. A fresh scene per scenario is also what "the value at edit
// start" means: no state carries over.
struct Scene {
    QQuickItem* root = nullptr;      // the scene root (the test's frame)
    QQuickItem* owner = nullptr;     // plainOwner: external + signal counters
    QQuickItem* plain = nullptr;     // the bare ScrubSpinBox (objectName "plain")
    QQuickItem* plainEdit = nullptr; // its editInput ("scrubEdit")
    QQuickItem* num = nullptr;       // the real AeNumberField (objectName "num")
    QQuickItem* numValueNode = nullptr; // its accessibility node ("aeAccessibleValue")
    QQuickItem* numEdit = nullptr;   // the AeNumberField's ScrubSpinBox editInput

    bool found() const {
        return root && owner && plain && plainEdit && num && numValueNode && numEdit;
    }
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    gui::QmlTheme theme;
    // The scene's glyphs come from the tinted scratch dir (see picker_keyboard.cpp):
    // without it the log fills with "Cannot open: ...svg" and nobody reads the run.
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    // The scene wraps the REAL controls (AeNumberField pulls in the real
    // ScrubSpinBox and AeIconButton; the bare ScrubSpinBox is the same file) and
    // wires the owner exactly as AeNumberField wires its own owner: `value` is fed
    // by a binding, and the control's changed()/valueEdited() carry writes back.
    QTemporaryDir sceneDir;
    check(sceneDir.isValid(), "a scratch dir for the scene");
    const QString scenePath = sceneDir.path() + QStringLiteral("/scene.qml");
    {
        QFile f(scenePath);
        check(f.open(QIODevice::WriteOnly | QIODevice::Text), "the scene file can be written");
        QByteArray qml;
        qml += "import QtQuick\n";
        qml += "import \"file:" AE_QML_DIR "\"\n";
        qml += "Item {\n";
        qml += "    width: 320; height: 160\n";
        qml += "    Item {\n";
        qml += "        id: plainOwner\n";
        qml += "        objectName: \"plainOwner\"\n";
        qml += "        property real external: 95\n";
        qml += "        property int changedCount: 0\n";
        qml += "        property int committedCount: 0\n";
        qml += "        property real lastChanged: -1000000\n";
        qml += "        ScrubSpinBox {\n";
        qml += "            id: plain\n";
        qml += "            objectName: \"plain\"\n";
        qml += "            width: 72; height: 24\n";
        qml += "            value: plainOwner.external\n";
        qml += "            from: 0; to: 100; step: 1; decimals: 0\n";
        qml += "            onChanged: (v) => { plainOwner.changedCount++; plainOwner.lastChanged = v; plainOwner.external = v }\n";
        qml += "            onCommitted: (v) => { plainOwner.committedCount++ }\n";
        qml += "        }\n";
        qml += "    }\n";
        qml += "    AeNumberField {\n";
        qml += "        id: num\n";
        qml += "        objectName: \"num\"\n";
        qml += "        y: 60\n";
        qml += "        width: 140\n";
        qml += "        label: \"Width\"\n";
        qml += "        placeholder: \"Auto\"\n";
        qml += "        placeholderValue: -1\n";
        qml += "        from: -1; to: 4096; step: 1; decimals: 2\n";
        qml += "        property real external: -1\n";
        qml += "        value: external\n";
        qml += "        onValueEdited: (v) => { external = v }\n";
        qml += "    }\n";
        qml += "}\n";
        f.write(qml);
    }

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QQmlComponent component(&engine, QUrl::fromLocalFile(scenePath));
    if (component.isError()) {
        QTextStream(stderr) << "the scene does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
        return 2;
    }

    // A real (offscreen) window: the F34 flow is focus-driven - beginEdit() focuses
    // the editor, Escape blurs it, and onActiveFocusChanged IS the edit lifecycle.
    // Measured: under the offscreen platform forceActiveFocus() really moves the
    // window's activeFocusItem, so the lifecycle runs exactly as in the dialog.
    QQuickWindow window;
    window.resize(320, 160);
    window.show();

    // One fresh scene per scenario (see struct note).
    Scene scene;
    auto makeScene = [&]() {
        if (scene.root) { delete scene.root; scene = Scene(); }
        QObject* created = component.create();
        scene.root = qobject_cast<QQuickItem*>(created);
        if (!scene.root) return scene;
        scene.root->setParentItem(window.contentItem());
        scene.owner = scene.root->findChild<QQuickItem*>(QStringLiteral("plainOwner"));
        scene.plain = scene.root->findChild<QQuickItem*>(QStringLiteral("plain"));
        scene.num = scene.root->findChild<QQuickItem*>(QStringLiteral("num"));
        if (scene.plain) scene.plainEdit = scene.plain->findChild<QQuickItem*>(QStringLiteral("scrubEdit"));
        if (scene.num) {
            scene.numValueNode = scene.num->findChild<QQuickItem*>(QStringLiteral("aeAccessibleValue"));
            scene.numEdit = scene.num->findChild<QQuickItem*>(QStringLiteral("scrubEdit"));
        }
        return scene;
    };

    makeScene();
    check(scene.found(), "the scene builds: plain ScrubSpinBox + AeNumberField with their editors");
    if (!scene.found()) return 1;

    // How an edit begins everywhere in the dialog: the click's release calls
    // beginEdit(), and the accessibility press calls it through the same function
    // ("This is the same state a plain click enters"). Both routes land here, so
    // the tests enter the edit through it too.
    auto beginPlainEdit = [&]() {
        return QMetaObject::invokeMethod(scene.plain, "beginEdit");
    };

    // ---- F34 core: the value FROM WHEN THIS EDIT BEGAN ------------------------
    // The scenario the old behavior gets wrong: the owner re-syncs `value` while
    // the edit is open (here: 95 -> 50), the user's typed "12" is never committed,
    // and Escape must undo the edit back to 95 - not keep 50 (the property's value
    // at Escape time) and not apply 12 (the uncommitted text).
    {
        check(beginPlainEdit(), "the edit begins (the route a click and a screen reader take)");
        check(scene.plainEdit->property("activeFocus").toBool(),
              "precondition: the editor has the focus the edit lifecycle needs");
        check(scene.plainEdit->property("text").toString() == QLatin1String("95"),
              "precondition: the editor opened on the value at edit start (95)");

        typeText(scene.plainEdit, QStringLiteral("12"));
        check(scene.plainEdit->property("text").toString() == QLatin1String("12"),
              "precondition: the typed text is in the editor, uncommitted");
        check(scene.plain->property("value").toReal() == 95.0,
              "precondition: typing alone did not move the value");

        // The dialog's mid-edit re-sync: an external write lands on `value`.
        scene.owner->setProperty("external", 50.0);
        check(scene.plain->property("value").toReal() == 50.0,
              "precondition: the owner's mid-edit re-sync landed (value is 50 now)");

        sendKey(scene.plainEdit, Qt::Key_Escape);
        check(scene.plain->property("value").toReal() == 95.0,
              "F34: Escape restores the value from when THIS edit began (95), "
              "not the re-synced value (50), not the typed text (12)");
        check(scene.owner->property("external").toReal() == 95.0,
              "F34: the owner re-syncs to the restored value (changed() carried it out)");
        check(!scene.plainEdit->property("activeFocus").toBool(),
              "F34: and the edit exits (item 33's blur-on-Escape stays)");
    }

    // ---- F34 "not the value from when the dialog opened" ----------------------
    // Two edits in a row: the first commits 30, the second is cancelled - the
    // restore must come back to 30 (this edit's start), not to 95 (where the
    // session started).
    {
        makeScene();
        check(scene.found(), "precondition: a fresh scene for the two-edits scenario");
        check(beginPlainEdit(), "precondition: first edit begins");
        typeText(scene.plainEdit, QStringLiteral("30"));
        sendKey(scene.plainEdit, Qt::Key_Return); // commit
        check(scene.plain->property("value").toReal() == 30.0,
              "precondition: the first edit committed 30");

        check(beginPlainEdit(), "precondition: second edit begins on 30");
        typeText(scene.plainEdit, QStringLiteral("12"));
        sendKey(scene.plainEdit, Qt::Key_Escape);
        check(scene.plain->property("value").toReal() == 30.0,
              "F34: Escape restores this edit's start (30), not the session's first value (95)");
    }

    // ---- F34 plain cancel: the settled common case ---------------------------
    // No re-sync happened, so decided behavior and the old keep-the-value behavior
    // agree - pinned here so a future change cannot drift it apart silently.
    {
        makeScene();
        check(scene.found(), "precondition: a fresh scene for the plain-cancel scenario");
        const int changedBefore = scene.owner->property("changedCount").toInt();
        check(beginPlainEdit(), "precondition: the edit begins");
        typeText(scene.plainEdit, QStringLiteral("12"));
        sendKey(scene.plainEdit, Qt::Key_Escape);
        check(scene.plain->property("value").toReal() == 95.0,
              "F34: a cancelled edit leaves the value where the edit found it (95)");
        check(scene.owner->property("external").toReal() == 95.0,
              "F34: and the owner agrees (nothing to re-sync, nothing to report)");
        check(scene.owner->property("changedCount").toInt() == changedBefore,
              "F34: a cancelled edit reports no live change to anyone");
    }

    // ---- F34 sentinel: the word comes back, not a number underneath ----------
    // AeNumberField's "Auto" IS the value -1 (placeholderValue). The accessibility
    // press is the route a probe and a screen reader use to enter the edit; the
    // restore must come back to the sentinel state INCLUDING the word.
    {
        makeScene();
        check(scene.found(), "precondition: a fresh scene for the sentinel scenario");
        check(QMetaObject::invokeMethod(scene.numValueNode, "accessibleBeginEdit"),
              "the edit begins through the accessibility press (the probe route)");
        check(scene.numEdit->property("activeFocus").toBool(),
              "precondition: the number field's editor has focus");
        check(scene.num->property("atPlaceholder").toBool(),
              "precondition: the field reads its placeholder word (Auto) before the edit");

        typeText(scene.numEdit, QStringLiteral("30"));
        // A mid-edit re-sync here too: the sentinel restore must beat it.
        scene.num->setProperty("external", 7.0);
        check(!scene.num->property("atPlaceholder").toBool(),
              "precondition: the re-sync moved the field off the sentinel");

        sendKey(scene.numEdit, Qt::Key_Escape);
        check(scene.num->property("value").toReal() == -1.0,
              "F34: Escape restores the sentinel value (-1), not 7 and not 30");
        check(scene.num->property("atPlaceholder").toBool(),
              "F34: and the field is back on its placeholder WORD (Auto), not a number");
        check(!scene.numEdit->property("activeFocus").toBool(),
              "F34: the number field's edit exits too");
    }

    // ---- the commit path is untouched ----------------------------------------
    // A regression that made every exit a cancel would pass everything above.
    {
        makeScene();
        check(scene.found(), "precondition: a fresh scene for the commit scenario");
        check(beginPlainEdit(), "precondition: the edit begins");
        typeText(scene.plainEdit, QStringLiteral("12"));
        sendKey(scene.plainEdit, Qt::Key_Return);
        check(scene.plain->property("value").toReal() == 12.0,
              "Enter still commits what was typed (12)");
        check(scene.owner->property("external").toReal() == 12.0,
              "and the owner sees the commit");
        check(scene.owner->property("committedCount").toInt() == 1,
              "and it is reported as finalized exactly once");
    }

    // ---- routing fact (supplementary, never the assertion by itself) ----------
    // The control's Escape lives on the editor; it must CONSUME the key - the
    // swallow is what keeps a dialog-level Escape (A2: cancel) from firing under an
    // undone edit.
    {
        makeScene();
        check(scene.found(), "precondition: a fresh scene for the routing fact");
        check(beginPlainEdit(), "precondition: the edit begins");
        check(sendKey(scene.plainEdit, Qt::Key_Escape),
              "the editor consumes Escape (an undone edit must not also cancel the dialog)");
    }

    // ---- the field's precision survives an accessibility write ----------------
    // Measured 2026-09-30 (probe_capped read back 0): accessibleSetValue used to
    // Math.round its input, so the size cap's 0.25 MB landed as 0 — and the scrub
    // drag path rounded the same way. The write must take the typed-edit path AT
    // THE FIELD'S PRECISION (decimals: 2 on this scene's number field).
    {
        makeScene();
        check(scene.found(), "precondition: a fresh scene for the precision check");
        check(QMetaObject::invokeMethod(scene.numValueNode, "accessibleSetValue",
                                        Q_ARG(QVariant, 0.25)),
              "the accessibility write lands through the entry point");
        check(qFuzzyCompare(scene.num->property("value").toReal() + 1.0, 1.25),
              "and the value keeps the field's decimals (0.25, not rounded to 0)");
        check(qFuzzyCompare(scene.num->property("external").toReal() + 1.0, 1.25),
              "and the owner's re-sync carries the same precision");
    }

    if (scene.root) delete scene.root;
    QTextStream(stdout) << (failures ? "=== FAILED " : "=== OK ") << failures << " failures\n";
    return failures ? 1 : 0;
}
