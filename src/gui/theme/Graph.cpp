#include "gui/theme/Graph.h"

#include "gui/theme/Colors.h"

namespace theme {

namespace {
// The neutral row's darkest column: Tailwind's neutral-950, the palette's own
// black. The contrast outline is the one token that must NOT follow the
// palette polarity (see Graph::outline() in the header), so it takes this cell
// from BOTH tables rather than a theme::Colors role - every content token in
// Colors flips to near-white in the dark palette.
constexpr int kNeutralBlackStep = 10;
} // namespace

//-------------------------------------------------------------------------------------------------
Graph::Graph() {
    // Same contract as TimeLine: read the cached palette that GUIResources
    // activated before any editor was built, so constructing a Graph never
    // touches settings or recomputes the tokens.
    assignFrom(Colors::current());
}

Graph Graph::from(const Colors& aColors) {
    Graph graph;
    graph.assignFrom(aColors);
    return graph;
}

void Graph::assignFrom(const Colors& aColors) {
    // The frame ticks read in the SAME ink as the frame numbers below them, at a
    // tier's alpha: the timeline's own rule for its ruler, where one token
    // (TimeRuler.cpp:106, headerContentColor == Colors::text) draws the ticks, the
    // numbers and the boundary labels, and the tier is the ALPHA (TimeRuler.cpp:22-30:
    // 0.85 major / 0.60 intermediate / 0.20 minor, one tick length for every level).
    // The panel used to draw its ticks at the separator tone (hairline) under
    // full-strength text numbers - two different tokens for one ruler, and the
    // separator tone is the ink that disappears over mid-grey artwork (measured in
    // Graph.h's axisText() note: 1.20:1 over mid-grey, against 4.54:1 for text).
    // The drawing layer (gui::drawPanel) owns the tier table and applies the alpha;
    // this token is the one ink both families are made of, the same one axisText()
    // hands the numbers.
    mGridMajor = aColors.text;

    // Value axis: the hover separator is the one line tone that stays legible
    // over the artwork the panel floats on.
    //
    // The labels read at FULL text strength - the token the timeline draws its own
    // ruler numbers in (theme::TimeLine::headerContentColor() == Colors::text), so
    // the panel's numbers and the ruler's numbers are the same ink. textMuted, the
    // token this used to be, is the too-dim grey the user reported: the halo that
    // would have backed these glyphs ships OFF (gui::kDrawTextHalo), so they sit on
    // arbitrary artwork like the rest of the panel's ink, and a mid grey is the one
    // tone that vanishes over mid-grey artwork. The contrast outline pass can only
    // RING such ink (black at kMinContrast = 3:1, gui::outline::kMinContrast), it
    // cannot brighten the glyph - so the fix is the ink, not the ring.
    //
    // WCAG contrast (relative luminance, sRGB linearised) of the two candidates,
    // computed from this palette's Tailwind cells:
    //
    //                                  light            dark
    //     text  vs halo/base       17.18 : 1       16.44 / 13.88 : 1
    //     text  vs recessed        16.44 : 1       16.44 : 1
    //     text  vs raised          14.23 : 1        9.51 : 1
    //     textMuted vs halo/base    4.54 : 1        7.11 /  6.00 : 1
    //     textMuted vs mid-grey    1.20 : 1        1.57 : 1   (artwork, no halo)
    //     text      vs mid-grey    4.54 : 1        3.62 : 1   (artwork, no halo)
    //
    // (light halo == base == neutral-50 #fafafa, dark halo neutral-950 #171717 over
    // base neutral-800 #262626; mid-grey is #808080, the worst case for both
    // palettes' greys.) text clears the application's own 3:1 threshold
    // (gui::outline::kMinContrast) on every surface by 3x or more, and by 5x on the
    // halo; textMuted meets it on the halo but falls to 1.20:1 - below the threshold,
    // so the outline pass rings it - where the label actually lands, on the artwork.
    mAxisLine = aColors.hairlineHover;
    mAxisText = aColors.text;

    // The frame ruler's band and hairline: the timeline's ruler tokens, read from the
    // same palette cells the timeline does (theme::TimeLine::headerBackgroundColor ==
    // base, rulerLineColor == hairline). The panel's ruler IS the timeline's ruler
    // (see GraphPanelModel.h), so the two surfaces cannot drift.
    mRulerBand = aColors.base;
    mRulerLine = aColors.hairline;
    // THE PLAYHEAD PILL'S NUMBER IS NOT THE RULER'S NUMBER INK. The pill is an
    // accentBright FILL (Colors::accentBright, the same cell TimeCursor fills), so
    // its number takes the token the timeline puts on that fill:
    // theme::Colors::accentBrightText (TimeLineEditorWidget.cpp sets
    // TimeCursor::setNumberColor to it, "max-contrast text on an accentBright
    // pill"). It was Colors::text - the ink of a number on the BASE surface - which
    // is near-white in the dark palette: near-white on the violet fill, measured at
    // 2 lit pixels of (240,238,246) in the shipped build, i.e. an invisible number
    // where the timeline draws a dark one. A pill's number is read against the
    // PILL, so it takes the pill's text token.
    mPlayheadText = aColors.accentBrightText;

    // Label halo: the palette's opaque floater body. The panel paints no
    // background of its own (see GraphPanelModel's header), so the halo is the
    // tone the text would sit on if it had one - and a halo that carried the
    // popup body's translucency would let the artwork show through the glyph
    // ring, which is the one thing the halo exists to prevent.
    mHalo = aColors.floaterBody;

    mOutline = paletteColor(kTailwindRows[kTailwindNeutralRow][kNeutralBlackStep]);

    // Fallback curve color, i.e. the generic line for a type channelColor()
    // does not name: the bright brand edge, the same family the playhead and
    // the selection read as.
    mCurve = aColors.accentBright;

    // Key markers: the channel's own hue as the fill (the drawing layer paints
    // curve.color, which is channelColor(channel.type(), channel.component()))
    // inside a floor-coloured ring, so a key stays visible on top of any curve
    // hue - including its own. The selected key is the brand edge, fill and ring
    // alike.
    mKeyEdge = aColors.base;
    mSelected = aColors.accentBright;

    // The marquee: the timeline's own box-select colours (accentBright at 24 for the
    // body, 230 for the edge).
    mMarqueeFill = aColors.accentBright;
    mMarqueeFill.setAlpha(24);
    mMarqueeEdge = aColors.accentBright;
    mMarqueeEdge.setAlpha(230);

    // The menu: the app's own context menu, token for token (see the header).
    mMenuBody = aColors.floaterBody;
    mMenuEdge = aColors.floaterEdge;
    mMenuText = aColors.text;
    mMenuTextDisabled = aColors.textDisabled;
    mMenuHover = aColors.accentHover;
    mMenuHoverText = aColors.accentText;
    // A rule on the floater body takes the floater's edge token, not hairline:
    // hairline shares the dark body's palette cell (invisible at 1px), while
    // floaterEdge is one step above the body in both themes. Same decision as
    // QMenu::separator (standard.ssa) and AppStyle's combo popup.
    mMenuSeparator = aColors.floaterEdge;

    // Tangent handles: a stored pair is filled with the raised surface the reference
    // fills it with; its stroke and stem take the CHANNEL's own hue where they are
    // drawn (GraphPanelFrame.cpp), so the palette names no grey for them.
    mHandleFill = aColors.raised;

    // The channel hue table (theme::Colors, THE CHANNEL HUE TABLE): the axis
    // cells, and each key type's own hue for a channel with no axis. Both the
    // panel and the timeline read the one table in Colors.h; this copy exists
    // so the panel's paint path does no palette lookup per curve.
    mAxisColors[kChannelAxisX] = aColors.axisX;
    mAxisColors[kChannelAxisY] = aColors.axisY;
    mAxisColors[kChannelAxisZ] = aColors.axisZ;

    // Only the GRAPHABLE types can reach the panel, and their component count is
    // what proves it: ctrl::graph::Channel::componentCount() returns 0 for Bone,
    // Pose, Mesh and FFD (graph_Channel.cpp:22-40), and Channel::graphableTypes()
    // is the filter that walks componentCount() > 0 (graph_Channel.cpp:50-63) -
    // so buildChannels (GraphPanelModel.cpp) can never publish a row or curve for
    // them. They are therefore NOT in this table (design section 7: "should be
    // DELETED rather than coloured"; section 8's one-line row still says they
    // "keep their hues", which is unreachable code - section 7's own reasoning
    // wins here); the TIMELINE still draws their keys, so
    // theme::Colors keeps their tokens and keyTypeColor() still names them.
    // baseKey stays the palette's neutral key tone, not a family's.
    mKeyColors[core::TimeKeyType_Move] = aColors.moveKey;
    mKeyColors[core::TimeKeyType_Rotate] = aColors.rotateKey;
    mKeyColors[core::TimeKeyType_Scale] = aColors.scaleKey;
    mKeyColors[core::TimeKeyType_Depth] = aColors.depthKey;
    mKeyColors[core::TimeKeyType_Opa] = aColors.opaKey;
    mKeyColors[core::TimeKeyType_Image] = aColors.imageKey;
    mKeyColors[core::TimeKeyType_HSV] = aColors.HSVKey;
    mKeyColors[core::TimeKeyType_Blur] = aColors.blurKey;
    mKeyColors[core::TimeKeyType_Wave] = aColors.waveKey;
}

QColor Graph::channelColor(core::TimeKeyType aType, int aComponent) const {
    // TimeKeyType_TERM is the sentinel, never a real key's type, and a value
    // past it is not a type this palette names: both fall back to the generic
    // curve color, exactly as keyColor() always has.
    const int type = (int)aType;
    if (type < 0 || type >= core::TimeKeyType_TERM)
        return mCurve;
    const int axis = channelAxis(aType, aComponent);
    if (axis >= 0 && axis < kChannelAxisCount)
        return mAxisColors[axis];
    // Bone/Pose/Mesh/FFD are graphable by no caller (see above), so an index
    // here is either a graphable type's own hue or an unnamed fallback.
    const QColor hue = mKeyColors[type];
    return hue.isValid() ? hue : mCurve;
}

QColor Graph::keyColor(core::TimeKeyType aType) const {
    // A timeline key belongs to the TYPE, so it takes the type's first
    // component's hue from the same table: X red for Move/Scale/Image/Blur,
    // its own hue otherwise (Opa amber, Rotate purple, ...).
    return channelColor(aType, 0);
}

} // namespace theme
