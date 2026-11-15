// Regression test for the noop packet in img::PSDUtil::decodePlanePackBits
// (src/img/PSDUtil.cpp) -- the PSD importer's own PackBits reader.
//
// It carried the same defect as util::PackBits::decode:
//     if (packHead == -128)
//         continue;
// A 0x80 header is one byte of "copy nothing", so the loop has to consume it.
// It did not, and `while (srcData < lineEnd)` re-read that byte forever: importing
// a PSD whose RLE data contains a single 0x80 never returned (one core at 100%,
// no dialog, no error). Photoshop does not emit 0x80, but a truncated or edited
// file can carry one, and a decoder whose failure mode is a hang is worse than
// one whose failure mode is a false return.
//
// The gate is "a line of nothing but noop packets terminates": pre-fix this test
// never returns. The other cases pin the contract around it -- a noop before a
// literal must decode to the same bytes as the literal alone (the fix must not
// turn a skippable packet into a decode error), and the width bounds still fail.
//
// Data layout for one plane (see PSDUtil::decodePlanePackBits and the PSD spec):
//   big-endian uint16 line length per row, then that many packed bytes per row.
#include <cstdio>
#include <vector>
#include <QTextStream>
#include "XCAssert.h"
#include "img/PSDUtil.h"

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

// One row: a big-endian length for the packed bytes that follow.
std::vector<uint8> plane(const std::vector<int>& aPacked) {
    std::vector<uint8> out;
    out.push_back((uint8)((aPacked.size() >> 8) & 0xff));
    out.push_back((uint8)(aPacked.size() & 0xff));
    for (int b : aPacked) out.push_back((uint8)b);
    return out;
}

// Decodes one plane; the destination is zeroed so a decode can be read back.
bool decode(const std::vector<uint8>& aSrc, int aW, int aH, int aStride, std::vector<uint8>& aDstOut) {
    aDstOut.assign((size_t)aW * aH * aStride, 0);
    return img::PSDUtil::decodePlanePackBits(aDstOut.data(), aDstOut.size(), aSrc.data(),
                                             aSrc.size(), aW, aH, aStride);
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
    // Control: 2 px wide, one row, one literal packet (header 1 = two literals).
    {
        std::vector<uint8> dst;
        const bool ok = decode(plane({0x01, 'A', 'B'}), 2, 1, 1, dst);
        check(ok, QStringLiteral("control: a literal-only row decodes"));
        check(dst.size() == 2 && dst[0] == 'A' && dst[1] == 'B',
              QStringLiteral("control: the literal bytes land in the destination"));
    }

    // A noop before the literal: same result as the control.
    {
        std::vector<uint8> dst;
        const bool ok = decode(plane({0x80, 0x01, 'A', 'B'}), 2, 1, 1, dst);
        check(ok, QStringLiteral("0x80 (noop) before a literal packet still decodes"));
        check(dst.size() == 2 && dst[0] == 'A' && dst[1] == 'B',
              QStringLiteral("the noop contributes nothing: the literal still lands"));
    }

    // The gate. Pre-fix this call never returns.
    {
        std::vector<uint8> dst;
        check(!decode(plane({0x80, 0x80}), 2, 1, 1, dst),
              QStringLiteral("a row of nothing but noop packets terminates and reports the width "
                             "mismatch (pre-fix: spins forever)"));
    }

    // The width bound is unaffected by the fix.
    {
        std::vector<uint8> dst;
        check(!decode(plane({0xFE, 'A'}), 2, 1, 1, dst),
              QStringLiteral("a repeat run wider than the row fails (x > width)"));
    }
    {
        std::vector<uint8> dst;
        std::vector<uint8> src = plane({0x03, 'A', 'B', 'C', 'D'});
        check(!decode(src, 2, 1, 1, dst),
              QStringLiteral("a literal run wider than the row fails (x > width)"));
    }

    QTextStream(stdout) << (failures == 0
                                ? QStringLiteral("=== psd_rle_noop: OK ===\n")
                                : QStringLiteral("=== psd_rle_noop: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
