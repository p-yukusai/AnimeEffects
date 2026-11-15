#include "gui/qml/ProjectSettingViewModel.h"

namespace gui {

namespace {
bool isHexDigit(QChar aChar) {
    const ushort u = aChar.unicode();
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

// The "#rrggbb" spelling of an RGB hex string, or nothing when the string is not
// one. Case is folded and "#abc" is expanded; alpha is not a viewport background and
// is refused rather than silently dropped. The EMPTY string is not a colour and not
// garbage - it is the theme default - so it normalizes to itself.
bool normalizeHex(const QString& aInput, QString* aOut) {
    QString value = aInput.trimmed();
    if (value.isEmpty()) {
        *aOut = QString();
        return true;
    }
    if (value.startsWith(QLatin1Char('#')))
        value.remove(0, 1);
    if (value.size() != 3 && value.size() != 6)
        return false;
    for (const QChar c : value) {
        if (!isHexDigit(c))
            return false;
    }
    if (value.size() == 3) {
        QString expanded;
        expanded.reserve(6);
        for (const QChar c : value) {
            expanded += c;
            expanded += c;
        }
        value = expanded;
    }
    *aOut = QStringLiteral("#") + value.toLower();
    return true;
}
} // namespace

ProjectSettingViewModel::ProjectSettingViewModel(QObject* aParent) : QObject(aParent) {}

void ProjectSettingViewModel::setCanvasWidth(int aWidth) {
    // Clamp in the setter, not at the control: the widget form's QSpinBox range was
    // [1, maxBufferSize], and every writer (a scrub, a typed edit, a stale binding)
    // takes the same floor and the same device ceiling here.
    const int clamped = qBound(1, aWidth, mMaxCanvasSize);
    if (mCanvasWidth == clamped)
        return;
    mCanvasWidth = clamped;
    emit canvasWidthChanged();
}

void ProjectSettingViewModel::setCanvasHeight(int aHeight) {
    const int clamped = qBound(1, aHeight, mMaxCanvasSize);
    if (mCanvasHeight == clamped)
        return;
    mCanvasHeight = clamped;
    emit canvasHeightChanged();
}

void ProjectSettingViewModel::setMaxCanvasSize(int aMaxSize) {
    // At least 1, so the ceiling can never sit below the floor the canvas clamp
    // needs (qBound's precondition).
    const int clamped = qMax(aMaxSize, 1);
    if (mMaxCanvasSize == clamped)
        return;
    mMaxCanvasSize = clamped;
    // A seeded size above the new ceiling cannot survive the clamp, so re-run the
    // pair through it: the widget form's QSpinBox range did this at construction,
    // and a late seed must not leave the model holding a size the device cannot
    // make. The notification goes out AFTER the pair is consistent, so a scene
    // re-reading `to` and `value` on it never sees the two disagree.
    setCanvasWidth(mCanvasWidth);
    setCanvasHeight(mCanvasHeight);
    emit maxCanvasSizeChanged();
}

void ProjectSettingViewModel::setFrameStart(int aStart) {
    // The widget form linked the pair (start->setMaximum(end)); the scene binds each
    // field's `to`/`from` to its partner too, but the INVARIANT is enforced here so
    // no writer - a scrub, a typed edit, a stale binding - can leave start > end.
    const int clamped = qMin(aStart, mFrameEnd);
    if (mFrameStart == clamped)
        return;
    mFrameStart = clamped;
    emit frameStartChanged();
}

void ProjectSettingViewModel::setFrameEnd(int aEnd) {
    // The other half of the same link (end->setMinimum(start)).
    const int clamped = qMax(aEnd, mFrameStart);
    if (mFrameEnd == clamped)
        return;
    mFrameEnd = clamped;
    emit frameEndChanged();
}

void ProjectSettingViewModel::setLoop(bool aLoop) {
    if (mLoop == aLoop)
        return;
    mLoop = aLoop;
    emit loopChanged();
}

void ProjectSettingViewModel::setFps(int aFps) {
    // The widget QSpinBox's floor: a project rate below 1 frame per second would
    // make the playback tick and the exporter divide by zero.
    const int clamped = qMax(aFps, 1);
    if (mFps == clamped)
        return;
    mFps = clamped;
    emit fpsChanged();
}

void ProjectSettingViewModel::setViewportBackgroundHex(const QString& aHex) {
    QString normalized;
    if (!normalizeHex(aHex, &normalized))
        return; // garbage: the value already held stands
    const bool wasThemeDefault = viewportBackgroundIsThemeDefault();
    if (mViewportBackgroundHex == normalized)
        return;
    mViewportBackgroundHex = normalized;
    emit viewportBackgroundHexChanged();
    // The derived flag only flips when the string crossed the empty/non-empty line,
    // so a colour edit does not wake a control that only cares about theme-default.
    if (wasThemeDefault != viewportBackgroundIsThemeDefault())
        emit viewportBackgroundIsThemeDefaultChanged();
}

void ProjectSettingViewModel::requestViewportBackgroundPick() {
    emit viewportBackgroundPickRequested();
    // The shell answers synchronously inside that emit with a modal chooser, and it
    // writes a new hex back only when the user accepts. Re-notify the derived flag
    // afterwards so a control that moved itself optimistically - the "use theme
    // background" box, unticked to ask for a colour - re-reads the model either way:
    // an accepted choice leaves it unticked, a cancelled chooser puts it back on the
    // theme, and neither case is visible without this second read.
    emit viewportBackgroundIsThemeDefaultChanged();
}

void ProjectSettingViewModel::accept() { emit acceptRequested(); }

void ProjectSettingViewModel::cancel() { emit cancelRequested(); }

} // namespace gui
