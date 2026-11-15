// Format 0.22: the authored skin-weight block, its node references, and its
// failure modes.
//
// The block lives INSIDE each ObjNode block and names descendant nodes that are
// serialized later, so the reader must defer those references to the tree's ID
// solver (the pre-0.21 influence cache did the same). This gate covers:
//   * a 0.22 save/reload that keeps entries, an empty Baked record, the mesh
//     signature and the per-node records;
//   * a 0.21 object-tree stream with no weight block (the reader does not
//     consume one, and the 0.22 reader rejects the same stream as missing it);
//   * corrupt counts, a NaN weight and an unresolved node reference, each of
//     which must fail the load instead of silently dropping authored paint.
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <cstdio>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <sstream>
#include <vector>
#include "XCAssert.h"
#include "util/StreamReader.h"
#include "util/StreamWriter.h"
#include "util/IProgressReporter.h"
#include "core/Deserializer.h"
#include "core/FolderNode.h"
#include "core/ObjectTree.h"
#include "core/Project.h"
#include "core/Serializer.h"
#include "core/SkinWeights.h"
#include "ctrl/ProjectLoader.h"
#include "ctrl/ProjectSaver.h"
#include "gl/DeviceInfo.h"
#include "gui/AudioPlaybackWidget.h"

// Project.cpp references this GUI entry point; this gate never loads a .aemus.
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

class NullAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

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

bool saveProject(core::Project& aProject, const QString& aPath) {
    ctrl::ProjectSaver saver;
    const bool ok = saver.save(aPath, aProject);
    if (!ok) {
        std::printf("save failed: %s\n", qPrintable(saver.log()));
    }
    return ok;
}

std::unique_ptr<core::Project> loadProject(
    const QString& aPath, const gl::DeviceInfo& aDevice, NullAnimator& aAnimator, bool* aOk
) {
    auto project = std::make_unique<core::Project>(aPath, aAnimator, nullptr);
    NullReporter reporter;
    ctrl::ProjectLoader loader;
    *aOk = loader.load(aPath, *project, aDevice, reporter);
    if (!*aOk) {
        std::printf("load failed: %s\n", qPrintable(loader.log().join(QStringLiteral(" / "))));
    }
    return project;
}

QByteArray readFile(const QString& aPath) {
    QFile file(aPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

bool writeFile(const QString& aPath, const QByteArray& aBytes) {
    QFile file(aPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return file.write(aBytes) == aBytes.size();
}

int findSignature(const QByteArray& aBytes, const char* aSignature) {
    const QByteArray signature(aSignature, 8);
    const int at = aBytes.indexOf(signature);
    return at;
}

void patchInt(QByteArray& aBytes, int aOffset, int aValue) {
    std::memcpy(aBytes.data() + aOffset, &aValue, sizeof(aValue));
}

void patchFloat(QByteArray& aBytes, int aOffset, float aValue) {
    std::memcpy(aBytes.data() + aOffset, &aValue, sizeof(aValue));
}

/// A 0.21-shaped object tree: two folders, no skin-weight blocks. It mirrors
/// exactly what the 0.22 writer lays out before the new block.
std::string makeOldTreeStream() {
    core::FolderNode top(QStringLiteral("root"));
    auto* child = new core::FolderNode(QStringLiteral("child"));
    top.children().pushBack(child);

    std::stringstream stream;
    util::StreamWriter writer(stream);
    core::Serializer serializer(writer);

    const auto treePos = serializer.beginBlock({'O', 'b', 'j', 'T', 'r', 'e', 'e', '_'});
    serializer.write(1);
    {
        const auto nodePos = serializer.beginBlock({'O', 'b', 'j', 'N', 'o', 'd', 'e', '_'});
        serializer.write(int(core::ObjectType_Folder));
        serializer.write(1);
        serializer.writeID(&top);
        top.serialize(serializer);
        serializer.endBlock(nodePos);
    }
    {
        const auto childPos = serializer.beginBlock({'O', 'b', 'j', 'N', 'o', 'd', 'e', '_'});
        serializer.write(int(core::ObjectType_Folder));
        serializer.write(0);
        serializer.writeID(child);
        child->serialize(serializer);
        serializer.endBlock(childPos);
    }
    serializer.endBlock(treePos);
    return stream.str();
}

bool readTree(const std::string& aBytes, int aMinor, NullReporter& aReporter, core::ObjectTree& aTree) {
    static gl::DeviceInfo device = makeDeviceInfo();
    std::istringstream input(aBytes);
    util::LEStreamReader reader(input);
    core::Deserializer::IDSolverType solver;
    core::Deserializer deserializer(
        reader, solver, aBytes.size(), QVersionNumber(0, aMinor), device, aReporter, 0
    );
    if (!aTree.deserialize(deserializer) || !deserializer.checkStream()) {
        return false;
    }
    return solver.solve();
}

} // namespace

XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    (void)app;

    using core::SkinWeights;

    QTemporaryDir tempDir;
    check(tempDir.isValid(), "a temporary directory is available");
    if (!tempDir.isValid()) {
        return 1;
    }
    const QString path = tempDir.filePath(QStringLiteral("weights.anie"));
    const gl::DeviceInfo device = makeDeviceInfo();
    NullAnimator animator;

    // 1. A 0.22 SAVE/LOAD. The owner record names a descendant, so the reader
    // must resolve it after the whole tree has been read.
    {
        core::Project project(QString(), animator, nullptr);
        auto* root = new core::FolderNode(QStringLiteral("root"));
        auto* child = new core::FolderNode(QStringLiteral("child"));
        root->children().pushBack(child);
        project.objectTree().grabTopNode(root);

        SkinWeights::NodeWeights record(*child, 7, 0xfedcba9876543210ULL);
        SkinWeights::BakedBone bone;
        bone.boneId = 5;
        bone.setWeight(0, 0.25f);
        bone.setWeight(3, 1.0f);
        record.setBakedBone(bone);
        record.setBakedBone(9, {});
        root->skinWeights().setNodeWeights(record);

        check(saveProject(project, path), "the 0.22 project saves");

        bool ok = false;
        auto loaded = loadProject(path, device, animator, &ok);
        check(ok, "the 0.22 project loads");
        if (ok) {
            auto* loadedRoot = loaded->objectTree().topNode();
            check(loadedRoot && loadedRoot->name() == QStringLiteral("root"), "the owner node came back");
            core::ObjectNode* loadedChild = loadedRoot ? loadedRoot->children().front() : nullptr;
            check(loadedChild && loadedChild->name() == QStringLiteral("child"), "the descendant came back");

            const SkinWeights::NodeWeights* restored =
                loadedRoot ? loadedRoot->skinWeights().find(*loadedChild) : nullptr;
            check(restored != nullptr, "the record's node reference resolved after the tree load");
            if (restored) {
                check(restored->vertexCount == 7, "the vertex count round-trips");
                check(restored->meshSignature == 0xfedcba9876543210ULL, "the mesh signature round-trips");
                check(restored->bakedBones.size() == 2, "both baked bones round-trip");
                check(restored->isBaked(5) && restored->isBaked(9), "both ids stay Baked");
                check(
                    restored->bakedFor(5)->weightAt(0) == 0.25f
                        && restored->bakedFor(5)->weightAt(3) == 1.0f,
                    "the entry values round-trip"
                );
                check(restored->bakedFor(9)->entries.isEmpty(), "the empty baked field stayed Baked with no entries");
                check(
                    restored->bakedBones[0].boneId == 5 && restored->bakedBones[1].boneId == 9,
                    "baked bones stay sorted by id"
                );
            }
        }
    }

    // 2. A 0.21 STREAM WITH NO BLOCK. The same bytes are read by both version
    // gates: 0.21 opens (it never looks for the block), 0.22 refuses (the block
    // is authored data and its absence at that version is corrupt).
    {
        const std::string oldBytes = makeOldTreeStream();
        NullReporter reporter;
        core::ObjectTree tree21;
        const bool ok21 = readTree(oldBytes, 21, reporter, tree21);
        check(ok21, "a 0.21 object tree without a weight block loads");
        if (ok21) {
            auto* loadedRoot = tree21.topNode();
            check(loadedRoot && loadedRoot->name() == QStringLiteral("root"), "the 0.21 tree has its top node");
            core::ObjectNode* loadedChild = loadedRoot ? loadedRoot->children().front() : nullptr;
            check(loadedChild && loadedChild->name() == QStringLiteral("child"), "the 0.21 tree has its child");
            check(loadedRoot && loadedRoot->skinWeights().isEmpty(), "the 0.21 tree has no weight records");
        }

        NullReporter reporter22;
        core::ObjectTree tree22;
        const bool ok22 = readTree(oldBytes, 22, reporter22, tree22);
        check(!ok22, "the 0.22 reader rejects an object node without its weight block");
    }

    // 3. CORRUPT BLOCKS. Patch the saved file by the block's own layout:
    // signature(8), length(8), record count(4), node ref(4), vertex count(4),
    // signature words(8), baked count(4), bone id(4), entry count(4), entries.
    {
        const QByteArray good = readFile(path);
        check(!good.isEmpty(), "the saved file can be read back");
        const int block = findSignature(good, "SkinWgt_");
        check(block >= 0, "the saved file carries a SkinWgt_ block");
        const int recordOffset = block + 16;
        int recordCount = 0;
        if (block >= 0) {
            std::memcpy(&recordCount, good.data() + recordOffset, sizeof(recordCount));
        }
        check(recordCount == 1, "the fixture has exactly one weight record");
        const int nodeRefOffset = block + 20;
        const int firstWeightOffset = block + 52;

        const auto expectRefused = [&](const char* aWhat, const std::function<void(QByteArray&)>& aPatch) {
            QByteArray damaged = good;
            aPatch(damaged);
            const QString damagedPath = tempDir.filePath(QStringLiteral("damaged.anie"));
            const bool written = writeFile(damagedPath, damaged);
            bool ok = false;
            if (written) {
                auto loaded = loadProject(damagedPath, device, animator, &ok);
                (void)loaded;
            }
            check(written && !ok, aWhat);
        };

        expectRefused("an overrunning record count fails the load", [&](QByteArray& aBytes) {
            patchInt(aBytes, recordOffset, 0x7fffffff);
        });
        expectRefused("a non-finite weight fails the load", [&](QByteArray& aBytes) {
            patchFloat(aBytes, firstWeightOffset, std::numeric_limits<float>::quiet_NaN());
        });
        expectRefused("an unresolved node reference fails the load", [&](QByteArray& aBytes) {
            patchInt(aBytes, nodeRefOffset, 9999);
        });
    }

    if (failures) {
        std::printf("%d failure(s)\n", failures);
    }
    return failures == 0 ? 0 : 1;
}
