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
    // The frame ticks read at the separator tone. There is no second, weaker
    // tone: the panel draws ticks and no grid, so nothing is a step behind.
    mGridMajor = aColors.hairline;

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

    // Label halo: the palette's opaque floater body. The panel paints no
    // background of its own (see GraphPanelModel's header), so the halo is the
    // tone the text would sit on if it had one - and a halo that carried the
    // popup body's translucency would let the artwork show through the glyph
    // ring, which is the one thing the halo exists to prevent.
    mHalo = aColors.floaterBody;

    mOutline = paletteColor(kTailwindRows[kTailwindNeutralRow][kNeutralBlackStep]);

    // Fallback curve color, i.e. the generic line for a type keyColor() does
    // not name: the bright brand edge, the same family the playhead and the
    // selection read as.
    mCurve = aColors.accentBright;

    // Key markers: the channel's own hue as the fill (the drawing layer paints
    // curve.color, which is keyColor(type)) inside a floor-coloured ring, so a key
    // stays visible on top of any curve hue - including its own. The selected key is
    // the brand edge, fill and ring alike.
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
    mMenuSeparator = aColors.hairline;

    // Tangent handles: raised like the widgets they emulate, edged with the
    // popup border tone, stems muted so they never outread the curve itself.
    mHandleFill = aColors.raised;
    mHandleEdge = aColors.floaterEdge;
    mHandleLine = aColors.textMuted;

    // Per-type curve colors are the timeline's key hues, so a curve on the
    // graph and its keys on the timeline read as the same feature. The
    // neutral family shares baseKey (the timeline's default key color); the
    // array covers every type below TimeKeyType_TERM.
    mKeyColors[core::TimeKeyType_Move] = aColors.moveKey;
    mKeyColors[core::TimeKeyType_Rotate] = aColors.rotateKey;
    mKeyColors[core::TimeKeyType_Scale] = aColors.scaleKey;
    mKeyColors[core::TimeKeyType_Depth] = aColors.depthKey;
    mKeyColors[core::TimeKeyType_Opa] = aColors.opaKey;
    mKeyColors[core::TimeKeyType_Bone] = aColors.baseKey;
    mKeyColors[core::TimeKeyType_Pose] = aColors.baseKey;
    mKeyColors[core::TimeKeyType_Mesh] = aColors.baseKey;
    mKeyColors[core::TimeKeyType_FFD] = aColors.baseKey;
    mKeyColors[core::TimeKeyType_Image] = aColors.imageKey;
    mKeyColors[core::TimeKeyType_HSV] = aColors.HSVKey;
    mKeyColors[core::TimeKeyType_Blur] = aColors.blurKey;
}

QColor Graph::keyColor(core::TimeKeyType aType) const {
    // TimeKeyType_TERM is the sentinel, never a real key's type, and a value
    // past it is not a type this palette names: both fall back to the generic
    // curve color.
    const int type = (int)aType;
    if (type < 0 || type >= core::TimeKeyType_TERM)
        return mCurve;
    return mKeyColors[type];
}

} // namespace theme
