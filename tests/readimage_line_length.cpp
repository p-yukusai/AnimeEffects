// Regression test for the line length check in core::Deserializer::readImage
// (src/core/Deserializer.cpp).
//
// readImage sizes its per-line working buffer from the image width --
// util::PackBits::worstEncodedSize(w) bytes -- and then reads that line's
// compressed bytes into it. The length ("linelen") comes straight from the file.
// Pre-fix nothing compared the two, so a damaged or hand-edited project could
// declare 16 MB of compressed bytes for a 100 px line and the loader copied all
// of it into the 101-byte block: the write runs off the heap block, and the
// bytes it copies are the ones the file supplies.
//
// The fix rejects a line that claims more encoded bytes than a line of w pixels
// can possibly hold. This test drives the real Deserializer over a crafted image
// block and asserts the refusal. Pre-fix the copy overshoots the block and the
// process dies inside readBuf (glibc's heap corruption report, or a SIGSEGV past
// the end of the arena) -- i.e. it never reaches its printout.
//
// Two cases, because one alone proves nothing: the same builder's well-formed
// stream must be ACCEPTED (otherwise a refusal could just as well come from a
// broken harness), and a stream whose length field is overstated must be
// refused.
//
// Block layout (little endian; see Serializer's image writer):
//   u32 compType=1, u32 w, u32 h, u64 length=<bytes of all line records>,
//   then per line and per channel: u32 linelen, linelen bytes of packed data.
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include <QTextStream>
#include "XCAssert.h"
#include "core/Deserializer.h"
#include "gl/DeviceInfo.h"
#include "util/IDSolver.h"
#include "util/IProgressReporter.h"
#include "util/PackBits.h"
#include "util/StreamReader.h"

namespace {

int failures = 0;

void check(bool aCond, const QString& aWhat) {
    if (aCond) {
        QTextStream(stdout) << "PASS " << aWhat << "\n";
    } else {
        ++failures;
        QTextStream(stdout) << "FAIL " << aWhat << "\n";
    }
}

class NullReporter : public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

void putU32(std::string& aOut, uint32 aValue) {
    for (int i = 0; i < 4; ++i) aOut.push_back((char)((aValue >> (8 * i)) & 0xff));
}

void putU64(std::string& aOut, uint64 aValue) {
    for (int i = 0; i < 8; ++i) aOut.push_back((char)((aValue >> (8 * i)) & 0xff));
}

// One line record: what the linelen field says, then the bytes the file really
// delivers for it (padded to the declared length, so an overstated length is
// backed by real data to copy -- exactly the shape that corrupted the heap).
struct Record {
    uint32 declaredLen;
    std::vector<uint8> packed;

    // A well-formed PackBits line for a pixels: one literal packet.
    static Record literal(uint32 aW) {
        const size_t count = aW < 128 ? aW : 128;
        Record r;
        r.declaredLen = (uint32)(1 + count);
        r.packed.push_back((uint8)(count - 1));
        for (size_t i = 0; i < count; ++i) r.packed.push_back((uint8)(0x30 + (i % 16)));
        return r;
    }

    static Record overstated(uint32 aLen, size_t aPayload) {
        Record r;
        r.declaredLen = aLen;
        r.packed.assign(aPayload, 0x00);
        return r;
    }
};

void putLine(std::string& aOut, const Record& aRecord) {
    putU32(aOut, aRecord.declaredLen);
    for (size_t i = 0; i < aRecord.declaredLen; ++i)
        aOut.push_back((char)(i < aRecord.packed.size() ? aRecord.packed[i] : 0x00));
}

// The block for a w x h image whose lines hold the given channel records.
std::string buildImage(uint32 aW, uint32 aH, const std::vector<Record>& aChannels) {
    std::string lines;
    for (const Record& r : aChannels) putLine(lines, r);

    std::string blob;
    putU32(blob, 1);  // compType: PackBits
    putU32(blob, aW);
    putU32(blob, aH);
    putU64(blob, (uint64)lines.size() * aH); // total compressed length
    blob += lines;
    return blob;
}

// Feeds the block to a real Deserializer; true when readImage accepted it.
bool readImage(const std::string& aBlob) {
    std::istringstream stream(aBlob, std::ios::in | std::ios::binary);
    util::LEStreamReader in(stream);
    util::IDSolver<void*> solver;
    NullReporter reporter;
    gl::DeviceInfo dev;
    dev.vender = "test";
    dev.renderer = "test";
    dev.version = "4";
    dev.maxTextureSize = 16384;
    dev.maxRenderBufferSize = 16384;
    core::Deserializer de(in, solver, aBlob.size(), QVersionNumber(0, 8), dev, reporter, 0);

    XCMemBlock block; // readImage asserts it arrives empty
    return de.readImage(block);
}

} // namespace

class AssertHandler : public XCAssertHandler {
public:
    void failure() const override { std::abort(); }
};
class ErrorHandler : public XCErrorHandler {
public:
    void critical(const QString&, const QString&, const QString&) const override { std::abort(); }
};

AssertHandler gAssertHandler;
ErrorHandler gErrorHandler;
XCAssertHandler* gXCAssertHandler = &gAssertHandler;
XCErrorHandler* gXCErrorHandler = &gErrorHandler;

int main() {
    // Control: 4 px wide, one line, four channels of one literal packet each
    // (4 data bytes + header = 5). The harness must accept this, or the refusal
    // below would prove nothing about the length check.
    {
        std::vector<Record> channels;
        for (int i = 0; i < 4; ++i) channels.push_back(Record::literal(4));
        check(readImage(buildImage(4, 1, channels)),
              QStringLiteral("control: a well-formed 4x1 image block is accepted"));
    }

    // The defect: a 100 px line (its buffer holds 101 bytes) declaring 16 MB,
    // with 512 KB really delivered -- pre-fix that is the copy that corrupts.
    {
        std::vector<Record> channels;
        for (int i = 0; i < 4; ++i) channels.push_back(Record::overstated(0x00FFFFFF, 512 * 1024));
        check(!readImage(buildImage(100, 1, channels)),
              QStringLiteral("a line declaring 16 MB for a 100 px line is refused "
                             "(pre-fix: copies 512 KB into the 101-byte line buffer)"));
    }

    // A near miss: exactly one byte over what a 100 px line can encode.
    {
        const size_t cap = util::PackBits::worstEncodedSize(100); // 101
        std::vector<Record> channels;
        for (int i = 0; i < 4; ++i)
            channels.push_back(Record::overstated((uint32)(cap + 1), cap + 1));
        check(!readImage(buildImage(100, 1, channels)),
              QStringLiteral("a line one byte longer than the line buffer is refused"));
    }

    QTextStream(stdout) << (failures == 0
                                ? QStringLiteral("=== readimage_line_length: OK ===\n")
                                : QStringLiteral("=== readimage_line_length: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
