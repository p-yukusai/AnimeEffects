#include "gui/HeaderBar.h"

#include "gui/KeyCommandMap.h"
#include "gui/MainMenuBar.h"
#include "gui/ViaPoint.h"
#include "gui/qml/DockSlotsModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/qml/ViewSettingsModel.h"
#include "gui/PopupWindow.h"
#include "gui/theme/Colors.h"

#include <QBitmap>
#include <QByteArray>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QQuickItem>
#include <QQuickWindow>
#include <QToolTip>
#include <QQmlContext>
#include <QUrl>
#include <QWindow>

#include <cstdlib>

#if AE_HAVE_XCB
#include <xcb/xcb.h>
#endif

namespace gui {

namespace {
// The header is 30px tall (the widget menu bar's height) with 26px controls centred
// in it. The font's own height adds the rest when the locale asks for a larger face,
// so a user who enlarges the dock font does not get clipped menu labels.
int headerHeightFor(const QFont& aFont) {
    return qMax(30, QFontMetrics(aFont).height() + 12);
}

#if AE_HAVE_XCB
// X11 only composites a top-level's alpha channel while a compositor owns the
// screen's _NET_WM_CM_S<n> selection. A window format can carry an alpha buffer
// without one (MEASURED under Xvfb: alphaBufferSize() is 8, and a transparent
// corner comes out black, not blended), so the format alone must not decide
// this. With a compositor the QML border keeps its antialiasing; without one the
// caller falls back to the 1-bit window mask.
bool x11CompositorActive(QWindow* aWindow) {
    auto* x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    if (!x11 || !x11->connection()) return false;
    const int screen = (aWindow && aWindow->screen())
                           ? QGuiApplication::screens().indexOf(aWindow->screen())
                           : 0;
    const QByteArray atomName = "_NET_WM_CM_S" + QByteArray::number(qMax(0, screen));
    xcb_connection_t* connection = x11->connection();
    xcb_intern_atom_reply_t* atom = xcb_intern_atom_reply(
        connection,
        xcb_intern_atom(connection, 0, static_cast<uint16_t>(atomName.size()), atomName.constData()),
        nullptr);
    if (!atom) return false;
    xcb_get_selection_owner_reply_t* owner = xcb_get_selection_owner_reply(
        connection, xcb_get_selection_owner(connection, atom->atom), nullptr);
    const bool active = owner && owner->owner != XCB_NONE;
    std::free(owner);
    std::free(atom);
    return active;
}
#endif

bool compositesPopupAlpha(QWindow* aWindow) {
    if (!aWindow || aWindow->format().alphaBufferSize() <= 0) return false;
    const QString platform = QGuiApplication::platformName();
    // Wayland's surfaces always carry their alpha to the compositor.
    if (platform.startsWith(QLatin1String("wayland"))) return true;
#if defined(Q_OS_MACOS)
    return true;
#elif AE_HAVE_XCB
    if (platform == QLatin1String("xcb")) return x11CompositorActive(aWindow);
#endif
    return false;
}
} // namespace

HeaderBar::HeaderBar(MainMenuBar& aMenu,
                     ViaPoint& aViaPoint,
                     KeyCommandMap& aKeyCommandMap,
                     const LocaleParam& aLocale,
                     QWidget* aParent):
    KeyForwardingQuickWidget(aParent),
    mTheme(new QmlTheme(this)),
    mViewSettings(new ViewSettingsModel(&aViaPoint.mainViewSetting(), this)),
    mDockSlots(new DockSlotsModel(this)) {
    setObjectName(QStringLiteral("headerBar"));
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setFixedHeight(headerHeightFor(qmlUiFont(aLocale)));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // See the class note: the window owns the command keys.
    setFocusPolicy(Qt::NoFocus);
    setClearColor(theme::Colors::current().base);

    // The rotation tooltips name the user's bindings, exactly as the Tools dock's
    // strip did - these three commands are rebindable and a stale tip is worse than
    // no tip.
    const auto binding = [&aKeyCommandMap](const char* aName) {
        auto* key = aKeyCommandMap.get(QString::fromLatin1(aName));
        return key ? key->binding.text() : QString();
    };
    mViewSettings->setKeyHints(binding("RotateCanvas15AntiClockwise"),
                               binding("ResetCanvasAngle"),
                               binding("RotateCanvas15Clockwise"));

    rootContext()->setContextProperty(QStringLiteral("mainMenu"), &aMenu);
    rootContext()->setContextProperty(QStringLiteral("viewSettings"), mViewSettings);
    rootContext()->setContextProperty(QStringLiteral("dockSlots"), mDockSlots);
    rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    rootContext()->setContextProperty(QStringLiteral("headerHost"), this);
    rootContext()->setContextProperty(QStringLiteral("uiFont"), qmlUiFont(aLocale));

    setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/HeaderBar.qml")));
    connect(this, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status aStatus) {
        if (aStatus == QQuickWidget::Error) {
            for (const QQmlError& error : errors())
                qWarning().noquote() << error.toString();
        }
    });

    connect(mViewSettings, &ViewSettingsModel::viewSettingChanged, this, &HeaderBar::viewSettingChanged);
    connect(mDockSlots, &DockSlotsModel::toggleRequested, this, [this](Qt::DockWidgetArea aArea) {
        emit dockSlotToggleRequested(int(aArea));
    });
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::showHeaderToolTip(QObject* aOwner, const QString& aText) {
    auto* item = qobject_cast<QQuickItem*>(aOwner);
    if (!item || !item->window() || aText.isEmpty())
        return;
    // Below the control, centred on it. The scene point must be the ITEM's point
    // mapped into the scene; a local (width/2, height) put every header tooltip at
    // the widget's top-left. The header's scene belongs to this QQuickWidget, whose
    // offscreen QQuickWindow does not know the widget's screen position; a popup
    // item's own window does.
    const QPointF local(item->width() / 2, item->height() + 2);
    const QPoint scenePoint = item->mapToScene(local).toPoint();
    const QPoint global = item->window() == quickWindow()
                          ? mapToGlobal(scenePoint)
                          : item->window()->mapToGlobal(scenePoint);
    QToolTip::showText(global, aText, this);
}

void HeaderBar::hideHeaderToolTip() {
    QToolTip::hideText();
}

void HeaderBar::refreshTheme() {
    mTheme->refresh();
    setClearColor(theme::Colors::current().base);
}

// The popup window's QML background is a rounded Rectangle rendered through a
// multisampled layer (HeaderMenu.qml). With a composited alpha window the
// compositor blends that layer's partial coverage directly; where the platform
// does not composite a Qt::Popup's alpha (and on Windows 11, where the system
// rounds it instead), the window itself has to be shaped, exactly as
// gui::AppStyle does for the widget menus. Qt's QQuickPopupWindow has already set
// the transparent clear color and the
// Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint flags (Qt source:
// qquickpopupwindow.cpp), so this only supplies the radius.
//
// The alpha path is preferred wherever it exists: a window region is 1-bit by
// definition, so its outermost cut cannot be antialiased. The region fallback
// still follows the same 8px outline, and the QML layer's partial pixels inside
// it keep the corner from looking like a bare staircase.
void HeaderBar::configurePopupWindow(QObject* aObject, const QString& aTitle) {
    auto* window = qobject_cast<QQuickWindow*>(aObject);
    if (!window || !(window->flags() & Qt::Popup))
        return;

    // Keep the transparent clear color and frameless popup flags explicit: a popup
    // window created by a style that does not use popup windows (Popup.Item) is not
    // this path, and a future style could hand us a differently configured one.
    window->setColor(Qt::transparent);
    // A top-level QWindow is named for window switchers and the accessibility
    // bridge by its title; an unnamed popup is an anonymous surface.
    window->setTitle(aTitle);
    constexpr int kPanelRadius = 8;
    if (usesSystemPopupRounding()) {
        // Windows 11 22H2+: let DWM round the window, so its shadow follows the
        // panel. A window region would make it un-roundable.
        roundPopupWindow(window);
        window->setMask(QRegion());
        return;
    }
    if (compositesPopupAlpha(window)) {
        // The compositor blends the QML background's own antialiased edge; a
        // region here would clip it back to binary coverage.
        window->setMask(QRegion());
        return;
    }

    QBitmap region(window->size());
    region.fill(Qt::color0);
    QPainter painter(&region);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::color1);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRect(QPoint(0, 0), window->size()), kPanelRadius, kPanelRadius);
    painter.end();
    window->setMask(QRegion(region));
}

} // namespace gui
