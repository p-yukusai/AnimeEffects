// icon_tint - build-time tint of the canonical monochrome icon set.
//
// The repo ships one canonical set (data/icons, checked in). This tool
// regenerates a theme's icons by keeping each glyph's alpha (anti-aliased
// edges included) and replacing the color, so light/dark themes need no
// baked icons in the repo. Invoked from CMake custom commands; ninja tracks
// the outputs, so it only re-runs when the canonical set or the tool
// changes.
//
// usage: icon_tint <srcDir> <dstDir> <#rrggbb> [names...]
//   with names: tint only those files; without: tint every *.png/*.svg in
//   srcDir. PNGs are recolored keeping their alpha; SVGs (monochrome, drawn
//   with currentColor) have the color substituted in the markup.
//
// The canonical SVG sources are never shown to Qt's renderer: the app loads
// the per-theme generated files, so currentColor is a pure build-time token.

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QStringList>
#include <cstdio>

static bool tintSvg(const QString& aSrc, const QString& aDst, const QColor& aColor) {
    QFile f(aSrc);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        fprintf(stderr, "icon_tint: cannot read %s\n", qPrintable(aSrc));
        return false;
    }
    QString svg = QString::fromUtf8(f.readAll());
    f.close();
    svg.replace("currentColor", aColor.name());
    QFile out(aDst);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        fprintf(stderr, "icon_tint: cannot write %s\n", qPrintable(aDst));
        return false;
    }
    out.write(svg.toUtf8());
    return true;
}

static bool tintOne(const QString& aSrc, const QString& aDst, const QColor& aColor) {
    QImage source(aSrc);
    if (source.isNull()) {
        fprintf(stderr, "icon_tint: cannot load %s\n", qPrintable(aSrc));
        return false;
    }
    source = source.convertToFormat(QImage::Format_ARGB32);
    QImage tinted(source.size(), QImage::Format_ARGB32);
    tinted.fill(aColor);
    {
        QPainter painter(&tinted);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(0, 0, source);
    }
    if (!tinted.save(aDst, "PNG")) {
        fprintf(stderr, "icon_tint: cannot save %s\n", qPrintable(aDst));
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: icon_tint <srcDir> <dstDir> <hexcolor> [names...]\n");
        return 2;
    }
    const QDir src(argv[1]);
    const QString dstDir(argv[2]);
    const QString colorArg(argv[3]);
    const QColor color(colorArg.startsWith('#') ? colorArg : "#" + colorArg);
    if (!color.isValid()) {
        fprintf(stderr, "icon_tint: bad color %s\n", argv[3]);
        return 2;
    }
    if (!QDir().mkpath(dstDir)) {
        fprintf(stderr, "icon_tint: cannot create %s\n", qPrintable(dstDir));
        return 2;
    }
    QStringList names;
    if (argc > 4) {
        for (int i = 4; i < argc; ++i) {
            names << argv[i];
        }
    } else {
        names = src.entryList(QStringList() << "*.png" << "*.svg", QDir::Files);
    }
    bool ok = true;
    for (const QString& name : names) {
        const bool tinted =
            name.endsWith(".svg") ? tintSvg(src.filePath(name), dstDir + "/" + name, color)
                                  : tintOne(src.filePath(name), dstDir + "/" + name, color);
        if (!tinted) {
            ok = false;
        }
    }
    return ok ? 0 : 1;
}
