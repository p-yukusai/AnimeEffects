// QML-module i18n verification.
//
// The Animation settings dialog's qsTr() strings must resolve through the SAME
// QTranslator the app installs on QCoreApplication (Main.cpp installs the
// LocaleDecider catalog that way; QML qsTr() consults the application's
// translators). The dialog strings use an explicit, rename-safe translation
// context via `pragma Translator` in AnimationSettingDialog.qml, so the catalog
// entries can never collide with the C++ context of the sibling widget class
// (gui::AnimationSettingDialog).
//
// This exercises exactly the two things that can silently break, for every one
// of the dialog's four qsTr() strings:
//   A) the catalog holds the string under the production context, and
//   B) a QML qsTr() under that pragma context resolves to the translation at
//      runtime (no English source leaks through).
// It deliberately does not render the whole dialog: that would pull the theme
// machinery (theme::Colors / theme::iconDir via QmlTheme) and the widget shell
// into what is purely a translation test.
//
// argv[1] = locale (es|ja|ko|ru|zh), argv[2] = .qm dir for the fallback file
// load. The catalog now ships embedded under :/i18n (see the qt_add_resources in
// the test CMake); the build output dir (AE_I18N_QM_DIR) is only a fallback.
// e.g.  QT_QPA_PLATFORM=offscreen ./build/tests/Debug/anim_setting_i18n ja
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QTranslator>
#include <QVariant>
#include <QString>
#include <QByteArray>
#include <QList>
#include <QUrl>
#include <QDir>
#include <cstdio>

namespace {
// source string -> per-locale translation; order matches the array below.
const char* kSources[4] = {
    "Animation settings", "Loop animation", "Frame range", "Frames per second"
};
const QString kExpected[5][4] = {
    // es, ja, ko, ru, zh
    { QString::fromUtf8("Configuración de animación"), QString::fromUtf8("Reproducir en bucle"),
      QString::fromUtf8("Rango de fotogramas"), QString::fromUtf8("Fotogramas por segundo") },
    { QString::fromUtf8("アニメーション設定"), QString::fromUtf8("ループ再生"),
      QString::fromUtf8("フレーム範囲"), QString::fromUtf8("フレーム毎秒") },
    { QString::fromUtf8("애니메이션 설정"), QString::fromUtf8("애니메이션 반복"),
      QString::fromUtf8("프레임 범위"), QString::fromUtf8("초당 프레임") },
    { QString::fromUtf8("Настройки анимации"), QString::fromUtf8("Зациклить анимацию"),
      QString::fromUtf8("Диапазон кадров"), QString::fromUtf8("Кадров в секунду") },
    { QString::fromUtf8("动画设置"), QString::fromUtf8("循环动画"),
      QString::fromUtf8("帧范围"), QString::fromUtf8("每秒帧数") },
};

int localeIndex(const QString& loc) {
    if (loc == QLatin1String("es")) return 0;
    if (loc == QLatin1String("ja")) return 1;
    if (loc == QLatin1String("ko")) return 2;
    if (loc == QLatin1String("ru")) return 3;
    if (loc == QLatin1String("zh")) return 4;
    return -1;
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

#ifndef AE_I18N_QM_DIR
#define AE_I18N_QM_DIR "assets/locale" // fallback if the build didn't pass one
#endif
    const QString dataDir = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QLatin1String(AE_I18N_QM_DIR);
    // Locales to verify: all five, or a single one via argv[1].
    QList<QString> locales = { QStringLiteral("es"), QStringLiteral("ja"), QStringLiteral("ko"),
                               QStringLiteral("ru"), QStringLiteral("zh") };
    if (argc > 1) locales = { QString::fromLocal8Bit(argv[1]) };

    const char* kContext = "AnimeEffects.Dialogs";
    int failures = 0;

    for (const QString& loc : locales) {
        const int li = localeIndex(loc);
        if (li < 0) {
            std::fprintf(stderr, "FAIL: unknown locale '%s'\n", qPrintable(loc));
            return 2;
        }

        // Load & install the same catalog the app installs on QCoreApplication.
        // The production path is embedded under :/i18n (built by
        // release_translations and embedded by the tests' qt_add_resources);
        // fall back to the build output dir so a plain file load still works
        // when the resource isn't linked in.
        QTranslator tr;
        if (!tr.load(QLatin1String("translation_") + loc, QLatin1String(":/i18n"))
            && !tr.load(QLatin1String("translation_") + loc, dataDir)) {
            std::fprintf(stderr, "FAIL: cannot load '%s/translation_%s.qm' (cwd=%s)\n",
                         qPrintable(dataDir), qPrintable(loc), qPrintable(QDir::currentPath()));
            return 2;
        }
        if (!QCoreApplication::installTranslator(&tr)) {
            std::fprintf(stderr, "FAIL: QCoreApplication::installTranslator returned false\n");
            return 2;
        }

        // A) Catalog content: every string resolves under the module context to a
        // non-identical translation.
        for (int i = 0; i < 4; ++i) {
            const QByteArray src(kSources[i]);
            const QString resolved = QCoreApplication::translate(kContext, src.constData());
            if (resolved != kExpected[li][i]) {
                std::fprintf(stderr, "FAIL[%s]: catalog context='%s' source='%s' -> '%s' (expected '%s')\n",
                             qPrintable(loc), kContext, src.constData(), qPrintable(resolved),
                             qPrintable(kExpected[li][i]));
                ++failures;
            }
        }

        // B) Runtime: a QML scene with the same pragma + qsTr()s must render the
        // translations (proves the runtime honors the pragma and consults the app's
        // translator). Same pragma + sources as AnimationSettingDialog.qml.
        QQmlEngine engine;
        QQmlComponent comp(&engine);
        QByteArray qml = "pragma Translator: \"AnimeEffects.Dialogs\"\n"
                         "import QtQuick\n"
                         "Item {\n";
        for (int i = 0; i < 4; ++i)
            qml += QStringLiteral("    property string s%1: qsTr(\"%2\")\n")
                       .arg(i).arg(QString::fromLatin1(kSources[i])).toUtf8();
        qml += "}\n";
        comp.setData(qml, QUrl());
        if (comp.isError()) {
            for (const auto& e : comp.errors())
                std::fprintf(stderr, "QML error: %s\n", qPrintable(e.toString()));
            std::fprintf(stderr, "FAIL[%s]: inline QML did not parse\n", qPrintable(loc));
            return 2;
        }
        QObject* obj = comp.create();
        if (!obj) {
            for (const auto& e : comp.errors())
                std::fprintf(stderr, "QML error: %s\n", qPrintable(e.toString()));
            std::fprintf(stderr, "FAIL[%s]: inline QML did not instantiate\n", qPrintable(loc));
            return 2;
        }
        for (int i = 0; i < 4; ++i) {
            const QString rendered =
                obj->property(QStringLiteral("s%1").arg(i).toLatin1().constData()).toString();
            if (rendered != kExpected[li][i]) {
                std::fprintf(stderr, "FAIL[%s]: runtime qsTr source='%s' -> '%s' (expected '%s')\n",
                             qPrintable(loc), kSources[i], qPrintable(rendered),
                             qPrintable(kExpected[li][i]));
                ++failures;
            }
        }
        delete obj;
        // Drop this locale's translator so it cannot shade the next iteration
        // (QTranslator lookup is most-recent-first).
        QCoreApplication::removeTranslator(&tr);

        if (failures == 0)
            std::printf("PASS: locale=%s all %d dialog qsTr() strings resolve via context '%s'\n",
                        qPrintable(loc), 4, kContext);
    }

    if (failures != 0) {
        std::fprintf(stderr, "FAIL: %d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
