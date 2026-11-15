// Contract test for the PSD live-link safety rules.
//
// A relink (the "reload images from file" action, the watcher's autoreload, and
// the forced relink the paste path runs) used to be destructive in four ways that
// this test pins:
//
//   1. it was not transactional -- the batch opened a cmnd::ScopedMacro up front,
//      and every failure path returned from inside it, so the macro's destructor
//      committed whatever half of the batch had already been pushed (an
//      ImageSetter here, a tree append there) while the layer-remover and the
//      key-updater passes never ran. A failing relink left the document half
//      reloaded and broke the NEXT relink of the same asset.
//   2. forceReload handed the matcher the live tree as its own "new" tree. Every
//      layer compared itself with itself, compared equal, and the "nothing
//      changed" branch then FREED the live pixel buffer while the object's
//      ImageKeys still held a handle to it: a blank layer. The other half of that
//      path (a node imported with its pixels, so no lazy loader) bailed out with
//      every node of the asset already flagged abandoned, so the batch did
//      nothing but poison the next one.
//   3. a layer that has vanished from the source file was silently dropped or
//      silently kept: the remover pass skipped anything referenced (the imported
//      layers always were), so the keeped ones just kept a stale bitmap with no
//      note, and the removal flag stayed set on them, so a later relink of the
//      same asset acted on it.
//   4. a rename in the source onto a name the project already used was applied:
//      the layer's pixels, rect and blend mode were written onto the other
//      layer's live node, and every ImageKey hanging off it displayed the other
//      layer's bitmap.
//   5. the file watcher connected a handler that ignored which file changed, slept
//      500ms on the GUI thread, and reloaded its own item -- with two watched
//      PSDs a save of either fired both, and watch/unwatch cycles accumulated
//      handlers that outlived the tab.
//
//   6. the watch action cast a null action item and slept on the GUI thread.
//
// Sections: 1 rename and refusal (LL-02, LL-07) | 2 forceReload re-reads the
// file (LL-06) | 3 a vanished layer is kept, marked and reported (LL-03) |
// 4 relinks do not accumulate nodes (LL-04) | 5 the watcher (LL-08) |
// 6 the key updater's null guard (LL-10, skipped: see below) | 7 the id solver
// refuses an unresolved reference (LL-10).
//
// Headless: needs QApplication for the widget/log plumbing, no GL, no display.
// Section 6 cannot run without a GL context -- constructing a core::ImageKey
// binds a GL mesh buffer -- so it reports a skip BY NAME rather than asserting
// through a weaker route. Run it with an argument to run one section:
//   QT_QPA_PLATFORM=offscreen ./psd_relink_safety 3
//
// The orchestrator adds this target to tests/CMakeLists.txt (copied from the
// psd_rle_noop target, which links the same img stack). It compiles the gui
// call sites the relink and the watcher run through -- there is no gui library
// in this project -- plus the real ResourceDialog.cpp, which supplies the
// vtable the moc key functions live in. Everything else those sources reference
// but do not construct is stubbed in this file, by name.
//
//   add_executable(psd_relink_safety psd_relink_safety.cpp
//       ../src/gui/ResourceTreeWidget.cpp
//       ../src/gui/ProjectHook.cpp
//       ../src/gui/ViaPoint.cpp
//       ../src/gui/EasyDialog.cpp
//       ../src/gui/ResourceDialog.cpp
//       ../src/gui/res/res_ResourceUpdater.cpp
//       ../src/gui/res/res_Item.cpp
//       ../src/gui/res/res_Notifier.cpp)
//   target_include_directories(psd_relink_safety PRIVATE .. ../src ../src/gui)
//   target_compile_definitions(psd_relink_safety PRIVATE
//       AE_MAJOR_VERSION=1
//       AE_MINOR_VERSION=7
//       AE_MICRO_VERSION=1
//       USE_GL_CORE_PROFILE
//   )
//   target_link_libraries(psd_relink_safety PRIVATE
//       Qt6::Core Qt6::Core5Compat Qt6::Widgets
//       Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets
//       Qt6::Svg Qt6::Xml Qt6::Concurrent Qt6::Multimedia
//       cmnd core ctrl gl img thr util pugixml
//   )
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QMetaMethod>
#include <QStringList>
#include <QTemporaryDir>
#include <QTreeWidgetItem>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "XCAssert.h"
#include "XCReport.h"
#include "cmnd/Stack.h"
#include "gui/MainWindow.h"
#include "gui/ObjectTreeWidget.h"
#include "gui/ResourceTreeWidget.h"
#include "gui/ViaPoint.h"
#include "gui/res/res_Item.h"
#include "gui/res/res_ResourceUpdater.h"
#include "core/ImageKey.h"
#include "core/ImageKeyUpdater.h"
#include "core/ObjectNode.h"
#include "core/Project.h"
#include "core/ResourceEvent.h"
#include "core/TimeLine.h"
#include "img/ColorRGBA.h"
#include "img/PSDReader.h"
#include "img/ResourceData.h"
#include "img/ResourceNode.h"
#include "img/Util.h"
#include "util/IDSolver.h"

// ---------------------------------------------------------------------------
// Harness: assert/error handlers (a failure is a test failure, not a crash log)
// ---------------------------------------------------------------------------
namespace {

class TestAssertHandler : public XCAssertHandler {
public:
    void failure() const override { std::printf("FAIL: XC_ASSERT\n"); std::exit(1); }
};

class TestErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString& aInfo, const QString&) const override {
        std::printf("FAIL: XC_FATAL_ERROR %s\n", aInfo.toUtf8().data());
        std::exit(1);
    }
};

TestAssertHandler gAssertHandler;
TestErrorHandler gErrorHandler;
} // namespace

// The project's own handler pointers, at global scope because the prebuilt
// libraries the relink links against reference them too. A failing XC_ASSERT or
// XC_FATAL_ERROR is a test failure (see main), not a crash log.
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                        \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                                   \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (false)

namespace {

// ---------------------------------------------------------------------------
// A PSD writer: the contract tests need a real source file that a real
// img::PSDReader can parse, with layers that carry (or do not carry) a lyid.
// ---------------------------------------------------------------------------
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
    // padded pascal name: one length byte, the bytes, then padding so the whole
    // field (length byte included) is a multiple of four long.
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

// One additional layer info block. Per-layer blocks carry no padding.
void writeInfo(ByteWriter& aDst, const char* aKey, const std::string& aPayload) {
    aDst.raw("8BIM");
    aDst.raw(aKey);
    aDst.u32((uint32)aPayload.size());
    aDst.raw(aPayload);
}

struct LayerSpec {
    QString name;
    QString identity; // "lyid" value; empty = write no lyid block at all
    uint8 rgb[3];
};

std::string layerRecord(const LayerSpec& aSpec) {
    ByteWriter w;

    // rect: t, l, b, r -- a 2x2 layer at the origin
    w.u32(0);
    w.u32(0);
    w.u32(2);
    w.u32(2);

    // channels: 0, 1, 2, each holding the 2-byte compression id and a 2x2 plane
    w.u16(3);
    for (int c = 0; c < 3; ++c) {
        w.u16((uint16)c);
        w.u32(6);
    }

    // blend mode signature, blend mode, opacity, clipping, flags, padding
    w.raw("8BIM");
    w.raw("norm");
    w.u8(255);
    w.u8(0);
    w.u8(0);
    w.u8(0);

    // layer extra data
    ByteWriter extra;
    extra.u32(0); // layer mask: length 0
    extra.u32(0); // blending range: length 0
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
        w.u16(0); // compression id: raw
        for (int i = 0; i < 4; ++i) {
            w.u8(aSpec.rgb[c]);
        }
    }
    return w.data();
}

// A minimal, complete PSD: 8-bit RGB, two pixels square, the given layers and a
// merged image data section. Exactly the shape img::PSDReader accepts: the layer
// and mask info section holds every layer record first and the channel image
// data for all of them after it, the way a real PSD lays it out.
std::string buildPsd(const std::vector<LayerSpec>& aLayers) {
    ByteWriter records;
    ByteWriter channelData;
    for (const LayerSpec& spec : aLayers) {
        records.raw(layerRecord(spec));
        channelData.raw(layerChannelData(spec));
    }

    // the layer info section is the layer count followed by the records, and is
    // followed in the file by the channel image data
    ByteWriter layers;
    layers.u16((uint16)aLayers.size());
    layers.append(records);

    ByteWriter layerAndMask;
    layerAndMask.u32((uint32)(layers.data().size() + channelData.data().size()));
    layerAndMask.append(layers);
    layerAndMask.append(channelData);
    layerAndMask.u32(0); // global layer mask info: length 0

    ByteWriter file;
    file.raw("8BPS");
    file.u16(1); // version
    for (int i = 0; i < 6; ++i) {
        file.u8(0); // reserved
    }
    file.u16(3); // channels
    file.u32(2); // height
    file.u32(2); // width
    file.u16(8); // depth (PSDReader reads a 16-bit field)
    file.u16(3); // mode: RGB
    file.u32(0); // color mode data: length 0
    file.u32(0); // image resources: length 0
    file.u32((uint32)layerAndMask.data().size());
    file.append(layerAndMask);

    // merged image data: raw, three 2x2 planes
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
    // aLoadImage = false, which is what ResourceUpdater::reload does: the nodes
    // come back with their lazy image loaders, the image reading deferred.
    return img::Util::createResourceNodes(*reader.format(), false);
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

// The layer's texture is the image padded one pixel on every side (the bilinear
// border), so (1, 1) is the source's (0, 0) -- the whole layer here is one
// color.
img::ColorRGBA corePixel(const img::ResourceData& aData) {
    return *aData.image().rawPixel<img::ColorRGBA>(1, 1);
}

bool samePixel(const img::ColorRGBA& aLhs, const img::ColorRGBA& aRhs) {
    return aLhs.r() == aRhs.r() && aLhs.g() == aRhs.g() && aLhs.b() == aRhs.b() && aLhs.a() == aRhs.a();
}

class TestAnimator : public core::Animator {
public:
    virtual core::Frame currentFrame() const override { return core::Frame(); }
    virtual void stop() override {}
    virtual void suspend() override {}
    virtual void resume() override {}
    virtual bool isSuspended() const override { return false; }
};

// The project's log, captured. A relink reports through ViaPoint::pushLog rather
// than a modal box (see ResourceUpdater), which is also what makes the report
// observable here.
class LoggingViaPoint : public gui::ViaPoint {
public:
    LoggingViaPoint() : gui::ViaPoint(nullptr) {}

    const QStringList& notes() const { return mNotes; }
    bool hasNoteContaining(const QString& aText) const {
        for (const QString& note : mNotes) {
            if (note.contains(aText)) {
                return true;
            }
        }
        return false;
    }

    virtual void pushLog(const QString& aText, ctrl::UILogType) override { mNotes.push_back(aText); }

private:
    QStringList mNotes;
};

// One project, its resource tree(s), and the GUI tree a relink is driven
// through. The temp directory is the project's asset root, so the paths the
// project resolves are the files this test wrote.
class RelinkFixture {
public:
    RelinkFixture() :
        mDir(), mVia(), mAnimator(), mProject(QString(), mAnimator, nullptr),
        mWidget(mVia, false, nullptr), mUpdater(mVia, mProject) {
        mProject.setFileName(mDir.filePath("project.anie"));
    }
    LoggingViaPoint& log() { return mVia; }
    gui::ResourceTreeWidget& tree() { return mWidget; }
    core::Project& project() { return mProject; }
    gui::res::ResourceUpdater& updater() { return mUpdater; }
    cmnd::Stack& stack() { return mProject.commandStack(); }

    QString absPath(const QString& aRelPath) const { return mDir.filePath(aRelPath); }

    bool overwrite(const QString& aRelPath, const std::string& aPsd) {
        return writeBytes(absPath(aRelPath), aPsd);
    }

    // A signal cannot be emitted on an object from outside its own class, so the
    // notification is delivered the way the OS delivers it: by invoking the
    // signal through its meta method. The handler under test only reads the path
    // argument, which is what this pins.
    void fireWatcher(QFileSystemWatcher* aWatcher, const QString& aPath) {
        const QMetaMethod signal = QMetaMethod::fromSignal(&QFileSystemWatcher::fileChanged);
        if (!signal.isValid() || !signal.invoke(aWatcher, Q_ARG(QString, aPath))) {
            std::printf("FAIL: could not deliver the watcher's notification\n");
            std::exit(1);
        }
    }

    // The per-path handler owner this widget created for the watched file: at
    // most one per path is the property being pinned.
    int handlerCount(const QString& aPath) const {
        return mWidget.findChildren<QObject*>(aPath).count();
    }

    // Parses a source file and registers it as one of the project's assets, the
    // way loading one does (lazy image loaders, as reload() re-parses them).
    img::ResourceNode* registerAsset(const QString& aRelPath, const std::string& aPsd) {
        if (!overwrite(aRelPath, aPsd)) {
            return nullptr;
        }
        std::unique_ptr<img::ResourceNode> root(createTreeFromPsd(readBack(aRelPath)));
        if (!root) {
            return nullptr;
        }
        mProject.resourceHolder().pushImageTree(*root, absPath(aRelPath));
        return root.release();
    }

    // Rebuilds the resource tree widget from the project's assets.
    void showProject() { mWidget.setProject(&mProject); }

    gui::res::Item* itemFor(const img::ResourceNode* aRoot) {
        for (int i = 0; i < mWidget.topLevelItemCount(); ++i) {
            if (gui::res::Item* item = gui::res::Item::cast(mWidget.topLevelItem(i))) {
                if (&item->node() == aRoot) {
                    return item;
                }
            }
        }
        return nullptr;
    }

    QTreeWidgetItem* layerItem(QTreeWidgetItem* aItem, const img::ResourceNode* aLayer) {
        if (gui::res::Item* item = gui::res::Item::cast(aItem)) {
            if (&item->node() == aLayer) {
                return aItem;
            }
        }
        for (int i = 0; i < aItem->childCount(); ++i) {
            if (QTreeWidgetItem* found = layerItem(aItem->child(i), aLayer)) {
                return found;
            }
        }
        return nullptr;
    }

    QTreeWidgetItem* layerItem(const img::ResourceNode* aLayer) {
        for (int i = 0; i < mWidget.topLevelItemCount(); ++i) {
            if (QTreeWidgetItem* found = layerItem(mWidget.topLevelItem(i), aLayer)) {
                return found;
            }
        }
        return nullptr;
    }

    // What a project object that renders from this layer does to its handle:
    // an origin-keeping reference, which is why an imported layer's cached
    // bitmap is the user's work and is never dropped by a relink. The holders
    // live in a list because a ResourceHandle does not carry its keep-origin
    // flag through a copy, so a reallocating container would drop the reference
    // as it grows.
    void referenceLayer(img::ResourceNode& aLayer) {
        mKeepers.push_back(aLayer.handle());
        mKeepers.back().setOriginKeeping(true);
    }

    std::string readBack(const QString& aRelPath) const {
        QFile file(absPath(aRelPath));
        if (!file.open(QFile::ReadOnly)) {
            return std::string();
        }
        const QByteArray bytes = file.readAll();
        return std::string(bytes.constData(), (size_t)bytes.size());
    }

private:
    QTemporaryDir mDir;
    LoggingViaPoint mVia;
    TestAnimator mAnimator;
    core::Project mProject;
    gui::ResourceTreeWidget mWidget;
    gui::res::ResourceUpdater mUpdater;
    std::list<img::ResourceHandle> mKeepers;
};
} // namespace

// ---------------------------------------------------------------------------
// Link stubs for the gui classes these sources reference but that this test does
// not drive, following the convention in tests/creation_select_keys.cpp.
//
// gui::ResourceDialog is NOT stubbed here: the target compiles the real
// src/gui/ResourceDialog.cpp, which is what supplies its vtable and its moc key
// functions. That file's only extra include is gui/MainMenuBar.h, and its only
// use of it is a type name inside a connect -- no MainMenuBar:: method is
// called -- so nothing beyond the Qt libraries is needed to link it.
// ---------------------------------------------------------------------------
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace gui {
void KeyCommandInvoker::onKeyPressed(const QKeyEvent*) {}
void KeyCommandInvoker::onKeyReleased(const QKeyEvent*) {}
} // namespace gui

// The watcher this test drives, and the popups the watch action raises (they
// would block a headless run, so they are recorded nowhere and return).
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

// The asset-removal path (res_ResourceUpdater::remove) reaches the object tree to
// delete the layers that reference a removed asset. This test drives only
// reload/load, never removal, so that one symbol is stubbed rather than dragging
// the whole object-tree view -- and the GL-bound layers it builds -- into a
// target that is deliberately headless.
namespace gui {
void ObjectTreeWidget::pushDeleteObjects(const QVector<core::ObjectNode*>&, cmnd::ScopedMacro&) {}
bool ObjectTreeWidget::canDeleteObjects(const QVector<core::ObjectNode*>&) const { return false; }
} // namespace gui

// ---------------------------------------------------------------------------
// 1. A relink that refuses applies nothing (LL-07), and a rename onto a name the
//    project already uses is reported rather than re-pointed (LL-02).
// ---------------------------------------------------------------------------
static int testRefusedRelinkAppliesNothing() {
    // v1: the source has layers A and B, both held by identity.
    const std::string v1 = buildPsd({{"A", "1", {(uint8)200, 30, 30}}, {"B", "2", {(uint8)30, 30, 200}}});
    // v2: layer A (identity 1) is renamed onto B's name, and the source's own B is
    // gone. Matching by name alone accepts the single same-named candidate and
    // writes this layer's pixels onto the OTHER layer's node.
    const std::string v2 = buildPsd({{"B", "1", {(uint8)10, 240, 10}}});

    RelinkFixture fixture;
    img::ResourceNode* root = fixture.registerAsset("asset.psd", v1);
    CHECK(root);
    fixture.showProject();
    gui::res::Item* item = fixture.itemFor(root);
    CHECK(item);

    img::ResourceNode* layerA = findNode(root, "A");
    img::ResourceNode* layerB = findNode(root, "B");
    CHECK(layerA);
    CHECK(layerB);
    // the project renders from both, so neither may be deleted by a relink
    fixture.referenceLayer(*layerA);
    fixture.referenceLayer(*layerB);
    CHECK(layerA->children().empty());
    CHECK(layerB->children().empty());

    // snapshot the document as the relink finds it
    layerA->data().loadImage();
    layerB->data().loadImage();
    const img::ColorRGBA pixelA = corePixel(layerA->data());
    const img::ColorRGBA pixelB = corePixel(layerB->data());
    const size_t childCount = root->children().size();
    const int itemChildCount = item->childCount();
    const bool wasEdited = fixture.stack().isEdited();

    CHECK(fixture.overwrite("asset.psd", v2));

    fixture.updater().reload(*item);

    // and the document is exactly as it was: no half-applied batch, no layer
    // data moved onto another layer's node, no orphaned GUI item. This is the
    // assertion the defect trips -- before the fix, A's pixels were written onto
    // B's live node, so B rendered the other layer's bitmap.
    CHECK(root->children().size() == childCount);
    CHECK(item->childCount() == itemChildCount);
    CHECK(samePixel(corePixel(layerA->data()), pixelA));
    CHECK(samePixel(corePixel(layerB->data()), pixelB));
    CHECK(fixture.stack().isEdited() == wasEdited);
    CHECK(!layerA->isAbandoned());
    CHECK(!layerB->isAbandoned());

    // the note names the layer the source renamed, and says nothing was applied
    CHECK(fixture.log().hasNoteContaining(layerA->treePath()));
    CHECK(fixture.log().hasNoteContaining("Nothing was changed"));

    // and the refusal is repeatable: the same relink does not drift the document
    // the second time either
    fixture.updater().reload(*item);
    CHECK(samePixel(corePixel(layerA->data()), pixelA));
    CHECK(samePixel(corePixel(layerB->data()), pixelB));
    CHECK(!layerA->isAbandoned());
    CHECK(!layerB->isAbandoned());

    // the same rule for a rename onto a name the project does not have yet: the
    // layer keeps its node (no orphan clone), the project's copy of it is not
    // deleted, and the batch is refused rather than applied
    const std::string moved = buildPsd({{"A", "1", {(uint8)200, 30, 30}}, {"C", "2", {(uint8)30, 30, 200}}});
    CHECK(fixture.overwrite("asset.psd", moved));
    const size_t childCountBeforeMove = root->children().size();

    fixture.updater().reload(*item);

    CHECK(fixture.log().hasNoteContaining("renamed"));
    CHECK(root->children().size() == childCountBeforeMove);
    CHECK(item->childCount() == itemChildCount);
    CHECK(findNode(root, "C") == nullptr);
    CHECK(samePixel(corePixel(layerA->data()), pixelA));
    CHECK(samePixel(corePixel(layerB->data()), pixelB));
    return 0;
}

// ---------------------------------------------------------------------------
// 2. forceReload re-reads the file instead of comparing the live tree with
//    itself: the layer's bitmap survives and no node is left flagged (LL-06).
// ---------------------------------------------------------------------------
static int testForceReloadKeepsTheLayerBitmap() {
    // (a) a normally imported node: its pixels were read at import, so it has no
    //     lazy loader. The self-comparison path used to bail out of the loader
    //     call with the node already flagged abandoned, so the force did
    //     nothing at all -- including re-reading a source that had changed.
    const std::string v1 = buildPsd({{"L", "5", {(uint8)60, 120, 180}}});
    const std::string v2 = buildPsd({{"L", "5", {(uint8)180, 120, 60}}}); // the same layer, edited

    RelinkFixture eager;
    const QString path = eager.absPath("eager.psd");
    CHECK(eager.overwrite("eager.psd", v1));
    eager.updater().load(eager.tree(), path);
    CHECK(eager.tree().topLevelItemCount() == 1);
    gui::res::Item* item = gui::res::Item::cast(eager.tree().topLevelItem(0));
    CHECK(item);

    img::ResourceNode* live = findNode(&item->node(), "L");
    CHECK(live);
    CHECK(live->data().hasImage());
    const img::ColorRGBA before = corePixel(live->data());

    // the source file changed on disk; a forced relink must pick it up
    CHECK(eager.overwrite("eager.psd", v2));
    eager.updater().forceReload(*item);

    // the layer renders the source's new pixels, and is not flagged for removal
    CHECK(live->data().hasImage());
    CHECK(!samePixel(corePixel(live->data()), before));
    CHECK(!live->isAbandoned());

    // (b) a node whose pixels are read lazily and whose loader has not run yet.
    //     The self-comparison path used to load the live node with itself and
    //     then FREE the very buffer the project's ImageKeys still hold a handle
    //     to, leaving a blank layer.
    RelinkFixture lazy;
    img::ResourceNode* lazyRoot = lazy.registerAsset("lazy.psd", v1);
    CHECK(lazyRoot);
    lazy.showProject();
    gui::res::Item* lazyItem = lazy.itemFor(lazyRoot);
    CHECK(lazyItem);
    img::ResourceNode* lazyLayer = findNode(lazyRoot, "L");
    CHECK(lazyLayer);
    CHECK(!lazyLayer->data().hasImage()); // its loader has not run

    lazy.updater().forceReload(*lazyItem);

    // the layer still renders: the forced relink read the file rather than
    // destroying the live buffer through a self-comparison
    CHECK(lazyLayer->data().hasImage());
    CHECK(lazyLayer->data().image().data() != nullptr);
    CHECK(!lazyLayer->isAbandoned());
    return 0;
}

// ---------------------------------------------------------------------------
// 3. A layer the source no longer has is kept, not dropped silently: the project
//    still renders from its bitmap, and the disappearance is reported and marked
//    (LL-03).
// ---------------------------------------------------------------------------
static int testAbsentLayerIsKeptAndReported() {
    const std::string v1 = buildPsd({{"kept", "1", {(uint8)90, 90, 30}}, {"dropped", "2", {(uint8)30, 90, 90}}});
    const std::string v2 = buildPsd({{"kept", "1", {(uint8)240, 240, 30}}}); // "dropped" is gone

    RelinkFixture fixture;
    img::ResourceNode* root = fixture.registerAsset("asset.psd", v1);
    CHECK(root);
    fixture.showProject();
    gui::res::Item* item = fixture.itemFor(root);
    CHECK(item);

    img::ResourceNode* kept = findNode(root, "kept");
    img::ResourceNode* dropped = findNode(root, "dropped");
    CHECK(kept);
    CHECK(dropped);
    kept->data().loadImage();
    dropped->data().loadImage();
    const img::ColorRGBA droppedPixel = corePixel(dropped->data());
    const uint8* droppedPixels = dropped->data().image().data();

    // the project renders from both layers, the way an imported PSD's default
    // ImageKey holds every layer's handle
    fixture.referenceLayer(*kept);
    fixture.referenceLayer(*dropped);

    CHECK(fixture.overwrite("asset.psd", v2));

    fixture.updater().reload(*item);

    // the layer that vanished is still in the document and still renders
    CHECK(findNode(root, "dropped") == dropped);
    CHECK(root->children().size() == 2);
    CHECK(item->childCount() == 2);
    CHECK(dropped->data().hasImage());
    CHECK(dropped->data().image().data() == droppedPixels);
    CHECK(samePixel(corePixel(dropped->data()), droppedPixel));

    // it is not carrying a removal flag a later relink would act on
    CHECK(!dropped->isAbandoned());

    // and it is marked in the resource tree so a stale bitmap is never mistaken
    // for a live one
    QTreeWidgetItem* droppedItem = fixture.layerItem(dropped);
    CHECK(droppedItem);
    CHECK(droppedItem->toolTip(gui::ResourceTreeWidget::kItemColumn).contains("source file"));

    // the note on the relink names the layer that was not found
    CHECK(fixture.log().hasNoteContaining("dropped"));

    // the layer that is still in the source was reloaded, not discarded
    CHECK(findNode(root, "kept") == kept);
    return 0;
}

// ---------------------------------------------------------------------------
// 4. A layer that has gone from the source and that nothing references is not a
//    leak: repeated relinks do not accumulate resource nodes (LL-04).
// ---------------------------------------------------------------------------
static int testRepeatedRelinksDoNotAccumulateNodes() {
    const std::string oneLayer = buildPsd({{"A", "1", {(uint8)10, 10, 200}}});
    const std::string twoLayers = buildPsd({{"A", "1", {(uint8)10, 10, 200}}, {"B", "2", {(uint8)200, 10, 10}}});
    // B renamed to C: the identity is unchanged, so the layer IS the project's
    // B and must not be appended as a new node.
    const std::string renamed = buildPsd({{"A", "1", {(uint8)10, 10, 200}}, {"C", "2", {(uint8)200, 10, 10}}});

    RelinkFixture fixture;
    img::ResourceNode* root = fixture.registerAsset("asset.psd", oneLayer);
    CHECK(root);
    fixture.showProject();
    gui::res::Item* item = fixture.itemFor(root);
    CHECK(item);

    // a new layer in the source: appended, because it genuinely has no
    // counterpart in the project
    CHECK(fixture.overwrite("asset.psd", twoLayers));
    fixture.updater().reload(*item);
    img::ResourceNode* added = findNode(root, "B");
    CHECK(added);
    CHECK(root->children().size() == 2);
    CHECK(item->childCount() == 2);

    // the append is content the project does not reference, so a relink after
    // the source drops it cleans it up instead of keeping a clone
    CHECK(fixture.overwrite("asset.psd", oneLayer));
    fixture.updater().reload(*item);
    CHECK(findNode(root, "B") == nullptr);
    CHECK(root->children().size() == 1);

    // that cycle repeats without growing the tree
    for (int i = 0; i < 3; ++i) {
        CHECK(fixture.overwrite("asset.psd", twoLayers));
        fixture.updater().reload(*item);
        CHECK(root->children().size() == 2);

        CHECK(fixture.overwrite("asset.psd", oneLayer));
        fixture.updater().reload(*item);
        CHECK(root->children().size() == 1);
        CHECK(item->childCount() == 1);
    }

    // put the appended layer back for the rename checks below
    CHECK(fixture.overwrite("asset.psd", twoLayers));
    fixture.updater().reload(*item);
    CHECK(root->children().size() == 2);

    // a rename is never applied as an append: the layer keeps its node, the
    // batch refuses, and repeated relinks of the renamed source do not add one
    // node each time
    const uint8* addedPixels = nullptr;
    for (int i = 0; i < 3; ++i) {
        added = findNode(root, "B");
        CHECK(added);
        added->data().loadImage();
        addedPixels = added->data().image().data();

        CHECK(fixture.overwrite("asset.psd", renamed));
        fixture.updater().reload(*item);
        CHECK(root->children().size() == 2);
        CHECK(item->childCount() == 2);
        // the node is the project's layer, with its own bitmap: nothing was
        // appended for it and no other layer's pixels landed on it
        CHECK(findNode(root, "B") == added);
        CHECK(findNode(root, "C") == nullptr);
        CHECK(added->data().image().data() == addedPixels);
    }
    CHECK(fixture.log().hasNoteContaining("renamed"));
    return 0;
}

// ---------------------------------------------------------------------------
// 5. The file watcher reloads only the asset whose file changed, with one
//    handler per watched file, and blocks no thread while doing it (LL-08).
// ---------------------------------------------------------------------------
static int testWatcherReloadsOnlyTheChangedFile() {
    const std::string a1 = buildPsd({{"M", "1", {(uint8)10, 10, 10}}, {"G", "2", {(uint8)20, 20, 20}}});
    const std::string a2 = buildPsd({{"M", "1", {(uint8)10, 10, 10}}}); // G is gone
    const std::string b1 = buildPsd({{"N", "10", {(uint8)30, 30, 30}}, {"H", "20", {(uint8)40, 40, 40}}});
    const std::string b2 = buildPsd({{"N", "10", {(uint8)30, 30, 30}}}); // H is gone

    RelinkFixture fixture;
    img::ResourceNode* rootA = fixture.registerAsset("a.psd", a1);
    img::ResourceNode* rootB = fixture.registerAsset("b.psd", b1);
    CHECK(rootA);
    CHECK(rootB);
    fixture.showProject();

    gui::res::Item* itemA = fixture.itemFor(rootA);
    gui::res::Item* itemB = fixture.itemFor(rootB);
    CHECK(itemA);
    CHECK(itemB);

    // the layers that are about to vanish are held by the project, the way an
    // imported layer's default ImageKey holds its bitmap. That is what makes the
    // relink report them by name, which is how this test tells which asset a
    // reported change actually reloaded.
    img::ResourceNode* layerG = findNode(rootA, "G");
    img::ResourceNode* layerH = findNode(rootB, "H");
    CHECK(layerG);
    CHECK(layerH);
    fixture.referenceLayer(*layerG);
    fixture.referenceLayer(*layerH);

    QFileSystemWatcher* watcher = gui::MainWindow::getWatcher();
    CHECK(watcher);

    // watch both, through the action the context menu triggers
    fixture.tree().setCurrentItem(itemA);
    fixture.tree().onWatchTriggered(true);
    fixture.tree().setCurrentItem(itemB);
    fixture.tree().onWatchTriggered(true);

    CHECK(watcher->files().contains(fixture.absPath("a.psd")));
    CHECK(watcher->files().contains(fixture.absPath("b.psd")));
    // one handler per watched path, nothing accumulated
    CHECK(fixture.handlerCount(fixture.absPath("a.psd")) == 1);
    CHECK(fixture.handlerCount(fixture.absPath("b.psd")) == 1);

    // both files change on disk; only b's change is reported
    CHECK(fixture.overwrite("a.psd", a2));
    CHECK(fixture.overwrite("b.psd", b2));
    CHECK(fixture.log().notes().isEmpty());

    // and it must not block the GUI thread to do it: the handler used to sleep
    // half a second before reloading
    QElapsedTimer elapsed;
    elapsed.start();
    fixture.fireWatcher(watcher, fixture.absPath("b.psd"));
    const qint64 spent = elapsed.elapsed();

    // only b was reloaded: its vanished layer is reported, a's is not
    CHECK(fixture.log().hasNoteContaining("H"));
    CHECK(spent < 300); // no half-second sleep on the GUI thread
    CHECK(!fixture.log().hasNoteContaining("G"));
    // a's document is untouched by b's save
    CHECK(findNode(rootA, "G") != nullptr);
    CHECK(itemA->childCount() == 2);
    CHECK(rootA->children().size() == 2);

    // and b's save reloaded b: its vanished layer is kept -- the project renders
    // from it -- is not left flagged, and is named in the note above
    CHECK(findNode(rootB, "H") == layerH);
    CHECK(rootB->children().size() == 2);
    CHECK(itemB->childCount() == 2);
    CHECK(!layerH->isAbandoned());

    // now a's change is reported, and only a is reloaded
    fixture.fireWatcher(watcher, fixture.absPath("a.psd"));
    CHECK(fixture.log().hasNoteContaining("G"));
    CHECK(findNode(rootA, "G") == layerG);
    CHECK(itemA->childCount() == 2);
    CHECK(!layerG->isAbandoned());

    // a watch that is removed does not leave its handler behind, and a
    // re-watch does not stack a second one on top of the first
    fixture.tree().setCurrentItem(itemA);
    fixture.tree().onWatchRemoveTriggered(true);
    CHECK(!watcher->files().contains(fixture.absPath("a.psd")));
    CHECK(fixture.handlerCount(fixture.absPath("a.psd")) == 0);

    fixture.tree().setCurrentItem(itemA);
    fixture.tree().onWatchTriggered(true);
    CHECK(watcher->files().contains(fixture.absPath("a.psd")));
    CHECK(fixture.handlerCount(fixture.absPath("a.psd")) == 1);

    // with nothing watched, a late change is a no-op (the handler that would
    // have reloaded through a dead item is gone)
    fixture.tree().setCurrentItem(itemA);
    fixture.tree().onWatchRemoveTriggered(true);
    fixture.tree().setCurrentItem(itemB);
    fixture.tree().onWatchRemoveTriggered(true);
    CHECK(fixture.handlerCount(fixture.absPath("b.psd")) == 0);
    const int notesBefore = fixture.log().notes().count();
    fixture.fireWatcher(watcher, fixture.absPath("b.psd"));
    CHECK(fixture.log().notes().count() == notesBefore);
    return 0;
}

// ---------------------------------------------------------------------------
// 6. The key updater skips an ImageKey whose image reference never resolved
//    instead of dereferencing it (LL-10, the call-site half).
//
// NOT RUNNABLE HEADLESS, and reported as a skip rather than weakened.
//
// The missing precondition is a live GL context: constructing a core::ImageKey
// at all binds a GL object on construction (its Data holds a GridMesh, whose mesh
// buffer is created by a GLBinder that makes the global GL context current), and
// QT_QPA_PLATFORM=offscreen provides none -- an offscreen QOpenGLWidget never
// receives initializeGL there, measured. The guard this section would exercise
// (ImageReloader::tryPushTarget refuses a key with no resource handle) sits one
// line beside the populated path, but with no ImageKey constructible no
// assertion for it can run at all. Skipped, not weakened: the section states why
// it cannot run instead of asserting something weaker that does.
// ---------------------------------------------------------------------------
static int testKeyUpdaterSkipsAnUnresolvedKey() {
    std::printf(
        "SKIP %s (needs a live GL context; QT_QPA_PLATFORM=offscreen provides none, so no "
        "core::ImageKey can be constructed)\n",
        __func__);
    return 0;
}

// ---------------------------------------------------------------------------
// 7. An unresolved id reference refuses the load instead of handing every solver
//    a null (LL-10, the solver half).
// ---------------------------------------------------------------------------
static int testUnresolvedIdReferenceRefusesTheLoad() {
    util::IDSolver<void*> solver;
    int solvedCount = 0;
    solver.pushReferencer(3, [&solvedCount](void*) { ++solvedCount; });

    // the id was never bound: the load must fail, and no solver may be handed a
    // value it would dereference
    CHECK(!solver.solve());
    CHECK(solvedCount == 0);
    CHECK(solver.unboundIds().count() == 1);
    CHECK(solver.unboundIds().front() == 3);

    // the unbound ids are recorded for whoever reports the failure
    CHECK(solver.unboundIds().contains(3));

    // once the reference is bound, the same solver set solves
    int data = 0;
    solver.pushData(3, &data);
    CHECK(solver.solve());
    CHECK(solvedCount == 1);
    CHECK(solver.unboundIds().isEmpty());

    // a mix: the bound reference is solved, and the load still fails on the
    // unbound one rather than handing it a null
    util::IDSolver<void*> mixed;
    int mixedCount = 0;
    int bound = 0;
    mixed.pushData(9, &bound);
    mixed.pushReferencer(9, [&mixedCount](void*) { ++mixedCount; });
    mixed.pushReferencer(10, [&mixedCount](void*) { ++mixedCount; });
    CHECK(!mixed.solve());
    CHECK(mixedCount == 1);
    CHECK(mixed.unboundIds() == QVector<int>({10}));
    return 0;
}

// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // The log notes are the report channel under test; they are part of the
    // contract and are asserted as such.
    QApplication app(argc, argv);

    const struct {
        const char* name;
        int (*func)();
    } tests[] = {
        {"refused relink applies nothing", testRefusedRelinkAppliesNothing},
        {"forceReload keeps the layer bitmap", testForceReloadKeepsTheLayerBitmap},
        {"absent layer is kept and reported", testAbsentLayerIsKeptAndReported},
        {"repeated relinks do not accumulate nodes", testRepeatedRelinksDoNotAccumulateNodes},
        {"watcher reloads only the changed file", testWatcherReloadsOnlyTheChangedFile},
        // skipped: needs a GL context, see the section's comment
        {"key updater skips an unresolved key", testKeyUpdaterSkipsAnUnresolvedKey},
        {"unresolved id reference refuses the load", testUnresolvedIdReferenceRefusesTheLoad},
    };

    // an optional argument runs a single section (1-based), so a failure can be
    // reproduced on its own
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
