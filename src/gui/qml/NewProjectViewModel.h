#ifndef GUI_QML_NEWPROJECTVIEWMODEL_H
#define GUI_QML_NEWPROJECTVIEWMODEL_H

#include <QObject>
#include <QString>

namespace gui {

// Everything the new-project dialog decides, with no widgets, no GL and no scene.
//
// This is the port of the form that used to be built in gui/NewProjectDialog.cpp
// out of a QFormLayout, three QSpinBoxes, a QCheckBox and a read-only QLineEdit.
// The values are the same and their defaults are the same (600 frames at 24 fps,
// a 512x512 canvas, no canvas override), and the one rule the widget form held is
// unchanged: a frame count / fps / canvas dimension is at least 1, so a setter
// clamps rather than trusting its caller.
//
// The model is deliberately widget-free and Qt6::Core only. It must NOT pull in
// core/Project.h: that header drags gui/AudioPlaybackWidget.h (and through it the
// widget stack) into anything that links this file, which would make the headless
// test that constructs this class link the GUI. The shell turns the model's plain
// numbers into a core::Project::Attribute; the model never names that type.
//
// It is set as the QML context property "newProjectModel", NOT "model": a
// Repeater (and a ListView) has a `model` property of its own, so a scene binding
// `something: model.value` would resolve the right-hand side to that shadowing
// property. Nothing here repeats, but the name keeps the rule uniform with the
// export form.
class NewProjectViewModel : public QObject {
    Q_OBJECT

    // The chosen asset path, and the leaf its field shows. `fileNameLabel` is a
    // derived read (QFileInfo(fileName).fileName()) rather than a second property
    // the scene has to keep in sync: it notifies with fileNameChanged().
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY fileNameChanged)
    Q_PROPERTY(QString fileNameLabel READ fileNameLabel NOTIFY fileNameChanged)

    Q_PROPERTY(int frameCount READ frameCount WRITE setFrameCount NOTIFY frameCountChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    Q_PROPERTY(bool specifiesCanvasSize READ specifiesCanvasSize WRITE setSpecifiesCanvasSize NOTIFY specifiesCanvasSizeChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth WRITE setCanvasWidth NOTIFY canvasWidthChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight WRITE setCanvasHeight NOTIFY canvasHeightChanged)
    // The live-link opt-in: when on, a newly created project monitors its asset
    // for on-disk changes and reloads it. The shell exposes the state; the
    // wiring that acts on it lives with the project machinery, not here.
    Q_PROPERTY(bool liveLink READ liveLink WRITE setLiveLink NOTIFY liveLinkChanged)

public:
    explicit NewProjectViewModel(QObject* aParent = nullptr);

    QString fileName() const { return mFileName; }
    QString fileNameLabel() const;
    int frameCount() const { return mFrameCount; }
    int fps() const { return mFps; }
    bool specifiesCanvasSize() const { return mSpecifiesCanvasSize; }
    int canvasWidth() const { return mCanvasWidth; }
    int canvasHeight() const { return mCanvasHeight; }
    bool liveLink() const { return mLiveLink; }

    void setFileName(const QString& aFileName);
    // These four clamp to >= 1 (the widget form's QSpinBox floor, stated once
    // here rather than in each control).
    void setFrameCount(int aFrameCount);
    void setFps(int aFps);
    void setSpecifiesCanvasSize(bool aSpecifies);
    void setCanvasWidth(int aWidth);
    void setCanvasHeight(int aHeight);
    void setLiveLink(bool aLiveLink);

    // The scene's three verbs. The file chooser is a WIDGET dialog the shell owns,
    // so browse() only asks for it; the shell answers and writes fileName back.
    Q_INVOKABLE void browse();
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();

signals:
    void fileNameChanged();
    void frameCountChanged();
    void fpsChanged();
    void specifiesCanvasSizeChanged();
    void canvasWidthChanged();
    void canvasHeightChanged();
    void liveLinkChanged();

    void browseRequested();
    void acceptRequested();
    void cancelRequested();

private:
    QString mFileName;
    int mFrameCount = 600;
    int mFps = 24;
    bool mSpecifiesCanvasSize = false;
    int mCanvasWidth = 512;
    int mCanvasHeight = 512;
    bool mLiveLink = false;
};

} // namespace gui

#endif // GUI_QML_NEWPROJECTVIEWMODEL_H
