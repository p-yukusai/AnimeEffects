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
    // Selected track: the lane band's tint — the accent at 6%, "a whisper, not
    // a flood" (design s3). It replaces the old whole-lane @selection@ fill
    // (accent @0.40); the row's 2px rail is the object tree's, because a rail
    // marks where a row STARTS and a row starts in the tree column. Selection
    // is ONE treatment across the two columns: this band tints the lanes' whole
    // visible width, and the tree row carries the same 0.06 tint plus the rail
    // (ObjectTreeWidget overrides the shared @selection@ row rule with this
    // tint in its own loaded stylesheet, since the app-wide rule's token is the
    // 0.40 flood). accentBright is the token the mock's proposed fill uses
    // (--accent-bright at 0.06) and the one the app builds translucent accent
    // overlays from.
    mTrackSelectColor = c.accentBright;
    mTrackSelectColor.setAlphaF(0.06);
    // Same hue as the row seams (hairline), so the LOWER alpha really is a lower
    // contrast. hairlineHover was a brighter token, and at 40% it could match the
    // 70% row seam - the "no opacity difference" report.
    mTrackSeperatorColor = c.hairline;
    shade();
}

void TimeLine::shade() {
    mTrackColor = QColor(mTrackColor.red(), mTrackColor.green(), mTrackColor.blue(), 180);
    mTrackEdgeColor = QColor(mTrackEdgeColor.red(), mTrackEdgeColor.green(), mTrackEdgeColor.blue(), 180);
    mTrackTextColor = QColor(mTrackTextColor.red(), mTrackTextColor.green(), mTrackTextColor.blue(), 180);
    // The fine type separators inside a lane: a 4-on/4-off dash at 40%. The dash is
    // what tells them apart from the SOLID 70% seams between rows/layers; solid at
    // both levels made the two concepts look the same.
    mTrackSeperatorColor =
        QColor(mTrackSeperatorColor.red(), mTrackSeperatorColor.green(), mTrackSeperatorColor.blue(), 102);
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
