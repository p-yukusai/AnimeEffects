#include "gui/ProjectSettingDialog.h"

#include "gui/ViaPoint.h"
#include "gui/qml/ProjectSettingViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"
#include "XC.h"

#include <QColorDialog>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>

namespace gui {

namespace {
// The window HUGS the scene: the QML root's implicitHeight is the natural height of
// the rows plus the footer band, and the constructor sizes the dialog to it. The
// floor is small for the same reason - the form is four rows, and a tall fixed
// default (the old fixSize()) left empty space under the last one.
constexpr int kDefaultWidth = 460;
constexpr int kMinimumWidth = 400;
constexpr int kMinimumHeight = 180;
} // namespace

ProjectSettingDialog::ProjectSettingDialog(ViaPoint& aViaPoint, core::Project& aProject,
                                         QWidget* aParent)
    : QDialog(aParent), mViaPoint(aViaPoint), mProject(aProject) {
    setObjectName(QStringLiteral("projectSettingWidget"));
    setWindowTitle(tr("Project settings"));
    // A native framed window, not a frameless popup: normal title bar, movable and
    // resizable by the window manager, and modal over the main window.
    setModal(true);
    setSizeGripEnabled(true);

    mModel = new ProjectSettingViewModel(this);
    mTheme = new QmlTheme(this);

    // The device's hard ceiling for a canvas dimension, the same number the widget
    // form gave its size spins: the smaller of the GL max texture and max renderbuffer
    // sizes. The model is told it FIRST, so the seed below clamps the project's
    // current size against it rather than trusting the file.
    {
        const auto devInfo = mViaPoint.glDeviceInfo();
        const int maxBufferSize = std::min(devInfo.maxTextureSize, devInfo.maxRenderBufferSize);
        XC_ASSERT(maxBufferSize > 0);
        mModel->setMaxCanvasSize(maxBufferSize);
    }

    // Seed from the project's own attribute: the shell is the only side that knows
    // core::Project, and the model carries the plain numbers from here on. The
    // getters below turn them back into the same values.
    const auto curSize = mProject.attribute().imageSize();
    const auto curRange = mProject.attribute().frameRange();
    mModel->setCanvasWidth(curSize.width());
    mModel->setCanvasHeight(curSize.height());
    // The END goes first: setFrameStart() keeps start <= end, so seeding a project
    // whose start already sits past the model's default end would otherwise pull that
    // start down to the default before the real end arrived.
    mModel->setFrameEnd(curRange.max());
    mModel->setFrameStart(curRange.min());
    mModel->setLoop(mProject.attribute().loop());
    mModel->setFps(mProject.attribute().fps());
    // The override when the project carries one, the empty string (theme default)
    // otherwise - which is what every file written before the override existed
    // reads as, and what the scene's "use theme background" box shows.
    mModel->setViewportBackgroundHex(mProject.attribute().hasViewportBackground()
                                         ? mProject.attribute().viewportBackground().name()
                                         : QString());

    mQuick = new QQuickWidget(this);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // The theme and the model are context properties, exactly as the export and
    // new-project shells do it: the scene reads `theme.base` and
    // `projectSettingModel.fps` with no C++ type registration. NOT "model": every
    // Repeater and ListView has a `model` property of its own, so a scene binding
    // would resolve the right-hand side to that shadowing property.
    mQuick->rootContext()->setContextProperty(QStringLiteral("projectSettingModel"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // The scene's backdrop: the theme's own window colour, so a resize does not
    // flash white.
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/ProjectSettingDialog.qml")));

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
    // carries the outcome, and the shell turns the request into the dialog result -
    // through the same confirmFrameRangeUpdating() gate the widget form's EasyDialog
    // OK callback used.
    connect(mModel, &ProjectSettingViewModel::acceptRequested, this, [this]() {
        if (confirmFrameRangeUpdating(frameRange()))
            accept();
    });
    connect(mModel, &ProjectSettingViewModel::cancelRequested, this, &QDialog::reject);
    connect(mModel, &ProjectSettingViewModel::viewportBackgroundPickRequested, this,
            &ProjectSettingDialog::chooseViewportBackground);
}

ProjectSettingDialog::~ProjectSettingDialog() {
    // Tear the scene down before the model/theme context objects die, so the scene's
    // binding teardown never re-reads a destroyed context property (the same rule the
    // export and new-project shells follow).
    delete mQuick;
    mQuick = nullptr;
}

QSize ProjectSettingDialog::canvasSize() const {
    return QSize(mModel->canvasWidth(), mModel->canvasHeight());
}

util::Range ProjectSettingDialog::frameRange() const {
    return util::Range(mModel->frameStart(), mModel->frameEnd());
}

bool ProjectSettingDialog::loop() const { return mModel->loop(); }

int ProjectSettingDialog::fps() const { return mModel->fps(); }

bool ProjectSettingDialog::viewportBackgroundIsThemeDefault() const {
    return mModel->viewportBackgroundIsThemeDefault();
}

QColor ProjectSettingDialog::viewportBackground() const {
    // Only meaningful when the override is set; the caller gates on
    // viewportBackgroundIsThemeDefault() first.
    return QColor(mModel->viewportBackgroundHex());
}

void ProjectSettingDialog::chooseViewportBackground() {
    // There is no colour picker anywhere else in the app; Qt's stock QColorDialog is
    // the shell's, exactly as the export dialog keeps its palette QFileDialog here
    // rather than in the scene. It opens on the colour in force - the override when
    // the project has one, the theme's canvas token (the same
    // GUIResources::viewportBackground() the app paints) when it follows the theme -
    // so the chooser starts where the swatch is.
    const QColor start = mModel->viewportBackgroundIsThemeDefault()
                             ? theme::Colors::current().recessed
                             : QColor(mModel->viewportBackgroundHex());
    const QColor chosen = QColorDialog::getColor(start, this, tr("Viewport background"));
    if (!chosen.isValid())
        return; // cancelled: the project keeps the colour it had
    mModel->setViewportBackgroundHex(chosen.name(QColor::HexRgb));
}

bool ProjectSettingDialog::confirmFrameRangeUpdating(const util::Range& aNewRange) const {
    XC_ASSERT(aNewRange.min() <= aNewRange.max()); // the linked fields keep start <= end
    // Keys may live outside the frame range - the range is a playback/export bound,
    // not a constraint on key positions - so every range change is valid.
    return true;
}

} // namespace gui
