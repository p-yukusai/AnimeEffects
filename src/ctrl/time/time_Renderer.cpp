#include "ctrl/time/time_Renderer.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/time/time_BodyModel.h"
#include "gui/theme/Colors.h"

#include <cmath>

using namespace core;

namespace ctrl {
namespace time {

    // This file's palette helpers went with the passes they served when those became models:
    // the key hues, the selection mix and the label alpha are ctrl/time/time_BodyModel.cpp's now
    // (theme::channelColor plus the model's own kSelectFillMix), and the renderer only adapts.

    Renderer::Renderer(QPainter& aPainter, const CameraInfo& aCamera, const theme::TimeLine& aTheme):
        mPainter(aPainter), mCamera(aCamera), mTheme(aTheme), mMargin(), mRange(), mScale() {}

    void Renderer::renderLines(const QVector<TimeLineRow>& aRows, const QRect& aCameraRect, const QRect& aCullRect) {
        // THE LANE PASS IS A MODEL NOW (gui/TimeBodyModel.h): the geometry and ink are
        // stated as data, so the pass can be re-hosted (the timeline is migrating to
        // Canvas Painter - see AGENTS.md). This renderer paints that model through the
        // shared QPainter rendering, which is byte-for-byte the pass it drew before.
        Inks inks;
        inks.body = mTheme.trackColor();
        inks.bodySelected = mTheme.trackSelectColor();
        inks.edge = mTheme.trackEdgeColor();
        inks.separator = mTheme.trackSeperatorColor();
        paintLines(mPainter, buildLines(aRows, aCameraRect, aCullRect, inks));
    }

    void Renderer::renderRangeMarkers(const QRect& aCameraRect) {
        // A MODEL NOW (ctrl/time/time_BodyModel.h): the two guides are stated as data and
        // painted through the shared QPainter rendering.
        paintGuides(
            mPainter, buildRangeMarkers(aCameraRect, mProjectRange, mPlayWindow.active(), *mScale, mMargin),
            SelectionBox()
        );
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

            // THE KEY PASS IS A MODEL NOW (ctrl/time/time_BodyModel.h): the marks are stated as
            // data and painted through the shared QPainter rendering, in the same order the pass
            // always used (a closed folder's summary first).
            const QColor laneInk = opaqueOver(
                row.selecting ? mTheme.trackSelectColor() : mTheme.trackColor(),
                theme::Colors::current().base
            );
            if (row.closedFolder && row.summaryHeight() > 0)
                paintKeys(
                    mPainter, buildChildKeys(row.node, row.summaryRect(), mMargin, *mScale, mRange)
                );
            paintKeys(mPainter, buildKeys(row.node, row, *mScale, mRange, mMargin, laneInk));
        }
    }


    void Renderer::renderSelectionRange(const QRect& aRect) {
        paintGuides(mPainter, {}, buildMarquee(aRect));
    }

} // namespace time
} // namespace ctrl
