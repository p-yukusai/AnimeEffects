#include "ctrl/time/time_Renderer.h"
#include "ctrl/TimeLineEditor.h"
#include "gui/theme/Colors.h"

using namespace core;

namespace ctrl {
namespace time {

    static QColor withAlpha(QColor aColor, int aAlpha) { aColor.setAlpha(aAlpha); return aColor; }

    Renderer::Renderer(QPainter& aPainter, const CameraInfo& aCamera, const theme::TimeLine& aTheme):
        mPainter(aPainter), mCamera(aCamera), mTheme(aTheme), mMargin(), mRange(), mScale() {}

    void Renderer::renderLines(const QVector<TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect) {
        // draw each line
        mPainter.setRenderHint(QPainter::Antialiasing);

        const QBrush kBrushBody(mTheme.trackColor());
        const QBrush kBrushBodySelect(mTheme.trackSelectColor());
        const QBrush kBrushEdge(mTheme.trackEdgeColor());
        const QBrush kBrushSepa(mTheme.trackSeperatorColor());
        const QBrush kBrushText(mTheme.trackTextColor());
        const int textWidth = 100;
        const int textLeft = aCameraRect.center().x() - textWidth / 2;

        for (const TimeLineRow& row : aRows) {
            const QRect rect = row.rect;

            // Rows are culled by their vertical band only: the horizontal
            // view extent is never bounded by the frame range, so lanes stay
            // visible (fills, keys and all) even when the view floats
            // entirely outside it.
            if (rect.bottom() < aCullRect.top() || aCullRect.bottom() < rect.top())
                continue;

            // The lane fills, separators and hairlines span the visible width
            // (the camera rect), not just the frame range: when the content is
            // narrower than the viewport, the lanes reach the panel edges.
            // There is no performance reason to bound them to the content.
            const QRect band(QPoint(aCameraRect.left(), rect.top()), QPoint(aCameraRect.right(), rect.bottom()));

            // draw line (fill only; the 1px border is a separate hairline
            // pass below, one per seam)
            mPainter.setPen(Qt::NoPen);
            mPainter.setBrush(row.selecting ? kBrushBodySelect : kBrushBody);
            mPainter.drawRect(band);

            if (!row.node || !row.node->timeLine())
                continue;

            if (!row.node->isSlimmedDown()) {
                // draw separators
                mPainter.setPen(QPen(kBrushSepa, 1, Qt::DotLine));
                const int sepa = row.node->timeLine()->validTypeCount();
                for (int i = 1; i < sepa; ++i) {
                    const float h = static_cast<float>(rect.height()) / sepa;
                    const float y = rect.top() + i * h;
                    const QPointF v0(band.left(), y);
                    const QPointF v1(band.right(), y);
                    mPainter.drawLine(v0, v1);
                }

                // draw type labels
                mPainter.setPen(QPen(kBrushText, 1));
                int i = 0;
                for (int typei = 0; typei < TimeKeyType_TERM; ++typei) {
                    auto type = TimeLine::getTimeKeyTypeInOrderOfOperations(typei);
                    if (row.node->timeLine()->isEmpty(type)) {
                        continue;
                    }

                    const float h = static_cast<float>(rect.height()) / sepa;
                    mPainter.drawText(
                        QRect(textLeft, rect.top() + static_cast<int>(i * h), textWidth, static_cast<int>(h)),
                        TimeLine::getTimeKeyName(type),
                        QTextOption(Qt::AlignCenter)
                    );
                    ++i;
                }
            }

        }

        // 1px borders: one hairline per seam (each row's top) plus the last
        // row's bottom, so adjacent rows' 1px outlines can't stack into a 2px
        // border. AA off keeps each line exactly one pixel. The hairlines span
        // the visible width like the fills above.
        mPainter.setRenderHint(QPainter::Antialiasing, false);
        mPainter.setPen(QPen(kBrushEdge, 1));
        mPainter.setBrush(Qt::NoBrush);
        for (const TimeLineRow& row : aRows) {
            if (row.rect.bottom() < aCullRect.top() || aCullRect.bottom() < row.rect.top()) continue;
            mPainter.drawLine(aCameraRect.left(), row.rect.top(), aCameraRect.right(), row.rect.top());
        }
        if (!aRows.isEmpty()) {
            const TimeLineRow& last = aRows.back();
            if (!(last.rect.bottom() < aCullRect.top() || aCullRect.bottom() < last.rect.top())) {
                mPainter.drawLine(aCameraRect.left(), last.rect.bottom(), aCameraRect.right(), last.rect.bottom());
            }
        }
        mPainter.setRenderHint(QPainter::Antialiasing);
    }

    void Renderer::renderRangeMarkers(const QRect& aCameraRect) {
        // Vertical hairlines at the frame range's first and last frame:
        // continue the ruler's boundary ticks down through the track area.
        // The ruler already marks the range ends (the first frame's tick, and
        // the specially-labeled last frame's tick), so the lines start where
        // those ticks end — at the ruler's bottom hairline — and run to the
        // bottom of the viewport.
        const int top = aCameraRect.top() + ctrl::TimeLineEditor::kHeaderHeight;
        const int x0 = mMargin + mScale->pixelWidth(mProjectRange.min());
        const int x1 = mMargin + mScale->pixelWidth(mProjectRange.max());
        // A degenerate range (min == max) collapses both markers onto one
        // pixel; skip the double draw.
        if (x0 == x1)
            return;

        mPainter.setRenderHint(QPainter::Antialiasing, false);
        // The design system's 1px section-separator token — the same line
        // family as the ruler hairline the markers start from and the row
        // seams they cross — at 40% opacity so the guides recede behind the
        // track content. While a temporary playback window overrides the
        // range, the markers drop to 25%: they no longer gate playback, and
        // the window's own brackets carry the boundary reading.
        const int markerAlpha = mPlayWindow.active() ? 64 : 102;
        mPainter.setPen(QPen(withAlpha(theme::Colors::current().hairline, markerAlpha), 1));
        mPainter.setBrush(Qt::NoBrush);
        mPainter.drawLine(x0, top, x0, aCameraRect.bottom());
        mPainter.drawLine(x1, top, x1, aCameraRect.bottom());
    }

    void Renderer::renderKeys(const QVector<TimeLineRow>& aRows, const QRect& aCullRect) {
        mPainter.setRenderHint(QPainter::Antialiasing);
        for (const TimeLineRow& row : aRows) {
            const QRect rect = row.rect;
            // same vertical-band culling as the background pass above
            if (rect.bottom() < aCullRect.top() || aCullRect.bottom() < rect.top())
                continue;
            if (!row.node || !row.node->timeLine())
                continue;

            const QPoint cpos(mMargin, rect.bottom());
            if (row.closedFolder) {
                drawChildKeys(row.node, cpos);
            }
            drawKeys(row.node, row);
        }
    }

    void Renderer::renderHandle(const QPoint& aPoint, int aRange) {
        const QPoint pos = aPoint + QPoint(0, -static_cast<int>(mCamera.leftTopPos().y()));
        const int range = aRange;

        const QBrush kBrushBody(withAlpha(theme::Colors::current().text, 180));
        const QBrush kBrushEdge(withAlpha(theme::Colors::current().textMuted, 180));

        mPainter.setPen(QPen(kBrushEdge, 1));
        mPainter.setBrush(kBrushBody);
        mPainter.drawLine(pos + QPoint(0, range), pos + QPoint(0, mCamera.screenHeight()));

        mPainter.setRenderHint(QPainter::Antialiasing);
        mPainter.drawEllipse(pos, range, range);
    }

    void Renderer::renderSelectionRange(const QRect& aRect) {
        if (aRect.width() >= 2 && aRect.height() >= 2) {
            // brand-hued selection box: the marching-ants edge is the accent
            // color, the fill the same color at lower opacity; corners are
            // slightly rounded (6px), which needs AA so the curve reads smooth
            mPainter.setRenderHint(QPainter::Antialiasing, true);
            const QBrush kSelectEdge(withAlpha(theme::Colors::current().accentBright, 230));
            const QBrush kSelectBody(withAlpha(theme::Colors::current().accentBright, 24));
            mPainter.setPen(QPen(kSelectEdge, 1, Qt::DashLine));
            mPainter.setBrush(kSelectBody);
            // Inset by half a pixel so the 1px pen is centered inside the
            // rect rather than straddling its edge; the AA'd edge is
            // intentionally soft to keep the rounded corners smooth.
            mPainter.drawRoundedRect(QRectF(aRect).adjusted(0.5, 0.5, -0.5, -0.5), 6, 6);
        }
    }

    void Renderer::drawKeys(const ObjectNode* aNode, const TimeLineRow& aRow) {
        const QBrush kBrushKeyEdge(theme::Colors::current().outline);
        QPointF holder[4] = {QPointF(0.0, -4.2), QPointF(4.2, 0.0), QPointF(0.0, 4.2), QPointF(-4.2, 0.0)};

        if (aNode && aNode->timeLine()) {
            const TimeLine& timeLine = *(aNode->timeLine());
            const int validNum = timeLine.validTypeCount();
            // Keys sit at their absolute frame position (margin + spacing*frame),
            // the same base the playhead, ruler ticks and child keys use.
            const int left = mMargin;
            const bool isSlimmed = aNode->isSlimmedDown();
            int validIndex = 0;

            for (int i = 0; i < TimeKeyType_TERM; ++i) {
                auto type = TimeLine::getTimeKeyTypeInOrderOfOperations(i);
                const TimeLine::MapType& map = timeLine.map(type);
                if (map.isEmpty())
                    continue;

                const float height = aRow.keyHeight(validIndex, validNum);
                ++validIndex;

                mPainter.setPen(QPen(kBrushKeyEdge, 1));

                auto itr = map.lowerBound(mRange.min());
                while (itr != map.end() && itr.key() <= mRange.max()) {
                    const bool isFocused = itr.value()->isFocused();
                    constexpr int lighter_val_selected = 150;
                    QBrush kBrushKeyBody1(theme::Colors::current().baseKey.lighter(lighter_val_selected));
                    QBrush kBrushKeyBody2(theme::Colors::current().baseKey);
                    switch (itr.value()->type()) {
                        case TimeKeyType_Move:
                            kBrushKeyBody1 = theme::Colors::current().moveKey;
                            kBrushKeyBody2 = theme::Colors::current().moveKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Rotate:
                            kBrushKeyBody1 = theme::Colors::current().rotateKey;
                            kBrushKeyBody2 = theme::Colors::current().rotateKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Scale:
                            kBrushKeyBody1 = theme::Colors::current().scaleKey;
                            kBrushKeyBody2 = theme::Colors::current().scaleKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Depth:
                            kBrushKeyBody1 = theme::Colors::current().depthKey;
                            kBrushKeyBody2 = theme::Colors::current().depthKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Opa:
                            kBrushKeyBody1 = theme::Colors::current().opaKey;
                            kBrushKeyBody2 = theme::Colors::current().opaKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Bone:
                            kBrushKeyBody1 = theme::Colors::current().boneKey;
                            kBrushKeyBody2 = theme::Colors::current().boneKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Pose:
                            kBrushKeyBody1 = theme::Colors::current().poseKey;
                            kBrushKeyBody2 = theme::Colors::current().poseKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Mesh:
                            kBrushKeyBody1 = theme::Colors::current().meshKey;
                            kBrushKeyBody2 = theme::Colors::current().meshKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_FFD:
                            kBrushKeyBody1 = theme::Colors::current().FFDKey;
                            kBrushKeyBody2 = theme::Colors::current().FFDKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Image:
                            kBrushKeyBody1 = theme::Colors::current().imageKey;
                            kBrushKeyBody2 = theme::Colors::current().imageKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_HSV:
                            kBrushKeyBody1 = theme::Colors::current().HSVKey;
                            kBrushKeyBody2 = theme::Colors::current().HSVKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_Blur:
                            kBrushKeyBody1 = theme::Colors::current().blurKey;
                            kBrushKeyBody2 = theme::Colors::current().blurKey.lighter(lighter_val_selected);
                            break;
                        case TimeKeyType_TERM:
                            break;
                    }
                    mPainter.setBrush(isFocused ? kBrushKeyBody2 : kBrushKeyBody1);

                    auto attr = mScale->attribute(itr.key());
                    QPointF pos(left + attr.pos + 0.5, height + 0.5f);

                    if (isSlimmed) {
                        /*
                        const QPointF poly[3] = {
                            pos + QPointF(0.0f, -2.5f),
                            pos + QPointF(-3.0f, 2.5f),
                            pos + QPointF(3.0f, 2.5f) };
                        mPainter.drawConvexPolygon(poly, 3);
                        */
                        // mPainter.drawEllipse(pos, 3.0f, 1.5f);
                        const QPointF poly[] = {
                            pos + QPointF(keyRadius * -1, keyRadiusSlim * -1),
                            pos + QPointF(keyRadius, keyRadiusSlim * -1),
                            pos + QPointF(keyRadius, keyRadiusSlim),
                            pos + QPointF(keyRadius * -1, keyRadiusSlim)};
                        mPainter.drawConvexPolygon(poly, 4);
                    } else if (itr.value()->canHoldChild()) {
                        const QPointF poly[4] = {pos + holder[0], pos + holder[1], pos + holder[2], pos + holder[3]};
                        mPainter.drawConvexPolygon(poly, 4);
                    } else {
                        mPainter.drawEllipse(pos, keyRadius, keyRadius);
                    }

                    ++itr;
                }
            }
        }
    }

    void Renderer::drawChildKeys(const ObjectNode* aNode, const QPoint& aPos) {
        const QBrush kBrushKey(theme::Colors::current().baseKey);

        mPainter.setPen(QPen(kBrushKey, 1));
        mPainter.setBrush(kBrushKey);

        for (auto child : aNode->children()) {
            ObjectNode::ConstIterator treeItr(child);
            while (treeItr.hasNext()) {
                const ObjectNode* node = treeItr.next();
                if (!node->timeLine())
                    continue;
                const TimeLine& timeLine = *(node->timeLine());

                for (int i = 0; i < TimeKeyType_TERM; ++i) {
                    auto type = TimeLine::getTimeKeyTypeInOrderOfOperations(i);
                    const TimeLine::MapType& map = timeLine.map(type);

                    auto itr = map.lowerBound(mRange.min());
                    while (itr != map.end() && itr.key() <= mRange.max()) {
                        auto attr = mScale->attribute(itr.key());
                        QPointF pos[3];
                        pos[0] = QPointF(aPos.x() + attr.pos + 0.5, aPos.y());
                        pos[1] = pos[0] + QPointF(keyRadiusChild, keyRadiusChildPoly * -1);
                        pos[2] = pos[0] + QPointF(keyRadiusChild * -1, keyRadiusChildPoly * -1);
                        mPainter.drawConvexPolygon(pos, 3);
                        ++itr;
                    }
                }
            }
        }
    }

} // namespace time
} // namespace ctrl
