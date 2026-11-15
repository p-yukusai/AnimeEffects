#include "gui/InkImGui.h"

#include "gui/GraphPanelFrame.h"

#include <QVarLengthArray>

#include <cfloat>
#include <cmath>

#include "imgui.h"
// ImDrawListSharedData (the draw list's font) and ImMin/ImMax live here.
#include "imgui_internal.h"

namespace gui {
namespace ink {

namespace {

// ImGui packs its colours A<<24|B<<16|G<<8|R; this is the ONE place the conversion happens,
// so no caller ever holds a packed value next to a QColor.
ImU32 packed(const QColor& aColor) {
    return IM_COL32(aColor.red(), aColor.green(), aColor.blue(), aColor.alpha());
}

// THE FAR EDGE, spelled once for both rect types. ImGui's rects are half-open, so the edge is
// the pixel AFTER the last included one. A QRectF(x, y, w, h) names the SAME pixels as
// QRect(x, y, w, h) - the contract gui/Ink.h states - and QRectF::right() is already x + w
// (unlike QRect::right(), which is x + w - 1), so the two spellings differ by exactly that one.
// MEASURED: adding +1 to the QRectF's right() widened every converted rect by a column - the
// play window's pills read 533 fill pixels against 522 before the fix.
ImVec2 minOf(const QRectF& aRect) { return ImVec2(float(aRect.left()), float(aRect.top())); }
ImVec2 maxOf(const QRectF& aRect) { return ImVec2(float(aRect.right()), float(aRect.bottom())); }
ImVec2 minOf(const QRect& aRect) { return ImVec2(float(aRect.left()), float(aRect.top())); }
ImVec2 maxOf(const QRect& aRect) { return ImVec2(float(aRect.right() + 1), float(aRect.bottom() + 1)); }

// The draw list's own font and size. The ink must NOT go through ImGui::CalcTextSize(): that
// one reads the current context (g.Font), and the drawing has to be callable from a headless
// gate that owns nothing but a draw list. ImDrawList keeps the frame's font in its shared data,
// so this is the same font the glyphs are drawn with.
ImFont* listFont(ImDrawList& aList) { return aList._Data ? aList._Data->Font : nullptr; }
float listFontSize(ImDrawList& aList) { return aList._Data ? aList._Data->FontSize : 0.0f; }

ImVec2 measureText(ImFont* aFont, float aFontSize, const char* aText) {
    if (!aFont || !aText || !*aText)
        return ImVec2(0.0f, 0.0f);
    return aFont->CalcTextSizeA(aFontSize, FLT_MAX, 0.0f, aText);
}

// How far below the position ImGui is HANDED the first row of a run's ink falls: the smallest
// top bearing among the run's glyphs, at the size it is drawn at. ImFont::RenderText places
// each glyph at `trunc(pos.y) + glyph->Y0 * scale`, so the run's ink begins on that row - the
// LINE BOX top plus this inset. 0 when the font cannot answer (no baked size, no glyph), which
// degrades to the line box's own top.
float inkTopInset(ImFont* aFont, float aFontSize, const char* aText) {
    if (!aFont || !aText || !*aText || !(aFontSize > 0.0f))
        return 0.0f;
    ImFontBaked* baked = aFont->GetFontBaked(aFontSize);
    if (!baked || !(baked->Size > 0.0f))
        return 0.0f;
    const float scale = aFontSize / baked->Size;
    float inset = FLT_MAX;
    // The panel's labels are numbers: the ASCII bytes are the whole run (the run aligns by its
    // HIGHEST glyph, so a '-' cannot drag the row down). A multi-byte character - the en dash
    // of a merged window pill - is skipped rather than read as the first of its bytes, which
    // would be a different glyph with a different bearing.
    for (const char* c = aText; *c; ++c) {
        const unsigned char byte = (unsigned char)*c;
        if (byte < 0x20 || byte > 0x7e)
            continue;
        if (const ImFontGlyph* glyph = baked->FindGlyphNoFallback(ImWchar(byte)))
            inset = ImMin(inset, glyph->Y0 * scale);
    }
    return inset == FLT_MAX ? 0.0f : inset;
}

} // namespace

InkImGui::InkImGui(ImDrawList& aList, StrokeSink aStroke) : mList(aList), mStroke(aStroke) {}

void InkImGui::setLineAntialias(bool aEnabled) {
    if (aEnabled)
        mList.Flags |= ImDrawListFlags_AntiAliasedLines;
    else
        mList.Flags &= ~ImDrawListFlags_AntiAliasedLines;
}

void InkImGui::pushClip(const QRect& aRect) {
    mList.PushClipRect(minOf(aRect), maxOf(aRect), true);
}

void InkImGui::popClip() { mList.PopClipRect(); }

void InkImGui::fillRect(const QRectF& aRect, const QColor& aColor, double aRadius) {
    mList.AddRectFilled(minOf(aRect), maxOf(aRect), packed(aColor), float(aRadius));
}

void InkImGui::strokeRect(const QRectF& aRect, const QColor& aColor, double aRadius, double aWidth) {
    mList.AddRect(minOf(aRect), maxOf(aRect), packed(aColor), float(aRadius), ImDrawFlags_None, float(aWidth));
}

void InkImGui::line(const QPointF& aFrom, const QPointF& aTo, const QColor& aColor, double aWidth) {
    mList.AddLine(ImVec2(float(aFrom.x()), float(aFrom.y())), ImVec2(float(aTo.x()), float(aTo.y())),
                  packed(aColor), float(aWidth));
}

void InkImGui::stroke(const QVector<QPointF>& aPoints, const QColor& aColor, double aHalfWidth) {
    if (aPoints.size() < 2)
        return;
    if (mStroke && mStroke(mList, aPoints, aColor, aHalfWidth))
        return;
    // The fallback is exactly what the panel shipped before the coverage pass: ImGui's own
    // antialiased polyline at the full width.
    QVarLengthArray<ImVec2, 256> points(int(aPoints.size()));
    for (int i = 0; i < aPoints.size(); ++i)
        points[i] = ImVec2(float(aPoints.at(i).x()), float(aPoints.at(i).y()));
    mList.AddPolyline(
        points.constData(), int(points.size()), packed(aColor), ImDrawFlags_None, float(aHalfWidth * 2.0)
    );
}

void InkImGui::circleFill(const QPointF& aCentre, double aRadius, const QColor& aColor) {
    mList.AddCircleFilled(ImVec2(float(aCentre.x()), float(aCentre.y())), float(aRadius), packed(aColor));
}

void InkImGui::circleStroke(const QPointF& aCentre, double aRadius, const QColor& aColor, double aWidth) {
    mList.AddCircle(ImVec2(float(aCentre.x()), float(aCentre.y())), float(aRadius), packed(aColor), 0, float(aWidth));
}

void InkImGui::text(
    const QRect& aBox, const QString& aText, HAlign aAlign, VAlign aVAlign, const QColor& aColor,
    const QColor& aHalo, int aRightGap
) {
    ImFont* font = listFont(mList);
    if (!font || aText.isEmpty() || aBox.isEmpty())
        return;
    const float fontSize = listFontSize(mList);
    const QByteArray utf8 = aText.toUtf8();
    const ImVec2 size = measureText(font, fontSize, utf8.constData());

    float x = float(aBox.left());
    if (aAlign == HAlign::Centre) {
        x += (float(aBox.width()) - size.x) * 0.5f;
    } else if (aAlign == HAlign::Right) {
        // THE RIGHT GAP IS TAKEN FROM THE BOX'S OWN SPARE WIDTH (gui::kValueLabelGap): a label
        // that fills its box is left exactly where right-alignment puts it, so the gap can
        // never push a number out of the column the model measured for it. What is left over -
        // a label wider than its box, which the measured column cannot produce but a wider
        // drawing font can - is the caller's clip to hold back.
        const float spare = ImMax(0.0f, float(aBox.width()) - size.x);
        x = float(aBox.left() + aBox.width()) - size.x - ImMin(float(aRightGap), spare);
    }
    // THE ORIGIN IS A WHOLE DEVICE PIXEL. Centring and right-alignment are float arithmetic on
    // a measured advance, so they land on a fraction; ImGui truncates the origin before it
    // rasterises, but depending on two float expressions agreeing with that truncation is what
    // makes a glyph's CELL and its INK disagree by a column. Snap once, here.
    const int snappedX = int(std::floor(x));
    // THE ROW IS A WHOLE PIXEL for the same reason: top alignment subtracts the font's ink
    // bearing and centring halves a line-height difference, and both can be fractional.
    const float rawY = aVAlign == VAlign::Top
        ? float(aBox.top() + kRulerNumberInkTop) - inkTopInset(font, fontSize, utf8.constData())
        : float(aBox.top()) + ImMax((float(aBox.height()) - size.y) * 0.5f, 0.0f);
    const int snappedY = int(std::floor(rawY));

    const ImU32 inkColor = packed(aColor);
    const ImVec2 at = ImVec2(float(snappedX), float(snappedY));
    // THE 16 HALO PASSES, then the run itself. The halo is what kept a label readable over
    // arbitrary artwork (the panel has no background). SHIPPED OFF (gui::kDrawTextHalo): the
    // panel's contrast-outline pass rings the ink that would otherwise be lost, so the flag is
    // the only difference between the two.
    if (kDrawTextHalo && aHalo.isValid()) {
        static const int kOffsets[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
        QColor haloInk = aHalo;
        haloInk.setAlphaF(qBound(0.0f, haloInk.alphaF() * float(kHaloAlpha) / 255.0f, 1.0f));
        const ImU32 haloColor = packed(haloInk);
        for (int radius = kHaloRadius; radius >= 1; --radius)
            for (const int* offset : kOffsets)
                mList.AddText(font, fontSize,
                              ImVec2(at.x + float(offset[0] * radius), at.y + float(offset[1] * radius)), haloColor,
                              utf8.constData());
    }
    mList.AddText(font, fontSize, at, inkColor, utf8.constData());
}

} // namespace ink
} // namespace gui
