#ifndef GUI_THEME_GRAPH_H
#define GUI_THEME_GRAPH_H

#include <QColor>
#include "core/TimeKeyType.h"
#include "gui/theme/Colors.h"

namespace theme {

// Appearance tokens for the animation graph overlay (gui::drawPanel, in
// src/gui/GraphPanelFrame.cpp).
//
// The panel is drawn by the gui layer from gui::GraphPanelModel, so - exactly
// like theme::TimeLine for ctrl::time::Renderer - the palette decisions live here
// and the drawing layer only reads them. Every token is a curated cell of
// theme::Colors, so an accent or light/dark change is one rebuild (Graph::from)
// and never a per-widget color.
//
// Curves are colored by CHANNEL through channelColor(), i.e. by the one hue
// table in theme::Colors (design section 7: "axis where there is an axis, type
// otherwise"), the same table the timeline draws its keys and lane labels from:
// a curve on the graph and its keys on the timeline read as the same feature. A
// KEY MARKER on the panel carries that hue too (its fill), so the curve, its key
// squares and its legend swatch are one colour, and the only thing the palette
// names for every key besides it is the edge that keeps the fill legible over
// arbitrary artwork.
class Graph {
public:
    Graph();

    // Curated tokens for the active palette.
    static Graph from(const Colors& aColors);

    QColor gridMajor() const { return mGridMajor; }  // the frame ticks (the panel has no grid)
    QColor axisLine() const { return mAxisLine; }    // value-axis base line
    // THE FRAME RULER'S BAND, in the timeline's own tokens: the strip is the
    // timeline's surface (theme::TimeLine::headerBackgroundColor() == Colors::base)
    // and the 1px rule under it is the timeline's ruler line
    // (theme::TimeLine::rulerLineColor() == Colors::hairline) - the two views'
    // rulers are one chrome (see THE RULER in GraphPanelModel.h).
    QColor rulerBand() const { return mRulerBand; }
    QColor rulerLine() const { return mRulerLine; }
    // The playhead pill's number. THE PILL'S OWN TEXT TOKEN, not the ruler's:
    // the pill is an accentBright fill, so its number is
    // theme::Colors::accentBrightText - the cell gui::TimeCursor is handed for the
    // same pill (TimeLineEditorWidget.cpp), and the one that reads DARK on the
    // violet fill in the dark palette (the shipped gate measured the old
    // Colors::text here as two near-white pixels, i.e. an invisible number). The
    // ruler's own numbers keep Colors::text through axisText(); a number on a fill
    // and a number on the base surface are different inks on purpose.
    QColor playheadText() const { return mPlayheadText; }
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
    QColor curve() const { return mCurve; }          // fallback curve color
    // The key marker's edge: the ring around a key square, and the ring around a
    // legend swatch. The FILL is the channel's own colour (channelColor), which is what
    // makes a key read as the feature it belongs to - see the class comment.
    QColor keyEdge() const { return mKeyEdge; }
    // Structural accent: the playhead's line+pill and the marquee edge (a handle is
    // NOT highlighted with it; a hovered handle is hue-derived, see GraphPanelFrame).
    QColor selected() const { return mSelected; }
    // A STORED tangent handle's fill: the raised surface the reference fills it with
    // (mock-graph.js:898-900). Its stroke and its stem take the CHANNEL's own hue
    // from the curve (PanelCurve::color), not from here - and the drawing layer
    // mixes that hue toward halo() by a small factor so a handle reads where it sits
    // ON its own curve (GraphPanelFrame.cpp's kHandleChromaMix). halo() is the
    // neutral the panel's text is drawn against, i.e. the palette's floor tone.
    QColor handleFill() const { return mHandleFill; }
    // The marquee: the live rectangle of a box-select drag. The same two decisions
    // the timeline's own box-select makes (ctrl/time/time_Renderer.cpp,
    // renderSelectionRange): the bright accent edge at 230/255 and the same hue as
    // the body at 24/255, so the gesture reads as one feature in both views.
    QColor marqueeFill() const { return mMarqueeFill; }
    QColor marqueeEdge() const { return mMarqueeEdge; }

    // The hue of ONE graph channel: the shared table in theme::Colors (THE
    // CHANNEL HUE TABLE), i.e. the axis hue where the component lies on an axis
    // and the key type's own hue otherwise. Falls back to curve() for a type the
    // palette does not name. The panel's curve stroke, key-marker fill and chip
    // text all read this one answer.
    QColor channelColor(core::TimeKeyType aType, int aComponent) const;

    // The hue a TYPE's timeline key and lane label are drawn in: the type's
    // first component through the same table (the X axis hue where that
    // component lies on X - Move/Scale/Image/Blur - its own hue otherwise).
    // Falls back to curve() for an unnamed type.
    QColor keyColor(core::TimeKeyType aType) const;

private:
    // Fills every token from one palette. A separate step rather than a
    // constructor body, so the default constructor and from() cannot recurse
    // into each other (from() must hand back a filled Graph, and Graph has no
    // other way to obtain storage).
    void assignFrom(const Colors& aColors);

    QColor mGridMajor;
    QColor mAxisLine;
    QColor mRulerBand;
    QColor mRulerLine;
    QColor mPlayheadText;
    QColor mAxisText;
    QColor mHalo;
    QColor mOutline;
    QColor mCurve;
    QColor mKeyEdge;
    QColor mSelected;
    QColor mHandleFill;
    QColor mMarqueeFill;
    QColor mMarqueeEdge;
    // The channel hue table's axis cells, from theme::Colors, so channelColor()
    // needs no palette lookup per paint.
    QColor mAxisColors[kChannelAxisCount];
    QColor mKeyColors[core::TimeKeyType_TERM];
};

} // namespace theme

#endif // GUI_THEME_GRAPH_H
