// AeComboBox's popup geometry, offscreen: a short list must not reserve the
// scrollbar lane, a long one must reserve exactly 12px, and the pill lane is flush
// with the popup's inner edge (the old popup padding pushed it 4px further in).
#include <QApplication>
#include <QFile>
#include <QScopedPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include "gui/qml/QmlTheme.h"
#include "gui/theme/Icons.h"

namespace {
int failures = 0;
void check(bool aOk, const QString& aWhat) {
    QTextStream(stdout) << (aOk ? "PASS  " : "FAIL  ") << aWhat << "\n";
    if (!aOk) ++failures;
}
QVariantList options(int aCount) {
    QVariantList out;
    for (int i = 0; i < aCount; ++i) {
        QVariantMap map;
        map.insert(QStringLiteral("label"), QStringLiteral("Option %1").arg(i + 1));
        map.insert(QStringLiteral("value"), i);
        out.append(map);
    }
    return out;
}
} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    gui::QmlTheme theme;
    QTemporaryDir iconDir;
    check(iconDir.isValid(), "a scratch dir for the tinted icons");
    theme::tintIcons(QStringLiteral(AE_ICON_SRC), iconDir.path(), theme::Colors::current());
    theme::setIconDir(iconDir.path());

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("theme"), &theme);
    QTemporaryDir sceneDir;
    const QString scenePath = sceneDir.path() + QStringLiteral("/scene.qml");
    {
        QFile f(scenePath);
        check(f.open(QIODevice::WriteOnly | QIODevice::Text), "the scene file can be written");
        f.write("import QtQuick\n"
                "import \"file:" AE_QML_DIR "\"\n"
                "Item { width: 400; height: 300\n"
                "  AeComboBox { objectName: \"combo\"; x: 20; y: 20; width: 180; height: 28; options: [] }\n"
                "}\n");
    }
    QQmlComponent component(&engine, QUrl::fromLocalFile(scenePath));
    check(component.status() == QQmlComponent::Ready, "the combo scene is Ready");
    if (component.status() != QQmlComponent::Ready) {
        for (const QQmlError& error : component.errors())
            QTextStream(stderr) << "scene error: " << error.toString() << "\n";
        return 2;
    }
    QScopedPointer<QObject> created(component.create());
    auto* root = qobject_cast<QQuickItem*>(created.data());
    check(root != nullptr, "the combo scene builds");
    if (!root) return 2;
    QQuickWindow window;
    window.resize(400, 300);
    window.show();
    root->setParentItem(window.contentItem());
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents();
        window.update();
    }
    auto* combo = root->findChild<QQuickItem*>(QStringLiteral("combo"));
    check(combo != nullptr, "the combo is addressable");
    if (!combo) return 2;

    auto openWith = [&](const QVariantList& aOptions) {
        combo->setProperty("options", aOptions);
        if (combo->property("popupOpen").toBool())
            QMetaObject::invokeMethod(combo, "togglePopup");
        QCoreApplication::processEvents();
        check(QMetaObject::invokeMethod(combo, "togglePopup"), "the popup opens");
        for (int i = 0; i < 6; ++i) {
            QCoreApplication::processEvents();
            window.update();
        }
    };
    auto close = [&]() {
        if (combo->property("popupOpen").toBool())
            QMetaObject::invokeMethod(combo, "togglePopup");
        for (int i = 0; i < 3; ++i) QCoreApplication::processEvents();
    };

    // ---- short list: no lane reserved -------------------------------------
    openWith(options(3));
    auto* list = combo->findChild<QQuickItem*>(QStringLiteral("aeComboList"));
    auto* content = combo->findChild<QQuickItem*>(QStringLiteral("aeComboPopupContent"));
    auto* bar = combo->findChild<QQuickItem*>(QStringLiteral("aeComboBar"));
    check(list && content && bar, "the short list, its wrapper and its bar exist");
    if (list && content && bar) {
        check(!bar->isVisible(), "a short list shows no scrollbar");
        check(qAbs(list->width() - (content->width() - 8.0)) < 0.5,
              QStringLiteral("and reserves no lane (list %1 of content %2)")
                  .arg(list->width()).arg(content->width()));
    }
    close();

    // ---- long list: exactly the 12px lane, flush to the popup edge --------
    openWith(options(30));
    list = combo->findChild<QQuickItem*>(QStringLiteral("aeComboList"));
    content = combo->findChild<QQuickItem*>(QStringLiteral("aeComboPopupContent"));
    bar = combo->findChild<QQuickItem*>(QStringLiteral("aeComboBar"));
    check(list && content && bar, "the long list, its wrapper and its bar exist");
    if (list && content && bar) {
        check(bar->isVisible(), "a long list shows its scrollbar");
        check(qAbs(list->width() - (content->width() - 20.0)) < 0.5,
              QStringLiteral("and reserves exactly the 12px lane (list %1 of content %2)")
                  .arg(list->width()).arg(content->width()));
        check(qAbs(bar->x() - (content->width() - 12.0)) < 0.5,
              QStringLiteral("the lane is flush with the popup edge (x=%1, content=%2)")
                  .arg(bar->x()).arg(content->width()));
        check(qAbs(bar->width() - 12.0) < 0.5, "the lane is 12px");
    }
    close();

    if (failures) {
        QTextStream(stdout) << "FAILED " << failures << " check(s)\n";
        return 1;
    }
    QTextStream(stdout) << "OK: combo popup reserves only what it needs and sits at the edge\n";
    return 0;
}
