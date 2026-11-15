#include "ctrl/time/time_BodyModel.h"

#include <QCoreApplication>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/TimeKey.h"
#include "core/TimeLine.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/TimeLineRow.h"
#include "ctrl/time/time_Scaler.h"
#include "gui/theme/Colors.h"

namespace ctrl {
namespace time {

namespace {

// The lane label's ink: the channel's hue at the timeline's own alpha (design s1 - one
// step quieter than the keys). Read from the SAME cells the keys use, so a lane's name,
// its keys and the graph panel's curve cannot disagree about a channel's colour.
constexpr int kLabelAlpha = 107; // 0.42

QColor withAlpha(QColor aColor, int aAlpha) {
    aColor.setAlpha(aAlpha);
    return aColor;
}

QColor keyColor(core::TimeKeyType aType) {
    return theme::channelColor(theme::Colors::current(), aType, 0);
}

constexpr int kLabelWidth = 100;

// THE FOCUSED KEY'S FILL: the channel hue mixed 0.45 of the way to the TEXT colour.
constexpr double kSelectFillMix = 0.45;

QColor mixTowardText(const QColor& aColor) {
    const QColor text = theme::Colors::current().text;
    const auto mix = [](int aChannel, int aTarget) {
        return static_cast<int>(std::lround(aChannel + (aTarget - aChannel) * kSelectFillMix));
    };
    return QColor(
        mix(aColor.red(), text.red()), mix(aColor.green(), text.green()), mix(aColor.blue(), text.blue()),
        aColor.alpha()
    );
}

// The selected key's ring: the key's OWN channel hue at the shared alpha, drawn OUTSIDE the marker.
constexpr double kSelectRingRadiusModel = keyRadius + kSelectRingOffset;
// A folder's holder diamond arm.
constexpr double kHolderArm = 4.2;

// A POLYGON PROPORTIONAL KEY'S HOLE: the marker's own outline scaled toward its centre.
// One helper for the lane keys and the closed folder's summaries, and the model stores
// the result so the QPainter and Canvas hosts stroke the same polygon.
QPolygonF insetPolygon(const QPolygonF& aPolygon, const QPointF& aCentre, double aScale) {
    QPolygonF inset;
    inset.reserve(aPolygon.size());
    for (const QPointF& point : aPolygon)
        inset.append(aCentre + (point - aCentre) * aScale);
    return inset;
}

// A POLYGON KEY MARKER'S CORNERS: every polygon marker (holder diamond, slimmed
// rectangle, closed folder's summary triangle) is rounded by the same radius. The
// corner is flattened through QPainterPath::toFillPolygon, so the model stores ONE
// polygon for both rasterisers - a path with arcs would be a second geometry.
QPolygonF roundedPolygon(const QPolygonF& aPolygon, double aRadius) {
    if (aPolygon.size() < 3 || aRadius <= 0.0)
        return aPolygon;

    const int count = aPolygon.size();
    const auto unit = [](const QPointF& aVector) {
        const double length = std::hypot(aVector.x(), aVector.y());
        return length > 1e-6 ? aVector / length : QPointF();
    };
    QVector<QPointF> in(count);
    QVector<QPointF> out(count);
    for (int i = 0; i < count; ++i) {
        const QPointF current = aPolygon[i];
        const QPointF previous = aPolygon[(i + count - 1) % count];
        const QPointF next = aPolygon[(i + 1) % count];
        const QPointF uPrevious = unit(previous - current);
        const QPointF uNext = unit(next - current);
        const double dot = std::clamp(
            uPrevious.x() * uNext.x() + uPrevious.y() * uNext.y(), -1.0, 1.0
        );
        const double angle = std::acos(dot);
        double distance = angle > 1e-6 ? aRadius / std::tan(angle * 0.5) : 0.0;
        distance = std::min(
            distance, 0.45 * std::min(std::hypot((previous - current).x(), (previous - current).y()),
                                      std::hypot((next - current).x(), (next - current).y()))
        );
        in[i] = current + uPrevious * distance;
        out[i] = current + uNext * distance;
    }

    QPainterPath path;
    path.moveTo(in[0]);
    for (int i = 0; i < count; ++i) {
        path.quadTo(aPolygon[i], out[i]);
        path.lineTo(in[(i + 1) % count]);
    }
    path.closeSubpath();
    return path.toFillPolygon();
}

} // namespace

QColor opaqueOver(const QColor& aTop, const QColor& aBottom) {
    const double a = aTop.alphaF();
    const auto mix = [a](double top, double bottom) { return top * a + bottom * (1.0 - a); };
    QColor out;
    out.setRgbF(mix(aTop.redF(), aBottom.redF()), mix(aTop.greenF(), aBottom.greenF()),
                mix(aTop.blueF(), aBottom.blueF()), 1.0);
    return out;
}

Lines buildLines(
    const QVector<ctrl::TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect, const Inks& aInks
) {
    Lines lines;
    lines.inks = aInks;
    const int textLeft = aCameraRect.center().x() - kLabelWidth / 2;

    const auto bandOver = [&aCameraRect](const QRect& aRect) {
        return QRect(
            QPoint(aCameraRect.left(), aRect.top()), QPoint(aCameraRect.right(), aRect.bottom())
        );
    };

    for (const ctrl::TimeLineRow& row : aRows) {
        const QRect rect = row.rect;
        // Rows are culled by their vertical band only: the horizontal view extent is never
        // bounded by the frame range, so lanes stay visible even when the view floats
        // entirely outside it.
        if (rect.bottom() < aCullRect.top() || aCullRect.bottom() < rect.top())
            continue;

        // The folder's own lanes. A summary-only row has no own lane: the whole row is
        // the summary band (see TimeLineRow), so it is not drawn twice.
        const QRect own = row.ownRect();
        if (own.height() > 0) {
            // The fill spans the visible width (the camera rect), not the frame range: when
            // the content is narrower than the viewport the lanes reach the panel edges.
            const QRect band = bandOver(own);
            Lane lane;
            lane.rect = band;
            lane.selected = row.selecting;

            if (row.node && row.node->timeLine() && !row.node->isSlimmedDown()) {
                const core::TimeLine& timeLine = *(row.node->timeLine());
                const int sepa = timeLine.validTypeCount();
                const float h = sepa > 0 ? static_cast<float>(own.height()) / sepa : 0.0f;

                for (int i = 1; i < sepa; ++i) {
                    const QPointF v0(band.left(), own.top() + i * h);
                    const QPointF v1(band.right(), own.top() + i * h);
                    lane.separators.append({int(v0.x()), int(v1.x()), v0.y()});
                }

                int i = 0;
                for (int typei = 0; typei < core::TimeKeyType_TERM; ++typei) {
                    const core::TimeKeyType type = core::TimeLine::getTimeKeyTypeInOrderOfOperations(typei);
                    if (timeLine.isEmpty(type))
                        continue;
                    Label label;
                    label.box = QRect(
                        textLeft, own.top() + static_cast<int>(i * h), kLabelWidth, static_cast<int>(h)
                    );
                    label.text = core::TimeLine::getTimeKeyDisplayName(type);
                    label.ink = withAlpha(keyColor(type), kLabelAlpha);
                    lane.labels.append(label);
                    ++i;
                }
            }

            lines.lanes.append(lane);
        }

        // The dedicated folder-summary band. It is a lane of its own, so the summary can
        // never paint over a child's lane - the reason it exists.
        const QRect summary = row.summaryRect();
        if (summary.height() > 0) {
            Lane lane;
            lane.rect = bandOver(summary);
            lane.selected = row.selecting;
            lane.summary = true;
            // THE DIVIDER IS A TYPE SEPARATOR, not a row seam: the summary band is a LANE of this
            // row, and the dashed 40% ink is what the timeline uses BETWEEN BANDS inside one
            // lane, while the solid 70% seam separates one ROW from the next. It used to be a
            // seam, which read as "this band is a special case" - it is not.
            if (row.ownRect().height() > 0) {
                lane.separators.append(
                    {aCameraRect.left(), aCameraRect.right(), double(summary.top())}
                );
            }
            // The band names WHAT it is: the keys of the folder's CHILDREN. It is
            // an ordinary lane label, in the same centred column and at the same
            // size as a channel name. Paint order puts the key passes AFTER the
            // labels, so a marker draws IN FRONT of this label exactly as it does
            // in front of "Move" or "Scale" - the label is behind the marker.
            Label label;
            label.box = QRect(textLeft, summary.top(), kLabelWidth, summary.height());
            label.text = QCoreApplication::translate("TimeLine", "Children");
            label.ink = withAlpha(theme::Colors::current().textMuted, kLabelAlpha);
            lane.labels.append(label);
            lines.lanes.append(lane);
        }
    }

    // 1px borders: one hairline per seam (each ROW's top) plus the LAST ROW's bottom, so
    // adjacent lanes' outlines cannot stack into a 2px border. The two rules are stated
    // over the rows, not over the culled lanes: the last-row bottom belongs to the row the
    // view pushed last, and a culled last row takes that hairline with it (the old pass's
    // own rule - a lane set that stops short must not grow a bottom border).
    for (const ctrl::TimeLineRow& row : aRows) {
        if (row.rect.bottom() < aCullRect.top() || aCullRect.bottom() < row.rect.top())
            continue;
        lines.seams.append({aCameraRect.left(), aCameraRect.right(), row.rect.top()});
        // NO SEAM FOR THE OWN-LANE / SUMMARY-LANE DIVIDER. That divider is a TYPE separator and is
        // published on the summary LANE (see above) - a seam here painted the SOLID 70% line right
        // over the dashed one, because this pass runs LAST. A seam separates one ROW from the next
        // (different layers); the summary band is a band of this row, so it takes the grid of a
        // band. (Measured: with both, the divider read as a full line at every zoom.)
    }
    if (!aRows.isEmpty()) {
        const ctrl::TimeLineRow& last = aRows.back();
        if (!(last.rect.bottom() < aCullRect.top() || aCullRect.bottom() < last.rect.top()))
            lines.seams.append({aCameraRect.left(), aCameraRect.right(), last.rect.bottom()});
    }

    return lines;
}

KeyPass buildKeys(
    const core::ObjectNode* aNode, const TimeLineRow& aRow, const ctrl::time::Scaler& aScale,
    const util::Range& aRange, int aMargin, const QColor& aLaneInk
) {
    KeyPass pass;
    pass.edge = theme::Colors::current().outline;
    if (!aNode || !aNode->timeLine())
        return pass;

    const core::TimeLine& timeLine = *(aNode->timeLine());
    const int validNum = timeLine.validTypeCount();
    const int left = aMargin;
    const bool isSlimmed = aNode->isSlimmedDown();
    int validIndex = 0;

    for (int i = 0; i < core::TimeKeyType_TERM; ++i) {
        const core::TimeKeyType type = core::TimeLine::getTimeKeyTypeInOrderOfOperations(i);
        const core::TimeLine::MapType& map = timeLine.map(type);
        if (map.isEmpty())
            continue;

        const float height = aRow.keyHeight(validIndex, validNum);
        ++validIndex;

        auto itr = map.lowerBound(aRange.min());
        while (itr != map.end() && itr.key() <= aRange.max()) {
            const bool isFocused = itr.value()->isFocused();
            const bool isProportional = itr.value()->isProportional();
            const QColor hue = keyColor(itr.value()->type());
            // A focused key mixes its ink toward the text colour. A PROPORTIONAL key is
            // HOLLOW: its hue becomes the RING's ink (no alpha), with the hole published
            // below so both hosts stroke the same band.
            const QColor fill = isFocused ? mixTowardText(hue) : hue;

            const QPointF pos(left + aScale.attribute(itr.key()).pos + 0.5, height + 0.5f);
            KeyMark mark;
            mark.centre = pos;
            mark.fill = fill;
            if (isSlimmed) {
                mark.shape = KeyShape::Polygon;
                QPolygonF base;
                base << pos + QPointF(keyRadius * -1, keyRadiusSlim * -1)
                     << pos + QPointF(keyRadius, keyRadiusSlim * -1)
                     << pos + QPointF(keyRadius, keyRadiusSlim)
                     << pos + QPointF(keyRadius * -1, keyRadiusSlim);
                mark.polygon = roundedPolygon(base, kKeyCornerRadius);
            } else if (itr.value()->canHoldChild()) {
                mark.shape = KeyShape::Polygon;
                QPolygonF base;
                base << pos + QPointF(0.0, -kHolderArm) << pos + QPointF(kHolderArm, 0.0)
                     << pos + QPointF(0.0, kHolderArm) << pos + QPointF(-kHolderArm, 0.0);
                mark.polygon = roundedPolygon(base, kKeyCornerRadius);
            } else {
                mark.shape = KeyShape::Ellipse;
                mark.radiusX = keyRadius;
                mark.radiusY = keyRadius;
            }
            if (isProportional) {
                mark.hollow = true;
                mark.holeInk = aLaneInk;
                if (mark.shape == KeyShape::Ellipse) {
                    mark.hollowRadius = mark.radiusX - kProportionalHoleInset;
                } else {
                    mark.hollowPolygon =
                        insetPolygon(mark.polygon, mark.centre, kProportionalPolygonInset);
                    mark.hollowHolePolygon =
                        insetPolygon(mark.polygon, mark.centre, kProportionalPolygonHoleInset);
                }
            }
            if (isFocused) {
                mark.hasRing = true;
                mark.ringRadius = kSelectRingRadiusModel;
                mark.ring = withAlpha(hue, kSelectRingAlpha);
            }
            pass.marks.append(mark);
            ++itr;
        }
    }
    return pass;
}

KeyPass buildChildKeys(
    const core::ObjectNode* aNode, const QRect& aSummaryRect, int aMargin,
    const ctrl::time::Scaler& aScale, const util::Range& aRange
) {
    KeyPass pass;
    pass.edge = theme::Colors::current().outline;
    pass.child = true;
    pass.stroke = false;
    if (!aNode || aSummaryRect.height() <= 0)
        return pass;

    // ONE MARKER PER FRAME: several hidden channels landing on the same frame used to
    // overdraw the same triangle, which hid the multiplicity and made the antialiased
    // edges compound. The summary is an overview of WHEN, not of how many.
    std::set<int> frames;
    std::set<int> focusedFrames;
    for (auto child : aNode->children()) {
        core::ObjectNode::ConstIterator treeItr(child);
        while (treeItr.hasNext()) {
            const core::ObjectNode* node = treeItr.next();
            if (!node->timeLine())
                continue;
            const core::TimeLine& timeLine = *(node->timeLine());

            for (int i = 0; i < core::TimeKeyType_TERM; ++i) {
                const core::TimeKeyType type = core::TimeLine::getTimeKeyTypeInOrderOfOperations(i);
                const core::TimeLine::MapType& map = timeLine.map(type);

                auto itr = map.lowerBound(aRange.min());
                while (itr != map.end() && itr.key() <= aRange.max()) {
                    frames.insert(itr.key());
                    if (itr.value()->isFocused())
                        focusedFrames.insert(itr.key());
                    ++itr;
                }
            }
        }
    }

    const float y = aSummaryRect.top() + 0.5f * aSummaryRect.height();
    for (int frame : frames) {
        const QPointF centre(aMargin + aScale.attribute(frame).pos + 0.5, y);
        KeyMark mark;
        mark.shape = KeyShape::Polygon;
        mark.centre = centre;
        QPolygonF base;
        base << centre + QPointF(-4.2, -3.1) << centre + QPointF(4.2, -3.1)
             << centre + QPointF(0.0, 3.1);
        mark.polygon = roundedPolygon(base, kKeyCornerRadius);
        // A frame holding a selected hidden key brightens its summary mark, so a
        // click on the band has a visible answer even though the child lane is hidden.
        mark.fill = focusedFrames.count(frame) ? mixTowardText(theme::Colors::current().baseKey)
                                               : theme::Colors::current().baseKey;
        pass.marks.append(mark);
    }
    return pass;
}

void paintKeys(QPainter& aPainter, const KeyPass& aPass) {
    // The markers are round and their rings are 1px arcs: the pass owns its antialiasing hint
    // (the old renderKeys set it once for the whole pass), or a caller that just painted
    // hairlines leaves the rings faceted and the ring's coverage profile wrong.
    aPainter.setRenderHint(QPainter::Antialiasing);
    for (const KeyMark& mark : aPass.marks) {
        if (mark.hollow) {
            // A PROPORTIONAL KEY: a filled hue disc, the LANE's ink over its centre,
            // then the ordinary edge. Fills (not strokes) are what keep this pass and
            // the Canvas one byte-identical.
            aPainter.setPen(Qt::NoPen);
            aPainter.setBrush(mark.fill);
            if (mark.shape == KeyShape::Ellipse) {
                aPainter.drawEllipse(mark.centre, mark.radiusX - kProportionalRingOuterInset,
                                     mark.radiusY - kProportionalRingOuterInset);
            } else {
                aPainter.drawConvexPolygon(mark.hollowPolygon);
            }
            aPainter.setBrush(mark.holeInk);
            if (mark.shape == KeyShape::Ellipse) {
                aPainter.drawEllipse(mark.centre, mark.hollowRadius, mark.hollowRadius);
            } else {
                aPainter.drawConvexPolygon(mark.hollowHolePolygon);
            }
            aPainter.setPen(aPass.stroke ? QPen(aPass.edge, 1) : QPen(Qt::NoPen));
            aPainter.setBrush(Qt::NoBrush);
            if (mark.shape == KeyShape::Ellipse)
                aPainter.drawEllipse(mark.centre, mark.radiusX, mark.radiusY);
            else
                aPainter.drawConvexPolygon(mark.polygon);
        } else {
            aPainter.setPen(aPass.stroke ? QPen(aPass.edge, 1) : QPen(Qt::NoPen));
            aPainter.setBrush(mark.fill);
            if (mark.shape == KeyShape::Ellipse)
                aPainter.drawEllipse(mark.centre, mark.radiusX, mark.radiusY);
            else
                aPainter.drawConvexPolygon(mark.polygon);
        }
        if (mark.hasRing) {
            aPainter.setPen(QPen(mark.ring, 1));
            aPainter.setBrush(Qt::NoBrush);
            aPainter.drawEllipse(mark.centre, mark.ringRadius, mark.ringRadius);
        }
    }
}

QVector<RangeMarker> buildRangeMarkers(
    const QRect& aCameraRect, const util::Range& aProjectRange, bool aPlayWindowActive,
    const ctrl::time::Scaler& aScale, int aMargin
) {
    QVector<RangeMarker> markers;
    const int top = aCameraRect.top() + ctrl::TimeLineEditor::kHeaderHeight;
    const int x0 = aMargin + aScale.pixelWidth(aProjectRange.min());
    const int x1 = aMargin + aScale.pixelWidth(aProjectRange.max());
    // A degenerate range collapses both markers onto one column: skip the double draw.
    if (x0 == x1)
        return markers;
    // The section-separator token at 40% so the guides recede behind the lanes; while a
    // playback window overrides the range they drop to 25% (the window's brackets carry the
    // boundary reading then).
    const int alpha = aPlayWindowActive ? 64 : 102;
    const QColor ink = withAlpha(theme::Colors::current().hairline, alpha);
    markers.append({x0, top, aCameraRect.bottom(), ink});
    markers.append({x1, top, aCameraRect.bottom(), ink});
    return markers;
}

SelectionBox buildMarquee(const QRect& aRect) {
    SelectionBox marquee;
    if (aRect.width() < 2 || aRect.height() < 2)
        return marquee;
    marquee.visible = true;
    // The RAW gesture rect; gui::ink::paintMarquee applies the half-pixel inset that
    // puts the 1px stroke on one device column (the graph panel uses the same helper).
    marquee.rect = QRectF(aRect);
    marquee.edge = withAlpha(theme::Colors::current().accentBright, 230);
    marquee.fill = withAlpha(theme::Colors::current().accentBright, 24);
    return marquee;
}

void paintGuides(QPainter& aPainter, const QVector<RangeMarker>& aMarkers, const SelectionBox& aMarquee) {
    if (!aMarkers.isEmpty()) {
        aPainter.setRenderHint(QPainter::Antialiasing, false);
        aPainter.setBrush(Qt::NoBrush);
        for (const RangeMarker& marker : aMarkers) {
            aPainter.setPen(QPen(marker.ink, 1));
            aPainter.drawLine(marker.x, marker.top, marker.x, marker.bottom);
        }
    }
    if (aMarquee.visible) {
        // ONE BOX TREATMENT for both views: rounded, accent wash, 1px SOLID edge.
        // The old dashed edge made the timeline's box a different gesture from the
        // graph panel's solid one; the fill carries the selection, not a dash pattern.
        aPainter.setRenderHint(QPainter::Antialiasing, true);
        aPainter.setPen(QPen(aMarquee.edge, 1));
        aPainter.setBrush(aMarquee.fill);
        aPainter.drawRoundedRect(
            aMarquee.rect.adjusted(0.5, 0.5, -0.5, -0.5), aMarquee.radius, aMarquee.radius
        );
    }
}

BodyScene buildBodyScene(
    const QVector<TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect,
    const ctrl::time::Scaler& aScale, const util::Range& aRange, const util::Range& aProjectRange,
    bool aPlayWindowActive, const QRect& aMarqueeRect, int aMargin, const Inks& aInks
) {
    BodyScene scene;
    scene.lines = buildLines(aRows, aCameraRect, aCullRect, aInks);
    scene.markers = buildRangeMarkers(aCameraRect, aProjectRange, aPlayWindowActive, aScale, aMargin);
    scene.selection = buildMarquee(aMarqueeRect);
    for (const TimeLineRow& row : aRows) {
        const QRect rect = row.rect;
        if (rect.bottom() < aCullRect.top() || aCullRect.bottom() < rect.top())
            continue;
        if (!row.node || !row.node->timeLine())
            continue;
        // A hollow key's hole is the LANE's own fill, so the ring reads on the
        // selected and the plain lane alike (the body model already knows both inks).
        const QColor laneInk = opaqueOver(
            row.selecting ? aInks.bodySelected : aInks.body, theme::Colors::current().base
        );
        if (row.summaryHeight() > 0)
            scene.keyPasses.append(
                buildChildKeys(row.node, row.summaryRect(), aMargin, aScale, aRange)
            );
        scene.keyPasses.append(buildKeys(row.node, row, aScale, aRange, aMargin, laneInk));
    }
    return scene;
}

void paintBodyScene(QPainter& aPainter, const BodyScene& aScene) {
    paintLines(aPainter, aScene.lines);
    paintGuides(aPainter, aScene.markers, SelectionBox());
    for (const KeyPass& pass : aScene.keyPasses)
        paintKeys(aPainter, pass);
    paintGuides(aPainter, {}, aScene.selection);
}

void paintLines(QPainter& aPainter, const Lines& aLines) {
    aPainter.setRenderHint(QPainter::Antialiasing);

    const QBrush body(aLines.inks.body);
    const QBrush bodySelected(aLines.inks.bodySelected);
    const QBrush separator(aLines.inks.separator);

    for (const Lane& lane : aLines.lanes) {
        // fill only; the 1px border is the separate seam pass below. A summary
        // band is the same surface as any lane: its 1px divider and its marker
        // shape are the distinction.
        aPainter.setPen(Qt::NoPen);
        aPainter.setBrush(lane.selected ? bodySelected : body);
        aPainter.drawRect(lane.rect);

        if (!lane.separators.isEmpty()) {
            // TYPE separators inside one lane: dashed 4-on/4-off at the lane's 40%
            // separator ink. The dash is the visual difference from the SOLID 70%
            // row seams (different layers); the longer dash than the old dots keeps
            // it legible at 1x. Snapped to its row with AA off so it is one crisp row.
            aPainter.setRenderHint(QPainter::Antialiasing, false);
            QPen pen(separator, 1, Qt::CustomDashLine);
            pen.setDashPattern(QVector<qreal>{4.0, 4.0});
            aPainter.setPen(pen);
            for (const Separator& each : lane.separators)
                aPainter.drawLine(each.x0, int(each.y), each.x1, int(each.y));
            aPainter.setRenderHint(QPainter::Antialiasing, true);
        }

        for (const Label& label : lane.labels) {
            aPainter.setPen(QPen(label.ink, 1));
            aPainter.drawText(label.box, label.text, QTextOption(Qt::AlignCenter));
        }
    }

    aPainter.setRenderHint(QPainter::Antialiasing, false);
    aPainter.setPen(QPen(aLines.inks.edge, 1));
    aPainter.setBrush(Qt::NoBrush);
    for (const Seam& seam : aLines.seams)
        aPainter.drawLine(seam.x0, seam.y, seam.x1, seam.y);
    aPainter.setRenderHint(QPainter::Antialiasing);
}

} // namespace time
} // namespace ctrl
