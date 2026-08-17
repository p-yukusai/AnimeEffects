#include "gui/theme/TimeLine.h"
#include "gui/theme/Colors.h"

#include <QApplication>
namespace theme {

//-------------------------------------------------------------------------------------------------
TimeLine::TimeLine() {
    reset();
}

void TimeLine::reset() {
    // Read the cached theme tokens, activated by GUIResources before any
    // timeline is built.
    const Colors c = Colors::current();

    mHeaderContentColor = c.text;
    // the ruler is part of the timeline surface: base, not elevated
    mHeaderBackgroundColor = c.base;
    // hairline under the ruler separates it from the tracks
    mRulerLineColor = c.hairline;

    // track surfaces sit on the base floor; only the edges/separators/labels
    // distinguish rows, never brightness
    mTrackColor = c.base;
    mTrackEdgeColor = c.hairline;
    mTrackTextColor = c.text;
    // selected track: the brand selection fill from the tokens (fixed alpha;
    // theme differences live in the Tailwind columns, not per-widget)
    mTrackSelectColor = c.selection;
    mTrackSeperatorColor = c.hairlineHover;
    shade();
}

void TimeLine::shade() {
    mTrackColor = QColor(mTrackColor.red(), mTrackColor.green(), mTrackColor.blue(), 180);
    mTrackEdgeColor = QColor(mTrackEdgeColor.red(), mTrackEdgeColor.green(), mTrackEdgeColor.blue(), 180);
    mTrackTextColor = QColor(mTrackTextColor.red(), mTrackTextColor.green(), mTrackTextColor.blue(), 180);
    mTrackSeperatorColor =
        QColor(mTrackSeperatorColor.red(), mTrackSeperatorColor.green(), mTrackSeperatorColor.blue(), 180);
}

QColor TimeLine::headerContentColor() const { return mHeaderContentColor; }

void TimeLine::setHeaderContentColor(const QColor& headerContentColor) { mHeaderContentColor = headerContentColor; }

QColor TimeLine::headerBackgroundColor() const { return mHeaderBackgroundColor; }
QColor TimeLine::rulerLineColor() const { return mRulerLineColor; }
void TimeLine::setRulerLineColor(const QColor& rulerLineColor) { mRulerLineColor = rulerLineColor; }

void TimeLine::setHeaderBackgroundColor(const QColor& headerBackgroundColor) {
    mHeaderBackgroundColor = headerBackgroundColor;
}

QColor TimeLine::trackSeperatorColor() const { return mTrackSeperatorColor; }

void TimeLine::setTrackSeperatorColor(const QColor& trackSeperatorColor) { mTrackSeperatorColor = trackSeperatorColor; }

QColor TimeLine::trackTextColor() const { return mTrackTextColor; }

void TimeLine::setTrackTextColor(const QColor& trackTextColor) { mTrackTextColor = trackTextColor; }

QColor TimeLine::trackSelectColor() const { return mTrackSelectColor; }

void TimeLine::setTrackSelectColor(const QColor& trackSelectColor) { mTrackSelectColor = trackSelectColor; }

QColor TimeLine::trackEdgeColor() const { return mTrackEdgeColor; }

void TimeLine::setTrackEdgeColor(const QColor& trackEdgeColor) { mTrackEdgeColor = trackEdgeColor; }

QColor TimeLine::trackColor() const { return mTrackColor; }

void TimeLine::setTrackColor(const QColor& trackColor) {
    mTrackColor = trackColor;
    shade();
}

} // namespace theme
