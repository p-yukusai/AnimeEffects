#include "TimeFormat.h"

namespace {

// Floor division for positive divisors: frame-to-second decomposition must
// round toward negative infinity, not toward zero (C++ division truncates),
// so negative frames count down through -1, -2, ... instead of colliding at
// "0" with the frames just before 0.
int floorDiv(int aValue, int aDivisor) {
    return aValue >= 0 ? aValue / aDivisor : (aValue - aDivisor + 1) / aDivisor;
}

// Floor-decompose a frame at aFps into whole seconds + the frame within that
// second. Frame-within-second stays in [0, aFps-1] even for negative frames
// (-1 @ 24fps -> -1s, 23f), matching how negative frames count down.
void decomposeSecondsFrames(int aFrame, int aFps, int& aSeconds, int& aFrames) {
    aSeconds = floorDiv(aFrame, aFps);
    aFrames = aFrame - aSeconds * aFps;
}

} // namespace

namespace core {

TimeFormat::TimeFormat(util::Range aRange, int aFps) {
    mRange = aRange;
    mFps = aFps;
}

QString TimeFormat::frameToString(const int aFrame, const TimeFormatType aTimeFormatType) const {
    switch (aTimeFormatType) {
    case TimeFormatType_Frames_From1:
        // Frames are natively user-facing 1-based now (the default range
        // starts at 1), so adding 1 would read every frame one too high.
        // Both From0 and From1 show the actual frame number; the From1
        // label is retained for backward compatibility with saved settings.
        return QString::number(aFrame);

    case TimeFormatType_Relative_FPS: {
        // Fractional seconds: the frame's exact time at the project fps
        // (frame 30 @ 24fps reads "1.25"). Always two decimals and no
        // trailing-zero trim, so the label width is constant while scrubbing
        // instead of jumping (1.25 -> 1.3 -> 1.35). 2 decimals still
        // distinguishes adjacent frames at any practical fps (1/fps >= 0.01
        // up to 100fps).
        return QString::number(aFrame / (double)mFps, 'f', 2);
    }

    case TimeFormatType_Seconds_Frames: { // (SS+FF)
        // fps-aware "seconds+frames": a frame at aFps decomposes so FF stays
        // in [0, aFps-1] (24fps -> 0..23). The frame field is separated from
        // the time units with '+', not ':'. SS carries into negatives exactly
        // like SS+FF above, so -1 frame reads "-1+23" (one frame before 0).
        int seconds, frames;
        decomposeSecondsFrames(aFrame, mFps, seconds, frames);
        return QString("%1+%2").arg(seconds).arg(frames, 2, 10, QLatin1Char('0'));
    }

    case TimeFormatType_Timecode_SMPTE: { // (HH:MM:SS+FF)
        // fps-aware SMPTE timecode, 'HH:MM:SS+FF'. The hours field is dropped
        // when it is 00 (the timecode starts at minutes), and every remaining
        // field is zero-padded so the width is stable while scrubbing. The
        // frame field is separated from the time units with '+'. Negative
        // frames get a leading '-', so -1 frame @ 24fps reads "-00:00+01".
        const int sign = aFrame < 0 ? -1 : 1;
        const int absFrame = qAbs(aFrame);
        int seconds, frames;
        decomposeSecondsFrames(absFrame, mFps, seconds, frames);
        const int hh = seconds / 3600;
        const int mm = (seconds / 60) % 60;
        const int ss = seconds % 60;
        const QString framesText = QString("%1").arg(frames, 2, 10, QLatin1Char('0'));
        const QString text = hh > 0
            ? QString("%1:%2:%3+%4")
                  .arg(hh, 2, 10, QLatin1Char('0'))
                  .arg(mm, 2, 10, QLatin1Char('0'))
                  .arg(ss, 2, 10, QLatin1Char('0'))
                  .arg(framesText)
            : QString("%1:%2+%3")
                  .arg(mm, 2, 10, QLatin1Char('0'))
                  .arg(ss, 2, 10, QLatin1Char('0'))
                  .arg(framesText);
        return sign < 0 ? QString("-") + text : text;
    }

    case TimeFormatType_Timecode_HHMMSSmmm: { // (HH:MM:SS:mmm)
        // fps-aware milliseconds: the frame is converted at the project fps,
        // so 24 frames @ 24fps reads "00:01:000" (one second), not 400ms.
        // The hours field is dropped when it is 00, and minutes/seconds
        // (2-digit) and milliseconds (3-digit) are zero-padded for a stable
        // width while scrubbing.
        const int sign = aFrame < 0 ? -1 : 1;
        const qint64 absFrame = qAbs(static_cast<qint64>(aFrame));
        const double ms = absFrame * 1000.0 / mFps;
        const DDHHMMSSmmm timeStruct = msToDDHHMMSSmmm(ms);
        const QString text = timeStruct.hours > 0
            ? QString("%1:%2:%3:%4")
                  .arg(timeStruct.hours, 2, 10, QLatin1Char('0'))
                  .arg(timeStruct.minutes, 2, 10, QLatin1Char('0'))
                  .arg(timeStruct.seconds, 2, 10, QLatin1Char('0'))
                  .arg(timeStruct.milliseconds, 3, 10, QLatin1Char('0'))
            : QString("%1:%2:%3")
                  .arg(timeStruct.minutes, 2, 10, QLatin1Char('0'))
                  .arg(timeStruct.seconds, 2, 10, QLatin1Char('0'))
                  .arg(timeStruct.milliseconds, 3, 10, QLatin1Char('0'));
        return sign < 0 ? QString("-") + text : text;
    }

    default:
        return QString::number(aFrame);
    }
}


DDHHMMSSmmm TimeFormat::msToDDHHMMSSmmm(double ms) {
    int d = 0, h = 0, mi = 0, s = 0;
    // Days to milli
    if (ms >= 86400 * 1000) {
        d = qFloor(ms / 1000 / 60 / 60 / 24);
        ms = ms - d * (86400 * 1000);
    }
    // Hour to milli
    if (ms >= 3600 * 1000) {
        h = qFloor(ms / 1000 / 60 / 60);
        ms = ms - h * (3600 * 1000);
    }
    // Minute to milli
    if (ms >= 60 * 1000) {
        mi = qFloor(ms / 1000 / 60);
        ms = ms - mi * (60 * 1000);
    }
    // Second to milli
    if (ms >= 1000) {
        s = qFloor(ms / 1000);
        ms = ms - s * 1000;
    }
    DDHHMMSSmmm timeStruct;
    timeStruct.days = d;
    timeStruct.hours = h;
    timeStruct.minutes = mi;
    timeStruct.seconds = s;
    timeStruct.milliseconds = static_cast<int>(ms);

    return timeStruct;
}

} // namespace core
