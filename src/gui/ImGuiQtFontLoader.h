#ifndef GUI_IMGUIQTFONTLOADER_H
#define GUI_IMGUIQTFONTLOADER_H

#include <QtGlobal> // qreal

class QFont;

struct ImFontAtlas;

namespace gui {
namespace imgui_qt {

// A Dear ImGui 1.92 ImFontLoader that rasterizes glyphs through Qt's QRawFont.
//
// WHY QT IS THE RASTERIZER. ImGui 1.92's built-in loader is stb_truetype, and stb
// cannot open the face Qt uses for CJK here: loading the system's default CJK face
// (NotoSansCJK-VF.ttc) makes stbtt_FindGlyphIndex abort
// (build/_deps/imgui-src/imstb_truetype.h:1590). [measured] Routing rasterization
// through QRawFont means ImGui draws with exactly the face the rest of the
// application draws with (Qt font database -> FreeType), and no ImGui file is
// touched.
//
// WHAT IS INSTALLED. One source for the application font, loaded from the bundled
// Geist bytes, plus one merged source per CJK writing system this application
// needs (Japanese, Korean, SimplifiedChinese, TraditionalChinese). Each CJK face is
// resolved by Qt itself - QRawFont::fromFont(aAppFont, writingSystem), i.e. "the face
// Qt would use for this script" - with no font file path and no fontconfig call of
// our own. A writing system is skipped when Qt hands back the application font's own
// family, when the resolved face does not support the writing system's sample
// character (QFontDatabase::writingSystemSample), or when its face duplicates one
// already installed.
//
// ROUTING is source order: ImGui asks each source in turn for a codepoint and stops
// at the first that answers (ImFontBaked_BuildLoadGlyph), so the application font is
// source 0 and the CJK faces are tried in the order above.
//
// INSTALL ORDER. Call this BEFORE adding any font to the atlas: SetFontLoader() is
// applied to an empty atlas here, and ImGui asserts that FontLoaderData is still
// NULL at that moment.
//
// Returns the number of font sources added, or 0 when nothing could be installed. A 0 return
// adds no source and leaves no loader of ours behind. The first three refusals - a non-positive
// aPixelSize, an atlas that already holds fonts or sources, fewer than 101 bytes of Geist -
// happen before the atlas is touched at all. Bytes Qt cannot open as a face are refused by the
// atlas install itself (the loader's FontSrcInit() runs on exactly those bytes), and that 0
// return hands the atlas back what it had: the half-built builder and texture the attempt
// created are discarded, RendererHasTextures is put back, and ImFontAtlas::FontLoader is set
// to the pointer it held before the call - NULL for an atlas that had none, which is the state
// in which ImGui makes its own loader choice (stb_truetype or FreeType) on the next
// AddFont()/Build(). What a 0 return cannot put back is the per-atlas data of a loader that was
// installed before the call: SetFontLoader() destroys it when ours is installed (ImGui's
// contract for every loader change), so such a loader re-creates it from its own LoaderInit()
// on the next build; and, on the refused-install path, the builder/texture state that attempt
// replaced, so FontLoaderName reports ImGui's literal "NULL" afterwards when the atlas had no
// previous loader. Nothing here leaves FontLoader NULL while Builder is non-NULL, which is the
// combination under which a later AddFont() would call through a NULL loader.
//
// MEASURED / INFERRED. The Qt call semantics this loader depends on (glyph index 0
// means "no glyph", boundingRect() origin is the bitmap origin, alphaMapForGlyph()
// returns Format_Alpha8 and a 0x0 image for whitespace) were measured with throwaway
// programs before this file was written; see tests/imgui_qt_font_loader.cpp for the
// measurements this loader is verified by.
//
// aDpr IS APPLIED AS ImFontConfig::RasterizerDensity, and that field is multiplied by the
// bake's own density inside ImGui (which AddFont initialises from the same field), exactly
// as in the stb and FreeType loaders. MEASURED consequence: 'A' at 20px is a 13x14 bitmap
// with aDpr=1 and 51x57 with aDpr=2 (20px x 2 x 2 = 80px of raster), with an identical
// advance. So for the dynamic texture path pass aDpr=1 and scale the requested size
// instead: ImGui then drives the density itself from io.DisplayFramebufferScale
// (ImGui::NewFrame(), imgui.cpp:4605). This loader honours whatever density a bake
// carries, so glyphs re-rasterize whenever it changes.
//
// NOT covered by that verification: fractional DPI scales, colour/emoji glyphs,
// supplementary-plane codepoints, bidi, and any glyph cache eviction policy (there is
// none here - see the .cpp).
int installQtFontLoader(ImFontAtlas& aAtlas, const QFont& aAppFont, float aPixelSize, int aDpr);

// The loader's Name, which is what ImFontAtlas::FontLoaderName reports once
// installed. Never null.
const char* loaderName();

// The pixel size to hand installQtFontLoader() so the overlay's text is drawn at the
// size the APPLICATION itself draws that font at - the timeline's ruler numbers are
// the reference. Call it beside the install:
//
//     const float pixelSize = imgui_qt::panelFontPixelSize(aWidget.font(), aWidget.logicalDpiY());
//     imgui_qt::installQtFontLoader(*io.Fonts, aWidget.font(), pixelSize, 1);
//
// WHY IT IS NOT QFontMetricsF::height(), which is what the panel asked for before.
// Qt's layout unit is the font's LINE height - QFontMetricsF::height() is
// ascent + descent + leading - and the application font (Geist, 9pt) resolves to a
// 15.578125 px line height over a face Qt RASTERIZES at 12 px, i.e. the line height is
// 1.298x the glyph size. Asking the atlas for the line height therefore drew every
// panel numeral ~30% larger than the timeline draws the same font, which is the
// "bigger than the timeline's numbers" complaint. [measured at 96 dpi: ascent
// 12.046875 + descent 3.53125 = 15.578125 == QFontMetricsF::height(), leading 0, and
// QRawFont::fromFont(QApplication::font()).pixelSize() == 12]
//
// THE EXPRESSION, measured against Qt's own answer. Qt resolves a point-sized font
// against the screen's logical DPI and rounds to the nearest whole pixel, and takes an
// explicitly pixel-sized font verbatim:
//
//     pixels = aFont.pixelSize() > 0 ? aFont.pixelSize()
//                                    : qRound(aFont.pointSizeF() * aLogicalDpi / 72.0)
//
// [measured at 96 dpi, comparing QRawFont::fromFont(font).pixelSize() - the size the
// face is really rasterized at - with the expressions above:
//   pointSizeF 8    -> 11  (product 10.667; qFloor says 10, qCeil 11)
//   pointSizeF 9    -> 12  (product 12.0)
//   pointSizeF 9.5  -> 13  (product 12.667; qFloor says 12)
//   pointSizeF 10   -> 13  (product 13.333; qCeil says 14)
//   pointSizeF 11   -> 15  (product 14.667; qFloor says 14)
//   pointSizeF 11.7 -> 16  (product 15.6;   qFloor says 15)
//   pointSizeF 13   -> 17  (product 17.333; qCeil says 18)
//   pixelSize 12/13/17/20 -> 12/13/17/20
// qRound agrees with Qt on all seven point sizes; qFloor and qCeil each disagree on
// three. High-DPI scaling does NOT change the answer: at devicePixelRatio 1.5 Qt keeps
// logicalDotsPerInch at 96 and the logical pixel size at 12 [measured], which is what
// the loader's RasterizerDensity/io.DisplayFramebufferScale path expects.]
//
// WHAT IT IS NOT. ctrl::TimeLineEditor::kNumberHeight (14) is the height of the BOX the
// ruler draws a number into, not a font size: the glyphs in it come from the widget
// font at its own resolved 12 px [src/gui/TimeRuler.cpp sets no font, so the painter
// uses the widget's]. Matching the timeline means matching Qt's resolved pixel size,
// not 14 - and 14 is also not a size the application font was ever rasterized at.
//
// aLogicalDpi is the DPI the caller's paint device reports (QWidget::logicalDpiY()).
// Returns 0 when the font states neither a pixel nor a point size, or the DPI is not
// positive: the caller owns its fallback, because only it knows at which size drawing
// is still legible. installQtFontLoader() refuses a non-positive size, so a 0 here
// adds no source and leaves the atlas untouched.
float panelFontPixelSize(const QFont& aFont, qreal aLogicalDpi);

} // namespace imgui_qt
} // namespace gui

#endif // GUI_IMGUIQTFONTLOADER_H
