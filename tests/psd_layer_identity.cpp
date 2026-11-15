// Regression test for the PSD live-link layer identity defects.
//
// 1. A layer's identity is Photoshop's stable "lyid", not the display name. The
//    display name is what the project used to key reimport matching on, so a
//    rename in the source orphaned the layer's edits and two layers renamed to
//    the same name matched to each other. Here a crafted PSD carries two layers
//    both named "dup" with different ids, and the resource tree must keep them
//    apart.
//
// 2. The lazy image loader captured a pointer into the img::PSDFormat it was
//    built from. The relink path owns that format on the stack and destroys it
//    as soon as createResourceNodes returns, so every node that survived a
//    reload held a closure into freed memory. Here the format is destroyed
//    before the loader is driven, and the loader must still produce the pixels it
//    decoded from that file -- and must not run a second time.
//
// 3. Deserializing a pasted image key bound it to the LAST matching tree with no
//    ambiguity check, so a paste whose layer name existed in two image trees
//    attached a wrong image under a right-looking name. Here the same-named
//    layers in two trees must resolve to the node whose id the key names, and a
//    key with no id at all must be REFUSED rather than last-matched.
//
// 4. A fill layer stores its appearance in a "SoCo"/"GdFl"/"PtFl" block and
//    carries no RGB planes, so makeInterleavedImage returns an empty block and
//    the import used to fail the WHOLE file on it. Here the import must succeed,
//    keep the layer as a 1x1 transparent placeholder, and report one warning that
//    names the layer and its kind. See img::psdUnsupportedLayerKind.
//
// 5. Layer masks are parsed and then ignored by the compositor, which is a silent
//    "my file looks different" cause. One aggregated warning must say how many
//    masks were dropped, without disturbing the masked layer's pixels.
//
// Run: xvfb-run -a ./psd_layer_identity
//
// Core: core::ImageKey's GridMesh binds GL in its constructor, and the loader
// cases build core::LayerNode, whose MeshTransformer reads :/shaders -- so both
// need a live GL context, set up in main() the same way roundtrip and
// media_exporter_e2e set theirs up, and the target embeds assets.qrc. Nothing is
// rendered -- only a current context is required. (QT_QPA_PLATFORM=offscreen
// alone is not enough: it provides no GL context.)
#include <QApplication>
#include <QByteArray>
#include <QChar>
#include <QFile>
#include <QJsonObject>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctions>
#include <QString>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "XC.h"
#include "gl/DeviceInfo.h"
#include "gl/Global.h"
#include "core/ImageKey.h"
#include "core/MaskKey.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "ctrl/ImageFileLoader.h"
#include "img/ColorRGBA.h"
#include "img/BlendMode.h"
#include "img/PSDFormat.h"
#include "img/PSDReader.h"
#include "img/ResourceData.h"
#include "img/ResourceNode.h"
#include "img/Util.h"
#include "util/IProgressReporter.h"
#include "XCAssert.h"
#include "XCReport.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to load
// a .aemus sidecar, which this test never has). Stub it so the link succeeds.
#include "gui/AudioPlaybackWidget.h"
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

class HarnessAssertHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

namespace {

class NullAnimator : public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

// The loader wants a progress sink; a harness has no user to report to.
class NullProgressReporter : public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

#define CHECK(cond)                                                                                          \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            std::printf("FAIL %d: %s\n", __LINE__, #cond);                                                   \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (false)

// Big-endian writer for the crafted PSD bytes.
class ByteWriter {
public:
    void u8(uint8 aValue) { mData.push_back((char)aValue); }
    void u16(uint16 aValue)
    {
        u8((uint8)((aValue >> 8) & 0xff));
        u8((uint8)(aValue & 0xff));
    }
    void u32(uint32 aValue)
    {
        u8((uint8)((aValue >> 24) & 0xff));
        u8((uint8)((aValue >> 16) & 0xff));
        u8((uint8)((aValue >> 8) & 0xff));
        u8((uint8)(aValue & 0xff));
    }
    void raw(const std::string& aValue) { mData += aValue; }
    void append(const ByteWriter& aRhs) { mData += aRhs.mData; }

    // padded pascal name: one length byte, the bytes, then padding so that the
    // whole field is a multiple of four long.
    void name(const QString& aValue)
    {
        const QByteArray bytes = aValue.toUtf8();
        u8((uint8)bytes.size());
        mData.append(bytes.constData(), bytes.size());
        size_t total = 1 + (size_t)bytes.size();
        while (total % 4 != 0) {
            u8(0);
            ++total;
        }
    }

    const std::string& data() const { return mData; }

private:
    std::string mData;
};

// One additional layer info block. Per-layer blocks carry no padding.
void writeInfo(ByteWriter& aDst, const char* aKey, const std::string& aPayload)
{
    aDst.raw("8BIM");
    aDst.raw(aKey);
    aDst.u32((uint32)aPayload.size());
    aDst.raw(aPayload);
}

struct LayerSpec {
    QString name;
    QString identity;    // "lyid" value; empty = write no lyid block at all
    QString unicodeName; // "luni" value; empty = write no luni block at all
    bool unicodeNameCountsNull; // whether the block's count includes the null
    uint8 rgb[3];
    // A fill-layer key ("SoCo"/"GdFl"/"PtFl"). A fill layer stores its appearance
    // in this block and carries no RGB planes, which is exactly the shape the
    // importer used to fail on.
    const char* fillKey = nullptr;
    // Write a 20-byte layer-mask record, the shape PSDReader parses into `mask`.
    bool masked = false;
    // ...and the mask's own PIXELS: channel id -2 over the mask rect, 0 on the left
    // column and 255 on the right (hidden / visible), so an assertion can name both.
    bool maskChannel = false;
    // An "lsct" section divider: 1 opens a group, 3 closes it (PSDFormat's enum).
    int divider = -1;
};

std::string layerRecord(const LayerSpec& aSpec)
{
    ByteWriter w;

    // A fill layer has no RGB planes; only its mask channel may exist, and the
    // classifier must see the channels list without 0/1/2.
    const bool hasRgb = aSpec.fillKey == nullptr;
    const int channelCount = (hasRgb ? 3 : 0) + (aSpec.maskChannel ? 1 : 0);

    // rect: t, l, b, r -- the layer is one 2x2 image at the origin
    w.u32(0);
    w.u32(0);
    w.u32(2);
    w.u32(2);

    // channels: 0, 1, 2, each holding the 2-byte compression id and a 2x2 plane
    // (the record's length field counts the compression id, so 6)
    w.u16((uint16)channelCount);
    for (int c = 0; c < (hasRgb ? 3 : 0); ++c) {
        w.u16((uint16)c);
        w.u32(6);
    }
    if (aSpec.maskChannel) {
        w.u16((uint16)-2);   // the user mask channel
        w.u32(6);
    }

    // blend mode signature, blend mode, opacity, clipping, flags, padding
    w.raw("8BIM");
    w.raw("norm");
    w.u8(255);
    w.u8(0);
    w.u8(0);
    w.u8(0);

    // layer extra data: layer mask, blending range, padded name, then the
    // additional layer info blocks ("lyid" and "luni" here)
    ByteWriter extra;
    if (aSpec.masked) {
        // a 20-byte S14 layer mask: rect (16), default colour, flags, 2 pad
        extra.u32(20);
        extra.u32(0);
        extra.u32(0);
        extra.u32(2);
        extra.u32(2);
        extra.u8(0); // default colour
        extra.u8(0); // flags
        extra.u16(0); // padding to the declared length
    } else {
        extra.u32(0); // layer mask: length 0
    }
    extra.u32(0); // blending range: length 0
    extra.name(aSpec.name);

    if (!aSpec.identity.isEmpty()) {
        ByteWriter id;
        id.u32((uint32)aSpec.identity.toUInt());
        writeInfo(extra, "lyid", id.data());
    }
    if (aSpec.divider >= 0) {
        ByteWriter section;
        section.u32((uint32)aSpec.divider);
        writeInfo(extra, "lsct", section.data());
    }
    if (!aSpec.unicodeName.isEmpty()) {
        const QString text = aSpec.unicodeName;
        std::vector<unsigned short> units;
        for (const QChar qc : text) {
            units.push_back((unsigned short)qc.unicode());
        }
        // the count convention differs between writers, so the spec decides
        const uint32 count = (uint32)units.size() + (aSpec.unicodeNameCountsNull ? 1 : 0);

        ByteWriter uni;
        uni.u32(count);
        for (unsigned short unit : units) {
            uni.u8((uint8)((unit >> 8) & 0xff));
            uni.u8((uint8)(unit & 0xff));
        }
        if (aSpec.unicodeNameCountsNull) {
            uni.u8(0);
            uni.u8(0);
        }
        writeInfo(extra, "luni", uni.data());
    }
    if (aSpec.fillKey) {
        // The classifier only scans for the key; the real block's payload is
        // Photoshop's fill settings, which the importer never reads.
        writeInfo(extra, aSpec.fillKey, std::string());
    }

    w.u32((uint32)extra.data().size());
    return w.data() + extra.data();
}

// the channel image data that follows all layer records: one compressed id and
// one 2x2 plane per channel. A fill layer declared no channels, so it contributes
// no bytes here.
std::string layerChannelData(const LayerSpec& aSpec)
{
    ByteWriter w;
    if (aSpec.fillKey && !aSpec.maskChannel) {
        return w.data();
    }
    for (int c = 0; c < (aSpec.fillKey ? 0 : 3); ++c) {
        w.u16(0); // compression id: raw
        for (int i = 0; i < 4; ++i) {
            w.u8(aSpec.rgb[c]);
        }
    }
    if (aSpec.maskChannel) {
        w.u16(0);   // compression id: raw
        // 2x2, row major: the LEFT column is hidden (0), the right is visible (255)
        w.u8(0);
        w.u8(255);
        w.u8(0);
        w.u8(255);
    }
    return w.data();
}

// A minimal, complete PSD: 8-bit RGB, two pixels square, the given layers and a
// merged image data section. Exactly the shape img::PSDReader accepts.
std::string buildPsd(const std::vector<LayerSpec>& aLayers)
{
    // the reader walks every layer record first and only then the channel image
    // data, so the two groups are written as two blocks.
    ByteWriter layers;
    layers.u16((uint16)aLayers.size());
    for (const LayerSpec& spec : aLayers) {
        layers.raw(layerRecord(spec));
    }
    for (const LayerSpec& spec : aLayers) {
        layers.raw(layerChannelData(spec));
    }

    ByteWriter layersInfo;
    layersInfo.u32((uint32)layers.data().size());
    layersInfo.append(layers);

    ByteWriter layerAndMask;
    layerAndMask.append(layersInfo);
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
    file.u16(8); // depth (2 bytes: the reader validates it against 1/8/16/32)
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

// Parses aPsd and builds the resource tree, leaving BOTH behind when it returns:
// the img::PSDFormat is local to this call, which is what the live-link path
// does, and what the loader used to dangle from.
img::ResourceNode* createTreeFromPsd(const std::string& aPsd)
{
    std::stringstream ss(aPsd, std::ios::in | std::ios::binary);
    img::PSDReader reader(ss);
    if (reader.resultCode() != img::PSDReader::ResultCode_Success) {
        std::printf("FAIL %d: psd parse: %s\n", __LINE__, reader.resultMessage().c_str());
        return nullptr;
    }
    return img::Util::createResourceNodes(*reader.format(), false);
}

img::ResourceNode* findNodeById(img::ResourceNode* aRoot, const QString& aId)
{
    if (aRoot->data().identityId() == aId) {
        return aRoot;
    }
    for (auto child : aRoot->children()) {
        if (img::ResourceNode* found = findNodeById(child, aId)) {
            return found;
        }
    }
    return nullptr;
}

// The pixel (1, 1) of the loaded texture, which is the layer's (0, 0) moved to
// the middle of the 1-pixel bilinear border the texture decode adds.
const img::ColorRGBA* corePixel(const img::ResourceData& aData)
{
    return aData.image().rawPixel<img::ColorRGBA>(1, 1);
}

// -----------------------------------------------------------------------------
// The stable id is parsed and carried into the node, and it survives a source
// rename that makes two layers collidingly named.
// -----------------------------------------------------------------------------
int testLayerIdentityIsParsed()
{
    const std::string psd = buildPsd({{"dup", "101", "A-uni", true, {10, 20, 30}},
                                       {"dup", "202", "B-uni", false, {200, 100, 50}}});

    std::stringstream ss(psd, std::ios::in | std::ios::binary);
    img::PSDReader reader(ss);
    if (reader.resultCode() != img::PSDReader::ResultCode_Success) {
        std::printf("FAIL %d: psd parse: %s\n", __LINE__, reader.resultMessage().c_str());
        return 1;
    }

    const auto& layers = reader.format()->layerAndMaskInfo().layers;
    CHECK(layers.size() == 2u);

    auto itr = layers.begin();
    CHECK((*itr)->identity == "101");
    CHECK((*itr)->name == "dup");
    CHECK((*itr)->unicodeName == "A-uni");
    ++itr;
    CHECK((*itr)->identity == "202");
    CHECK((*itr)->unicodeName == "B-uni");

    img::ResourceNode* root = img::Util::createResourceNodes(*reader.format(), false);
    CHECK(root != nullptr);
    CHECK(root->children().size() == 2u);

    img::ResourceNode* layer101 = findNodeById(root, "101");
    img::ResourceNode* layer202 = findNodeById(root, "202");
    CHECK(layer101 != nullptr);
    CHECK(layer202 != nullptr);
    CHECK(layer101 != layer202);
    CHECK(layer101->data().isLayer());
    // the display name is untouched: both still read "dup"
    CHECK(layer101->data().identifier() == "dup");
    CHECK(layer202->data().identifier() == "dup");
    // the identity is the stable id, and the two colliding names resolve apart
    CHECK(layer101->data().identityId() == "101");
    CHECK(layer202->data().identityId() == "202");

    delete root;
    return 0;
}

// -----------------------------------------------------------------------------
// A file with no "lyid" has no stable id, so the identity falls back to the
// name -- and to the real ("luni") name where the padded Pascal field is stale.
// -----------------------------------------------------------------------------
int testIdentityFallsBackToName()
{
    const std::string psd = buildPsd({{"stale", "", "real-unicode", true, {10, 20, 30}}});

    img::ResourceNode* root = createTreeFromPsd(psd);
    CHECK(root != nullptr);
    CHECK(root->children().size() == 1u);

    img::ResourceNode* layer = root->children().front();
    CHECK(layer->data().identifier() == "stale");
    CHECK(layer->data().identityId() == "real-unicode");

    delete root;
    return 0;
}

// -----------------------------------------------------------------------------
// The lazy loader owns what it reads, so it is still correct after the
// img::PSDFormat it was built from is gone, on the node itself and on a copy,
// and it runs once only.
// -----------------------------------------------------------------------------
int testSurvivesSourceDestruction()
{
    LayerSpec spec;
    spec.name = "dup";
    spec.identity = "101";
    spec.rgb[0] = 10;
    spec.rgb[1] = 20;
    spec.rgb[2] = 30;
    spec.unicodeNameCountsNull = true;

    img::ResourceNode* layer = nullptr;
    img::ResourceNode* root = nullptr;
    {
        const std::string psd = buildPsd({spec});
        // the tree, the reader and its format all die with this block
        root = createTreeFromPsd(psd);
        CHECK(root != nullptr);
        CHECK(root->children().size() == 1);
        layer = root->children().front();
    }
    // Nothing of the source file survives: the loader's own bytes do.

    // a copy of the data, which is what the relink path installs for a new node
    img::ResourceData copied(layer->data().identifier(), layer->data().identityId(), layer);
    CHECK(!copied.loadImage()); // a data of its own carries no loader
    copied.copyFrom(layer->data());
    CHECK(copied.loadImage());
    CHECK(copied.hasImage());
    const img::ColorRGBA* pixel = corePixel(copied);
    CHECK(pixel != nullptr);
    CHECK(pixel->r() == 10);
    CHECK(pixel->g() == 20);
    CHECK(pixel->b() == 30);
    CHECK(pixel->a() == 255);

    // one-shot: the closure must not be invoked a second time, and the pixels it
    // already produced must stand
    CHECK(!copied.loadImage());
    CHECK(corePixel(copied)->r() == 10);

    // the node's own still lives on next
    CHECK(layer->data().loadImage());
    CHECK(corePixel(layer->data())->b() == 30);
    CHECK(!layer->data().loadImage());

    delete root;
    return 0;
}

// -----------------------------------------------------------------------------
// Key resolution: the stable id picks the node the key names out of trees that
// carry same-named layers, and a name-only key is refused instead of being bound
// to whichever tree came last.
// -----------------------------------------------------------------------------
int testJsonResolutionIsUnambiguous()
{
    NullAnimator animator;
    core::Project project("psd_layer_identity.anie", animator, nullptr);

    const std::string psdA = buildPsd({{"dup", "101", "", true, {10, 20, 30}}});
    const std::string psdB = buildPsd({{"dup", "202", "", true, {200, 100, 50}}});
    img::ResourceNode* treeA = createTreeFromPsd(psdA);
    img::ResourceNode* treeB = createTreeFromPsd(psdB);
    CHECK(treeA != nullptr);
    CHECK(treeB != nullptr);
    project.resourceHolder().pushImageTree(*treeA, "assets/a.psd");
    project.resourceHolder().pushImageTree(*treeB, "assets/b.psd");

    img::ResourceNode* nodeA = findNodeById(treeA, "101");
    img::ResourceNode* nodeB = findNodeById(treeB, "202");
    CHECK(nodeA != nullptr);
    CHECK(nodeB != nullptr);

    // serialize a key bound to each of the two same-named layers
    core::ImageKey keyA;
    keyA.setImage(nodeA->handle(), img::BlendMode_Normal);
    core::ImageKey keyB;
    keyB.setImage(nodeB->handle(), img::BlendMode_Normal);
    QJsonObject jsonA;
    jsonA["Image"] = keyA.serializeToJson();
    QJsonObject jsonB;
    jsonB["Image"] = keyB.serializeToJson();
    CHECK(jsonA["Image"].toObject()["Identifier"].toString() == "dup");
    CHECK(jsonA["Image"].toObject()["LayerId"].toString() == "101");
    CHECK(jsonB["Image"].toObject()["LayerId"].toString() == "202");

    // the id resolves to the node it names, not to the last tree that happens to
    // hold a layer of that name
    core::ImageKey pastedA;
    CHECK(pastedA.deserializeFromJson(jsonA, project.pointee()));
    CHECK(pastedA.data().resource()->serialAddress() == nodeA);
    core::ImageKey pastedB;
    CHECK(pastedB.deserializeFromJson(jsonB, project.pointee()));
    CHECK(pastedB.data().resource()->serialAddress() == nodeB);

    // an old file with no id at all: the name is ambiguous across the two trees,
    // and that must be refused rather than resolved by last match
    QJsonObject oldJson = jsonA;
    QJsonObject image = oldJson["Image"].toObject();
    image.remove("LayerId");
    oldJson["Image"] = image;
    CHECK(!oldJson["Image"].toObject().contains("LayerId"));
    core::ImageKey refused;
    CHECK(!refused.deserializeFromJson(oldJson, project.pointee()));

    // control: with exactly one layer of that name, an id-less key still resolves
    {
        core::Project single("psd_layer_identity_single.anie", animator, nullptr);
        img::ResourceNode* treeS = createTreeFromPsd(psdA);
        CHECK(treeS != nullptr);
        single.resourceHolder().pushImageTree(*treeS, "assets/a.psd");
        img::ResourceNode* nodeS = findNodeById(treeS, "101");
        CHECK(nodeS != nullptr);
        core::ImageKey resolved;
        CHECK(resolved.deserializeFromJson(oldJson, single.pointee()));
        CHECK(resolved.data().resource()->serialAddress() == nodeS);
    }

    return 0;
}

// -----------------------------------------------------------------------------
// The importer is file-based (the resource tree keeps the path), so crafted bytes
// go through a real file with the .psd suffix load() dispatches on.
// -----------------------------------------------------------------------------
bool writePsdFile(const std::string& aPsd, const QString& aPath)
{
    QFile file(aPath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    return file.write(aPsd.data(), (qint64)aPsd.size()) == (qint64)aPsd.size();
}

// -----------------------------------------------------------------------------
// A fill layer is not a broken file. It stores its appearance in a "SoCo" block
// and carries no RGB planes, which used to make makeInterleavedImage return an
// empty block and the whole import fail. Now the project must open, the layer
// must survive as an empty 1x1 placeholder, and warnings() must say which layer
// and what kind.
// -----------------------------------------------------------------------------
int testUnsupportedFillLayerDegradesWithOneWarning()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath("fill.psd");
    CHECK(writePsdFile(buildPsd({{"paint", "", "", true, {0, 0, 0}, "SoCo"}}), path));

    NullAnimator animator;
    core::Project project("psd_unsupported_fill.anie", animator, nullptr);
    NullProgressReporter reporter;
    ctrl::ImageFileLoader loader(gl::DeviceInfo::instance());

    std::printf("assert: a solid-fill layer PSD opens instead of failing\n");
    CHECK(loader.load(path, project, reporter));

    std::printf("assert: warnings() is exactly one line naming 'paint' and its kind 'solid fill'\n");
    CHECK(loader.warnings().size() == 1);
    const QString note = loader.warnings().front();
    CHECK(note.contains("paint"));
    CHECK(note.contains("solid fill"));
    CHECK(note.contains("imported empty"));
    CHECK(!note.contains("flattened"));

    std::printf("assert: the layer exists as a 1x1 fully transparent placeholder\n");
    const core::ObjectNode* top = project.objectTree().topNode();
    CHECK(top != nullptr);
    CHECK(top->children().size() == 1);
    const core::ObjectNode* layerNode = top->children().front();
    CHECK(layerNode != nullptr);
    CHECK(layerNode->name() == "paint");
    CHECK(layerNode->initialRect() == QRect(QPoint(0, 0), QSize(1, 1)));

    const img::ResourceNode* resRoot = project.resourceHolder().imageTree(0).topNode;
    CHECK(resRoot != nullptr);
    CHECK(resRoot->children().size() == 1);
    const img::ResourceNode* resNode = resRoot->children().front();
    CHECK(resNode->data().identifier() == "paint");
    CHECK(resNode->data().hasImage());
    CHECK(resNode->data().image().pixelSize() == QSize(1, 1));
    const img::ColorRGBA* pixel = resNode->data().image().rawPixel<img::ColorRGBA>(0, 0);
    CHECK(pixel != nullptr);
    CHECK(pixel->a() == 0);

    return 0;
}

// -----------------------------------------------------------------------------
// A LAYER MASK IS IMPORTED AS A MATTE. It used to be parsed, re-written and then
// dropped with one aggregated warning; it is now a core::MaskKey on the node, which
// is what makes an imported file look like the source instead of "the same but
// without the mask". A mask whose PIXELS cannot be decoded still counts into the
// warning, so nothing is dropped silently.
// -----------------------------------------------------------------------------
// A PSD GROUP carries its own mask, and Photoshop draws it in DOCUMENT space: it masks
// the group's composite, which is what a folder's matte does here. The field is
// anchored to the canvas the import establishes, not to the header's size, so a forced
// canvas size cannot misplace it.
int testGroupMaskBecomesAFolderMatte()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath("groupmask.psd");
    CHECK(writePsdFile(
        // PSD stores layers BOTTOM-TO-TOP and the importer walks them in reverse: the
        // closing divider is written first, then the child, then the opening one - a
        // group written in reading order pops an empty stack.
        buildPsd({
            {"</Layer group>", "", "", true, {0, 0, 0}, nullptr, false, false, 3},
            {"child", "", "", true, {200, 210, 220}},
            {"<Layer group>", "", "", true, {0, 0, 0}, nullptr, true, true, 1},
        }),
        path
    ));

    NullAnimator animator;
    core::Project project("psd_groupmask.anie", animator, nullptr);
    NullProgressReporter reporter;
    ctrl::ImageFileLoader loader(gl::DeviceInfo::instance());
    CHECK(loader.load(path, project, reporter));

    std::printf("assert: a GROUP's mask becomes a matte on the folder\n");
    core::MaskKey* maskKey = nullptr;
    for (core::ObjectNode* node : project.objectTree().topNode()->children()) {
        if (!node->timeLine()) continue;
        if (auto* key = static_cast<core::MaskKey*>(node->timeLine()->defaultKey(core::TimeKeyType_Mask)))
            maskKey = key;
    }
    CHECK(maskKey != nullptr);
    if (maskKey) {
        const core::MaskKey::Data& data = maskKey->data();
        CHECK(data.isValid());
        CHECK(data.isPainted());
        // ANCHORED TO THE CANVAS, which is the folder's content space
        const QRectF canvas(QPointF(0.0, 0.0), QSizeF(project.attribute().imageSize()));
        CHECK(data.contentRect() == canvas);
        const uchar* row = data.field().constScanLine(0);
        std::printf(
            "MEASURED group matte: %dx%d over %dx%d, texels %d %d\n", data.texelSize().width(),
            data.texelSize().height(), int(canvas.width()), int(canvas.height()), row[0], row[1]
        );
        CHECK(row[0] == 0);
        CHECK(row[1] == 255);
    }

    std::printf("assert: and nothing is reported as an ignored mask\n");
    for (const QString& warning : loader.warnings()) {
        CHECK(!warning.contains(QStringLiteral("masks are not supported")));
    }
    return 0;
}

int testLayerMaskIsReportedOnce()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    const QString path = dir.filePath("masked.psd");
    CHECK(writePsdFile(
        buildPsd({
            {"masked", "", "", true, {10, 20, 30}, nullptr, true, true},
            {"plain", "", "", true, {40, 50, 60}},
        }),
        path
    ));

    NullAnimator animator;
    core::Project project("psd_masked.anie", animator, nullptr);
    NullProgressReporter reporter;
    ctrl::ImageFileLoader loader(gl::DeviceInfo::instance());

    std::printf("assert: a PSD with a layer mask imports successfully\n");
    CHECK(loader.load(path, project, reporter));

    std::printf("assert: an imported mask is NOT reported as ignored\n");
    for (const QString& warning : loader.warnings()) {
        CHECK(!warning.contains(QStringLiteral("masks are not supported")));
    }

    std::printf("assert: the mask arrives as a MATTE on the layer's node\n");
    {
        core::MaskKey* maskKey = nullptr;
        for (core::ObjectNode* node : project.objectTree().topNode()->children()) {
            if (!node->timeLine()) continue;
            if (auto* key = static_cast<core::MaskKey*>(node->timeLine()->defaultKey(core::TimeKeyType_Mask)))
                maskKey = key;
        }
        CHECK(maskKey != nullptr);
        if (maskKey) {
            const core::MaskKey::Data& data = maskKey->data();
            CHECK(data.isValid());
            CHECK(data.texelSize() == QSize(2, 2));
            CHECK(data.isPainted());
            // the fixture's left column is hidden (0) and its right column visible (255)
            const uchar* row = data.field().constScanLine(0);
            std::printf("MEASURED imported matte: texels %d %d %d %d\n", row[0], row[1], row[0], row[1]);
            CHECK(row[0] == 0);
            CHECK(row[1] == 255);
        }
    }

    std::printf("assert: the masked layer's own pixels still import\n");
    const img::ResourceNode* resRoot = project.resourceHolder().imageTree(0).topNode;
    CHECK(resRoot != nullptr);
    const img::ResourceNode* masked = nullptr;
    for (const img::ResourceNode* child : resRoot->children()) {
        if (child->data().identifier() == "masked") {
            masked = child;
        }
    }
    CHECK(masked != nullptr);
    CHECK(masked->data().hasImage());
    const img::ColorRGBA* pixel = corePixel(masked->data());
    CHECK(pixel != nullptr);
    CHECK(pixel->r() == 10);
    CHECK(pixel->g() == 20);
    CHECK(pixel->b() == 30);

    return 0;
}

int run()
{
    CHECK(testLayerIdentityIsParsed() == 0);
    CHECK(testIdentityFallsBackToName() == 0);
    CHECK(testSurvivesSourceDestruction() == 0);
    CHECK(testJsonResolutionIsUnambiguous() == 0);
    CHECK(testUnsupportedFillLayerDegradesWithOneWarning() == 0);
    CHECK(testLayerMaskIsReportedOnce() == 0);
    CHECK(testGroupMaskBecomesAFolderMatte() == 0);
    std::printf("psd_layer_identity: all checks passed\n");
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // A live GL context (cf. roundtrip / media_exporter_e2e): core::ImageKey's
    // GridMesh binds GL in its constructor, so case 4's real ImageKey objects
    // need a current context. Nothing renders, so a 4x4 widget is enough.
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

    // ImageFileLoader::load() reads the GL texture limit through
    // gl::DeviceInfo::instance(), which asserts when no instance was published.
    // The exporters' harnesses publish a synthetic one for the same reason.
    gl::DeviceInfo deviceInfo;
    deviceInfo.vender = "harness";
    deviceInfo.renderer = "harness";
    deviceInfo.version = "5";
    deviceInfo.maxTextureSize = 16384;
    deviceInfo.maxRenderBufferSize = 16384;
    gl::DeviceInfo::setInstance(&deviceInfo);

    return run();
}
