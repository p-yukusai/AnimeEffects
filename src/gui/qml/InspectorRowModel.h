#ifndef GUI_QML_INSPECTORROWMODEL_H
#define GUI_QML_INSPECTORROWMODEL_H

#include <QColor>
#include <QObject>
#include <QString>

#include "core/TimeKeyType.h"

namespace core { class TimeLine; }

namespace gui {

class InspectorViewModel;

// One keyable property row. The row owns WHAT to show and HOW to write it; the
// QML owns where it sits. Values are the same three-state model the widget
// inspector used:
//   static   - no explicit keys; edits write the default key
//   animated - keys exist elsewhere; this frame is evaluated between them
//   keyed    - this frame owns an explicit key
class InspectorRowModel: public QObject {
    Q_OBJECT
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool keyEnabled READ keyEnabled NOTIFY changed)
    Q_PROPERTY(QString keyState READ keyState NOTIFY changed)
    Q_PROPERTY(QColor keyColor READ keyColor NOTIFY changed)

    // Move / Image both expose their primary pair; unused values stay zero.
    Q_PROPERTY(double x READ x NOTIFY changed)
    Q_PROPERTY(double y READ y NOTIFY changed)
    Q_PROPERTY(double centroidX READ centroidX NOTIFY changed)
    Q_PROPERTY(double centroidY READ centroidY NOTIFY changed)
    Q_PROPERTY(bool smooth READ smooth NOTIFY changed)
    Q_PROPERTY(double angle READ angle NOTIFY changed)
    Q_PROPERTY(double scaleX READ scaleX NOTIFY changed)
    Q_PROPERTY(double scaleY READ scaleY NOTIFY changed)
    Q_PROPERTY(double depth READ depth NOTIFY changed)
    Q_PROPERTY(double opacity READ opacity NOTIFY changed)
    Q_PROPERTY(QString assetName READ assetName NOTIFY changed)
    Q_PROPERTY(double centerX READ centerX NOTIFY changed)
    Q_PROPERTY(double centerY READ centerY NOTIFY changed)
    Q_PROPERTY(int cellSize READ cellSize NOTIFY changed)

public:
    enum class Kind { Move, Rotate, Scale, Depth, Opacity, Image, Pose, FFD };
    Q_ENUM(Kind)

    InspectorRowModel(Kind aKind, InspectorViewModel& aOwner, QObject* aParent = nullptr);

    QString kind() const;
    QString label() const;
    bool visible() const { return mVisible; }
    bool enabled() const { return mEnabled; }
    bool keyEnabled() const { return mKeyEnabled; }
    QString keyState() const { return mKeyState; }
    QColor keyColor() const { return mKeyColor; }

    double x() const { return mX; }
    double y() const { return mY; }
    double centroidX() const { return mCentroidX; }
    double centroidY() const { return mCentroidY; }
    bool smooth() const { return mSmooth; }
    double angle() const { return mAngle; }
    double scaleX() const { return mScaleX; }
    double scaleY() const { return mScaleY; }
    double depth() const { return mDepth; }
    double opacity() const { return mOpacity; }
    QString assetName() const { return mAssetName; }
    double centerX() const { return mCenterX; }
    double centerY() const { return mCenterY; }
    int cellSize() const { return mCellSize; }

    void refresh();
    void refreshTheme();

    Q_INVOKABLE void toggleKey();
    Q_INVOKABLE void browseAsset();

    Q_INVOKABLE void setX(double aValue);
    Q_INVOKABLE void setY(double aValue);
    Q_INVOKABLE void setCentroidX(double aValue);
    Q_INVOKABLE void setCentroidY(double aValue);
    Q_INVOKABLE void setSmooth(bool aValue);
    Q_INVOKABLE void setAngle(double aValue);
    Q_INVOKABLE void setScaleX(double aValue);
    Q_INVOKABLE void setScaleY(double aValue);
    Q_INVOKABLE void setDepth(double aValue);
    Q_INVOKABLE void setOpacity(double aValue);
    Q_INVOKABLE void setCenterX(double aValue);
    Q_INVOKABLE void setCenterY(double aValue);
    Q_INVOKABLE void setCellSize(int aValue);

signals:
    void changed();

private:
    core::TimeKeyType type() const;
    void knock();
    void readMove(core::TimeLine& aLine, int aFrame);
    void readImage(core::TimeLine& aLine, int aFrame);

    InspectorViewModel& mOwner;
    Kind mKind;
    bool mVisible = false;
    bool mEnabled = false;
    bool mKeyEnabled = false;
    QString mKeyState;
    QColor mKeyColor;

    double mX = 0.0;
    double mY = 0.0;
    double mCentroidX = 0.0;
    double mCentroidY = 0.0;
    bool mSmooth = false;
    double mAngle = 0.0;
    double mScaleX = 1.0;
    double mScaleY = 1.0;
    double mDepth = 0.0;
    double mOpacity = 1.0;
    QString mAssetName;
    double mCenterX = 0.0;
    double mCenterY = 0.0;
    int mCellSize = 0;
};

} // namespace gui

#endif // GUI_QML_INSPECTORROWMODEL_H
