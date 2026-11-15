#ifndef GUI_QML_PROJECTSETTINGVIEWMODEL_H
#define GUI_QML_PROJECTSETTINGVIEWMODEL_H

#include <QObject>
#include <QString>

namespace gui {

// Everything the project-settings dialog decides, with no widgets, no GL and no
// scene.
//
// This is the port of the form that used to be built inside gui/MainMenuBar.cpp
// out of a QFormLayout, four QSpinBoxes and a QCheckBox. The values are the same
// as the ones the widget form opened on, and the two rules the form held are
// unchanged:
//
//   * a canvas dimension is at least 1 and at most the device's ceiling
//     (min(maxTextureSize, maxRenderBufferSize), which the shell reads from GL and
//     sets here before seeding);
//   * the frame range never inverts - the widget form linked its two spin boxes
//     (start->setMaximum(end), end->setMinimum(start)), and the setter enforces the
//     same invariant for every writer.
//
// The model is deliberately widget-free and Qt6::Core only. It must NOT pull in
// core/Project.h: that header drags gui/AudioPlaybackWidget.h (and through it the
// widget stack) into anything that links this file, which would make the headless
// test that constructs this class link the GUI. The shell reads the project's
// attribute and writes plain numbers here; the model never names a core type.
//
// It is set as the QML context property "projectSettingModel", NOT "model": a
// Repeater (and a ListView) has a `model` property of its own, so a scene binding
// `something: model.fps` would resolve the right-hand side to that shadowing
// property.
class ProjectSettingViewModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(int canvasWidth READ canvasWidth WRITE setCanvasWidth NOTIFY canvasWidthChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight WRITE setCanvasHeight NOTIFY canvasHeightChanged)
    // Read-only to QML: the shell narrows it to the device's real ceiling before it
    // seeds the canvas size, and no field writes it back.
    Q_PROPERTY(int maxCanvasSize READ maxCanvasSize NOTIFY maxCanvasSizeChanged)
    // 1-indexed inclusive project extent; either bound may be negative (pre-roll
    // frames before 0 are a valid project extent).
    Q_PROPERTY(int frameStart READ frameStart WRITE setFrameStart NOTIFY frameStartChanged)
    Q_PROPERTY(int frameEnd READ frameEnd WRITE setFrameEnd NOTIFY frameEndChanged)
    Q_PROPERTY(bool loop READ loop WRITE setLoop NOTIFY loopChanged)
    Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged)
    // The viewport background override as its "#rrggbb" spelling. The EMPTY string
    // is "follow the theme", which is what every project did before the override
    // existed; the shell writes a colour here only when the user chose one. The
    // model cannot name a QColor - see the header rule above - so the colour
    // travels as its hex spelling and the setter normalizes it.
    Q_PROPERTY(QString viewportBackgroundHex READ viewportBackgroundHex WRITE setViewportBackgroundHex NOTIFY viewportBackgroundHexChanged)
    // Read-only: the box that puts the override back on the theme. It is derived
    // from the hex (empty == theme), so it has its own notify: a control bound to
    // it re-reads the flag when the emptiness flips, not on every colour edit.
    Q_PROPERTY(bool viewportBackgroundIsThemeDefault READ viewportBackgroundIsThemeDefault NOTIFY viewportBackgroundIsThemeDefaultChanged)

public:
    explicit ProjectSettingViewModel(QObject* aParent = nullptr);

    int canvasWidth() const { return mCanvasWidth; }
    int canvasHeight() const { return mCanvasHeight; }
    int maxCanvasSize() const { return mMaxCanvasSize; }
    int frameStart() const { return mFrameStart; }
    int frameEnd() const { return mFrameEnd; }
    bool loop() const { return mLoop; }
    int fps() const { return mFps; }
    QString viewportBackgroundHex() const { return mViewportBackgroundHex; }
    bool viewportBackgroundIsThemeDefault() const { return mViewportBackgroundHex.isEmpty(); }

    // Every setter clamps rather than trusting its caller (a scrub, a typed edit, a
    // stale binding, a test): the same floors/ceilings the widget QSpinBoxes had,
    // stated once here instead of in each control.
    void setCanvasWidth(int aWidth);
    void setCanvasHeight(int aHeight);
    // Not a QML writer: the shell sets the device ceiling from ViaPoint's GL info
    // before it seeds the canvas size.
    void setMaxCanvasSize(int aMaxSize);
    void setFrameStart(int aStart);
    void setFrameEnd(int aEnd);
    void setLoop(bool aLoop);
    void setFps(int aFps);
    // Normalizes an RGB hex spelling to "#rrggbb" (case folded, "#abc" expanded) and
    // refuses anything else by keeping the value already held. The EMPTY string is
    // not garbage: it clears the override back to the theme default.
    void setViewportBackgroundHex(const QString& aHex);

    // The scene's third verb: ask the shell to open its colour chooser. The model
    // cannot open one (widget-free), so it only reports the request; the shell
    // answers by writing setViewportBackgroundHex().
    Q_INVOKABLE void requestViewportBackgroundPick();

    // The scene's two verbs. The shell turns them into the QDialog's result.
    Q_INVOKABLE void accept();
    Q_INVOKABLE void cancel();

signals:
    void canvasWidthChanged();
    void canvasHeightChanged();
    void maxCanvasSizeChanged();
    void frameStartChanged();
    void frameEndChanged();
    void loopChanged();
    void fpsChanged();
    void viewportBackgroundHexChanged();
    void viewportBackgroundIsThemeDefaultChanged();

    void acceptRequested();
    void cancelRequested();
    void viewportBackgroundPickRequested();

private:
    // The floor until the shell seeds the project's own size: this dialog only ever
    // edits an existing project, and a bare model must still satisfy the
    // 1..maxCanvasSize invariant.
    int mCanvasWidth = 1;
    int mCanvasHeight = 1;
    // Open until the shell narrows it to the device's real ceiling (the widget form
    // capped its canvas spins at min(maxTextureSize, maxRenderBufferSize) - only the
    // shell can ask GL).
    int mMaxCanvasSize = 2147483647;
    // The same defaults core::Project::Attribute's constructor gives a project
    // (Range(1, 600) at 24 fps, loop off), so a bare model describes a default
    // project rather than an arbitrary one.
    int mFrameStart = 1;
    int mFrameEnd = 600;
    bool mLoop = false;
    int mFps = 24;
    // Empty until the shell seeds a project's override (or the user chooses one);
    // empty IS the theme default. Not a QColor: see the header rule.
    QString mViewportBackgroundHex;
};

} // namespace gui

#endif // GUI_QML_PROJECTSETTINGVIEWMODEL_H
