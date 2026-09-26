#pragma once
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace dg
{
constexpr float kPi    = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717958f;

inline float midiToHz (float n)             { return 440.0f * std::exp2 ((n - 69.0f) / 12.0f); }
inline float dbToGain (float db)            { return std::pow (10.0f, db / 20.0f); }
inline float clampf (float x, float a, float b) { return std::min (std::max (x, a), b); }
inline float smoothstep (float x)           { x = clampf (x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }

// xorshift RNG: realtime-safe, deterministic per seed (used for round-robin variations)
struct FastRandom
{
    uint32_t s = 0x12345678u;
    void seed (uint32_t v)   { s = v != 0 ? v : 0x9e3779b9u; }
    uint32_t nextU()         { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float uni()              { return (float) (nextU() >> 8) * (1.0f / 16777216.0f); }   // [0,1)
    float bi()               { return uni() * 2.0f - 1.0f; }                               // [-1,1)
    float gauss()            { return (uni() + uni() + uni() + uni() - 2.0f) * 1.7320508f; } // ~N(0,1)
};

// RBJ biquad, allocation free
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;

    float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    void reset() noexcept { z1 = z2 = 0.0f; }

    void set (float nb0, float nb1, float nb2, float na0, float na1, float na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }
    static float w0 (float sr, float f) { return kTwoPi * std::min (f, sr * 0.49f) / sr; }

    void lowPass (float sr, float f, float q)
    {
        const float w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (float sr, float f, float q)
    {
        const float w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void peak (float sr, float f, float q, float db)
    {
        const float A = std::pow (10.0f, db / 40.0f);
        const float w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / (2.0f * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    void lowShelf (float sr, float f, float db)
    {
        const float A = std::pow (10.0f, db / 40.0f);
        const float w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / 1.41421356f, sa = 2.0f * std::sqrt (A) * al;
        set (A * ((A + 1) - (A - 1) * c + sa), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sa),
             (A + 1) + (A - 1) * c + sa, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sa);
    }
    void highShelf (float sr, float f, float db)
    {
        const float A = std::pow (10.0f, db / 40.0f);
        const float w = w0 (sr, f), c = std::cos (w), al = std::sin (w) / 1.41421356f, sa = 2.0f * std::sqrt (A) * al;
        set (A * ((A + 1) + (A - 1) * c + sa), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sa),
             (A + 1) - (A - 1) * c + sa, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sa);
    }
};
} // namespace dg
