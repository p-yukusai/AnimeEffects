// Stable bone ids (format 0.22).
//
// The influence map's uint8 bone index is a GPU attribute and follows tree
// order, so authored weights cannot address it. A Bone2 carries a project-local
// id instead: fresh on construction, preserved by the copy constructor and by a
// shadow, restored from a 0.22 file, and absent from 0.21 (which keeps the
// fresh id so its first save gains stable identity).
//
// This gate pins the id lifecycle and the version gate on the stream. It uses a
// standalone Bone2 block so the 0.21 case can drop exactly the trailing id; a
// BoneKey stream embeds one id per bone and cannot be trimmed that simply.
#include <QApplication>
#include <QJsonObject>
#include <QVersionNumber>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "XCAssert.h"
#include "util/StreamReader.h"
#include "util/StreamWriter.h"
#include "util/IProgressReporter.h"
#include "core/Bone2.h"
#include "core/BoneKey.h"
#include "core/Deserializer.h"
#include "core/Serializer.h"
#include "gl/DeviceInfo.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references the GUI entry point; this gate never opens a project.
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
int failures = 0;

void check(bool aCondition, const char* aWhat) {
    if (aCondition) {
        std::printf("PASS  %s\n", aWhat);
    } else {
        std::printf("FAIL  %s\n", aWhat);
        ++failures;
    }
}

class NullReporter: public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

gl::DeviceInfo makeDeviceInfo() {
    gl::DeviceInfo dev;
    dev.vender = "test";
    dev.renderer = "test";
    dev.version = "5";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    return dev;
}

std::string serializeBone(const core::Bone2& aBone) {
    std::stringstream stream;
    util::StreamWriter writer(stream);
    core::Serializer serializer(writer);
    if (!aBone.serialize(serializer)) {
        return std::string();
    }
    return stream.str();
}

struct ReadBone {
    bool ok = false;
    int id = 0;
};

ReadBone readBone(const std::string& aBytes, int aMinor) {
    ReadBone result;
    static gl::DeviceInfo device = makeDeviceInfo();
    std::istringstream input(aBytes);
    util::LEStreamReader reader(input);
    core::Deserializer::IDSolverType solver;
    NullReporter reporter;
    core::Deserializer deserializer(
        reader, solver, aBytes.size(), QVersionNumber(0, aMinor), device, reporter, 0
    );
    core::Bone2 bone;
    if (!bone.deserialize(deserializer) || !deserializer.checkStream() || !solver.solve()) {
        return result;
    }
    result.ok = true;
    result.id = bone.boneId();
    return result;
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    (void)app;

    using core::Bone2;

    // Fresh ids: nonzero and distinct.
    Bone2 first;
    Bone2 second;
    check(first.boneId() > 0, "a fresh bone has a positive id");
    check(first.boneId() != second.boneId(), "fresh bones get distinct ids");

    // reserveBoneId moves the counter forward, saturates at the stored value,
    // and ignores values that are not ids.
    Bone2::reserveBoneId(100000);
    Bone2 reserved;
    check(reserved.boneId() > 100000, "reserveBoneId moves the counter past the stored id");
    Bone2::reserveBoneId(0);
    Bone2 afterZeroReserve;
    check(afterZeroReserve.boneId() > reserved.boneId(), "a non-positive reserve does not reset the counter");

    // A copy (a cloned key) addresses the same bone; a shadow is the same bone
    // at another frame and reports its origin's id.
    Bone2 copy(first);
    check(copy.boneId() == first.boneId(), "the copy constructor preserves the id");
    {
        auto* shadow = first.createShadow();
        check(shadow && shadow->boneId() == first.boneId(), "a shadow reports its origin's id");
        delete shadow;
    }

    // Give the serialized bone geometry so the id is not the only field the
    // stream carries; the values are irrelevant to the gate.
    Bone2 source;
    source.setWorldPos(QVector2D(12.0f, -3.0f), nullptr);
    source.setRange(0, QVector2D(0.25f, 0.5f));
    source.setRange(1, QVector2D(0.5f, 0.25f));
    core::BoneShape shape;
    shape.setSegment(util::Segment2D(QVector2D(0.0f, 0.0f), QVector2D(0.0f, 4.0f)));
    shape.setRadius(QVector2D(1.0f, 1.0f), QVector2D(0.5f, 0.5f));
    QPolygonF polygon;
    polygon << QPointF(-1, -1) << QPointF(1, -1) << QPointF(1, 1) << QPointF(-1, 1);
    shape.setPolygon(polygon);
    source.setShape(shape);
    source.updateWorldTransform();
    const int storedId = source.boneId();

    const std::string bytes = serializeBone(source);
    check(!bytes.empty(), "the bone serializes");

    // 0.22 reads the stored id.
    const ReadBone at22 = readBone(bytes, 22);
    check(at22.ok && at22.id == storedId, "a 0.22 bone reads its stored id");

    // 0.21's Bone2 block ends after mRotate. Dropping the trailing id makes a
    // stream the old layout would have written; the loader must keep the fresh
    // constructor id instead of consuming four bytes that are not there.
    std::string oldBytes = bytes;
    check(oldBytes.size() > 4, "the serialized bone has room for an id");
    oldBytes.resize(oldBytes.size() - 4);
    const ReadBone at21 = readBone(oldBytes, 21);
    check(at21.ok, "a 0.21 bone block loads without an id");
    check(at21.id > 0, "a 0.21 bone keeps a fresh positive id");
    check(at21.id != storedId, "a 0.21 load does not read the id bytes");

    if (failures) {
        std::printf("%d failure(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
