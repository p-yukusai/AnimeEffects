#ifndef CTRL_GRAPH_GEOMETRY_H
#define CTRL_GRAPH_GEOMETRY_H

#include <QPoint>
#include <QPointF>
#include <QRect>
#include "core/Frame.h"
#include "ctrl/graph/graph_Curve.h"
#include "ctrl/graph/graph_Scaler.h"

namespace ctrl {
namespace graph {

// THE ONE HOME OF THE PANEL'S SHARED GEOMETRY.
//
// The animation graph panel is DRAWN by one layer (gui::drawPanel, fed by
// gui::GraphPanelModel) and PICKED by another (ctrl::GraphEditor::hitTest), and
// the two have to agree to the pixel: a drawing layer that computes its own
// rects silently desyncs from the pick, so a user grabs something invisible or
// the visible thing does not respond. That is the whole reason this header
// exists, and it is why the geometry lives here rather than beside either
// consumer:
//   * the editor's hit-test reads kKeyHitRadius / kHandleHitRadius and the
//     keyRect() / handlePoint() mappings below;
//   * the model publishes the drawing geometry FROM these same functions
//     (keyDot() for a key marker, handlePoint() for a tangent handle's dot);
//   * the drawing layer draws only what the model published, so the drawn
//     marker and the geometry the editor grabs are derived from ONE mapping by
//     construction, not by a coincidence two copies of the arithmetic would have
//     to keep. Their SIZES differ deliberately - see the visual/hit split below.
//
// Nothing here holds state and nothing reads the document: every entry is a
// pure mapping through the panel's Scaler, or a radius. Curve::handle() - the
// NORMALIZED handle space these functions map through - stays on graph::Curve,
// where the easing that defines it lives; this header maps that space to pixels.
//
// ---------------------------------------------------------------------------
// VISUAL AND HIT GEOMETRY ARE SEPARATE, AND THAT IS THE RULE HERE.
//
// A marker is DRAWN small and GRABBED large, so a key that is comfortable to hit
// is not a clumsy blob on the curve. This is the timeline's own rule ("those two
// should not be the same for small elements"), and the panel takes it from
// there. For a key:
//
//   * VISUAL - a circle of radius kKeyDotRadius centred on the key's mapped
//     point, i.e. keyDot(). The radius is THE TIMELINE'S OWN KEY MARKER:
//     src/ctrl/time/time_Renderer.h:17-20 declares ctrl::radiiFactor = 1.25 and
//     ctrl::keyRadius = 3.0 * radiiFactor = 3.75 (px), and drawKeys() paints a
//     key body with `mPainter.drawEllipse(pos, keyRadius, keyRadius)`
//     (src/ctrl/time/time_Renderer.cpp:290). 3.75 px is therefore the size the
//     application already uses for a key, and the graph panel draws the same
//     marker so a key reads the same in both views. tests/graph_panel_model.cpp
//     pins kKeyDotRadius to ctrl::keyRadius, so the two cannot drift apart.
//     (Before this split the panel painted a 10px square - 1.33x the timeline's
//     7.5px dot across, 2.3x its area - because the pick square was reused as
//     the picture.)
//   * HIT - the editor's distance rule with kKeyHitRadius: a press within 10 px
//     of a key's centre grabs it (GraphEditor's hitTest). keyRect() is that
//     rule's RECTANGLE form - a square of side 2 * kKeyRadius centred on the
//     same point - and the model still publishes it as `keys`, because the
//     editor's marquee and the panel's right-click key menu test a RECT. It is
//     hit geometry: it is not what gets painted, and the two are deliberately
//     different sizes.
//
// The same split holds for a tangent handle: kHandleRadius (5 px) is the drawn
// dot at handlePoint(), kHandleHitRadius (12 px) the grab - and the grab is
// deliberately larger than a key's own, because a handle is the more specific
// target and the hit-test resolves a handle first.

// The drawn radius of a key's marker, in pixels: a circle of this radius at
// keyDot(). == ctrl::keyRadius (src/ctrl/time/time_Renderer.h), the timeline's
// own key-body radius: 3.0 px * ctrl::radiiFactor (1.25) = 3.75 px.
constexpr double kKeyDotRadius = 3.75;

// The half-side of a key's HIT square, in pixels: keyRect() is 2 * kKeyRadius
// across, centred on the same point as the dot. NOT a drawn size any more - the
// marker is the circle keyDot() names (see the split above). This square is what
// the editor's marquee and the panel's right-click menu test for a key, and it
// is also the side of a legend row's colour chip (a square chip, not a marker).
constexpr int kKeyRadius = 5;

// The grab radius of a key, in pixels. The hit-test compares a press against
// this with `<=`, so a press EXACTLY this far from a key's centre still hits.
// Larger than kKeyDotRadius by design: the hit area is generous, the ink is not.
constexpr int kKeyHitRadius = 10;

// The drawn radius of a tangent handle's dot, in pixels.
constexpr int kHandleRadius = 5;

// The grab radius of a tangent handle, in pixels. Deliberately larger than a
// key's own kKeyHitRadius, because a handle is the more specific target of the
// two and the hit-test resolves a handle first.
constexpr int kHandleHitRadius = 12;

// The pixel centre of the key at (frame, value): the point the marker is drawn
// about, the point keyRect() is centred on, and the point the hit-test measures
// from. One derivation, so the three can never disagree.
inline QPoint keyCenter(const Scaler& aScale, core::Frame aFrame, float aValue) {
    return QPoint(aScale.pixelX(aFrame), aScale.pixelY(aValue));
}

// A key's HIT rectangle: a square of side 2 * kKeyRadius centred on the mapped
// point. The editor's hit-test, its marquee and the panel's right-click menu all
// use this rect, so the clickable key is exactly the key they agree on. It is
// NOT the drawn marker - that is the circle keyDot() below (see the header).
inline QRect keyRect(const Scaler& aScale, core::Frame aFrame, float aValue) {
    const QPoint center = keyCenter(aScale, aFrame, aValue);
    return QRect(center.x() - kKeyRadius, center.y() - kKeyRadius, 2 * kKeyRadius, 2 * kKeyRadius);
}

// The key MARKER's centre in panel pixels: a circle of radius kKeyDotRadius
// drawn about this point. The model publishes it per key (`PanelCurve::keyDots`)
// so the drawing layer never has to reconstruct a centre from a CLIPPED hit
// rect - which would put the dot in the wrong place for a key on the plot's
// edge, since keyRect() is cut there while the marker stays on the key.
inline QPointF keyDot(const Scaler& aScale, core::Frame aFrame, float aValue) {
    const QPoint center = keyCenter(aScale, aFrame, aValue);
    return QPointF(center.x(), center.y());
}

// Where a Custom easing's tangent handle lands in panel pixels. x normalizes
// along the segment's frame span and y across its value span - the space
// Curve::handle() stores handles in, because it is the space playback consumes -
// so the pixel is that normalized point pushed through the two axes.
//
// aHandleIndex 0 leaves the segment's `from` key and 1 arrives at its `to` key.
// The editor's hit-test grabs a handle here (within kHandleHitRadius) and the
// model publishes its dot here (drawn at radius kHandleRadius), so a grab at the
// dot is the drag the editor performs.
inline QPointF handlePoint(const Scaler& aScale, const Curve::Segment& aSegment, int aHandleIndex) {
    const QPointF normalized = Curve::handle(aSegment, aHandleIndex);
    const double frameSpan = static_cast<double>(aSegment.to->frame() - aSegment.from->frame());
    const double valueSpan = static_cast<double>(aSegment.toValue - aSegment.fromValue);

    const double frame = static_cast<double>(aSegment.from->frame()) + normalized.x() * frameSpan;
    const float value = aSegment.fromValue + static_cast<float>(normalized.y() * valueSpan);
    return QPointF(aScale.pixelXOfFrame(frame), static_cast<double>(aScale.pixelY(value)));
}

} // namespace graph
} // namespace ctrl

#endif // CTRL_GRAPH_GEOMETRY_H
