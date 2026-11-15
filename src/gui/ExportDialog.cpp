#include "gui/ExportDialog.h"

#include "gui/DestinationPicker.h"
#include "gui/GUIResources.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QQuickWidget>
#include <QQmlContext>
#include <QScreen>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace gui {

namespace {
// The dialog's own size. It opens at a comfortable size and is resizable, with a
// floor low enough for a small laptop screen.
constexpr int kDefaultWidth = 620;
constexpr int kDefaultHeight = 720;
constexpr int kMinimumWidth = 560;
constexpr int kMinimumHeight = 640;
} // namespace

ExportDialog::ExportDialog(const ExportViewModel::Init& aInit, GUIResources& aResources,
                           QWidget* aParent)
    : QDialog(aParent), mResources(aResources) {
    setObjectName(QStringLiteral("exportWidget"));
    setWindowTitle(tr("Export parameters"));
    // A native framed window, not a frameless popup: normal title bar, movable and
    // resizable by the window manager, and modal over the main window.
    setModal(true);
    setSizeGripEnabled(true);
    // The floor is "low enough for a small laptop screen" - and on a screen
    // SHORTER than the floor the minimum itself pushes the action row off the
    // screen (D25: the row must stay reachable). So the floor yields to the
    // screen: the scene scrolls its content at any size below natural (D25's
    // own rule), so a screen-clamped minimum costs the form nothing it does not
    // already handle. Interpretation flagged in the increment's report: the
    // line names "small screen" as a cause of content not fitting; this is the
    // shell's half of that clause.
    const QRect avail = screen() ? screen()->availableGeometry() : QRect();
    const int maxW = avail.width() > 0 ? avail.width() : kDefaultWidth;
    const int maxH = avail.height() > 0 ? avail.height() : kDefaultHeight;
    // A5 (decided 2026-09-30): the dialog restores its last size across sessions;
    // the first open uses the designed default. A remembered size clamps to the
    // available screen exactly like the default does, and Qt clamps it up to the
    // minimum floor on resize.
    const QSize remembered = QSettings().value(QStringLiteral("exportdialog/size")).toSize();
    const QSize wanted = remembered.isValid() && remembered.width() > 0
                             ? remembered
                             : QSize(kDefaultWidth, kDefaultHeight);
    setMinimumSize(qMin(kMinimumWidth, maxW), qMin(kMinimumHeight, maxH));
    resize(qMin(wanted.width(), maxW), qMin(wanted.height(), maxH));

    mModel = new ExportViewModel(aInit, this);
    mTheme = new QmlTheme(this);

    mQuick = new QQuickWidget(this);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // The theme and the model are context properties, exactly as the animation
    // settings dialog does it: the scene reads `theme.text` and `model.destinationId`
    // with no C++ type registration.
    // NOT "model": every Repeater and ListView has a `model` property of its own, so a
    // scene binding `Repeater { model: model.facts }` resolves the right-hand side to
    // the Repeater's own (still unset) property and silently creates ZERO delegates.
    // That is exactly how the key-facts summary and the frame-range rows rendered as
    // empty space while `model.facts.count` read 7.
    mQuick->rootContext()->setContextProperty(QStringLiteral("exportModel"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // The scene's backdrop: the theme's own window colour, so a resize does not
    // flash white.
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/ExportDialog.qml")));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(mQuick);

    // The scene's buttons do not know about QDialog, and they should not: the model
    // carries the outcome, and the shell turns the request into the dialog result.
    connect(mModel, &ExportViewModel::acceptRequested, this, &QDialog::accept);
    connect(mModel, &ExportViewModel::cancelRequested, this, &QDialog::reject);
    connect(mModel, &ExportViewModel::changeDestinationRequested, this, [this]() {
        // The picker is a WIDGET dialog (gui/DestinationPicker) and stays one: it is
        // a searchable list with its own accessibility names that the GUI probes
        // already address. The scene asks for it; the shell answers with its id.
        DestinationPicker picker(mResources, mModel->destinationId(), this);
        if (picker.exec() != QDialog::Accepted)
            return;
        mModel->setDestinationId(picker.chosenId());
    });
    connect(mModel, &ExportViewModel::paletteRequested, this, &ExportDialog::choosePalette);
}

ExportDialog::~ExportDialog() {
    // Tear the scene down before the model/theme context objects die, so the scene's
    // binding teardown never re-reads a destroyed context property (the same rule the
    // animation settings shell follows).
    delete mQuick;
    mQuick = nullptr;
}

void ExportDialog::done(int aResult) {
    // A5: the size the dialog is closed at is the size it returns at. Saved on
    // every close route (accept, reject, Esc, the window button) rather than on
    // resize-end: the user's final size is what "remembers its size" means, and a
    // half-resized transient state is never the choice.
    QSettings().setValue(QStringLiteral("exportdialog/size"), size());
    QDialog::done(aResult);
}

void ExportDialog::choosePalette() {
    const QString start = mModel->palettePath();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Palette"), start.isEmpty() ? QString() : QFileInfo(start).absolutePath(),
        tr("FFmpeg palette file (*.png)"));
    if (path.isEmpty())
        return;
    mModel->setPalettePath(path);
}

} // namespace gui
