// A Dear ImGui 1.92 ImFontLoader that rasterizes glyphs through Qt's QRawFont.
//
// WHY. ImGui's built-in loader is stb_truetype, which cannot open the face Qt uses
// for CJK here: loading NotoSansCJK-VF.ttc makes stbtt_FindGlyphIndex abort
// (build/_deps/imgui-src/imstb_truetype.h:1590) [measured]. QRawFont is the same
// face the rest of the application draws with (Qt font database -> FreeType), so
// going through it renders CJK text with the application's own font stack and left
// ImGui's sources untouched.
//
// SHAPE. The two reference implementations this file mirrors are the stb loader at
// the end of build/_deps/imgui-src/imgui_draw.cpp (ImFontAtlasGetFontLoaderForStbTruetype)
// and build/_deps/imgui-src/misc/freetype/imgui_freetype.cpp (ImGuiFreeType::GetFontLoader):
// one loader instance, one opaque per-atlas state, one opaque struct per source, one
// per (source, baked) stride inside the buffer ImGui allocates, and glyphs written
// into the atlas with ImFontAtlasBakedSetFontGlyphBitmap().

#include "gui/ImGuiQtFontLoader.h"

#include <cstring>

#include <QByteArray>
#include <QCoreApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QImage>
#include <QList>
#include <QRawFont>
#include <QString>
#include <QStringList>
#include <QTransform>

#include "imgui.h"
#include "imgui_internal.h"

namespace gui {
namespace imgui_qt {
namespace {

//-----------------------------------------------------------------------------
// Source description, carried in ImFontConfig::FontLoaderFlags.
//
// ImFontConfig has no spare field an installer may fill in (AddFont() asserts that
// FontLoaderData is NULL when it calls FontSrcInit()), so the *kind* of a source has
// to travel inside a field FontSrcInit() can read. FontLoaderFlags is documented as
// builder-implementation-dependent ("Settings for custom font builder"), which is
// exactly what this is; ImGui itself never looks at it.
//-----------------------------------------------------------------------------

constexpr unsigned int kSourceKindShift = 0;
constexpr unsigned int kSourceKindMask = 0x1u << kSourceKindShift;
constexpr unsigned int kSourceKind_FileData = 0u;      // face parsed from src->FontData
constexpr unsigned int kSourceKind_WritingSystem = 1u; // face resolved by Qt for a writing system
constexpr unsigned int kWritingSystemShift = 8;
constexpr unsigned int kWritingSystemMask = 0xFFu << kWritingSystemShift;

constexpr unsigned int makeSourceLoaderFlags(unsigned int aKind, unsigned int aWritingSystem)
{
    return (aKind << kSourceKindShift) |
           ((aWritingSystem << kWritingSystemShift) & kWritingSystemMask);
}

// Stand-in FontData byte for a writing-system source. AddFont() requires *some* font
// data (or a per-source loader, which ImGui rejects for a loader that has
// LoaderInit/LoaderShutdown: "FIXME-NEWATLAS: Unsupported yet"), but such a source is
// resolved from Qt's font database and this byte is never read - FontSrcInit() looks
// at FontLoaderFlags instead. Not owned by the atlas, so ImGui never frees it.
unsigned char gUnusedSourceByte = 0;

//-----------------------------------------------------------------------------
// Per-atlas state (atlas->FontLoaderData), owned by the loader.
//
// WHY THERE IS NO GLYPH CACHE IN HERE. ImGui's ImFontBaked pool already is the
// cache: it holds IndexAdvanceX/IndexLookup/Glyphs per (font, size, density) and
// calls FontBakedLoadGlyph() only for a codepoint it has never loaded, so a second
// cache keyed by (source, codepoint, pixel size, density) would duplicate that
// storage and could go stale against the atlas rectangles, which are remapped
// wholesale on repack/grow. This state therefore holds only what the hooks cannot
// recover by themselves.
//-----------------------------------------------------------------------------

struct AtlasData
{
    QFont appFont;              // the font installQtFontLoader() was handed
    bool  appFontValid = false; // false until the installer has handed it over
};

//-----------------------------------------------------------------------------
// Per-source state (ImFontConfig::FontLoaderData), owned by the loader.
//-----------------------------------------------------------------------------

struct FontSrcData
{
    // The face, at the source's own SizePixels. Every bake gets a *copy* of this with
    // its own pixel size set (see FontSrcBakedData), so one QRawFont per source is
    // enough and no "which size is this face currently at" bookkeeping is needed -
    // unlike the FreeType loader, which activates sizes on one shared FT_Face.
    QRawFont rawFont;
};

//-----------------------------------------------------------------------------
// Per (source, baked) state: one instance inside ImFontBaked::FontLoaderDatas,
// stride ImFontLoader::FontBakedSrcLoaderDataSize. ImGui hands over uninitialized
// memory, so the object is placement-constructed in FontBakedInit() and destroyed in
// FontBakedDestroy().
//-----------------------------------------------------------------------------

struct FontSrcBakedData
{
    QRawFont rawFont;                 // the source face at this bake's raster pixel size
    float    rasterDensity = 1.0f;    // src->RasterizerDensity * baked->RasterizerDensity
};

//-----------------------------------------------------------------------------
// Helpers
//-----------------------------------------------------------------------------

// Map one codepoint through one face. Returns 0 when the face has no glyph for it.
//
// MEASURED, and the reason this is not just supportsCharacter(): DroidSansFallbackFull
// (the face Qt resolves for Japanese/Chinese here) maps 'A' to glyph 0, and Qt reports
// that as index 0 - the same value it reports for "no mapping". Index 0 is therefore
// never a hit, exactly as in the stb and FreeType loaders.
//
// MEASURED Qt API detail: glyphIndexesForChars() takes the destination capacity as an
// in/out *numGlyphs and returns false when it is 0 on entry; it has to be seeded with
// the buffer size (1 here).
quint32 glyphIndexForCodepoint(const QRawFont& aFace, unsigned int aCodepoint)
{
    if (aCodepoint > 0x10FFFFu)
        return 0;
    if (aCodepoint > 0xFFFFu)
    {
        // Outside the BMP. Unreachable from ImGui when ImWchar is 16-bit (this build's
        // default: ImWchar is unsigned short, so a codepoint is always one UTF-16 unit),
        // and there is no per-codepoint QRawFont entry point above the BMP, so the
        // string one is used. NOT exercised by this job's test (see the header).
        const char32_t character = (char32_t)aCodepoint;
        const QList<quint32> indexes = aFace.glyphIndexesForString(QString::fromUcs4(&character, 1));
        return indexes.isEmpty() ? 0u : indexes.at(0);
    }
    quint32 glyphIndex = 0;
    int numGlyphs = 1;
    const QChar character = QChar((char16_t)aCodepoint);
    if (!aFace.glyphIndexesForChars(&character, 1, &glyphIndex, &numGlyphs))
        return 0;
    return glyphIndex;
}

// Layout size of one source at one bake, copied from the stb loader's arithmetic: a
// merged source with its own SizePixels is scaled relative to the primary source.
float layoutSizeForSource(const ImFontConfig* aSrc, const ImFontBaked* aBaked)
{
    float size = aBaked->Size;
    if (aSrc->MergeMode && aSrc->SizePixels != 0.0f && aBaked->OwnerFont != nullptr && !aBaked->OwnerFont->Sources.empty())
    {
        const float referenceSize = aBaked->OwnerFont->Sources[0]->SizePixels;
        if (referenceSize != 0.0f)
            size *= aSrc->SizePixels / referenceSize;
    }
    return size * aSrc->ExtraSizeScale;
}

void setSourceName(char (&aName)[40], const QString& aText)
{
    const QByteArray utf8 = aText.toUtf8();
    const int count = (utf8.size() < (int)sizeof(aName) - 1) ? utf8.size() : (int)sizeof(aName) - 1;
    memcpy(aName, utf8.constData(), (size_t)count);
    aName[count] = '\0';
}

// The application font's own file. The qrc resource first: that is the copy Main.cpp
// registers as the application font (assets/assets.qrc), so the two agree by
// construction. The checkout's copy is what a standalone run (this job's test) uses.
QByteArray readPrimaryFontData()
{
    const QStringList candidates = {
        QStringLiteral(":/fonts/Geist-Regular.ttf"),
        QStringLiteral("assets/fonts/Geist-Regular.ttf"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/assets/fonts/Geist-Regular.ttf"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/../assets/fonts/Geist-Regular.ttf")
    };
    for (const QString& path : candidates)
    {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            return file.readAll();
    }
    return QByteArray();
}

struct FallbackSource
{
    QFontDatabase::WritingSystem writingSystem;
    QString family;
};

// The CJK faces Qt itself would use for the application font, in the order they are
// tried. No font file path and no fontconfig call: QRawFont::fromFont(aAppFont, ws) is
// "the face Qt would use for this writing system".
//
// MEASURED on this machine with the app's Geist as the application font:
//   Japanese            -> "Droid Sans Fallback"
//   Korean              -> "Noto Sans CJK KR"
//   SimplifiedChinese   -> "Droid Sans Fallback"  (duplicate of Japanese)
//   TraditionalChinese  -> "Droid Sans Fallback"  (duplicate of Japanese)
// so four writing systems become two sources. Overlap *between* different faces is
// left alone: the chain is ordered, ImGui stops at the first source that has the
// codepoint, and a second face still answers for codepoints the first one lacks.
QList<FallbackSource> resolveFallbackSources(const QFont& aAppFont, const QString& aPrimaryFamily)
{
    static const QFontDatabase::WritingSystem kWritingSystems[] = {
        QFontDatabase::Japanese,
        QFontDatabase::Korean,
        QFontDatabase::SimplifiedChinese,
        QFontDatabase::TraditionalChinese
    };

    QList<FallbackSource> resolved;
    for (const QFontDatabase::WritingSystem writingSystem : kWritingSystems)
    {
        const QRawFont face = QRawFont::fromFont(aAppFont, writingSystem);
        if (!face.isValid())
            continue;
        const QString family = face.familyName();
        if (family.isEmpty() || family == aPrimaryFamily)
            continue; // Qt handed back the application font itself: not a fallback
        bool duplicate = false;
        for (const FallbackSource& kept : resolved)
            if (kept.family == family)
                duplicate = true;
        if (duplicate)
            continue; // one source already stands for this face
        const QString sample = QFontDatabase::writingSystemSample(writingSystem);
        if (sample.isEmpty() || !face.supportsCharacter(sample.at(0)))
            continue; // a face that cannot draw the script's sample is not the script's face
        resolved.append({ writingSystem, family });
    }
    return resolved;
}

//-----------------------------------------------------------------------------
// Loader hooks
//-----------------------------------------------------------------------------

AtlasData* atlasDataOf(ImFontAtlas* aAtlas)
{
    return (AtlasData*)aAtlas->FontLoaderData;
}

bool qtLoaderInit(ImFontAtlas* aAtlas)
{
    IM_ASSERT(aAtlas->FontLoaderData == nullptr);
    aAtlas->FontLoaderData = IM_NEW(AtlasData)();
    return true;
}

void qtLoaderShutdown(ImFontAtlas* aAtlas)
{
    AtlasData* data = atlasDataOf(aAtlas);
    IM_ASSERT(data != nullptr);
    IM_DELETE(data);
    aAtlas->FontLoaderData = nullptr;
}

bool qtFontSrcInit(ImFontAtlas* aAtlas, ImFontConfig* aSrc)
{
    AtlasData* data = atlasDataOf(aAtlas);
    IM_ASSERT(data != nullptr);

    // The face is resolved here rather than carried over from installQtFontLoader()
    // because ImFontConfig offers no field the installer may pre-fill; the fields the
    // installer *did* leave behind are the atlas state above and FontLoaderFlags.
    //
    // Mirror the reference loaders: a merged source with no size of its own inherits the
    // primary source's size, so the layout scale factor stays exactly 1. This has to happen
    // before the size is required below, not after.
    if (aSrc->MergeMode && aSrc->SizePixels == 0.0f && aSrc->DstFont != nullptr && !aSrc->DstFont->Sources.empty())
        aSrc->SizePixels = aSrc->DstFont->Sources[0]->SizePixels;

    const float pixelSize = aSrc->SizePixels;
    if (pixelSize <= 0.0f)
        return false; // QRawFont cannot be rasterized at an undefined size

    FontSrcData* srcData = IM_NEW(FontSrcData)();
    if ((aSrc->FontLoaderFlags & kSourceKindMask) == kSourceKind_WritingSystem)
    {
        const int writingSystem = (int)((aSrc->FontLoaderFlags & kWritingSystemMask) >> kWritingSystemShift);
        if (!data->appFontValid || writingSystem <= 0)
        {
            IM_DELETE(srcData);
            return false;
        }
        srcData->rawFont = QRawFont::fromFont(data->appFont, (QFontDatabase::WritingSystem)writingSystem);
    }
    else
    {
        if (aSrc->FontData == nullptr || aSrc->FontDataSize <= 0)
        {
            IM_DELETE(srcData);
            return false;
        }
        // Zero copy: ImGui keeps FontData alive for the atlas's whole life (the
        // documented 1.92 requirement) and the face is destroyed before ImGui frees it
        // (ImFontAtlasFontDestroyOutput() runs ahead of ImFontAtlasFontDestroySourceData()).
        const QByteArray fontData = QByteArray::fromRawData((const char*)aSrc->FontData, aSrc->FontDataSize);
        srcData->rawFont.loadFromData(fontData, pixelSize, QFont::PreferDefaultHinting);
    }
    if (!srcData->rawFont.isValid())
    {
        IM_DELETE(srcData);
        return false;
    }
    srcData->rawFont.setPixelSize(pixelSize);

    aSrc->FontLoaderData = srcData;
    return true;
}

void qtFontSrcDestroy(ImFontAtlas* aAtlas, ImFontConfig* aSrc)
{
    IM_UNUSED(aAtlas);
    // Reached twice on the way out: ImFontAtlas::ClearFonts() runs
    // ImFontAtlasBuildDestroy() and then ClearInputData(), and both funnel into
    // ImFontAtlasFontDestroyOutput(), which walks every source. The second pass must be
    // a no-op - the reference loaders tolerate it the same way (they call IM_DELETE on a
    // null pointer and never assert here).
    FontSrcData* srcData = (FontSrcData*)aSrc->FontLoaderData;
    if (srcData == nullptr)
        return;
    IM_DELETE(srcData);
    aSrc->FontLoaderData = nullptr;
}

bool qtFontSrcContainsGlyph(ImFontAtlas* aAtlas, ImFontConfig* aSrc, ImWchar aCodepoint)
{
    IM_UNUSED(aAtlas);
    const FontSrcData* srcData = (const FontSrcData*)aSrc->FontLoaderData;
    IM_ASSERT(srcData != nullptr);
    // Both halves are needed: supportsCharacter() alone says yes for a codepoint that
    // then maps to glyph 0 (DroidSansFallbackFull and 'A' [measured]), which is the
    // glyph index reserved for "no mapping".
    if (!srcData->rawFont.supportsCharacter((unsigned int)aCodepoint))
        return false;
    return glyphIndexForCodepoint(srcData->rawFont, aCodepoint) != 0;
}

bool qtFontBakedInit(ImFontAtlas* aAtlas, ImFontConfig* aSrc, ImFontBaked* aBaked, void* aBakedSrcData)
{
    IM_UNUSED(aAtlas);
    const FontSrcData* srcData = (const FontSrcData*)aSrc->FontLoaderData;
    IM_ASSERT(srcData != nullptr);

    FontSrcBakedData* bakedData = (FontSrcBakedData*)aBakedSrcData;
    IM_ASSERT(bakedData != nullptr);
    IM_PLACEMENT_NEW(bakedData) FontSrcBakedData();

    // Same density product as the reference loaders, so a bake keyed on a different
    // density gets its own face at its own raster size - that is what makes glyphs
    // re-rasterize when the DPI scale changes.
    const float density = aSrc->RasterizerDensity * aBaked->RasterizerDensity;
    bakedData->rasterDensity = (density > 0.0f) ? density : 1.0f;

    bakedData->rawFont = srcData->rawFont;
    bakedData->rawFont.setPixelSize(layoutSizeForSource(aSrc, aBaked) * bakedData->rasterDensity);

    if (aSrc->MergeMode == false)
    {
        // Same convention as the reference loaders (baked->Ascent positive, Descent
        // negative), read from the raster-resolution face and scaled back down by the
        // density. Qt reports descent positive (FreeType's descender is negative), hence
        // the sign; the rounding split (ceil up, floor down) is the FreeType loader's.
        aBaked->Ascent = ImCeil((float)bakedData->rawFont.ascent() / bakedData->rasterDensity);
        aBaked->Descent = -ImFloor((float)bakedData->rawFont.descent() / bakedData->rasterDensity);
    }
    return true;
}

void qtFontBakedDestroy(ImFontAtlas* aAtlas, ImFontConfig* aSrc, ImFontBaked* aBaked, void* aBakedSrcData)
{
    IM_UNUSED(aAtlas);
    IM_UNUSED(aSrc);
    IM_UNUSED(aBaked);
    FontSrcBakedData* bakedData = (FontSrcBakedData*)aBakedSrcData;
    IM_ASSERT(bakedData != nullptr);
    bakedData->~FontSrcBakedData();
}

bool qtFontBakedLoadGlyph(ImFontAtlas* aAtlas, ImFontConfig* aSrc, ImFontBaked* aBaked, void* aBakedSrcData,
                          ImWchar aCodepoint, ImFontGlyph* aOutGlyph, float* aOutAdvanceX)
{
    FontSrcBakedData* bakedData = (FontSrcBakedData*)aBakedSrcData;
    IM_ASSERT(bakedData != nullptr);

    const quint32 glyphIndex = glyphIndexForCodepoint(bakedData->rawFont, aCodepoint);
    if (glyphIndex == 0)
        return false; // 0 is both "the face has no such glyph" and the .notdef mapping [measured]

    const float reciprocalDensity = 1.0f / bakedData->rasterDensity;

    // NO OpenType feature is requested here, and none is needed: the application face's
    // digit glyphs have EQUAL advance widths at every size this loader rasterizes at, so
    // number strings cannot shift layout. [measured on Geist-Regular.ttf through
    // QRawFont::advancesForGlyphIndexes over '0'..'9' - which is the call below -
    // identical to 6 decimals for all ten at pixel size 12 (7.187500), 13 (7.796875),
    // 14 (8.390625), the line height 15.578 (9.343750) and at double each of those as
    // the density-2 raster the atlas uses (14.390625, 15.593750, 16.796875, 18.687500);
    // QFontMetricsF::horizontalAdvance agrees (7.187500 for each digit at 12px), and
    // boundingRect("0123456789") is 72px wide - 10 x 7.1875 = 71.875, rounded up by
    // the integer QRect.] If a future face ever needed tabular
    // figures, the request cannot go here: this loader rasterizes a QRawFont, which has
    // no per-feature API, so it would have to be QFont::setFeature(QFont::Tag("tnum"), 1)
    // on the QFont the source's face is resolved from (src->FontSrcInit()).
    //
    // The number this function returns is NOT what the atlas ends up carrying: every
    // source is added with ImFontConfig::PixelSnapH (installQtFontLoader), so ImGui
    // rounds the advance to a whole pixel (7.1875 -> 7 at 12px) in both the render and
    // the metrics path, and that rounded value is what CalcTextSize() and RenderText()
    // use. The reason is in installQtFontLoader(): a fractional advance is what puts
    // every glyph after the first on a half pixel, i.e. what blurs the panel's text
    // beside the timeline's.

    QPointF advance;
    if (!bakedData->rawFont.advancesForGlyphIndexes(&glyphIndex, &advance, 1))
        return false;

    // Metrics-only mode. ImGui uses it for layout (ImFontBaked_BuildLoadGlyphAdvanceX:
    // bakes >= 128px, or LoadNoRenderOnLayout) and rasterizes later, so nothing is
    // packed or drawn here.
    if (aOutAdvanceX != nullptr)
    {
        IM_ASSERT(aOutGlyph == nullptr);
        *aOutAdvanceX = (float)advance.x() * reciprocalDensity;
        return true;
    }

    // PixelAntialiasing, not SubPixelAntialiasing: an atlas is sampled at whole pixels,
    // and subpixel coverage would need an LCD filter and a per-pixel position. Measured:
    // the result is a Format_Alpha8 image whose size is the glyph's ink box.
    const QImage alphaMap = bakedData->rawFont.alphaMapForGlyph(glyphIndex, QRawFont::PixelAntialiasing, QTransform());

    // A failed rasterization and an inkless glyph (a space) both arrive as a 0x0 image, and
    // Qt offers no error code to tell them apart, so both are reported as "found, nothing to
    // draw" with the metrics the face gives. This is the case ImGui prepares for in
    // ImFontAtlasBuildSetupFontBakedBlanks() - returning false instead would make ImGui answer
    // for every space with the *fallback character's* advance. MEASURED before this rule was
    // in place: Geist's space is glyph 737 with an advance of 5.000 px at 20px, and reporting
    // it as missing produced ImGui glyph metrics of "found = false, advance = 0.000 px".
    const int bitmapWidth = alphaMap.width();
    const int bitmapHeight = alphaMap.height();

    aOutGlyph->Codepoint = aCodepoint;
    aOutGlyph->AdvanceX = (float)advance.x() * reciprocalDensity;

    if (bitmapWidth <= 0 || bitmapHeight <= 0)
        return true; // no ink: a valid glyph with nothing to pack

    // MEASURED Qt API detail: boundingRect() is expressed relative to the pen origin in
    // device pixels, y pointing down, and equals FreeType's (bitmap_left, -bitmap_top,
    // width, height). For Geist 'A' at 32px: boundingRect (0, -22, 21, 22) for a 21x22
    // alphaMap whose only inked pixels are inside that box.
    const QRectF box = bakedData->rawFont.boundingRect(glyphIndex);

    ImFontAtlasRectId packId = ImFontAtlasPackAddRect(aAtlas, bitmapWidth, bitmapHeight);
    if (packId == ImFontAtlasRectId_Invalid)
    {
        // Pathological out of memory case (TexMaxWidth/TexMaxHeight set too small?)
        IM_ASSERT(packId != ImFontAtlasRectId_Invalid && "Out of texture memory.");
        return false;
    }
    ImTextureRect* rect = ImFontAtlasPackGetRect(aAtlas, packId);

    const float referenceSize = aBaked->OwnerFont->Sources[0]->SizePixels;
    const float offsetsScale = (referenceSize != 0.0f) ? (aBaked->Size / referenceSize) : 1.0f;
    const float fontOffsetX = ImFloor(aSrc->GlyphOffset.x * offsetsScale + 0.5f); // Snap scaled offset.
    const float fontOffsetY = ImFloor(aSrc->GlyphOffset.y * offsetsScale + 0.5f) + aBaked->Ascent;

    // Bearings divided back by the rasterizer density, exactly as the FreeType loader
    // does, so the glyph occupies its layout box at the requested size while the atlas
    // holds a bitmap rasterized at density x size.
    const float glyphOffsetX = (float)box.x() * reciprocalDensity;
    const float glyphOffsetY = (float)box.y() * reciprocalDensity;
    aOutGlyph->X0 = glyphOffsetX + fontOffsetX;
    aOutGlyph->Y0 = glyphOffsetY + fontOffsetY;
    aOutGlyph->X1 = glyphOffsetX + (float)bitmapWidth * reciprocalDensity + fontOffsetX;
    aOutGlyph->Y1 = glyphOffsetY + (float)bitmapHeight * reciprocalDensity + fontOffsetY;
    aOutGlyph->Visible = true;
    aOutGlyph->Colored = false; // alphaMapForGlyph() is coverage only; no colour bitmap
    aOutGlyph->PackId = packId;

    // Format_Alpha8 with the image's own stride: no temporary buffer, and
    // ImFontAtlasBakedSetFontGlyphBitmap() expands Alpha8 into the atlas format
    // (RGBA32 by default) itself - the same entry point and the same conversion path
    // the stb loader uses.
    ImFontAtlasBakedSetFontGlyphBitmap(aAtlas, aBaked, aSrc, aOutGlyph, rect,
        alphaMap.constBits(), ImTextureFormat_Alpha8, alphaMap.bytesPerLine());
    return true;
}

const ImFontLoader* qtFontLoader()
{
    static ImFontLoader loader;
    loader.Name = "Qt/QRawFont";
    loader.LoaderInit = qtLoaderInit;
    loader.LoaderShutdown = qtLoaderShutdown;
    loader.FontSrcInit = qtFontSrcInit;
    loader.FontSrcDestroy = qtFontSrcDestroy;
    loader.FontSrcContainsGlyph = qtFontSrcContainsGlyph;
    loader.FontBakedInit = qtFontBakedInit;
    loader.FontBakedDestroy = qtFontBakedDestroy;
    loader.FontBakedLoadGlyph = qtFontBakedLoadGlyph;
    loader.FontBakedSrcLoaderDataSize = sizeof(FontSrcBakedData);
    return &loader;
}

// Hand an atlas back the state installQtFontLoader() found it in when the atlas install
// itself refused the primary source.
//
// WHY Clear() AND NOT JUST SetFontLoader(): the refused AddFontFromMemoryTTF() ran
// ImFontAtlasBuildInit(), so the atlas is no longer the untouched thing it was - it has a
// builder and a texture behind it, and its loader (ours) has been LoaderInit()'d. ImGui only
// selects a default loader while atlas->Builder == NULL (ImFontAtlasBuildInit), so resetting
// the loader pointer alone would leave the next AddFont() calling through a NULL
// atlas->FontLoader. Clear() runs the installed loader's LoaderShutdown() - freeing this
// loader's AtlasData - and drops the builder, the texture and the (already rolled back)
// sources; SetFontLoader() then puts the loader pointer back, which for a NULL argument is
// the same "nothing chosen yet" state a fresh atlas is in, and ImGui re-selects its own
// default on the next AddFont()/Build().
//
// NOT RESTORED, because the public API cannot: the per-atlas data of a loader that was
// installed before the call (SetFontLoader() destroys it on the way in - that is ImGui's
// contract for every loader change - and its LoaderInit() re-creates it on the next build),
// and the builder/texture that the failed attempt created (so TexIsBuilt is left false, and
// FontLoaderName reports ImGui's own literal "NULL" when the atlas had no previous loader).
void restoreAtlasAfterRefusedInstall(ImFontAtlas& aAtlas, const ImFontLoader* aPreviousLoader,
                                     bool aPreviousRendererHasTextures)
{
    aAtlas.Clear();
    aAtlas.RendererHasTextures = aPreviousRendererHasTextures;
    aAtlas.SetFontLoader(aPreviousLoader);
}

} // namespace

const char* loaderName()
{
    return qtFontLoader()->Name;
}

float panelFontPixelSize(const QFont& aFont, qreal aLogicalDpi)
{
    // An explicit pixel size is already the rasterization size, so it is taken
    // verbatim - Qt's font engine does the same (a 12/13/17/20px font resolves to
    // 12/13/17/20px [measured]).
    if (aFont.pixelSize() > 0)
        return float(aFont.pixelSize());

    // A point-sized font: points are 1/72 inch, so the pixel size is the point size
    // scaled by the DPI over 72, rounded to the nearest whole pixel - the rounding Qt
    // itself applies [measured: see the table in the header, where qRound agrees with
    // QRawFont::fromFont().pixelSize() for all seven probed point sizes].
    const qreal pointSize = aFont.pointSizeF();
    if (pointSize > 0.0 && aLogicalDpi > 0.0)
        return float(qRound(pointSize * aLogicalDpi / 72.0));

    // Neither a pixel nor a usable point size: no answer, and the caller owns its
    // fallback (see the header).
    return 0.0f;
}

int installQtFontLoader(ImFontAtlas& aAtlas, const QFont& aAppFont, float aPixelSize, int aDpr)
{
    // The three refusals below happen before the atlas is touched at all. That is what lets
    // an atlas which had nothing to do with this loader come out of a 0 return untouched.

    // Refuse a size we cannot rasterize rather than inventing one: the caller owns the
    // layout, and a 0 return leaves the atlas untouched.
    if (!(aPixelSize > 0.0f))
        return 0;

    // Installing into a non-empty atlas would leave the fonts already added holding the
    // previous loader's per-source data while the atlas announces ours.
    if (aAtlas.Fonts.Size > 0 || aAtlas.Sources.Size > 0)
        return 0;

    const QByteArray fontBytes = readPrimaryFontData();
    // AddFontFromMemoryTTF() asserts font_data_size > 100 (a heuristic against a wrong
    // argument), so anything shorter is not the font and must not be handed over.
    if (fontBytes.size() <= 100)
        return 0;

    // The primary face's family, read from the bytes that are about to become source 0:
    // the "same family as the application font" skip rule has to compare against the
    // face that will really be installed, not against whatever aAppFont resolves to.
    //
    // This load is deliberately NOT a gate. FontSrcInit() makes the same Qt call with the
    // same bytes, so a face rejected here is rejected there too - and making it a gate would
    // leave the refused-install path below unreachable and therefore untestable. The atlas
    // install is the one authority on whether the source can be installed, and its refusal
    // is undone.
    QRawFont primaryFace;
    primaryFace.loadFromData(fontBytes, aPixelSize, QFont::PreferDefaultHinting);
    const QString primaryFamily = primaryFace.familyName();

    const float density = (aDpr > 0) ? (float)aDpr : 1.0f;

    // What the atlas looked like before this call, so a refused install can hand it back:
    // ImGui has no "uninstall" call of its own (see restoreAtlasAfterRefusedInstall).
    const ImFontLoader* previousLoader = aAtlas.FontLoader;
    const bool previousRendererHasTextures = aAtlas.RendererHasTextures;

    // Only now touch the atlas.
    aAtlas.SetFontLoader(qtFontLoader());

    // The dynamic path is the one this loader is written for: FontBakedLoadGlyph() loads
    // one glyph on demand and hands ImGui a queued texture update. Left false, ImGui
    // preloads every source's glyph ranges at Build() instead - the behaviour this loader
    // exists to avoid. An atlas owned by a context gets this field copied from
    // io.BackendFlags a moment later (ImFontAtlasBuildUpdateRendererHasTexturesFromContext),
    // so only a context-less atlas is set here - which is also the only case where nothing
    // else would ever set it.
    if (aAtlas.OwnerContext == NULL)
        aAtlas.RendererHasTextures = true;

    // The bytes go to ImGui with ownership transferred (FontDataOwnedByAtlas defaults to
    // true): the pointer must stay valid for the atlas's whole life and ImGui releases it
    // with IM_FREE, so it is allocated with IM_ALLOC rather than handing ImGui a buffer
    // Qt owns.
    void* ownedBytes = IM_ALLOC((size_t)fontBytes.size());
    memcpy(ownedBytes, fontBytes.constData(), (size_t)fontBytes.size());

    ImFontConfig primaryCfg;
    setSourceName(primaryCfg.Name, primaryFamily);
    primaryCfg.FontLoaderFlags = makeSourceLoaderFlags(kSourceKind_FileData, 0);
    primaryCfg.RasterizerDensity = density;
    // PIXEL-SNAPPED ADVANCES, or every glyph after the first is bilinearly blurred.
    // ImGui truncates a run's origin ONCE (ImFont::RenderText's "align to be pixel
    // perfect" IM_TRUNC) and then accumulates each glyph's AdvanceX in floats, so a
    // fractional advance puts glyph 2..n on fractional columns - and the atlas is
    // sampled with GL_LINEAR (imgui_impl_opengl3.cpp's texture parameters), which
    // blends two texels at a fractional position. MEASURED on the panel's own face at
    // its own size (Geist, 12px): every digit's advance is 7.1875, so "0123456789"
    // drawn at x=8 placed its glyphs at 8, 16.1875, 22.375, 29.5625, ... and only the
    // first landed on a whole pixel. In the rendered frame the first digit came out
    // BYTE-IDENTICAL to Qt's own rasterization of the same glyph (0 differing pixels
    // over its 8x20 cell, at a one-row alignment offset), while the rest carried 0-7 strong pixels of 34-57 inked
    // - the smeared text the timeline's ruler numbers never show, because Qt snaps
    // each glyph's pen to a whole pixel. The whole string: 6.4% of its inked pixels at
    // full coverage against Qt's 12.7%, and 34 ambiguous columns against 23.
    //
    // ImGui applies this flag itself, in ImFontAtlasBakedAddFontGlyph() (the render
    // path) and ImFontAtlasBakedAddFontGlyphAdvancedX() (the metrics-only path), so
    // the measured width and the drawn width agree. Its documented caveat ("prevents
    // fractional font size from working correctly") does not apply here: the atlas is
    // baked at the integer pixel size panelFontPixelSize() derives and drawn at it.
    primaryCfg.PixelSnapH = true;
    // GetGlyphRangesDefault() states the primary source's coverage (Basic Latin + Latin
    // Supplement). With the dynamic atlas above it is not used for loading at all; it is
    // the correct answer for the legacy path, which uses it to decide what to preload.
    if (aAtlas.AddFontFromMemoryTTF(ownedBytes, (int)fontBytes.size(), aPixelSize, &primaryCfg,
                                    aAtlas.GetGlyphRangesDefault()) == NULL)
    {
        // The primary source is what every other source merges into; nothing was added, so
        // hand the atlas back what it had. ImGui rolled its own half of the attempt back
        // (ImFontAtlasFontDestroySourceData() frees ownedBytes), so only ours is left.
        restoreAtlasAfterRefusedInstall(aAtlas, previousLoader, previousRendererHasTextures);
        return 0;
    }

    // AddFont() ran LoaderInit(), so the per-atlas state now exists and can carry what the
    // merged sources' FontSrcInit() needs.
    AtlasData* data = atlasDataOf(&aAtlas);
    if (data == nullptr)
    {
        // ImGui accepted the source but the per-atlas state it should have created is not
        // there, so no glyph could be served: hand the atlas back rather than leave a loader
        // installed that would fail on the first glyph.
        restoreAtlasAfterRefusedInstall(aAtlas, previousLoader, previousRendererHasTextures);
        return 0;
    }
    data->appFont = aAppFont;
    data->appFontValid = true;

    // Resolved only now, that source 0 is really installed: a refused install returned above,
    // and these four QRawFont::fromFont() calls are not free.
    const QList<FallbackSource> fallbacks = resolveFallbackSources(aAppFont, primaryFamily);

    // The fallback sources. No GlyphRanges: a CJK face cannot be preloaded wholesale
    // (21k ideographs) and, on the dynamic path, declaring ranges changes nothing -
    // routing at load time is source order plus FontSrcContainsGlyph(). A NULL range makes
    // the *legacy* path fall back to GetGlyphRangesDefault() for these sources too, which
    // costs nothing either: source 0 answers for every ASCII codepoint first.
    int sourceCount = 1;
    for (const FallbackSource& fallback : fallbacks)
    {
        ImFontConfig cfg;
        cfg.MergeMode = true;
        cfg.FontLoaderFlags = makeSourceLoaderFlags(kSourceKind_WritingSystem, (unsigned int)fallback.writingSystem);
        cfg.RasterizerDensity = density;
        cfg.SizePixels = aPixelSize; // keeps the merge scale factor at exactly 1
        // Same reason as the primary source above: a merged face's advance is just as
        // fractional, and one unsnapped source in the chain is enough to put every
        // glyph after it on a half pixel.
        cfg.PixelSnapH = true;
        cfg.FontData = &gUnusedSourceByte;
        cfg.FontDataSize = 1;
        cfg.FontDataOwnedByAtlas = false;
        setSourceName(cfg.Name, fallback.family);
        if (aAtlas.AddFont(&cfg) != NULL)
            ++sourceCount;
    }
    return sourceCount;
}

} // namespace imgui_qt
} // namespace gui
