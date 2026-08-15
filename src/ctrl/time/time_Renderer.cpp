#include "ctrl/time/time_Renderer.h"
#include "ctrl/TimeLineEditor.h"
#include "gui/theme/Colors.h"

using namespace core;

namespace ctrl {
namespace time {

    static QColor withAlpha(QColor aColor, int aAlpha) { aColor.setAlpha(aAlpha); return aColor; }

    Renderer::Renderer(QPainter& aPainter, const CameraInfo& aCamera, const theme::TimeLine& aTheme,
        const TimeFormatType& timeFormat):
        mPainter(aPainter), mCamera(aCamera), mTheme(aTheme), mMargin(), mRange(), mScale(), timeFormatVar(timeFormat){}

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
            const QPoint cpos(mMargin, rect.bottom());

            if (!aCullRect.intersects(rect))
                continue;

            // draw line (fill only — the 1px border is one hairline per seam
            // below; per-row outlines would double the shared boundary)
            mPainter.setPen(Qt::NoPen);
            mPainter.setBrush(row.selecting ? kBrushBodySelect : kBrushBody);
            mPainter.drawRect(rect);

            if (!row.node || !row.node->timeLine())
                continue;

            if (!row.node->isSlimmedDown()) {
                // draw separators
                mPainter.setPen(QPen(kBrushSepa, 1, Qt::DotLine));
                const int sepa = row.node->timeLine()->validTypeCount();
                for (int i = 1; i < sepa; ++i) {
                    const float h = static_cast<float>(rect.height()) / sepa;
                    const float y = rect.top() + i * h;
                    const QPointF v0(rect.left(), y);
                    const QPointF v1(rect.right(), y);
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

            // draw child keys
            if (row.closedFolder) {
                drawChildKeys(row.node, cpos);
            }
            // draw keys
            drawKeys(row.node, row);
        }

        // 1px borders: one hairline per seam (each row's top) plus the last
        // row's bottom, so adjacent rows' 1px outlines can't stack into a 2px
        // border. AA off keeps each line exactly one pixel.
        mPainter.setRenderHint(QPainter::Antialiasing, false);
        mPainter.setPen(QPen(kBrushEdge, 1));
        mPainter.setBrush(Qt::NoBrush);
        for (const TimeLineRow& row : aRows) {
            if (!aCullRect.intersects(row.rect)) continue;
            mPainter.drawLine(row.rect.left(), row.rect.top(), row.rect.right(), row.rect.top());
        }
        if (!aRows.isEmpty()) {
            const TimeLineRow& last = aRows.back();
            if (aCullRect.intersects(last.rect)) {
                mPainter.drawLine(last.rect.left(), last.rect.bottom(), last.rect.right(), last.rect.bottom());
            }
        }
        mPainter.setRenderHint(QPainter::Antialiasing);
    }

    void Renderer::renderHeader(int aHeight, int aFps) {
        const QRect cameraRect(-mCamera.leftTopPos().toPoint(), mCamera.screenSize());

        mPainter.setRenderHint(QPainter::Antialiasing, false);

        // draw header background
        {
            const QBrush kBrush(mTheme.headerBackgroundColor());
            QRect rect = cameraRect;
            rect.setHeight(aHeight);
            mPainter.setPen(QPen(kBrush, 1));
            mPainter.setBrush(kBrush);
            mPainter.drawRect(rect);

            // hairline under the ruler separates it from the track area.
            // The ticks end one row above this line (top + height - 1), so
            // the line never crosses them.
            const int lineY = rect.top() + rect.height();
            mPainter.setPen(QPen(QBrush(mTheme.rulerLineColor()), 1));
            mPainter.drawLine(rect.left(), lineY, rect.right(), lineY);
        }

        // draw header info
        {
            const QBrush kBrush(mTheme.headerContentColor());
            const int numberWidth = 6;
            const QPoint lt(mMargin, cameraRect.top());
            // the ticks end one row above the ruler hairline (drawn at
            // top + height) so they never draw over it
            const QPoint rb = lt + QPoint(mScale->maxPixelWidth(), aHeight - 1);
            const TimeFormat timeFormat(mRange, aFps);

            // The project's last frame owns the end of the ruler: when it is
            // in view but not on a scale multiple, it reads as a major tick
            // with its number, and any numbered neighbor whose label would
            // overlap it yields instead.
            const int lastFrame = mScale->maxFrame();
            const auto lastAttr = mScale->attribute(lastFrame);
            const bool lastLabelActive = lastFrame >= mRange.min() && lastFrame <= mRange.max() &&
                                         lastAttr.grid.y() < 10;
            const int lastLabelCenter = lt.x() + lastAttr.grid.x();
            const QString lastNumber =
                lastLabelActive ? timeFormat.frameToString(lastFrame, timeFormatVar) : QString();
            const int lastNumberWidth = numberWidth * lastNumber.size();

            for (int i = mRange.min(); i <= mRange.max(); ++i) {
                auto attr = mScale->attribute(i);
                if (attr.grid.y() <= 0) continue;

                QPoint pos(lt.x() + attr.grid.x(), rb.y());

                // Fade the tick tiers so minor blips recede and even the
                // numbered major ticks stay a step below full brightness.
                const qreal alpha = attr.grid.y() >= 10 ? 0.85
                                  : attr.grid.y() >= 8  ? 0.65
                                  : attr.grid.y() >= 6  ? 0.45
                                                        : 0.25;
                QColor tickColor = kBrush.color();
                tickColor.setAlphaF(alpha);
                mPainter.setPen(QPen(tickColor, 1));
                mPainter.drawLine(pos, pos + QPoint(0, -attr.grid.y()));

                if (attr.showNumber) {
                    QString number = timeFormat.frameToString(i, timeFormatVar);
                    const int width = static_cast<int>(numberWidth * number.size());
                    // A neighbor label never overlaps the end label: the last
                    // frame's number always wins the space.
                    if (lastLabelActive && i < lastFrame) {
                        if (pos.x() + (width >> 1) >= lastLabelCenter - (lastNumberWidth >> 1))
                            continue;
                    }
                    mPainter.setPen(QPen(kBrush, 1));
                    const int left = pos.x() - (width >> 1);
                    const int top = lt.y() + ctrl::TimeLineEditor::kNumberTop;
                    const QRect rect(
                        QPoint(left, top), QPoint(left + width + 1, top + ctrl::TimeLineEditor::kNumberHeight)
                    );
                    mPainter.drawText(rect, number);
                }
            }

            if (lastLabelActive) {
                const QPoint pos(lt.x() + lastAttr.grid.x(), rb.y());

                QColor tickColor = kBrush.color();
                tickColor.setAlphaF(0.85);
                mPainter.setPen(QPen(tickColor, 1));
                mPainter.drawLine(pos, pos + QPoint(0, -10));

                // The loop already labels this frame when it falls on a
                // tier B/C multiple; otherwise the end number is ours.
                if (!lastAttr.showNumber) {
                    mPainter.setPen(QPen(kBrush, 1));
                    const int width = numberWidth * lastNumber.size();
                    const int left = pos.x() - (width >> 1);
                    const int top = lt.y() + ctrl::TimeLineEditor::kNumberTop;
                    const QRect rect(
                        QPoint(left, top), QPoint(left + width + 1, top + ctrl::TimeLineEditor::kNumberHeight)
                    );
                    mPainter.drawText(rect, lastNumber);
                }
            }
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
        const QBrush kBrushKeyBody1(theme::Colors::current().textMuted);
        const QBrush kBrushKeyBody2(theme::Colors::current().text);
        const QBrush kBrushKeyEdge(theme::Colors::current().outline);
        QPointF holder[4] = {QPointF(0.0, -4.2), QPointF(4.2, 0.0), QPointF(0.0, 4.2), QPointF(-4.2, 0.0)};

        if (aNode && aNode->timeLine()) {
            const TimeLine& timeLine = *(aNode->timeLine());
            const int validNum = timeLine.validTypeCount();
            const int left = aRow.rect.left();
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
                    mPainter.setBrush(isFocused ? kBrushKeyBody2 : kBrushKeyBody1);

                    auto attr = mScale->attribute(itr.key());
                    QPointF pos(left + attr.grid.x() + 0.5, static_cast<double>(height + 0.5f));

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
                            pos + QPointF(-3.0, -2.0),
                            pos + QPointF(3.0, -2.0),
                            pos + QPointF(3.0, 2.0),
                            pos + QPointF(-3.0, 2.0)};
                        mPainter.drawConvexPolygon(poly, 4);
                    } else if (itr.value()->canHoldChild()) {
                        const QPointF poly[4] = {pos + holder[0], pos + holder[1], pos + holder[2], pos + holder[3]};
                        mPainter.drawConvexPolygon(poly, 4);
                    } else {
                        mPainter.drawEllipse(pos, 3.0, 3.0);
                    }

                    ++itr;
                }
            }
        }
    }

    void Renderer::drawChildKeys(const ObjectNode* aNode, const QPoint& aPos) {
        const QBrush kBrushKey(theme::Colors::current().textMuted);

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
                        pos[0] = QPointF(aPos.x() + attr.grid.x() + 0.5, aPos.y());
                        pos[1] = pos[0] + QPointF(3, -5);
                        pos[2] = pos[0] + QPointF(-3, -5);
                        mPainter.drawConvexPolygon(pos, 3);
                        ++itr;
                    }
                }
            }
        }
    }

} // namespace time
} // namespace ctrl
