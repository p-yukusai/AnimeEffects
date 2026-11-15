// Regression test for util::PackBits::decode (src/util/PackBits.cpp).
//
// The pre-fix body handled PackBits' noop packet like this:
//     if (head == -128)
//         continue;
// A 0x80 header is one byte of "copy nothing", so the loop has to consume it.
// It did not: `while (p < end)` re-read the same byte forever, one core at 100%,
// with no progress and no allocation. Measured on a real project, the loader sat
// in this loop while the rest of the machine was already under pressure from the
// same load, which is why it is fixed as a defect and not just an unreachable
// branch: a single 0x80 anywhere in a project's image data hangs the load.
//
// The last property (a stream of nothing but noop packets terminates) is the
// gate: pre-fix this test never returns. The others are the contract the loader
// depends on, so the fix can not trade the hang for a silent decode error.
//
// The file is self-contained: PackBits.cpp is compiled in directly, so the test
// needs only Qt6::Core, like tests/treeutil_delete_uaf.cpp.
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
#include "util/PackBits.h"

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

std::vector<uint8> bytes(std::initializer_list<int> aValues) {
    std::vector<uint8> out;
    out.reserve(aValues.size());
    for (int v : aValues) out.push_back((uint8)v);
    return out;
}

// Decodes aSrc (a complete PackBits stream) into aDstSize bytes. The decoding
// source is a local copy because XCMemBlock holds a non-const pointer.
bool decode(const std::vector<uint8>& aSrc, size_t aDstSize, std::vector<uint8>& aDstOut) {
    std::vector<uint8> src = aSrc;
    aDstOut.assign(aDstSize, 0xcc);
    util::PackBits decoder;
    XCMemBlock dstBlock(aDstOut.data(), aDstOut.size());
    return decoder.decode(XCMemBlock(src.data(), src.size()), dstBlock);
}

void expectDecode(const std::vector<uint8>& aSrc, size_t aDstSize, bool aExpect,
                  const QString& aWhat) {
    std::vector<uint8> dst;
    check(decode(aSrc, aDstSize, dst) == aExpect, aWhat);
}

} // namespace

int main() {
    // --- the noop packet (the regression) -----------------------------------
    // 0x80 is a packet on its own: it copies nothing and must be skipped. The
    // bytes after it decide the result, so a stream with a noop has to decode
    // exactly like the same stream without it.
    {
        std::vector<uint8> dst;
        const bool ok = decode(bytes({0x80, 0x00, 'A'}), 1, dst);
        check(ok, QStringLiteral("0x80 (noop) before a literal packet decodes"));
        check(dst.size() == 1 && dst[0] == 'A',
              QStringLiteral("the noop contributes nothing: the literal still lands (got \"%1\")")
                  .arg(dst.empty() ? QStringLiteral("<none>") : QChar(dst[0])));
    }
    {
        // The gate. Pre-fix this call never returns.
        std::vector<uint8> onlyNoops(64, 0x80);
        expectDecode(onlyNoops, 1, false,
                     QStringLiteral("64 noop packets terminate and report failure "
                                    "(pre-fix: spins forever)"));
    }
    {
        // Noop + a run + a literal, i.e. a stream an encoder that does emit
        // 0x80 could produce.
        std::vector<uint8> dst;
        const bool ok = decode(bytes({0x80, 0x80, 0xFE, 'Z', 0x01, 'a', 'b'}), 5, dst);
        check(ok, QStringLiteral("noops between a repeat run and a literal decode"));
        const char expect[6] = {'Z', 'Z', 'Z', 'a', 'b', 0};
        check(dst.size() == 5 && std::memcmp(dst.data(), expect, 5) == 0,
              QStringLiteral("the run repeats 3x and the literal follows it"));
    }

    // --- the bounds the loader relies on ------------------------------------
    expectDecode(bytes({0x01, 'A', 'B'}), 1, false,
                 QStringLiteral("a literal run longer than the destination fails (x > dlen)"));
    expectDecode(bytes({0x81, 'A'}), 4, false,
                 QStringLiteral("a repeat run longer than the destination fails (x > dlen)"));
    expectDecode(bytes({0x05, 'A'}), 8, false,
                 QStringLiteral("a literal run the source can not fill fails (no read past the end)"));
    expectDecode(bytes({0x00, 'A'}), 4, false,
                 QStringLiteral("too few bytes decoded fails (x != dlen)"));

    // --- encode -> decode round trip -----------------------------------------
    {
        std::vector<uint8> src;
        src.reserve(400);
        for (int i = 0; i < 300; ++i) src.push_back(0x41);            // one long run
        for (int i = 0; i < 40; ++i) src.push_back((uint8)('a' + (i % 7))); // literals
        for (int i = 0; i < 60; ++i) src.push_back((uint8)i);          // literals, all distinct

        const size_t cap = util::PackBits::worstEncodedSize(src.size());
        std::vector<uint8> enc(cap, 0);
        util::PackBits encoder;
        const size_t encSize = encoder.encode(XCMemBlock(src.data(), src.size()), enc.data());
        check(encSize > 0 && encSize <= cap,
              QStringLiteral("encode stays inside worstEncodedSize (%1 <= %2)")
                  .arg(encSize).arg(cap));
        enc.resize(encSize);

        std::vector<uint8> dst;
        const bool ok = decode(enc, src.size(), dst);
        check(ok, QStringLiteral("the encoded stream decodes"));
        check(dst == src, QStringLiteral("the round trip preserves every byte (runs and literals)"));
    }

    QTextStream(stdout) << (failures == 0
                                ? QStringLiteral("=== packbits_decode: OK ===\n")
                                : QStringLiteral("=== packbits_decode: %1 FAILED ===\n").arg(failures));
    return failures == 0 ? 0 : 1;
}
