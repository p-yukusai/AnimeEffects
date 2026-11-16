#include "gui/KeyBindingDialog.h"

#include "gui/KeyCommandMap.h"
#include "gui/qml/KeyBindingViewModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/theme/Colors.h"

#include <QApplication>
#include <QMessageBox>
#include <QQuickItem>
#include <QQuickWidget>
#include <QQmlContext>
#include <QScreen>
#include <QSettings>
#include <QUrl>
#include <QVBoxLayout>

namespace gui {

namespace {
// The window HUGS the scene: the QML root's implicitHeight is the tab row plus the
// tallest group's rows plus the footer band, and the constructor sizes the dialog to
// it. The floor is for a screen shorter than a full tab (the list's own scroll host
// covers the rest), and the width fits the longest command label beside its field.
constexpr int kDefaultWidth = 560;
constexpr int kMinimumWidth = 480;
constexpr int kMinimumHeight = 320;
} // namespace

KeyBindingDialog::KeyBindingDialog(KeyCommandMap& aMap, QWidget* aParent)
    : QDialog(aParent), mKeyCommandMap(aMap) {
    setObjectName(QStringLiteral("keyBindingWidget"));
    setWindowTitle(tr("Key Bindings"));
    // A native framed window, not a frameless popup: normal title bar, movable and
    // resizable by the window manager, and modal over the main window.
    setModal(true);
    setSizeGripEnabled(true);

    mModel = new KeyBindingViewModel(this);
    // Snapshot the map into plain rows BEFORE the scene is built: it needs one row
    // per command, and the model must never see the map (it pulls in QtWidgets).
    for (auto command : mKeyCommandMap.commands()) {
        if (!command)
            continue;
        mModel->addRow(command->key, command->group, command->label, command->binding, command->defaultBinding);
    }

    mTheme = new QmlTheme(this);

    mQuick = new QQuickWidget(this);
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    // The theme and the model are context properties, exactly as the new-project and
    // export shells do it: the scene reads `theme.base` and `keyBindingModel.groups`
    // with no C++ type registration. NOT "model": every Repeater and ListView has a
    // `model` property of its own, so a scene binding would resolve the right-hand
    // side to that shadowing property.
    mQuick->rootContext()->setContextProperty(QStringLiteral("keyBindingModel"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // The scene's backdrop: the theme's own window colour, so a resize does not flash
    // white.
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/KeyBindingDialog.qml")));

    // Size to the scene, now that it exists: the form's height is a fact the scene
    // reports through its root's implicitHeight (the tab row + the tallest group's
    // rows + the footer), so the window opens without empty space below the last
    // command. SizeRootObjectToView does not overwrite implicitHeight. Both
    // dimensions yield to the screen, and the list's scroll host covers a screen
    // shorter than the tallest tab.
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
    connect(mModel, &KeyBindingViewModel::acceptRequested, this, [this]() {
        // The one route that keeps what the user chose, and it applies in the widget
        // form's order: flush every row into the map, then persist.
        saveSettings();
        accept();
    });
    connect(mModel, &KeyBindingViewModel::cancelRequested, this, &QDialog::reject);
    connect(mModel, &KeyBindingViewModel::resetRequested, this, [this]() {
        // The confirmation is a WIDGET dialog (QMessageBox) and stays one: the reset
        // discards every custom chord at once, so it asks first. The words are the
        // widget form's own tr() entries, so no new catalog context is introduced.
        const auto answer = QMessageBox::question(
            this, tr("Reset keybindings"),
            tr("Reset all keybindings to their default values? This will discard any custom bindings."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer == QMessageBox::Yes)
            mModel->resetAll();
    });
}

KeyBindingDialog::~KeyBindingDialog() {
    // Tear the scene down before the model/theme context objects die, so the scene's
    // binding teardown never re-reads a destroyed context property (the same rule the
    // new-project and export shells follow).
    delete mQuick;
    mQuick = nullptr;
}

void KeyBindingDialog::saveSettings() {
    // Only the rows the form still holds are written back, so a row that was never
    // touched keeps the binding the map already had.
    for (int i = 0; i < mModel->count(); ++i) {
        if (auto* command = mKeyCommandMap.get(mModel->keyAt(i)))
            command->binding = mModel->bindingAt(i);
    }

    QSettings settings(
        QSettings::IniFormat, QSettings::UserScope, QApplication::organizationName(), QApplication::applicationName()
    );
    settings.beginGroup("keybindings");
    mKeyCommandMap.writeTo(settings);
    settings.endGroup();
}

} // namespace gui
