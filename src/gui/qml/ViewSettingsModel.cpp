#include "gui/qml/ViewSettingsModel.h"

#include "gui/MainViewSetting.h"

namespace gui {

//-------------------------------------------------------------------------------------------------
ViewSettingsModel::ViewSettingsModel(MainViewSetting* aSetting, QObject* aParent):
    QObject(aParent),
    mSetting(aSetting) {
}

bool ViewSettingsModel::showMesh() const {
    return mSetting && mSetting->showLayerMesh;
}

bool ViewSettingsModel::cutOutsideFrame() const {
    return mSetting && mSetting->cutImagesByTheFrame;
}

bool ViewSettingsModel::flipCanvas() const {
    return mSetting && mSetting->flipCanvas;
}

void ViewSettingsModel::setShowMesh(bool aValue) {
    if (!mSetting || mSetting->showLayerMesh == aValue) return;
    mSetting->showLayerMesh = aValue;
    emit changed();
    emit viewSettingChanged();
}

void ViewSettingsModel::setCutOutsideFrame(bool aValue) {
    if (!mSetting || mSetting->cutImagesByTheFrame == aValue) return;
    mSetting->cutImagesByTheFrame = aValue;
    emit changed();
    emit viewSettingChanged();
}

void ViewSettingsModel::setFlipCanvas(bool aValue) {
    if (!mSetting || mSetting->flipCanvas == aValue) return;
    mSetting->flipCanvas = aValue;
    emit changed();
    emit viewSettingChanged();
}

QString ViewSettingsModel::showMeshTip() const {
    return tr("Show mesh");
}

QString ViewSettingsModel::cutOutsideFrameTip() const {
    return tr("Do not draw outside the frame");
}

QString ViewSettingsModel::rotateCounterClockwiseTip() const {
    return tr("Rotate the canvas counterclockwise (%1)").arg(mRotateCounterClockwise);
}

QString ViewSettingsModel::resetRotationTip() const {
    return tr("Reset rotation of the canvas (%1)").arg(mResetRotation);
}

QString ViewSettingsModel::rotateClockwiseTip() const {
    return tr("Rotate the canvas clockwise (%1)").arg(mRotateClockwise);
}

QString ViewSettingsModel::flipCanvasTip() const {
    return tr("Flip canvas");
}

void ViewSettingsModel::refresh() {
    emit changed();
}

void ViewSettingsModel::setKeyHints(const QString& aCounterClockwise,
                                    const QString& aReset,
                                    const QString& aClockwise) {
    if (mRotateCounterClockwise == aCounterClockwise && mResetRotation == aReset
        && mRotateClockwise == aClockwise)
        return;
    mRotateCounterClockwise = aCounterClockwise;
    mResetRotation = aReset;
    mRotateClockwise = aClockwise;
    emit keyHintsChanged();
}

// The three rotation verbs are TRIGGERS, not states: the setting is a flag the main
// view consumes on the same call (MainDisplayWidget::onViewSettingChanged), which is
// why it is raised, announced and lowered again - the deleted ViewPanel did exactly
// this and a latched flag would rotate on every later view change.
void ViewSettingsModel::rotateCounterClockwise() {
    if (!mSetting) return;
    mSetting->rotateViewACW = true;
    emit viewSettingChanged();
    mSetting->rotateViewACW = false;
}

void ViewSettingsModel::resetRotation() {
    if (!mSetting) return;
    mSetting->resetRotateView = true;
    emit viewSettingChanged();
    mSetting->resetRotateView = false;
}

void ViewSettingsModel::rotateClockwise() {
    if (!mSetting) return;
    mSetting->rotateViewCW = true;
    emit viewSettingChanged();
    mSetting->rotateViewCW = false;
}

} // namespace gui
