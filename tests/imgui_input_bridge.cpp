// The viewport overlay's input bridge, measured headlessly.
//
// gui::ImGuiInputBridge translates the Qt events a viewport widget receives into
// Dear ImGui's input queue and answers whether the overlay consumed them. This
// file drives the bridge with synthetic Qt events around real ImGui frames and
// prints what each step observed. The frame here is deliberately the LEGACY one:
// a context, io.DisplaySize, a built font atlas (GetTexDataAsRGBA32, the path
// imgui.h still documents for backends without
// ImGuiBackendFlags_RendererHasTextures) and NewFrame()/Render() with no renderer
// backend at all - no GL, no window.
//
// What this can and cannot show:
//  - it CAN show the translation (keys, characters, buttons, wheel, DPI), the
//    consumption policy against a real active ImGui InputText, the platform IME
//    callback's caret rectangle, Qt's touch point translation and ImGui's own
//    duplicate-key filtering, because all of those are observations of ImGui
//    state;
//  - it CANNOT show a real IME session (no platform input context exists under
//    the offscreen plugin, so nothing asks the focus object for
//    Qt::ImCursorRectangle - the bridge's recorded rectangle is asserted
//    instead), multi-touch gestures, or pen pressure/tilt/rotation (ImGui has no
//    API for them - the bridge drops them).
//
// Run: QT_QPA_PLATFORM=offscreen /tmp/inputbridgetest
#include <QApplication>
#include <QCoreApplication>
#include <QEventPoint>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "gui/ImGuiInputBridge.h"
#include "imgui.h"

namespace {

int gChecks = 0;
int gFailures = 0;

void check(bool aCondition, const QString& aWhat) {
    ++gChecks;
    if (!aCondition)
        ++gFailures;
    printf("  [%s] %s\n", aCondition ? "ok  " : "FAIL", qPrintable(aWhat));
}

// A stand-in for a handler "the application" installed before any bridge existed
// (or after one took the slot). Counted, so the policy can be observed
// functionally and not just by comparing pointers.
int gApplicationImeDataCalls = 0;
void applicationImeData(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData*) { ++gApplicationImeDataCalls; }

constexpr float kDisplayW = 400.0f;
constexpr float kDisplayH = 300.0f;
// The overlay window the scene draws. Everything outside it is "the application".
constexpr float kWindowX = 10.0f;
constexpr float kWindowY = 20.0f;
constexpr float kWindowW = 200.0f;
constexpr float kWindowH = 80.0f;

struct Scene {
    bool showInputText = false;
    bool requestFocus = false;
};

char gBuffer[64] = "";
ImVec2 gItemMin;
ImVec2 gItemMax;
// The character queue as the frame saw it: ImGuiInputTextState clears
// io.InputQueueCharacters once it has consumed them (imgui_widgets.cpp:5070), so
// the queue has to be read after NewFrame() and before the scene is drawn.
ImVector<ImWchar> gQueueThisFrame;

void beginFrame(const Scene& aScene) {
    ImGuiIO& io = ImGui::GetIO();
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    gQueueThisFrame = io.InputQueueCharacters;
    ImGui::SetNextWindowPos(ImVec2(kWindowX, kWindowY));
    ImGui::SetNextWindowSize(ImVec2(kWindowW, kWindowH));
    ImGui::Begin("overlay");
    if (aScene.showInputText) {
        if (aScene.requestFocus)
            ImGui::SetKeyboardFocusHere();
        ImGui::InputText("##field", gBuffer, sizeof(gBuffer));
        gItemMin = ImGui::GetItemRectMin();
        gItemMax = ImGui::GetItemRectMax();
    }
    ImGui::End();
}

void endFrame() { ImGui::Render(); }

void frame(const Scene& aScene) {
    beginFrame(aScene);
    endFrame();
}

// Frames with no new input, which is also what lets ImGui's trickled event queue
// drain (UpdateInputEvents() breaks out of the queue once another kind of event
// was applied in the same frame and leaves the rest for the next one).
void drain(int aFrames) {
    const Scene scene;
    for (int i = 0; i < aFrames; ++i)
        frame(scene);
}

// Frames until nothing in the overlay is active any more (an item that stops
// being submitted loses the active id at the next NewFrame()).
void settle() {
    const Scene scene;
    for (int i = 0; i < 8; ++i) {
        frame(scene);
        if (!ImGui::IsAnyItemActive())
            return;
    }
}

QKeyEvent keyPress(int aKey, Qt::KeyboardModifiers aModifiers, const QString& aText, bool aAutoRepeat = false) {
    return QKeyEvent(QEvent::KeyPress, aKey, aModifiers, aText, aAutoRepeat, 1);
}

QKeyEvent keyRelease(int aKey, Qt::KeyboardModifiers aModifiers) {
    return QKeyEvent(QEvent::KeyRelease, aKey, aModifiers, QString(), false, 1);
}

// A viewport widget that accepts touch and records what Qt delivered to it.
class Viewport: public QWidget {
public:
    explicit Viewport(QWidget* aParent): QWidget(aParent) {
        setAttribute(Qt::WA_AcceptTouchEvents);
    }

    bool sawTouch = false;
    QPointF lastTouchPosition;

protected:
    bool event(QEvent* aEvent) override {
        switch (aEvent->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel: {
            auto* touch = static_cast<QTouchEvent*>(aEvent);
            sawTouch = true;
            if (touch->pointCount() > 0)
                lastTouchPosition = touch->points().constFirst().position();
            touch->accept();
            return true;
        }
        default:
            return QWidget::event(aEvent);
        }
    }
};

bool queueHas(const ImVector<ImWchar>& aQueue, const QString& aText) {
    if (aQueue.Size != aText.size())
        return false;
    for (int i = 0; i < aQueue.Size; ++i)
        if (aQueue[i] != aText.at(i).unicode())
            return false;
    return true;
}

//-------------------------------------------------------------------------------------------------
void sectionKeyTranslation(gui::ImGuiInputBridge& aBridge) {
    printf("\n1. key translation (Qt key code -> ImGuiKey, characters, modifiers)\n");
    ImGuiIO& io = ImGui::GetIO();
    const Scene scene;

    {
        aBridge.handleKey(keyPress(Qt::Key_A, Qt::NoModifier, QStringLiteral("a")));
        beginFrame(scene);
        check(ImGui::IsKeyDown(ImGuiKey_A),
              QStringLiteral("Qt::Key_A with text 'a' -> ImGuiKey_A is down, named '%1'").arg(QString::fromLatin1(ImGui::GetKeyName(ImGuiKey_A))));
        check(queueHas(gQueueThisFrame, QStringLiteral("a")), "and one character 'a' is queued");
        endFrame();
    }
    {
        aBridge.handleKey(keyRelease(Qt::Key_A, Qt::NoModifier));
        beginFrame(scene);
        check(!ImGui::IsKeyDown(ImGuiKey_A), "KeyRelease clears the same key");
        check(gQueueThisFrame.Size == 0, "a release queues no character");
        endFrame();
    }
    {
        // One event per modifier, each carrying the full modifier state.
        struct Case { const char* name; Qt::KeyboardModifiers mods; bool ImGuiIO::*field; } cases[] = {
            {"Ctrl", Qt::ControlModifier, &ImGuiIO::KeyCtrl},
            {"Shift", Qt::ShiftModifier, &ImGuiIO::KeyShift},
            {"Alt", Qt::AltModifier, &ImGuiIO::KeyAlt},
            {"Meta", Qt::MetaModifier, &ImGuiIO::KeySuper},
        };
        for (const Case& c : cases) {
            aBridge.handleKey(keyPress(Qt::Key_B, c.mods, QString()));
            beginFrame(scene);
            check(io.*(c.field), QStringLiteral("Qt::%1Modifier -> io.Key%1 (submitted as ImGuiMod_%1)").arg(QString::fromLatin1(c.name)));
            endFrame();
        }
        aBridge.handleKey(keyPress(Qt::Key_B, Qt::NoModifier, QString()));
        beginFrame(scene);
        check(!io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !io.KeySuper, "and all four clear again");
        endFrame();
        aBridge.handleKey(keyRelease(Qt::Key_B, Qt::NoModifier));
    }
    {
        // Keypad: Qt reports Key_5 + KeypadModifier for the keypad's 5.
        aBridge.handleKey(keyPress(Qt::Key_5, Qt::KeypadModifier, QStringLiteral("5")));
        beginFrame(scene);
        check(ImGui::IsKeyDown(ImGuiKey_Keypad5), "Key_5 + KeypadModifier -> ImGuiKey_Keypad5");
        check(!ImGui::IsKeyDown(ImGuiKey_5), "and not the main-row ImGuiKey_5");
        endFrame();
        aBridge.handleKey(keyRelease(Qt::Key_5, Qt::KeypadModifier));
        aBridge.handleKey(keyPress(Qt::Key_Enter, Qt::KeypadModifier, QStringLiteral("\r")));
        beginFrame(scene);
        check(ImGui::IsKeyDown(ImGuiKey_KeypadEnter), "keypad Enter -> ImGuiKey_KeypadEnter");
        endFrame();
        aBridge.handleKey(keyRelease(Qt::Key_Enter, Qt::KeypadModifier));
    }
    {
        struct Case { int qt; ImGuiKey imgui; const char* name; } cases[] = {
            {Qt::Key_Left, ImGuiKey_LeftArrow, "Qt::Key_Left -> ImGuiKey_LeftArrow"},
            {Qt::Key_PageUp, ImGuiKey_PageUp, "Qt::Key_PageUp -> ImGuiKey_PageUp"},
            {Qt::Key_Backspace, ImGuiKey_Backspace, "Qt::Key_Backspace -> ImGuiKey_Backspace"},
            {Qt::Key_QuoteLeft, ImGuiKey_GraveAccent, "Qt::Key_QuoteLeft -> ImGuiKey_GraveAccent"},
            {Qt::Key_Period, ImGuiKey_Period, "Qt::Key_Period -> ImGuiKey_Period"},
            {Qt::Key_BracketLeft, ImGuiKey_LeftBracket, "Qt::Key_BracketLeft -> ImGuiKey_LeftBracket"},
            {Qt::Key_F12, ImGuiKey_F12, "Qt::Key_F12 -> ImGuiKey_F12"},
            {Qt::Key_Control, ImGuiKey_LeftCtrl, "Qt::Key_Control -> ImGuiKey_LeftCtrl"},
            {Qt::Key_Escape, ImGuiKey_Escape, "Qt::Key_Escape -> ImGuiKey_Escape"},
        };
        for (const Case& c : cases) {
            aBridge.handleKey(keyPress(c.qt, Qt::NoModifier, QString()));
            beginFrame(scene);
            check(ImGui::IsKeyDown(c.imgui), QString::fromLatin1(c.name));
            endFrame();
            aBridge.handleKey(keyRelease(c.qt, Qt::NoModifier));
        }
    }
    {
        // Ctrl+C hands Qt text() == "\x03"; that control code must not become a
        // character in an ImGui text field (GLFW's character callback, the model
        // every ImGui backend follows for text, never reports control characters).
        aBridge.handleKey(keyPress(Qt::Key_C, Qt::ControlModifier, QString(QChar(0x03))));
        beginFrame(scene);
        check(gQueueThisFrame.Size == 0, "Ctrl+C (text() == U+0003) queues no character");
        endFrame();
        aBridge.handleKey(keyRelease(Qt::Key_C, Qt::ControlModifier));
    }
    drain(2);
}

//-------------------------------------------------------------------------------------------------
void sectionConsumption(gui::ImGuiInputBridge& aBridge) {
    printf("\n2. consumption policy (io.WantCaptureKeyboard vs the application's routing)\n");
    const Scene none;

    for (int i = 0; i < 3; ++i)
        frame(none);
    check(!ImGui::IsAnyItemActive(), "PRECONDITION: nothing in the overlay is active");
    check(!gui::ImGuiInputBridge::keyboardCaptured(), "no text field -> keyboardCaptured() is false");
    {
        const QKeyEvent press = keyPress(Qt::Key_G, Qt::NoModifier, QStringLiteral("g"));
        check(!aBridge.handleKey(press), "handleKey returns false: the application MUST act on the key (MoveCanvas etc.)");
        check(!gui::ImGuiInputBridge::keyboardCaptured(), "and the flag still says so after the call");
        frame(none);
        aBridge.handleKey(keyRelease(Qt::Key_G, Qt::NoModifier));
    }

    // A real InputText, focused. Requesting focus takes a frame or two to make the
    // item active, and io.WantCaptureKeyboard is computed at the next NewFrame, so
    // this loops until BOTH are observable and then asserts them: a run that cannot
    // reach that state fails instead of silently skipping.
    int attempts = 0;
    for (; attempts < 10; ++attempts) {
        Scene scene;
        scene.showInputText = true;
        scene.requestFocus = !ImGui::IsAnyItemActive();
        frame(scene);
        if (ImGui::IsAnyItemActive() && gui::ImGuiInputBridge::keyboardCaptured())
            break;
    }
    printf("  (the InputText became the active item after %d frames)\n", attempts + 1);
    check(ImGui::IsAnyItemActive(), "PRECONDITION: the ImGui InputText really is the active item");
    check(gui::ImGuiInputBridge::keyboardCaptured(), "PRECONDITION: io.WantCaptureKeyboard is set for it");
    {
        Scene scene;
        scene.showInputText = true;
        const QKeyEvent press = keyPress(Qt::Key_X, Qt::NoModifier, QStringLiteral("x"));
        check(aBridge.handleKey(press), "handleKey returns true: a rebindable command must NOT fire here");
        frame(scene);
        check(QString::fromLatin1(gBuffer) == QStringLiteral("x"), "the keystroke really reached the InputText buffer");
        aBridge.handleKey(keyRelease(Qt::Key_X, Qt::NoModifier));
    }
}

//-------------------------------------------------------------------------------------------------
void sectionIme(gui::ImGuiInputBridge& aBridge) {
    printf("\n4. IME (commit, pre-edit, and the candidate-window caret)\n");
    Scene scene;
    scene.showInputText = true;

    check(aBridge.imeWantsTextInput(), "PRECONDITION: ImGui reported WantTextInput to the platform");
    const QRectF caret = aBridge.imeCursorRectangle();
    printf("  ImGui pushed the caret rect as %.0f,%.0f %.0fx%.0f; the InputText item is %.0f,%.0f..%.0f,%.0f; ImGui font size %.0f\n",
           caret.x(), caret.y(), caret.width(), caret.height(),
           double(gItemMin.x), double(gItemMin.y), double(gItemMax.x), double(gItemMax.y), double(ImGui::GetFontSize()));
    check(caret.height() == double(ImGui::GetFontSize()),
          "the caret rect height is the ImGui line height (ImGuiPlatformImeData::InputLineHeight)");
    check(caret.left() >= gItemMin.x - 2.0 && caret.left() <= gItemMax.x + 2.0 &&
              caret.top() >= gItemMin.y - 1.0 && caret.top() <= gItemMax.y + 1.0,
          "and it lies inside the InputText item's rectangle, i.e. it matches the ImGui caret");
    {
        const QVariant answer = aBridge.inputMethodQuery(Qt::ImCursorRectangle);
        check(answer.canConvert<QRectF>() && answer.toRectF() == caret,
              "inputMethodQuery(Qt::ImCursorRectangle) answers the widget's candidate-window rect with it");
        check(aBridge.inputMethodQuery(Qt::ImEnabled).toBool(), "inputMethodQuery(Qt::ImEnabled) is true while a field wants text");
    }

    // The consumed/not-consumed answer, over the field and away from it, behind its
    // preconditions. It must track io.WantCaptureKeyboard, because an IME event that
    // reached the widget while an ImGui field owns the keyboard belongs to ImGui.
    check(gui::ImGuiInputBridge::keyboardCaptured(),
          "PRECONDITION: an ImGui text field really is receiving the keyboard");

    // The pre-edit is Qt's composition and must NOT reach ImGui's text.
    {
        QInputMethodEvent preedit(QStringLiteral("\u306b\u307b\u3093"), QList<QInputMethodEvent::Attribute>());
        check(aBridge.handleInputMethod(preedit),
              "a pre-edit while a field is active is consumed: the application must not act on the IME either");
        check(aBridge.preeditString() == QStringLiteral("\u306b\u307b\u3093"), "the pre-edit is recorded as the composition");
        beginFrame(scene);
        check(gQueueThisFrame.Size == 0, "the pre-edit does NOT reach io.InputQueueCharacters");
        endFrame();
    }

    // The commit is the text that goes in, as UTF-8.
    {
        QInputMethodEvent commit;
        commit.setCommitString(QStringLiteral("\u65e5\u672c"));
        check(aBridge.handleInputMethod(commit), "a commit while a field is active is consumed too");
        check(aBridge.preeditString().isEmpty(), "a commit (empty pre-edit) clears the composition");
        beginFrame(scene);
        check(queueHas(gQueueThisFrame, QStringLiteral("\u65e5\u672c")),
              "the commit reaches io.InputQueueCharacters as the two code points of \u65e5\u672c");
        printf("  queue the frame saw: %d code point(s), first 0x%04X\n",
               gQueueThisFrame.Size, gQueueThisFrame.Size ? unsigned(gQueueThisFrame[0]) : 0u);
        endFrame();
        check(QString::fromUtf8(gBuffer).endsWith(QStringLiteral("\u65e5\u672c")),
              QStringLiteral("and the InputText buffer ends with the committed text ('%1')").arg(QString::fromUtf8(gBuffer)));
    }

    // The other direction: no active field, so the IME event is the application's.
    // The field has to be genuinely gone first (its active id is released at the
    // next NewFrame() once it stops being submitted).
    {
        const Scene none;
        for (int i = 0; i < 8 && (ImGui::IsAnyItemActive() || gui::ImGuiInputBridge::keyboardCaptured()); ++i)
            frame(none);
        check(!ImGui::IsAnyItemActive(), "PRECONDITION: the InputText stopped being submitted and released its active id");
        check(!gui::ImGuiInputBridge::keyboardCaptured(), "PRECONDITION: the overlay no longer captures the keyboard");
        QInputMethodEvent orphan;
        orphan.setCommitString(QStringLiteral("z"));
        check(!aBridge.handleInputMethod(orphan),
              "with no active ImGui field the IME event is passed through: handleInputMethod returns false");
        beginFrame(none);
        check(queueHas(gQueueThisFrame, QStringLiteral("z")),
              "and the text still went into ImGuiIO - only the consumption answer changed");
        endFrame();
    }
}

//-------------------------------------------------------------------------------------------------
// The platform IME slot is process-wide and its callback carries no user data, so
// exactly one live bridge can be "the IME target". These assertions drive the real
// ImGui::EndFrame() callback and pin the destruction policy documented in
// ImGuiInputBridge.h: an older bridge must not un-wire the survivor, a dying target
// hands the slot to the survivor, the handler the application had installed before
// the first bridge comes back when the last one goes away, and a handler installed
// afterwards is left alone. main() calls this before it builds its own bridge, which
// is what makes "the first bridge remembers what was in the slot" observable.
void sectionImeHandlerPolicy(QWidget& aViewport) {
    printf("\n0. IME handler policy across two bridges (ownership, survivor, restore)\n");
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    using ImeDataFn = decltype(ImGuiPlatformIO::Platform_SetImeDataFn);
    const ImeDataFn saved = platform.Platform_SetImeDataFn;
    check(saved != nullptr, "PRECONDITION: the fresh context starts with ImGui's own default handler in the slot");

    // (a) A handler the application installed BEFORE any bridge: it is replaced while
    //     a bridge owns the slot, never called, and restored when the last bridge is
    //     destroyed.
    {
        gBuffer[0] = '\0';
        platform.Platform_SetImeDataFn = &applicationImeData;
        {
            gui::ImGuiInputBridge bridge(aViewport);
            check(platform.Platform_SetImeDataFn != &applicationImeData,
                  "constructing a bridge takes the slot over from the application's handler");
            gApplicationImeDataCalls = 0;
            Scene field;
            field.showInputText = true;
            for (int i = 0; i < 10 && !bridge.imeWantsTextInput(); ++i) {
                field.requestFocus = !ImGui::IsAnyItemActive();
                frame(field);
            }
            check(ImGui::IsAnyItemActive(), "PRECONDITION: the ImGui field really is active");
            check(bridge.imeWantsTextInput(), "PRECONDITION: the bridge owns the slot and ImGui pushed WantTextInput to it");
            check(gApplicationImeDataCalls == 0,
                  "and EndFrame() never reaches the application's handler while the bridge owns the slot");
        }
        check(platform.Platform_SetImeDataFn == &applicationImeData,
              "destroying the last bridge restores the handler the application had installed (only what is ours is cleared)");
        gApplicationImeDataCalls = 0;
        {
            const Scene none; // the field's disappearance flips WantTextInput, so the callback fires
            frame(none);
            frame(none);
        }
        check(gApplicationImeDataCalls > 0, "and that restored handler really is the one ImGui::EndFrame() calls again");
    }
    platform.Platform_SetImeDataFn = saved; // leave the slot as it was found

    // (b) Two bridges: destroying the OLDER one must leave the surviving target
    //     receiving ImGui's IME data without it needing another event.
    {
        gBuffer[0] = '\0';
        auto* older = new gui::ImGuiInputBridge(aViewport);
        auto* newer = new gui::ImGuiInputBridge(aViewport); // built second -> the IME target

        Scene field;
        field.showInputText = true;
        for (int i = 0; i < 10 && !newer->imeWantsTextInput(); ++i) {
            field.requestFocus = !ImGui::IsAnyItemActive();
            frame(field);
        }
        check(newer->imeWantsTextInput(),
              "PRECONDITION: the newer bridge really is the IME target and received WantTextInput");
        const QRectF caretBefore = newer->imeCursorRectangle();
        check(caretBefore.width() == 1.0 && caretBefore.height() > 0.0,
              "PRECONDITION: and ImGui pushed a caret rectangle to it");

        delete older; // the bug: this used to null the handler the survivor still needed

        // Nothing from here to the assertions calls a method on the survivor: any
        // bridge call would re-run ensureWired() and hide exactly the un-wiring
        // this is about. The text goes in through ImGuiIO, the way any bridge would.
        ImGui::GetIO().AddInputCharactersUTF8("WWWW"); // widens the line, so the caret must move
        frame(field);
        frame(field);
        const QRectF caretAfter = newer->imeCursorRectangle();
        printf("  the survivor's caret rectangle x: %.1f before the older bridge was destroyed, %.1f after committing text\n",
               caretBefore.x(), caretAfter.x());
        check(caretAfter.x() != caretBefore.x(),
              "the surviving bridge still receives the IME callback after the older one is destroyed (its caret moved)");

        const Scene none;
        for (int i = 0; i < 8 && newer->imeWantsTextInput(); ++i)
            frame(none);
        check(!newer->imeWantsTextInput(),
              "and it is still told when the field goes away (WantTextInput false), not left with stale data");

        delete newer;
        check(platform.Platform_SetImeDataFn == saved, "the last bridge restores the slot it found");
    }

    // (c) A handler installed AFTER a bridge took the slot is not ours: destroying
    //     that bridge must not clobber it.
    {
        auto* bridge = new gui::ImGuiInputBridge(aViewport);
        platform.Platform_SetImeDataFn = &applicationImeData; // something else installs its own
        delete bridge;
        check(platform.Platform_SetImeDataFn == &applicationImeData,
              "a handler installed after the bridge took the slot is left alone when that bridge is destroyed");
    }
    platform.Platform_SetImeDataFn = saved;

    // The sections below assume gBuffer starts empty (they compare its whole
    // content against the text they typed); this section committed text into it.
    gBuffer[0] = '\0';
}

//-------------------------------------------------------------------------------------------------
void sectionMouse(gui::ImGuiInputBridge& aBridge) {
    printf("\n3. mouse (positions in widget-logical pixels, buttons, wheel notches)\n");
    ImGuiIO& io = ImGui::GetIO();
    const Scene scene;
    const QPointF outside(330, 240); // outside the overlay window, inside the widget
    const QPointF inside(120, 60);

    {
        const QMouseEvent move(QEvent::MouseMove, outside, outside + QPointF(1000, 700), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        aBridge.handleMouseMove(move);
        beginFrame(scene);
        check(io.MousePos.x == 330.0f && io.MousePos.y == 240.0f,
              "handleMouseMove -> io.MousePos is the event's logical position, unconverted");
        check(!gui::ImGuiInputBridge::mouseCaptured(), "outside every ImGui window -> mouseCaptured() is false");
        endFrame();
    }
    {
        const QMouseEvent press(QEvent::MouseButtonPress, outside, outside + QPointF(1000, 700), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        check(!aBridge.handleMouseButton(press), "a press outside the overlay returns false: the application acts on it");
        beginFrame(scene);
        check(io.MouseDown[0] && ImGui::IsMouseClicked(0), "the button still went into ImGuiIO (state is always fed)");
        endFrame();
        const QMouseEvent release(QEvent::MouseButtonRelease, outside, outside + QPointF(1000, 700), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        aBridge.handleMouseButton(release);
        beginFrame(scene);
        check(!io.MouseDown[0], "MouseButtonRelease clears io.MouseDown[0]");
        endFrame();
    }
    {
        // Move onto the overlay, let a frame see it, then press: the consumption
        // answer for that press comes from this frame's WantCaptureMouse.
        const QMouseEvent move(QEvent::MouseMove, inside, inside + QPointF(1000, 700), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        aBridge.handleMouseMove(move);
        frame(scene);
        check(gui::ImGuiInputBridge::mouseCaptured(), "hovering the overlay window -> mouseCaptured() is true");
        const QMouseEvent press(QEvent::MouseButtonPress, inside, inside + QPointF(1000, 700), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        check(aBridge.handleMouseButton(press), "the press on the overlay is consumed");
        beginFrame(scene);
        endFrame();
        aBridge.handleMouseButton(QMouseEvent(QEvent::MouseButtonRelease, inside, inside + QPointF(1000, 700), Qt::LeftButton, Qt::NoButton, Qt::NoModifier));
        frame(scene);
    }
    {
        const QMouseEvent move(QEvent::MouseMove, inside, inside + QPointF(1000, 700), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        aBridge.handleMouseMove(move);
        frame(scene);
    }
    {
        // The WHEEL is the one input the application routes by this answer
        // (AGENTS.md: while the graph panel is visible the wheel zooms the graph, so
        // the canvas must not also zoom; hiding the graph gives it back). Both
        // directions are pinned behind their precondition, so a constant
        // `return true` / `return false` fails here instead of leaving the suite
        // green.
        aBridge.handleMouseMove(QMouseEvent(QEvent::MouseMove, inside, inside + QPointF(1000, 700), Qt::NoButton, Qt::NoButton, Qt::NoModifier));
        frame(scene);
        check(gui::ImGuiInputBridge::mouseCaptured(), "PRECONDITION: the pointer really is over the ImGui overlay window");
        drain(2);
        const QWheelEvent overOverlay(inside, inside + QPointF(1000, 700), QPoint(0, 0), QPoint(0, 120),
                                      Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        check(aBridge.handleWheel(overOverlay),
              "a wheel over the overlay returns true: the graph zooms and the canvas must not zoom too");
        // handleWheel queues a position of its own before the wheel, and ImGui's
        // trickle rule only applies one event kind per NewFrame(), so the queue has
        // to drain before the next position is the one the frame sees.
        drain(3);

        aBridge.handleMouseMove(QMouseEvent(QEvent::MouseMove, outside, outside + QPointF(1000, 700), Qt::NoButton, Qt::NoButton, Qt::NoModifier));
        frame(scene);
        check(!gui::ImGuiInputBridge::mouseCaptured(), "PRECONDITION: the pointer really is outside every ImGui window");
        drain(2);
        const QWheelEvent overCanvas(outside, outside + QPointF(1000, 700), QPoint(0, 0), QPoint(0, 120),
                                     Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        check(!aBridge.handleWheel(overCanvas),
              "and a wheel outside every ImGui window returns false: the canvas zooms");
        drain(3);
    }
    {
        // Qt's angleDelta is in eighths of a degree; one notch is 120 and one Qt
        // notch upward is +1.0 in ImGui (notch away from the user = scroll up).
        const QWheelEvent up(outside, outside + QPointF(1000, 700), QPoint(0, 0), QPoint(0, 120),
                             Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        aBridge.handleWheel(up);
        beginFrame(scene);
        check(io.MouseWheel == 1.0f, "angleDelta().y() == +120 -> io.MouseWheel == +1.0 (one notch up)");
        endFrame();

        const QWheelEvent down(outside, outside + QPointF(1000, 700), QPoint(0, 0), QPoint(0, -240),
                               Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        aBridge.handleWheel(down);
        beginFrame(scene);
        check(io.MouseWheel == -2.0f, "angleDelta().y() == -240 -> io.MouseWheel == -2.0 (two notches down)");
        endFrame();

        const QWheelEvent right(outside, outside + QPointF(1000, 700), QPoint(0, 0), QPoint(120, 0),
                                Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        aBridge.handleWheel(right);
        beginFrame(scene);
        check(io.MouseWheelH == 1.0f && io.MouseWheel == 0.0f,
              "angleDelta().x() == +120 -> io.MouseWheelH == +1.0 with no vertical wheel (same sign, as ImGui's backends pass theirs through)");
        endFrame();
    }
    {
        // Measured consequence of io.ConfigInputTrickleEventQueue (default true):
        // UpdateInputEvents() stops feeding the queue once another kind of event was
        // applied in the same NewFrame(), so a wheel queued in the same frame as a
        // move is latched in the NEXT frame's io.MouseWheel. Nothing is lost.
        aBridge.handleMouseMove(QMouseEvent(QEvent::MouseMove, inside, inside + QPointF(1000, 700), Qt::NoButton, Qt::NoButton, Qt::NoModifier));
        aBridge.handleWheel(QWheelEvent(outside, outside + QPointF(1000, 700), QPoint(0, 0), QPoint(0, 120),
                                        Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false));
        beginFrame(scene);
        const float sameFrame = io.MouseWheel;
        endFrame();
        beginFrame(scene);
        const float nextFrame = io.MouseWheel;
        endFrame();
        printf("  trickle: move+wheel in one frame -> io.MouseWheel %g in that frame, %g in the next\n", double(sameFrame), double(nextFrame));
        check(sameFrame == 0.0f && nextFrame == 1.0f,
              "with trickling on, a wheel behind a move lands one frame later (measured, not lost)");
    }
    drain(2);
}

//-------------------------------------------------------------------------------------------------
void sectionTablet(gui::ImGuiInputBridge& aBridge) {
    printf("\n5. tablet (pointer position + button; pressure/tilt/rotation dropped)\n");
    ImGuiIO& io = ImGui::GetIO();
    const Scene scene;
    const QPointF inside(120, 60);
    const QPointF global = inside + QPointF(1000, 700);
    const QPointingDevice* device = QPointingDevice::primaryPointingDevice();

    // A pen move first, so the frame before the press knows the pointer is over
    // the overlay - the same one-frame rule the mouse follows.
    const QTabletEvent move(QEvent::TabletMove, device, inside, global, 0.4, 2.0f, -1.0f, 0.1f, 30.0, 0.0f,
                            Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    aBridge.handleTablet(move);
    frame(scene);
    check(gui::ImGuiInputBridge::mouseCaptured(), "PRECONDITION: the pen is over the overlay window");

    const QTabletEvent press(QEvent::TabletPress, device, inside, global, 0.7, 2.0f, -1.0f, 0.1f, 30.0, 0.0f,
                             Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    check(aBridge.handleTablet(press), "a pen press on the overlay is consumed");
    beginFrame(scene);
    check(io.MousePos.x == 120.0f && io.MousePos.y == 60.0f, "the pen press moves io.MousePos to the pen position");
    check(io.MouseDown[0] && ImGui::IsMouseClicked(0), "and presses the left button (exactly one press event)");
    check(io.MouseSource == ImGuiMouseSource_Pen, "io.MouseSource says Pen, so ImGui knows it is not a mouse");
    endFrame();

    // Qt synthesises a mouse event for a tablet event the widget did not accept.
    // Should both reach ImGui, the duplicate must not become a second press:
    // ImGuiIO::AddMouseButtonEvent() filters a button event whose state is
    // unchanged.
    const QMouseEvent duplicate(QEvent::MouseButtonPress, inside, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    check(aBridge.handleMouseButton(duplicate), "the synthesised duplicate mouse press is still consumed (the pointer is over the overlay)");
    beginFrame(scene);
    check(io.MouseDown[0] && !ImGui::IsMouseClicked(0),
          "ImGui filtered the duplicate: the button is still down and NO second press happened");
    endFrame();

    const QTabletEvent release(QEvent::TabletRelease, device, inside, global, 0.0, 2.0f, -1.0f, 0.0f, 0.0, 0.0f,
                               Qt::NoModifier, Qt::LeftButton, Qt::NoButton);
    aBridge.handleTablet(release);
    beginFrame(scene);
    check(!io.MouseDown[0], "the pen release releases the left button");
    endFrame();
    printf("  dropped by ImGui's API (no mapping exists): pressure, xTilt, yTilt, tangentialPressure, rotation, z\n");
    drain(2);
}

//-------------------------------------------------------------------------------------------------
void sectionTouch(gui::ImGuiInputBridge& aBridge, Viewport& aViewport, QWidget& aWindow) {
    printf("\n6. touch (single point emulates the pointer, multiple points are left alone)\n");
    ImGuiIO& io = ImGui::GetIO();
    const Scene scene;
    const QPointingDevice* device = QPointingDevice::primaryPointingDevice();

    // Qt translates a touch point's global position into the receiving widget's
    // local position when the event goes through the window; a QEventPoint handed
    // straight to a widget keeps position() == (0,0), and a global position that is
    // not over the widget (off the screen, off the widget) is delivered to whoever
    // is there instead. So the points are built at the global position that maps
    // back to the local position this wants, and what Qt produced is read back.
    // QWidget::windowHandle() answers for the native (top-level) widget, not for a
    // child, so the delivery goes through the window the viewport lives in.
    // Let the trickled event queue of the previous sections drain completely first,
    // so the frame count below is this event's and not leftover work.
    drain(4);
    QWindow* handle = aWindow.windowHandle();
    check(handle != nullptr, "PRECONDITION: the viewport's window has a window handle to deliver through");
    if (!handle)
        return;
    const auto pointAt = [&](QEventPoint::State aState, const QPointF& aLocal) {
        const QPointF global(aViewport.mapToGlobal(aLocal.toPoint()));
        return QEventPoint(0, aState, global, global);
    };
    const auto deliver = [&](QTouchEvent& aEvent) { QCoreApplication::sendEvent(handle, &aEvent); };

    // Deliberately not where the tablet section left the pointer (120,60): a touch
    // press whose position is the one ImGui already has is filtered out by
    // AddMousePosEvent(), and then the TouchScreen rule below has no "mouse moved"
    // in that frame to hold the button back and the button lands immediately. A
    // press at a position the pointer was not already at is the realistic case.
    const QPointF first(150, 70);
    const QPointF second(120, 60);
    const QList<QEventPoint> onePressed = {pointAt(QEventPoint::Pressed, first)};
    QTouchEvent press(QEvent::TouchBegin, device, Qt::NoModifier, onePressed);
    deliver(press);
    check(aViewport.sawTouch, "PRECONDITION: Qt delivered the touch event to the viewport widget");
    const QPointF local = press.point(0).position();
    printf("  Qt translated the touch point to widget-local %.0f,%.0f (built for %.0f,%.0f)\n",
           local.x(), local.y(), first.x(), first.y());
    check(local == first, "PRECONDITION: Qt's translation arrives at the local position it was built for");
    check(gui::ImGuiInputBridge::mouseCaptured(), "PRECONDITION: that position is over the overlay window");

    // ImGui's trickle rule for touch (#2702: a touch screen has no initial hover)
    // defers the button behind the position of the same event by one frame, so each
    // button change below shows up on the second frame.
    check(aBridge.handleTouch(press), "one touch point on the overlay is consumed");
    beginFrame(scene);
    check(io.MousePos.x == float(std::floor(local.x())) && io.MousePos.y == float(std::floor(local.y())),
          "io.MousePos is Qt's translated widget-local touch position");
    check(io.MouseSource == ImGuiMouseSource_TouchScreen, "io.MouseSource says TouchScreen");
    endFrame();
    bool pressedSeen = false;
    int framesToPress = 0;
    for (int i = 1; i <= 4 && !pressedSeen; ++i) {
        beginFrame(scene);
        pressedSeen = io.MouseDown[0] && ImGui::IsMouseClicked(0);
        endFrame();
        framesToPress = i;
    }
    printf("  the emulated button appeared in frame %d after the frame that showed its position\n", framesToPress);
    check(pressedSeen && io.MouseDown[0], "the single point presses the left button, once");
    check(framesToPress == 1,
          "and exactly one frame behind its own position: ImGui's TouchScreen trickle rule (#2702) holds the button back from the frame that applied the move");

    const QList<QEventPoint> oneMoved = {pointAt(QEventPoint::Updated, second)};
    QTouchEvent move(QEvent::TouchUpdate, device, Qt::NoModifier, oneMoved);
    deliver(move);
    aBridge.handleTouch(move);
    beginFrame(scene);
    check(io.MousePos.x == float(std::floor(move.point(0).position().x())) && io.MousePos.y == float(std::floor(move.point(0).position().y())),
          "a single-point move drags the emulated pointer");
    endFrame();

    const QList<QEventPoint> oneReleased = {pointAt(QEventPoint::Released, second)};
    QTouchEvent release(QEvent::TouchEnd, device, Qt::NoModifier, oneReleased);
    deliver(release);
    aBridge.handleTouch(release);
    frame(scene);
    beginFrame(scene);
    check(!io.MouseDown[0], "releasing the single point releases the button");
    endFrame();

    // Two points: not a pointer.
    const QList<QEventPoint> two = {pointAt(QEventPoint::Pressed, first), pointAt(QEventPoint::Pressed, second)};
    QTouchEvent pinch(QEvent::TouchBegin, device, Qt::NoModifier, two);
    deliver(pinch);
    check(pinch.pointCount() == 2, "PRECONDITION: two touch points were delivered");
    check(!aBridge.handleTouch(pinch), "two touch points are NOT consumed");
    drain(2);
    check(!io.MouseDown[0], "and no emulated button is left down");

    // A second finger arriving while one finger was emulated as the pointer: the
    // event is still not consumed, but the emulated button must not stay down.
    QTouchEvent oneAgain(QEvent::TouchBegin, device, Qt::NoModifier, {pointAt(QEventPoint::Pressed, first)});
    deliver(oneAgain);
    aBridge.handleTouch(oneAgain);
    frame(scene);
    frame(scene);
    beginFrame(scene);
    check(io.MouseDown[0], "PRECONDITION: the first finger's emulated button is down");
    endFrame();
    QTouchEvent secondFinger(QEvent::TouchBegin, device, Qt::NoModifier, two);
    deliver(secondFinger);
    check(!aBridge.handleTouch(secondFinger), "the second finger's event is not consumed either");
    frame(scene);
    beginFrame(scene);
    check(!io.MouseDown[0], "and the emulated button the first finger had pressed is released");
    endFrame();
    drain(3);
}

//-------------------------------------------------------------------------------------------------
void sectionAutoRepeatAndDuplicates(gui::ImGuiInputBridge& aBridge) {
    printf("\n8. auto-repeat and duplicate keys (what ImGui's own input model expects)\n");
    ImGuiIO& io = ImGui::GetIO();
    const Scene scene;
    printf("  io.KeyRepeatDelay = %.3fs, io.KeyRepeatRate = %.3fs\n", double(io.KeyRepeatDelay), double(io.KeyRepeatRate));

    // A duplicate press for a key that is already down changes nothing: ImGui's
    // AddKeyAnalogEvent() drops an event whose down state did not change, so the
    // typematic clock is not restarted.
    aBridge.handleKey(keyPress(Qt::Key_H, Qt::NoModifier, QStringLiteral("h")));
    beginFrame(scene);
    const bool firstPress = ImGui::IsKeyPressed(ImGuiKey_H);
    endFrame();
    aBridge.handleKey(keyPress(Qt::Key_H, Qt::NoModifier, QStringLiteral("h"))); // duplicate, not a repeat
    beginFrame(scene);
    const bool duplicatePress = ImGui::IsKeyPressed(ImGuiKey_H);
    endFrame();
    check(firstPress, "the first press of H is a press");
    check(!duplicatePress && ImGui::IsKeyDown(ImGuiKey_H),
          "a duplicate press for a key already down is filtered: still down, but not pressed again");

    // Qt's auto-repeat: the key state must NOT be re-submitted (ImGui would filter
    // it anyway) but the character must be, because that is what makes a held key
    // repeat in a text field (GLFW's character callback fires for repeats while
    // ImGui_ImplGlfw_KeyCallback() drops GLFW_REPEAT).
    aBridge.handleKey(keyPress(Qt::Key_H, Qt::NoModifier, QStringLiteral("h"), true));
    beginFrame(scene);
    check(!ImGui::IsKeyPressed(ImGuiKey_H), "an auto-repeat press does not re-trigger the key press");
    check(queueHas(gQueueThisFrame, QStringLiteral("h")), "but its character IS queued (held keys repeat text)");
    endFrame();
    aBridge.handleKey(keyRelease(Qt::Key_H, Qt::NoModifier));
    frame(scene);

    // The repeat itself comes from ImGui's typematic engine over the sustained down
    // state, not from the platform: no further events are fed below, only frames
    // with their DeltaTime.
    aBridge.handleKey(keyPress(Qt::Key_J, Qt::NoModifier, QStringLiteral("j")));
    beginFrame(scene);
    endFrame();
    const float step = io.KeyRepeatDelay / 4.0f;
    double elapsed = 0.0;
    bool repeated = false;
    int frames = 0;
    for (; frames < 64; ++frames) {
        io.DeltaTime = step;
        ImGui::NewFrame();
        ImGui::Begin("overlay");
        ImGui::End();
        elapsed += double(step);
        repeated = ImGui::IsKeyPressed(ImGuiKey_J, true);
        ImGui::Render();
        if (repeated)
            break;
    }
    printf("  ImGui reported its own repeat of J after %d frames / %.3fs of DeltaTime\n", frames + 1, elapsed);
    check(repeated, "holding a key with no further platform events still repeats, from ImGui's own timers");
    check(elapsed >= double(io.KeyRepeatDelay), "and not before io.KeyRepeatDelay");
    aBridge.handleKey(keyRelease(Qt::Key_J, Qt::NoModifier));
    frame(scene);
    beginFrame(scene);
    check(!ImGui::IsKeyDown(ImGuiKey_J), "the release clears it");
    endFrame();
    drain(2);
}

//-------------------------------------------------------------------------------------------------
void sectionDpr(gui::ImGuiInputBridge& aBridge) {
    printf("\n7. DPI (io.DisplayFramebufferScale, and what re-rasterizes the glyphs)\n");
    ImGuiIO& io = ImGui::GetIO();
    const Scene scene;

    aBridge.handleDprChanged(2.0);
    check(io.DisplayFramebufferScale.x == 2.0f && io.DisplayFramebufferScale.y == 2.0f,
          "handleDprChanged(2.0) sets io.DisplayFramebufferScale to (2,2)");
    beginFrame(scene);
    check(ImGui::GetMainViewport()->FramebufferScale.x == 2.0f,
          "the main viewport picks it up at the next NewFrame (imgui.cpp copies it there)");
    endFrame();
    check(ImGui::GetDrawData()->FramebufferScale.x == 2.0f,
          "and ImDrawData::FramebufferScale carries it to the renderer backend");
    // This harness is the legacy atlas (no ImGuiBackendFlags_RendererHasTextures),
    // which cannot bake at a new density: this is the case
    // ImFontConfig::RasterizerDensity exists for. The application is NOT on this
    // path - ImGui_ImplOpenGL3_Init() sets RendererHasTextures, measured below.
    printf("  legacy atlas at the new scale: GetFontBaked() == %s\n", ImGui::GetFontBaked() ? "a font" : "NULL");
    check(ImGui::GetFontBaked() == nullptr,
          "legacy atlas (no RendererHasTextures) cannot bake at the new density -> that knob is ImFontConfig::RasterizerDensity");
    aBridge.handleDprChanged(1.0);
    frame(scene);
    check(io.DisplayFramebufferScale.x == 1.0f, "handleDprChanged(1.0) restores it");
}

void sectionDprDynamicAtlas() {
    printf("\n7b. the application's path: a 1.92 dynamic atlas (ImGuiBackendFlags_RendererHasTextures)\n");
    // ImGui::CreateContext() only makes the new context current if none exists, so
    // switching has to be explicit.
    ImGuiContext* context = ImGui::CreateContext();
    ImGui::SetCurrentContext(context);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(kDisplayW, kDisplayH);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

    const auto runFrames = [&](int aCount) {
        for (int i = 0; i < aCount; ++i) {
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::Begin("overlay");
            ImGui::Text("glyphs");
            ImGui::End();
            ImGui::Render();
        }
    };
    runFrames(3);
    const float before = ImGui::GetFont()->CurrentRasterizerDensity;
    io.DisplayFramebufferScale = ImVec2(2.0f, 2.0f);
    runFrames(3);
    const float after = ImGui::GetFont()->CurrentRasterizerDensity;
    printf("  font CurrentRasterizerDensity at scale 1: %.2f, at scale 2: %.2f\n", double(before), double(after));
    check(before == 1.0f && after == 2.0f,
          "with RendererHasTextures the atlas density follows io.DisplayFramebufferScale by itself (glyphs rebake, no extra call needed)");
    printf("  font atlas texture status now: %d (1 = WantCreate, 2 = WantUpdates: the backend is asked to refresh the texture)\n",
           int(io.Fonts->TexData->Status));
    ImGui::DestroyContext(context);
}

} // namespace

//-------------------------------------------------------------------------------------------------
int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    printf("Qt %s, platform '%s'\n", qVersion(), QGuiApplication::platformName().toUtf8().constData());

    // The viewport is a child of a window: Qt translates a touch event's scene
    // position into the receiving widget's coordinates on the way through the
    // window (QWidgetWindow), which is the path the application has too.
    QWidget window;
    Viewport viewport(&window);
    viewport.setGeometry(0, 0, int(kDisplayW), int(kDisplayH));
    window.resize(int(kDisplayW), int(kDisplayH));
    window.show();
    viewport.show();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(kDisplayW, kDisplayH);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    ImGui::StyleColorsDark();
    unsigned char* pixels = nullptr;
    int atlasW = 0, atlasH = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasW, &atlasH);
    io.Fonts->SetTexID((ImTextureID)(intptr_t)1);
    printf("Dear ImGui %s, no renderer backend, font atlas %dx%d from GetTexDataAsRGBA32, DisplaySize %.0fx%.0f, widget %.0fx%.0f dpr %g\n",
           IMGUI_VERSION, atlasW, atlasH, double(io.DisplaySize.x), double(io.DisplaySize.y),
           double(viewport.width()), double(viewport.height()), viewport.devicePixelRatioF());

    // Before the main bridge exists: the IME handler policy's "the first bridge
    // remembers the handler that was already in the slot" is only observable with
    // no other bridge alive.
    sectionImeHandlerPolicy(viewport);

    gui::ImGuiInputBridge bridge(viewport);

    sectionKeyTranslation(bridge);
    sectionConsumption(bridge);
    sectionIme(bridge);

    // The checks below need no active ImGui item.
    settle();
    check(!ImGui::IsAnyItemActive(), "the InputText released its active id when it stopped being submitted");

    sectionMouse(bridge);
    sectionTablet(bridge);
    sectionTouch(bridge, viewport, window);
    sectionAutoRepeatAndDuplicates(bridge);
    sectionDpr(bridge);
    sectionDprDynamicAtlas();

    printf("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures ? 1 : 0;
}
