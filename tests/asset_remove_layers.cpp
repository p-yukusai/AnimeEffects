// Regression test for removing an asset that layers still use.
//
// Removing an asset from the assets panel used to be a dead end when any layer
// rendered from it: ResourceUpdater::remove found the live reference, put up
// "Some layers are still referenced by objects" and changed NOTHING, so the user
// had to find every layer by hand before the asset could go. The behaviour now
// is to offer to delete those layers with the asset, in ONE undo step.
//
// The pieces this pins, each of which can go wrong on its own:
//
//   1. WHICH layers reference an asset. An object renders from the layer node
//      its ImageKey serializes the address of, and that node is a layer UNDER
//      the asset's top node -- so the search must walk the asset's whole subtree,
//      and must not match a layer that renders from a DIFFERENT asset
//      (core::ObjectTree::findNodesUsingResource).
//
//   2. That the asset can actually be taken out: deleting the referencing
//      layers first drops every origin-keeping reference, so the asset tree is
//      no longer held (img::ResourceNode::isKeeped) when it is removed.
//
//   3. That ONE undo restores BOTH the asset and its layers, with the layers
//      back in their original sibling order. A multi-node removal replays its
//      commands in reverse, and both cmnd::RemoveTree and obj::RemoveItem
//      restore a node at a STORED index, so the removals have to be appended
//      highest sibling index first -- get that wrong and undo silently reorders
//      the layers (z-order changes).
//
//   4. That an asset nothing references is still removed on its own, with no
//      references and no layer touched.
//
//   5. That the assets panel's "Delete unused" takes every unreferenced asset
//      and only those, leaving the referenced ones and every layer where they
//      were, and that the panel offers the action in the non-modal dialog while
//      the modal picker (which has no menu bar) cannot.
//
// Headless but for a GL context: constructing a core::LayerNode's default
// ImageKey (LayerNode::setDefaultImage) binds GL through its GridMesh, so, like
// psd_layer_identity, this runs under xvfb with a current context. Run:
//   xvfb-run -a ./build/tests/Debug/asset_remove_layers
#include <QApplication>
#include <QAction>
#include <QFile>
#include <QFileSystemWatcher>
#include <QJsonObject>
#include <QMenuBar>
#include <QOpenGLVersionFunctions>
#include <QOpenGLWidget>
#include <QStringList>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTreeWidgetItem>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "XC.h"
#include "XCAssert.h"
#include "XCReport.h"
#include "cmnd/Stack.h"
#include "core/FolderNode.h"
#include "core/ImageKey.h"
#include "core/LayerNode.h"
#include "core/ObjectTree.h"
#include "core/Project.h"
#include "gl/Global.h"
#include "gui/AudioPlaybackWidget.h"
#include "gui/GUIResources.h"
#include "gui/KeyCommandInvoker.h"
#include "gui/MainWindow.h"
#include "gui/ObjectTreeWidget.h"
#include "gui/ResourceDialog.h"
#include "gui/ResourceTreeWidget.h"
#include "gui/ViaPoint.h"
#include "gui/res/res_Item.h"
#include "gui/res/res_ResourceUpdater.h"
#include "img/PSDReader.h"
#include "img/ResourceNode.h"
#include "img/Util.h"

// ---------------------------------------------------------------------------
// Harness: a failing XC_ASSERT or XC_FATAL_ERROR is a test failure, not a crash
// log. Same handlers as psd_relink_safety / psd_layer_identity.
// ---------------------------------------------------------------------------
namespace {

class TestAssertHandler: public XCAssertHandler {
public:
    void failure() const override {
        std::printf("FAIL: XC_ASSERT\n");
        std::exit(1);
    }
};

class TestErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString& aInfo, const QString&) const override {
        std::printf("FAIL: XC_FATAL_ERROR %s\n", aInfo.toUtf8().data());
        std::exit(1);
    }
};

TestAssertHandler gAssertHandler;
TestErrorHandler gErrorHandler;
} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                                   \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (false)

// ---------------------------------------------------------------------------
// A minimal PSD writer: the test needs a real source file that img::PSDReader
// parses into an asset tree with layers that carry images. Copied from
// psd_relink_safety (the same shape PSDReader accepts).
// ---------------------------------------------------------------------------
namespace {

class ByteWriter {
public:
    void u8(uint8 aValue) { mData.push_back((char)aValue); }
    void u16(uint16 aValue) {
        u8((uint8)((aValue >> 8) & 0xff));
        u8((uint8)(aValue & 0xff));
    }
    void u32(uint32 aValue) {
        u16((uint16)((aValue >> 16) & 0xffff));
        u16((uint16)(aValue & 0xffff));
    }
    void raw(const std::string& aValue) { mData += aValue; }
    void append(const ByteWriter& aRhs) { mData += aRhs.mData; }
    void name(const QString& aValue) {
        const QByteArray bytes = aValue.toUtf8();
        u8((uint8)bytes.size());
        mData.append(bytes.constData(), bytes.size());
        while (mData.size() % 4 != 0) {
            u8(0);
        }
    }
    const std::string& data() const { return mData; }

private:
    std::string mData;
};

void writeInfo(ByteWriter& aDst, const char* aKey, const std::string& aPayload) {
    aDst.raw("8BIM");
    aDst.raw(aKey);
    aDst.u32((uint32)aPayload.size());
    aDst.raw(aPayload);
}

struct LayerSpec {
    QString name;
    QString identity;
    uint8 rgb[3];
};

std::string layerRecord(const LayerSpec& aSpec) {
    ByteWriter w;
    w.u32(0);
    w.u32(0);
    w.u32(2);
    w.u32(2);
    w.u16(3);
    for (int c = 0; c < 3; ++c) {
        w.u16((uint16)c);
        w.u32(6);
    }
    w.raw("8BIM");
    w.raw("norm");
    w.u8(255);
    w.u8(0);
    w.u8(0);
    w.u8(0);

    ByteWriter extra;
    extra.u32(0);
    extra.u32(0);
    extra.name(aSpec.name);
    if (!aSpec.identity.isEmpty()) {
        ByteWriter id;
        id.u32((uint32)aSpec.identity.toUInt());
        writeInfo(extra, "lyid", id.data());
    }
    w.u32((uint32)extra.data().size());
    return w.data() + extra.data();
}

std::string layerChannelData(const LayerSpec& aSpec) {
    ByteWriter w;
    for (int c = 0; c < 3; ++c) {
        w.u16(0);
        for (int i = 0; i < 4; ++i) {
            w.u8(aSpec.rgb[c]);
        }
    }
    return w.data();
}

std::string buildPsd(const std::vector<LayerSpec>& aLayers) {
    ByteWriter records;
    ByteWriter channelData;
    for (const LayerSpec& spec : aLayers) {
        records.raw(layerRecord(spec));
        channelData.raw(layerChannelData(spec));
    }

    ByteWriter layers;
    layers.u16((uint16)aLayers.size());
    layers.append(records);

    ByteWriter layerAndMask;
    layerAndMask.u32((uint32)(layers.data().size() + channelData.data().size()));
    layerAndMask.append(layers);
    layerAndMask.append(channelData);
    layerAndMask.u32(0);

    ByteWriter file;
    file.raw("8BPS");
    file.u16(1);
    for (int i = 0; i < 6; ++i) {
        file.u8(0);
    }
    file.u16(3);
    file.u32(2); // height
    file.u32(2); // width
    file.u16(8);
    file.u16(3);
    file.u32(0);
    file.u32(0);
    file.u32((uint32)layerAndMask.data().size());
    file.append(layerAndMask);

    file.u16(0);
    for (int i = 0; i < 3; ++i) {
        for (int p = 0; p < 4; ++p) {
            file.u8(0x40);
        }
    }
    return file.data();
}

bool writeBytes(const QString& aPath, const std::string& aData) {
    QFile file(aPath);
    if (!file.open(QFile::WriteOnly)) {
        return false;
    }
    return file.write(aData.c_str(), (qint64)aData.size()) == (qint64)aData.size();
}

img::ResourceNode* createTreeFromPsd(const std::string& aPsd) {
    std::stringstream ss(aPsd, std::ios::in | std::ios::binary);
    img::PSDReader reader(ss);
    if (reader.resultCode() != img::PSDReader::ResultCode_Success) {
        std::printf("FAIL %d: psd parse: %s\n", __LINE__, reader.resultMessage().c_str());
        return nullptr;
    }
    return img::Util::createResourceNodes(*reader.format(), true);
}

img::ResourceNode* findNode(img::ResourceNode* aRoot, const QString& aIdentifier) {
    if (aRoot->data().identifier() == aIdentifier) {
        return aRoot;
    }
    for (auto child : aRoot->children()) {
        if (img::ResourceNode* found = findNode(child, aIdentifier)) {
            return found;
        }
    }
    return nullptr;
}

class TestAnimator: public core::Animator {
public:
    virtual core::Frame currentFrame() const override { return core::Frame(); }
    virtual void stop() override {}
    virtual void suspend() override {}
    virtual void resume() override {}
    virtual bool isSuspended() const override { return false; }
};

// ---------------------------------------------------------------------------
// One project: a resource root directory, the shared GUI resources, the
// ViaPoint the asset removal reaches the object tree through, and both trees.
// ---------------------------------------------------------------------------
class Fixture {
public:
    Fixture():
        mDir(),
        mAnimator(),
        mResources(QStringLiteral(":")),
        mVia(nullptr),
        mProject(QString(), mAnimator, nullptr),
        mObjTree(mVia, mResources, nullptr),
        mResTree(mVia, false, nullptr) {
        mProject.setFileName(mDir.filePath("project.anie"));
        // The wiring MainWindow does: the asset removal reaches the object tree
        // (to delete the layers that reference a removed asset) through ViaPoint.
        mVia.setObjectTreeWidget(&mObjTree);
        mVia.setProject(&mProject);
    }

    core::Project& project() { return mProject; }
    gui::ViaPoint& via() { return mVia; }
    gui::ObjectTreeWidget& objectTree() { return mObjTree; }
    gui::ResourceTreeWidget& resourceTree() { return mResTree; }
    cmnd::Stack& stack() { return mProject.commandStack(); }

    // A PSD on disk, imported as one asset of this project.
    img::ResourceNode* registerAsset(const QString& aRelPath, const std::string& aPsd) {
        if (!writeBytes(mDir.filePath(aRelPath), aPsd)) {
            return nullptr;
        }
        std::unique_ptr<img::ResourceNode> root(createTreeFromPsd(aPsd));
        if (!root) {
            return nullptr;
        }
        mProject.resourceHolder().pushImageTree(*root, mDir.filePath(aRelPath));
        return root.release();
    }

    // The top-level folder every project object tree has.
    core::FolderNode* grabTopFolder() {
        auto* top = new core::FolderNode(QStringLiteral("root"));
        mProject.objectTree().grabTopNode(top);
        return top;
    }

    // A layer node under aParent. With aResource null it renders from nothing and
    // so references no asset.
    core::LayerNode* addLayer(core::FolderNode& aParent, const QString& aName, img::ResourceNode* aResource) {
        auto* node = new core::LayerNode(aName, mProject.objectTree().shaderHolder());
        aParent.children().pushBack(node);
        if (aResource) {
            node->setDefaultImage(aResource->handle());
        }
        return node;
    }

    // Builds both trees from the project, the way opening it does.
    void showProject() {
        mObjTree.setProject(&mProject);
        mResTree.setProject(&mProject);
    }

    int assetCount() const { return (int)mProject.resourceHolder().imageTrees().size(); }

    bool holdsAsset(const img::ResourceNode* aTop) const {
        for (auto& tree : mProject.resourceHolder().imageTrees()) {
            if (tree.topNode == aTop) {
                return true;
            }
        }
        return false;
    }

    gui::res::Item* assetItem(const img::ResourceNode* aTop) {
        for (int i = 0; i < mResTree.topLevelItemCount(); ++i) {
            if (gui::res::Item* item = gui::res::Item::cast(mResTree.topLevelItem(i))) {
                if (&item->node() == aTop) {
                    return item;
                }
            }
        }
        return nullptr;
    }

    // The names of the top folder's children, in model order.
    QStringList childNames(core::FolderNode& aParent) {
        QStringList names;
        for (auto child : aParent.children()) {
            names << child->name();
        }
        return names;
    }

private:
    QTemporaryDir mDir;
    TestAnimator mAnimator;
    gui::GUIResources mResources;
    gui::ViaPoint mVia;
    core::Project mProject;
    gui::ObjectTreeWidget mObjTree;
    gui::ResourceTreeWidget mResTree;
};
} // namespace

// ---------------------------------------------------------------------------
// Link stubs for the gui classes these sources reference but that this test does
// not drive, following the convention in psd_relink_safety.
// ---------------------------------------------------------------------------
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace gui {
void KeyCommandInvoker::onKeyPressed(const QKeyEvent*) {}
void KeyCommandInvoker::onKeyReleased(const QKeyEvent*) {}
// The QML accessibility factory is irrelevant here (no QML scene is built), so
// it is stubbed rather than pulling the Qt Quick stack into this target.
void installQmlFormAccessibility() {}
} // namespace gui

namespace {
QFileSystemWatcher* gTestWatcher = nullptr;
}
void gui::MainWindow::autoSave() {}
void gui::MainWindow::onGeneralSettingsApplied() {}
QFileSystemWatcher* gui::MainWindow::getWatcher() {
    if (!gTestWatcher) {
        gTestWatcher = new QFileSystemWatcher();
    }
    return gTestWatcher;
}
void gui::MainWindow::showInfoPopup(const QString&, const QString&, const QString&, const QString&) {}

// ---------------------------------------------------------------------------
// 1. The search walks the asset's whole subtree and matches only THIS asset.
//
// A layer renders from one of the asset's LAYER nodes, not from its top node, so
// a search that compared the top node alone would find nothing and the asset
// could never be removed with its layers. And a layer that renders from a
// DIFFERENT asset must not be collected -- deleting it would destroy unrelated
// work.
// ---------------------------------------------------------------------------
static int testReferencesAreFoundThroughTheAsset() {
    Fixture fixture;

    img::ResourceNode* assetA =
        fixture.registerAsset("a.psd", buildPsd({{"A1", "1", {(uint8)200, 30, 30}}, {"A2", "2", {(uint8)30, 200, 30}}}));
    img::ResourceNode* assetB =
        fixture.registerAsset("b.psd", buildPsd({{"B1", "3", {(uint8)30, 30, 200}}}));
    CHECK(assetA);
    CHECK(assetB);

    core::FolderNode* top = fixture.grabTopFolder();
    img::ResourceNode* a1 = findNode(assetA, "A1");
    img::ResourceNode* a2 = findNode(assetA, "A2");
    img::ResourceNode* b1 = findNode(assetB, "B1");
    CHECK(a1);
    CHECK(a2);
    CHECK(b1);

    core::LayerNode* usesA1 = fixture.addLayer(*top, "usesA1", a1);
    core::LayerNode* usesA2 = fixture.addLayer(*top, "usesA2", a2);
    core::LayerNode* usesB1 = fixture.addLayer(*top, "usesB1", b1);
    core::LayerNode* usesNothing = fixture.addLayer(*top, "usesNothing", nullptr);
    CHECK(usesA1);
    CHECK(usesA2);
    CHECK(usesB1);
    CHECK(usesNothing);

    QVector<core::ObjectNode*> refsA = fixture.project().objectTree().findNodesUsingResource(*assetA);
    CHECK(refsA.size() == 2);
    CHECK(refsA.contains(usesA1));
    CHECK(refsA.contains(usesA2));

    QVector<core::ObjectNode*> refsB = fixture.project().objectTree().findNodesUsingResource(*assetB);
    CHECK(refsB.size() == 1);
    CHECK(refsB.contains(usesB1));
    CHECK(!refsB.contains(usesNothing));
    return 0;
}

// ---------------------------------------------------------------------------
// 2. Removing a referenced asset takes its layers, in ONE undo step, and undo
//    restores every layer to its original position.
//
// The referencing layers sit at sibling indices 1 and 3 (with unrelated layers
// around them) on purpose: the removals have to be appended highest index first
// for undo to put them back in order. If that is wrong, the count still matches
// and only the ORDER moves -- a z-order change with no other symptom.
// ---------------------------------------------------------------------------
static int testRemoveTakesItsLayersInOneUndoStep() {
    Fixture fixture;

    img::ResourceNode* asset =
        fixture.registerAsset("a.psd", buildPsd({{"A1", "1", {(uint8)200, 30, 30}}, {"A2", "2", {(uint8)30, 200, 30}}}));
    img::ResourceNode* other = fixture.registerAsset("b.psd", buildPsd({{"B1", "3", {(uint8)30, 30, 200}}}));
    CHECK(asset);
    CHECK(other);

    core::FolderNode* top = fixture.grabTopFolder();
    img::ResourceNode* a1 = findNode(asset, "A1");
    img::ResourceNode* a2 = findNode(asset, "A2");
    img::ResourceNode* b1 = findNode(other, "B1");
    CHECK(a1);
    CHECK(a2);
    CHECK(b1);

    // [X, usesA1, Y, usesA2, Z] -- the two referencing layers are the 1st and 3rd
    fixture.addLayer(*top, "X", nullptr);
    core::LayerNode* usesA1 = fixture.addLayer(*top, "usesA1", a1);
    fixture.addLayer(*top, "Y", nullptr);
    core::LayerNode* usesA2 = fixture.addLayer(*top, "usesA2", a2);
    fixture.addLayer(*top, "Z", b1);

    CHECK(top->children().size() == 5);
    CHECK(a1->isKeeped()); // the layers render from it
    CHECK(a2->isKeeped());

    fixture.showProject();
    gui::res::Item* item = fixture.assetItem(asset);
    CHECK(item);

    QVector<core::ObjectNode*> refs = fixture.project().objectTree().findNodesUsingResource(*asset);
    CHECK(refs.size() == 2);
    CHECK(refs.contains(usesA1));
    CHECK(refs.contains(usesA2));

    gui::res::ResourceUpdater updater(fixture.via(), fixture.project());
    CHECK(updater.remove(fixture.resourceTree(), *item, refs));

    // the asset is gone, and the layers that rendered from it are gone with it
    CHECK(!fixture.holdsAsset(asset));
    CHECK(fixture.holdsAsset(other)); // the unrelated asset is untouched
    CHECK(fixture.assetCount() == 1);
    CHECK(fixture.resourceTree().topLevelItemCount() == 1);
    CHECK(!a1->isKeeped());
    CHECK(!a2->isKeeped());
    CHECK(top->children().size() == 3);
    CHECK(fixture.childNames(*top) == QStringList({"X", "Y", "Z"}));
    CHECK(fixture.objectTree().topLevelItem(0)->childCount() == 3);
    CHECK(fixture.stack().isEdited());

    // ONE undo brings back BOTH the asset and its layers, and Order survives.
    fixture.stack().undo();
    CHECK(fixture.assetCount() == 2);
    CHECK(fixture.holdsAsset(asset));
    CHECK(fixture.resourceTree().topLevelItemCount() == 2);
    CHECK(top->children().size() == 5);
    CHECK(fixture.childNames(*top) == QStringList({"X", "usesA1", "Y", "usesA2", "Z"}));
    CHECK(fixture.objectTree().topLevelItem(0)->childCount() == 5);
    CHECK(a1->isKeeped());
    CHECK(a2->isKeeped());
    CHECK(fixture.project().objectTree().findNodesUsingResource(*asset).size() == 2);

    // and redo removes them again, in order
    fixture.stack().redo();
    CHECK(top->children().size() == 3);
    CHECK(fixture.childNames(*top) == QStringList({"X", "Y", "Z"}));
    CHECK(fixture.objectTree().topLevelItem(0)->childCount() == 3);
    CHECK(!fixture.holdsAsset(asset));
    CHECK(fixture.resourceTree().topLevelItemCount() == 1);
    return 0;
}

// ---------------------------------------------------------------------------
// 3. An asset nothing references is removed on its own: no references are
//    offered, no layer is touched, and the removal still undoes.
// ---------------------------------------------------------------------------
static int testUnreferencedAssetIsRemovedAlone() {
    Fixture fixture;

    img::ResourceNode* asset = fixture.registerAsset("a.psd", buildPsd({{"A1", "1", {(uint8)200, 30, 30}}}));
    CHECK(asset);

    core::FolderNode* top = fixture.grabTopFolder();
    fixture.addLayer(*top, "kept", nullptr);

    fixture.showProject();
    gui::res::Item* item = fixture.assetItem(asset);
    CHECK(item);

    QVector<core::ObjectNode*> refs = fixture.project().objectTree().findNodesUsingResource(*asset);
    CHECK(refs.isEmpty());

    gui::res::ResourceUpdater updater(fixture.via(), fixture.project());
    CHECK(updater.remove(fixture.resourceTree(), *item, refs));

    CHECK(fixture.project().resourceHolder().imageTrees().empty());
    CHECK(fixture.resourceTree().topLevelItemCount() == 0);
    CHECK(top->children().size() == 1); // the unrelated layer was never touched
    CHECK(fixture.childNames(*top) == QStringList({"kept"}));

    fixture.stack().undo();
    CHECK(fixture.project().resourceHolder().imageTrees().size() == 1);
    CHECK(fixture.resourceTree().topLevelItemCount() == 1);
    CHECK(top->children().size() == 1);
    return 0;
}

// ---------------------------------------------------------------------------
// 4. "Delete unused" removes every asset no layer renders from, and leaves the
//    referenced ones -- and every layer -- exactly where they were. What it
//    removed is still undoable, and the count it reports is what went.
// ---------------------------------------------------------------------------
static int testDeleteUnusedRemovesOnlyUnreferenced() {
    Fixture fixture;

    img::ResourceNode* used = fixture.registerAsset("used.psd", buildPsd({{"U1", "1", {(uint8)10, 20, 30}}}));
    img::ResourceNode* alsoUsed =
        fixture.registerAsset("alsoused.psd", buildPsd({{"V1", "2", {(uint8)40, 50, 60}}}));
    img::ResourceNode* unused = fixture.registerAsset("unused.psd", buildPsd({{"W1", "3", {(uint8)70, 80, 90}}}));
    CHECK(used);
    CHECK(alsoUsed);
    CHECK(unused);

    core::FolderNode* top = fixture.grabTopFolder();
    img::ResourceNode* u1 = findNode(used, "U1");
    img::ResourceNode* v1 = findNode(alsoUsed, "V1");
    CHECK(u1);
    CHECK(v1);
    fixture.addLayer(*top, "usesUsed", u1);
    fixture.addLayer(*top, "usesAlsoUsed", v1);
    fixture.addLayer(*top, "usesNothing", nullptr);

    fixture.showProject();
    CHECK(fixture.assetCount() == 3);
    CHECK(fixture.resourceTree().topLevelItemCount() == 3);

    const int removed = fixture.resourceTree().removeUnusedAssets();
    CHECK(removed == 1);
    CHECK(fixture.assetCount() == 2);
    CHECK(fixture.resourceTree().topLevelItemCount() == 2);
    CHECK(fixture.holdsAsset(used));
    CHECK(fixture.holdsAsset(alsoUsed));
    CHECK(!fixture.holdsAsset(unused));
    // Nothing but the unused asset moved: every layer stayed put.
    CHECK(top->children().size() == 3);
    CHECK(fixture.childNames(*top) == QStringList({"usesUsed", "usesAlsoUsed", "usesNothing"}));
    CHECK(fixture.stack().isEdited());

    fixture.stack().undo();
    CHECK(fixture.assetCount() == 3);
    CHECK(fixture.holdsAsset(unused));
    CHECK(fixture.resourceTree().topLevelItemCount() == 3);
    return 0;
}

// ---------------------------------------------------------------------------
// 5. With nothing unused the bulk action is a no-op: no asset goes, and it
//    reports zero -- which is what stops the dialog claiming a deletion.
// ---------------------------------------------------------------------------
static int testDeleteUnusedWithNothingToDelete() {
    Fixture fixture;

    img::ResourceNode* asset = fixture.registerAsset("a.psd", buildPsd({{"A1", "1", {(uint8)1, 2, 3}}}));
    CHECK(asset);
    core::FolderNode* top = fixture.grabTopFolder();
    fixture.addLayer(*top, "usesIt", findNode(asset, "A1"));
    fixture.showProject();

    CHECK(fixture.resourceTree().removeUnusedAssets() == 0);
    CHECK(fixture.assetCount() == 1);
    CHECK(fixture.resourceTree().topLevelItemCount() == 1);
    CHECK(!fixture.stack().isEdited());
    return 0;
}

// ---------------------------------------------------------------------------
// 6. The dialog the user actually opens carries the action, and triggering it
//    removes the unused asset. This is the wiring the unit test above cannot
//    see: 5 proves the tree removes the right assets, this proves the panel
//    offers the command at all.
// ---------------------------------------------------------------------------
static int testDialogExposesDeleteUnused() {
    Fixture fixture;

    img::ResourceNode* used = fixture.registerAsset("used.psd", buildPsd({{"U1", "1", {(uint8)10, 20, 30}}}));
    img::ResourceNode* unused = fixture.registerAsset("unused.psd", buildPsd({{"W1", "3", {(uint8)70, 80, 90}}}));
    CHECK(used);
    CHECK(unused);

    core::FolderNode* top = fixture.grabTopFolder();
    fixture.addLayer(*top, "usesUsed", findNode(used, "U1"));
    fixture.showProject();

    // The panel as MainWindow makes it: non-modal, which is the shape that has
    // the menu bar. The modal picker must NOT offer a bulk delete.
    gui::ResourceDialog dialog(fixture.via(), false, nullptr);
    dialog.setProject(&fixture.project());

    QMenuBar* menuBar = dialog.findChild<QMenuBar*>();
    CHECK(menuBar);
    QAction* action = nullptr;
    for (QAction* candidate : menuBar->actions()) {
        if (candidate->text() == QStringLiteral("Delete unused")) {
            action = candidate;
            break;
        }
    }
    CHECK(action);
    CHECK(action->isEnabled());

    action->trigger();

    CHECK(fixture.assetCount() == 1);
    CHECK(fixture.holdsAsset(used));
    CHECK(!fixture.holdsAsset(unused));
    CHECK(top->children().size() == 1); // the only layer was never touched
    return 0;
}

// ---------------------------------------------------------------------------
// 7. The modal variant -- the picker the layer panels open -- has no menu bar,
//    so it cannot carry a bulk delete. This is the design boundary, asserted so
//    a later edit that adds the action unconditionally fails here.
// ---------------------------------------------------------------------------
static int testModalPickerHasNoDeleteUnused() {
    Fixture fixture;
    fixture.registerAsset("a.psd", buildPsd({{"A1", "1", {(uint8)1, 2, 3}}}));
    fixture.showProject();

    gui::ResourceDialog dialog(fixture.via(), true, nullptr);
    dialog.setProject(&fixture.project());

    CHECK(dialog.findChild<QMenuBar*>() == nullptr);
    return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // A live GL context: core::LayerNode's default ImageKey binds GL through its
    // GridMesh, so the layers this test builds need one (cf. psd_layer_identity).
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setVersion(gl::Global::kVersion.first, gl::Global::kVersion.second);
    QSurfaceFormat::setDefaultFormat(fmt);
    QOpenGLWidget glWidget;
    glWidget.resize(4, 4);
    glWidget.show();
    glWidget.makeCurrent();
    if (!glWidget.context()) {
        std::printf("FAIL %d: GL context\n", __LINE__);
        return 1;
    }
    auto funcs = QOpenGLVersionFunctionsFactory::get<gl::Global::Functions>(glWidget.context());
    if (!funcs || !funcs->initializeOpenGLFunctions()) {
        std::printf("FAIL %d: GL funcs\n", __LINE__);
        return 1;
    }
    gl::Global::setContext(glWidget);
    gl::Global::setFunctions(*funcs);

    const struct {
        const char* name;
        int (*func)();
    } tests[] = {
        {"references found through the asset", testReferencesAreFoundThroughTheAsset},
        {"remove takes its layers in one undo step", testRemoveTakesItsLayersInOneUndoStep},
        {"unreferenced asset removed alone", testUnreferencedAssetIsRemovedAlone},
        {"delete unused removes only unreferenced", testDeleteUnusedRemovesOnlyUnreferenced},
        {"delete unused with nothing to delete", testDeleteUnusedWithNothingToDelete},
        {"dialog exposes delete unused", testDialogExposesDeleteUnused},
        {"modal picker has no delete unused", testModalPickerHasNoDeleteUnused},
    };

    int only = 0;
    if (argc > 1) {
        only = std::atoi(argv[1]);
    }

    int failed = 0;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        const auto& test = tests[i];
        if (only != 0 && (int)i + 1 != only) {
            continue;
        }
        std::fflush(stdout);
        const int result = test.func();
        std::fflush(stdout);
        if (result == 0) {
            std::printf("PASS %s\n", test.name);
        } else {
            std::printf("FAIL %s\n", test.name);
            ++failed;
        }
        std::fflush(stdout);
    }
    return failed == 0 ? 0 : 1;
}
