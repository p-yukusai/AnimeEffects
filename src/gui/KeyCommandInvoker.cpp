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
// ImGuiInputBridge::keyboardCaptured()). Measured against the vendored Dear ImGui
// 1.92.7, the flag is computed once per frame in NewFrame, and it is NOT "a text
// field is active": it is true when an ImGui item is ACTIVE (g.ActiveId != 0 - a
// dragged control holds that too, not only a caret), when a MODAL ImGui window is
// open, or when keyboard navigation is enabled and has taken the keyboard
// (io.NavActive && ImGuiConfigFlags_NavEnableKeyboard && io.ConfigNavCaptureKeyboard);
// a per-frame override can force it either way. So the question it answers is
// "is a Dear ImGui widget consuming keys", which is the question a rebindable
// command has to ask before acting on one.
//
// Today it answers false in every frame. This tree's graph panel creates an ImGui
// context and draws through its own draw lists (GraphPanelFrame), but hosts no
// ImGui widgets at all, so there is never an ActiveId, a modal or Nav to set the
// flag - and a build without the overlay returns false outright. The gate is here
// for the widget that will be hosted, not to paper over a bug: without it a
// "Delete" typed into that widget would delete the selected key of the timeline.
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

    // Shift's symbol folded back onto its key (see ctrl::unshiftedKeyForShift)
    const int eventKey = (aEvent->modifiers() & Qt::ShiftModifier) ? ctrl::unshiftedKeyForShift(aEvent->key())
                                                                   : aEvent->key();
    const ctrl::KeyBinding keyBind(eventKey, aEvent->modifiers());
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
