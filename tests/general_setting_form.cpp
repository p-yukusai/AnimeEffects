// The settings dialog, headless: what the model holds and what the scene GIVES it.
//
// The dialog used to be a QTabWidget/QFormLayout filled by hand, and MainMenuBar read
// its *HasChanged() answers by comparing a saved "before" value against a control, so
// nothing about it could be asked without a shown widget. It is a Qt Quick scene now,
// and both halves of the contract can be driven with no window, no GL context and no
// widget:
//
//   * the MODEL: the values the form opens on when no setting is present, the clamps
//     a typed/spun number has to obey, and the "before vs now" predicates the shell's
//     public API forwards (every *HasChanged() must read false on open and true once
//     its value moves - that is the whole point of the port);
//   * the SCENE: the REAL GeneralSettingDialog.qml compiles, its root is an item, and
//     its controls are actually wired to the model. A checkbox is driven by its own
//     click() (AeCheckBox records that writing `checked` from a binding does NOT fire
//     toggled(), so a click is the only honest driver - the new-project test's rule),
//     the tab buttons write currentTab, and OK/Cancel/the reset reach the model's own
//     verbs.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/general_setting_form
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
#include <QVariantList>
#include <QVariantMap>

#include "gui/qml/GeneralSettingViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

static int failures = 0;

static void check(bool ok, const QString& what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

// The control labelled with `aText` that is actually a control (an invokable
// click()): a bare "the first CheckBox" would keep passing if another one were added
// above it.
//
// The walk covers the VISUAL tree as well as the QObject tree, and that is
// load-bearing: a Repeater's delegates are parented into the visual tree (the Row that
// lays them out) but NOT under the root's QObject children, so a
// `findChildren<QObject*>()`-only search saw the tab strip, the theme group and the
// accent swatches as absent while the application rendered them perfectly. Measured:
// the tab Repeater reported count=4 and a QObject-only walk returned none of them.
static void collectItems(QQuickItem* aItem, QList<QObject*>& aOut) {
    aOut << aItem;
    const QList<QQuickItem*> children = aItem->childItems();
    for (QQuickItem* child : children)
        collectItems(child, aOut);
}

static QObject* controlWithText(QQuickItem* aRoot, const QString& aText) {
    QList<QObject*> all;
    collectItems(aRoot, all);
    all << aRoot->findChildren<QObject*>();
    for (QObject* o : all) {
        if (o->property("text").toString() != aText) continue;
        if (o->metaObject()->indexOfMethod("click()") >= 0) return o;
    }
    return nullptr;
}

// One click, through the control's own activation, so the toggled()/clicked() the
// form listens for is the one Qt emits.
static bool click(QObject* aButton) { return QMetaObject::invokeMethod(aButton, "click"); }

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // The application's style, so the scene builds the same controls it ships.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    gui::GeneralSettingViewModel model;
    // The values the shell hands over when no setting is present; seeding them must
    // install BOTH the "before" and the "now" side, so nothing reads as changed.
    gui::GeneralSettingViewModel::Init init;
    model.seed(init);

    // ---- The values the dialog opens on when the settings file says nothing. Every
    // one of these is the widget form's own fallback (GeneralSettingDialog.cpp's read
    // block), so the two forms open identically on a fresh profile.
    check(model.languageIndex() == 0, QStringLiteral("language defaults to 0 (Auto) (%1)").arg(model.languageIndex()));
    // util::Easing::Type_Linear's display index (Floor, Round, Linear, ...).
    check(model.easingIndex() == 2, QStringLiteral("easing defaults to Linear (%1)").arg(model.easingIndex()));
    check(model.rangeIndex() == 2, QStringLiteral("range defaults to 2 (All) (%1)").arg(model.rangeIndex()));
    check(model.timeFormatIndex() == 0, QStringLiteral("timeline format defaults to 0 (Frame number) (%1)").arg(model.timeFormatIndex()));
    check(model.themeKey() == QLatin1String("dark"), QStringLiteral("theme defaults to dark (%1)").arg(model.themeKey()));
    check(model.accentIndex() == 6, QStringLiteral("accent defaults to violet (%1)").arg(model.accentIndex()));
    check(model.donationAllowed(), "the donation menu is allowed by default");
    check(model.autoSave(), "autosave is ON by default");
    check(model.autoSaveDelayMinutes() == 5, QStringLiteral("autosave runs every 5 minutes (%1)").arg(model.autoSaveDelayMinutes()));
    check(!model.autoShowMesh(), "the FFD mesh is not shown by default");
    check(!model.autoCbCopy(), "the copy-to-clipboard box starts unticked");
    check(model.resIDCheck(), "the resource-id check is ON by default");
    check(!model.ignoreWarnings(), "export warnings are shown by default");
    check(!model.forceSolverLoad(), "projects are not force-loaded by default");
    check(model.keyDelayMs() == 125, QStringLiteral("the keybind delay is 125 ms (%1)").arg(model.keyDelayMs()));
    check(!model.invertMainViewScaling(), "canvas scrolling is not inverted by default");
    check(!model.invertTimeLineScaling(), "timeline scrolling is not inverted by default");
    check(model.middleMouseMoveCanvas(), "the middle mouse moves the canvas by default");
    check(model.currentTab() == 0, "the form opens on the General tab");

    // ---- Nothing reads as changed right after seeding: that is what makes OK-close
    // a no-op write on a fresh profile, and what the shell's save-gating relies on.
    check(!model.languageHasChanged() && !model.easingHasChanged() && !model.rangeHasChanged() &&
              !model.timeFormatHasChanged() && !model.themeHasChanged() && !model.accentHasChanged() &&
              !model.donationHasChanged() && !model.autoSaveHasChanged() && !model.autoSaveDelayHasChanged() &&
              !model.resIDCheckHasChanged() && !model.cbCopyHasChanged() && !model.forceSolverLoadHasChanged() &&
              !model.ignoreWarningsHasChanged() && !model.keyDelayHasChanged() && !model.mouseHasChanged(),
          "no predicate reads changed immediately after seeding");

    // ---- Every predicate flips when ITS value moves (and the shell's public
    // *HasChanged() methods are one-line forwards of exactly these).
    model.setLanguageIndex(3);
    check(model.languageHasChanged(), "languageHasChanged() follows languageIndex");
    model.setEasingIndex(5);
    check(model.easingHasChanged(), "easingHasChanged() follows easingIndex");
    model.setRangeIndex(0);
    check(model.rangeHasChanged(), "rangeHasChanged() follows rangeIndex");
    model.setTimeFormatIndex(4);
    check(model.timeFormatHasChanged(), "timeFormatHasChanged() follows timeFormatIndex");
    model.setThemeKey(QStringLiteral("light"));
    check(model.themeHasChanged(), "themeHasChanged() follows themeKey");
    model.setAccentIndex(0);
    check(model.accentHasChanged(), "accentHasChanged() follows accentIndex");
    model.setDonationAllowed(false);
    check(model.donationHasChanged(), "donationHasChanged() follows donationAllowed");
    model.setAutoSave(false);
    check(model.autoSaveHasChanged(), "autoSaveHasChanged() follows autoSave");
    model.setAutoSaveDelayMinutes(9);
    check(model.autoSaveDelayHasChanged(), "autoSaveDelayHasChanged() follows autoSaveDelayMinutes");
    model.setResIDCheck(false);
    check(model.resIDCheckHasChanged(), "resIDCheckHasChanged() follows resIDCheck");
    model.setAutoCbCopy(true);
    check(model.cbCopyHasChanged(), "cbCopyHasChanged() follows autoCbCopy");
    model.setForceSolverLoad(true);
    check(model.forceSolverLoadHasChanged(), "forceSolverLoadHasChanged() follows forceSolverLoad");
    model.setIgnoreWarnings(true);
    check(model.ignoreWarningsHasChanged(), "ignoreWarningsHasChanged() follows ignoreWarnings");
    model.setKeyDelayMs(200);
    check(model.keyDelayHasChanged(), "keyDelayHasChanged() follows keyDelayMs");
    model.setMiddleMouseMoveCanvas(false);
    check(model.mouseHasChanged(), "mouseHasChanged() follows the three mouse flags");
    // ...and a mouse flag set back to its seeded value still reads changed, because
    // the other two differ - the predicate is the whole triple, exactly as the
    // widget's MouseSetting comparison was.
    model.setInvertMainViewScaling(true);
    check(model.mouseHasChanged(), "mouseHasChanged() is true while any one flag differs");

    // The option lists the scene repeats over and the combos bind.
    check(model.languageOptions().size() == 7, QStringLiteral("seven languages (%1)").arg(model.languageOptions().size()));
    // util::Easing::displayCount() - 1: the display order minus Type_Custom.
    check(model.easingOptions().size() == 13, QStringLiteral("thirteen easings (%1)").arg(model.easingOptions().size()));
    check(model.rangeOptions().size() == 3, QStringLiteral("three ranges (%1)").arg(model.rangeOptions().size()));
    check(model.timeFormatOptions().size() == 5, QStringLiteral("five timeline formats (%1)").arg(model.timeFormatOptions().size()));
    check(!model.languageOptions().first().toMap().value(QStringLiteral("label")).toString().isEmpty(),
          "an option carries its label");

    // ---- The two floors the widget's QSpinBoxes held are enforced at the model, so
    // every writer (a scrub, a typed edit, a stale binding) obeys them.
    model.setAutoSaveDelayMinutes(0);
    check(model.autoSaveDelayMinutes() == 1,
          QStringLiteral("setAutoSaveDelayMinutes(0) clamps to 1 (%1)").arg(model.autoSaveDelayMinutes()));
    model.setKeyDelayMs(-5);
    check(model.keyDelayMs() == 0, QStringLiteral("setKeyDelayMs(-5) clamps to 0 (%1)").arg(model.keyDelayMs()));

    // ---- Re-seeding clears every predicate again (the shell seeds once, on open).
    model.seed(init);
    check(!model.languageHasChanged() && !model.easingHasChanged() && !model.rangeHasChanged() &&
              !model.timeFormatHasChanged() && !model.themeHasChanged() && !model.accentHasChanged() &&
              !model.donationHasChanged() && !model.autoSaveHasChanged() && !model.autoSaveDelayHasChanged() &&
              !model.resIDCheckHasChanged() && !model.cbCopyHasChanged() && !model.forceSolverLoadHasChanged() &&
              !model.ignoreWarningsHasChanged() && !model.keyDelayHasChanged() && !model.mouseHasChanged(),
          "re-seeding clears every predicate");
    check(model.keyDelayMs() == 125, "and restores the seeded values too");

    // The verbs carry the outcome the shell turns into the dialog result.
    bool accepted = false;
    bool cancelled = false;
    bool resetRequested = false;
    QObject::connect(&model, &gui::GeneralSettingViewModel::acceptRequested, [&]() { accepted = true; });
    QObject::connect(&model, &gui::GeneralSettingViewModel::cancelRequested, [&]() { cancelled = true; });
    QObject::connect(&model, &gui::GeneralSettingViewModel::resetRecentFilesRequested, [&]() { resetRequested = true; });

    // ---- The scene. The glyphs are read from a scratch dir the application tints on
    // startup (theme::tintIcons -> setIconDir); doing the same here keeps the log
    // readable instead of one "Cannot open: file:///..." per Image.
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    // The two lists the shell seeds; a representative set is enough to render the
    // groups.
    {
        QVariantList themes;
        const char* const ids[] = {"system", "light", "dark"};
        for (const char* id : ids) {
            QVariantMap map;
            map.insert(QStringLiteral("value"), QString::fromLatin1(id));
            map.insert(QStringLiteral("label"), QString::fromLatin1(id));
            themes.append(map);
        }
        model.setThemeOptions(themes);

        QVariantList accents;
        for (int i = 0; i < 8; ++i) {
            QVariantMap map;
            map.insert(QStringLiteral("value"), i);
            map.insert(QStringLiteral("swatch"), QStringLiteral("#7c3aed"));
            accents.append(map);
        }
        model.setAccentOptions(accents);
    }

    QQmlEngine engine;
    // NOT "model": a Repeater's own `model` property shadows the context property.
    engine.rootContext()->setContextProperty(QStringLiteral("generalSettingModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);

    QQmlComponent component(
        &engine, QUrl::fromLocalFile(QStringLiteral(AE_QML_DIR "/GeneralSettingDialog.qml")));
    check(component.status() == QQmlComponent::Ready, "the dialog's QML is Ready");
    if (component.isError()) {
        QTextStream(stderr) << "the settings dialog's QML does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
    }
    check(!component.isError(), "and it reports no error");

    QObject* created = component.create();
    check(created != nullptr, "the scene builds an object");
    auto* root = qobject_cast<QQuickItem*>(created);
    check(root != nullptr, "and its root is an Item the scene can be searched in");
    if (!root) return 1;

    // ---- A checkbox is wired to the model: found by its words, driven by a real
    // click, observed on the MODEL. autoShowMesh is the one that saves the moment it
    // is clicked, so a click must be visible on the model with no OK involved.
    QObject* mesh = controlWithText(root, QStringLiteral("Show mesh when selecting FFD"));
    check(mesh != nullptr, "the FFD-mesh checkbox is in the scene");
    if (mesh) {
        check(!model.autoShowMesh(), "precondition: it opens unticked");
        check(click(mesh), "the checkbox accepts a click");
        check(model.autoShowMesh(), "clicking it turns the model's autoShowMesh on");
        check(click(mesh), "the checkbox accepts a second click");
        check(!model.autoShowMesh(), "and clicking it again turns it back off");
    }

    // ---- The tab strip writes currentTab, and the pages follow it (bound visible).
    QObject* qolTab = controlWithText(root, QStringLiteral("QoL"));
    check(qolTab != nullptr, "the QoL tab button is in the scene");
    if (qolTab) {
        check(model.currentTab() == 0, "precondition: the General tab is current");
        check(click(qolTab), "the tab accepts a click");
        check(model.currentTab() == 1, "clicking QoL writes currentTab");
        QObject* generalPage = root->findChild<QObject*>(QStringLiteral("pageGeneral"));
        QObject* qolPage = root->findChild<QObject*>(QStringLiteral("pageQol"));
        check(generalPage && !generalPage->property("visible").toBool(),
              "the General page hides when QoL becomes current");
        check(qolPage && qolPage->property("visible").toBool(),
              "and the QoL page shows");
    }

    // ---- The reset button reaches the model's own verb (the shell answers it with a
    // QSettings remove and the app's info popup).
    QObject* reset = controlWithText(root, QStringLiteral("Reset recent files list"));
    check(reset != nullptr, "the reset-recents button is in the scene");
    if (reset) {
        check(click(reset), "the reset button accepts a click");
        check(resetRequested, "and clicking it raises resetRecentFilesRequested");
    }

    // ---- The footer's two buttons reach the model's outcome verbs.
    QObject* ok = controlWithText(root, QStringLiteral("OK"));
    check(ok != nullptr, "the OK button is in the scene");
    if (ok) {
        check(click(ok), "the OK button accepts a click");
        check(accepted, "and clicking it raises acceptRequested");
    }
    QObject* cancel = controlWithText(root, QStringLiteral("Cancel"));
    check(cancel != nullptr, "the Cancel button is in the scene");
    if (cancel) {
        check(click(cancel), "the Cancel button accepts a click");
        check(cancelled, "and clicking it raises cancelRequested");
    }

    if (failures == 0) {
        QTextStream(stdout) << "\nall checks passed\n";
        return 0;
    }
    QTextStream(stdout) << "\n" << failures << " check(s) FAILED\n";
    return 1;
}
