// THE HEADER'S WIDGET-FREE MODELS.
//
// The header is a Qt Quick scene, but none of it needs a QQuickWindow to answer the
// questions it owns:
//   * gui::MenuCommand - the command registry row: enabled, checkable/checked and
//     one trigger route shared by click, keyboard and accessibility.
//   * gui::ViewSettingsModel - the six viewport controls that moved out of the Tools
//     dock, including the rotation verbs' raise/announce/lower contract.
//   * gui::DockSlotsModel - the three layout toggles and the window-owned state.
//
// Run: QT_QPA_PLATFORM=offscreen ./build/tests/Debug/header_model
#include <QCoreApplication>
#include <QSignalSpy>
#include <QTextStream>

#include "gui/MainViewSetting.h"
#include "gui/qml/DockSlotsModel.h"
#include "gui/qml/MenuCommand.h"
#include "gui/qml/ViewSettingsModel.h"

namespace {

int failures = 0;

void check(bool aOk, const QString& aWhat) {
    if (aOk) {
        QTextStream(stdout) << "PASS  " << aWhat << "\n";
        return;
    }
    ++failures;
    QTextStream(stdout) << "FAIL  " << aWhat << "\n";
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    // ---- MenuCommand: one route, an enabled gate, authoritative checking ----------
    {
        int ran = 0;
        gui::MenuCommand command(QStringLiteral("Open project"), [&]() { ++ran; });
        check(command.text() == QStringLiteral("Open project"), QStringLiteral("a command keeps its words"));
        check(command.isEnabled(), QStringLiteral("a command starts enabled"));

        command.trigger();
        check(ran == 1, QStringLiteral("trigger runs the callback"));

        command.setEnabled(false);
        check(!command.isEnabled(), QStringLiteral("a command can be disabled"));
        command.trigger();
        check(ran == 1, QStringLiteral("a disabled command is a no-op"));
        command.setEnabled(true);
        command.trigger();
        check(ran == 2, QStringLiteral("and enabled again it runs once more"));
    }
    {
        int toggled = 0;
        gui::MenuCommand toggler(QStringLiteral("Tools"), [&]() { ++toggled; }, true);
        toggler.setChecked(true);
        check(toggler.isChecked(), QStringLiteral("a checkable command takes an external state"));
        const bool before = toggler.isChecked();
        toggler.trigger();
        check(toggled == 1, QStringLiteral("its trigger runs the callback"));
        check(toggler.isChecked() == before,
              QStringLiteral("but it never flips its own checked state (the window owns it)"));
    }

    // ---- ViewSettingsModel: the six controls that left the Tools dock ------------
    {
        gui::MainViewSetting setting;
        gui::ViewSettingsModel model(&setting);
        QSignalSpy changed(&model, &gui::ViewSettingsModel::changed);
        QSignalSpy announced(&model, &gui::ViewSettingsModel::viewSettingChanged);

        check(!model.showMesh() && !model.cutOutsideFrame() && !model.flipCanvas(),
              QStringLiteral("the view toggles start off"));
        model.setShowMesh(true);
        check(setting.showLayerMesh && model.showMesh(), QStringLiteral("Show mesh writes the setting"));
        check(changed.count() == 1 && announced.count() == 1,
              QStringLiteral("and announces the change once"));
        model.setShowMesh(true);
        check(changed.count() == 1 && announced.count() == 1,
              QStringLiteral("writing the same value notifies nothing"));

        model.setCutOutsideFrame(true);
        model.setFlipCanvas(true);
        check(setting.cutImagesByTheFrame && setting.flipCanvas,
              QStringLiteral("the other two toggles write their settings"));
        check(changed.count() == 3, QStringLiteral("each toggle is one change"));

        // The rotation verbs are TRIGGERS. The flag is up while the announcement
        // runs and down immediately after; a latched flag would rotate the canvas
        // on every later view change (the deleted ViewPanel's contract).
        bool sawAcw = false;
        bool sawReset = false;
        bool sawCw = false;
        QObject::connect(&model, &gui::ViewSettingsModel::viewSettingChanged, [&]() {
            sawAcw = sawAcw || setting.rotateViewACW;
            sawReset = sawReset || setting.resetRotateView;
            sawCw = sawCw || setting.rotateViewCW;
        });
        model.rotateCounterClockwise();
        model.resetRotation();
        model.rotateClockwise();
        check(sawAcw && sawReset && sawCw, QStringLiteral("each rotation verb raises its flag"));
        check(!setting.rotateViewACW && !setting.resetRotateView && !setting.rotateViewCW,
              QStringLiteral("and lowers it again"));

        model.setKeyHints(QStringLiteral("Alt+Q"), QStringLiteral("F1"), QStringLiteral("Alt+E"));
        check(model.rotateCounterClockwiseTip().contains(QStringLiteral("Alt+Q"))
                  && model.resetRotationTip().contains(QStringLiteral("F1"))
                  && model.rotateClockwiseTip().contains(QStringLiteral("Alt+E")),
              QStringLiteral("the rotation tooltips name the live bindings"));
        check(!model.showMeshTip().isEmpty() && !model.cutOutsideFrameTip().isEmpty()
                  && !model.flipCanvasTip().isEmpty(),
              QStringLiteral("every view control has its words"));
    }

    // ---- DockSlotsModel: the three layout toggles --------------------------------
    {
        gui::DockSlotsModel model;
        QVector<gui::DockSlotSpec> specs = {
            {Qt::LeftDockWidgetArea, QStringLiteral("sidebar-left"), QStringLiteral("Show or hide the left side")},
            {Qt::RightDockWidgetArea, QStringLiteral("sidebar-right"), QStringLiteral("Show or hide the right side")},
            {Qt::BottomDockWidgetArea, QStringLiteral("sidebar-bottom"), QStringLiteral("Show or hide the bottom")},
        };
        model.setSlots(specs);
        const QVariantList toggles = model.toggles();
        check(toggles.size() == 3, QStringLiteral("three slots, in window order"));
        auto* left = qobject_cast<gui::DockSlotItem*>(toggles.at(0).value<QObject*>());
        auto* bottom = qobject_cast<gui::DockSlotItem*>(toggles.at(2).value<QObject*>());
        check(left && left->area() == Qt::LeftDockWidgetArea
                  && left->glyph() == QStringLiteral("sidebar-left"),
              QStringLiteral("the first is the left slot with the left glyph"));
        check(bottom && bottom->area() == Qt::BottomDockWidgetArea, QStringLiteral("the last is the bottom slot"));

        model.setSlotState(Qt::LeftDockWidgetArea, true, true,
                           QStringLiteral("Show or hide the left side"),
                           QStringLiteral("Show or hide the left side"));
        check(left->isChecked() && left->isAvailable(), QStringLiteral("a populated slot can be lit and enabled"));
        check(left->tip() == QStringLiteral("Show or hide the left side"), QStringLiteral("its tooltip follows"));

        model.setSlotState(Qt::BottomDockWidgetArea, false, false,
                           QStringLiteral("Show or hide the bottom"),
                           QStringLiteral("Nothing is docked on the bottom"));
        check(!bottom->isChecked() && !bottom->isAvailable(), QStringLiteral("an empty slot is unlit and disabled"));
        check(bottom->tip() == QStringLiteral("Nothing is docked on the bottom"),
              QStringLiteral("and its tooltip explains why it cannot be used"));

        QSignalSpy requested(&model, &gui::DockSlotsModel::toggleRequested);
        model.toggle(1);
        check(requested.count() == 1, QStringLiteral("a slot press asks for a toggle"));
        check(requested.at(0).at(0).value<Qt::DockWidgetArea>() == Qt::RightDockWidgetArea,
              QStringLiteral("naming the area, never a QWidget"));
        model.toggle(-1);
        model.toggle(99);
        check(requested.count() == 1, QStringLiteral("an out-of-range press is a no-op"));
    }

    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: the header models' contract holds\n";
    return 0;
}
