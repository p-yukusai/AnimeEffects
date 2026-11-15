#include <QDebug>
#include "gui/KeyCommandInvoker.h"
#include "gui/KeyCommandMap.h"
#ifdef AE_ENABLE_IMGUI
#include "gui/ImGuiInputBridge.h"
#endif

namespace gui {
namespace {

// WHETHER THE VIEWPORT OVERLAY OWNS THE KEYBOARD RIGHT NOW.
//
// io.WantCaptureKeyboard, read at the last completed frame (see
// ImGuiInputBridge::keyboardCaptured()): a Dear ImGui text field is active and the
// keys typed into it are the field's. False whenever no ImGui field is active,
// which is the whole time today, and false in a build without the overlay - so the
// application's own key routing is unchanged in both.
bool overlayOwnsKeyboard() {
#ifdef AE_ENABLE_IMGUI
    return ImGuiInputBridge::keyboardCaptured();
#else
    return false;
#endif
}

} // namespace

KeyCommandInvoker::KeyCommandInvoker(KeyCommandMap& aMap): mMap(aMap), mLastCommand(nullptr), mLastKey() {}

void KeyCommandInvoker::onKeyPressed(const QKeyEvent* aEvent) {
    // if (aEvent->isAutoRepeat()) return;

    // release the previous command
    releaseLastCommand();

    // THE OVERLAY'S GATE. A rebindable command must NOT fire while the overlay owns
    // the keyboard: the key belongs to the ImGui field, and "Delete" typed into it
    // must not delete the selected key of the timeline. This is the one place every
    // rebindable command fires - the two-key chords below and the plain bindings
    // below them - so the whole rule is this one test. The release of a command
    // already armed is deliberately above it: letting go is never the field's.
    if (overlayOwnsKeyboard())
        return;

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
