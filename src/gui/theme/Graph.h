#ifndef GUI_THEME_GRAPH_H
#define GUI_THEME_GRAPH_H

#include <QColor>
#include "core/TimeKeyType.h"
#include "gui/theme/Colors.h"

namespace theme {

// Appearance tokens for the animation graph overlay (gui::drawPanel, in
// src/gui/ImGuiOverlay.cpp).
//
// The panel is drawn by the gui layer from gui::GraphPanelModel, so - exactly
// like theme::TimeLine for ctrl::time::Renderer - the palette decisions live here
// and the drawing layer only reads them. Every token is a curated cell of
// theme::Colors, so an accent or light/dark change is one rebuild (Graph::from)
// and never a per-widget color.
//
// Curves are colored by KEY TYPE through keyColor(), i.e. the same hues the
// timeline draws its keys in: a curve on the graph and its keys on the timeline
// read as the same feature. A KEY MARKER on the panel carries that hue too (its
// fill), so the curve, its key squares and its legend swatch are one colour, and
// the only thing the palette names for every key besides it is the edge that keeps
// the fill legible over arbitrary artwork.
class Graph {
public:
    Graph();

    // Curated tokens for the active palette.
    static Graph from(const Colors& aColors);

    QColor gridMajor() const { return mGridMajor; }  // the frame ticks (the panel has no grid)
    QColor axisLine() const { return mAxisLine; }    // value-axis base line
    // Value and frame labels. The FULL text token - the ink the timeline draws its own
    // ruler numbers in - not a muted one: these glyphs are the only panel content the
    // halo does not back (it ships off), so they sit on arbitrary artwork and a mid grey
    // is what disappears there. assignFrom() carries the measured WCAG ratios for the
    // choice, against this palette's halo and against mid-grey artwork.
    QColor axisText() const { return mAxisText; }
    // Label halo: the glyphs drawn once behind every label so the text stays
    // readable over arbitrary artwork. The panel has no background of its own
    // (see GraphPanelModel's header), so the halo is the panel tone the text
    // would sit on if it had one, and the text colour is chosen against it.
    QColor halo() const { return mHalo; }
    // Contrast outline: the ring the composite pass (gui::outline, in
    // src/gui/ImGuiOverlay.cpp) draws around panel ink whose own colour is too
    // close to the artwork underneath it. It is BLACK on both palettes, on
    // purpose: the ring exists only where the panel has lost contrast against
    // arbitrary artwork, and a theme's polarity is a statement about the
    // application's own surfaces, not about what happens to be behind the
    // panel. The palette's black is the neutral row's darkest cell, the same
    // cell in the light and the dark table.
    QColor outline() const { return mOutline; }
    QColor curve() const { return mCurve; }          // fallback curve color
    // The key marker's edge: the ring around a key square, and the ring around a
    // legend swatch. The FILL is the channel's own colour (keyColor), which is what
    // makes a key read as the feature it belongs to - see the class comment.
    QColor keyEdge() const { return mKeyEdge; }
    QColor selected() const { return mSelected; }    // selected key / handle
    QColor handleFill() const { return mHandleFill; } // tangent handle fill
    QColor handleEdge() const { return mHandleEdge; } // tangent handle edge
    QColor handleLine() const { return mHandleLine; } // tangent handle stem
    // The marquee: the live rectangle of a box-select drag. The same two decisions
    // the timeline's own box-select makes (ctrl/time/time_Renderer.cpp,
    // renderSelectionRange): the bright accent edge at 230/255 and the same hue as
    // the body at 24/255, so the gesture reads as one feature in both views.
    QColor marqueeFill() const { return mMarqueeFill; }
    QColor marqueeEdge() const { return mMarqueeEdge; }

    // ---- the key context menu ----
    //
    // The panel's one ImGui POPUP (the key context menu, ImGuiOverlay::
    // drawKeyContextMenu). Its appearance is the application's own context menu's:
    // gui::AppStyle draws a QMenu as a rounded floater (radius 8) filled with
    // floaterBody and edged with floaterEdge, highlights a row with accentHover
    // under accentText text, and separates with the hairline - and these tokens are
    // those five decisions plus the disabled text. applyPanelStyle (declared in
    // gui/ImGuiOverlay.h) copies them, and the panel's geometry tokens below, into
    // ImGui's style; that is the ONE place the palette meets ImGuiStyle, and it is
    // re-applied on every frame so a runtime palette or accent change is picked up
    // without any signal (see applyPanelStyle).
    QColor menuBody() const { return mMenuBody; }
    QColor menuEdge() const { return mMenuEdge; }
    QColor menuText() const { return mMenuText; }
    QColor menuTextDisabled() const { return mMenuTextDisabled; }
    QColor menuHover() const { return mMenuHover; }
    QColor menuHoverText() const { return mMenuHoverText; }
    QColor menuSeparator() const { return mMenuSeparator; }

    // The menu's METRICS. They are not palette cells (the palette carries colours),
    // and they do not follow the light/dark polarity: they are the panel's own
    // context for the app's menu geometry, stated here so the popup's layout is not
    // a second copy of them.
    //
    // menuRounding is gui::AppStyle's own QMenu panel radius (8) and
    // menuItemRounding its item pill radius (4). The paddings are the popup's own -
    // it is an ImGui window confined to the viewport and has to stay compact, so its
    // window padding (6/4) and a row's inner padding (6) are the panel's numbers and
    // NOT Qt's font-relative ones (0.3em/0.25em panel, 0.23em/1.3em item, which
    // ImGui's pixel-based style cannot express at all).
    float menuRounding() const { return kMenuRounding; }
    float menuItemRounding() const { return kMenuItemRounding; }
    float menuPadX() const { return kMenuPadX; }
    float menuPadY() const { return kMenuPadY; }
    float menuItemPadX() const { return kMenuItemPadX; }

    // The key-type's own color (the timeline's per-key hues). Falls back to
    // curve() for a type the palette does not name.
    QColor keyColor(core::TimeKeyType aType) const;

private:
    // Fills every token from one palette. A separate step rather than a
    // constructor body, so the default constructor and from() cannot recurse
    // into each other (from() must hand back a filled Graph, and Graph has no
    // other way to obtain storage).
    void assignFrom(const Colors& aColors);

    // The menu's metrics (see the accessors above): the app's QMenu panel and item
    // radii, and this popup's own paddings.
    static constexpr float kMenuRounding = 8.0f;
    static constexpr float kMenuItemRounding = 4.0f;
    static constexpr float kMenuPadX = 6.0f;
    static constexpr float kMenuPadY = 4.0f;
    static constexpr float kMenuItemPadX = 6.0f;

    QColor mGridMajor;
    QColor mAxisLine;
    QColor mAxisText;
    QColor mHalo;
    QColor mOutline;
    QColor mCurve;
    QColor mKeyEdge;
    QColor mSelected;
    QColor mHandleFill;
    QColor mHandleEdge;
    QColor mHandleLine;
    QColor mMarqueeFill;
    QColor mMarqueeEdge;
    QColor mMenuBody;
    QColor mMenuEdge;
    QColor mMenuText;
    QColor mMenuTextDisabled;
    QColor mMenuHover;
    QColor mMenuHoverText;
    QColor mMenuSeparator;
    QColor mKeyColors[core::TimeKeyType_TERM];
};

} // namespace theme

#endif // GUI_THEME_GRAPH_H
