#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Icons.h"
#include <QUrl>

namespace gui {

QmlTheme::QmlTheme(QObject* aParent) : QObject(aParent) {}

void QmlTheme::refresh() {
    emit themeChanged();
}

QColor QmlTheme::base() const { return theme::Colors::current().base; }
QColor QmlTheme::raised() const { return theme::Colors::current().raised; }
QColor QmlTheme::raisedHover() const { return theme::Colors::current().raisedHover; }
QColor QmlTheme::recessed() const { return theme::Colors::current().recessed; }
QColor QmlTheme::active() const { return theme::Colors::current().active; }
QColor QmlTheme::activeHover() const { return theme::Colors::current().activeHover; }
QColor QmlTheme::hairline() const { return theme::Colors::current().hairline; }
QColor QmlTheme::outline() const { return theme::Colors::current().outline; }
QColor QmlTheme::focus() const { return theme::Colors::current().focus; }
QColor QmlTheme::hover() const { return theme::Colors::current().hover; }
QColor QmlTheme::text() const { return theme::Colors::current().text; }
QColor QmlTheme::textMuted() const { return theme::Colors::current().textMuted; }
QColor QmlTheme::textDisabled() const { return theme::Colors::current().textDisabled; }
QColor QmlTheme::accent() const { return theme::Colors::current().accent; }
QColor QmlTheme::accentBright() const { return theme::Colors::current().accentBright; }
QColor QmlTheme::accentHover() const { return theme::Colors::current().accentHover; }
QColor QmlTheme::accentText() const { return theme::Colors::current().accentText; }
QColor QmlTheme::icon() const { return theme::Colors::current().icon; }

QUrl QmlTheme::closeIcon() const { return QUrl::fromLocalFile(theme::iconDir() + "/x.svg"); }

} // namespace gui
