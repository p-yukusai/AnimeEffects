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
    Q_PROPERTY(QColor textMutedDim READ textMutedDim NOTIFY themeChanged)
    Q_PROPERTY(QColor textDisabled READ textDisabled NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY themeChanged)
    Q_PROPERTY(QColor accentBright READ accentBright NOTIFY themeChanged)
    Q_PROPERTY(QColor accentHover READ accentHover NOTIFY themeChanged)
    Q_PROPERTY(QColor accentText READ accentText NOTIFY themeChanged)
    Q_PROPERTY(QColor icon READ icon NOTIFY themeChanged)
    Q_PROPERTY(QColor selection READ selection NOTIFY themeChanged)
    Q_PROPERTY(QColor selectionText READ selectionText NOTIFY themeChanged)
    Q_PROPERTY(QColor floaterBody READ floaterBody NOTIFY themeChanged)
    Q_PROPERTY(QColor floaterEdge READ floaterEdge NOTIFY themeChanged)
    // Semantic colors. Deliberately NOT accent tokens: the accent is the user's
    // chosen hue (it can be green), and an error has to read as an error whatever
    // that hue is. They are tokens in theme::Colors now (red/amber rows at their
    // own theme column), not literals here, so the command log's warning badge
    // and these properties cannot disagree.
    Q_PROPERTY(QColor danger READ danger NOTIFY themeChanged)
    // The MATTE's own hue, from the same key-type table the timeline inks its keys
    // with: a mask key dot in the panel's text colour read as "no state" beside every
    // other channel's hue.
    Q_PROPERTY(QColor maskKey READ maskKey NOTIFY themeChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY themeChanged)
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
    QColor textMutedDim() const;
    QColor textDisabled() const;
    QColor accent() const;
    QColor accentBright() const;
    QColor accentHover() const;
    QColor accentText() const;
    QColor icon() const;
    QColor selection() const;
    QColor selectionText() const;
    QColor floaterBody() const;
    QColor floaterEdge() const;
    QColor danger() const;
    QColor maskKey() const;
    QColor warning() const;
    // The tinted phosphor x close glyph (theme::iconDir()/x.svg), for a
    // popover close button in QML.
    QUrl closeIcon() const;

    // Any tinted glyph by icon name, as a file URL the QML Image can load.
    //
    // tintIcons() flattens the source tree (root glyphs + the ph/ phosphor
    // imports) into one directory, each file already carrying the active theme's
    // colour, so a name is all a scene needs ("trash", "caret-right",
    // "arrow-clockwise"). The tint is part of the generated file, which is why
    // QML must not tint the Image itself: a ColorOverlay would fight the
    // multi-colour message glyphs the same directory also holds.
    Q_INVOKABLE QUrl iconUrl(const QString& aName) const;

signals:
    void themeChanged();
};

} // namespace gui

#endif // GUI_QML_QMLTHEME_H
