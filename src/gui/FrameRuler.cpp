#include "gui/FrameRuler.h"

#include <QFontMetrics>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "core/TimeFormat.h"

#include "ctrl/TimeLineEditor.h"
#include "gui/PillInk.h"

// The metrics that are also the timeline editor's public constants are pinned to them
// here, so the ruler's band, number row and pill cannot drift from the widget that
// hosts them (the graph panel's scaler reserves the same band, and asserts it there).
static_assert(gui::frameRuler::kBandHeight == ctrl::TimeLineEditor::kHeaderHeight,
              "the ruler's band must be the timeline's kHeaderHeight");
static_assert(gui::frameRuler::kNumberTop == ctrl::TimeLineEditor::kNumberTop,
              "the ruler's number row must start where the timeline's does");
static_assert(gui::frameRuler::kNumberHeight == ctrl::TimeLineEditor::kNumberHeight,
              "the ruler's number row must be the timeline's kNumberHeight");
static_assert(gui::frameRuler::kPillPadding == ctrl::TimeLineEditor::kPillPadding,
              "the ruler's pill must use the timeline's padding");
static_assert(gui::frameRuler::kPillRadius == ctrl::TimeLineEditor::kPillRadius,
              "the ruler's pill must use the timeline's radius");

namespace gui {
namespace frameRuler {

using TickLevel = ctrl::time::Scaler::TickLevel;

qreal tickAlpha(int aLevel) {
    switch (static_cast<TickLevel>(aLevel)) {
        case TickLevel::Major:        return 0.85;
        case TickLevel::Intermediate: return 0.60;
        case TickLevel::Minor:        return 0.20;
        case TickLevel::None:         return 0.0;
    }
    return 0.0;
}

namespace {

// The box a boundary label reserves, so a ladder number that would enter it yields.
// Symmetric: a label yields only when it actually enters the span, on either side.
struct Span {
    int left = 0;
    int right = 0;
};

bool collides(const Label& aLabel, const Span& aSpan) {
    return aLabel.left < aSpan.right + kNumberMargin &&
        aLabel.left + aLabel.width > aSpan.left - kNumberMargin;
}

// Two published labels never come closer than kNumberMargin, or they read as one
// number. Measured on the BOXES AS PUBLISHED, which is why this runs on the label list
// and not on the reserved boundary spans alone.
bool touches(const Label& aLabel, const Label& aOther) {
    return aLabel.left < aOther.left + aOther.width + kNumberMargin &&
        aOther.left < aLabel.left + aLabel.width + kNumberMargin;
}

} // namespace

Model build(const Inputs& aInputs, const Axis& aAxis) {
    Model model;
    model.band = aInputs.band;
    if (aInputs.band.isEmpty())
        return model;

    const double ppf = aAxis.pixelsPerFrame;
    const auto pixelOf = [&aAxis](int aFrame) { return int(std::lround(aAxis.pixelOfFrame(aFrame))); };
    const auto levelOf = [&aAxis, ppf](int aFrame) { return int(aAxis.attributeAt(aFrame, ppf).level); };
    const auto numberText = [&aInputs](int aFrame) {
        return core::TimeFormat(aInputs.frameRange, aInputs.fps).frameToString(aFrame, aInputs.formatType);
    };

    // THE PLAYHEAD'S PILL, before the labels, because its width is what suppresses
    // them. The host measured the text (QFontMetrics vs the ImGui atlas), so the span
    // is the pill's own geometry rather than a guessed number.
    if (aInputs.hasPlayhead && !aInputs.playheadText.isEmpty() && aInputs.playheadTextWidth > 0) {
        const int width = aInputs.playheadTextWidth + 2 * kPillPadding;
        const int centre = pixelOf(aInputs.playheadFrame);
        model.playhead.rect = QRect(centre - width / 2, aInputs.band.top() + kPillTop, width, kNumberHeight);
        model.playhead.text = aInputs.playheadText;
        model.playheadLineTop = aInputs.band.top() + kLineTop;
    }
    const int pillHalf = model.playhead.rect.width() / 2;
    const auto underPill = [&model, pillHalf](int aCentre) {
        return pillHalf > 0 && qAbs(aCentre - model.playhead.rect.center().x()) < pillHalf;
    };

    // THE BOUNDARY FRAMES: the project's own first and last frames, in view, own their
    // slots. Off-major boundaries get a Major tick and a number; a band too narrow for
    // both labels collapses them into one "first-last" label, and the ladder's numbered
    // neighbours yield to whichever slot the boundary block reserved. A one-frame range
    // has no ends to mark, so it publishes neither label (the ladder still ticks and
    // numbers that frame if its own tier says so).
    const int firstBoundary = aInputs.frameRange.min();
    const int lastBoundary = aInputs.frameRange.max();
    const bool hasRange = aInputs.hasFrameRange;
    // A one-frame range has no ENDS to label, but its single frame is still the project's
    // frame and must be MARKED: the ladder may classify it as None (nothing drawn at all)
    // or as a minor tick, and the old ruler drew a Major mark for it in both cases.
    const bool degenerate = hasRange && firstBoundary == lastBoundary;
    const int firstX = hasRange ? pixelOf(firstBoundary) : -1;
    const int lastX = hasRange ? pixelOf(lastBoundary) : -1;
    const auto inBand = [&aInputs](int aX) {
        return aInputs.band.left() <= aX && aX <= aInputs.band.right();
    };
    const bool firstInView = hasRange && inBand(firstX);
    const bool lastInView = hasRange && inBand(lastX);
    const bool firstActive = firstInView && levelOf(firstBoundary) != int(TickLevel::Major);
    const bool lastActive = lastInView && levelOf(lastBoundary) != int(TickLevel::Major);
    const QString firstText = firstInView ? numberText(firstBoundary) : QString();
    const QString lastText = lastInView ? numberText(lastBoundary) : QString();
    const int firstWidth = kNumberCharWidth * int(firstText.size());
    const int lastWidth = kNumberCharWidth * int(lastText.size());
    const bool merged = !degenerate && firstInView && lastInView &&
        (firstX + (firstWidth >> 1) + kNumberMargin >= lastX - (lastWidth >> 1));
    const QString mergedText = merged ? firstText + QString::fromUtf8("\u2013") + lastText : QString();
    const int mergedWidth = kNumberCharWidth * int(mergedText.size());
    const int mergedCentre = merged ? (firstX + lastX) / 2 : 0;

    // A boundary label the ladder would also draw is replaced by the boundary block's
    // own Major tick (the ladder's tick at that frame is skipped, so the two do not
    // stack). An on-major boundary keeps the ladder's tick when the labels are not
    // merged.
    // The LABELS are suppressed for a degenerate range (signed off: "7-7" is noise), the
    // TICKS are not.
    const bool drawFirstTick = degenerate ? firstInView : (firstInView && (merged || firstActive));
    const bool drawLastTick = degenerate ? false : (lastInView && (merged || lastActive));
    const auto isBoundaryFrame = [&](int aFrame) {
        return (aFrame == firstBoundary && drawFirstTick) || (aFrame == lastBoundary && drawLastTick);
    };

    // THE TICKS: the ladder's classification at the host's rate, enumerated in strides
    // of its finest drawn level (a deep zoom-out can span millions of frames for a
    // handful of ticks). Positions are filtered to the band and deduplicated by column
    // - a zoomed-out window can round two frames onto one pixel, and the more
    // prominent tier wins the column.
    const int stride = std::max(1, aAxis.tickStride);
    int frame = aInputs.visibleFrames.min();
    if (const int rem = frame % stride)
        frame += rem > 0 ? stride - rem : -rem;
    for (; frame <= aInputs.visibleFrames.max(); frame += stride) {
        if (isBoundaryFrame(frame))
            continue;
        const int level = levelOf(frame);
        if (level == int(TickLevel::None))
            continue;
        const int x = pixelOf(frame);
        if (!inBand(x))
            continue;
        model.ticks.append({x, level, aInputs.activeRange.contains(frame)});
    }
    if (drawFirstTick)
        model.ticks.append(
            {firstX, int(TickLevel::Major), aInputs.activeRange.contains(firstBoundary), true}
        );
    if (drawLastTick)
        model.ticks.append({lastX, int(TickLevel::Major), aInputs.activeRange.contains(lastBoundary), true});
    // Ascending, one tick per column: the boundary mark first (it is the project's edge
    // and carries its own in-range classification), then the more prominent tier. Without
    // the boundary-first key a ladder tick that rounds onto the same column could take the
    // edge's place - and a dim ladder tick then stood where the edge's Major mark belongs.
    std::sort(model.ticks.begin(), model.ticks.end(), [](const Tick& aLhs, const Tick& aRhs) {
        if (aLhs.x != aRhs.x)
            return aLhs.x < aRhs.x;
        if (aLhs.boundary != aRhs.boundary)
            return aLhs.boundary;
        return aLhs.level > aRhs.level;
    });
    for (int i = 0; i < model.ticks.size();) {
        if (0 < i && model.ticks.at(i).x == model.ticks.at(i - 1).x)
            model.ticks.removeAt(i);
        else
            ++i;
    }

    // THE LABELS. A number's box is centred on its tick and NOT moved: the host clips it
    // at its own edge, so a number that leaves the view is cut rather than sliding away
    // from the frame it names. A number wider than the whole band is dropped - it could
    // not be read at any position.
    const auto labelFor = [&aInputs](int aCentre, int aWidth, const QString& aText) {
        return Label{aCentre - (aWidth >> 1), aInputs.labelTop, aWidth, aText};
    };

    // The boundary labels are accepted FIRST, so the forced first/last number always
    // wins its slot.
    // A number is never dropped for being WIDER than the band: the host clips the
    // overhang, exactly as the timeline always did (the panel used to drop such a label
    // because it clamped boxes into its band; with the clamp gone there is nothing to
    // protect, so both hosts clip and the rule is one rule).
    const auto accept = [&model](Label aLabel) {
        for (const Label& other : model.labels) {
            if (touches(aLabel, other))
                return false;
        }
        model.labels.append(aLabel);
        return true;
    };
    if (degenerate) {
        // No boundary label: there are no ends. (The tick above still marks the frame.)
    } else if (merged) {
        // Inside the active range only when BOTH of its frames are.
        if (!underPill(mergedCentre)) {
            Label label = labelFor(mergedCentre, mergedWidth, mergedText);
            label.frame = firstBoundary;
            label.endFrame = lastBoundary;
            label.boundary = true;
            label.inActiveRange =
                aInputs.activeRange.contains(firstBoundary) && aInputs.activeRange.contains(lastBoundary);
            accept(label);
        }
    } else {
        if (firstActive && !underPill(firstX)) {
            Label label = labelFor(firstX, firstWidth, firstText);
            label.frame = firstBoundary;
            label.boundary = true;
            label.inActiveRange = aInputs.activeRange.contains(firstBoundary);
            accept(label);
        }
        if (lastActive && !underPill(lastX)) {
            Label label = labelFor(lastX, lastWidth, lastText);
            label.frame = lastBoundary;
            label.boundary = true;
            label.inActiveRange = aInputs.activeRange.contains(lastBoundary);
            accept(label);
        }
    }

    // The spans the boundary labels reserve, so a ladder number that would enter one
    // yields (the same margin rule as a published label, applied to the slot the
    // boundary owns even when the pill suppressed its text).
    QVector<Span> reserved;
    if (merged) {
        reserved.append({mergedCentre - (mergedWidth >> 1), mergedCentre + (mergedWidth >> 1)});
    } else {
        if (firstActive)
            reserved.append({firstX - (firstWidth >> 1), firstX + (firstWidth >> 1)});
        if (lastActive)
            reserved.append({lastX - (lastWidth >> 1), lastX + (lastWidth >> 1)});
    }

    // THE LADDER'S NUMBERS. Walked in frame order from the same enumeration the ticks
    // came from, so a number belongs to a frame the ladder ticked.
    frame = aInputs.visibleFrames.min();
    if (const int rem = frame % stride)
        frame += rem > 0 ? stride - rem : -rem;
    for (; frame <= aInputs.visibleFrames.max(); frame += stride) {
        const ctrl::time::Scaler::Attribute attribute = aAxis.attributeAt(frame, ppf);
        if (attribute.level == TickLevel::None || !attribute.showNumber)
            continue;
        if (frame == firstBoundary && (merged || firstActive))
            continue; // the boundary block writes this one
        if (frame == lastBoundary && (merged || lastActive))
            continue;
        const int x = pixelOf(frame);
        // CULLED BY ITS BOX, NOT BY ITS TICK. A number whose tick is a few pixels outside the
        // band still has visible glyphs inside it, and the old ruler drew that tail (its
        // frame window ran a few pixels past the strip on purpose). Only a box that misses
        // the band entirely is dropped; the host clips what hangs over the edge.
        if (x + (kNumberCharWidth * int(numberText(frame).size()) >> 1) < aInputs.band.left() ||
            x - (kNumberCharWidth * int(numberText(frame).size()) >> 1) > aInputs.band.right())
            continue;
        if (underPill(x))
            continue; // the pill owns its column
        const QString text = numberText(frame);
        const int width = kNumberCharWidth * int(text.size());
        Label label = labelFor(x, width, text);
        label.frame = frame;
        label.inActiveRange = aInputs.activeRange.contains(frame);
        bool yields = false;
        for (const Span& span : reserved) {
            if (collides(label, span)) {
                yields = true;
                break;
            }
        }
        if (yields)
            continue;
        accept(label);
    }

    std::sort(model.labels.begin(), model.labels.end(), [](const Label& aLhs, const Label& aRhs) {
        return aLhs.left < aRhs.left;
    });

    // THE TEMPORARY PLAYBACK WINDOW'S OVERLAY: the wash inside the visible columns, the
    // two 2px bracket stems from the pill row to the band's bottom, and the label arms
    // at the stems' inner faces - collapsed into ONE centred "start-end" pill when the
    // two would touch. The end bracket lands on frame `end`'s tick: the window is the
    // inclusive [start, end], so it spans [tick(start), tick(end)].
    if (aInputs.windowStart >= 0 && aInputs.windowEnd >= 0 && aInputs.windowEnd > aInputs.windowStart) {
        const int x0 = pixelOf(aInputs.windowStart);
        const int x1 = pixelOf(aInputs.windowEnd);
        if (x1 > x0) {
            PlayWindowOverlay& out = model.window;
            out.active = true;
            const int top = aInputs.band.top();
            const int pillTop = top + kPillTop;
            const int stemBottom = aInputs.band.bottom() + 1;
            const int c0 = std::max(x0, aInputs.visible.left());
            const int c1 = std::min(x1 + 1, aInputs.visible.right() + 1);
            if (c1 > c0)
                out.band = QRect(c0, top, c1 - c0, kBandHeight);
            out.startStem = QRect(x0, pillTop, 2, stemBottom - pillTop);
            out.endStem = QRect(x1 - 1, pillTop, 2, stemBottom - pillTop);
            const int wL = aInputs.windowStartTextWidth + 2 * kPillPadding;
            const int wR = aInputs.windowEndTextWidth + 2 * kPillPadding;
            const QRect pillL(x0 + 2, pillTop, wL, kNumberHeight);
            const QRect pillR(x1 - 1 - wR, pillTop, wR, kNumberHeight);
            if (pillL.right() >= pillR.left()) {
                const int width = aInputs.windowMergedTextWidth + 2 * kPillPadding;
                out.merged.rect = QRect((x0 + x1) / 2 - width / 2, pillTop, width, kNumberHeight);
                out.merged.text = aInputs.windowStartText + QString::fromUtf8("\u2013") + aInputs.windowEndText;
            } else {
                out.start.rect = pillL;
                out.start.text = aInputs.windowStartText;
                out.end.rect = pillR;
                out.end.text = aInputs.windowEndText;
            }
        }
    }

    return model;
}

void paintRuler(QPainter& aPainter, const Model& aModel, const PaintInk& aInk) {
    if (aModel.band.isEmpty())
        return;

    aPainter.save();
    aPainter.setRenderHint(QPainter::Antialiasing, false);

    // Ticks: SHORT marks only, one length for every tier, prominence by alpha. A tick
    // outside the active playback range recedes by the shared factor.
    const int base = aModel.band.bottom();
    for (const Tick& tick : aModel.ticks) {
        QColor colour = aInk.content;
        colour.setAlphaF(tickAlpha(tick.level) * (tick.inActiveRange ? 1.0 : kOutsideRangeFactor));
        aPainter.setPen(QPen(colour, 1));
        aPainter.drawLine(tick.x, base, tick.x, base - kTickLength);
    }

    // Numbers: the box is the tick's centre and the ladder's 6px-per-character reserve,
    // in the ruler's own inclusive rect convention (the one the timeline has always
    // used; the panel forms its own from left/top/width).
    for (const Label& label : aModel.labels) {
        QColor colour = aInk.content;
        if (!label.inActiveRange)
            colour.setAlphaF(kOutsideRangeFactor);
        aPainter.setPen(QPen(colour, 1));
        const QRect rect(
            QPoint(label.left, label.top), QPoint(label.left + label.width + 1, label.top + kNumberHeight)
        );
        aPainter.drawText(rect, label.text);
    }

    // The temporary playback window: the wash, then the stems, then the pills (a merged
    // pill wider than the window must not hide the stems' lower half).
    const PlayWindowOverlay& window = aModel.window;
    if (window.active) {
        if (!window.band.isEmpty()) {
            aPainter.setPen(Qt::NoPen);
            aPainter.setBrush(aInk.windowBand);
            aPainter.drawRect(window.band);
        }
        aPainter.setPen(Qt::NoPen);
        aPainter.setBrush(aInk.windowEdge);
        if (!window.startStem.isEmpty())
            aPainter.drawRect(window.startStem);
        if (!window.endStem.isEmpty())
            aPainter.drawRect(window.endStem);
        if (window.merged.isValid()) {
            paintPill(aPainter, window.merged.rect, aInk.windowEdge, kPillRadius);
            paintPillText(aPainter, window.merged.rect, window.merged.text, aInk.windowText, aModel.band.top());
        } else {
            if (window.start.isValid()) {
                paintPill(aPainter, window.start.rect, aInk.windowEdge, kPillRadius);
                // Square the face against the stem (the pill's left side), so the
                // bracket joint reads continuous.
                aPainter.fillRect(QRect(window.start.rect.x(), window.start.rect.y(), kPillRadius, kPillRadius),
                                  aInk.windowEdge);
                aPainter.fillRect(QRect(window.start.rect.x(), window.start.rect.bottom() - kPillRadius + 1,
                                        kPillRadius, kPillRadius),
                                  aInk.windowEdge);
                paintPillText(aPainter, window.start.rect, window.start.text, aInk.windowText, aModel.band.top());
            }
            if (window.end.isValid()) {
                paintPill(aPainter, window.end.rect, aInk.windowEdge, kPillRadius);
                aPainter.fillRect(QRect(window.end.rect.right() - kPillRadius + 1, window.end.rect.y(), kPillRadius,
                                        kPillRadius),
                                  aInk.windowEdge);
                aPainter.fillRect(QRect(window.end.rect.right() - kPillRadius + 1,
                                        window.end.rect.bottom() - kPillRadius + 1, kPillRadius, kPillRadius),
                                  aInk.windowEdge);
                paintPillText(aPainter, window.end.rect, window.end.text, aInk.windowText, aModel.band.top());
            }
        }
    }

    // THE PLAYHEAD'S PILL, LAST: it sits on the strip and over the temporary window's
    // band, and the model already reserved its column (no number is drawn under it).
    if (aModel.playhead.isValid()) {
        paintPill(aPainter, aModel.playhead.rect, aInk.playheadFill, kPillRadius);
        paintPillText(aPainter, aModel.playhead.rect, aModel.playhead.text, aInk.playheadText, aModel.band.top());
    }

    aPainter.restore();
}

void paintPillText(QPainter& aPainter, const QRect& aPill, const QString& aText, const QColor& aInk, int aTop) {
    // THE PILL'S TEXT COLUMN, stated as the integer it is: the pill is measured as
    // `advance + 2 * kPillPadding`, so `left + kPillPadding` IS the centred column for
    // an even and an odd text width alike. Writing it that way lets the panel reproduce
    // the same column without QFontMetrics at draw time.
    const QFontMetrics fm(aPainter.font());
    const int textWidth = fm.horizontalAdvance(aText);
    const QRect rect(
        aPill.left() + kPillPadding, aTop + kNumberTop, textWidth, kNumberHeight
    );
    aPainter.setPen(QPen(aInk, 1));
    aPainter.drawText(rect, aText);
}

} // namespace frameRuler
} // namespace gui
