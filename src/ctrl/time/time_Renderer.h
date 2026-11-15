#ifndef CTRL_TIME_RENDERER_H
#define CTRL_TIME_RENDERER_H

#include <QVector>
#include <QPainter>
#include <QSettings>
#include "util/Range.h"
#include "core/CameraInfo.h"
#include "core/ObjectNode.h"
#include "core/TimeFormat.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/time/time_Scaler.h"
#include "gui/theme/TimeLine.h"

namespace ctrl {
    constexpr float radiiFactor = 1.25;
    constexpr float childRadiiFactor = 1.0;
    constexpr double keyRadius = 3.0 * radiiFactor;
    constexpr double keyRadiusSlim = 2.0 * radiiFactor;
    constexpr double keyRadiusChild = 3.0 * childRadiiFactor;
    constexpr double keyRadiusChildPoly = 5.0 * childRadiiFactor;
    // ONE rounding radius for every POLYGON key marker (holder diamond, slimmed
    // rectangle, closed folder's summary triangle). 2px is visibly rounded at 1x
    // while the diamond still reads as a diamond; see AGENTS.md for the matrix.
    constexpr double kKeyCornerRadius = 2.0;
namespace time {

    // THE SELECTED KEY'S RING - one rule for BOTH views, so it is named here and
    // read by the graph's marker pass too (gui::drawPanel):
    //   * ink   - the key's OWN channel hue at kSelectRingAlpha (0.85), 1px wide;
    //   * place - an ellipse centred on the marker whose radius is the marker's own
    //             radius PLUS kSelectRingOffset, drawn OUTSIDE the marker so the
    //             fill keeps its size. The graph's marker radius is
    //             ctrl::graph::kKeyDotRadius, pinned to keyRadius, so the two
    //             engines put the same band at the same distance.
    // The timeline's pen is centred on the radius (QPainter); the graph's ImGui
    // circle strokes INWARD, so it adds half a pixel (see its marker pass). That
    // half pixel is the only difference, and it is the engines' stroke convention,
    // not the rule's.
    constexpr int kSelectRingAlpha = 217;      // 0.85 of the channel hue
    constexpr double kSelectRingOffset = 2.6;  // px past the marker's own radius

    // THE PROPORTIONAL KEY IS A RING, not a softer fill. A key whose frame/value
    // follow its absolute neighbours is still the same channel hue at the same radius
    // and with the same edge, but its body is HOLLOW: a hue band around a hole, inside
    // the key's ordinary edge. Opacity was the old cue and it read as "the same key,
    // disabled" beside the selection and focus states; a hole is a shape difference
    // that survives a small marker, a light theme and a dense lane.
    //
    // The ring is built from FILLS, not a stroked circle: the hue disc reaches
    // kProportionalRingOuterInset inside the marker's radius (so the ordinary 1px edge
    // sits on its outside), and a disc of the LANE's own ink, of radius
    // radius - kProportionalHoleInset, is drawn over its centre. At the timeline's
    // 3.75px key that is a 2px hue band around a 1.25px hole. Fills are what keep the
    // QPainter and Canvas hosts byte-identical; a 2px stroked circle put their two
    // antialiasing kernels 69 pixels apart on the parity gate. The graph panel's marker
    // pass reads the SAME two insets, and fills its hole with the panel floor
    // (theme::Graph::keyEdge), so one proportional key cannot be inked two ways.
    constexpr double kProportionalRingOuterInset = 0.5;
    constexpr double kProportionalHoleInset = 2.5;
    // A POLYGON's ring is two scaled copies of its own outline (a slimmed lane's
    // rectangle, a folder's holder diamond, a closed folder's summary triangle): the
    // hue disc is the kProportionalPolygonInset copy, the hole the
    // kProportionalPolygonHoleInset one.
    constexpr double kProportionalPolygonInset = 0.62;
    constexpr double kProportionalPolygonHoleInset = 0.32;

    class Renderer {
    public:
        Renderer(QPainter& aPainter, const core::CameraInfo& aCamera, const theme::TimeLine& aTheme);

        void setMargin(int aMargin) { mMargin = aMargin; }
        void setRange(const util::Range& aRange) { mRange = aRange; }
        void setTimeScale(const Scaler& aScale) { mScale = &aScale; }
        // The project frame range: the range markers sit at its first and
        // last frame's ticks. The temporary window dims the markers while it
        // overrides; it does not move them.
        void setProjectRange(const util::Range& aRange) { mProjectRange = aRange; }
        // The temporary playback window (see ctrl::TimeLineEditor::PlayWindow);
        // the range markers dim while it is active.
        void setPlayWindow(const ctrl::TimeLineEditor::PlayWindow& aWindow) { mPlayWindow = aWindow; }

        // The track background: fills, lane separators, type labels and the
        // row seams. Keys are a separate pass (renderKeys) so background
        // layers like the range markers can paint between them.
        void renderLines(const QVector<TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect);
        void renderRangeMarkers(const QRect& aCameraRect);
        // Key graphics for every row: child-key summaries on closed folders
        // and the per-lane key shapes.
        void renderKeys(const QVector<TimeLineRow>& aRows, const QRect& aCullRect);
        void renderSelectionRange(const QRect& aRect);

    private:
        void drawTimeCurrent();
        void drawKeys(const core::ObjectNode* aNode, const TimeLineRow& aRow);
        void drawChildKeys(const core::ObjectNode* aNode, const QPoint& aPos);

        QPainter& mPainter;
        const core::CameraInfo& mCamera;
        const theme::TimeLine& mTheme;
        int mMargin;
        util::Range mRange;        // the visible frame range (culling)
        util::Range mProjectRange; // the project frame range (range markers)
        const Scaler* mScale;
        ctrl::TimeLineEditor::PlayWindow mPlayWindow;
    };

} // namespace time
} // namespace ctrl

#endif // CTRL_TIME_RENDERER_H
