#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"
#include "gui/theme/Icons.h"
#include <QColor>
#include <QUrl>

namespace gui {

namespace {
// The semantic roles have no cell in the Tailwind token table (which is
// achromatic surfaces plus the selectable accent row), so they are the one
// place a literal colour is right. Tailwind red/amber: the 600 step on the
// pale theme, the 400 step on the deep one, so both clear their background.
QColor semantic(bool aDark, const char* aLightHex, const char* aDarkHex) {
    return QColor(QString::fromLatin1(aDark ? aDarkHex : aLightHex));
}
} // namespace

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
QColor QmlTheme::textMutedDim() const { return theme::Colors::current().textMutedDim; }
QColor QmlTheme::textDisabled() const { return theme::Colors::current().textDisabled; }
QColor QmlTheme::accent() const { return theme::Colors::current().accent; }
QColor QmlTheme::accentBright() const { return theme::Colors::current().accentBright; }
QColor QmlTheme::accentHover() const { return theme::Colors::current().accentHover; }
QColor QmlTheme::accentText() const { return theme::Colors::current().accentText; }
QColor QmlTheme::icon() const { return theme::Colors::current().icon; }
QColor QmlTheme::selection() const { return theme::Colors::current().selection; }
QColor QmlTheme::selectionText() const { return theme::Colors::current().selectionText; }
QColor QmlTheme::floaterBody() const { return theme::Colors::current().floaterBody; }
QColor QmlTheme::floaterEdge() const { return theme::Colors::current().floaterEdge; }

QColor QmlTheme::danger() const {
    return semantic(theme::Colors::current().isDark, "#dc2626", "#f87171");
}

QColor QmlTheme::warning() const {
    return semantic(theme::Colors::current().isDark, "#b45309", "#fbbf24");
}

QUrl QmlTheme::closeIcon() const { return QUrl::fromLocalFile(theme::iconDir() + "/x.svg"); }

QUrl QmlTheme::iconUrl(const QString& aName) const {
    return QUrl::fromLocalFile(theme::iconDir() + "/" + aName + ".svg");
}

} // namespace gui
