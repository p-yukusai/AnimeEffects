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
// Run: xvfb-run -a ./psd_layer_identity
//
// Core: core::ImageKey's GridMesh binds GL in its constructor, so case 4 needs a
// live GL context, set up in main() the same way roundtrip and media_exporter_e2e
// set theirs up. Nothing is rendered -- only a current context is required.
// (QT_QPA_PLATFORM=offscreen alone is not enough: it provides no GL context.)
#include <QApplication>
#include <QByteArray>
#include <QChar>
#include <QJsonObject>
#include <QString>
#include <QOpenGLWidget>
#include <QOpenGLVersionFunctions>
#include <QSurfaceFormat>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "XC.h"
#include "gl/Global.h"
#include "core/ImageKey.h"
#include "core/Project.h"
#include "core/Animator.h"
#include "img/ColorRGBA.h"
#include "img/BlendMode.h"
#include "img/PSDFormat.h"
#include "img/PSDReader.h"
#include "img/ResourceData.h"
#include "img/ResourceNode.h"
#include "img/Util.h"
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
};

std::string layerRecord(const LayerSpec& aSpec)
{
    ByteWriter w;

    // rect: t, l, b, r -- the layer is one 2x2 image at the origin
    w.u32(0);
    w.u32(0);
    w.u32(2);
    w.u32(2);

    // channels: 0, 1, 2, each holding the 2-byte compression id and a 2x2 plane
    // (the record's length field counts the compression id, so 6)
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

    // layer extra data: layer mask, blending range, padded name, then the
    // additional layer info blocks ("lyid" and "luni" here)
    ByteWriter extra;
    extra.u32(0); // layer mask: length 0
    extra.u32(0); // blending range: length 0
    extra.name(aSpec.name);

    if (!aSpec.identity.isEmpty()) {
        ByteWriter id;
        id.u32((uint32)aSpec.identity.toUInt());
        writeInfo(extra, "lyid", id.data());
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

    w.u32((uint32)extra.data().size());
    return w.data() + extra.data();
}

// the channel image data that follows all layer records: one compressed id and
// one 2x2 plane per channel.
std::string layerChannelData(const LayerSpec& aSpec)
{
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

int run()
{
    CHECK(testLayerIdentityIsParsed() == 0);
    CHECK(testIdentityFallsBackToName() == 0);
    CHECK(testSurvivesSourceDestruction() == 0);
    CHECK(testJsonResolutionIsUnambiguous() == 0);
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

    return run();
}
