#ifndef GUI_INKIMGUI_H
#define GUI_INKIMGUI_H

#include "gui/Ink.h"

struct ImDrawList;

namespace gui {
namespace ink {

// THE SHIPPED BACKEND: the graph panel's ink over ImGui's draw list. It is the reference the
// other two are measured against, because it is what the panel has always looked like.
//
// The text rules are not re-invented here: `drawLabel`'s alignment, its whole-pixel snapping,
// its ink-top rule for the ruler's row and its halo live in this class now, exactly as they
// lived in GraphPanelFrame.cpp's anonymous namespace (the code was moved, not rewritten).
class InkImGui : public Ink {
public:
    // The coverage stroke pass is a property of THIS backend: it exists because ImGui's own
    // antialiased polyline is not area-exact, and it appends ImDrawList callbacks that read the
    // backend's framebuffer. It therefore stays with the GL code in GraphPanelFrame.cpp, and
    // the backend asks for it through this hook. A hook that is absent, or that answers false
    // (the pass was never initialised, or its objects were refused), falls back to ImGui's own
    // polyline at the same width - a panel drawn with the fringe stroke beats a panel with no
    // curves; the other two backends have no such dependency and always stroke.
    using StrokeSink = bool (*)(ImDrawList& aList, const QVector<QPointF>& aPoints, const QColor& aColor,
                                double aHalfWidth);

    explicit InkImGui(ImDrawList& aList, StrokeSink aStroke = nullptr);

    void setLineAntialias(bool aEnabled) override;
    void pushClip(const QRect& aRect) override;
    void popClip() override;
    void fillRect(const QRectF& aRect, const QColor& aColor, double aRadius = 0.0) override;
    void strokeRect(const QRectF& aRect, const QColor& aColor, double aRadius, double aWidth) override;
    void line(const QPointF& aFrom, const QPointF& aTo, const QColor& aColor, double aWidth) override;
    void stroke(const QVector<QPointF>& aPoints, const QColor& aColor, double aHalfWidth) override;
    void circleFill(const QPointF& aCentre, double aRadius, const QColor& aColor) override;
    void circleStroke(const QPointF& aCentre, double aRadius, const QColor& aColor, double aWidth) override;
    void text(
        const QRect& aBox, const QString& aText, HAlign aAlign, VAlign aVAlign, const QColor& aColor,
        const QColor& aHalo = QColor(), int aRightGap = 0
    ) override;

    // The draw list this backend draws into (the frame driver owns it).
    ImDrawList& list() const { return mList; }

private:
    ImDrawList& mList;
    StrokeSink mStroke;
};

} // namespace ink
} // namespace gui

#endif // GUI_INKIMGUI_H
