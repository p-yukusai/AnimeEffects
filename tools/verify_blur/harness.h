// Shared bookkeeping and stubs for the verification harness suites: check/fail counters,
// expect/caseReport reporting, and the no-op Animator/ProgressReporter implementations the
// production import/export paths require. Header-only so every suite translation unit
// reports into the same counters.
#ifndef VERIFY_BLUR_HARNESS_H
#define VERIFY_BLUR_HARNESS_H

#include <cstdio>
#include <QString>
#include "core/Animator.h"
#include "util/IProgressReporter.h"
#include "scene.h"

namespace hb {

inline int gChecks = 0;
inline int gFails = 0;

inline bool expect(bool aCond, const QString& aWhat) {
    ++gChecks;
    if (!aCond) {
        ++gFails;
        std::printf("    [FAIL] %s\n", aWhat.toUtf8().constData());
    }
    return aCond;
}

inline void suiteHeader(const char* aName) { std::printf("== %s ==\n", aName); }

inline void caseReport(const QString& aName, bool aOk, const QString& aDetails) {
    std::printf(
        "  [%s] %-58s %s\n", aOk ? "PASS" : "FAIL", aName.toUtf8().constData(), aDetails.toUtf8().constData());
}

inline QString diffStr(const scene::Diff& aD) {
    return QString("max=%1 mean=%2 >2:%3 >4:%4").arg(aD.maxDiff).arg(aD.mean, 0, 'f', 3).arg(aD.over2).arg(aD.over4);
}

// Animator with a fixed frame 0 and no playback; core::Project needs one
class StubAnimator: public core::Animator {
public:
    core::Frame currentFrame() const override { return core::Frame(0); }
    void stop() override {}
    void suspend() override {}
    void resume() override {}
    bool isSuspended() const override { return false; }
};

// IProgressReporter that swallows everything; the production loaders need one
class NullProgressReporter: public util::IProgressReporter {
public:
    void setSection(const QString&) override {}
    void setMaximum(int) override {}
    void setProgress(int) override {}
    bool wasCanceled() const override { return false; }
};

} // namespace hb

#endif // VERIFY_BLUR_HARNESS_H
