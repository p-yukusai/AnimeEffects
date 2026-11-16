#ifndef CMND_GESTURE_H
#define CMND_GESTURE_H

#include "cmnd/Base.h"
#include "cmnd/ScopedMacro.h"
#include "cmnd/Stack.h"

namespace cmnd {

// One open edit against the document.
//
// The document is value-replayable: cmnd/Stable.h requires undo/redo to rebuild it
// from the values a command carries, with no help from the editor that created it.
// An interactive edit therefore edits through exactly ONE command, and a Gesture is
// the object that says which command that is and whether the edit is still open:
//
//   * begin() opens the edit: it runs aBuild inside the macro that wraps this
//     commit (grab listeners there, push any key that must share the macro), then
//     pushes the command aBuild returned and adopts it. The macro closes after the
//     push, which is when the grabbed listeners fire -- the same order the editors
//     used to write out by hand.
//   * continues() is true while that command is still the one the stack would
//     coalesce into (Stack::isModifiable). A later commit may then rewrite it, so a
//     whole drag is one undo step. Any undo, redo or clear drops the stack's
//     modifiable link, continues() goes false, and the next commit opens a new edit
//     instead of corrupting the history.
//   * end() closes the edit. A gesture that is never committed pushes nothing, and
//     letting it die is a no-op: nothing here is committed implicitly.
//
// This is the engine half of the seam. The timeline half -- the listener that turns
// one commit into one TimeLineEvent, and the key push that must happen inside the
// same macro -- stays with the editor, because it is not cmnd's vocabulary.
template<typename tCommand>
class Gesture {
public:
    explicit Gesture(Stack& aStack): mStack(aStack), mCommand(nullptr) {}

    Gesture(const Gesture&) = delete;
    Gesture& operator=(const Gesture&) = delete;

    // True while the command this edit pushed is still the stack's modifiable one,
    // i.e. a further commit may rewrite it instead of pushing a new command.
    bool continues() const { return mCommand && mStack.isModifiable(mCommand); }

    // The command this edit owns; null until the first commit, and stale after an
    // undo/redo has taken it off the top of the stack. Check continues() before use.
    tCommand* command() const { return mCommand; }

    // First commit of the edit. aBuild runs inside this commit's macro and returns
    // the command to push.
    template<typename tBuild>
    tCommand* begin(const QString& aName, tBuild&& aBuild) {
        ScopedMacro macro(mStack, aName);
        tCommand* command = aBuild(macro);
        mCommand = command;
        mStack.push(command);
        return command;
    }

    // Closes the edit: the next commit begins a new one. Idempotent.
    void end() { mCommand = nullptr; }

private:
    Stack& mStack;
    tCommand* mCommand;
};

} // namespace cmnd

#endif // CMND_GESTURE_H
