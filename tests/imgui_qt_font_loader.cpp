// Standalone, headless proof for gui::imgui_qt::installQtFontLoader().
//
// It IS registered with CMake: tests/CMakeLists.txt declares the imgui_qt_font_loader target
// inside the AE_ENABLE_IMGUI block, so scripts/run_suites.sh discovers and runs it, and a
// failure here fails the suite. The hand-rolled g++ command below is the equivalent manual
// fallback (it compiles the ImGui sources directly instead of linking ae_imgui) and is what
// this file's numbers were measured with; run it by hand from the repository root:
//
//   g++ -std=c++17 -fPIC -I build/_deps/imgui-src -I src $(pkg-config --cflags Qt6Gui)
//     src/gui/ImGuiQtFontLoader.cpp tests/imgui_qt_font_loader.cpp
//     build/_deps/imgui-src/imgui.cpp build/_deps/imgui-src/imgui_draw.cpp
//     build/_deps/imgui-src/imgui_tables.cpp build/_deps/imgui-src/imgui_widgets.cpp
//     $(pkg-config --libs Qt6Gui) -o /tmp/fontloadertest
//   QT_QPA_PLATFORM=offscreen /tmp/fontloadertest
//
// (each of those two commands is one line; the line breaks above are for readability)
//
// No display and no GL: QGuiApplication on the offscreen platform is enough. No ImGui
// context is created either - every atlas call used here is context-free, which is
// itself part of what the assertions below establish.
//
// Every number printed here is measured by this run. A failed assertion prints [FAIL]
// and the process exits non-zero; a precondition that regressed is asserted rather than
// skipped, so a broken CJK path cannot pass quietly.

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <QDir>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QRawFont>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include "imgui.h"
#include "imgui_internal.h"

#include "gui/ImGuiQtFontLoader.h"

static int gChecks = 0;
static int gFailures = 0;

// Float comparison for the metrics that are allowed to pick up sub-pixel rounding when the
// rasterizer density changes.
static bool near(float aA, float aB, float aTolerance)
{
    const float difference = aA - aB;
    return difference <= aTolerance && difference >= -aTolerance;
}

static void check(bool aPassed, const char* aFormat, ...)
{
    char message[1024];
    va_list args;
    va_start(args, aFormat);
    vsnprintf(message, sizeof(message), aFormat, args);
    va_end(args);
    ++gChecks;
    if (!aPassed)
        ++gFailures;
    printf("%s %s\n", aPassed ? "[ ok ]" : "[FAIL]", message);
}

// The findings of one glyph, read out of the atlas immediately: ImFontBaked::Glyphs is a
// vector, so a pointer into it does not survive another glyph load.
struct GlyphProbe
{
    bool  found = false;
    int   sourceIdx = -1;
    int   bitmapWidth = 0;
    int   bitmapHeight = 0;
    long  inkPixels = 0;
    float advanceX = 0.0f;
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    bool  visible = false;
    bool  hasRect = false;
    ImTextureRect rect = {}; // copy: coordinates in the texture that was current at load time
};

// Count non-zero coverage pixels in one atlas rectangle. The atlas texture format is
// ImTextureFormat_RGBA32 by default, in which case the coverage is the alpha byte of each
// IM_COL32 pixel.
static long countInk(ImFontAtlas& aAtlas, const ImTextureRect& aRect)
{
    ImTextureData* tex = aAtlas.TexData;
    if (tex == NULL || tex->Pixels == NULL || aRect.w == 0 || aRect.h == 0)
        return 0;
    long ink = 0;
    for (int y = 0; y < (int)aRect.h; ++y)
    {
        const unsigned char* row = (const unsigned char*)tex->GetPixelsAt(aRect.x, aRect.y + y);
        for (int x = 0; x < (int)aRect.w; ++x)
        {
            const unsigned char alpha = (tex->Format == ImTextureFormat_Alpha8) ? row[x] : row[x * 4 + 3];
            if (alpha != 0)
                ++ink;
        }
    }
    return ink;
}

// Load one codepoint at one pixel size through the atlas and report what it produced.
// FindGlyphNoFallback() returns NULL when the font has no glyph (instead of substituting
// the fallback glyph), so "found" is unambiguous.
static GlyphProbe probeGlyph(ImFontAtlas& aAtlas, float aSize, ImWchar aCodepoint)
{
    GlyphProbe probe;
    ImFontBaked* baked = aAtlas.Fonts[0]->GetFontBaked(aSize);
    if (baked == NULL)
        return probe;
    ImFontGlyph* glyph = baked->FindGlyphNoFallback(aCodepoint);
    if (glyph == NULL || glyph->Codepoint != aCodepoint)
        return probe;
    probe.found = true;
    probe.sourceIdx = (int)glyph->SourceIdx;
    probe.advanceX = glyph->AdvanceX;
    probe.x0 = glyph->X0;
    probe.y0 = glyph->Y0;
    probe.x1 = glyph->X1;
    probe.y1 = glyph->Y1;
    probe.visible = glyph->Visible;
    if (ImTextureRect* rect = ImFontAtlasPackGetRectSafe(&aAtlas, glyph->PackId))
    {
        probe.rect = *rect;
        probe.hasRect = true;
        probe.bitmapWidth = (int)rect->w;
        probe.bitmapHeight = (int)rect->h;
        probe.inkPixels = countInk(aAtlas, *rect);
    }
    return probe;
}

// The first source the installed loader answers for a codepoint, asked exactly the way
// ImGui asks it while routing (ImFontBaked_BuildLoadGlyph walks the sources in order).
// -1 means no installed source has the glyph.
static int firstSourceCovering(ImFontAtlas& aAtlas, ImWchar aCodepoint)
{
    for (int i = 0; i < aAtlas.Sources.Size; ++i)
        if (aAtlas.FontLoader->FontSrcContainsGlyph(&aAtlas, &aAtlas.Sources[i], aCodepoint))
            return i;
    return -1;
}

static void printSourceCoverage(ImFontAtlas& aAtlas, ImWchar aCodepoint)
{
    printf("  loader coverage of U+%04X, source by source:\n", (unsigned int)aCodepoint);
    for (int i = 0; i < aAtlas.Sources.Size; ++i)
        printf("    source %d (\"%s\", merge=%d): FontSrcContainsGlyph=%d\n", i, aAtlas.Sources[i].Name,
               aAtlas.Sources[i].MergeMode ? 1 : 0,
               aAtlas.FontLoader->FontSrcContainsGlyph(&aAtlas, &aAtlas.Sources[i], aCodepoint) ? 1 : 0);
}

// The glyph index and Qt alpha bitmap for one codepoint, produced by the test itself
// through the public Qt API - the reference the atlas rectangle is compared against.
struct QtRaster
{
    bool   ok = false;
    quint32 glyphIndex = 0;
    QImage bitmap;
};

static QtRaster rasterizeWithQt(const QRawFont& aFace, unsigned int aCodepoint)
{
    QtRaster result;
    quint32 glyphIndex = 0;
    int numGlyphs = 1; // in/out: capacity in, count out
    const QChar character = QChar((char16_t)aCodepoint);
    if (!aFace.glyphIndexesForChars(&character, 1, &glyphIndex, &numGlyphs) || glyphIndex == 0)
        return result;
    result.glyphIndex = glyphIndex;
    result.bitmap = aFace.alphaMapForGlyph(glyphIndex, QRawFont::PixelAntialiasing, QTransform());
    result.ok = !result.bitmap.isNull();
    return result;
}

// Compare one atlas rectangle with Qt's own bitmap for the same glyph, pixel by pixel.
// This is what proves the atlas holds Qt's rasterization and not something rescaled,
// shifted or mis-strided on the way in (the RGBA32 expansion and the Alpha8 pitch are
// both exercised by it).
static int atlasVersusQtMismatches(ImFontAtlas& aAtlas, const ImTextureRect& aRect, const QImage& aQt)
{
    ImTextureData* tex = aAtlas.TexData;
    if (tex == NULL || tex->Pixels == NULL || aQt.isNull())
        return -1;
    if (aQt.width() != (int)aRect.w || aQt.height() != (int)aRect.h)
        return -1;
    int mismatches = 0;
    for (int y = 0; y < aQt.height(); ++y)
    {
        const unsigned char* atlasRow = (const unsigned char*)tex->GetPixelsAt(aRect.x, aRect.y + y);
        const unsigned char* qtRow = aQt.constScanLine(y);
        for (int x = 0; x < aQt.width(); ++x)
        {
            const unsigned char atlasAlpha = (tex->Format == ImTextureFormat_Alpha8) ? atlasRow[x] : atlasRow[x * 4 + 3];
            if (atlasAlpha != qtRow[x])
                ++mismatches;
        }
    }
    return mismatches;
}

// The face a source family stands for, resolved the same two ways the loader and Qt would:
// through the application font for a writing system, then by family name.
static QRawFont faceForFamily(const QFont& aAppFont, const QString& aFamily)
{
    const QFontDatabase::WritingSystem writingSystems[] = {
        QFontDatabase::Japanese, QFontDatabase::Korean,
        QFontDatabase::SimplifiedChinese, QFontDatabase::TraditionalChinese
    };
    for (const QFontDatabase::WritingSystem writingSystem : writingSystems)
    {
        const QRawFont face = QRawFont::fromFont(aAppFont, writingSystem);
        if (face.isValid() && face.familyName() == aFamily)
            return face;
    }
    QFont byName;
    byName.setFamily(aFamily);
    const QRawFont face = QRawFont::fromFont(byName);
    return (face.isValid() && face.familyName() == aFamily) ? face : QRawFont();
}

static QString describeTexture(ImFontAtlas& aAtlas)
{
    ImTextureData* tex = aAtlas.TexData;
    if (tex == NULL)
        return QStringLiteral("<no texture>");
    return QStringLiteral("TexList=%1 texture #%2 %3x%4 fmt=%5(%6 B/px) status=%7 updates=%8 used=(%9,%10 %11x%12) texIsBuilt=%13")
        .arg(aAtlas.TexList.Size)
        .arg(tex->UniqueID)
        .arg(tex->Width)
        .arg(tex->Height)
        .arg((int)tex->Format)
        .arg(tex->BytesPerPixel)
        .arg((int)tex->Status)
        .arg(tex->Updates.Size)
        .arg(tex->UsedRect.x)
        .arg(tex->UsedRect.y)
        .arg(tex->UsedRect.w)
        .arg(tex->UsedRect.h)
        .arg(aAtlas.TexIsBuilt ? 1 : 0);
}

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv); // offscreen: QRawFont needs the Qt font database

    printf("== environment ==\n");
    printf("Qt %s, imgui %s (IMWCHAR %zu-bit, IM_UNICODE_CODEPOINT_MAX U+%04X)\n",
           qVersion(), IMGUI_VERSION, sizeof(ImWchar) * 8, (unsigned int)IM_UNICODE_CODEPOINT_MAX);
    printf("working directory: %s\n", qPrintable(QDir::currentPath()));

    const QString geistPath = QStringLiteral("assets/fonts/Geist-Regular.ttf");
    if (!QFile::exists(geistPath))
    {
        printf("[FAIL] %s does not exist - run this from the repository root\n", qPrintable(geistPath));
        return 2;
    }
    const int fontId = QFontDatabase::addApplicationFont(geistPath);
    printf("addApplicationFont(%s) -> id=%d families=%s\n", qPrintable(geistPath), fontId,
           qPrintable(QFontDatabase::applicationFontFamilies(fontId).join(QStringLiteral(","))));
    check(fontId >= 0, "the bundled Geist file registered with Qt's font database (id %d)", fontId);

    QFont appFont(QStringLiteral("Geist"));
    appFont.setPixelSize(16);
    const float kSize = 20.0f;

    // The bytes the loader will hand to ImGui for source 0, read here as well so the test can
    // rasterize the same face itself and compare the atlas against it.
    const QByteArray geistBytes = []() {
        QFile file(QStringLiteral("assets/fonts/Geist-Regular.ttf"));
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }();
    QRawFont primaryFace;
    primaryFace.loadFromData(geistBytes, kSize, QFont::PreferDefaultHinting);

    const QRawFont appFace = QRawFont::fromFont(appFont);
    printf("application font: family=%s valid=%d supports('A')=%d supports(U+4E00)=%d\n",
           qPrintable(appFace.familyName()), appFace.isValid() ? 1 : 0,
           appFace.supportsCharacter((uint)'A') ? 1 : 0, appFace.supportsCharacter(0x4E00u) ? 1 : 0);
    check(appFace.isValid(), "the application font (\"Geist\") resolves to a usable QRawFont");
    check(appFace.supportsCharacter((uint)'A') && !appFace.supportsCharacter(0x4E00u),
          "the application face has Latin and does NOT have U+4E00 (so CJK must come from a fallback)");

    printf("\n== what Qt resolves for the CJK writing systems ==\n");
    const QFontDatabase::WritingSystem writingSystems[] = {
        QFontDatabase::Japanese, QFontDatabase::Korean,
        QFontDatabase::SimplifiedChinese, QFontDatabase::TraditionalChinese
    };
    QStringList resolvedFamilies;
    for (const QFontDatabase::WritingSystem writingSystem : writingSystems)
    {
        const QRawFont face = QRawFont::fromFont(appFont, writingSystem);
        const QString sample = QFontDatabase::writingSystemSample(writingSystem);
        printf("  writingSystem=%d: valid=%d family=\"%s\" sampleSupports=%d(U+%04X) supports(U+4E00)=%d supports(U+AC00)=%d\n",
               (int)writingSystem, face.isValid() ? 1 : 0, qPrintable(face.familyName()),
               !sample.isEmpty() && face.supportsCharacter(sample.at(0)) ? 1 : 0,
               sample.isEmpty() ? 0u : (unsigned int)sample.at(0).unicode(),
               face.supportsCharacter(0x4E00u) ? 1 : 0, face.supportsCharacter(0xAC00u) ? 1 : 0);
        if (face.isValid() && !resolvedFamilies.contains(face.familyName()))
            resolvedFamilies.append(face.familyName());
    }
    printf("  distinct solved families: %s\n", qPrintable(resolvedFamilies.join(QStringLiteral(", "))));

    printf("\n== 1. install: loader identity and sources ==\n");
    ImFontAtlas atlas;
    const int sourceCount = gui::imgui_qt::installQtFontLoader(atlas, appFont, kSize, 1);
    printf("installQtFontLoader(size=%.1f, dpr=1) returned %d; FontLoaderName=\"%s\" (loaderName()=\"%s\")\n",
           kSize, sourceCount, atlas.FontLoaderName ? atlas.FontLoaderName : "<null>", gui::imgui_qt::loaderName());
    check(atlas.FontLoader != NULL && atlas.FontLoaderName != NULL &&
          strcmp(atlas.FontLoaderName, gui::imgui_qt::loaderName()) == 0,
          "the atlas reports our loader (FontLoaderName == loaderName() == \"%s\")", gui::imgui_qt::loaderName());
    check(sourceCount == atlas.Sources.Size,
          "installQtFontLoader() returned %d and the atlas holds %d sources", sourceCount, atlas.Sources.Size);

    // The three documented skip rules are asserted, not inferred from a count floor. The
    // third one - "a face that duplicates one already installed" - is what the family
    // comparison in resolveFallbackSources() implements, and on this machine three of the
    // four writing systems resolve to the same family, so without it the install would add
    // three sources for one face and every assertion below would still have passed on a
    // count floor alone. The list printed just above is the evidence; this is the gate.
    const QString primarySourceFamily = primaryFace.familyName();
    QStringList installedFamilies;
    QStringList repeatedFamilies;
    for (int i = 0; i < atlas.Sources.Size; ++i)
    {
        const QString family = QString::fromUtf8(atlas.Sources[i].Name);
        if (installedFamilies.contains(family))
            repeatedFamilies.append(family);
        installedFamilies.append(family);
    }
    check(repeatedFamilies.isEmpty(),
          "no two installed sources name the same family (repeated: %s)",
          qPrintable(repeatedFamilies.isEmpty() ? QStringLiteral("<none>") : repeatedFamilies.join(QStringLiteral(", "))));
    check(installedFamilies.value(0) == primarySourceFamily,
          "source 0 names the primary face's own family (\"%s\")", qPrintable(installedFamilies.value(0)));

    // The exact expected count, derived from the families Qt resolved above rather than
    // hardcoded, so it holds on any machine: one source for the primary face, plus one for
    // each *distinct* resolved family that is not the primary face's own. On this machine
    // Qt resolves "Droid Sans Fallback" (Japanese, SimplifiedChinese, TraditionalChinese) and
    // "Noto Sans CJK KR" (Korean), so the expectation here is 1 + 2 = 3.
    QStringList expectedFallbackFamilies;
    for (const QString& family : resolvedFamilies)
        if (family != primarySourceFamily && !expectedFallbackFamilies.contains(family))
            expectedFallbackFamilies.append(family);
    printf("  expectation from the resolved families: 1 primary + %d distinct fallback famil%s (%s) = %d sources\n",
           (int)expectedFallbackFamilies.size(), expectedFallbackFamilies.size() == 1 ? "y" : "ies",
           qPrintable(expectedFallbackFamilies.isEmpty() ? QStringLiteral("<none>") : expectedFallbackFamilies.join(QStringLiteral(", "))),
           1 + (int)expectedFallbackFamilies.size());
    check(sourceCount == 1 + expectedFallbackFamilies.size(),
          "installQtFontLoader() returned %d sources: 1 primary (\"%s\") + %d distinct resolved fallback famil%s",
          sourceCount, qPrintable(primarySourceFamily), (int)expectedFallbackFamilies.size(),
          expectedFallbackFamilies.size() == 1 ? "y" : "ies");
    check(atlas.RendererHasTextures, "the context-less atlas was put on the dynamic texture path (RendererHasTextures=1)");
    for (int i = 0; i < atlas.Sources.Size; ++i)
        printf("  source %d: name=\"%s\" merge=%d sizePixels=%.1f rasterizerDensity=%.2f glyphRanges=%s fontData=%p(%d bytes, owned=%d)\n",
               i, atlas.Sources[i].Name, atlas.Sources[i].MergeMode ? 1 : 0,
               (double)atlas.Sources[i].SizePixels, (double)atlas.Sources[i].RasterizerDensity,
               atlas.Sources[i].GlyphRanges ? "set" : "NULL", atlas.Sources[i].FontData,
               atlas.Sources[i].FontDataSize, atlas.Sources[i].FontDataOwnedByAtlas ? 1 : 0);

    // Build() with RendererHasTextures set does not preload any glyph range [measured here:
    // the used rectangle after Build() covers only the built-in cursor/lines rects].
    atlas.Build();
    printf("after Build(): %s\n", qPrintable(describeTexture(atlas)));
    check(atlas.TexData != NULL && atlas.TexData->Pixels != NULL, "Build() created the atlas texture");
    check(atlas.Fonts.Size == 1, "all %d sources merged into one ImFont", atlas.Sources.Size);
    const int updatesAfterBuild = atlas.TexData->Updates.Size;

    // Direct measurement that nothing was preloaded: with RendererHasTextures set, Build()
    // must not walk any glyph range. Neither 'A' nor ' ' is loaded at this point - the blank
    // glyphs are only created when ImGui sets up a baked size, and this measurement shows that
    // even that had not happened yet for the size asked for.
    ImFontBaked* bakedAtRequestSize = atlas.Fonts[0]->GetFontBaked(kSize);
    printf("fresh bake at %.0fpx: size=%.1f ascent=%.1f descent=%.1f IsGlyphLoaded('A')=%d IsGlyphLoaded(' ')=%d\n",
           kSize, (double)bakedAtRequestSize->Size, (double)bakedAtRequestSize->Ascent,
           (double)bakedAtRequestSize->Descent, bakedAtRequestSize->IsGlyphLoaded('A') ? 1 : 0,
           bakedAtRequestSize->IsGlyphLoaded((ImWchar)' ') ? 1 : 0);
    check(!bakedAtRequestSize->IsGlyphLoaded('A'),
          "no glyph range was preloaded by Build(): 'A' is not loaded until it is asked for");

    // A backend that has already uploaded the texture the atlas built: this is what the
    // dynamic path looks like after the first frame, and it is what makes later glyphs show
    // up as *queued updates* rather than as part of a texture that does not exist yet.
    atlas.TexData->SetTexID((ImTextureID)1);
    atlas.TexData->SetStatus(ImTextureStatus_OK);
    printf("backend simulated (SetTexID(1) + SetStatus(OK)): %s\n", qPrintable(describeTexture(atlas)));

    printf("\n== 2. Latin: 'A' from the application-font source ==\n");
    const GlyphProbe latin = probeGlyph(atlas, kSize, (ImWchar)'A');
    printf("'A' @%.0fpx: found=%d source=%d bitmap=%dx%d ink=%ld advance=%.3f box=(%.2f,%.2f)-(%.2f,%.2f) visible=%d\n",
           kSize, latin.found ? 1 : 0, latin.sourceIdx, latin.bitmapWidth, latin.bitmapHeight, latin.inkPixels,
           (double)latin.advanceX, (double)latin.x0, (double)latin.y0, (double)latin.x1, (double)latin.y1,
           latin.visible ? 1 : 0);
    check(latin.found && latin.visible, "'A' was rasterized by the loader at %.0fpx", kSize);
    check(latin.sourceIdx == 0, "'A' comes from source 0 (the Geist source), not a fallback");
    check(latin.inkPixels > 0, "'A' has ink: %ld non-zero alpha pixels in its %dx%d atlas rectangle",
          latin.inkPixels, latin.bitmapWidth, latin.bitmapHeight);
    check(latin.advanceX > 0.0f, "'A' has a positive advance (%.3f px)", (double)latin.advanceX);

    // The rectangle must be Qt's rasterization of the same face at the same size, byte for
    // byte: this is what proves the Alpha8 -> RGBA32 expansion and the image pitch are right.
    {
        const QtRaster reference = rasterizeWithQt(primaryFace, (unsigned int)'A');
        const int mismatches = latin.hasRect ? atlasVersusQtMismatches(atlas, latin.rect, reference.bitmap) : -1;
        printf("  reference: Qt glyph index %u, %dx%d alpha map; atlas rectangle (%d,%d %dx%d) mismatches=%d\n",
               reference.glyphIndex, reference.bitmap.width(), reference.bitmap.height(),
               latin.rect.x, latin.rect.y, latin.rect.w, latin.rect.h, mismatches);
        check(reference.ok && mismatches == 0,
              "the atlas rectangle for 'A' equals QRawFont::alphaMapForGlyph() for the same face and size, "
              "pixel for pixel (%d mismatching pixels)", mismatches);
    }

    printf("\n== 2b. a glyph with no ink: the space ==\n");
    {
        // A whitespace glyph is the case that catches a loader treating "empty bitmap" as
        // "glyph missing": ImGui would then answer with the fallback character's advance and
        // every space in the interface would be laid out with the wrong width.
        const QtRaster spaceReference = rasterizeWithQt(primaryFace, (unsigned int)' ');
        QPointF spaceAdvance;
        primaryFace.advancesForGlyphIndexes(&spaceReference.glyphIndex, &spaceAdvance, 1);
        const GlyphProbe space = probeGlyph(atlas, kSize, (ImWchar)' ');
        printf("' ' @%.0fpx: found=%d source=%d bitmap=%dx%d ink=%ld advance=%.3f visible=%d | "
               "Qt: glyph index %u, bitmap=%dx%d, advance=%.3f\n",
               kSize, space.found ? 1 : 0, space.sourceIdx, space.bitmapWidth, space.bitmapHeight,
               space.inkPixels, (double)space.advanceX, space.visible ? 1 : 0, spaceReference.glyphIndex,
               spaceReference.bitmap.width(), spaceReference.bitmap.height(), (double)spaceAdvance.x());
        check(spaceReference.glyphIndex != 0, "the Geist face maps the space to glyph %u", spaceReference.glyphIndex);
        check(space.found, "' ' is reported as FOUND even though it has no ink");
        check(!space.visible, "' ' is marked invisible (ImGui expects that for whitespace)");
        check(space.advanceX == (float)spaceAdvance.x(),
              "' ' reports the face's own space advance (%.3f px, Qt says %.3f px), not a fallback character's",
              (double)space.advanceX, (double)spaceAdvance.x());
        check(space.sourceIdx == 0, "' ' comes from source 0 (Geist), like every other ASCII codepoint");
    }

    printf("\n== 2c. the panel's own size: advances are WHOLE pixels ==\n");
    {
        // WHY THIS IS A GATE. gui::GraphPanelFrame draws the panel's numbers with this loader,
        // at the size panelFontPixelSize() derives, and ImGui truncates a run's origin ONCE
        // and then accumulates each glyph's AdvanceX in floats (ImFont::RenderText's "align to
        // be pixel perfect"). A fractional advance therefore puts every glyph after the first
        // on a fractional column, and the atlas is sampled with GL_LINEAR - so each of them is
        // bilinearly blurred while the first is sharp. That is what made the graph panel's
        // ruler numbers read blurrier than the timeline's beside them: MEASURED at 12px the
        // digit advance is 7.1875, the run "0123456789" put its glyphs at 8, 16.1875, 22.375,
        // 29.5625 ..., and the rendered frame carried 6.4% of the run's inked pixels at full
        // coverage against Qt's 12.7% (the first glyph, the only one on a whole pixel, matched
        // Qt byte for byte). installQtFontLoader() adds every source with PixelSnapH, and
        // ImGui applies it in BOTH the render and the metrics path, so what is drawn and what
        // CalcTextSize() measures are the same rounded number - which is why the assertion is
        // on the advance the atlas carries and not on a drawn pixel.
        QFont panelFont(QStringLiteral("Geist"));
        panelFont.setPointSizeF(9.0); // what the application sets: 9pt -> 12px at 96 dpi
        const float panelSize = gui::imgui_qt::panelFontPixelSize(panelFont, 96.0);
        ImFontAtlas panelAtlas;
        const int panelSources = gui::imgui_qt::installQtFontLoader(panelAtlas, panelFont, panelSize, 1);
        panelAtlas.Build();
        QRawFont panelFace;
        panelFace.loadFromData(geistBytes, panelSize, QFont::PreferDefaultHinting);
        printf("panel size: installQtFontLoader(%.1f) -> %d sources; face valid=%d\n", (double)panelSize,
               panelSources, panelFace.isValid() ? 1 : 0);
        check(panelSize > 0.0f && panelSources >= 1 && panelFace.isValid(),
              "the panel's size (%.1f px) installs %d source(s) and Qt resolves the face", (double)panelSize,
              panelSources);
        float firstDigitAdvance = 0.0f;
        float firstFaceAdvance = 0.0f;
        bool wholePixels = true;
        bool tabular = true;
        bool roundedFromQt = true;
        for (ImWchar codepoint = '0'; codepoint <= '9'; ++codepoint) {
            const GlyphProbe digit = probeGlyph(panelAtlas, panelSize, codepoint);
            const QList<quint32> indexes = panelFace.glyphIndexesForString(QString(QChar(codepoint)));
            QPointF faceAdvance;
            const bool haveIndex = !indexes.isEmpty() && indexes.at(0) != 0 &&
                panelFace.advancesForGlyphIndexes(&indexes[0], &faceAdvance, 1);
            if (codepoint == '0') {
                firstDigitAdvance = digit.advanceX;
                firstFaceAdvance = (float)faceAdvance.x();
            }
            wholePixels = wholePixels && digit.found && (digit.advanceX == std::floor(digit.advanceX));
            tabular = tabular && (digit.advanceX == firstDigitAdvance);
            roundedFromQt = roundedFromQt && haveIndex &&
                (std::lround(digit.advanceX) == std::lround(faceAdvance.x()));
            printf("  digit '%c' @%.1fpx: found=%d advance=%.4f (Qt %.4f, rounded %ld)\n", (char)codepoint,
                   (double)panelSize, digit.found ? 1 : 0, (double)digit.advanceX, (double)faceAdvance.x(),
                   haveIndex ? std::lround(faceAdvance.x()) : -1L);
        }
        check(tabular, "every digit reports the same advance (%.4f px): the face's figures are tabular",
              (double)firstDigitAdvance);
        check(wholePixels,
              "every digit's advance is a WHOLE number of pixels (%.1f px) - a fractional one is what put the "
              "panel's glyphs after the first on half pixels and blurred them", (double)firstDigitAdvance);
        check(roundedFromQt,
              "the whole-pixel advance is Qt's own advance ROUNDED (%.4f -> %.1f px), not a different metric",
              (double)firstFaceAdvance, (double)firstDigitAdvance);
    }

    printf("\n== 3. CJK: U+4E00 from a FALLBACK source ==\n");
    printSourceCoverage(atlas, (ImWchar)0x4E00);
    const int cjkSource = firstSourceCovering(atlas, (ImWchar)0x4E00);
    // Precondition, asserted rather than skipped: if no fallback covers U+4E00 the CJK path
    // regressed and this run must fail loudly.
    check(cjkSource >= 1, "a FALLBACK source (index >= 1) covers U+4E00; first one is source %d", cjkSource);
    const bool primaryHasCjk = atlas.FontLoader->FontSrcContainsGlyph(&atlas, &atlas.Sources[0], (ImWchar)0x4E00) != 0;
    check(!primaryHasCjk, "the application-font source (\"%s\") reports NO U+4E00, so the fallback is really carrying it",
          atlas.Sources[0].Name);
    if (cjkSource >= 1)
    {
        const QString family = QString::fromUtf8(atlas.Sources[cjkSource].Name);
        const QRawFont fallbackFace = faceForFamily(appFont, family);
        printf("  the covering source is family \"%s\": valid=%d supports(U+4E00)=%d supports('A')=%d\n",
               qPrintable(family), fallbackFace.isValid() ? 1 : 0,
               fallbackFace.supportsCharacter(0x4E00u) ? 1 : 0, fallbackFace.supportsCharacter((uint)'A') ? 1 : 0);
        check(fallbackFace.isValid() && fallbackFace.supportsCharacter(0x4E00u),
              "the fallback face \"%s\" reports supportsCharacter(U+4E00) = true while Geist reports false",
              qPrintable(family));
        check(!appFace.supportsCharacter(0x4E00u), "Geist itself reports supportsCharacter(U+4E00) = false");
        const GlyphProbe cjk = probeGlyph(atlas, kSize, (ImWchar)0x4E00);
        printf("U+4E00 @%.0fpx: found=%d source=%d bitmap=%dx%d ink=%ld advance=%.3f box=(%.2f,%.2f)-(%.2f,%.2f)\n",
               kSize, cjk.found ? 1 : 0, cjk.sourceIdx, cjk.bitmapWidth, cjk.bitmapHeight, cjk.inkPixels,
               (double)cjk.advanceX, (double)cjk.x0, (double)cjk.y0, (double)cjk.x1, (double)cjk.y1);
        check(cjk.found && cjk.sourceIdx == cjkSource, "U+4E00 was rasterized by fallback source %d", cjkSource);
        check(cjk.inkPixels > 0, "U+4E00 has ink from the fallback source: %ld non-zero alpha pixels (U+4E00 is the "
                                 "single horizontal stroke, so a thin %dx%d rectangle is the correct shape)",
              cjk.inkPixels, cjk.bitmapWidth, cjk.bitmapHeight);
        check(cjk.advanceX > 0.0f, "U+4E00 has a positive advance (%.3f px)", (double)cjk.advanceX);

        // Same byte-for-byte comparison on the fallback path: the CJK rectangle in the atlas
        // must be Qt's own rasterization of the resolved fallback face.
        QRawFont sizedFallbackFace = fallbackFace;
        sizedFallbackFace.setPixelSize(kSize);
        const QtRaster reference = rasterizeWithQt(sizedFallbackFace, 0x4E00u);
        const int mismatches = cjk.hasRect ? atlasVersusQtMismatches(atlas, cjk.rect, reference.bitmap) : -1;
        printf("  reference: Qt glyph index %u, %dx%d alpha map; atlas rectangle (%d,%d %dx%d) mismatches=%d\n",
               reference.glyphIndex, reference.bitmap.width(), reference.bitmap.height(),
               cjk.rect.x, cjk.rect.y, cjk.rect.w, cjk.rect.h, mismatches);
        check(reference.ok && mismatches == 0,
              "the atlas rectangle for U+4E00 equals QRawFont::alphaMapForGlyph() for the fallback face, "
              "pixel for pixel (%d mismatching pixels)", mismatches);
        // A denser ideograph as a second witness, so the result does not rest on a glyph that
        // is a single stroke.
        const GlyphProbe middle = probeGlyph(atlas, kSize, (ImWchar)0x4E2D);
        printf("U+4E2D @%.0fpx: found=%d source=%d bitmap=%dx%d ink=%ld advance=%.3f (second witness)\n",
               kSize, middle.found ? 1 : 0, middle.sourceIdx, middle.bitmapWidth, middle.bitmapHeight,
               middle.inkPixels, (double)middle.advanceX);
        check(middle.found && middle.sourceIdx == cjkSource && middle.inkPixels > 0 && middle.advanceX > 0.0f,
              "U+4E2D also comes from fallback source %d with ink and a positive advance", cjkSource);
    }
    else
    {
        printf("  (dependent CJK checks not run: the precondition above failed)\n");
    }

    printf("\n== 4. Korean: U+AC00 ==\n");
    const QRawFont koreanFace = QRawFont::fromFont(appFont, QFontDatabase::Korean);
    const bool koreanResolved = koreanFace.isValid() &&
                                koreanFace.familyName() != appFace.familyName() &&
                                koreanFace.supportsCharacter(0xAC00u);
    printf("QRawFont::fromFont(appFont, Korean): valid=%d family=\"%s\" supports(U+AC00)=%d\n",
           koreanFace.isValid() ? 1 : 0, qPrintable(koreanFace.familyName()),
           koreanFace.supportsCharacter(0xAC00u) ? 1 : 0);
    printf("  writing systems that resolved on this machine: %s\n", qPrintable(resolvedFamilies.join(QStringLiteral(", "))));
    printSourceCoverage(atlas, (ImWchar)0xAC00);
    const int hangulSource = firstSourceCovering(atlas, (ImWchar)0xAC00);
    if (hangulSource >= 0)
    {
        check(koreanResolved, "U+AC00 is covered by source %d and a Korean face resolved here (\"%s\")",
              hangulSource, qPrintable(koreanFace.familyName()));
        const GlyphProbe hangul = probeGlyph(atlas, kSize, (ImWchar)0xAC00);
        printf("U+AC00 @%.0fpx: found=%d source=%d bitmap=%dx%d ink=%ld advance=%.3f\n",
               kSize, hangul.found ? 1 : 0, hangul.sourceIdx, hangul.bitmapWidth, hangul.bitmapHeight,
               hangul.inkPixels, (double)hangul.advanceX);
        check(hangul.found && hangul.inkPixels > 0 && hangul.advanceX > 0.0f,
              "U+AC00 has ink (%ld pixels) and a positive advance (%.3f px), rasterized by source %d",
              hangul.inkPixels, (double)hangul.advanceX, hangul.sourceIdx);
        // Source order decides: on this machine the Japanese source's face covers Hangul too, so
        // it wins for U+AC00 even though the Korean face is installed as a later source. The
        // Korean source is not dead - it answers for whatever the earlier face lacks - but the
        // measured routing is worth stating rather than implying.
        const int koreanSourceIdx = [&atlas, &koreanFace]() {
            for (int i = 0; i < atlas.Sources.Size; ++i)
                if (QString::fromUtf8(atlas.Sources[i].Name) == koreanFace.familyName())
                    return i;
            return -1;
        }();
        printf("  routing: source %d answers U+AC00; the Korean face \"%s\" is installed as a later source "
               "(source %d) and is only reached for codepoints the earlier face lacks\n",
               hangul.sourceIdx, qPrintable(koreanFace.familyName()), koreanSourceIdx);
        check(koreanSourceIdx < 0 || firstSourceCovering(atlas, (ImWchar)0xAC00) <= koreanSourceIdx,
              "routing follows source order for U+AC00 (%d <= the Korean source %d)",
              firstSourceCovering(atlas, (ImWchar)0xAC00), koreanSourceIdx);
    }
    else
    {
        // "or - if no Korean face resolves on this machine - assert exactly what is true"
        check(!koreanResolved, "no installed source covers U+AC00, and consistently no Korean face resolves here");
        printf("  (no source covers U+AC00: the resolved writing systems are listed above)\n");
    }

    printf("\n== 5. ImFontBaked::Size is honoured ==\n");
    const GlyphProbe small = probeGlyph(atlas, 16.0f, (ImWchar)0x4E00);
    const GlyphProbe large = probeGlyph(atlas, 40.0f, (ImWchar)0x4E00);
    printf("U+4E00 @16px: found=%d bitmap=%dx%d ink=%ld advance=%.3f | @40px: found=%d bitmap=%dx%d ink=%ld advance=%.3f\n",
           small.found ? 1 : 0, small.bitmapWidth, small.bitmapHeight, small.inkPixels, (double)small.advanceX,
           large.found ? 1 : 0, large.bitmapWidth, large.bitmapHeight, large.inkPixels, (double)large.advanceX);
    check(small.found && large.found, "U+4E00 bakes at both 16px and 40px");
    check(small.bitmapWidth != large.bitmapWidth || small.bitmapHeight != large.bitmapHeight,
          "the same codepoint produces different bitmap dimensions at 16px (%dx%d) and 40px (%dx%d), "
          "so ImFontBaked::Size drives rasterization",
          small.bitmapWidth, small.bitmapHeight, large.bitmapWidth, large.bitmapHeight);
    const GlyphProbe latinSmall = probeGlyph(atlas, 16.0f, (ImWchar)'A');
    const GlyphProbe latinLarge = probeGlyph(atlas, 40.0f, (ImWchar)'A');
    printf("'A' @16px: bitmap=%dx%d advance=%.3f | @40px: bitmap=%dx%d advance=%.3f\n",
           latinSmall.bitmapWidth, latinSmall.bitmapHeight, (double)latinSmall.advanceX,
           latinLarge.bitmapWidth, latinLarge.bitmapHeight, (double)latinLarge.advanceX);
    check(latinSmall.bitmapHeight != latinLarge.bitmapHeight && latinLarge.advanceX > latinSmall.advanceX,
          "'A' scales too: %dpx tall at 16px vs %dpx at 40px, advance %.3f -> %.3f",
          latinSmall.bitmapHeight, latinLarge.bitmapHeight, (double)latinSmall.advanceX, (double)latinLarge.advanceX);

    printf("\n== 6. dynamic atlas: a codepoint in no range, then one too large to fit ==\n");
    printf("baseline: %s (updates after Build() = %d)\n", qPrintable(describeTexture(atlas)), updatesAfterBuild);
    for (int i = 0; i < atlas.Sources.Size; ++i)
        printf("  source %d declares glyphRanges=%s\n", i, atlas.Sources[i].GlyphRanges ? "set" : "NULL");

    ImTextureData* before = atlas.TexData;
    const int beforeUpdates = before->Updates.Size;
    const int beforeUsedW = (int)before->UsedRect.w;
    const int beforeUniqueId = before->UniqueID;
    const int beforeWidth = before->Width;
    const int beforeHeight = before->Height;
    const int beforeTexCount = atlas.TexList.Size;

    // U+3042 (HIRAGANA LETTER A) is in no source's declared range and has never been asked for.
    const GlyphProbe late = probeGlyph(atlas, kSize, (ImWchar)0x3042);
    ImTextureData* after = atlas.TexData;
    printf("after requesting U+3042: found=%d source=%d bitmap=%dx%d ink=%ld\n",
           late.found ? 1 : 0, late.sourceIdx, late.bitmapWidth, late.bitmapHeight, late.inkPixels);
    printf("  %s\n", qPrintable(describeTexture(atlas)));
    check(late.found && late.inkPixels > 0, "U+3042 (declared in no range) loaded lazily with ink (%ld pixels)", late.inkPixels);
    const bool queued = after == before && after->Status == ImTextureStatus_WantUpdates && after->Updates.Size > beforeUpdates;
    const bool regrown = after != before || after->UniqueID != beforeUniqueId ||
                         after->Width != beforeWidth || after->Height != beforeHeight ||
                         atlas.TexList.Size > beforeTexCount;
    check(queued || regrown,
          "requesting a codepoint that was in no range changed the atlas: queued=%d (updates %d -> %d, status %d), "
          "grown=%d (texture #%d %dx%d, TexList %d -> %d)",
          queued ? 1 : 0, beforeUpdates, after->Updates.Size, (int)after->Status, regrown ? 1 : 0,
          after->UniqueID, after->Width, after->Height, beforeTexCount, atlas.TexList.Size);
    check(after->UsedRect.w >= beforeUsedW, "the atlas used rectangle did not shrink (%d -> %d)",
          beforeUsedW, (int)after->UsedRect.w);

    // 400px is bigger than the atlas can hold next to its existing content, so packing has to
    // make space: ImFontAtlasPackAddRect -> ImFontAtlasTextureMakeSpace -> texture growth.
    const int beforeGrowTexCount = atlas.TexList.Size;
    const int beforeGrowUniqueId = atlas.TexData->UniqueID;
    const int beforeGrowWidth = atlas.TexData->Width;
    const int beforeGrowHeight = atlas.TexData->Height;
    const GlyphProbe huge = probeGlyph(atlas, 400.0f, (ImWchar)0x4E2D);
    ImTextureData* grown = atlas.TexData;
    printf("after requesting U+4E2D at 400px: found=%d bitmap=%dx%d ink=%ld advance=%.1f\n",
           huge.found ? 1 : 0, huge.bitmapWidth, huge.bitmapHeight, huge.inkPixels, (double)huge.advanceX);
    printf("  %s\n", qPrintable(describeTexture(atlas)));
    check(huge.found && huge.inkPixels > 0, "the 400px U+4E2D glyph rasterized with ink (%ld pixels)", huge.inkPixels);
    check(atlas.TexList.Size > beforeGrowTexCount || grown->UniqueID != beforeGrowUniqueId ||
          grown->Width != beforeGrowWidth || grown->Height != beforeGrowHeight,
          "a glyph too large for the current texture grew the atlas: TexList %d -> %d, texture #%d %dx%d -> #%d %dx%d",
          beforeGrowTexCount, atlas.TexList.Size, beforeGrowUniqueId, beforeGrowWidth, beforeGrowHeight,
          grown->UniqueID, grown->Width, grown->Height);

    printf("\n== 7. rasterizer density (aDpr): glyphs re-rasterize when the DPI scale changes ==\n");
    {
        // A second, independent atlas installed at dpr=2. The density must multiply the raster
        // size (crisper bitmaps) while the glyph metrics stay in layout pixels - the reference
        // loaders' behaviour, and what makes a bake keyed on a different density produce a
        // different bitmap for the same ImFontBaked::Size.
        ImFontAtlas denseAtlas;
        const int denseSources = gui::imgui_qt::installQtFontLoader(denseAtlas, appFont, kSize, 2);
        check(denseSources >= 2, "the dpr=2 atlas installed %d sources", denseSources);
        for (int i = 0; i < denseAtlas.Sources.Size; ++i)
            printf("  dpr=2 source %d: \"%s\" rasterizerDensity=%.2f\n", i, denseAtlas.Sources[i].Name,
                   (double)denseAtlas.Sources[i].RasterizerDensity);
        denseAtlas.Build();
        ImFontBaked* thinBaked = atlas.Fonts[0]->GetFontBaked(kSize);      // density 1 (the first atlas)
        ImFontBaked* denseBaked = denseAtlas.Fonts[0]->GetFontBaked(kSize); // density 2
        printf("bake density 1: ascent=%.1f descent=%.1f | density 2: ascent=%.1f descent=%.1f\n",
               (double)thinBaked->Ascent, (double)thinBaked->Descent,
               (double)denseBaked->Ascent, (double)denseBaked->Descent);
        const GlyphProbe denseLatin = probeGlyph(denseAtlas, kSize, (ImWchar)'A');
        printf("'A' @%.0fpx: dpr=1 bitmap=%dx%d advance=%.3f | dpr=2 bitmap=%dx%d advance=%.3f\n",
               kSize, latin.bitmapWidth, latin.bitmapHeight, (double)latin.advanceX,
               denseLatin.bitmapWidth, denseLatin.bitmapHeight, (double)denseLatin.advanceX);
        check(denseLatin.found && denseLatin.bitmapWidth > latin.bitmapWidth &&
              denseLatin.bitmapHeight > latin.bitmapHeight,
              "the same size rasterized at dpr=2 has a larger bitmap (%dx%d vs %dx%d): the density "
              "reaches the rasterizer",
              denseLatin.bitmapWidth, denseLatin.bitmapHeight, latin.bitmapWidth, latin.bitmapHeight);
        // ImGui multiplies the source's RasterizerDensity by the bake's own density (AddFont
        // initialises font->CurrentRasterizerDensity from the same field), so dpr=2 rasters at
        // 20 x 2 x 2 = 80px. That is ImGui's arithmetic, not this loader's - the stb and
        // FreeType loaders compute the same product.
        check(denseLatin.bitmapWidth >= 4 * latin.bitmapWidth - 4 && denseLatin.bitmapHeight >= 4 * latin.bitmapHeight - 4,
              "the dpr=2 bitmap is about 4x the dpr=1 bitmap per axis (%dx%d vs %dx%d): Size x source "
              "density x bake density = 20 x 2 x 2 = 80px of raster",
              denseLatin.bitmapWidth, denseLatin.bitmapHeight, latin.bitmapWidth, latin.bitmapHeight);
        check(near(denseLatin.advanceX, latin.advanceX, 0.01f),
              "the dpr=2 advance is unchanged (%.3f px vs %.3f px): density is a rasterization detail, "
              "not a layout change",
              (double)denseLatin.advanceX, (double)latin.advanceX);
        check(near(denseBaked->Ascent, thinBaked->Ascent, 1.0f) && near(denseBaked->Descent, thinBaked->Descent, 1.0f),
              "ascent/descent are unchanged by the density (%.1f/%.1f vs %.1f/%.1f)",
              (double)denseBaked->Ascent, (double)denseBaked->Descent, (double)thinBaked->Ascent, (double)thinBaked->Descent);
    }

    printf("\n== 8. a fractional requested pixel size ==\n");
    {
        // aPixelSize is a float and the app's own metrics are fractional; ImGui rounds the size
        // it bakes with (GetRoundedFontSize == IM_ROUND), so the loader must not care.
        ImFontAtlas fractionalAtlas;
        const int fractionalSources = gui::imgui_qt::installQtFontLoader(fractionalAtlas, appFont, 13.5f, 1);
        fractionalAtlas.Build();
        const GlyphProbe fractionalLatin = probeGlyph(fractionalAtlas, 13.5f, (ImWchar)'A');
        const GlyphProbe fractionalCjk = probeGlyph(fractionalAtlas, 13.5f, (ImWchar)0x4E00);
        ImFontBaked* fractionalBaked = fractionalAtlas.Fonts[0]->GetFontBaked(13.5f);
        printf("installQtFontLoader(13.5) -> %d sources; source0 sizePixels=%.2f; baked Size=%.2f ascent=%.1f\n",
               fractionalSources, (double)fractionalAtlas.Sources[0].SizePixels,
               (double)fractionalBaked->Size, (double)fractionalBaked->Ascent);
        printf("'A' @13.5 -> found=%d bitmap=%dx%d advance=%.3f | U+4E00 -> found=%d source=%d bitmap=%dx%d ink=%ld\n",
               fractionalLatin.found ? 1 : 0, fractionalLatin.bitmapWidth, fractionalLatin.bitmapHeight,
               (double)fractionalLatin.advanceX, fractionalCjk.found ? 1 : 0, fractionalCjk.sourceIdx,
               fractionalCjk.bitmapWidth, fractionalCjk.bitmapHeight, fractionalCjk.inkPixels);
        check(fractionalSources >= 2, "a fractional aPixelSize installs the same %d sources", fractionalSources);
        check(fractionalBaked != NULL && fractionalBaked->Size == 14.0f,
              "ImGui rounds the requested 13.5 to a 14px bake (that is what the loader rasterizes at)");
        check(fractionalLatin.found && fractionalLatin.inkPixels > 0 && fractionalLatin.advanceX > 0.0f,
              "'A' rasterizes at the fractional request");
        check(fractionalCjk.found && fractionalCjk.sourceIdx >= 1 && fractionalCjk.inkPixels > 0,
              "U+4E00 still routes to fallback source %d at the fractional request", fractionalCjk.sourceIdx);
    }

    printf("\n== 9. the 0-return contract: a loader installed before the call survives it ==\n");
    {
        // ImGuiQtFontLoader.h promises that a 0 return "adds no source and leaves no loader of
        // ours behind", so the loader the atlas held before the call must be the one it holds
        // after. The only witness available for that is pointer identity, so a marker loader
        // is installed through the public ImFontAtlas::SetFontLoader() and found again by
        // pointer. It has no hooks: an atlas with no builder runs none of them, and the atlas
        // it is installed on here never gets a font.
        static ImFontLoader marker;
        marker.Name = "test/marker";

        ImFontAtlas markerAtlas;
        markerAtlas.SetFontLoader(&marker);
        check(markerAtlas.FontLoader == &marker && markerAtlas.FontLoaderName != NULL &&
              strcmp(markerAtlas.FontLoaderName, marker.Name) == 0,
              "the marker loader is installed on the atlas before the call (FontLoaderName=\"%s\")",
              markerAtlas.FontLoaderName ? markerAtlas.FontLoaderName : "<null>");

        // (a) a non-positive pixel size: refused before the atlas is touched.
        const int zeroSources = gui::imgui_qt::installQtFontLoader(markerAtlas, appFont, 0.0f, 1);
        const int negativeSources = gui::imgui_qt::installQtFontLoader(markerAtlas, appFont, -1.0f, 1);
        printf("installQtFontLoader(size=0) -> %d; (size=-1) -> %d; FontLoader=%p (\"%s\") Sources=%d RendererHasTextures=%d\n",
               zeroSources, negativeSources, (void*)markerAtlas.FontLoader,
               markerAtlas.FontLoaderName ? markerAtlas.FontLoaderName : "<null>",
               markerAtlas.Sources.Size, markerAtlas.RendererHasTextures ? 1 : 0);
        check(zeroSources == 0 && negativeSources == 0,
              "a non-positive pixel size returns 0 (%d and %d)", zeroSources, negativeSources);
        check(markerAtlas.FontLoader == &marker && markerAtlas.FontLoaderName != NULL &&
              strcmp(markerAtlas.FontLoaderName, marker.Name) == 0 &&
              markerAtlas.Sources.Size == 0 && markerAtlas.Fonts.Size == 0,
              "both 0 returns left the loader that was installed alone and added nothing");

        // (b) an atlas that already holds sources: also refused before the atlas is touched.
        ImFontAtlas busyAtlas;
        const int busySources = gui::imgui_qt::installQtFontLoader(busyAtlas, appFont, kSize, 1);
        const int secondInstall = gui::imgui_qt::installQtFontLoader(busyAtlas, appFont, kSize, 1);
        check(secondInstall == 0 && busyAtlas.FontLoader != NULL && busyAtlas.FontLoaderName != NULL &&
              strcmp(busyAtlas.FontLoaderName, gui::imgui_qt::loaderName()) == 0 &&
              busyAtlas.Sources.Size == busySources,
              "a second install on the atlas that already holds %d sources returns 0 and changes nothing",
              busySources);

        // (c) no previous loader: there must be none afterwards either. That is the state in
        // which ImGui's own ImFontAtlasBuildInit() selects a default loader at all.
        ImFontAtlas emptyAtlas;
        const int emptyReturn = gui::imgui_qt::installQtFontLoader(emptyAtlas, appFont, 0.0f, 1);
        check(emptyReturn == 0 && emptyAtlas.FontLoader == NULL && emptyAtlas.Sources.Size == 0,
              "a fresh atlas is still loader-less after a 0 return, so ImGui's default still applies");

        // (d) the primary font cannot be opened. This is the refused-install path - the 0
        // return the loader cannot decide before touching the atlas, because the atlas install
        // is what runs FontSrcInit() over the bytes. readPrimaryFontData() reads the checkout's
        // assets/fonts/Geist-Regular.ttf (checked below: the :/fonts resource is not registered
        // in this binary), so calling the install from a directory whose Geist file is not a
        // font is what makes the primary source's AddFont() refuse it.
        QTemporaryDir brokenTree;
        check(brokenTree.isValid(), "a temporary directory for the unopenable-font case exists");
        if (brokenTree.isValid())
        {
            const QString originalCwd = QDir::currentPath();
            QDir(brokenTree.path()).mkpath(QStringLiteral("assets/fonts"));
            QFile brokenFont(brokenTree.path() + QStringLiteral("/assets/fonts/Geist-Regular.ttf"));
            const QByteArray notAFont(400, '\0');
            const bool wrote = brokenFont.open(QIODevice::WriteOnly) &&
                               brokenFont.write(notAFont) == notAFont.size();
            brokenFont.close();
            const bool resourceAbsent = !QFile::exists(QStringLiteral(":/fonts/Geist-Regular.ttf"));
            const bool moved = QDir::setCurrent(brokenTree.path());

            ImFontAtlas brokenAtlas;
            brokenAtlas.SetFontLoader(&marker);
            const int brokenReturn = gui::imgui_qt::installQtFontLoader(brokenAtlas, appFont, kSize, 1);
            const bool markerSurvived = brokenAtlas.FontLoader == &marker &&
                                        brokenAtlas.FontLoaderName != NULL &&
                                        strcmp(brokenAtlas.FontLoaderName, marker.Name) == 0;
            const int brokenSources = brokenAtlas.Sources.Size;
            const int brokenFonts = brokenAtlas.Fonts.Size;
            const bool brokenRendererFlag = brokenAtlas.RendererHasTextures;
            const bool brokenBuilder = brokenAtlas.Builder != NULL;
            if (moved)
                QDir::setCurrent(originalCwd);

            printf("unopenable Geist (%d zero bytes): resource absent=%d; install returned %d; FontLoader=%p (\"%s\") "
                   "Sources=%d Fonts=%d RendererHasTextures=%d Builder=%d\n",
                   (int)notAFont.size(), resourceAbsent ? 1 : 0, brokenReturn, (void*)brokenAtlas.FontLoader,
                   brokenAtlas.FontLoaderName ? brokenAtlas.FontLoaderName : "<null>",
                   brokenSources, brokenFonts, brokenRendererFlag ? 1 : 0, brokenBuilder ? 1 : 0);
            check(wrote && moved && resourceAbsent,
                  "the unopenable-font case could be staged: the file was written, the process moved into the "
                  "temporary directory, and no :/fonts resource shadows the checkout copy");
            check(brokenReturn == 0, "installQtFontLoader() returned %d for an unopenable primary font", brokenReturn);
            check(markerSurvived,
                  "the 0 return restored the loader that was installed before the call (FontLoader=%p is the "
                  "marker installed above)", (void*)brokenAtlas.FontLoader);
            check(brokenSources == 0 && brokenFonts == 0,
                  "the refused install added no source (%d) and no font (%d)", brokenSources, brokenFonts);
            check(brokenRendererFlag == false,
                  "the refused install put RendererHasTextures back (false again, as before the call)");
            check(!brokenBuilder,
                  "the refused install discarded the builder the failed attempt created, so the next "
                  "AddFont()/Build() re-runs ImFontAtlasBuildInit() and selects ImGui's default loader");
        }
    }

    printf("\n== summary ==\n");
    printf("%d checks, %d failures\n", gChecks, gFailures);
    printf(gFailures == 0 ? "ALL MEASURED CLAIMS HELD\n" : "THERE WERE FAILURES\n");
    return gFailures == 0 ? 0 : 1;
}
