#include <fstream>
#include <QCoreApplication>
#include "qdir.h"
#include "util/IDSolver.h"
#include "ctrl/ProjectLoader.h"
#include "core/BoneKey.h"
#include "core/Deserializer.h"
#include "core/FormatVersion.h"
#include "core/PosePalette.h"
#include "core/TimeLine.h"
#include <qstandardpaths.h>

namespace ctrl {

namespace {

// The skeleton cap is a runtime limit, not a file limit: every bone is
// authored and saved, but only the first PosePalette::kMaxCount can deform.
// Say so on load, where the user would otherwise get a silently partial
// skeleton (the same shape as the PSD importer's degraded-layer warnings).
QString oneOverCapBoneWarning(const core::ObjectNode& aNode, const core::BoneKey& aKey, int aBoneCount) {
    // The explicit context names the class (there is no Q_OBJECT here). It stays on
    // the CALL's line so scripts/i18n/check_tr_contexts.py can see that this context
    // is asked for by name; wrapping it onto its own line read as "no call site" and
    // failed the suite's catalog gate.
    return QCoreApplication::translate("ctrl::ProjectLoader",
                                       "The skeleton on \"%1\" (frame %2) has %3 bones; only the first %4 can deform a mesh.")
        .arg(aNode.name())
        .arg(aKey.frame())
        .arg(aBoneCount)
        .arg(int(core::PosePalette::kMaxCount));
}

// The influence maps are derived state: 0.21 does not write them, and a <= 0.20
// section is discarded on read. Every loader path ends here, after the ID
// solver, so the GUI, render_probe and the round-trip harness all start from the
// same rebuilt caches. resetCaches schedules the actual per-vertex work on the
// project's paralleler; the first accessor() waits for it.
bool rebuildBoneInfluenceCaches(
    core::Project& aProject, util::IProgressReporter& aReporter, QStringList& aLog, QStringList& aWarnings
) {
    core::ObjectNode* top = aProject.objectTree().topNode();
    if (!top)
        return true;

    int total = 0;
    for (core::ObjectNode::Iterator itr(top); itr.hasNext();) {
        core::ObjectNode* node = itr.next();
        if (node && node->timeLine())
            total += node->timeLine()->map(core::TimeKeyType_Bone).size();
    }

    if (total <= 0)
        return true; // no bone keys: leave the load report unchanged

    aReporter.setSection("Rebuilding bone influence caches...");
    aReporter.setMaximum(qMax(1, total));
    aReporter.setProgress(0);

    int done = 0;
    for (core::ObjectNode::Iterator itr(top); itr.hasNext();) {
        core::ObjectNode* node = itr.next();
        if (!node || !node->timeLine())
            continue;

        const core::TimeLine::MapType& boneKeys = node->timeLine()->map(core::TimeKeyType_Bone);
        for (auto itrKey = boneKeys.begin(); itrKey != boneKeys.end(); ++itrKey) {
            if (aReporter.wasCanceled()) {
                aLog.push_back("Canceled while rebuilding bone influence caches.");
                return false;
            }

            auto* boneKey = dynamic_cast<core::BoneKey*>(itrKey.value());
            if (boneKey) {
                int boneCount = 0;
                for (const core::Bone2* topBone : boneKey->data().topBones()) {
                    for (core::Bone2::ConstIterator boneItr(topBone); boneItr.hasNext();) {
                        boneItr.next();
                        ++boneCount;
                    }
                }
                if (boneCount > int(core::PosePalette::kMaxCount)) {
                    aWarnings.push_back(oneOverCapBoneWarning(*node, *boneKey, boneCount));
                }

                boneKey->resetCaches(aProject, *node);
            }
            aReporter.setProgress(++done);
        }
    }

    aReporter.setProgress(total);
    return true;
}

} // namespace

ProjectLoader::ProjectLoader(): mLog(), mWarnings(), mVersion() {}

bool ProjectLoader::load(
    const QString& aPath,
    core::Project& aProject,
    const gl::DeviceInfo& aGLDeviceInfo,
    util::IProgressReporter& aReporter
) {
    XC_DEBUG_REPORT() << "project path =" << aPath;
    mWarnings.clear();
    std::ifstream file(aPath.toLocal8Bit(), /*std::ios::in |*/ std::ios::binary);

    if (file.fail()) {
        QSettings settings;
        QStringList recentfiles = settings.value("projectloader/recents").toStringList();
        qInfo() << aPath;
        if (recentfiles.contains(aPath)) {
            auto unavailableIndex = recentfiles.indexOf(aPath);
            recentfiles.removeAt(unavailableIndex);
            settings.setValue("projectloader/recents", recentfiles);
            qInfo() << recentfiles;
            mLog.push_back(
                "Project removed, renamed or otherwise unavailable, please open it manually.\n"
                "This path has been removed from your recents."
            );
            settings.sync();
        }

        mLog.push_back("Can not open the project file.");
        return false;
    }

    // max file size
    const size_t maxFileSize = (size_t)file.seekg(0, std::ios::end).tellg();
    file.seekg(0, std::ios::beg);

    // setup reporter
    int rShiftCount = 0;
    {
        size_t maxSize = maxFileSize;
        while (maxSize > (size_t)std::numeric_limits<int>::max()) {
            maxSize >>= 1;
            ++rShiftCount;
        }
        aReporter.setSection("Loading the Project File...");
        aReporter.setMaximum((int)maxSize);
        aReporter.setProgress(0);
    }

    // setup reader
    util::LEStreamReader in(file);

    if (!readHeader(in)) {
        mLog.push_back("Failed to read header.");
        return false;
    }

    if (!readGlobalBlock(in, aProject)) {
        mLog.push_back("Failed to read global block.");
        return false;
    }

    core::Deserializer::IDSolverType idSolver;
    core::Deserializer deserializer(in, idSolver, maxFileSize, mVersion, aGLDeviceInfo, aReporter, rShiftCount);
    deserializer.reportCurrent();

    // resources block
    if (!aProject.resourceHolder().deserialize(deserializer)) {
        QString log = "Failed to read resources block. : ";
        for (auto scope : deserializer.logScopes()) {
            log += scope + "/";
        }
        mLog.push_back(log);

        for (auto deslog : deserializer.log()) {
            mLog.push_back(deslog);
        }
        return false;
    }

    deserializer.reportCurrent();

    // object tree block
    if (!aProject.objectTree().deserialize(deserializer)) {
        QString log = "Failed to read object tree block. : ";
        for (auto scope : deserializer.logScopes()) {
            log += scope + "/";
        }
        mLog.push_back(log);

        for (auto deslog : deserializer.log()) {
            mLog.push_back(deslog);
        }
        return false;
    }

    deserializer.reportCurrent();

    // solve id references
    if (!idSolver.solve()) {
        mLog.push_back("Failed to solve id references.");
        return false;
    }

    // Rebuild the derived skinning caches now that node references resolve.
    if (!rebuildBoneInfluenceCaches(aProject, aReporter, mLog, mWarnings)) {
        return false;
    }

    mLog.push_back("Success.");
    return true;
}

bool ProjectLoader::readHeader(util::LEStreamReader& aIn) {
    // signature
    const std::string signature = aIn.readString(6);
    if (signature != "ANIMFX")
        return false;

    // endian
    uint8 endian[2];
    aIn.readBuf(endian, 2);
    if (endian[0] != 0xff || endian[1] != 0x00)
        return false;

    // major version
    const int majorVersion = aIn.readUInt32();
    // minor version
    const int minorVersion = aIn.readUInt32();

    if (majorVersion < AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION ||
        (majorVersion == AE_PROJECT_FORMAT_OLDEST_MAJOR_VERSION && minorVersion < AE_PROJECT_FORMAT_OLDEST_MINOR_VERSION
        )) {
        mLog.push_back("The file version is too old to read properly.");
        return false;
    }
    if (majorVersion > AE_PROJECT_FORMAT_MAJOR_VERSION ||
        (majorVersion == AE_PROJECT_FORMAT_MAJOR_VERSION && minorVersion > AE_PROJECT_FORMAT_MINOR_VERSION)) {
        mLog.push_back("This file has been made with a new version of AnimeEffects, unable to read.");
        return false;
    }

    mVersion = QVersionNumber(majorVersion, minorVersion);

    // reserved
    if (!aIn.skipZeroArea(16))
        return false;

    return !aIn.isFailed();
}

bool ProjectLoader::readGlobalBlock(util::LEStreamReader& aIn, core::Project& aProject) {
    // signature
    const std::string signature = aIn.readString(4);
    if (signature != "GLBL")
        return false;

    // length
    const uint32 length = aIn.readUInt32();
    if (length != 64)
        return false;

    // width
    const uint32 width = aIn.readUInt32();
    // height
    const uint32 height = aIn.readUInt32();
    // frame range end (the historical "max frame" slot)
    uint32 frameEnd = aIn.readUInt32();
    // fps
    const uint32 fps = aIn.readUInt32();
    // loop
    const uint8 loop = aIn.readByte();

    // reserved: the frame range start lives in the first 4 bytes (zero in
    // every format <= 0.10, so those files load as [0, frameEnd]). From minor
    // 0.12 the optional viewport background override follows it (a presence
    // byte then an ARGB word); older files have the whole region zero and load
    // as no override. The rest stays reserved and zero.
    uint32 frameStart = aIn.readUInt32();

    bool hasViewportBackground = false;
    uint32 viewportBackground = 0;
    if (mVersion.minorVersion() >= 12) {
        hasViewportBackground = aIn.readByte() != 0;
        viewportBackground = aIn.readUInt32();
        if (!aIn.skipZeroArea(47 - 4 - 1 - 4))
            return false;
    } else if (!aIn.skipZeroArea(47 - 4)) {
        return false;
    }

    if (aIn.isFailed())
        return false;

    // A corrupt file may carry a start past the end (every file <= 0.10 has
    // start 0, so only hand-edited or damaged files can invert the range);
    // swap to a valid range rather than letting a negative range break the
    // loop span, the row geometry and the playhead clamp.
    if (frameStart > frameEnd) {
        const uint32 tmp = frameStart;
        frameStart = frameEnd;
        frameEnd = tmp;
    }

    // set values
    aProject.attribute().setImageSize(QSize((int)width, (int)height));
    aProject.attribute().setFrameRange(util::Range((int)frameStart, (int)frameEnd));
    aProject.attribute().setFps(fps);
    aProject.attribute().setLoop((bool)loop);
    if (hasViewportBackground) {
        aProject.attribute().setViewportBackground(QColor::fromRgba(viewportBackground));
    }
    return true;
}

} // namespace ctrl
