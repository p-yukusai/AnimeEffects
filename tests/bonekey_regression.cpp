// Regression test for a use-after-free in BoneKey::deserialize.
//
// Deserializing a BoneKey resolves per-binding-cache node references lazily via
// util::IDSolver::solve(), which runs AFTER the whole deserialize returns. The
// original code captured `&mBindingCaches.back()` (a pointer into the QList's
// inline buffer) into the deferred lambda. With >= 2 binding caches the next
// push_back() reallocates the buffer, so when solve() later writes
// `cache->node = node`, it writes through a dangling pointer — heap corruption
// that glibc only detects on free (the close-tab crash: "corrupted size vs.
// prev_size", "double free"). This test emits a minimal BoneKey stream with 2
// binding caches, deserializes it, runs the solver, and asserts both bindings
// resolve to the intended nodes. Run under ASan (-DAE_ENABLE_ASAN=ON); the
// pre-fix code aborts here with a heap-use-after-free.
#include <QApplication>
#include <sstream>
#include <QMatrix4x4>
#include <QSettings>
#include <QJsonObject>
#include <cstdio>
#include <vector>
#include "XCAssert.h"
#include "util/StreamWriter.h"
#include "util/StreamReader.h"
#include "util/IDSolver.h"
#include "core/Deserializer.h"
#include "core/Serializer.h"
#include "core/BoneKey.h"
#include "core/ObjectNode.h"
#include "core/ObjectType.h"
#include "gl/DeviceInfo.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to load
// a .aemus sidecar, which this test never has). Stub it so the link succeeds.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

// Minimal ObjectNode subclass so the test holds real, live node pointers
// (ObjectNode is abstract). BindingCache.node stores ObjectNode*; the test only
// needs valid, distinct addresses.
namespace core {
class TestNode : public ObjectNode {
public:
    ObjectType type() const override { return ObjectType_Layer; }
    void setName(const QString&) override {}
    const QString& name() const override { static QString s; return s; }
    float initialDepth() const override { return 0; }
    void setVisibility(bool) override {}
    bool isVisible() const override { return true; }
    void setSlimDown(bool) override {}
    bool isSlimmedDown() const override { return false; }
    bool canHoldChild() const override { return false; }
    void setInitialRect(const QRect&) override {}
    QRect initialRect() const override { return QRect(); }
    bool serialize(Serializer&) const override { return true; }
    bool deserialize(Deserializer&) override { return true; }
};
} // namespace core

namespace {
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
} // namespace
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

class NullReporter : public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

static gl::DeviceInfo makeDeviceInfo() {
    gl::DeviceInfo dev;
    dev.vender = "test";
    dev.renderer = "test";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    return dev;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    (void)argv;
    gl::DeviceInfo dev = makeDeviceInfo();
    gl::DeviceInfo::setInstance(&dev);

    core::TestNode nodeA;
    core::TestNode nodeB;

    // Emit the BoneKey stream: topBoneCount=0, cacheOwner id=-1, cacheCount=0,
    // bindingCacheCount=2, then per binding cache: node id, boneIndex, innerMtx.
    std::stringstream stream;
    util::StreamWriter writer(stream);

    // top bone count
    writer.write((sint32)0);
    // cache owner reference id (>= 0 so orderIDData registers the solver)
    writer.write((sint32)0);
    // cache count
    writer.write((sint32)0);
    // binding cache count
    writer.write((sint32)2);
    // binding cache 0
    writer.write((sint32)1); // node id -> nodeA
    writer.write((sint32)7); // boneIndex
    for (int i = 0; i < 16; ++i) writer.write((float32)(i + 1)); // innerMtx
    // binding cache 1
    writer.write((sint32)2); // node id -> nodeB
    writer.write((sint32)9); // boneIndex
    for (int i = 0; i < 16; ++i) writer.write((float32)(i + 20)); // innerMtx

    std::istringstream input(stream.str());
    util::LEStreamReader reader(input);
    core::Deserializer::IDSolverType solver;
    NullReporter reporter;
    core::Deserializer deser(reader, solver, (size_t)stream.str().size(), QVersionNumber(0, 11), dev, reporter, 0);

    // Seed solver data: cacheOwner id 0 -> nodeA, node ids 1 and 2 ->
    // nodeA/nodeB (mirrors bindIDData).
    solver.pushData(0, &nodeA);
    solver.pushData(1, &nodeA);
    solver.pushData(2, &nodeB);

    core::BoneKey key;
    if (!key.deserialize(deser)) {
        std::fprintf(stderr, "deserialize key failed\n");
        return 1;
    }
    // Resolve deferred references.
    if (!solver.solve()) {
        std::fprintf(stderr, "solve failed\n");
        return 1;
    }

    const auto& bcs = key.bindingCaches();
    if (bcs.size() != 2) {
        std::fprintf(stderr, "binding caches count = %d (expected 2)\n", (int)bcs.size());
        return 1;
    }
    if (bcs[0].node != &nodeA || bcs[0].boneIndex != 7) {
        std::fprintf(stderr, "binding cache 0 wrong: node=%p idx=%d\n", (void*)bcs[0].node, (int)bcs[0].boneIndex);
        return 1;
    }
    if (bcs[1].node != &nodeB || bcs[1].boneIndex != 9) {
        std::fprintf(stderr, "binding cache 1 wrong: node=%p idx=%d\n", (void*)bcs[1].node, (int)bcs[1].boneIndex);
        return 1;
    }
    std::fprintf(stderr, "PASS: BoneKey binding caches resolved after QList reallocation\n");
    return 0;
}
