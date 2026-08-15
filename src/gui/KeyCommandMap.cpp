#include "gui/KeyCommandMap.h"

#include "MainDisplayWidget.h"

namespace gui {

//-------------------------------------------------------------------------------------------------
KeyCommandMap::KeyCommand::KeyCommand(): key(), group(), label(), binding(), invoker(), releaser() {}

KeyCommandMap::KeyCommand::KeyCommand(
    const QString& aKey, const QString& aGroup, const QString& aLabel, const ctrl::KeyBinding& aBinding
):
    key(aKey), group(aGroup), label(aLabel), binding(aBinding), invoker(), releaser() {}

//-------------------------------------------------------------------------------------------------
KeyCommandMap::KeyCommandMap(QWidget& aParent):
     QObject(&aParent), mCommands(), mSubKeyCommands(), mSearchMap(), mParent(aParent) {
    auto general = tr("General");
    auto timeline = tr("Timeline");
    auto view = tr("View");
    auto tools = tr("Tools");

    addNewKey("Undo", general, tr("Undo last action"), ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier));

    addNewKey(
        "Redo", general, tr("Redo last action"), ctrl::KeyBinding(Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier)
    );

    addNewKey("Copy", timeline, tr("Copy selected key(s)"), ctrl::KeyBinding(Qt::Key_C, Qt::ControlModifier));

    addNewKey("Paste", timeline, tr("Paste selected key(s)"), ctrl::KeyBinding(Qt::Key_V, Qt::ControlModifier));

    addNewKey("Delete", timeline, tr("Delete selected key(s)"), ctrl::KeyBinding(Qt::Key_Delete));

    addNewKey("SaveProject", general, tr("Save project"), ctrl::KeyBinding(Qt::Key_S, Qt::ControlModifier));

    addNewKey("ToggleDocks", general, tr("Hide/Show docks"), ctrl::KeyBinding(Qt::Key_Q, Qt::ControlModifier));

    addNewKey(
        "MoveRight", timeline, tr("Move one frame to the right"), ctrl::KeyBinding(Qt::Key_Right, Qt::ControlModifier)
    );

    addNewKey(
        "MoveLeft", timeline, tr("Move one frame to the Left"), ctrl::KeyBinding(Qt::Key_Left, Qt::ControlModifier)
    );

    addNewKey(
        "MoveToInit", timeline, tr("Move to the initial frame"), ctrl::KeyBinding(Qt::Key_Up, Qt::ControlModifier)
    );

    addNewKey(
        "MoveToLast", timeline, tr("Move to the last frame"), ctrl::KeyBinding(Qt::Key_Down, Qt::ControlModifier)
    );

    addNewKey("ToggleRepeat", timeline, tr("Enable/disable looping"), ctrl::KeyBinding(Qt::Key_R, Qt::ControlModifier));

    addNewKey(
        "PlayPause", timeline, tr("Play or pause playback"), ctrl::KeyBinding(Qt::Key_Space, Qt::ControlModifier)
    );

    addNewKey("MoveCanvas", view, tr("Move canvas"), ctrl::KeyBinding(Qt::Key_Space, Qt::NoModifier));

    addNewKey("RotateCanvas", view, tr("Rotate canvas"), ctrl::KeyBinding(Qt::Key_Space, Qt::ShiftModifier));

    addNewKey(
        "RotateCanvas15Clockwise", view, tr("Rotate canvas 15° clockwise"), ctrl::KeyBinding(Qt::Key_E, Qt::AltModifier)
    );

    addNewKey(
        "RotateCanvas15AntiClockwise",
        view,
        tr("Rotate canvas 15° anticlockwise"),
        ctrl::KeyBinding(Qt::Key_Q, Qt::AltModifier)
    );

    addNewKey("ResetCanvasAngle", view, tr("Reset canvas angle"), ctrl::KeyBinding(Qt::Key_F1));

    addNewKey("SelectCursor", tools, tr("Select cursor tool"), ctrl::KeyBinding(Qt::Key_1));

    addNewKey("SelectSRT", tools, tr("Select SRT editor"), ctrl::KeyBinding(Qt::Key_2));

    addNewKey("SelectBone", tools, tr("Select bone editor"), ctrl::KeyBinding(Qt::Key_3));

    addNewKey("SelectPose", tools, tr("Select pose editor"), ctrl::KeyBinding(Qt::Key_4));

    addNewKey("SelectMesh", tools, tr("Select mesh editor"), ctrl::KeyBinding(Qt::Key_5));

    addNewKey("SelectFFD", tools, tr("Select FFD editor"), ctrl::KeyBinding(Qt::Key_6));

    addNewKey("MoveFrameLeft", timeline, tr("Move one frame to the left"), ctrl::KeyBinding(Qt::Key_1, Qt::AltModifier));

    addNewKey("MoveFrameRight", timeline, tr("Move one frame to the right"), ctrl::KeyBinding(Qt::Key_3, Qt::AltModifier));

    resetSubKeyCommands();
}

KeyCommandMap::~KeyCommandMap() { qDeleteAll(mCommands); }

void KeyCommandMap::addNewKey(
    const QString& aKey, const QString& aGroup, const QString& aName, const ctrl::KeyBinding& aBinding
) {
    auto command = new KeyCommand(aKey, aGroup, aName, aBinding);
    mSearchMap[aKey] = command;
    mCommands.push_back(command);
}

void KeyCommandMap::readFrom(const QSettings& aSrc) {
    QSettings settings;
    if (settings.value("keybindReset").isValid() && settings.value("keybindReset").toBool()) {
        // this->deleteFrom(aSrc);
        for (auto command : mCommands) {
            if (aSrc.value(command->key).isValid()) {
                aSrc.value(command->key).clear();
            }
        }
        settings.value("keybindReset").clear();
        settings.sync();
        return;
    }

    for (auto command : mCommands) {
        readValue(aSrc, *command);
    }
    resetSubKeyCommands();
}

void KeyCommandMap::deleteFrom(const QSettings& aSrc) {
    for (auto command : mCommands) {
        eraseValue(aSrc, *command);
    }
}

void KeyCommandMap::writeTo(QSettings& aDest) {
    for (auto command : mCommands) {
        writeValue(aDest, *command);
    }
    resetSubKeyCommands();
}

void KeyCommandMap::readValue(const QSettings& aSrc, KeyCommand& aCommand) {
    auto v = aSrc.value(aCommand.key);
    aCommand.binding.setSerialValue(v.isValid() ? v.toString() : QString());
}

void KeyCommandMap::eraseValue(const QSettings& aSrc, KeyCommand& aCommand) {
    if (aSrc.value(aCommand.key).isValid()) {
        aSrc.value(aCommand.key).clear();
    };
}

void KeyCommandMap::writeValue(QSettings& aDest, const KeyCommand& aCommand) {
    aDest.setValue(aCommand.key, aCommand.binding.serialValue());
}

void KeyCommandMap::resetSubKeyCommands() {
    mSubKeyCommands.clear();

    for (auto command : mCommands) {
        if (command->binding.hasSubKeyCode()) {
            mSubKeyCommands.push_back(command);
        }
    }
}

} // namespace gui
