#include <fstream>
#include "core/FormatVersion.h"
#include "core/Serializer.h"
#include "ctrl/ProjectSaver.h"

namespace ctrl {

ProjectSaver::ProjectSaver(): mLog() {}

bool ProjectSaver::save(const QString& aFilePath, const core::Project& aProject) {
    std::ofstream file(aFilePath.toLocal8Bit(), std::ios::out | std::ios::binary);

    if (file.fail()) {
        mLog = "Unable to save the project.";
        return false;
    }

    util::StreamWriter out(file);

    if (!writeHeader(out)) {
        mLog = "Failed to write header.";
        return false;
    }

    if (!writeGlobalBlock(out, aProject)) {
        mLog = "Failed to write global block.";
        return false;
    }

    core::Serializer serializer(out);

    if (!aProject.resourceHolder().serialize(serializer)) {
        mLog = "Failed to write resources block.";
        return false;
    }

    // write object tree
    if (!aProject.objectTree().serialize(serializer)) {
        mLog = "Failed to write object tree block.";
        return false;
    }

    mLog = "Success.";
    return true;
}

bool ProjectSaver::writeHeader(util::StreamWriter& aOut) {
    static const std::array<uint8, 6> kSignature{'A', 'N', 'I', 'M', 'F', 'X'};
    static const std::array<uint8, 2> kEndian{0xff, 0x00};
    static const uint32 kMajorVersion = AE_PROJECT_FORMAT_MAJOR_VERSION;
    static const uint32 kMinorVersion = AE_PROJECT_FORMAT_MINOR_VERSION;
    static const int kReserveSize = 16;

    // signature
    aOut.write(kSignature);
    // endian (little)
    aOut.write(kEndian);
    // major version
    aOut.write(kMajorVersion);
    // minor version
    aOut.write(kMinorVersion);
    // reserved
    aOut.writeZero(kReserveSize);

    return !aOut.isFailed();
}

bool ProjectSaver::writeGlobalBlock(util::StreamWriter& aOut, const core::Project& aProject) {
    static const std::array<sint8, 4> kSignature{'G', 'L', 'B', 'L'};
    static const uint32 kBlockLength = 64;
    // The reserve's first 4 bytes carry the frame range start (0 in every
    // format <= 0.10, where the whole region was zero-filled), then the
    // viewport background override (a presence byte and an ARGB word, zero for
    // an unset override in any format <= 0.11); the rest stays reserved. The
    // frame range end sits in the historical "max frame" slot.
    static const int kReserveSize = 47;

    const QSize imageSize = aProject.attribute().imageSize();
    const util::Range frameRange = aProject.attribute().frameRange();
    const int fps = aProject.attribute().fps();
    const bool loop = aProject.attribute().loop();
    const bool hasViewportBackground = aProject.attribute().hasViewportBackground();
    const QColor viewportBackground = aProject.attribute().viewportBackground();
    XC_ASSERT(frameRange.min() <= frameRange.max()); // negative ranges are valid
    XC_ASSERT(fps > 0);

    // block signature
    aOut.write(kSignature);
    // block length
    aOut.write(kBlockLength);

    // width
    aOut.write((uint32)imageSize.width());
    // height
    aOut.write((uint32)imageSize.height());
    // frame range end (the historical "max frame" slot)
    aOut.write((uint32)frameRange.max());
    // fps
    aOut.write((uint32)fps);
    // loop
    aOut.write((uint8)loop);

    // reserved: frame range start, then the viewport background override
    // (presence byte + ARGB word), the rest zero
    aOut.write((uint32)frameRange.min());
    aOut.write((uint8)hasViewportBackground);
    aOut.write((uint32)(hasViewportBackground ? viewportBackground.rgba() : 0u));
    aOut.writeZero(kReserveSize - 4 - 1 - 4);

    return !aOut.isFailed();
}

} // namespace ctrl
