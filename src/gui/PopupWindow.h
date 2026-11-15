#ifndef GUI_POPUPWINDOW_H
#define GUI_POPUPWINDOW_H

#include <QWidget>

#ifdef Q_OS_WIN
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    // Lowercase on purpose: MinGW resolves headers case-sensitively, and only
    // windows.h exists in its sysroot.
    #include <windows.h>
    #include <dwmapi.h>
    #include <QOperatingSystemVersion>
#endif

namespace gui {

// Shaping helpers for the app's popup windows (menus, the combo drop-down, the
// easing picker, the animation-settings popup). They all paint an 8px rounded
// body over a frameless, translucent window; whether the *platform* rounds the
// window as well — and therefore whether the widget must supply a window region
// of its own — is platform dependent:
//
//  - On Windows 11 (22H2+) DWM rounds the popup once asked to, and that is what
//    makes the system-drawn shadow follow the rounded panel. A widget must then
//    NOT set a mask: Microsoft's rounded-corners guidance lists "window regions"
//    and "per-pixel alpha layering" as things that make a window un-roundable
//    "even if they call the opt-in API", so a mask silently defeats the
//    rounding — the un-masked combo drop-down is where that showed up first.
//  - Everywhere else the region mask is what keeps the corner cut clean on
//    compositors that don't composite Qt::Popup translucency, and it makes that
//    cut click-through.
//
// Both functions are safe on every platform: below Windows 11 22H2, and off
// Windows, usesSystemPopupRounding() is false and roundPopupWindow() does
// nothing, so the mask path is the one that runs.
//
// Header-only on purpose: these are a handful of lines, and a separate
// translation unit would have to be added to every test target that compiles
// the gui sources directly.

inline bool usesSystemPopupRounding() {
#ifdef Q_OS_WIN
    // DWMWA_WINDOW_CORNER_PREFERENCE exists from Windows build 22000 on;
    // 22000 is the RTM build and 22621 is 22H2, so "> 22000" means Windows 11
    // 22H2 or newer.
    return QOperatingSystemVersion::current().microVersion() > 22000;
#else
    return false;
#endif
}

// Asks the platform to round this popup window. Call it once the popup's flags
// are final (a showEvent is the natural place) — it forces the native window to
// exist, so calling it before the window flags are set would create the window
// with the wrong flags.
inline void roundPopupWindow(QWidget* aWidget) {
    if (aWidget == nullptr)
        return;
#ifdef Q_OS_WIN
    if (usesSystemPopupRounding()) {
        // winId() forces the native window to exist, which is what the
        // attribute is set on.
        const auto hwnd = reinterpret_cast<HWND>(aWidget->winId());
        constexpr DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    }
#else
    Q_UNUSED(aWidget);
#endif
}

} // namespace gui

#endif // GUI_POPUPWINDOW_H
