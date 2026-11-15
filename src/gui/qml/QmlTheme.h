#ifndef GUI_QML_QMLTHEME_H
#define GUI_QML_QMLTHEME_H

#include <QColor>
#include <QObject>
#include <QUrl>

namespace gui {

// Exposes the widget theme palette (theme::Colors) to a Qt Quick scene as
// QML-readable properties (theme.base, theme.text, theme.accent, ...).
//
// The values are read from theme::Colors::current() at get time, so the bridge
// always reflects the active theme when a property is re-read. Each property
// notifies on themeChanged(); call refresh() after a theme change so a long-lived
// (reused) scene re-reads the palette instead of keeping its first-loaded values.
// One instance is set as a QQuickWidget context property named "theme", so QML
// binds declarations like
//   Rectangle { color: theme.recessed }
// without per-control QSS.
class QmlTheme : public QObject {
    Q_OBJECT
public:
    explicit QmlTheme(QObject* aParent = nullptr);

    // Re-emit themeChanged() so every bound property is re-read from the current
    // theme. Call on each open of a reused scene (and after a theme switch).
    void refresh();

    Q_PROPERTY(QColor base READ base NOTIFY themeChanged)
    Q_PROPERTY(QColor raised READ raised NOTIFY themeChanged)
    Q_PROPERTY(QColor raisedHover READ raisedHover NOTIFY themeChanged)
    Q_PROPERTY(QColor recessed READ recessed NOTIFY themeChanged)
    Q_PROPERTY(QColor active READ active NOTIFY themeChanged)
    Q_PROPERTY(QColor activeHover READ activeHover NOTIFY themeChanged)
    Q_PROPERTY(QColor hairline READ hairline NOTIFY themeChanged)
    Q_PROPERTY(QColor outline READ outline NOTIFY themeChanged)
    Q_PROPERTY(QColor focus READ focus NOTIFY themeChanged)
    Q_PROPERTY(QColor hover READ hover NOTIFY themeChanged)
    Q_PROPERTY(QColor text READ text NOTIFY themeChanged)
    Q_PROPERTY(QColor textMuted READ textMuted NOTIFY themeChanged)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor accentBright READ accentBright NOTIFY themeChanged)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY themeChanged)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY themeChanged)
    Q_PROPERTY(QColor icon READ icon NOTIFY themeChanged)
    Q_PROPERTY(QUrl closeIcon READ closeIcon NOTIFY themeChanged)

    QColor base() const;
    QColor raised() const;
    QColor raisedHover() const;
    QColor recessed() const;
    QColor active() const;
    QColor activeHover() const;
    QColor hairline() const;
    QColor outline() const;
    QColor focus() const;
    QColor hover() const;
    QColor text() const;
    QColor textMuted() const;
    QColor textDisabled() const;
    QColor accent() const;
    QColor accentBright() const;
    QColor accentHover() const;
    QColor accentText() const;
    QColor icon() const;
    // The tinted phosphor x close glyph (theme::iconDir()/x.svg), for a
    // popover close button in QML.
    QUrl closeIcon() const;

signals:
    void themeChanged();
};

} // namespace gui

#endif // GUI_QML_QMLTHEME_H
