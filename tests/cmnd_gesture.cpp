// Contract test for cmnd::Gesture<tCommand> (src/cmnd/Gesture.h) against the
// real cmnd::Stack. Gesture is the "one open edit" seam: an interactive drag
// edits the document through exactly ONE command, and the gesture is what says
// which command that is and whether the edit is still open.
//
// The properties pinned here, all through the PUBLIC API (Stack::push/undo/redo/
// clear/isModifiable, Gesture::begin/continues/command/end, and our own command's
// counters):
//   * begin() runs its builder while a ScopedMacro is live, pushes the single
//     command the builder returned, and the grabbed listener fires exactly once
//     when the macro closes *after* the push (so nothing ran inside the builder).
//   * continues() is true from begin() until an undo/redo/clear drops the stack's
//     modifiable link, and a later commit may rewrite the SAME command, so a
//     sequence of rewrites is still ONE undo step back to the pre-edit value.
//   * end() closes the edit; the next begin() pushes a second command, so two
//     edits need two undos.
//   * a gesture that is never committed pushes nothing.
//   * a useless command is never executed and contributes no undo step.
//
// Headless: QCoreApplication only, no display, no files, no cwd dependence.
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>
#include "XCAssert.h"
#include "cmnd/Gesture.h"
#include "cmnd/Stable.h"

class AssertHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class ErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
AssertHandler gAssertHandler;
ErrorHandler gErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace {

#define CHECK(cond, msg)                                                                \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::printf("FAIL %d: %s -- %s\n", __LINE__, #cond, msg);                   \
            return 1;                                                                   \
        }                                                                               \
    } while (false)

// The command under test: a Stable command that replays a float value into the
// document (the reference the test owns) and counts how often it actually ran.
// mOldValue is the value the document held when the command was built, i.e. the
// pre-edit value: Stable requires undo to rebuild the document from carried
// values, with no help from the editor, so a command that spans a whole drag must
// carry the value it started from.
class ValueCommand : public cmnd::Stable {
public:
    ValueCommand(float& aDocument, float aValue, bool aUseless = false)
        : mDocument(aDocument),
          mValue(aValue),
          mOldValue(aDocument),
          mUseless(aUseless),
          mExecuted(false),
          mApplyCount(0),
          mUndoCount(0) {}

    QString name() const override { return QStringLiteral("value"); }
    bool isUseless() const override { return mUseless; }

    void redo() override {
        mDocument = mValue;
        ++mApplyCount;
        mExecuted = true;
    }
    void undo() override {
        mDocument = mOldValue;
        ++mUndoCount;
        mExecuted = false;
    }

    // A further commit of the SAME open edit: store the new value and re-apply if
    // the command has already run (the stack executes on the first push/commit).
    void modifyValue(float aValue) {
        mValue = aValue;
        if (mExecuted)
            redo();
    }

    int applyCount() const { return mApplyCount; }
    int undoCount() const { return mUndoCount; }

private:
    float& mDocument;
    float mValue;
    float mOldValue;
    bool mUseless;
    bool mExecuted;
    int mApplyCount;
    int mUndoCount;
};

// Counts what the stack reports to a listener grabbed inside an edit's macro.
// The macro OWNS the listener it grabbed (Stack::Macro::killListeners does
// qDeleteAll(mListeners)), so every listener below is heap-allocated and is never
// deleted by the test.
class CountingListener : public cmnd::Listener {
public:
    void onExecuted() override { ++executed; }
    void onUndone() override { ++undone; }
    void onRedone() override { ++redone; }

    int executed = 0;
    int undone = 0;
    int redone = 0;
};

int run() {
    // ---- (1)(2)(3) begin / coalescing / one undo step ----
    {
        cmnd::Stack stack;
        float document = 0.0f;
        cmnd::Gesture<ValueCommand> gesture(stack);

        CountingListener* listener = nullptr;
        float valueInsideBuilder = -1.0f;
        int firesInsideBuilder = -1;

        ValueCommand* command = gesture.begin(QStringLiteral("edit"), [&](cmnd::ScopedMacro& macro) {
            // Reaching grabListener() at all proves the ScopedMacro is live: the
            // stack asserts on a missing macro. Capture the state BEFORE the
            // macro closes to pin the push/close order.
            listener = new CountingListener;
            macro.grabListener(listener);
            valueInsideBuilder = document;
            firesInsideBuilder = listener->executed;
            return new ValueCommand(document, 5.0f);
        });

        // (1) begin(): builder ran inside the macro, one command pushed + adopted,
        // listened exactly once at close.
        CHECK(command != nullptr, "(1) begin() returned the built command");
        CHECK(command == gesture.command(), "(1) begin() adopts the command it pushed");
        CHECK(stack.isModifiable(gesture.command()),
              "(1) right after begin the pushed command is the stack's modifiable one");
        CHECK(valueInsideBuilder == 0.0f,
              "(1) inside the builder the command has not run yet (macro still open)");
        CHECK(firesInsideBuilder == 0,
              "(1) inside the builder the grabbed listener has not fired yet");
        CHECK(listener->executed == 1,
              "(1) the listener grabbed in the builder fired exactly once when begin returned");
        CHECK(command->applyCount() == 1,
              "(1) the command executed exactly once, at macro close");
        CHECK(document == 5.0f,
              "(1) the command's effect is in the document after begin()");

        // (2) coalescing: same command, still the open edit.
        CHECK(gesture.continues(), "(2) continues() is true right after begin()");
        command->modifyValue(6.0f);
        CHECK(gesture.continues(), "(2) continues() is still true after rewriting the same command");
        CHECK(document == 6.0f, "(2) the document holds the rewritten value");

        // (3) several rewrites, then one undo takes the WHOLE gesture back.
        command->modifyValue(7.0f);
        command->modifyValue(8.0f);
        command->modifyValue(9.0f);
        CHECK(command->applyCount() == 5,
              "(3) the command re-applied for every rewrite before the undo");
        CHECK(document == 9.0f, "(3) the document holds the last rewrite before undo");

        bool undone = false;
        stack.undo(&undone);
        CHECK(undone, "(3) undo reported that it undid a step");
        CHECK(!gesture.continues(), "(3) continues() is false after stack.undo()");
        CHECK(document == 0.0f,
              "(3) undo restored the pre-edit value: the whole gesture was ONE undo step");
        CHECK(command->undoCount() == 1, "(3) the command was undone exactly once");
        CHECK(listener->undone == 1, "(3) the grabbed listener saw one undo");

        bool redone = false;
        stack.redo(&redone);
        CHECK(redone, "(3) redo reported that it redid a step");
        CHECK(!gesture.continues(), "(3) continues() is false after stack.redo()");
        CHECK(document == 9.0f, "(3) redo re-applied the final value of the gesture");
        CHECK(listener->redone == 1, "(3) the grabbed listener saw one redo");

        // (1b) the gesture pushed exactly ONE command: at the top of the history,
        // one undo reaches the start and a second undo has nothing left.
        bool oneUndo = false;
        stack.undo(&oneUndo);
        CHECK(oneUndo, "(1) one undo is available at the top of the history");
        CHECK(document == 0.0f, "(1) that one undo reaches the pre-edit value");
        bool secondUndo = true;
        stack.undo(&secondUndo);
        CHECK(!secondUndo, "(1) a second undo has nothing to undo: the gesture pushed one command");
    }

    // ---- (4) Stack::clear() also drops the gate ----
    {
        cmnd::Stack stack;
        float document = 2.0f;
        cmnd::Gesture<ValueCommand> gesture(stack);
        gesture.begin(QStringLiteral("edit"),
                      [&](cmnd::ScopedMacro&) { return new ValueCommand(document, 3.0f); });
        CHECK(gesture.continues(), "(4) the edit continues before clear()");
        stack.clear();
        CHECK(!gesture.continues(), "(4) Stack::clear() drops the modifiable link");
    }

    // ---- (5) end() closes the edit; a second edit is a second command ----
    {
        cmnd::Stack stack;
        float document = 0.0f;
        cmnd::Gesture<ValueCommand> gesture(stack);

        gesture.begin(QStringLiteral("first"),
                      [&](cmnd::ScopedMacro&) { return new ValueCommand(document, 4.0f); });
        CHECK(document == 4.0f, "(5) the first edit applied");
        gesture.end();
        CHECK(!gesture.continues(), "(5) end() closes the edit: continues() is false");

        gesture.begin(QStringLiteral("second"),
                      [&](cmnd::ScopedMacro&) { return new ValueCommand(document, 9.0f); });
        CHECK(document == 9.0f, "(5) the second edit applied");
        CHECK(gesture.continues(), "(5) the second edit is the new open edit");

        bool undo1 = false;
        stack.undo(&undo1);
        CHECK(undo1 && document == 4.0f, "(5) the first undo reverses the second edit");
        bool undo2 = false;
        stack.undo(&undo2);
        CHECK(undo2 && document == 0.0f, "(5) the second undo reverses the first edit");
        bool undo3 = true;
        stack.undo(&undo3);
        CHECK(!undo3 && document == 0.0f, "(5) a two-edit sequence needs exactly two undos");
    }

    // ---- (6) a gesture that is never committed pushes nothing ----
    {
        cmnd::Stack stack;
        float document = 7.0f;
        {
            cmnd::Gesture<ValueCommand> gesture(stack);
            CHECK(!gesture.continues(), "(6) a fresh gesture continues no edit");
            // Destroyed without begin(): must be a no-op.
        }
        bool undone = true;
        stack.undo(&undone);
        CHECK(!undone, "(6) an uncommitted gesture left nothing to undo");
        CHECK(document == 7.0f, "(6) the document is still at the start, needing zero undos");
    }

    // ---- (7) a useless command is never executed and is no undo step ----
    {
        cmnd::Stack stack;
        float document = 1.0f;
        cmnd::Gesture<ValueCommand> gesture(stack);

        gesture.begin(QStringLiteral("real"),
                      [&](cmnd::ScopedMacro&) { return new ValueCommand(document, 2.0f); });
        gesture.end();
        CHECK(document == 2.0f, "(7) the real edit applied");

        CountingListener* listener = nullptr;
        gesture.begin(QStringLiteral("useless"), [&](cmnd::ScopedMacro& macro) {
            listener = new CountingListener;
            macro.grabListener(listener);
            return new ValueCommand(document, 3.0f, true); // isUseless() == true
        });
        CHECK(document == 2.0f, "(7) a useless command's value never reaches the document");
        CHECK(gesture.command()->applyCount() == 0, "(7) a useless command is never executed");
        CHECK(listener->executed == 0, "(7) a useless commit fires no listener");
        gesture.end();

        bool undone = false;
        stack.undo(&undone);
        CHECK(undone && document == 1.0f,
              "(7) one undo skips the useless commit and reverses the real edit behind it");
        bool undoneAgain = true;
        stack.undo(&undoneAgain);
        CHECK(!undoneAgain, "(7) the useless commit contributed no undo step of its own");
    }

    std::printf("OK: cmnd::Gesture contract holds against cmnd::Stack\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    return run();
}
