// The new-project dialog, headless: what the model holds and what the scene GIVES it.
//
// The dialog used to be a QWidget/QFormLayout filled by hand, so its fields could only
// be driven through a shown widget (a window, a platform, a style). It is a Qt Quick
// scene now, and every fact the form shows lives on gui::NewProjectViewModel - the same
// object the scene binds to. So both halves of the contract can be asked with no
// window, no GL context and no widget:
//
//   * the MODEL: the defaults a new project starts from, the clamps a typed/spun value
//     has to obey, and the two derived facts (the basename label, the live-link flag);
//   * the SCENE: the REAL NewProjectDialog.qml compiles, its root is an item, and the
//     "Live link" checkbox is actually wired to the model - clicking it is what proves
//     the binding, not the property it renders.
//
// The click is deliberate: AeCheckBox is a Qt Quick CheckBox and its own header records
// that writing `checked` from a binding or directly does NOT fire toggled(), so the
// form's `onToggled: newProjectModel.liveLink = checked` only runs on real activation
// (click(), Space, a press on the row). A test that poked `checked` would pass while
// the wiring was missing entirely.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/new_project_form
#include <QApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>

#include "gui/qml/NewProjectViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// The checkbox labelled with `aText`, addressed the way the scene builds it: by its
// words plus the fact that it IS a checkbox (an invokable click()). A bare "the first
// CheckBox" would keep passing if another one were added above it.
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

// The exact words the design fixes for the live-link row's muted helper line.
static bool hasText(QQuickItem* aRoot, const QString& aText) {
    const QList<QObject*> all = aRoot->findChildren<QObject*>();
    for (QObject* o : all)
        if (o->property("text").toString() == aText) return true;
    return false;
}

// One click, through the control's own activation, so the toggled() the form listens
// for is the one Qt emits - see the header note.
static bool click(QObject* aButton) {
    return QMetaObject::invokeMethod(aButton, "click");
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // The application's style, so the scene builds the same controls it ships.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    gui::NewProjectViewModel model;

    // ---- The defaults a brand-new project starts from. 600 frames at 24 fps and no
    // canvas size of its own is what the widget form opened with, and it is what the
    // scene's first paint must show.
    check(model.fileName().isEmpty(), "the dialog opens with no asset chosen");
    check(model.fileNameLabel().isEmpty(), "and so there is no asset name to show");
    check(model.frameCount() == 600,
          QStringLiteral("the frame count starts at 600 (%1)").arg(model.frameCount()));
    check(model.fps() == 24, QStringLiteral("the frame rate starts at 24 (%1)").arg(model.fps()));
    check(model.canvasWidth() == 512,
          QStringLiteral("the canvas width starts at 512 (%1)").arg(model.canvasWidth()));
    check(model.canvasHeight() == 512,
          QStringLiteral("the canvas height starts at 512 (%1)").arg(model.canvasHeight()));
    check(!model.specifiesCanvasSize(),
          "canvas size is UNSPECIFIED until the box is ticked (the spins stay disabled)");
    check(!model.liveLink(), "live link starts off, so no watcher is armed");

    // ---- The clamps. A spin box can be scrolled below its minimum and a value can
    // arrive from a stale binding, so every count is clamped at the model, not only by
    // the control's range.
    model.setFrameCount(0);
    check(model.frameCount() == 1,
          QStringLiteral("setFrameCount(0) clamps to 1 (%1)").arg(model.frameCount()));
    model.setFps(-5);
    check(model.fps() == 1, QStringLiteral("setFps(-5) clamps to 1 (%1)").arg(model.fps()));
    model.setCanvasWidth(0);
    check(model.canvasWidth() == 1,
          QStringLiteral("setCanvasWidth(0) clamps to 1 (%1)").arg(model.canvasWidth()));
    model.setCanvasHeight(-1);
    check(model.canvasHeight() == 1,
          QStringLiteral("setCanvasHeight(-1) clamps to 1 (%1)").arg(model.canvasHeight()));

    // ---- The asset: the model keeps the whole path, and the label is the BASENAME
    // the read-only line shows - a full path there would be a different string a probe
    // could mistake for the same one.
    const QString assetPath = QStringLiteral("/home/someone/drawings/sketch.psd");
    model.setFileName(assetPath);
    check(model.fileName() == assetPath, "setFileName keeps the path it was given");
    check(model.fileNameLabel() == QLatin1String("sketch.psd"),
          QStringLiteral("and fileNameLabel is the basename (%1)").arg(model.fileNameLabel()));
    model.setFileName(QString());
    check(model.fileNameLabel().isEmpty(), "clearing the asset clears the label with it");

    // ---- Live link round-trips through the setter (the checkbox reads it back).
    model.setLiveLink(true);
    check(model.liveLink(), "setLiveLink(true) round-trips");
    model.setLiveLink(false);
    check(!model.liveLink(), "setLiveLink(false) round-trips");

    // ---- The scene. The glyphs are read from a scratch dir the application tints on
    // startup (theme::tintIcons -> setIconDir); doing the same here keeps the log
    // readable instead of one "Cannot open: file:///..." per Image.
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    QQmlEngine engine;
    // NOT "model": a Repeater's own `model` property shadows the context property.
    engine.rootContext()->setContextProperty(QStringLiteral("newProjectModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(AE_QML_DIR "/NewProjectDialog.qml")));
    check(component.status() == QQmlComponent::Ready, "the dialog's QML is Ready");
    if (component.isError()) {
        QTextStream(stderr) << "the new-project dialog's QML does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
    }
    check(!component.isError(), "and it reports no error");

    QObject* created = component.create();
    check(created != nullptr, "the scene builds an object");
    auto* root = qobject_cast<QQuickItem*>(created);
    check(root != nullptr, "and its root is an Item the scene can be searched in");
    if (!root) return 1;

    // ---- The Live-link wiring. Found by its words, driven by a real click, and
    // observed on the MODEL - which is the object the form's `onToggled` writes and
    // the shell reads back through liveLink().
    // An asset is chosen first: the row may be gated on there being something to
    // watch, and a click on a disabled control would prove nothing either way.
    model.setFileName(QStringLiteral("/tmp/probe/sketch.psd"));
    QObject* live = checkBoxWithText(root, QStringLiteral("Live link"));
    check(live != nullptr, "the Live link checkbox is in the scene");
    if (live) {
        check(!model.liveLink(), "precondition: it opens unticked");
        check(click(live), "the checkbox accepts a click");
        check(model.liveLink(), "clicking Live link turns the model's liveLink on");
        check(click(live), "the checkbox accepts a second click");
        check(!model.liveLink(), "and clicking it again turns it back off");
    }
    check(hasText(root, QStringLiteral("Reload when the asset file changes on disk")),
          "the live-link row carries its helper sentence verbatim");

    if (failures == 0) {
        QTextStream(stdout) << "\nall new-project form checks passed\n";
        return 0;
    }
    QTextStream(stdout) << "\n" << failures << " check(s) FAILED\n";
    return 1;
}
