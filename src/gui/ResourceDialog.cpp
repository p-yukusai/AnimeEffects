#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QMenuBar>
#include <QFileDialog>
#include <QMessageBox>
#include "img/PSDReader.h"
#include "img/PSDUtil.h"
#include "gui/ResourceDialog.h"
#include "gui/MainMenuBar.h"
#include "MainWindow.h"
#include "img/Util.h"

namespace gui {

ResourceDialog::ResourceDialog(ViaPoint& aViaPoint, bool aModal, QWidget* aParent):
    EasyDialog(tr("Project assets"), aParent, aModal), mViaPoint(aViaPoint), mProject(), mTree() {
    // menu bar
    if (!aModal) {
        // Remove redundant menu
        auto menuBar = new QMenuBar(this);
        // auto fileMenu = new QMenu(tr("File"), menuBar);
        auto addResource = new QAction(tr("Add Assets"));
        connect(addResource, &QAction::triggered, this, &ResourceDialog::onAddResourceTriggered);
        auto deleteUnused = new QAction(tr("Delete unused"), menuBar);
        connect(deleteUnused, &QAction::triggered, this, &ResourceDialog::onDeleteUnusedTriggered);
        // fileMenu->addAction(addResource);
        menuBar->setNativeMenuBar(false);
        menuBar->addAction(addResource);
        menuBar->addAction(deleteUnused);
        // menuBar->addMenu(fileMenu);
        menuBar->show();
        this->setMenuBar(menuBar);
    }

    // resource tree
    mTree = new ResourceTreeWidget(aViaPoint, !aModal, this);
    mTree->setSelectionMode(QAbstractItemView::ExtendedSelection);

    this->setMainWidget(mTree, false);

    // modal only
    if (aModal) {
        // button box
        this->setOkCancel();
        this->setOkEnable(false);

        // updater
        mTree->onNodeSelectionChanged.connect([=](const NodeList& aNodes) {
            this->mNodeList.clear();
            for (auto node : aNodes) {
                if (node->data().hasImage()) {
                    this->mNodeList.push_back(node);
                }
            }
            const bool isValid = !this->mNodeList.isEmpty();
            this->setOkEnable(isValid);
        });
    }
}

void ResourceDialog::setProject(core::Project* aProject) {
    if (aProject) {
        mProject = aProject->pointee();
    } else {
        mProject.reset();
    }
    mTree->setProject(aProject);
}

void ResourceDialog::watchAllAssets() { mTree->watchAllTopItems(); }

void ResourceDialog::updateResourcePath() {
    if (mProject) {
        mTree->updateTreeRootName(mProject->resourceHolder());
    }
}

bool ResourceDialog::hasValidNode() const { return this->result() == QDialog::Accepted && !mNodeList.isEmpty(); }

void ResourceDialog::onAddResourceTriggered(bool) {
    if (!mProject)
        return;

    const QStringList fileName = QFileDialog::getOpenFileNames(
        this, tr("Open Files"), "", "ImageFile (*.psd *.ora *.jpg *.jpeg *.png *.gif *.tiff *.tif *.webp)"
    );
    if (fileName.isEmpty())
        return;

    for (int i = 0; i < fileName.count(); i++) {
        mTree->load(fileName[i]);
    }
}

void ResourceDialog::onDeleteUnusedTriggered(bool) {
    if (!mProject)
        return;

    // The tree owns the removal so the panel's context-menu Delete and this bulk
    // action share one definition of "unused" (and one removal path).
    const int count = mTree->removeUnusedAssets();
    if (count == 0) {
        MainWindow::showInfoPopup(tr("Delete unused"), tr("No unused assets to delete"), "Info");
    }
}

void ResourceDialog::keyPressEvent(QKeyEvent* aEvent) {
    EasyDialog::keyPressEvent(aEvent);

    if (!this->isModal()) {
        mViaPoint.throwKeyPressingToKeyCommandInvoker(aEvent);
    }
}

void ResourceDialog::keyReleaseEvent(QKeyEvent* aEvent) {
    EasyDialog::keyReleaseEvent(aEvent);

    if (!this->isModal()) {
        mViaPoint.throwKeyReleasingToKeyCommandInvoker(aEvent);
    }
}

} // namespace gui
