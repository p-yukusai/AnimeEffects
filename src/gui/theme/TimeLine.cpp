#include "gui/theme/TimeLine.h"

namespace theme {

//-------------------------------------------------------------------------------------------------
TimeLine::TimeLine():
    mHeaderContentColor(QColor(75, 75, 75, 255)),
    mHeaderBackgroundColor(QColor(236, 236, 236, 255)),
    mRulerLineColor(QColor(200, 200, 200, 255)),
    mTrackColor(QColor(250, 250, 250, 255)),
    mTrackEdgeColor(QColor(190, 190, 190, 255)),
    mTrackTextColor(QColor(170, 170, 170, 255)),
    mTrackSelectColor(QColor(235, 240, 250, 255)),
    mTrackSeperatorColor(QColor(200, 200, 205, 255)) {
    reset();
}

void TimeLine::reset() {
    QPalette palette;
    qreal lightness = palette.window().color().lightnessF();

    if (lightness > 0.5) { // Light theme
        mHeaderContentColor = QColor(75, 75, 75, 255);   // neutral, no chroma
        mHeaderBackgroundColor = QColor(236, 236, 236, 255); // matches the chrome
        mRulerLineColor = QColor(200, 200, 200, 255);    // hairline under the ruler

        mTrackColor = QColor(250, 250, 250, 255);
        mTrackEdgeColor = QColor(190, 190, 190, 255);
        mTrackTextColor = QColor(170, 170, 170, 255);
        mTrackSelectColor = QColor(235, 240, 250, 180);
        mTrackSeperatorColor = QColor(200, 200, 205, 255);
    } else { // Dark theme
        mHeaderContentColor = palette.text().color();
        // the ruler is part of the timeline surface: base, not elevated
        mHeaderBackgroundColor = QColor(38, 38, 38, 255);
        // hairline under the ruler separates it from the tracks
        mRulerLineColor = QColor(52, 52, 52, 255);

        // track surfaces sit on the #262626 floor (the base token); only the
        // edges/separators/labels distinguish rows, never brightness
        mTrackColor = QColor(38, 38, 38, 255);
        // row seams are hairlines (the #343434 token) like the other
        // separators; the lane separators sit one step above (74, 75, 76)
        mTrackEdgeColor = QColor(52, 52, 52, 255);
        mTrackTextColor = palette.text().color(); // QColor(44, 45, 46, 255);
        // selection token: brand oklch hue (279.3 deg) at low elevation — the
        // selected object's track reads as the selection color. The 40% tint
        // is baked into the alpha (dark only; the light theme keeps its
        // stronger 180 alpha below).
        mTrackSelectColor = QColor(54, 56, 109, 102);
        mTrackSeperatorColor = QColor(74, 75, 76, 255);
    }
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
