#pragma once
#include "DspUtil.h"
#include <array>

namespace dg
{
enum class NoiseType : int { Pick = 0, Fret, Release, String, Slide, Scrape, Count };

// Every performance noise is a short swept band-pass noise event with its own mixer level.
class NoiseLayer
{
public:
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        for (auto& e : ev) e.on = false;
        rng.seed (0xC0FFEEu);
    }

    void allOff() { for (auto& e : ev) e.on = false; }

    void trigger (NoiseType t, float amp, float f0, float f1, float len, float attack, float q)
    {
        if (amp <= 0.0f) return;
        Ev* slot = nullptr;
        float oldest = -1.0f;
        for (auto& e : ev)
        {
            if (! e.on) { slot = &e; break; }
            if (e.t > oldest) { oldest = e.t; slot = &e; }
        }
        Ev& e = *slot;
        e = Ev {};
        e.on = true; e.type = t; e.amp = amp; e.f0 = f0; e.f1 = f1;
        e.len = std::max (0.002f, len);
        e.attack = std::max (0.0002f, attack);
        e.k = 1.0f / std::max (0.3f, q);
    }

    float process (const std::array<float, 6>& mix)
    {
        float out = 0.0f;
        const float inv = 1.0f / sr;
        for (auto& e : ev)
        {
            if (! e.on) continue;
            const float x = e.t / e.len;
            if (x >= 1.0f) { e.on = false; continue; }

            const float envA = e.t < e.attack ? e.t / e.attack : 1.0f;
            const float envD = (1.0f - x) * (1.0f - x);
            const float fc = clampf (e.f0 + (e.f1 - e.f0) * x, 40.0f, sr * 0.45f);

            // TPT state-variable band-pass
            const float gC = std::tan (kPi * fc / sr);
            const float a1 = 1.0f / (1.0f + gC * (gC + e.k));
            const float a2 = gC * a1, a3 = gC * a2;
            float n = rng.bi();
            if (e.type == NoiseType::Scrape) n *= rng.uni() > 0.75f ? 1.6f : 0.4f;   // grainy winding texture
            const float v3 = n - e.ic2;
            const float v1 = a1 * e.ic1 + a2 * v3;
            const float v2 = e.ic2 + a2 * e.ic1 + a3 * v3;
            e.ic1 = 2.0f * v1 - e.ic1;
            e.ic2 = 2.0f * v2 - e.ic2;

            out += v1 * e.k * e.amp * envA * envD * mix[(size_t) e.type];
            e.t += inv;
        }
        return out;
    }

private:
    struct Ev
    {
        bool on = false;
        NoiseType type = NoiseType::Pick;
        float amp = 0.0f, f0 = 1000.0f, f1 = 1000.0f, len = 0.01f, attack = 0.001f, k = 1.0f, t = 0.0f, ic1 = 0.0f, ic2 = 0.0f;
    };
    std::array<Ev, 32> ev {};
    float sr = 44100.0f;
    FastRandom rng;
};
} // namespace dg
