// BoneKey stream compatibility gate.
//
// Format 0.21 stopped writing the derived influence cache (cache owner,
// per-node influence maps, binding caches); ProjectLoader rebuilds it after the
// ID solver. A <= 0.20 file still carries that section, so the reader must
// consume it byte-exactly and discard it. A mis-sized skip would desynchronise
// every later block and surface much later as a corrupt project, so each case
// below ends with a sentinel word and asserts the stream is exactly in sync.
//
// This file used to be the regression for a use-after-free in the old
// deserialiser: it captured &mBindingCaches.back() into a deferred IDSolver
// lambda, and a second push_back() reallocated the QList before solve() wrote
// through the pointer (the close-tab "corrupted size vs. prev_size" crash). The
// discard path deliberately stores no binding caches, so that shape cannot be
// constructed here any more; the >= 2-cache, >= 2-binding streams below keep
// the same input footprint under ASan, and the solver run in readBoneKey()
// proves the discard left no unresolved reference behind.
#include <QApplication>
#include <QJsonObject>
#include <QVersionNumber>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include "XCAssert.h"
#include "util/StreamWriter.h"
#include "util/StreamReader.h"
#include "util/IDSolver.h"
#include "core/Deserializer.h"
#include "core/BoneKey.h"
#include "core/ObjectNode.h"
#include "gl/DeviceInfo.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references the GUI AudioPlaybackWidget::deserialize (only to load
// a .aemus sidecar, which this test never has). Stub it so the link succeeds.
bool AudioPlaybackWidget::deserialize(const QJsonObject&, std::vector<audioConfig>*) { return false; }

namespace {
class HarnessAssertHandler: public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class HarnessErrorHandler: public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};
HarnessAssertHandler gAssertHandler;
HarnessErrorHandler gErrorHandler;
} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

class NullReporter: public util::IProgressReporter {
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

// Write one legacy influence-map record: two int32 header words followed by
// 64 B per vertex (4 x GLint indices + 4 x GLfloat weights, two sets each).
static void writeLegacyInfluence(util::StreamWriter& aWriter, int aVertexCount) {
    aWriter.write((sint32)aVertexCount);
    aWriter.write((sint32)32); // max bone count
    for (int i = 0; i < aVertexCount * 16; ++i) {
        aWriter.write((sint32)i);
    }
}

static void writeLegacyCache(util::StreamWriter& aWriter, int aNodeId, int aVertexCount) {
    aWriter.write((sint32)aNodeId);
    for (int i = 0; i < 16; ++i) {
        aWriter.write((float32)(i + 1)); // inner matrix
    }
    aWriter.write((float32)1.0f); // obsolete origin offset
    aWriter.write((float32)2.0f);
    aWriter.write((sint32)0); // frame sign: value
    aWriter.write((sint32)0); // frame sign: milli
    writeLegacyInfluence(aWriter, aVertexCount);
}

// The complete <= 0.20 section: top bone count 0 (no authored geometry), cache
// owner, caches, binding caches, then the sentinel the caller reads back.
static std::string makeLegacyStream(
    int aCacheCount, int aVertexCount, int aBindingCount, int aSentinel
) {
    std::stringstream stream;
    util::StreamWriter writer(stream);

    writer.write((sint32)0); // top bone count
    writer.write((sint32)0); // cache owner reference id
    writer.write((sint32)aCacheCount);
    for (int i = 0; i < aCacheCount; ++i) {
        writeLegacyCache(writer, i + 1, aVertexCount);
    }

    writer.write((sint32)aBindingCount);
    for (int i = 0; i < aBindingCount; ++i) {
        writer.write((sint32)(i + 1)); // node reference id
        writer.write((sint32)(7 + i)); // bone index
        for (int k = 0; k < 16; ++k) {
            writer.write((float32)(k + 20 + i)); // inner matrix
        }
    }

    writer.write((sint32)aSentinel);
    return stream.str();
}

struct ReadResult {
    bool ok = false;
    int sentinel = 0;
};

// Deserialise a standalone BoneKey stream at the given minor version, run the
// deferred solver like ProjectLoader does, then read the trailing sentinel.
static ReadResult readBoneKey(const std::string& aBytes, int aMinor) {
    ReadResult result;
    static gl::DeviceInfo device = makeDeviceInfo();

    std::istringstream input(aBytes);
    util::LEStreamReader reader(input);
    core::Deserializer::IDSolverType solver;
    NullReporter reporter;
    core::Deserializer deserializer(
        reader, solver, aBytes.size(), QVersionNumber(0, aMinor), device, reporter, 0
    );

    core::BoneKey key;
    if (!key.deserialize(deserializer)) {
        return result;
    }
    if (!solver.solve()) {
        return result;
    }
    if (key.cacheOwner() != nullptr || !key.caches().isEmpty() || !key.bindingCaches().isEmpty()) {
        return result;
    }

    result.sentinel = deserializer.getRead<int>();
    result.ok = deserializer.checkStream();
    return result;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    (void)argv;

    const int kSentinel = 0x5EEDBEEF;

    // 0.20: the legacy section is present (two maps, two binding caches, three
    // vertices each) and must be skipped exactly, without retaining anything.
    {
        const std::string bytes = makeLegacyStream(2, 3, 2, kSentinel);
        const ReadResult result = readBoneKey(bytes, 20);
        if (!result.ok || result.sentinel != kSentinel) {
            std::fprintf(stderr, "0.20 legacy discard desynchronised the stream\n");
            return 1;
        }
    }

    // 0.20 with empty maps: the per-vertex skip is zero bytes, but the record
    // header and every fixed field must still be consumed.
    {
        const std::string bytes = makeLegacyStream(2, 0, 2, kSentinel);
        const ReadResult result = readBoneKey(bytes, 20);
        if (!result.ok || result.sentinel != kSentinel) {
            std::fprintf(stderr, "0.20 empty influence record desynchronised the stream\n");
            return 1;
        }
    }

    // 0.21: there is no derived section at all; the BoneKey block is bones only.
    {
        std::stringstream stream;
        util::StreamWriter writer(stream);
        writer.write((sint32)0); // top bone count
        writer.write((sint32)kSentinel);

        const ReadResult result = readBoneKey(stream.str(), 21);
        if (!result.ok || result.sentinel != kSentinel) {
            std::fprintf(stderr, "0.21 BoneKey expected no cache section\n");
            return 1;
        }
    }

    // A damaged count fails where it is read instead of desynchronising later.
    {
        std::stringstream stream;
        util::StreamWriter writer(stream);
        writer.write((sint32)0);  // top bone count
        writer.write((sint32)0);  // cache owner
        writer.write((sint32)-1); // cache count
        const ReadResult result = readBoneKey(stream.str(), 20);
        if (result.ok) {
            std::fprintf(stderr, "negative cache count was accepted\n");
            return 1;
        }
    }

    // A huge vertex count is rejected by skipBytes against the remaining file
    // before it can allocate or seek wildly.
    {
        std::stringstream stream;
        util::StreamWriter writer(stream);
        writer.write((sint32)0); // top bone count
        writer.write((sint32)0); // cache owner
        writer.write((sint32)1); // cache count
        writer.write((sint32)1); // node id
        for (int i = 0; i < 16; ++i) {
            writer.write((float32)i); // inner matrix
        }
        writer.write((float32)0.0f); // obsolete origin offset
        writer.write((float32)0.0f);
        writer.write((sint32)0); // frame sign: value
        writer.write((sint32)0); // frame sign: milli
        writer.write((sint32)std::numeric_limits<int>::max()); // vertex count
        writer.write((sint32)32);                               // max bone count
        const ReadResult result = readBoneKey(stream.str(), 20);
        if (result.ok) {
            std::fprintf(stderr, "huge influence vertex count was accepted\n");
            return 1;
        }
    }

    std::fprintf(stderr, "PASS: BoneKey legacy cache section discarded in sync; 0.21 has none\n");
    return 0;
}
