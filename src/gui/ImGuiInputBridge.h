#ifndef GUI_IMGUIINPUTBRIDGE_H
#define GUI_IMGUIINPUTBRIDGE_H

#include <QtCore/qnamespace.h>

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVariant>

class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QTabletEvent;
class QTouchEvent;
class QWheelEvent;
class QWidget;

// The three Dear ImGui names a platform IME handler needs. Forward-declared (with
// the exact struct-ness imgui.h uses) so this header does not drag imgui.h into
// every translation unit that includes it; the definitions live in the .cpp.
struct ImGuiContext;
struct ImGuiViewport;
struct ImGuiPlatformImeData;

namespace gui {

// Qt -> Dear ImGui input translation for the viewport overlay.
//
// Every handle*() call does two things in this order:
//   1. push the event into the current Dear ImGui context (io.AddMousePosEvent(),
//      io.AddKeyEvent(), io.AddInputCharactersUTF8(), ...) - ALWAYS, whatever the
//      answer is, because ImGui derives its own latched state from that stream;
//   2. return whether the overlay consumed the event, i.e. whether the
//      application must NOT also act on it.
//
// The consumption answer is io.WantCaptureMouse / io.WantCaptureKeyboard read at
// the last completed NewFrame(). That is one frame of latency by construction and
// that is the intended usage: ImGui computes the flags while processing the frame,
// the application reads them in its event handler between frames. A consequence
// the integrator has to know: mouse MOVE events must be fed to this bridge as they
// arrive, because a press can only be attributed correctly if the frame before it
// already knew where the pointer was hovering.
//
// Coordinate space (measured against the vendored 1.92.7 and against what the
// application's overlay already does): everything here is in the widget's LOGICAL
// (device independent) pixels, which is the space QMouseEvent::position() and
// QWheelEvent::position() already use and the space ImGui::GetCursorScreenPos()
// reports. io.DisplaySize must therefore be the widget's logical size and
// io.DisplayFramebufferScale the devicePixelRatio, which is exactly what
// ImGuiOverlay::paint() sets up in the application today. ImGui scales the draw
// projection by ImDrawData::FramebufferScale itself, and the renderer backend
// (ImGui_ImplOpenGL3) sets the GL viewport to the framebuffer size in device
// pixels from draw_data. Feeding device pixels here would make hit-testing miss
// the overlay on every screen with a ratio != 1.
//
// One instance serves one viewport widget (the application has one overlay
// viewport, but two instances can be alive at once and are supported).
//
// ---- the platform IME handler policy ----
//
// ImGuiPlatformIO::Platform_SetImeDataFn is one process-wide slot whose signature
// carries no user-data pointer in 1.92, so exactly one live instance can be
// findable as "the IME target" - the one whose imeWantsTextInput() and
// imeCursorRectangle() ImGui::EndFrame() updates. The policy is:
//
//  * Every input handler call (and construction) makes its instance the target
//    again, through ensureWired(): the widget the user is typing into owns the
//    candidate window, so the last event wins.
//  * The first live instance to take the slot remembers whatever handler was
//    already in it - the application's own, or ImGui's default. This class never
//    assumes the slot was empty before it arrived.
//  * Destroying an instance that is NOT the target touches nothing at all. An
//    older bridge going away must not un-wire the survivor the slot still names.
//  * Destroying the target while another instance survives hands the slot to the
//    newest surviving instance and re-installs the handler for it, so the survivor
//    keeps receiving want-text-input/caret updates without needing another event.
//  * Destroying the LAST live instance restores the remembered application handler,
//    but only if the slot still holds this class's handler: an application that
//    installed its own handler afterwards keeps it (only clear what is yours).
//  * All of that bookkeeping is reset with the last instance, so a bridge built
//    afterwards records the slot afresh.
//
// With no ImGui context only the target bookkeeping happens; the handler is
// installed by the first ensureWired() that finds a context.
class ImGuiInputBridge {
public:
    explicit ImGuiInputBridge(QWidget& aWidget);
    ~ImGuiInputBridge();

    // ---- the contract the integration owner wires ----

    // The mouse handlers put the position first: ImGui attributes a press to the
    // window under the position carried by that press.
    bool handleMouseMove(const QMouseEvent& aEvent);
    bool handleMouseButton(const QMouseEvent& aEvent);
    bool handleWheel(const QWheelEvent& aEvent);
    bool handleKey(const QKeyEvent& aEvent);
    bool handleInputMethod(const QInputMethodEvent& aEvent);
    bool handleTablet(const QTabletEvent& aEvent);
    bool handleTouch(const QTouchEvent& aEvent);
    void handleFocus(bool aFocused);
    void handleDprChanged(qreal aDpr);

    // io.WantCaptureKeyboard / io.WantCaptureMouse, false when no ImGui context
    // exists at all. See the class comment for when the application has to ask.
    static bool keyboardCaptured();
    static bool mouseCaptured();

    // ---- what the QWidget side has to answer ----

    // The body of the widget's inputMethodQuery() override. Qt's platform input
    // context asks the FOCUS OBJECT for these, so the widget must forward them
    // and Qt's candidate window follows the ImGui caret.
    //
    // The widget side therefore has to do three things besides forwarding: set
    // Qt::WA_InputMethodEnabled (a plain QOpenGLWidget is not an input item, so Qt
    // would never send it an input method event), take focus while an ImGui text
    // field is active (Qt asks the focus OBJECT), and accept the tablet/touch
    // events this bridge consumed so Qt does not synthesise mouse events for them.
    QVariant inputMethodQuery(Qt::InputMethodQuery aQuery) const;

    // The composition string Qt is currently showing for this widget (Qt owns the
    // rendering of it; nothing is inserted into ImGui until the commit).
    QString preeditString() const { return mPreedit; }

    // The last caret rectangle Dear ImGui asked the platform to display the
    // candidate window for, in widget-logical pixels. This is what
    // inputMethodQuery(Qt::ImCursorRectangle) answers with.
    QRectF imeCursorRectangle() const { return mImeCursorRectangle; }

    // What Dear ImGui last reported as "a widget wants text input"
    // (ImGuiPlatformImeData::WantTextInput, i.e. io.WantTextInput).
    bool imeWantsTextInput() const { return mImeWantsTextInput; }

private:
    // ImGuiPlatformIO::Platform_SetImeDataFn - called from ImGui::EndFrame() when
    // the IME data changed. It is a static member so it can reach the private
    // state of the active instance (the handler signature has no user data).
    static void platformImeData(ImGuiContext* aContext, ImGuiViewport* aViewport, ImGuiPlatformImeData* aData);

    void onPlatformImeData(const void* aData);
    // Makes this instance the IME target and (re)installs the platform IME
    // handler; safe to call before an ImGui context exists, in which case it only
    // makes this instance the target (see the class comment for the full policy).
    void ensureWired();

    QWidget& mWidget;
    QString mPreedit;
    QRectF mImeCursorRectangle;
    bool mImeWantsTextInput;
    // A touch press was emulated as a left mouse button and has not been released
    // yet. Used to keep ImGui from being left with a stuck button when a second
    // finger turns the gesture into something this bridge does not emulate.
    bool mEmulatedTouchButton;
    // The live-bridge registry (newest first, head in the .cpp's sBridgeList): the
    // destructor needs to find the surviving instance to hand the IME slot to. See
    // the class comment for the full policy.
    ImGuiInputBridge* mNextBridge;
};

} // namespace gui

#endif // GUI_IMGUIINPUTBRIDGE_H
