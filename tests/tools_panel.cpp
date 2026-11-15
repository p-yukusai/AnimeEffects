// The Tools dock's SCENE: what it builds, what it shows and what it wires.
//
// tests/tool_view_model.cpp pins the model's decisions; this gate pins the things the
// model cannot see, and each one is a defect the widget dock had:
//
//   * EVERY icon is a NAMED, reachable control. The widget toolbox's buttons were
//     icon-only with a tooltip, so AT-SPI answered with the GROUP BOX's name for all
//     seven - a screen reader could not tell them apart and no probe could address one;
//   * the seven tools fit on ONE row. The widget dock was 172px wide and wrapped the
//     seventh (the mask brush) onto a line of its own;
//   * a MODE row is icons and the heading says which mode is up - the measured complaint
//     was that four unlabeled glyphs were the only way to tell which mask the brush
//     writes;
//   * the mask's target and channel are COMBOS showing their current value, and the
//     channel row disappears for the layer's own matte;
//   * the brush's numeric fields are real spin buttons wired to the model (the widget
//     showed a read-only "Hardness:  50");
//   * the whole panel SCROLLS: the widget dock had no scroll bar anywhere and clipped a
//     tall panel.
//
// EVERY CONTROL IS ADDRESSED THROUGH QAccessible, not through objectNames: that is the
// same route a screen reader and scripts/gui/ae_ui.py take, so a control this test can
// press is a control a user without a mouse can press. A name that only exists in the
// QML source proves nothing - the first port of the inspector shipped a hidden label
// that way.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/tools_panel
#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QAccessibleValueInterface>
#include <QApplication>
#include <QFont>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "gui/MainViewSetting.h"
#include "gui/qml/QmlFormAccessibility.h"
#include "gui/qml/QmlTheme.h"
#include "gui/qml/ToolViewModel.h"
#include "gui/theme/Icons.h"

namespace {

int failures = 0;

void check(bool aOk, const QString& aWhat) {
    QTextStream(stdout) << (aOk ? "PASS  " : "FAIL  ") << aWhat << "\n";
    if (!aOk)
        ++failures;
}

// Every item under a root, delegates included: a Repeater's delegates are parented into
// the VISUAL tree, so QObject::findChildren misses them (the measured trap AGENTS.md
// records for the settings form).
void collectItems(QQuickItem* aItem, QList<QQuickItem*>& aOut) {
    aOut << aItem;
    for (QQuickItem* child : aItem->childItems())
        collectItems(child, aOut);
}

QAccessibleInterface* accessible(QQuickItem* aItem) {
    return aItem ? QAccessible::queryAccessibleInterface(aItem) : nullptr;
}

// The items a screen reader (and the probes) can find under aRoot by NAME and ROLE.
// Role as well as name because a panel heading and its tool button legitimately share a
// word ("Transform"), and because a spin field publishes both a StaticText label and the
// SpinBox itself.
QList<QQuickItem*> controls(QQuickItem* aRoot, const QString& aName, QAccessible::Role aRole) {
    QList<QQuickItem*> out;
    QList<QQuickItem*> all;
    collectItems(aRoot, all);
    for (QQuickItem* item : all) {
        QAccessibleInterface* iface = accessible(item);
        if (!iface || iface->role() != aRole)
            continue;
        if (iface->text(QAccessible::Name) != aName)
            continue;
        out << item;
    }
    return out;
}

QQuickItem* control(QQuickItem* aRoot, const QString& aName, QAccessible::Role aRole) {
    const QList<QQuickItem*> found = controls(aRoot, aName, aRole);
    return found.isEmpty() ? nullptr : found.first();
}

// The control that is ON SCREEN. A QML item that is `visible: false` stays in the tree and
// keeps its accessible node, so "the node exists" says nothing about what the panel
// offers: the chooser this gate asserts is GONE is a hidden item that still answers.
QQuickItem* visibleControl(QQuickItem* aRoot, const QString& aName, QAccessible::Role aRole) {
    for (QQuickItem* item : controls(aRoot, aName, aRole)) {
        if (item->isVisible())
            return item;
    }
    return nullptr;
}

// A press through the accessibility action, i.e. what a screen reader does (and what
// scripts/gui/ae_ui.py's `click` does through AT-SPI).
bool press(QQuickItem* aItem) {
    QAccessibleInterface* iface = accessible(aItem);
    if (!iface || !iface->actionInterface())
        return false;
    iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    return true;
}

// A value write through the accessibility value protocol.
bool setValue(QQuickItem* aItem, double aValue) {
    QAccessibleInterface* iface = accessible(aItem);
    QAccessibleValueInterface* value = iface ? iface->valueInterface() : nullptr;
    if (!value)
        return false;
    value->setCurrentValue(aValue);
    return true;
}

QString describeRect(QQuickItem* aItem, QQuickItem* aFrame) {
    const QPointF p = aItem->mapToItem(aFrame, 0, 0);
    return QStringLiteral("x=%1 y=%2 w=%3 h=%4").arg(p.x()).arg(p.y()).arg(aItem->width()).arg(aItem->height());
}

qreal topOf(QQuickItem* aItem, QQuickItem* aFrame) { return aItem->mapToItem(aFrame, 0, 0).y(); }

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    // THE APPLICATION'S OWN ACCESSIBILITY WIRING. Without it a QML numeric field has no
    // Value interface (Qt Quick's `Accessible` attached type has no value property), so a
    // write through QAccessible lands nowhere - which is exactly the regression
    // gui/QmlFormAccessibility was written for. A scene test that skipped it would be
    // testing a dock the application never builds.
    gui::installQmlFormAccessibility();

    // The application's own palette and tinted glyphs: a scene that cannot find its
    // icons renders empty buttons and would pass a structural test while looking broken.
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    gui::MainViewSetting view;
    gui::ToolViewModel model;
    model.setViewSetting(&view);
    model.setDriverActive(true);
    gui::ToolKeyHints hints;
    hints.rotateCounterClockwise = QStringLiteral("Alt+Q");
    hints.resetRotation = QStringLiteral("F1");
    hints.rotateClockwise = QStringLiteral("Alt+E");
    hints.brushSizeDecrease = QStringLiteral("[");
    hints.brushSizeIncrease = QStringLiteral("]");
    hints.brushHardnessDecrease = QStringLiteral("Shift+[");
    hints.brushHardnessIncrease = QStringLiteral("Shift+]");
    model.setKeyHints(hints);

    int clearRequested = 0;
    QObject::connect(&model, &gui::ToolViewModel::clearMaskRequested, [&]() { ++clearRequested; });

    QQmlEngine engine;
    // NOT "model": a Repeater's own `model` property shadows the context property.
    engine.rootContext()->setContextProperty(QStringLiteral("toolModel"), &model);
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    engine.rootContext()->setContextProperty(QStringLiteral("uiFont"), QApplication::font());

    QQmlComponent component(&engine, QUrl(QStringLiteral("file:" AE_QML_DIR "/ToolsPanel.qml")));
    if (component.isError()) {
        QTextStream(stderr) << "the tools panel's QML does not load:\n";
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "  " << error.toString() << "\n";
        return 2;
    }
    check(component.status() == QQmlComponent::Ready, "the tools panel's QML is Ready");

    // A real window, because a ScrollView and the layouts inside it polish against one.
    // The scene paints nothing here; it is the geometry and the wiring under test.
    QQuickWindow window;
    QScopedPointer<QObject> created(component.create());
    auto* root = qobject_cast<QQuickItem*>(created.data());
    if (!root) {
        QTextStream(stderr) << "the tools panel is not an Item\n";
        return 2;
    }
    root->setParentItem(window.contentItem());
    const qreal dockWidth = 236;
    root->setWidth(dockWidth);
    root->setHeight(620);
    window.resize(int(dockWidth), 620);
    window.show();
    QCoreApplication::processEvents();

    // ---- the toolbox -----------------------------------------------------------
    const QVariantList tools = model.tools();
    QList<QQuickItem*> toolButtons;
    for (int i = 0; i < tools.size(); ++i) {
        const QVariantMap entry = tools.at(i).toMap();
        const QString tip = entry.value(QStringLiteral("tip")).toString();
        QQuickItem* button = control(root, tip, QAccessible::Button);
        check(button != nullptr, QStringLiteral("the toolbox has a reachable button named '%1'").arg(tip));
        if (button)
            toolButtons << button;
    }
    check(toolButtons.size() == 7, QStringLiteral("all seven are named and reachable (%1)").arg(toolButtons.size()));
    if (toolButtons.size() == 7) {
        const qreal firstTop = topOf(toolButtons.first(), root);
        bool oneRow = true;
        for (QQuickItem* button : toolButtons) {
            if (qAbs(topOf(button, root) - firstTop) > 0.5)
                oneRow = false;
        }
        check(oneRow, QStringLiteral("all seven tools sit on ONE row at 236px (%1)")
                          .arg(describeRect(toolButtons.last(), root)));
        check(toolButtons.last()->mapToItem(root, 0, 0).x() + toolButtons.last()->width() <= dockWidth,
              QStringLiteral("and the seventh ends inside the dock (%1)").arg(describeRect(toolButtons.last(), root)));
        QTextStream(stdout) << "MEASURED  seventh tool: " << describeRect(toolButtons.last(), root) << "\n";
    }

    // ---- pressing a tool -------------------------------------------------------
    QQuickItem* boneButton = control(root, QStringLiteral("Bone editor"), QAccessible::Button);
    check(boneButton && press(boneButton), "the bone editor's button accepts a press");
    QCoreApplication::processEvents();
    check(model.tool() == int(ctrl::ToolType_Bone), "pressing it selects the tool in the model");
    // The panel heading names the tool and is a HEADING, not a second button with the
    // same word: that is what keeps a screen reader (and a probe) able to tell them apart.
    check(control(root, QStringLiteral("Bone editor"), QAccessible::Heading) != nullptr,
          "the panel heading names the tool as a heading");
    check(control(root, QStringLiteral("Bone editor"), QAccessible::Button) == boneButton,
          "and the only BUTTON with that name is the toolbox's");

    // ---- the modes -------------------------------------------------------------
    check(control(root, QStringLiteral("Add bones"), QAccessible::Button) != nullptr,
          "the bone editor's mode row publishes its mode names");
    // The caption names the mode: at this point it is "Add bones", not the mode the
    // heading would show after a switch.
    check(control(root, QStringLiteral("Add bones"), QAccessible::Heading) == nullptr,
          "the caption is not a heading (the heading names the TOOL)");
    QQuickItem* paintInfl = control(root, QStringLiteral("Paint influence"), QAccessible::Button);
    check(paintInfl && press(paintInfl), "the Paint influence mode button accepts a press");
    QCoreApplication::processEvents();
    check(model.modeIndex() == int(ctrl::BoneEditMode_PaintInfl), "and writes the model's mode");
    {
        // seven mode buttons on ONE row: the widget wrapped them at 172px
        QQuickItem* eraseInfl = control(root, QStringLiteral("Erase influence"), QAccessible::Button);
        check(eraseInfl && qAbs(topOf(eraseInfl, root) - topOf(paintInfl, root)) <= 0.5,
              QStringLiteral("seven mode buttons fit on one row at 236px (%1)")
                  .arg(eraseInfl ? describeRect(eraseInfl, root) : QString()));
        if (eraseInfl)
            QTextStream(stdout) << "MEASURED  last mode button: " << describeRect(eraseInfl, root) << "\n";
    }

    // ---- the brush's numeric fields are real edit paths ------------------------
    QQuickItem* sizeField = control(root, QStringLiteral("Size"), QAccessible::SpinBox);
    check(sizeField != nullptr, "the brush's Size row publishes a SpinBox named 'Size'");
    if (sizeField) {
        const double before = model.brush()->radius();
        check(setValue(sizeField, 137.0), "its value interface accepts a write");
        QCoreApplication::processEvents();
        check(qAbs(model.brush()->radius() - 137.0) < 0.001,
              QStringLiteral("and the write lands on the brush (%1 -> %2)")
                  .arg(before).arg(model.brush()->radius()));
    }
    // the percentages are shown as 0..100 and stored as 0..1
    QQuickItem* hardnessField = control(root, QStringLiteral("Hardness"), QAccessible::SpinBox);
    check(hardnessField != nullptr, "the Hardness row publishes its field");
    if (hardnessField) {
        check(setValue(hardnessField, 25.0), "Hardness accepts a percentage");
        check(qAbs(model.brush()->hardness() - 0.25) < 0.001,
              QStringLiteral("and stores it as a fraction (%1)").arg(model.brush()->hardness()));
    }

    // ---- the mask: two combos, one of which disappears -------------------------
    QQuickItem* maskButton = control(root, QStringLiteral("Mask paint"), QAccessible::Button);
    check(maskButton && press(maskButton), "the mask tool's button accepts a press");
    QCoreApplication::processEvents();
    check(model.tool() == int(ctrl::ToolType_Mask), "the mask tool is up");
    // Opacity is offered EXACTLY where the target reads it: the mask brush (MaskEditor
    // multiplies its stamp by brush.opacity). It is not a brush row for the bone, pose or
    // FFD brushes, which never read it - the reported "why is there no opacity?".
    check(model.opacityVisible(), "the mask brush offers its Opacity row");
    check(control(root, QStringLiteral("Filter mask"), QAccessible::Heading) != nullptr,
          "the heading says the panel edits a FILTER mask");
    // A combo reports its VALUE as its name (Qt's own rule), which is exactly what makes
    // "which mask am I painting" readable without hovering. ONE chooser, one label: the
    // field names itself ("Wave Deform amount"), not a lane and then a channel.
    check(visibleControl(root, QStringLiteral("Wave Deform amount"), QAccessible::ComboBox) != nullptr,
          "the field chooser reads 'Wave Deform amount'");
    check(visibleControl(root, QStringLiteral("Amount"), QAccessible::ComboBox) == nullptr,
          "and there is no second, channel chooser any more");

    // the same row, chosen through the list: the matte
    model.setMaskField(6);
    QCoreApplication::processEvents();
    check(visibleControl(root, QStringLiteral("Filter mask"), QAccessible::Heading) != nullptr,
          "unpinned, the heading still names the KIND (the chooser names the field)");
    check(visibleControl(root, QStringLiteral("Layer mask"), QAccessible::ComboBox) != nullptr,
          "and the chooser names it");

    // ---- ENTERED FROM A CARD the field is a caption, not a question -------------
    model.beginLayerMaskPaint();
    QCoreApplication::processEvents();
    check(model.maskPinned(), "a card entry pins the field");
    check(visibleControl(root, QStringLiteral("Layer mask"), QAccessible::ComboBox) == nullptr,
          "the chooser is GONE: nobody asked a question the card already answered");
    // ...and the HEADING is the field's name now, so no caption row is needed either: one
    // row of chrome (the heading) carries the name and the field's own two actions.
    check(visibleControl(root, QStringLiteral("Layer mask"), QAccessible::Heading) != nullptr,
          "the heading names the field");
    QQuickItem* change = visibleControl(root, QStringLiteral("Change what is painted"), QAccessible::Button);
    check(change != nullptr, "and its disclosure is there");
    if (change) {
        check(press(change), "the disclosure accepts a press");
        QCoreApplication::processEvents();
        check(!model.maskPinned(), "which re-opens the chooser");
        check(visibleControl(root, QStringLiteral("Layer mask"), QAccessible::ComboBox) != nullptr,
              "and the chooser is back");
    }

    // the field's own utility is a QUIET text action, not the panel's primary button
    QQuickItem* clearButton = visibleControl(root, QStringLiteral("Clear"), QAccessible::Button);
    check(clearButton != nullptr, "the field row carries a Clear action");
    if (clearButton) {
        check(press(clearButton), "it accepts a press");
        check(clearRequested == 1, "and asks the host to clear the field");
    }

    // ---- the view strip speaks the bound keys ---------------------------------
    QQuickItem* rotateCcw = nullptr;
    {
        // the rotation button's NAME carries the live binding
        QList<QQuickItem*> all;
        collectItems(root, all);
        for (QQuickItem* item : all) {
            QAccessibleInterface* iface = accessible(item);
            if (!iface || iface->role() != QAccessible::Button)
                continue;
            if (iface->text(QAccessible::Name).contains(QStringLiteral("Alt+Q")))
                rotateCcw = item;
        }
    }
    check(rotateCcw != nullptr, "the rotation button's name carries the live binding (Alt+Q)");
    if (rotateCcw) {
        check(press(rotateCcw), "and it is pressable");
        check(!view.rotateViewACW, "the rotation flag is lowered again after the trigger");
    }

    // ---- the panel scrolls ----------------------------------------------------
    bool sawScrollView = false;
    QList<QQuickItem*> all;
    collectItems(root, all);
    for (QQuickItem* item : all) {
        const QByteArray name(item->metaObject()->className());
        if (name.contains("ScrollView"))
            sawScrollView = true;
    }
    check(sawScrollView, "the panel is inside a ScrollView (the widget dock clipped tall panels)");

    // ---- the icon URLs carry the tint generation -------------------------------
    const QUrl before = theme.iconUrl(QStringLiteral("bone"));
    theme.refresh();
    const QUrl after = theme.iconUrl(QStringLiteral("bone"));
    check(before != after, QStringLiteral("a theme refresh changes the icon URL (%1 -> %2)")
                               .arg(before.toString(), after.toString()));
    check(after.toString().contains(QStringLiteral("?v=")),
          "so a QQuickImage's URL-keyed cache reloads the re-tinted glyph");
    // ...and the SCENE's own Image follows it. The URL alone is not enough: iconUrl() is a
    // Q_INVOKABLE, and a method call in a binding registers no dependency, so a button's
    // `source` would keep the theme it was built with. Measured before the fix: the dock
    // kept the dark theme's near-white glyphs on the light background.
    {
        QQuickItem* icon = nullptr;
        QList<QQuickItem*> allItems;
        collectItems(root, allItems);
        for (QQuickItem* item : allItems) {
            const QString source = item->property("source").toString();
            if (source.contains(QStringLiteral(".svg"))) {
                icon = item;
                break;
            }
        }
        check(icon != nullptr, "a toolbox glyph is an Image the scene can be asked about");
        if (icon) {
            const QString before = icon->property("source").toString();
            theme.refresh();
            QCoreApplication::processEvents();
            const QString now = icon->property("source").toString();
            check(now != before,
                  QStringLiteral("and its source FOLLOWS a theme refresh (%1 -> %2)")
                      .arg(before, now));
        }
    }
    // The checked glyph is the PLAIN one: the <name>-active pair is accentText-tinted,
    // which measured 1.35:1 on the light theme's pale accent fill. An icon URL is the
    // plain glyph whatever the button's state.
    check(!theme.iconUrl(QStringLiteral("bone")).toString().contains(QStringLiteral("-active")),
          "the checked state does not ask for a second glyph file");

    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: the tools dock's scene builds, shows and wires its controls\n";
    return 0;
}
