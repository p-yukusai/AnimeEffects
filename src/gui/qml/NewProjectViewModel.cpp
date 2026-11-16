#include "gui/qml/NewProjectViewModel.h"

#include <QFileInfo>

namespace gui {

NewProjectViewModel::NewProjectViewModel(QObject* aParent) : QObject(aParent) {}

QString NewProjectViewModel::fileNameLabel() const {
    // The leaf name, for a field that has no room for a whole path; empty while
    // nothing is chosen, so the scene can key its placeholder off the same state.
    return mFileName.isEmpty() ? QString() : QFileInfo(mFileName).fileName();
}

void NewProjectViewModel::setFileName(const QString& aFileName) {
    if (mFileName == aFileName)
        return;
    mFileName = aFileName;
    // One signal for both fileName and its derived label: they always move
    // together, so there is no second thing to keep in sync.
    emit fileNameChanged();
}

void NewProjectViewModel::setFrameCount(int aFrameCount) {
    // Clamp in the setter, not at the control: the same floor the widget
    // QSpinBox had, held for every writer (a scrub, a typed edit, a test).
    const int clamped = qMax(aFrameCount, 1);
    if (mFrameCount == clamped)
        return;
    mFrameCount = clamped;
    emit frameCountChanged();
}

void NewProjectViewModel::setFps(int aFps) {
    const int clamped = qMax(aFps, 1);
    if (mFps == clamped)
        return;
    mFps = clamped;
    emit fpsChanged();
}

void NewProjectViewModel::setSpecifiesCanvasSize(bool aSpecifies) {
    if (mSpecifiesCanvasSize == aSpecifies)
        return;
    mSpecifiesCanvasSize = aSpecifies;
    emit specifiesCanvasSizeChanged();
}

void NewProjectViewModel::setCanvasWidth(int aWidth) {
    const int clamped = qMax(aWidth, 1);
    if (mCanvasWidth == clamped)
        return;
    mCanvasWidth = clamped;
    emit canvasWidthChanged();
}

void NewProjectViewModel::setCanvasHeight(int aHeight) {
    const int clamped = qMax(aHeight, 1);
    if (mCanvasHeight == clamped)
        return;
    mCanvasHeight = clamped;
    emit canvasHeightChanged();
}

void NewProjectViewModel::setLiveLink(bool aLiveLink) {
    if (mLiveLink == aLiveLink)
        return;
    mLiveLink = aLiveLink;
    emit liveLinkChanged();
}

void NewProjectViewModel::browse() { emit browseRequested(); }

void NewProjectViewModel::accept() { emit acceptRequested(); }

void NewProjectViewModel::cancel() { emit cancelRequested(); }

} // namespace gui
