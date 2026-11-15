#include "gui/ObjectTreeWidget.h"

#include <QApplication>
#include <QEventLoop>
#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QQuickImageProvider>
#include <QQuickWidget>
#include <QQmlContext>
#include <QRegularExpression>
#include <QScrollArea>
#include <QVBoxLayout>

#include "cmnd/ScopedMacro.h"
#include "core/FFDKey.h"
#include "core/ImageKey.h"
#include "core/Project.h"
#include "core/TimeKeyBlender.h"
#include "core/TimeKeyGatherer.h"
#include "ctrl/CmndName.h"
#include "ctrl/TimeLineEditor.h"
#include "ctrl/TimeLineUtil.h"
#include "ctrl/TreeActions.h"
#include "gui/QmlHost.h"
#include "gui/ResourceDialog.h"
#include "gui/ResourceTreeWidget.h"
#include "gui/qml/ObjectTreeModel.h"
#include "gui/qml/QmlTheme.h"
#include "gui/res/res_Item.h"
#include "gui/res/res_ResourceUpdater.h"
#include "gui/theme/Colors.h"
#include "img/ResourceNode.h"

namespace gui {
namespace {

//-------------------------------------------------------------------------------------------------
// The layer thumbnails and the matte chips, as QML images.
//
// A QQuickWidget renders on the GUI thread, so a provider may read the model directly; what
// it must NOT do is make the pixmap on demand for every delegate that comes into view -
// gui::ThumbnailCache already keys the result by (buffer, size, dpr), and the model avoids
// even asking until a row is drawn.
class TreeThumbnailProvider: public QQuickImageProvider {
public:
    explicit TreeThumbnailProvider(ObjectTreeModel* aModel):
        QQuickImageProvider(QQuickImageProvider::Image), mModel(aModel) {}

    QImage requestImage(const QString& aId, QSize* aSize, const QSize&) override {
        // image://treethumb/<row>?<key>: the row names the node, the key is the buffer's
        // identity (so a reimport re-loads the cell and nothing else does)
        const int row = aId.section(QLatin1Char('?'), 0, 0).toInt();
        const QImage image = mModel->thumbnailImage(row);
        if (aSize)
            *aSize = image.size();
        return image;
    }

private:
    ObjectTreeModel* mModel;
};

class TreeMatteProvider: public QQuickImageProvider {
public:
    explicit TreeMatteProvider(ObjectTreeModel* aModel):
        QQuickImageProvider(QQuickImageProvider::Image), mModel(aModel) {}

    QImage requestImage(const QString& aId, QSize* aSize, const QSize&) override {
        const int row = aId.section(QLatin1Char('?'), 0, 0).toInt();
        const QImage image = mModel->matteImage(row);
        if (aSize)
            *aSize = image.size();
        return image;
    }

private:
    ObjectTreeModel* mModel;
};

// The label strings the tree's menu and its reports show, in the context the catalogs
// already carry ("gui::ObjectTreeWidget" is a catalog KEY: see ObjectTreeModel::trTree).
QString trTree(const char* aText) {
    return QCoreApplication::translate("gui::ObjectTreeWidget", aText);
}

int extractIntFromStr(const QString& str) {
    return QRegularExpression(QStringLiteral(R"(-?\b\d+(?:\.\d+)?\b)")).match(str).captured(0).toInt();
}

} // namespace

//-------------------------------------------------------------------------------------------------
ObjectTreeWidget::ObjectTreeWidget(
    ViaPoint& aViaPoint, GUIResources& aResources, QWidget* aParent, const LocaleParam& aLocale
):
    QWidget(aParent),
    mViaPoint(aViaPoint),
    mGUIResources(aResources),
    mProject(),
    mModel(new ObjectTreeModel(this)),
    mTheme(new QmlTheme(this)),
    mQuick(new KeyForwardingQuickWidget(this)),
    mScrollOffset(0) {
    setObjectName(QStringLiteral("objectTree")); // the dock surface's QSS id
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    // A QQuickWidget's own size hint comes from its ROOT item, and the scene's root is a
    // plain Item: without a floor the splitter gave this pane 0px and the dock showed only
    // its lanes (measured on the first run).
    setMinimumWidth(140);

    // ---- the scene ----------------------------------------------------------
    mQuick->setResizeMode(QQuickWidget::SizeRootObjectToView);
    mQuick->rootContext()->setContextProperty(QStringLiteral("treeModel"), mModel);
    mQuick->rootContext()->setContextProperty(QStringLiteral("theme"), mTheme);
    // QML's Qt.application.font is the PROCESS font; the dock's face comes from LocaleParam
    // and reaches widgets through standard.ssa, so the scene is handed it explicitly
    mQuick->rootContext()->setContextProperty(QStringLiteral("uiFont"), qmlUiFont(aLocale));
    // the model measures names in the SAME face the scene draws them with (see contentWidth)
    mModel->setContentFont(qmlUiFont(aLocale));
    mQuick->engine()->addImageProvider(QStringLiteral("treethumb"), new TreeThumbnailProvider(mModel));
    mQuick->engine()->addImageProvider(QStringLiteral("treematte"), new TreeMatteProvider(mModel));
    mQuick->setClearColor(theme::Colors::current().base);
    mQuick->setSource(QUrl(QStringLiteral("qrc:/AnimeEffects/Dialogs/qml/ObjectTreePanel.qml")));
    connect(mQuick, &QQuickWidget::statusChanged, this, [this](QQuickWidget::Status aStatus) {
        if (aStatus == QQuickWidget::Error) {
            for (const QQmlError& error : mQuick->errors())
                qWarning().noquote() << error.toString();
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(mQuick);

    // ---- the model's outward notifications ----------------------------------
    connect(mModel, &ObjectTreeModel::selectionChanged, this, [this]() {
        onSelectionChanged(mModel->representNode());
    });
    connect(mModel, &ObjectTreeModel::rowsChanged, this, [this]() { onRowsUpdated(); });
    connect(mModel, &ObjectTreeModel::visibilityChanged, this, [this]() { onVisibilityUpdated(); });
    connect(mModel, &ObjectTreeModel::scrollOffsetChanged, this, [this]() {
        // the scene moved (or was moved): the timeline's camera follows the same pixels
        mScrollOffset = mModel->scrollOffset();
        onScrollUpdated(mScrollOffset);
    });

    // ---- the host's jobs ----------------------------------------------------
    connect(mModel, &ObjectTreeModel::pasteRequested, this, [this](core::ObjectNode* aNode) {
        if (aNode)
            pasteKeys(*aNode);
    });
    connect(mModel, &ObjectTreeModel::createLayerRequested, this, [this](
                                                                     core::ObjectNode* aParent, int aIndex
                                                                 ) { createLayerDialog(aParent, aIndex); });

    connect(mModel, &ObjectTreeModel::contextMenuRequested, this, [this](int aRow, const QPoint& aScenePos) {
        showMenu(aRow, aScenePos);
    });
}

ObjectTreeWidget::~ObjectTreeWidget() {
    // The scene binds to the model and the theme: tear it down first, as every dock here
    // does, so teardown never re-reads a destroyed context property (and the providers,
    // which hold the model, die with the engine the widget owns).
    delete mQuick;
    mQuick = nullptr;
}

void ObjectTreeWidget::setProject(core::Project* aProject) {
    if (aProject)
        mProject = aProject->pointee();
    else
        mProject.reset();
    mScrollOffset = 0;
    mModel->setScrollOffset(0);
    mModel->setProject(aProject);
    // the model rebuilt and announced nothing yet (its signals fire while this object is
    // already connected, so the dock has already been told; this is for a project set
    // before the dock's own wiring exists)
    onRowsUpdated();
    onSelectionChanged(nullptr);
}

core::ObjectNode* ObjectTreeWidget::findSelectingRepresentNode() {
    return mModel->representNode();
}

QVector<core::ObjectNode*> ObjectTreeWidget::selectedNodes() const { return mModel->selectedNodes(); }

const AnimationRows& ObjectTreeWidget::rows() const { return mModel->rows(); }

void ObjectTreeWidget::setScrollOffset(int aOffset) { mModel->setScrollOffset(aOffset); }

void ObjectTreeWidget::refreshRows() { mModel->refreshRows(); }

void ObjectTreeWidget::frameChanged() { mModel->frameChanged(); }

void ObjectTreeWidget::refreshTheme() {
    mTheme->refresh();
    if (mQuick)
        mQuick->setClearColor(theme::Colors::current().base);
}

//-------------------------------------------------------------------------------------------------
// the menu
//-------------------------------------------------------------------------------------------------
void ObjectTreeWidget::showMenu(int aRow, const QPoint& aScenePos) {
    if (!mModel->project())
        return;
    const QPoint global = mQuick->mapToGlobal(aScenePos);

    // THE MENU ACTS ON THE SELECTION. The widget's menu acted on the row it was opened on,
    // so selecting five layers and opening the menu on one of them offered a single-layer
    // Delete: a multi-selection was unusable. A right-click on a row that is NOT selected
    // selects it first (every list view does), and the model's press route does that.
    mModel->pressRow(aRow, 0);

    QMenu menu(this);
    core::ObjectNode* represent = mModel->representNode();

    if (represent) {
        menu.addAction(represent->isSlimmedDown() ? trTree("Enlarge") : trTree("Contract"), [this]() {
            mModel->setSelectionSlimmed(!mModel->anySelectedIsSlimmed());
        });
        menu.addSeparator();
    }
    if (aRow == 0) {
        menu.addAction(trTree("Add new assets"), [this]() { mModel->openAddAssets(); });
        menu.addAction(trTree("Add missing assets"), [this]() { mModel->addMissingAssets(); });
        menu.addSeparator();
    }
    menu.addAction(trTree("Rename"), [this, aRow]() { mModel->beginRename(aRow); });
    menu.addAction(trTree("Create layer"), [this, aRow]() { mModel->requestCreateLayer(aRow); });
    menu.addAction(trTree("Create folder"), [this, aRow]() { mModel->createFolder(aRow); });

    if (core::ObjectNode* pasteTarget = mModel->pasteTargetNode(aRow)) {
        menu.addSeparator();
        menu.addAction(trTree("Paste from clipboard"), [this, pasteTarget]() { pasteKeys(*pasteTarget); });
        menu.addAction(trTree("Duplicate node"), [this]() { mModel->duplicateSelection(); });
        menu.addAction(trTree("Add keys at playhead"), [this]() { mModel->addKeysAtPlayheadToSelection(); });
        menu.addSeparator();
        menu.addAction(trTree("Delete"), [this]() { mModel->deleteSelection(); });
    }
    menu.exec(global);
}

//-------------------------------------------------------------------------------------------------
// paste: the widget's own behaviour, unchanged, now keyed by NODE
//-------------------------------------------------------------------------------------------------
void ObjectTreeWidget::pasteKeys(core::ObjectNode& aNode) {
    core::Project* project = mModel->project();
    if (!project)
        return;

    const bool isFolder = !aNode.children().empty();
    const QString pasted = ctrl::TimeLineEditor::pasteCbKeys(aNode, project->pointee(), isFolder);
    QStringList lines = pasted.split(QLatin1Char('\n'));
    const int successNum = lines.isEmpty() ? 0 : extractIntFromStr(lines.first());
    const bool keyErrored = lines.size() > 1;
    const QStringList errors = lines.filter(QStringLiteral("Error"));
    const QStringList nullLogs = lines.filter(QRegularExpression(QStringLiteral("^(?!.*[\\d:])[^\\n]*$")));
    const QStringList keyTypeErrors = lines.filter(QStringLiteral("Key types errored"));

    QStringList warnings;
    if (successNum != 0) {
        const auto keys = ctrl::TimeLineEditor::getTypesFromCb(project->pointee(), &aNode);
        for (const auto& key : keys) {
            if (key->type() == core::TimeKeyType_Image) {
                const auto* imageKey = dynamic_cast<const core::ImageKey*>(key);
                ctrl::TimeLineUtil::assignImageKeyCellSize(
                    *project, aNode, key->frame(), imageKey->data().gridMesh().cellSize()
                );
            }
            if (key->type() == core::TimeKeyType_FFD) {
                const auto* ffdKey = dynamic_cast<const core::FFDKey*>(key);
                // the pasted FFD key must match the mesh its node has at that frame
                core::LayerMesh* areaMesh =
                    core::TimeKeyBlender::getAreaMesh(aNode, project->currentTimeInfo());
                if (areaMesh && ffdKey->data().count() != areaMesh->vertexCount()) {
                    warnings.append(
                        trTree(
                            "The requested FFD key mesh (at frame %1) has %2 vertices, but the parent mesh of the "
                            "targeted node has %3 vertices, artifacts may be present."
                        )
                            .arg(key->frame())
                            .arg(ffdKey->data().count())
                            .arg(areaMesh->vertexCount())
                    );
                }
            }
            if (key->type() == core::TimeKeyType_Bone) {
                auto* boneKey = dynamic_cast<core::BoneKey*>(key);
                boneKey->resetCaches(*project, aNode);
            }
        }

        // Believe me, this is the easiest way to do it: rebuild a resource tree offscreen and
        // let the resource updater reload every item, so a pasted image key's texture exists.
        auto* tree = new ResourceTreeWidget(mViaPoint, false, this);
        tree->hide();
        tree->setProject(project);
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (res::Item* item = res::Item::cast(tree->topLevelItem(i))) {
                res::ResourceUpdater updater(mViaPoint, *project);
                updater.forceReload(*item);
            }
        }
        tree->deleteLater();
    }

    QMessageBox box;
    if (successNum != 0) {
        if (!keyErrored) {
            box.setText(trTree("Successfully pasted %1 keys.").arg(successNum));
            if (!warnings.isEmpty()) {
                box.setText(box.text()
                            + QLatin1Char('\n')
                            + trTree("Warnings present for %1 keys. The log is available below.")
                                  .arg(warnings.size()));
                box.setDetailedText(warnings.join(QLatin1Char('\n')));
            }
        } else {
            box.setText(
                trTree("Successfully pasted %1 keys.\n%2 error(s) have been detected.\nThe log is available below.")
                    .arg(successNum)
                    .arg(errors.size())
            );
        }
    } else {
        box.setText(trTree("Failed to paste key(s)"));
        if (!keyErrored) {
            box.setDetailedText(trTree(
                "Clipboard does not contain valid JSON information or the timeline already has a key in the same "
                "frame."
            ));
        }
    }
    if (keyErrored && !errors.isEmpty() && !nullLogs.isEmpty() && !keyTypeErrors.isEmpty()) {
        QString log;
        log.append(QStringLiteral("--- Error log ---\n"));
        log.append(errors.join(QStringLiteral(".\n")));
        log.append(QStringLiteral("\n-----\n"));
        log.append(nullLogs.join(QStringLiteral(".\n")));
        log.append(QStringLiteral("\n-----\n"));
        log.append(keyTypeErrors.join(QStringLiteral(".\n")));
        log.append(QStringLiteral("\n-----\n"));
        box.setDetailedText(log);
    }
    if (box.detailedText().isEmpty() && successNum == 0)
        box.setDetailedText(trTree("Timeline has a key in the same frame."));
    box.exec();
}

//-------------------------------------------------------------------------------------------------
// a layer from an asset
//-------------------------------------------------------------------------------------------------
void ObjectTreeWidget::createLayerDialog(core::ObjectNode* aParent, int aIndex) {
    core::Project* project = mModel->project();
    if (!project || !aParent)
        return;

    QScopedPointer<ResourceDialog> dialog(new ResourceDialog(mViaPoint, true, this));
    dialog->setProject(project);
    dialog->exec();
    if (!dialog->hasValidNode())
        return;

    // The dialog offers a list: the layers land one after another from aIndex, each with the
    // z-depth its place in that list gives it (the importer's own rule).
    int index = aIndex;
    int resIndex = 0;
    const auto nodes = dialog->nodeList();
    for (img::ResourceNode* node : nodes) {
        if (!node || !node->data().hasImage())
            continue;
        if (ctrl::tree::createLayerFromResource(*project, *aParent, index, *node, resIndex++))
            ++index;
    }
}

} // namespace gui
