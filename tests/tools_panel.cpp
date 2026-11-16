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

    gui::ToolViewModel model;
    model.setDriverActive(true);
    gui::ToolKeyHints hints;
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
    // THE DOCK'S WIDTH ARRIVES AFTER THE SCENE IS BUILT, and a Flow lays its items
    // out from its OWN width: the first pass runs while the root is still 0-wide
    // (one item per row) and the reflow is a POLISH pass. The live window gets that
    // from its frame loop; this test has to drain it explicitly, or every measured
    // rect is the pre-resize layout (measured: the toolbox read as one column).
    root->setWidth(dockWidth - 1);
    root->setWidth(dockWidth);
    for (int i = 0; i < 12; ++i) {
        QCoreApplication::processEvents();
        window.update();
    }
    // A Polish pass after the content has a height can arrive later than the first 12
    // turns (the ScrollView's scrollbar changes what is available to reflow).
    for (int i = 0; i < 60; ++i) {
        QCoreApplication::processEvents();
        window.update();
    }

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
    {
        int checked = 0;
        for (QQuickItem* button : toolButtons) {
            if (button->property("checked").toBool())
                ++checked;
        }
        check(checked == 1, QStringLiteral("exactly one toolbox button is checked (%1)").arg(checked));
    }
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents();
        window.update();
    }
    // The panel heading names the tool and is a HEADING, not a second button with the
    // same word: that is what keeps a screen reader (and a probe) able to tell them apart.
    check(control(root, QStringLiteral("Bone editor"), QAccessible::Heading) != nullptr,
          "the panel heading names the tool as a heading");
    check(control(root, QStringLiteral("Bone editor"), QAccessible::Button) == boneButton,
          "and the only BUTTON with that name is the toolbox's");

    // ---- the modes -------------------------------------------------------------
    const QStringList boneModeNames = {
        QStringLiteral("Add bones"), QStringLiteral("Remove bones"), QStringLiteral("Move joints"),
        QStringLiteral("Bind bone to node"), QStringLiteral("Adjust envelope"),
        QStringLiteral("Envelope brush"), QStringLiteral("Paint weights")};
    QList<QQuickItem*> modeButtons;
    for (const QString& name : boneModeNames) {
        QQuickItem* button = control(root, name, QAccessible::Button);
        check(button != nullptr, QStringLiteral("the bone editor's mode row publishes '%1'").arg(name));
        if (button)
            modeButtons << button;
    }
    check(modeButtons.size() == 7, QStringLiteral("all seven modes are named and reachable (%1)").arg(modeButtons.size()));
    // The one Envelope brush replaced the old paint/erase pair, so seven modes fit one
    // row again; the four-column grid that made room for the eighth is gone.
    if (modeButtons.size() == 7) {
        const qreal firstTop = topOf(modeButtons[0], root);
        bool oneRow = true;
        bool columns = true;
        for (int i = 0; i < 7; ++i) {
            oneRow = oneRow && qAbs(topOf(modeButtons[i], root) - firstTop) <= 0.5;
            const qreal expected = modeButtons[0]->mapToItem(root, 0, 0).x() + 28.0 * i;
            columns = columns && qAbs(modeButtons[i]->mapToItem(root, 0, 0).x() - expected) <= 0.5;
        }
        check(oneRow, QStringLiteral("the seven modes sit on ONE row"));
        check(columns, QStringLiteral("the seven columns follow the 26+2 grid"));
        check(modeButtons[6]->mapToItem(root, 0, 0).x() + modeButtons[6]->width() <= dockWidth,
              QStringLiteral("the last mode ends inside the dock (%1)").arg(describeRect(modeButtons[6], root)));
        QTextStream(stdout) << "MEASURED  first mode: " << describeRect(modeButtons[0], root)
                            << "  last mode: " << describeRect(modeButtons[6], root) << "\n";
    }
    // The glyphs say what each mode does. Adjust is the envelope RANGE (broadcast),
    // Envelope brush is a brush qualified by that same range (the small badge is the
    // range simplified for 18px), and the PLAIN brush is weights. The envelope brush is
    // the qualified variant; the unqualified brush is the canonical weight paint.
    {
        const QVariantList modes = model.modes();
        check(modes.size() == 7, "the model publishes the seven mode entries");
        if (modes.size() == 7) {
            const QString adjustIcon = modes.at(4).toMap().value(QStringLiteral("icon")).toString();
            const QString inflIcon = modes.at(5).toMap().value(QStringLiteral("icon")).toString();
            const QString weightsIcon = modes.at(6).toMap().value(QStringLiteral("icon")).toString();
            check(adjustIcon == QStringLiteral("envelope") && inflIcon == QStringLiteral("paint-envelope")
                      && weightsIcon == QStringLiteral("paint-brush"),
                  QStringLiteral("the field row: range, qualified brush, plain brush (%1, %2, %3)")
                      .arg(adjustIcon, inflIcon, weightsIcon));
            check(inflIcon != weightsIcon, "the envelope brush and the weight brush are not the same glyph");
            int plainBrushes = 0;
            for (const QVariant& mode : modes) {
                if (mode.toMap().value(QStringLiteral("icon")).toString() == QStringLiteral("paint-brush")) {
                    ++plainBrushes;
                }
            }
            check(plainBrushes == 1,
                  QStringLiteral("exactly one plain brush, and it is weights (%1)").arg(plainBrushes));
            check(weightsIcon != QStringLiteral("drop"), "the weights mode is not the opacity droplet");
        }
        QQuickItem* weightsButton = control(root, QStringLiteral("Paint weights"), QAccessible::Button);
        QQuickItem* inflButton = control(root, QStringLiteral("Envelope brush"), QAccessible::Button);
        bool weightsGlyph = false;
        bool influenceGlyph = false;
        if (weightsButton) {
            QList<QQuickItem*> items;
            collectItems(weightsButton, items);
            for (QQuickItem* item : items) {
                if (item->property("source").toString().contains(QStringLiteral("paint-brush.svg"))) {
                    weightsGlyph = true;
                }
            }
        }
        if (inflButton) {
            QList<QQuickItem*> items;
            collectItems(inflButton, items);
            for (QQuickItem* item : items) {
                if (item->property("source").toString().contains(QStringLiteral("paint-envelope.svg"))) {
                    influenceGlyph = true;
                }
            }
        }
        check(weightsGlyph, "the Paint weights button resolves the plain brush glyph");
        check(influenceGlyph, "the Envelope brush button resolves the range-qualified brush");
    }
    // The caption names the mode: at this point it is "Add bones", not the mode the
    // heading would show after a switch.
    check(control(root, QStringLiteral("Add bones"), QAccessible::Heading) == nullptr,
          "the caption is not a heading (the heading names the TOOL)");
    QQuickItem* paintInfl = control(root, QStringLiteral("Envelope brush"), QAccessible::Button);
    check(paintInfl && press(paintInfl), "the Envelope brush mode button accepts a press");
    QCoreApplication::processEvents();
    check(model.modeIndex() == int(ctrl::BoneEditMode_PaintInfl), "and writes the model's mode");
    {
        QQuickItem* paintWeights = control(root, QStringLiteral("Paint weights"), QAccessible::Button);
        check(paintWeights && press(paintWeights), "the Paint weights mode button accepts a press");
        QCoreApplication::processEvents();
        check(model.modeIndex() == 6, "and writes the weights mode (the seventh row entry)");
        check(model.modeName() == QStringLiteral("Paint weights"), "the heading names the weights mode");
        // Back to the envelope brush for the shared brush rows the rest of this gate
        // reads. The panel is rebuilt when the row set changes, so this is a fresh
        // button, not the pointer from the previous mode.
        QQuickItem* paintInflAgain = control(root, QStringLiteral("Envelope brush"), QAccessible::Button);
        check(paintInflAgain && press(paintInflAgain), "Envelope brush can be pressed again");
        QCoreApplication::processEvents();
        check(model.modeIndex() == int(ctrl::BoneEditMode_PaintInfl), "and restores the envelope mode");

        // The VALUE row is the one explicit parameter: size/hardness are the
        // footprint and flow the rate, so the old direction toggles are gone.
        QQuickItem* valueField = visibleControl(root, QStringLiteral("Value"), QAccessible::SpinBox);
        check(valueField != nullptr, "the envelope brush publishes a Value field");
        if (valueField) {
            check(setValue(valueField, 123.0), "the Value field accepts a write");
            QCoreApplication::processEvents();
            check(qAbs(model.envelopeValue() - 123.0) < 0.001,
                  QStringLiteral("and the value lands on the model (%1)").arg(model.envelopeValue()));
        }
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

    // ---- the mask: a derived Target row, or an explained empty state -------------
    QQuickItem* maskButton = control(root, QStringLiteral("Mask paint"), QAccessible::Button);
    check(maskButton && press(maskButton), "the mask tool's button accepts a press");
    QCoreApplication::processEvents();
    check(model.tool() == int(ctrl::ToolType_Mask), "the mask tool is up");
    // Opacity is offered EXACTLY where the target reads it: the mask brush (MaskEditor
    // multiplies its stamp by brush.opacity). It is not a brush row for the bone, pose or
    // FFD brushes, which never read it - the reported "why is there no opacity?".
    check(model.opacityVisible(), "the mask brush offers its Opacity row");
    check(control(root, QStringLiteral("Mask"), QAccessible::Heading) != nullptr,
          "the heading names the TOOL, not a field caption");
    // With no document context the chooser cannot invent a target. The panel must not
    // draw a brush that would silently no-op: it says what is missing instead.
    check(visibleControl(root, QStringLiteral("Select a layer to paint a mask."),
                         QAccessible::StaticText) != nullptr,
          "no context: the empty state explains the missing layer");
    check(visibleControl(root, QStringLiteral("Wave Deform amount"), QAccessible::ComboBox) == nullptr,
          "and no field is offered before there is a target");
    check(visibleControl(root, QStringLiteral("Size"), QAccessible::SpinBox) == nullptr,
          "the brush rows are hidden while nothing is paintable");

    // The host derives the real target set. The panel must show the one Target
    // control, the Value row and the brush, and the list must contain only the
    // fields the host says are paintable.
    QVector<gui::MaskField> fields;
    fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_HSV,
                                 core::FilterMaskChannel_Amount, QStringLiteral("HSV amount")});
    fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_Wave,
                                 core::FilterMaskChannel_Amount, QStringLiteral("Wave Deform amount")});
    fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_Wave,
                                 core::FilterMaskChannel_TimeOffset,
                                 QStringLiteral("Wave Deform time offset")});
    fields.append(gui::MaskField{ctrl::MaskTargetKind_LayerMask, core::TimeKeyType_TERM,
                                 core::FilterMaskChannel_Amount, QStringLiteral("Layer mask")});
    model.setMaskFields(fields, QString());
    for (int i = 0; i < 8; ++i) {
        QCoreApplication::processEvents();
        window.update();
    }
    check(visibleControl(root, QStringLiteral("Wave Deform amount"), QAccessible::ComboBox) != nullptr,
          "a real target set shows the one Target chooser");
    check(visibleControl(root, QStringLiteral("Value"), QAccessible::SpinBox) != nullptr,
          "and the value row is offered");
    check(visibleControl(root, QStringLiteral("Size"), QAccessible::SpinBox) != nullptr,
          "and the brush is offered");
    // The combo and the numeric field are the same control height. In a RowLayout
    // `height` does not size an item; the combo used to keep its 28px dialog height
    // while the fields under it were 23px.
    {
        QQuickItem* targetCombo = nullptr;
        for (QQuickItem* item :
             root->findChildren<QQuickItem*>(QStringLiteral("maskTargetCombo"))) {
            if (item->isVisible()) {
                targetCombo = item;
                break;
            }
        }
        QQuickItem* valueField = visibleControl(root, QStringLiteral("Value"),
                                                QAccessible::SpinBox);
        check(targetCombo != nullptr && valueField != nullptr,
              "the Target combo and the Value field are both addressable");
        if (targetCombo && valueField) {
            check(qAbs(targetCombo->height() - theme.toolFieldHeight()) < 0.5,
                  QStringLiteral("the Target combo uses the dock field height (%1)")
                      .arg(targetCombo->height()));
            check(qAbs(valueField->height() - theme.toolFieldHeight()) < 0.5,
                  QStringLiteral("the Value field uses the same height (%1)")
                      .arg(valueField->height()));
            check(qAbs(targetCombo->height() - valueField->height()) < 0.5,
                  "and the two fields are the same height");
        }
    }

    // The mask rows and the brush rows must use ONE vertical rhythm. They are separate
    // Columns (the brush rows are reusable), so a spacing added to only one is exactly how
    // Target/Value ended up 6px apart while Size/Hardness stayed 2px apart.
    {
        const auto rowFor = [&](const QString& aLabel) -> QQuickItem* {
            QList<QQuickItem*> allItems;
            collectItems(root, allItems);
            for (QQuickItem* item : allItems) {
                if (!item->isVisible() || item->property("text").toString() != aLabel)
                    continue;
                return item->parentItem();
            }
            return nullptr;
        };
        QQuickItem* targetRow = rowFor(QStringLiteral("Target"));
        QQuickItem* valueRow = rowFor(QStringLiteral("Value"));
        QQuickItem* sizeRow = rowFor(QStringLiteral("Size"));
        QQuickItem* hardnessRow = rowFor(QStringLiteral("Hardness"));
        check(targetRow && valueRow && sizeRow && hardnessRow,
              "the Target, Value, Size and Hardness rows are all addressable");
        if (targetRow && valueRow && sizeRow && hardnessRow) {
            const qreal targetTop = targetRow->mapToItem(root, 0, 0).y();
            const qreal valueTop = valueRow->mapToItem(root, 0, 0).y();
            const qreal sizeTop = sizeRow->mapToItem(root, 0, 0).y();
            const qreal hardnessTop = hardnessRow->mapToItem(root, 0, 0).y();
            check(qAbs(targetRow->height() - 28.0) < 0.5
                      && qAbs(valueRow->height() - 28.0) < 0.5
                      && qAbs(sizeRow->height() - 28.0) < 0.5
                      && qAbs(hardnessRow->height() - 28.0) < 0.5,
                  QStringLiteral("every row is 28px (Target %1, Value %2, Size %3, Hardness %4)")
                      .arg(targetRow->height())
                      .arg(valueRow->height())
                      .arg(sizeRow->height())
                      .arg(hardnessRow->height()));
            check(qAbs((valueTop - targetTop) - (hardnessTop - sizeTop)) < 0.5,
                  QStringLiteral("Target->Value uses the same row pitch as Size->Hardness "
                                 "(%1 vs %2)")
                      .arg(valueTop - targetTop)
                      .arg(hardnessTop - sizeTop));
        }
    }

    // A card entry selects the matte; the control stays one Target row.
    model.beginLayerMaskPaint();
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents();
        window.update();
    }
    check(model.maskParam().isLayerMask(), "a card entry selects the matte");
    check(visibleControl(root, QStringLiteral("Layer mask"), QAccessible::ComboBox) != nullptr,
          "and the Target control names it");
    check(visibleControl(root, QStringLiteral("Mask"), QAccessible::Heading) != nullptr,
          "while the heading still names the tool");

    // the field's own utility is a QUIET text action, and cannot be used with no target
    QQuickItem* clearButton = visibleControl(root, QStringLiteral("Clear"), QAccessible::Button);
    check(clearButton != nullptr, "the mask panel carries a Clear action");
    if (clearButton) {
        check(clearButton->isEnabled(), "Clear is enabled with a paintable target");
        check(press(clearButton), "the action is pressable");
        check(clearRequested == 1, "and it asks the host to clear the field");
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

    // A visual record for a UI change: set AE_TOOLS_PANEL_SHOT=<file> to write the
    // panel's own window. A gate does not need it; the reviewer does.
    if (const QByteArray shot = qgetenv("AE_TOOLS_PANEL_SHOT"); !shot.isEmpty()) {
        // The shot is the panel a UI change is about, not whatever the last assertion
        // happened to leave up. Default to weights; AE_TOOLS_PANEL_SHOT_MODE=envelope
        // selects the envelope brush, where its range badge is the thing under review.
        const QByteArray shotMode = qgetenv("AE_TOOLS_PANEL_SHOT_MODE");
        if (shotMode == "mask") {
            QVector<gui::MaskField> fields;
            fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_HSV,
                                         core::FilterMaskChannel_Amount,
                                         QStringLiteral("HSV amount")});
            fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_Wave,
                                         core::FilterMaskChannel_Amount,
                                         QStringLiteral("Wave Deform amount")});
            fields.append(gui::MaskField{ctrl::MaskTargetKind_FilterInput, core::TimeKeyType_Wave,
                                         core::FilterMaskChannel_TimeOffset,
                                         QStringLiteral("Wave Deform time offset")});
            fields.append(gui::MaskField{ctrl::MaskTargetKind_LayerMask, core::TimeKeyType_TERM,
                                         core::FilterMaskChannel_Amount,
                                         QStringLiteral("Layer mask")});
            model.activate(ctrl::ToolType_Mask);
            model.setMaskFields(fields, QString());
            for (int i = 0; i < 8; ++i) {
                QCoreApplication::processEvents();
                window.update();
            }
        } else {
            model.activate(ctrl::ToolType_Bone);
            model.setMode(shotMode == "envelope" ? ctrl::BoneEditMode_PaintInfl
                                                 : ctrl::BoneEditMode_PaintWeights);
        }
        for (int i = 0; i < 8; ++i) {
            QCoreApplication::processEvents();
            window.update();
        }
        check(window.grabWindow().save(QString::fromLocal8Bit(shot)),
              QStringLiteral("the panel window is captured to %1").arg(QString::fromLocal8Bit(shot)));
    }

    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: the tools dock's scene builds, shows and wires its controls\n";
    return 0;
}
