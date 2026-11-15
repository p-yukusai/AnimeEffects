#ifndef GUI_QML_VIEWSETTINGSMODEL_H
#define GUI_QML_VIEWSETTINGSMODEL_H

#include <QObject>
#include <QString>

namespace gui {

class MainViewSetting;

// The header's viewport controls - the six verbs that used to sit in the Tools
// dock's "View Settings" strip, now in the window header.
//
// It keeps the exact semantics the dock had, including the part that is easy to get
// wrong: the three rotation verbs are TRIGGERS, not latched states. The flag is
// raised, announced, and lowered again in the same call because
// MainDisplayWidget::onViewSettingChanged consumes it once; a flag left on would
// rotate the canvas again on every later view change. The two drawing toggles and
// the flip are real state and stay latched.
//
// The key bindings are read from the application's key map by the host and handed
// in, so the tooltips name the user's actual keys (every one of them is rebindable).
class ViewSettingsModel: public QObject {
    Q_OBJECT
    Q_PROPERTY(bool showMesh READ showMesh WRITE setShowMesh NOTIFY changed)
    Q_PROPERTY(bool cutOutsideFrame READ cutOutsideFrame WRITE setCutOutsideFrame NOTIFY changed)
    Q_PROPERTY(bool flipCanvas READ flipCanvas WRITE setFlipCanvas NOTIFY changed)
    Q_PROPERTY(QString showMeshTip READ showMeshTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString cutOutsideFrameTip READ cutOutsideFrameTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString rotateCounterClockwiseTip READ rotateCounterClockwiseTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString resetRotationTip READ resetRotationTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString rotateClockwiseTip READ rotateClockwiseTip NOTIFY keyHintsChanged)
    Q_PROPERTY(QString flipCanvasTip READ flipCanvasTip NOTIFY keyHintsChanged)
public:
    explicit ViewSettingsModel(MainViewSetting* aSetting, QObject* aParent = nullptr);

    bool showMesh() const;
    bool cutOutsideFrame() const;
    bool flipCanvas() const;

    void setShowMesh(bool aValue);
    void setCutOutsideFrame(bool aValue);
    void setFlipCanvas(bool aValue);

    QString showMeshTip() const;
    QString cutOutsideFrameTip() const;
    QString rotateCounterClockwiseTip() const;
    QString resetRotationTip() const;
    QString rotateClockwiseTip() const;
    QString flipCanvasTip() const;

    // Re-announce the underlying MainViewSetting. Another owner can write it
    // directly (the FFD tool's autoshow-mesh convenience does); the scene's check
    // states are bindings to THIS model's properties, so a direct write needs one
    // notification to be seen.
    void refresh();

    // The three live bindings, as the key map spells them (possibly empty).
    void setKeyHints(const QString& aCounterClockwise, const QString& aReset, const QString& aClockwise);

    // The trigger verbs. Each raises its flag for exactly one announcement.
    Q_INVOKABLE void rotateCounterClockwise();
    Q_INVOKABLE void resetRotation();
    Q_INVOKABLE void rotateClockwise();

signals:
    // One of the latched toggles moved: the scene re-reads its check state.
    void changed();
    // The MainViewSetting changed in a way the viewport must consume now (a toggle
    // or one of the trigger pulses).
    void viewSettingChanged();
    // A key binding was re-spelled, so every tooltip that names one re-reads.
    void keyHintsChanged();

private:
    MainViewSetting* mSetting = nullptr;
    QString mRotateCounterClockwise;
    QString mResetRotation;
    QString mRotateClockwise;
};

} // namespace gui

#endif // GUI_QML_VIEWSETTINGSMODEL_H
