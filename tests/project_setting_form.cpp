// The project-settings dialog, headless: what the model holds and what the scene
// GIVES it.
//
// The dialog used to be a QWidget/QFormLayout built by hand inside
// gui/MainMenuBar.cpp, so its fields could only be driven through a shown widget (a
// window, a platform, a style). It is a Qt Quick scene now, and every fact the form
// shows lives on gui::ProjectSettingViewModel - the same object the scene binds to.
// So both halves of the contract can be asked with no window, no GL context and no
// widget:
//
//   * the MODEL: the clamps a typed/spun value has to obey - a canvas dimension
//     inside 1..maxCanvasSize, a rate of at least 1, and the frame range's
//     start <= end link the widget form kept with setMaximum/setMinimum;
//   * the SCENE: the REAL ProjectSettingDialog.qml compiles, its root is an item,
//     and its fields are wired to the model - driving a field through the control's
//     own edit path (the accessibility node, which takes the SAME route as a typed
//     edit) is what proves the write-back, not the property each field renders.
//
// The edits are deliberate: AeCheckBox's own header records that writing `checked`
// from a binding or directly does NOT fire toggled(), so the form's
// `onToggled: projectSettingModel.loop = checked` only runs on real activation
// (click(), Space, a press on the row); and AeNumberField's aeAccessibleValue node
// exists precisely so an edit that is not a scrub lands in `valueEdited`, which is
// what the form's `onValueEdited` handles.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/project_setting_form
#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVariant>

#include "gui/qml/ProjectSettingViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// Let the scene's bindings/queued work settle before a property is read.
static void spin(int aMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < aMs) QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}

// The checkbox with these words. Addressed by its text plus the fact that it IS a
// checkbox (an invokable click()); a bare "the first CheckBox" would keep passing if
// another one were added above it.
static QObject* checkBoxWithText(QQuickItem* aRoot, const QString& aText) {
    QList<QObject*> all;
    all << aRoot;
    all << aRoot->findChildren<QObject*>();
    for (QObject* o : all) {
        if (o->property("text").toString() != aText) continue;
        if (o->metaObject()->indexOfMethod("click()") >= 0) return o;
    }
    return nullptr;
}

// The AeButton with these words; same addressing rule as above.
static QObject* buttonWithText(QQuickItem* aRoot, const QString& aText) {
    QList<QObject*> all;
    all << aRoot;
    all << aRoot->findChildren<QObject*>();
    for (QObject* o : all) {
        if (!QByteArray(o->metaObject()->className()).contains("AeButton")) continue;
        if (o->property("text").toString() == aText) return o;
    }
    return nullptr;
}

// A numeric field by the words a screen reader addresses it by. The class check keeps
// this from matching the AeFieldLabel beside it (which has no `value` at all).
static QQuickItem* numberFieldWithLabel(QQuickItem* aRoot, const QString& aLabel) {
    QList<QQuickItem*> all;
    all << aRoot;
    all << aRoot->findChildren<QQuickItem*>();
    for (QQuickItem* o : all) {
        if (!QByteArray(o->metaObject()->className()).contains("AeNumberField")) continue;
        if (o->property("label").toString() == aLabel) return o;
    }
    return nullptr;
}

static bool hasText(QQuickItem* aRoot, const QString& aText) {
    const QList<QObject*> all = aRoot->findChildren<QObject*>();
    for (QObject* o : all)
        if (o->property("text").toString() == aText) return true;
    return false;
}

// One click, through the control's own activation, so the toggled()/clicked() the
// form listens for is the one Qt emits.
static bool click(QObject* aButton) { return QMetaObject::invokeMethod(aButton, "click"); }

// The field's own edit path: an accessibility write lands in
// AeNumberField.valueEdited, exactly where a typed edit lands.
static bool editValue(QQuickItem* aField, double aValue) {
    QQuickItem* node = aField ? aField->findChild<QQuickItem*>(QStringLiteral("aeAccessibleValue")) : nullptr;
    return node && QMetaObject::invokeMethod(node, "accessibleSetValue", Q_ARG(QVariant, aValue));
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // The application's style, so the scene builds the same controls it ships.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    gui::ProjectSettingViewModel model;

    // ---- The bare model. The shell always seeds it from the project's own
    // attribute (this dialog only ever edits an existing project), so a model with
    // nothing seeded describes a default project: core::Project::Attribute's own
    // 1..600 range at 24 fps, no loop, and the canvas at the clamp floor.
    check(model.maxCanvasSize() == 2147483647,
          QStringLiteral("the canvas ceiling is OPEN until the shell sets the device's (%1)")
              .arg(model.maxCanvasSize()));
    check(model.canvasWidth() == 1,
          QStringLiteral("an unseeded canvas width sits at the floor (%1)").arg(model.canvasWidth()));
    check(model.canvasHeight() == 1,
          QStringLiteral("and so does the height (%1)").arg(model.canvasHeight()));
    check(model.frameStart() == 1,
          QStringLiteral("the frame range starts at 1 (%1)").arg(model.frameStart()));
    check(model.frameEnd() == 600,
          QStringLiteral("and ends at 600 (%1)").arg(model.frameEnd()));
    check(model.fps() == 24, QStringLiteral("the rate is 24 fps (%1)").arg(model.fps()));
    check(!model.loop(), "loop starts off");

    // ---- The canvas clamps. A spin box can be scrolled below its minimum and a
    // value can arrive from a stale binding, so every dimension is clamped at the
    // model, not only by the control's own range.
    model.setMaxCanvasSize(4096);
    check(model.maxCanvasSize() == 4096, "setMaxCanvasSize(4096) round-trips");
    model.setCanvasWidth(0);
    check(model.canvasWidth() == 1,
          QStringLiteral("setCanvasWidth(0) clamps to 1 (%1)").arg(model.canvasWidth()));
    model.setCanvasHeight(-1);
    check(model.canvasHeight() == 1,
          QStringLiteral("setCanvasHeight(-1) clamps to 1 (%1)").arg(model.canvasHeight()));
    model.setCanvasWidth(100000);
    check(model.canvasWidth() == 4096,
          QStringLiteral("setCanvasWidth(100000) clamps to the device ceiling (%1)")
              .arg(model.canvasWidth()));
    model.setCanvasHeight(99999);
    check(model.canvasHeight() == 4096,
          QStringLiteral("setCanvasHeight(99999) clamps to it too (%1)").arg(model.canvasHeight()));
    model.setCanvasWidth(1280);
    model.setCanvasHeight(720);
    check(model.canvasWidth() == 1280 && model.canvasHeight() == 720,
          "an in-range canvas size is kept unchanged");

    // A ceiling that ARRIVES LATE (the shell sets the device limit before the seed,
    // but a smaller device after a bigger one is the same code path) pulls the pair
    // down with it; raising it again must not invent a size.
    model.setMaxCanvasSize(64);
    check(model.canvasWidth() == 64 && model.canvasHeight() == 64,
          QStringLiteral("lowering the ceiling re-clamps the pair (%1 x %2)")
              .arg(model.canvasWidth())
              .arg(model.canvasHeight()));
    model.setMaxCanvasSize(4096);
    check(model.canvasWidth() == 64 && model.canvasHeight() == 64,
          "raising it back leaves the clamped pair alone (no re-clamp invents a size)");

    // ---- The rate floor: a project below 1 fps would make the playback tick and
    // the exporter divide by zero.
    model.setFps(-5);
    check(model.fps() == 1, QStringLiteral("setFps(-5) clamps to 1 (%1)").arg(model.fps()));
    model.setFps(60);
    check(model.fps() == 60, QStringLiteral("setFps(60) round-trips (%1)").arg(model.fps()));

    // ---- The frame range's link. The widget form kept start <= end with
    // setMaximum/setMinimum on the two spin boxes (and either bound may be negative:
    // pre-roll frames before 0 are a valid project extent).
    model.setFrameEnd(100);
    model.setFrameStart(500);
    check(model.frameStart() == 100,
          QStringLiteral("a start past the end is pulled down to it (%1)").arg(model.frameStart()));
    model.setFrameStart(-50);
    check(model.frameStart() == -50,
          QStringLiteral("a negative start is a normal frame, not an error (%1)").arg(model.frameStart()));
    model.setFrameEnd(-80);
    check(model.frameEnd() == -50,
          QStringLiteral("and an end below the start is pushed back up (%1)").arg(model.frameEnd()));

    // ---- The loop flag round-trips through the setter (the checkbox reads it back).
    model.setLoop(true);
    check(model.loop(), "setLoop(true) round-trips");
    model.setLoop(false);
    check(!model.loop(), "setLoop(false) round-trips");

    // ---- The viewport-background override. The EMPTY string is "follow the theme" -
    // the state every project had before the override existed - and a colour is
    // normalized to lowercase "#rrggbb"; garbage is refused by keeping the value
    // already held, never by inventing one.
    check(model.viewportBackgroundIsThemeDefault(), "an unseeded model follows the theme");
    check(model.viewportBackgroundHex().isEmpty(), "and its hex is empty");
    model.setViewportBackgroundHex(QStringLiteral("#A1B2C3"));
    check(model.viewportBackgroundHex() == QStringLiteral("#a1b2c3"),
          QStringLiteral("a hex is normalized to lowercase #rrggbb (%1)")
              .arg(model.viewportBackgroundHex()));
    check(!model.viewportBackgroundIsThemeDefault(),
          "and a colour means the theme default no longer applies");
    model.setViewportBackgroundHex(QStringLiteral("112233"));
    check(model.viewportBackgroundHex() == QStringLiteral("#112233"),
          QStringLiteral("a bare hex without '#' is accepted (%1)")
              .arg(model.viewportBackgroundHex()));
    model.setViewportBackgroundHex(QStringLiteral("#abc"));
    check(model.viewportBackgroundHex() == QStringLiteral("#aabbcc"),
          QStringLiteral("a three-digit hex expands (%1)").arg(model.viewportBackgroundHex()));
    model.setViewportBackgroundHex(QStringLiteral("not a colour"));
    check(model.viewportBackgroundHex() == QStringLiteral("#aabbcc"),
          "garbage is refused, keeping the previous value");
    model.setViewportBackgroundHex(QStringLiteral("#12345"));
    check(model.viewportBackgroundHex() == QStringLiteral("#aabbcc"),
          "a wrong-length hex is refused too");
    model.setViewportBackgroundHex(QString());
    check(model.viewportBackgroundIsThemeDefault(),
          "the empty string returns to the theme default");

    // ---- The scene. The glyphs are read from a scratch dir the application tints on
    // startup (theme::tintIcons -> setIconDir); doing the same here keeps the log
    // readable instead of one "Cannot open: file:///..." per Image.
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    // The state the shell would have seeded: a device ceiling, a real canvas, a
    // range that starts BEFORE zero (a pre-roll extent) and 24 fps.
    model.setMaxCanvasSize(4096);
    model.setCanvasWidth(1280);
    model.setCanvasHeight(720);
    model.setFrameEnd(600);
    model.setFrameStart(-24);
    model.setFps(24);

    QQmlEngine engine;
    // NOT "model": a Repeater's own `model` property shadows the context property.
    engine.rootContext()->setContextProperty(QStringLiteral("projectSettingModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(AE_QML_DIR "/ProjectSettingDialog.qml")));
    check(component.status() == QQmlComponent::Ready, "the dialog's QML is Ready");
    if (component.isError()) {
        QTextStream(stderr) << "the project-settings dialog's QML does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
    }
    check(!component.isError(), "and it reports no error");

    QObject* created = component.create();
    check(created != nullptr, "the scene builds an object");
    auto* root = qobject_cast<QQuickItem*>(created);
    check(root != nullptr, "and its root is an Item the scene can be searched in");
    if (!root) return 1;
    spin(60);

    // ---- The rows the widget form had, by their words.
    check(hasText(root, QStringLiteral("Canvas size")), "the Canvas size row is labelled");
    check(hasText(root, QStringLiteral("Frame range")), "the Frame range row is labelled");
    check(hasText(root, QStringLiteral("Frames per second")),
          "the Frames per second row is labelled");

    // ---- The frame-range fields' bounds follow the partner, which is the widget
    // form's setMaximum/setMinimum stated in the scene (an accessibility client reads
    // the same bound through aeAccessibleValue).
    QQuickItem* startField = numberFieldWithLabel(root, QStringLiteral("Frame start"));
    QQuickItem* endField = numberFieldWithLabel(root, QStringLiteral("Frame end"));
    check(startField != nullptr, "the Frame start field is in the scene");
    check(endField != nullptr, "the Frame end field is in the scene");
    if (startField && endField) {
        check(startField->property("value").toInt() == -24,
              QStringLiteral("Frame start shows the seeded value (%1)")
                  .arg(startField->property("value").toInt()));
        check(endField->property("value").toInt() == 600,
              QStringLiteral("Frame end shows the seeded value (%1)")
                  .arg(endField->property("value").toInt()));
        check(startField->property("to").toInt() == 600,
              QStringLiteral("Frame start stops at Frame end (%1)")
                  .arg(startField->property("to").toInt()));
        check(endField->property("from").toInt() == -24,
              QStringLiteral("and Frame end starts at Frame start (%1)")
                  .arg(endField->property("from").toInt()));

        // An edit that would invert is clamped by the MODEL through the field's own
        // write path, so the pair can never end up start > end.
        check(editValue(startField, 5000), "the Frame start field's own edit path ran");
        spin(30);
        check(model.frameStart() == 600,
              QStringLiteral("an edit past Frame end lands clamped on the model (%1)")
                  .arg(model.frameStart()));
        check(startField->property("value").toInt() == 600,
              QStringLiteral("and the field follows the clamped value back (%1)")
                  .arg(startField->property("value").toInt()));
        check(endField->property("from").toInt() == 600,
              "Frame end's own floor followed the edit too (the pair stays consistent)");
    }

    // ---- The canvas pair: two fields on one row, capped at the ceiling the shell
    // read from GL, with the model enforcing the same cap.
    QQuickItem* widthField = numberFieldWithLabel(root, QStringLiteral("Canvas width"));
    QQuickItem* heightField = numberFieldWithLabel(root, QStringLiteral("Canvas height"));
    check(widthField != nullptr, "the Canvas width field is in the scene");
    check(heightField != nullptr, "the Canvas height field is in the scene");
    if (widthField && heightField) {
        check(widthField->property("value").toInt() == 1280,
              QStringLiteral("Canvas width shows the seeded value (%1)")
                  .arg(widthField->property("value").toInt()));
        check(widthField->property("from").toInt() == 1 && heightField->property("from").toInt() == 1,
              "both canvas fields floor at 1");
        check(widthField->property("to").toInt() == 4096
                  && heightField->property("to").toInt() == 4096,
              QStringLiteral("and both stop at the device ceiling (%1)")
                  .arg(widthField->property("to").toInt()));
        check(editValue(widthField, 99999), "the Canvas width field's own edit path ran");
        spin(30);
        check(model.canvasWidth() == 4096,
              QStringLiteral("an edit past the ceiling lands clamped on the model (%1)")
                  .arg(model.canvasWidth()));
        check(widthField->property("value").toInt() == 4096,
              QStringLiteral("and the field follows the clamped value back (%1)")
                  .arg(widthField->property("value").toInt()));
    }

    // ---- The rate field: same floor in the scene and in the model.
    QQuickItem* fpsField = numberFieldWithLabel(root, QStringLiteral("Frames per second"));
    check(fpsField != nullptr, "the Frames per second field is in the scene");
    if (fpsField) {
        check(fpsField->property("from").toInt() == 1, "the rate field floors at 1 frame per second");
        check(editValue(fpsField, -5), "the rate field's own edit path ran");
        spin(30);
        check(model.fps() == 1, QStringLiteral("a rate below 1 lands clamped on the model (%1)")
                                    .arg(model.fps()));
        check(fpsField->property("value").toInt() == 1,
              QStringLiteral("and the field follows the clamped value back (%1)")
                  .arg(fpsField->property("value").toInt()));
    }

    // ---- The loop wiring. Found by its words, driven by a real click, and observed
    // on the MODEL - which is the object the form's `onToggled` writes and the shell
    // reads back through loop().
    QObject* loopBox = checkBoxWithText(root, QStringLiteral("Loop animation"));
    check(loopBox != nullptr, "the Loop animation checkbox is in the scene");
    if (loopBox) {
        check(!model.loop(), "precondition: it opens unticked");
        check(click(loopBox), "the checkbox accepts a click");
        spin(30);
        check(model.loop(), "clicking Loop animation turns the model's loop on");
        check(click(loopBox), "the checkbox accepts a second click");
        spin(30);
        check(!model.loop(), "and clicking it again turns it back off");
    }

    // ---- The viewport-background row. The swatch paints the colour in force - the
    // project's override when set, the theme's recessed canvas token (what
    // GUIResources::viewportBackground() returns) when not. Choose… asks the shell for
    // the chooser the scene cannot open; the box puts the override back on the theme.
    QQuickItem* swatch =
        root->findChild<QQuickItem*>(QStringLiteral("viewportBackgroundSwatch"));
    check(swatch != nullptr, "the viewport-background swatch is in the scene");
    if (swatch) {
        check(swatch->property("color").value<QColor>().name() == theme.recessed().name(),
              QStringLiteral("with no override the swatch shows the theme's recessed colour (%1)")
                  .arg(swatch->property("color").value<QColor>().name()));
        model.setViewportBackgroundHex(QStringLiteral("#123456"));
        spin(30);
        check(swatch->property("color").value<QColor>().name() == QStringLiteral("#123456"),
              QStringLiteral("and it follows the override when one is set (%1)")
                  .arg(swatch->property("color").value<QColor>().name()));
        model.setViewportBackgroundHex(QString());
        spin(30);
        check(swatch->property("color").value<QColor>().name() == theme.recessed().name(),
              "clearing it puts the swatch back on the theme colour");
    }

    int backgroundPicks = 0;
    QObject::connect(&model, &gui::ProjectSettingViewModel::viewportBackgroundPickRequested,
                     [&backgroundPicks]() { ++backgroundPicks; });
    QObject* chooseBackground = buttonWithText(root, QStringLiteral("Choose…"));
    check(chooseBackground != nullptr, "the viewport-background Choose… button is in the scene");
    if (chooseBackground) {
        check(click(chooseBackground), "Choose… accepts a click");
        check(backgroundPicks == 1, "and it asks the shell for its colour chooser");
    }

    QObject* useThemeBox = checkBoxWithText(root, QStringLiteral("Use theme background"));
    check(useThemeBox != nullptr, "the Use theme background box is in the scene");
    if (useThemeBox) {
        model.setViewportBackgroundHex(QStringLiteral("#abcdef"));
        spin(30);
        check(!useThemeBox->property("checked").toBool(),
              "a project with an override opens the box unticked");
        check(click(useThemeBox), "the box accepts a click");
        spin(30);
        check(model.viewportBackgroundIsThemeDefault(),
              "ticking it returns the project to the theme default");
        check(useThemeBox->property("checked").toBool(),
              "and the box re-reads the model as ticked");
    }

    // ---- The footer: OK and Cancel ask the model, which is what the shell turns into
    // the dialog result (through confirmFrameRangeUpdating() for OK).
    int accepted = 0;
    int cancelled = 0;
    QObject::connect(&model, &gui::ProjectSettingViewModel::acceptRequested,
                     [&accepted]() { ++accepted; });
    QObject::connect(&model, &gui::ProjectSettingViewModel::cancelRequested,
                     [&cancelled]() { ++cancelled; });
    QObject* ok = buttonWithText(root, QStringLiteral("OK"));
    QObject* cancel = buttonWithText(root, QStringLiteral("Cancel"));
    check(ok != nullptr, "the footer's OK button is in the scene");
    check(cancel != nullptr, "the footer's Cancel button is in the scene");
    if (ok && cancel) {
        check(click(ok), "OK accepts a click");
        check(accepted == 1, "and OK asks the model to accept (the shell's accept route)");
        check(click(cancel), "Cancel accepts a click");
        check(cancelled == 1, "and Cancel asks the model to cancel (the shell's reject route)");
    }

    if (failures == 0) {
        QTextStream(stdout) << "\nall project-settings form checks passed\n";
        return 0;
    }
    QTextStream(stdout) << "\n" << failures << " check(s) FAILED\n";
    return 1;
}
