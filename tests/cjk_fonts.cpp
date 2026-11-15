// REAL CJK SUPPORT: the shipped collection, the locale policy, and the glyphs.
//
// The application used to rely on whatever CJK face the host had installed; the
// review of master's font work found its shipped files were the wrong family
// (Noto Serif / Thin) and were never reachable under the names it asked for. This
// gate pins the replacement: one Noto Sans CJK collection with JP/KR/SC families,
// registered from the file the build ships, plus the pure policy that picks a
// family for the interface language and the fallback a Latin UI carries.
//
// It reads the font from the source tree, not from the application's qrc: embedding
// 32 MB into the shared assets.qrc generated a 162 MB C++ file in EVERY test
// target (measured), which is why the collection has its own app-only resource.
#include <cstdio>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QRawFont>
#include <QString>
#include <QStringList>

#include "gui/LocaleFonts.h"
#include "gui/LocaleParam.h"

namespace {
int failures = 0;
int checks = 0;
void expect(bool aPassed, const char* aWhat) {
    ++checks;
    if (!aPassed) {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

QString findFont() {
    const QStringList candidates = {
        QStringLiteral("assets/fonts/NotoSansCJK-VF.ttc"),
        QStringLiteral("../assets/fonts/NotoSansCJK-VF.ttc"),
        QStringLiteral("../../assets/fonts/NotoSansCJK-VF.ttc"),
    };
    for (const QString& path : candidates) {
        if (QFile::exists(path))
            return path;
    }
    return QString();
}

int inkPixels(const QFont& aFont, const QString& aText) {
    QImage image(220, 56, QImage::Format_ARGB32);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        painter.setFont(aFont);
        painter.drawText(4, 38, aText);
    }
    int ink = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qGray(image.pixel(x, y)) < 200)
                ++ink;
        }
    }
    return ink;
}
} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    const QString path = findFont();
    expect(!path.isEmpty(), "the shipped CJK collection is present in the source tree");
    if (path.isEmpty()) {
        std::printf("%d checks, %d failures\n", checks, failures);
        return 1;
    }

    const int id = QFontDatabase::addApplicationFont(path);
    expect(id >= 0, "Qt registers the collection");
    const QStringList families = QFontDatabase::applicationFontFamilies(id);
    expect(families.contains(QStringLiteral("Noto Sans CJK JP")), "the collection exposes the JP family");
    expect(families.contains(QStringLiteral("Noto Sans CJK KR")), "the collection exposes the KR family");
    expect(families.contains(QStringLiteral("Noto Sans CJK SC")), "the collection exposes the SC family");

    // The glyphs each interface language needs, through the same face Qt will draw.
    for (const char* family : {"Noto Sans CJK JP", "Noto Sans CJK KR", "Noto Sans CJK SC"}) {
        QFont font(QString::fromLatin1(family));
        font.setPointSize(14);
        const QRawFont face = QRawFont::fromFont(font);
        expect(face.isValid(), "the family resolves to a valid face");
        expect(face.familyName() == QString::fromLatin1(family), "the face is the family that was asked for");
        expect(face.supportsCharacter(QChar(0x6F22)), "the face has a Han glyph");
        expect(face.supportsCharacter(QChar(0xAC00)), "the face has a Hangul glyph");
        expect(face.supportsCharacter(QChar(0x3042)), "the face has a kana glyph");
        expect(inkPixels(font, QString::fromUtf8("漢 가 あ")) > 0, "the face paints CJK text");
    }

    // The policy: CJK interfaces take their region's family; a Latin UI keeps Geist
    // and carries the JP face, which covers Han and Hangul for names and text layers.
    using namespace gui::locale_fonts;
    expect(defaultFamilyForLanguage(QStringLiteral("ja")) == QStringLiteral("Noto Sans CJK JP"), "ja gets JP");
    expect(defaultFamilyForLanguage(QStringLiteral("ko")) == QStringLiteral("Noto Sans CJK KR"), "ko gets KR");
    expect(defaultFamilyForLanguage(QStringLiteral("zh")) == QStringLiteral("Noto Sans CJK SC"), "zh gets SC");
    expect(defaultFamilyForLanguage(QStringLiteral("en")) == QStringLiteral("Geist"), "en keeps Geist");
    expect(defaultFamilyForLanguage(QString()) == QStringLiteral("Geist"), "an empty locale keeps Geist");

    expect(fallbackFamiliesFor(QStringLiteral("Geist")).contains(QStringLiteral("Noto Sans CJK JP")),
           "a Latin UI carries the JP fallback");
    expect(fallbackFamiliesFor(QStringLiteral("Noto Sans CJK KR")).isEmpty(),
           "a CJK primary needs no second face");
    expect(fallbackFamiliesFor(QStringLiteral("DejaVu Sans")).contains(QStringLiteral("Noto Sans CJK JP")),
           "a custom Latin font keeps the CJK fallback");

    gui::LocaleParam param;
    param.fontFamily = QStringLiteral("Geist");
    param.fallbackFamilies = { QStringLiteral("Noto Sans CJK JP") };
    param.fontSize = QStringLiteral("10pt");
    expect(param.fontFamilies() == (QStringList{QStringLiteral("Geist"), QStringLiteral("Noto Sans CJK JP")}),
           "families() is primary then fallback");
    const QFont processFont = gui::locale_fonts::fontFor(param);
    expect(processFont.families().contains(QStringLiteral("Noto Sans CJK JP")),
           "fontFor carries the fallback into the QFont");
    expect(processFont.pointSizeF() == 10.0, "fontFor applies the locale size");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
