#include "gui/NewProjectDialog.h"

#include "gui/qml/NewProjectViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"

#include <QDir>
#include <QFileDialog>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>

namespace gui {

namespace {
// The window HUGS the scene: the QML root's implicitHeight is the natural height of
// the rows plus the footer band, and the constructor sizes the dialog to it. The
// floor is small for the same reason - the form is a short, fixed set of rows, and a
// tall fixed default (the old 430) left ~130px of empty space under the last one.
constexpr int kDefaultWidth = 460;
constexpr int kMinimumWidth = 400;
constexpr int kMinimumHeight = 180;
} // namespace

NewProjectDialog::NewProjectDialog(QWidget* aParent) : QDialog(aParent) {
    setObjectName(QStringLiteral("newProjectWidget"));
    setWindowTitle(tr("New Project"));
    // A native framed window, not a frameless popup: normal title bar, movable and
    // resizable by the window manager, and modal over the main window.
    setModal(true);
    setSizeGripEnabled(true);

    mModel = new NewProjectViewModel(this);
    mTheme = new QmlTheme(this);

    mQuick = new QQuickWidget(this);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // The theme and the model are context properties, exactly as the export shell
    // does it: the scene reads `theme.base` and `newProjectModel.frameCount` with
    // no C++ type registration. NOT "model": every Repeater and ListView has a
    // `model` property of its own, so a scene binding would resolve the right-hand
    // side to that shadowing property.
    mQuick->rootContext()->setContextProperty(QStringLiteral("newProjectModel"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // The scene's backdrop: the theme's own window colour, so a resize does not
    // flash white.
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/NewProjectDialog.qml")));

    // Size to the scene, now that it exists: the form is a fixed set of rows, so the
    // window opens at exactly their height rather than at a guessed default with
    // empty space below. The scene reports that height through the root's
    // implicitHeight (see the QML); SizeRootObjectToView does not overwrite it. Both
    // dimensions yield to the screen, and the scene's own scroll host covers a screen
    // shorter than the rows.
    const QRect avail = screen() ? screen()->availableGeometry() : QRect();
    const int maxW = avail.width() > 0 ? avail.width() : kDefaultWidth;
    const int maxH = avail.height() > 0 ? avail.height() : kMinimumHeight;
    int wantedH = kMinimumHeight;
    if (auto* scene = mQuick->rootObject()) {
        const int natural = qRound(scene->implicitHeight());
        if (natural > 0)
            wantedH = natural;
    }
    setMinimumSize(qMin(kMinimumWidth, maxW), qMin(kMinimumHeight, maxH));
    resize(qMin(kDefaultWidth, maxW), qMin(wantedH, maxH));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mQuick);

    // The scene's buttons do not know about QDialog, and they should not: the model
    // carries the outcome, and the shell turns the request into the dialog result.
    connect(mModel, &NewProjectViewModel::acceptRequested, this, &QDialog::accept);
    connect(mModel, &NewProjectViewModel::cancelRequested, this, &QDialog::reject);
    connect(mModel, &NewProjectViewModel::browseRequested, this, [this]() {
        // The file chooser is a WIDGET dialog (the platform's own) and stays one:
        // the scene asks for it, the shell answers with a path. The filter is the
        // same one the widget form had, and the strings are the EXISTING C++ tr()
        // entries of this class, so no new catalog context is introduced.
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Open File"), QDir::homePath(),
            "ImageFile (*.psd *.ora *.jpg *.jpeg *.png *.gif *.tiff *.tif *.webp)");
        if (!path.isEmpty())
            mModel->setFileName(path);
    });
}

NewProjectDialog::~NewProjectDialog() {
    // Tear the scene down before the model/theme context objects die, so the scene's
    // binding teardown never re-reads a destroyed context property (the same rule the
    // export and animation-settings shells follow).
    delete mQuick;
    mQuick = nullptr;
}

QString NewProjectDialog::fileName() const { return mModel->fileName(); }

core::Project::Attribute NewProjectDialog::attribute() const {
    core::Project::Attribute attribute;
    attribute.setFrameRange(util::Range(1, mModel->frameCount()));
    attribute.setFps(mModel->fps());
    attribute.setImageSize(QSize(mModel->canvasWidth(), mModel->canvasHeight()));
    return attribute;
}

bool NewProjectDialog::specifiesCanvasSize() const { return mModel->specifiesCanvasSize(); }

bool NewProjectDialog::liveLink() const { return mModel->liveLink(); }

} // namespace gui
