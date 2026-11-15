#include "gui/ImGuiInputBridge.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QWidget>

#include "imgui.h"

namespace gui {
namespace {

// ImGuiPlatformIO::Platform_SetImeDataFn carries no user-data pointer in 1.92
// (io.SetPlatformImeDataFn was renamed to platform_io.Platform_SetImeDataFn in
// 1.91.1), so the handler finds its instance through these statics. See the class
// comment in the header for the ownership policy they implement.
//
// Newest first: the head is the instance a dying target hands the slot to.
ImGuiInputBridge* sBridgeList = nullptr;
// The current IME target - the instance ImGui::EndFrame() pushes its IME data to.
// It is the instance of the most recent handler call, and it may be any member of
// sBridgeList, not necessarily its head.
ImGuiInputBridge* sImeBridge = nullptr;
// What the process-wide slot held before the first live instance took it over,
// restored when the last one goes away; only meaningful while sHandlerOwnedByUs.
using ImeDataFn = decltype(ImGuiPlatformIO::Platform_SetImeDataFn);
ImeDataFn sAppImeDataFn = nullptr;
bool sHandlerOwnedByUs = false;

bool hasContext() { return ImGui::GetCurrentContext() != nullptr; }

ImVec2 toImVec2(const QPointF& aPos) { return ImVec2(float(aPos.x()), float(aPos.y())); }

// Qt::MouseButton -> ImGui's button index (0=left, 1=right, 2=middle, 3/4 = the
// two extra buttons, which is what Qt's Back/Forward map to on the platforms that
// have them). -1 means "ImGui has no such button".
int toImGuiMouseButton(Qt::MouseButton aButton) {
    switch (aButton) {
    case Qt::LeftButton: return 0;
    case Qt::RightButton: return 1;
    case Qt::MiddleButton: return 2;
    case Qt::BackButton: return 3;
    case Qt::ForwardButton: return 4;
    default: return -1;
    }
}

// Qt reports the modifier state on every event it delivers, and ImGui wants the
// modifiers as key events (they are also written back into io.KeyCtrl etc.).
// Feeding them from mouse and key events alike is what the vendored GLFW backend
// does (ImGui_ImplGlfw_UpdateKeyModifiers() on every mouse and key callback).
//
// Qt::KeypadModifier is deliberately not translated: it is a Qt refinement of a
// key's identity (see toImGuiKey()) and not an ImGui modifier.
void pushModifiers(ImGuiIO& aIo, Qt::KeyboardModifiers aModifiers) {
    aIo.AddKeyEvent(ImGuiMod_Ctrl, (aModifiers & Qt::ControlModifier) != 0);
    aIo.AddKeyEvent(ImGuiMod_Shift, (aModifiers & Qt::ShiftModifier) != 0);
    aIo.AddKeyEvent(ImGuiMod_Alt, (aModifiers & Qt::AltModifier) != 0);
    aIo.AddKeyEvent(ImGuiMod_Super, (aModifiers & Qt::MetaModifier) != 0);
}

// Qt -> ImGui key translation. ImGui keys are physical keys named after a US
// keyboard, so this maps key CODES and never looks at the event's text (the text
// goes to io.AddInputCharactersUTF8 separately, which is how a layout produces a
// character the physical key does not name).
ImGuiKey toImGuiKey(int aKey, Qt::KeyboardModifiers aModifiers) {
    const bool keypad = aModifiers.testFlag(Qt::KeypadModifier);

    // Qt reports the keypad digits as Key_0..Key_9 plus KeypadModifier, and ImGui
    // has a separate ImGuiKey_Keypad0..9 run for them.
    if (keypad && aKey >= Qt::Key_0 && aKey <= Qt::Key_9)
        return ImGuiKey(ImGuiKey_Keypad0 + (aKey - Qt::Key_0));
    if (keypad) {
        switch (aKey) {
        case Qt::Key_Asterisk: return ImGuiKey_KeypadMultiply;
        case Qt::Key_Plus: return ImGuiKey_KeypadAdd;
        case Qt::Key_Comma: return ImGuiKey_KeypadDecimal;
        case Qt::Key_Period: return ImGuiKey_KeypadDecimal;
        case Qt::Key_Minus: return ImGuiKey_KeypadSubtract;
        case Qt::Key_Slash: return ImGuiKey_KeypadDivide;
        case Qt::Key_Equal: return ImGuiKey_KeypadEqual;
        case Qt::Key_Enter: return ImGuiKey_KeypadEnter;
        default: break;
        }
    }

    // Qt and ImGui agree on the order of these runs, so each is an offset.
    if (aKey >= Qt::Key_A && aKey <= Qt::Key_Z)
        return ImGuiKey(ImGuiKey_A + (aKey - Qt::Key_A));
    if (aKey >= Qt::Key_0 && aKey <= Qt::Key_9)
        return ImGuiKey(ImGuiKey_0 + (aKey - Qt::Key_0));
    // Qt::Key_F1..F35 are contiguous and ImGuiKey_F1..F24 are contiguous with the
    // same spacing.
    if (aKey >= Qt::Key_F1 && aKey <= Qt::Key_F24)
        return ImGuiKey(ImGuiKey_F1 + (aKey - Qt::Key_F1));

    switch (aKey) {
    case Qt::Key_Tab:
    case Qt::Key_Backtab: return ImGuiKey_Tab; // Shift+Tab: ImGui reads the ImGuiMod_Shift
    case Qt::Key_Left: return ImGuiKey_LeftArrow;
    case Qt::Key_Right: return ImGuiKey_RightArrow;
    case Qt::Key_Up: return ImGuiKey_UpArrow;
    case Qt::Key_Down: return ImGuiKey_DownArrow;
    case Qt::Key_PageUp: return ImGuiKey_PageUp;
    case Qt::Key_PageDown: return ImGuiKey_PageDown;
    case Qt::Key_Home: return ImGuiKey_Home;
    case Qt::Key_End: return ImGuiKey_End;
    case Qt::Key_Insert: return ImGuiKey_Insert;
    case Qt::Key_Delete: return ImGuiKey_Delete;
    case Qt::Key_Backspace: return ImGuiKey_Backspace;
    case Qt::Key_Space: return ImGuiKey_Space;
    case Qt::Key_Return:
    case Qt::Key_Enter: return ImGuiKey_Enter; // keypad Enter was taken above
    case Qt::Key_Escape: return ImGuiKey_Escape;
    // Qt does not distinguish left from right for these four (it is the native
    // key code that does). ImGui's RightXXX keys stay unused; the modifier state
    // is carried by ImGuiMod_* events and is unambiguous.
    case Qt::Key_Shift: return ImGuiKey_LeftShift;
    case Qt::Key_Control: return ImGuiKey_LeftCtrl;
    case Qt::Key_Alt: return ImGuiKey_LeftAlt;
    case Qt::Key_Meta: return ImGuiKey_LeftSuper;
    case Qt::Key_AltGr: return ImGuiKey_RightAlt;
    case Qt::Key_Menu: return ImGuiKey_Menu;
    case Qt::Key_CapsLock: return ImGuiKey_CapsLock;
    case Qt::Key_NumLock: return ImGuiKey_NumLock;
    case Qt::Key_ScrollLock: return ImGuiKey_ScrollLock;
    case Qt::Key_Print: return ImGuiKey_PrintScreen;
    case Qt::Key_Pause: return ImGuiKey_Pause;
    case Qt::Key_Apostrophe: return ImGuiKey_Apostrophe;
    case Qt::Key_Comma: return ImGuiKey_Comma;
    case Qt::Key_Minus: return ImGuiKey_Minus;
    case Qt::Key_Period: return ImGuiKey_Period;
    case Qt::Key_Slash: return ImGuiKey_Slash;
    case Qt::Key_Semicolon: return ImGuiKey_Semicolon;
    case Qt::Key_Equal: return ImGuiKey_Equal;
    case Qt::Key_BracketLeft: return ImGuiKey_LeftBracket;
    // The non-US backslash (ImGuiKey_Oem102) reports as Qt::Key_Backslash too and
    // is only distinguishable by the native scan code; ImGuiKey_Oem102 is left
    // unmapped rather than guessed.
    case Qt::Key_Backslash: return ImGuiKey_Backslash;
    case Qt::Key_BracketRight: return ImGuiKey_RightBracket;
    case Qt::Key_QuoteLeft: return ImGuiKey_GraveAccent;
    default: return ImGuiKey_None;
    }
}

// The characters this event contributes, with the control codes removed. Qt hands
// out control characters in QKeyEvent::text() for shortcuts (Ctrl+C is text()
// == "\x03"), and GLFW's character callback - the model every ImGui backend
// follows for text - never reports control characters. Passing them on would put
// 0x03 into an ImGui InputText buffer for a Ctrl+C that the application should
// have handled itself.
QString printableOnly(const QString& aText) {
    QString out;
    out.reserve(aText.size());
    for (const QChar c : aText) {
        const ushort u = c.unicode();
        if (u >= 0x20 && u != 0x7f)
            out.append(c);
    }
    return out;
}

} // namespace

//-------------------------------------------------------------------------------------------------
ImGuiInputBridge::ImGuiInputBridge(QWidget& aWidget):
    mWidget(aWidget),
    mPreedit(),
    mImeCursorRectangle(),
    mImeWantsTextInput(false),
    mEmulatedTouchButton(false),
    mNextBridge(sBridgeList) {
    sBridgeList = this;
    ensureWired();
}

ImGuiInputBridge::~ImGuiInputBridge() {
    // Leave the live-bridge registry. The head is the instance a dying target hands
    // the slot to, so this runs even when no ImGui context exists.
    if (sBridgeList == this) {
        sBridgeList = mNextBridge;
    } else {
        for (ImGuiInputBridge* it = sBridgeList; it != nullptr; it = it->mNextBridge) {
            if (it->mNextBridge == this) {
                it->mNextBridge = mNextBridge;
                break;
            }
        }
    }
    mNextBridge = nullptr;

    if (sImeBridge != this) {
        // Another instance is the IME target: the slot is not this instance's to
        // touch. Clearing the handler here used to un-wire a survivor that
        // sImeBridge still named.
        return;
    }

    if (sBridgeList != nullptr) {
        // This instance was the target and a survivor exists: hand it the slot and
        // re-install our handler (something else may have taken the slot while this
        // instance was alive). The survivor then needs no further event to keep
        // receiving IME data.
        sImeBridge = sBridgeList;
        if (hasContext())
            ImGui::GetPlatformIO().Platform_SetImeDataFn = &ImGuiInputBridge::platformImeData;
        return;
    }

    // Last live instance. Only clear what is ours: an application that installed its
    // own handler after this one took the slot keeps it.
    sImeBridge = nullptr;
    if (hasContext()) {
        ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
        if (sHandlerOwnedByUs && platform.Platform_SetImeDataFn == &ImGuiInputBridge::platformImeData)
            platform.Platform_SetImeDataFn = sAppImeDataFn;
    }
    sAppImeDataFn = nullptr;
    sHandlerOwnedByUs = false;
}

void ImGuiInputBridge::ensureWired() {
    sImeBridge = this;
    if (!hasContext())
        return;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    if (!sHandlerOwnedByUs) {
        // No live instance holds the slot yet: remember what was there (the
        // application's handler, or ImGui's default) so the last destructor can put
        // it back. A later instance must NOT overwrite this with our own handler.
        sAppImeDataFn = platform.Platform_SetImeDataFn;
        sHandlerOwnedByUs = true;
    }
    platform.Platform_SetImeDataFn = &ImGuiInputBridge::platformImeData;
}

//-------------------------------------------------------------------------------------------------
bool ImGuiInputBridge::handleMouseMove(const QMouseEvent& aEvent) {
    if (!hasContext())
        return false;
    ensureWired(); // the context may not have existed when this bridge was built
    ImGuiIO& io = ImGui::GetIO();
    // Qt compresses mouse moves (Qt::AA_CompressHighFrequencyEvents, on by default
    // where the platform sends more moves than the event loop drains), so what
    // arrives here is the latest position, not every one. That loses nothing:
    // ImGui::AddMousePosEvent() drops a position equal to the last queued one, and
    // ImGui::UpdateInputEvents() applies mouse positions by assignment - only the
    // last position in a frame is observable to the overlay either way.
    const ImVec2 pos = toImVec2(aEvent.position());
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(pos.x, pos.y);
    pushModifiers(io, aEvent.modifiers());
    return mouseCaptured();
}

bool ImGuiInputBridge::handleMouseButton(const QMouseEvent& aEvent) {
    if (!hasContext())
        return false;
    ensureWired(); // the context may not have existed when this bridge was built
    ImGuiIO& io = ImGui::GetIO();
    const bool pressed = (aEvent.type() != QEvent::MouseButtonRelease);
    // Position before button: the press is attributed to the window under the
    // position carried with it.
    const ImVec2 pos = toImVec2(aEvent.position());
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(pos.x, pos.y);
    const int button = toImGuiMouseButton(aEvent.button());
    if (button >= 0)
        io.AddMouseButtonEvent(button, pressed);
    pushModifiers(io, aEvent.modifiers());
    return mouseCaptured();
}

bool ImGuiInputBridge::handleWheel(const QWheelEvent& aEvent) {
    if (!hasContext())
        return false;
    ensureWired(); // the context may not have existed when this bridge was built
    ImGuiIO& io = ImGui::GetIO();
    // Qt's QWheelEvent::angleDelta() is in eighths of a degree and one mouse wheel
    // notch is 120 units; ImGui's wheel is in notches. Both axes use the same sign
    // for "the wheel was rotated forwards/right", and ImGui's backends pass the
    // platform's raw offsets through unchanged (ImGui_ImplGlfw passes glfw's
    // xoffset/yoffset straight to AddMouseWheelEvent), so no sign flip.
    //
    // With io.ConfigInputTrickleEventQueue left at its default (true),
    // UpdateInputEvents() stops feeding the queue once an event of another kind
    // was applied in the same NewFrame(): a wheel queued in the same frame as a
    // move lands in the NEXT frame's io.MouseWheel. Measured in
    // tests/imgui_input_bridge.cpp; nothing is lost, it is latched one frame late.
    const QPoint angle = aEvent.angleDelta();
    const ImVec2 pos = toImVec2(aEvent.position());
    io.AddMouseSourceEvent(ImGuiMouseSource_Mouse);
    io.AddMousePosEvent(pos.x, pos.y);
    io.AddMouseWheelEvent(float(angle.x()) / 120.0f, float(angle.y()) / 120.0f);
    pushModifiers(io, aEvent.modifiers());
    return mouseCaptured();
}

bool ImGuiInputBridge::handleKey(const QKeyEvent& aEvent) {
    if (!hasContext())
        return false;
    ensureWired(); // the context may not have existed when this bridge was built
    ImGuiIO& io = ImGui::GetIO();
    const bool pressed = (aEvent.type() != QEvent::KeyRelease);
    const ImGuiKey key = toImGuiKey(aEvent.key(), aEvent.modifiers());
    if (key != ImGuiKey_None)
        io.AddKeyEvent(key, pressed);
    pushModifiers(io, aEvent.modifiers());
    if (pressed) {
        // Characters are fed on every press INCLUDING Qt's auto-repeats: that is
        // what makes a held key repeat in an ImGui text field, and it is the
        // equivalent of GLFW's character callback firing for repeats while
        // ImGui_ImplGlfw_KeyCallback() drops GLFW_REPEAT.
        //
        // The repeat does NOT need to reach the key state: ImGui::AddKeyAnalogEvent()
        // drops a key event whose down state is unchanged ("Filter duplicate"), and
        // ImGui's own typematic engine (IsKeyPressed(key, repeat=true) over
        // io.KeyRepeatDelay/io.KeyRepeatRate) derives the repeats from the sustained
        // down state. So a repeated press costs one filtered call and nothing else.
        const QString text = printableOnly(aEvent.text());
        if (!text.isEmpty())
            io.AddInputCharactersUTF8(text.toUtf8().constData());
    }
    return keyboardCaptured();
}

//-------------------------------------------------------------------------------------------------
bool ImGuiInputBridge::handleInputMethod(const QInputMethodEvent& aEvent) {
    if (!hasContext())
        return false;
    ensureWired(); // the context may not have existed when this bridge was built
    ImGuiIO& io = ImGui::GetIO();

    // The pre-edit is Qt's composition: the platform input context draws it (and
    // the candidate window), and ImGui has no pre-edit concept at all. Feeding it
    // to ImGui as characters would insert the composition and then insert the
    // commit as well. Keep it so the widget side can draw/underline it and answer
    // inputMethodQuery(), and insert nothing.
    mPreedit = aEvent.preeditString();

    // The commit is the finished text. Qt's replacementStart()/replacementLength()
    // describe which part of the pre-edit this replaces; nothing of the pre-edit
    // was ever inserted into ImGui, so the whole commit string is the insertion
    // and the replacement range has no counterpart to apply to.
    const QString commit = aEvent.commitString();
    if (!commit.isEmpty())
        io.AddInputCharactersUTF8(commit.toUtf8().constData());

    // An event that reached the widget means Qt already considers this widget its
    // input item; the answer follows the same policy as the keyboard for
    // consistency (an active ImGui text field owns the key events too).
    return keyboardCaptured();
}

QVariant ImGuiInputBridge::inputMethodQuery(Qt::InputMethodQuery aQuery) const {
    switch (aQuery) {
    case Qt::ImEnabled:
        // Qt asks whether this input item accepts input-method events. Answer it
        // with "there is a text field" rather than "the widget is enabled", so a
        // focused viewport with no ImGui text field does not raise an IME. The
        // integrator can flip this to mWidget.isEnabled() if their platform
        // caches the answer and refuses to ask again (see handleDprChanged()'s
        // sibling: the ImGui callback pushes update(Qt::ImCursorRectangle) on
        // every change of the IME data, which is what re-asks).
        return mWidget.isEnabled() && mImeWantsTextInput;
    case Qt::ImCursorRectangle:
        return mImeCursorRectangle;
    case Qt::ImAnchorRectangle:
        // No selection inside the ImGui text field is visible to us.
        return mImeCursorRectangle;
    case Qt::ImCursorPosition:
        // Relative to ImSurroundingText, which is empty below.
        return mPreedit.size();
    case Qt::ImAnchorPosition:
        return 0;
    case Qt::ImSurroundingText:
        // The overlay's text belongs to ImGui's InputText and ImGui does not
        // publish it. A real IME that needs the surrounding text (to build a
        // candidate list from it) would need the integrator to hand the buffer
        // here; empty is the honest answer and not a stand-in for the text.
        return QString();
    case Qt::ImHints:
        // No hints: ImGui's InputText flags (password, digits only, ...) are not
        // visible from here either.
        return QVariant::fromValue(Qt::ImhNone);
    case Qt::ImInputItemClipRectangle:
        return QRectF(mWidget.rect());
    default:
        return QVariant();
    }
}

//-------------------------------------------------------------------------------------------------
bool ImGuiInputBridge::handleTablet(const QTabletEvent& aEvent) {
    if (!hasContext())
        return false;
    ensureWired(); // the context may not have existed when this bridge was built
    ImGuiIO& io = ImGui::GetIO();
    const bool pressed = (aEvent.type() == QEvent::TabletPress);
    const bool released = (aEvent.type() == QEvent::TabletRelease);

    // A pen is a pointer to ImGui, so it goes in through the mouse API - and
    // ImGuiMouseSource_Pen is what tells ImGui the pointer is a pressure pen.
    //
    // Qt synthesises a mouse event for a tablet event that the widget does NOT
    // accept, so the widget must accept what this bridge consumed, or the same
    // gesture arrives twice; the application's MainDisplayWidget::tabletEvent()
    // already ends in aEvent->accept().
    const ImVec2 pos = toImVec2(aEvent.position());
    io.AddMouseSourceEvent(ImGuiMouseSource_Pen);
    io.AddMousePosEvent(pos.x, pos.y);
    if (pressed || released) {
        const int button = toImGuiMouseButton(aEvent.button() != Qt::NoButton ? aEvent.button() : Qt::LeftButton);
        if (button >= 0)
            io.AddMouseButtonEvent(button, pressed);
    }
    pushModifiers(io, aEvent.modifiers());

    // Dropped because ImGui has no API for any of it: pressure(), xTilt(), yTilt(),
    // tangentialPressure(), rotation(), z().
    return mouseCaptured();
}

bool ImGuiInputBridge::handleTouch(const QTouchEvent& aEvent) {
    if (!hasContext())
        return false;
    ensureWired(); // the context may not have existed when this bridge was built
    ImGuiIO& io = ImGui::GetIO();

    if (aEvent.pointCount() != 1) {
        // More than one finger is not a pointer: the application's own gesture
        // handling owns it, and this returns false so it can act. If the first
        // finger had been emulated as a left button, release it here - otherwise
        // ImGui would be left with the button down for the rest of the gesture.
        // (The point count can also be 0; that is not a pointer either.)
        if (mEmulatedTouchButton) {
            io.AddMouseButtonEvent(0, false);
            mEmulatedTouchButton = false;
        }
        return false;
    }

    // QPointerEvent::point() has no const overload in Qt 6.11, and this handler
    // never mutates the event, so read the point list directly (pointCount() == 1
    // was established above).
    const QEventPoint& point = aEvent.points().constFirst();
    const ImVec2 pos = toImVec2(point.position());
    io.AddMouseSourceEvent(ImGuiMouseSource_TouchScreen);
    io.AddMousePosEvent(pos.x, pos.y);
    switch (point.state()) {
    case QEventPoint::Pressed:
        io.AddMouseButtonEvent(0, true);
        mEmulatedTouchButton = true;
        break;
    case QEventPoint::Released:
        io.AddMouseButtonEvent(0, false);
        mEmulatedTouchButton = false;
        break;
    default:
        // Updated/Stationary/Unknown: a move, which the position already carried.
        break;
    }
    return mouseCaptured();
}

//-------------------------------------------------------------------------------------------------
void ImGuiInputBridge::handleFocus(bool aFocused) {
    if (!hasContext())
        return;
    ensureWired();
    ImGuiIO& io = ImGui::GetIO();
    // Application window focus, which is what io.AddFocusEvent() means and what
    // the widget's focusInEvent/focusOutEvent (MainDisplayWidget) forward here.
    // On focus loss ImGui clears its own key and mouse state
    // (UpdateInputEvents() -> ClearInputKeys()/ClearInputMouse()).
    io.AddFocusEvent(aFocused);
    if (!aFocused) {
        mPreedit.clear();
        mEmulatedTouchButton = false;
        if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) && QGuiApplication::inputMethod())
            QGuiApplication::inputMethod()->reset();
    }
}

void ImGuiInputBridge::handleDprChanged(qreal aDpr) {
    if (aDpr <= 0.0 || !hasContext())
        return;
    ensureWired();
    ImGuiIO& io = ImGui::GetIO();

    // DisplaySize stays in logical pixels (ImGui's own convention: the GLFW
    // backend sets DisplaySize to the window size and DisplayFramebufferScale to
    // framebuffer/window), so a DPI change moves the scale, not the size. The
    // frame loop keeps io.DisplaySize equal to the widget's logical size.
    io.DisplayFramebufferScale = ImVec2(float(aDpr), float(aDpr));

    // Dear ImGui 1.92, measured against the vendored sources and in
    // tests/imgui_input_bridge.cpp:
    //  - with ImGuiBackendFlags_RendererHasTextures (what
    //    ImGui_ImplOpenGL3_Init() sets - the application's backend is on this
    //    path), this one line is all that is needed: imgui.cpp:15820 copies
    //    io.DisplayFramebufferScale into the main viewport, and every
    //    ImGui::Begin() sets g.FontRasterizerDensity from the viewport's
    //    FramebufferScale (imgui.cpp:4605), so the dynamic atlas re-bakes the
    //    glyphs at the new density and asks the backend for a texture update.
    //  - without that backend flag the atlas was baked once at font-load time and
    //    cannot rebake: at a new scale ImGui::GetFontBaked() returns NULL and text
    //    has no glyphs. That case is what ImFontConfig::RasterizerDensity is for
    //    (its own comment in imgui.h calls it the legacy knob): set it when adding
    //    the fonts, and rebuild the atlas on a DPI change.
    //  - io.FontGlobalScale is obsolete (moved to style.FontScaleMain in 1.92,
    //    imgui.cpp:10883 asserts if it is used), and ImFont::Scale is legacy too.
}

//-------------------------------------------------------------------------------------------------
bool ImGuiInputBridge::keyboardCaptured() {
    if (!hasContext())
        return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

bool ImGuiInputBridge::mouseCaptured() {
    if (!hasContext())
        return false;
    return ImGui::GetIO().WantCaptureMouse;
}

//-------------------------------------------------------------------------------------------------
void ImGuiInputBridge::platformImeData(ImGuiContext*, ImGuiViewport*, ImGuiPlatformImeData* aData) {
    if (!sImeBridge || !aData)
        return;
    sImeBridge->onPlatformImeData(aData);
}

void ImGuiInputBridge::onPlatformImeData(const void* aData) {
    const ImGuiPlatformImeData& data = *static_cast<const ImGuiPlatformImeData*>(aData);
    mImeWantsTextInput = data.WantTextInput;

    // ImGui's InputPos is in main-viewport screen coordinates, the same space as
    // GetCursorScreenPos() and io.DisplaySize - logical pixels, with the viewport
    // at (0,0) when the overlay covers the widget - so it is already the widget's
    // coordinate space. InputPos points one pixel left of the caret and one line
    // HEIGHT above it (imgui_widgets.cpp:5624 uses cursor_screen_pos.y - FontSize),
    // so the rectangle is anchored at InputPos with the line height as its height.
    mImeCursorRectangle = QRectF(qreal(data.InputPos.x), qreal(data.InputPos.y), 1.0, qreal(data.InputLineHeight));

    // Tell Qt the caret moved. update(Qt::ImCursorRectangle) makes the platform
    // input context re-query the focus object, and that query lands in
    // ImGuiInputBridge::inputMethodQuery(Qt::ImCursorRectangle) through the
    // widget's own override - which is how the candidate window is placed.
    //
    // QInputMethod has no setCursorRectangle() in Qt 6.11 (verified in
    // /usr/include/qt6/QtGui/qinputmethod.h: only update()/reset()/commit() plus
    // getters), so the query round-trip is the only public mechanism. It requires
    // the widget to be Qt's focus object while a text field is active; if some
    // other widget holds focus, Qt asks that one instead.
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) && QGuiApplication::inputMethod())
        QGuiApplication::inputMethod()->update(Qt::ImCursorRectangle);
}

} // namespace gui
