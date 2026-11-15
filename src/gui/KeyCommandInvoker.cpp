#include <QDebug>
#include "gui/KeyCommandInvoker.h"
#include "gui/KeyCommandMap.h"

namespace gui {

KeyCommandInvoker::KeyCommandInvoker(KeyCommandMap& aMap): mMap(aMap), mLastCommand(nullptr), mLastKey() {}

void KeyCommandInvoker::onKeyPressed(const QKeyEvent* aEvent) {
    // if (aEvent->isAutoRepeat()) return;

    // release the previous command
    releaseLastCommand();

    const ctrl::KeyBinding keyBind(aEvent->key(), aEvent->modifiers());
    if (!keyBind.isValidBinding())
        return;

    // firstly, search commands which contains sub key.
    {
        if (mLastKey.isValidBinding() && !keyBind.hasAnyModifiers()) {
            auto keyBindWithSub = mLastKey;
            keyBindWithSub.setSubKeyCode(aEvent->key());

            for (auto command : mMap.subKeyCommands()) {
                if (!command->invokers.isEmpty() && command->binding.isValidBinding() &&
                    command->binding.matchesExactlyWith(keyBindWithSub)) {
                    // invoke
                    mLastCommand = command;
                    mLastKey = ctrl::KeyBinding();
                    for (const auto& invoker : command->invokers)
                        invoker();
                    return;
                }
            }
        }
        mLastKey = keyBind;
    }

    // each key commands
    for (auto command : mMap.commands()) {
        if (!command->invokers.isEmpty() && command->binding.isValidBinding() &&
            command->binding.matchesExactlyWith(keyBind)) {
            // invoke every listener (several widgets may react to one key)
            mLastCommand = command;
            for (const auto& invoker : command->invokers)
                invoker();
            break;
        }
    }
}

void KeyCommandInvoker::onKeyReleased(const QKeyEvent* aEvent) {
    // qDebug() << "rls" << aEvent->key() << aEvent->modifiers() << aEvent->isAutoRepeat();
    Q_UNUSED(aEvent);
    // if (aEvent->isAutoRepeat()) return;
    releaseLastCommand();
}

void KeyCommandInvoker::releaseLastCommand() {
    if (mLastCommand) {
        for (const auto& releaser : mLastCommand->releasers)
            releaser();
        mLastCommand = nullptr;
    }
}

} // namespace gui
