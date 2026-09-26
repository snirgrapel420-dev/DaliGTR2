// DaliGuitar engine tests - plain C++17, no JUCE needed.
// Build: g++ -std=c++17 -O2 -ISource/Engine tests/engine_test.cpp -o engine_test && ./engine_test
#include "StringVoice.h"
#include "Fretboard.h"
#include "NoiseLayer.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace dg;

static int failures = 0;
#define CHECK(cond, ...) do { if (cond) { std::printf ("  ok    "); } else { std::printf ("  FAIL  "); ++failures; } \
                              std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

static constexpr double SR = 48000.0;

// autocorrelation pitch estimate with parabolic refinement
static double estimatePitch (const std::vector<float>& x, int start, int len)
{
    auto corr = [&] (int lag) { double c = 0; for (int i = 0; i < len; ++i) c += x[(size_t) (start + i)] * x[(size_t) (start + i + lag)]; return c; };
    int best = 0; double bestV = -1e30;
    for (int lag = (int) (SR / 1500); lag < (int) (SR / 60); ++lag) { const double c = corr (lag); if (c > bestV) { bestV = c; best = lag; } }
    const double a = corr (best - 1), b = corr (best), c = corr (best + 1);
    const double denom = a - 2 * b + c;
    return SR / (best + (denom != 0 ? 0.5 * (a - c) / denom : 0.0));
}

static std::vector<float> renderNote (int note, int fret, float vel, const VoiceGlobals& g, double seconds, StringVoice& v)
{
    StringVoice::PickParams p;
    p.note = note; p.fret = fret; p.velocity = vel; p.intonation = 0.0f;
    v.pick (p, g);
    std::vector<float> out;
    for (int i = 0; i < (int) (SR * seconds); ++i) out.push_back (v.process (0.0f, g));
    return out;
}

static bool finite (const std::vector<float>& x) { for (float s : x) if (! std::isfinite (s)) return false; return true; }

int main()
{
    VoiceGlobals g;
    g.driftCents = 0.0f;

    std::printf ("Tuning across the neck\n");
    for (int note : { 40, 45, 52, 57, 64, 71, 76, 86 })
    {
        StringVoice v; v.prepare (SR, 0, 40.0f);
        const auto x = renderNote (note, 0, 0.7f, g, 0.6, v);
        const double f = estimatePitch (x, (int) (SR * 0.25), 4000);
        const double expected = 440.0 * std::pow (2.0, (note - 69) / 12.0);
        const double cents = 1200.0 * std::log2 (f / expected);
        CHECK (finite (x) && std::abs (cents) < 5.0, "note %d: %.2f Hz (%.1f cents)", note, f, cents);
    }

    std::printf ("String behaviour\n");
    {
        StringVoice v; v.prepare (SR, 2, 50.0f);
        auto x = renderNote (57, 7, 0.8f, g, 0.4, v);
        v.legato (59, 9, AttackType::Hammer, 0.6f, 0.05f);
        for (int i = 0; i < (int) (SR * 0.5); ++i) x.push_back (v.process (0.0f, g));
        const double f = estimatePitch (x, (int) (SR * 0.6), 4000);
        CHECK (std::abs (1200.0 * std::log2 (f / 246.94)) < 8.0, "hammer-on A3 -> B3 lands at %.1f Hz", f);

        v.release (false, 0.4f);
        int n = 0;
        while (v.isSounding() && n < (int) SR * 3) { v.process (0.0f, g); ++n; }
        CHECK (! v.isSounding(), "fretted note stops after release (%.3f s)", n / SR);
    }
    {
        StringVoice v; v.prepare (SR, 0, 40.0f);
        const auto x = renderNote (40, 0, 0.8f, g, 2.0, v);
        auto rms = [&] (double t) { double s = 0; for (int i = 0; i < 2400; ++i) s += x[(size_t) (t * SR) + i] * x[(size_t) (t * SR) + i]; return std::sqrt (s / 2400); };
        CHECK (rms (1.5) > rms (0.1) * 0.05, "open low E sustains (%.4f -> %.4f)", rms (0.1), rms (1.5));

        VoiceGlobals pm = g; pm.palmMute = 0.8f;
        StringVoice m; m.prepare (SR, 0, 40.0f);
        const auto y = renderNote (40, 0, 0.8f, pm, 1.0, m);
        double s = 0; for (int i = 0; i < 2400; ++i) s += y[(size_t) (0.5 * SR) + i] * y[(size_t) (0.5 * SR) + i];
        CHECK (std::sqrt (s / 2400) < rms (0.5) * 0.1, "palm mute decays much faster than open note");
    }
    {
        // bend via pitch wheel: +2 semitones should arrive
        StringVoice v; v.prepare (SR, 4, 59.0f);
        VoiceGlobals b = g; b.wheelBendSemis = 2.0f;
        const auto x = renderNote (67, 8, 0.8f, b, 1.0, v);
        const double f = estimatePitch (x, (int) (SR * 0.6), 4000);
        CHECK (std::abs (1200.0 * std::log2 (f / 440.0)) < 10.0, "bend G4 +1 tone reaches A4 (%.1f Hz)", f);
    }

    std::printf ("Stability\n");
    {
        StringVoice a, b; a.prepare (SR, 0, 40.0f); b.prepare (SR, 1, 45.0f);
        VoiceGlobals r = g; r.resonance = 1.0f;
        StringVoice::PickParams p; p.note = 40; p.fret = 0; p.velocity = 1.0f; a.pick (p, r);
        float la = 0, lb = 0, peak = 0; bool ok = true;
        for (int i = 0; i < (int) (SR * 5); ++i)
        {
            const float bus = la + lb;
            la = a.process (bus - la, r);
            lb = b.process (bus - lb, r);
            if (! std::isfinite (la) || ! std::isfinite (lb)) ok = false;
            peak = std::max (peak, std::abs (lb));
        }
        CHECK (ok && peak < 0.2f, "sympathetic resonance stays stable (peak %.4f)", peak);
    }
    {
        StringVoice v; v.prepare (SR, 5, 64.0f);
        bool ok = true;
        for (int k = 0; k < 64; ++k)
        {
            StringVoice::PickParams p; p.note = 64 + (k % 22); p.fret = k % 22; p.velocity = 1.0f;
            p.down = k % 2 == 0; p.variation = k % 24; p.artic = (Artic) (k % (int) Artic::Count);
            v.pick (p, g);
            for (int i = 0; i < 1200; ++i) if (! std::isfinite (v.process (0.0f, g))) ok = false;
        }
        CHECK (ok, "64 fast repicks through every articulation: no NaN/Inf");
    }
    {
        NoiseLayer n; n.prepare (SR);
        std::array<float, 6> mix { 1, 1, 1, 1, 1, 1 };
        for (int k = 0; k < 40; ++k) n.trigger ((NoiseType) (k % 6), 1.0f, 500.0f, 5000.0f, 0.05f, 0.001f, 3.0f);
        bool ok = true;
        for (int i = 0; i < (int) SR; ++i) if (! std::isfinite (n.process (mix))) ok = false;
        CHECK (ok, "noise layer handles voice stealing");
    }

    std::printf ("Fretboard\n");
    {
        Fretboard fb; fb.setTuning (0);
        bool ok = true;
        for (int note = fb.lowestNote(); note <= fb.highestNote(); ++note)
        {
            const auto fp = fb.choose (note, 5.0f, Fretboard::Automatic, 7.0f, 0u, -1);
            if (! fp.valid() || fb.openNote (fp.string) + fp.fret != note) ok = false;
        }
        CHECK (ok, "every note E2..D6 maps to a correct string/fret");

        const auto hi = fb.choose (67, 5.0f, Fretboard::High, 7.0f, 0u, -1);
        CHECK (hi.fret >= 10, "High position puts G4 at fret %d (string %d)", hi.fret, 6 - hi.string);
        const auto lo = fb.choose (67, 5.0f, Fretboard::Low, 7.0f, 0u, -1);
        CHECK (lo.fret <= 5, "Low position puts G4 at fret %d (string %d)", lo.fret, 6 - lo.string);

        const auto busy = fb.choose (64, 0.0f, Fretboard::Automatic, 7.0f, 1u << 5, -1);
        CHECK (busy.string != 5, "busy string is avoided (E4 moves to string %d)", 6 - busy.string);

        fb.setTuning (4);
        CHECK (fb.lowestNote() == 36, "Drop C tuning lowest note = C2");
    }

    std::printf (failures == 0 ? "\nALL TESTS PASSED\n" : "\n%d TEST(S) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
