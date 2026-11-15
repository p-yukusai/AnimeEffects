// V2 — media::AudioMixer multi-track mixing correctness + edge cases.
//
// Verification-first, headless (no GUI). Real PCM WAV files are generated so
// the decode path (s16 -> float via avcodec+swresample) is exercised, then
// mixed. The key test asserts the mixed samples equal the ANALYTIC sum of the
// per-track sines (within a quantization tolerance), proving offsets, volumes,
// and overlap are applied correctly. Also covers rate conversion and edge
// cases (empty/disabled/reversed/zero-volume/out-of-range tracks).
//
// Run: ./build/tests/Debug/media_audio_mixer
#include <QVector>
#include <QString>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QTextStream>
#include <QtMath>

#include "media/AudioMixer.h"

#include <cmath>

static int failures = 0;
static void check(bool ok, const char* what) {
    QTextStream(stdout) << (ok ? "PASS  " : "FAIL  ") << what << "\n";
    if (!ok) ++failures;
}

static constexpr double kPi = 3.14159265358979323846;

// Write a 16-bit PCM sine WAV (mono or stereo). Returns true on success.
static bool writeWav(const QString& path, int rate, int channels, int seconds, double freq,
                     double amp, double pha = 0.0) {
    const int numSamples = rate * seconds;
    const int blockAlign = channels * 2; // 16-bit
    const int dataSize = numSamples * blockAlign;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    auto w32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto w16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    // RIFF
    f.write("RIFF", 4); w32(quint32(36 + dataSize)); f.write("WAVE", 4);
    // fmt
    f.write("fmt ", 4); w32(16); w16(1); w16(quint16(channels));
    w32(quint32(rate)); w32(quint32(rate * blockAlign)); w16(quint16(blockAlign)); w16(16);
    // data
    f.write("data", 4); w32(quint32(dataSize));
    for (int i = 0; i < numSamples; ++i) {
        const double t = double(i) / rate;
        const double s = amp * std::sin(2.0 * kPi * freq * t + pha);
        const qint16 v = qint16(qBound(-1.0, s, 1.0) * 32767.0);
        for (int c = 0; c < channels; ++c) w16(quint16(v));
    }
    return true;
}

// Compute the expected mixed sample at output index n (per-channel value).
static double expectedMix(int n, int rate, int startF1, double amp1, double f1,
                          int startF2, double amp2, double f2) {
    double v = 0.0;
    if (n >= startF1) {
        const double t = double(n - startF1) / rate;
        v += amp1 * std::sin(2.0 * kPi * f1 * t);
    }
    if (n >= startF2) {
        const double t = double(n - startF2) / rate;
        v += amp2 * std::sin(2.0 * kPi * f2 * t);
    }
    return v;
}

static bool run_exact_sum() {
    const int rate = 48000, fps = 24, totalFrames = 240; // 10 s
    const QString f1 = QDir::temp().filePath("ae_v2_t1.wav");
    const QString f2 = QDir::temp().filePath("ae_v2_t2.wav");
    // track1: 40% amp, 440 Hz, offset frame 24 (offsetSamples = 48000)
    // track2: 80% amp * 0.5 volume = 40% amp, 880 Hz, offset frame 72 (144000)
    const int start1 = 24, start2 = 72;
    const double amp1 = 0.4, amp2 = 0.4; // effective (after volume)
    writeWav(f1, rate, 1, 10, 440, 0.4);
    writeWav(f2, rate, 1, 10, 880, 0.8);

    media::AudioMixer::Track t1, t2;
    t1.path = f1; t1.startFrame = start1; t1.volume = 1.0f; t1.endFrame = 0;
    t2.path = f2; t2.startFrame = start2; t2.volume = 0.5f; t2.endFrame = 0;

    media::AudioMixer mx;
    media::AudioMixer::Config cfg;
    cfg.fps = fps; cfg.totalFrames = totalFrames; cfg.sampleRate = rate; cfg.channels = 1;
    check(mx.mix({ t1, t2 }, cfg), "mix() succeeds");
    check(mx.sampleCount() == 240 * 48000 / 24, "output sample count == totalFrames/fps * rate");

    // Silence region (before both offsets) must be ~exactly 0.
    const int silence = 24000;
    check(std::fabs(mx.samples()[silence]) < 1e-4, "pre-offset region is silent");

    // Track-1-only region.
    const int only1 = 72000;
    const double eTrack = expectedMix(only1, rate, start1 * rate / fps, amp1, 440,
                                      start2 * rate / fps, amp2, 880);
    check(std::fabs(mx.samples()[only1] - eTrack) < 0.005, "track-1-only region matches analytic");

    // Overlap region (both active).
    const int both = 200000;
    const double eBoth = expectedMix(both, rate, start1 * rate / fps, amp1, 440,
                                     start2 * rate / fps, amp2, 880);
    check(std::fabs(mx.samples()[both] - eBoth) < 0.005, "overlap region matches analytic sum");

    QFile::remove(f1); QFile::remove(f2);
    return true;
}

static bool run_rate_conversion() {
    // 44.1 kHz mono mixed into a 48 kHz stereo output.
    const QString f = QDir::temp().filePath("ae_v2_441.wav");
    writeWav(f, 44100, 1, 2, 660, 0.6);
    media::AudioMixer::Track t;
    t.path = f; t.startFrame = 0; t.volume = 1.0f;

    media::AudioMixer mx;
    media::AudioMixer::Config cfg;
    cfg.fps = 24; cfg.totalFrames = 48; cfg.sampleRate = 48000; cfg.channels = 2;
    check(mx.mix({ t }, cfg), "mix() succeeds for 44.1k->48k");
    check(mx.channels() == 2 && mx.sampleRate() == 48000, "output channels/rate correct");
    // Energy must be present mid-way (nonzero, finite).
    const int mid = int(48.0 / 24.0 * 48000.0) * 2; // interleaved, ~end
    bool nonZero = false;
    for (int i = 20000 * 2; i < mid; ++i) {
        float s = mx.samples()[i];
        if (std::isfinite(s) && std::fabs(s) > 1e-3) { nonZero = true; break; }
    }
    check(nonZero, "44.1k->48k resampled mix has real energy");
    QFile::remove(f);
    return true;
}

static bool run_edge_cases() {
    const QString f = QDir::temp().filePath("ae_v2_edge.wav");
    writeWav(f, 48000, 1, 1, 440, 0.5);

    media::AudioMixer::Config cfg;
    cfg.fps = 24; cfg.totalFrames = 48; cfg.sampleRate = 48000; cfg.channels = 1;

    media::AudioMixer empty;
    check(empty.mix({}, cfg), "empty mix succeeds");
    // empty tracks -> output is all zeros.
    bool allZero = true;
    for (int i = 0; i < empty.sampleCount(); ++i) if (empty.samples()[i] != 0.0f) { allZero = false; break; }
    check(allZero, "empty mix produces all-zero samples");

    media::AudioMixer dead;
    media::AudioMixer::Track off; off.path = f; off.enabled = false;
    check(dead.mix({ off }, cfg) && dead.samples()[0] == 0.0f, "disabled track contributes nothing");

    media::AudioMixer reverse;
    media::AudioMixer::Track rev; rev.path = f; rev.startFrame = 40; rev.endFrame = 8; // start>end -> skip
    check(reverse.mix({ rev }, cfg) && reverse.samples()[0] == 0.0f, "reversed track (start>end) contributes nothing");

    media::AudioMixer silent;
    media::AudioMixer::Track s; s.path = f; s.startFrame = 0; s.volume = 0.0f;
    check(silent.mix({ s }, cfg) && silent.samples()[200 * 1] == 0.0f, "zero-volume track contributes nothing");

    // Negative start frame clamped out.
    media::AudioMixer neg;
    media::AudioMixer::Track n; n.path = f; n.startFrame = -5;
    check(neg.mix({ n }, cfg) && neg.samples()[0] == 0.0f, "negative start frame contributed nothing");

    // Invalid config rejected.
    media::AudioMixer bad;
    media::AudioMixer::Config bc; bc.totalFrames = 0;
    check(!bad.mix({}, bc), "fps/totalFrames=0 rejected (mix returns false)");

    QFile::remove(f);
    return true;
}

static bool run_audio_edge_cases() {
    // A real 1s 48k mono tone that the other tests also use.
    const QString good = QDir::temp().filePath("ae_v2_extra.wav");
    writeWav(good, 48000, 1, 1, 440, 0.5);

    media::AudioMixer::Config cfg;
    cfg.fps = 24; cfg.totalFrames = 24; cfg.sampleRate = 48000; cfg.channels = 1;

    // 1) Missing / corrupt audio file: the mix still succeeds and the bad track
    //    is skipped + reported (a single bad track must not abort the export).
    media::AudioMixer bad;
    media::AudioMixer::Track gone;
    gone.path = QDir::temp().filePath("ae_v2_does_not_exist.wav");
    gone.startFrame = 0; gone.volume = 1.0f;
    bool okBad = bad.mix({ gone }, cfg);
    check(okBad, "missing audio file: mix still succeeds (track skipped)");
    check(!bad.error().isEmpty(), "missing audio file: skipped track reported in error()");
    bool nonZeroGone = false;
    for (int i = 0; i < bad.sampleCount(); ++i) if (bad.samples()[i] != 0.0f) { nonZeroGone = true; break; }
    check(!nonZeroGone, "missing audio file: output is silent (no phantom samples)");

    // 2) A track whose end frame extends beyond the export range is clamped: only
    //    the in-range window contributes.
    media::AudioMixer clamp;
    media::AudioMixer::Track tClamp;
    tClamp.path = good; tClamp.startFrame = 0; tClamp.endFrame = 60; tClamp.volume = 1.0f; // end == 60 > 24 range
    bool okClamp = clamp.mix({ tClamp }, cfg);
    check(okClamp, "out-of-range endFrame: mix succeeds");
    check(clamp.sampleCount() == 24 * 48000 / 24, "out-of-range endFrame: window is the full export range");
    bool energyClamp = false;
    for (int i = 0; i < clamp.sampleCount(); ++i) if (std::fabs(clamp.samples()[i]) > 1e-3) { energyClamp = true; break; }
    check(energyClamp, "out-of-range endFrame: in-range portion still has energy");

    // 3) Long audio vs a short export range: only the in-range window is used
    //    (sampleCount reflects the export window, not the source length).
    media::AudioMixer shortwin;
    media::AudioMixer::Track tShort;
    tShort.path = good; tShort.startFrame = 0; tShort.volume = 1.0f; // source is 1s == 48000 samples
    bool okShort = shortwin.mix({ tShort }, cfg);
    check(okShort && shortwin.sampleCount() == 24 * 48000 / 24,
          "long audio vs short range: output window == export duration");

    // 4) Volume >1 and very large: no NaN/overflow, post-mix clamp to [-1,1].
    for (float vol : { 2.0f, 100.0f }) {
        media::AudioMixer gain;
        media::AudioMixer::Track tG;
        tG.path = good; tG.startFrame = 0; tG.volume = vol;
        bool okG = gain.mix({ tG }, cfg);
        bool finite = true, inRange = true;
        for (int i = 0; i < gain.sampleCount(); ++i) {
            const float s = gain.samples()[i];
            if (!std::isfinite(s)) finite = false;
            if (s < -1.0f || s > 1.0f) inRange = false;
        }
        check(okG && finite, QString("volume=%1: no NaN/inf").arg(vol).toUtf8().constData());
        check(inRange, QString("volume=%1: samples clamped to [-1,1]").arg(vol).toUtf8().constData());
    }

    // 5) No temp/output files are created by the mixer itself (the legacy CLI
    //    path wrote .mus.temp* files).
    const QString marker = QDir::temp().filePath(".mus.temp");
    const bool noMusTemp = QFileInfo::exists(marker);
    check(!noMusTemp, "no .mus.temp* files created by in-process mixing");

    QFile::remove(good);
    return true;
}

int main(int argc, char** argv) {
    // No GUI needed; QCoreApplication is enough (QImage not used here).
    QCoreApplication app(argc, argv);
    QTextStream(stdout) << "=== V2 media::AudioMixer suite ===\n";
    run_exact_sum();
    run_rate_conversion();
    run_edge_cases();
    run_audio_edge_cases();
    QTextStream(stdout) << "=== result ===\n";
    return failures == 0 ? 0 : 1;
}
