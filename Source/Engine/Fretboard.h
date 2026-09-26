#pragma once
#include "DspUtil.h"

namespace dg
{
constexpr int kNumStrings = 6;
constexpr int kNumFrets   = 22;

struct FretPos
{
    int string = -1;   // 0 = low E (string 6) ... 5 = high E (string 1)
    int fret   = -1;
    bool valid() const noexcept { return string >= 0; }
};

// The String/Fret engine: for every note it scores every possible (string, fret)
// and picks the one a real guitarist would most likely use.
class Fretboard
{
public:
    enum Pref { Automatic = 0, Low, Mid, High, Custom };

    void setTuning (int index)
    {
        static constexpr int t[5][kNumStrings] = {
            { 40, 45, 50, 55, 59, 64 },   // E standard
            { 38, 45, 50, 55, 59, 64 },   // Drop D
            { 39, 44, 49, 54, 58, 63 },   // Eb standard
            { 38, 43, 48, 53, 57, 62 },   // D standard
            { 36, 43, 48, 53, 57, 62 },   // Drop C
        };
        const int i = std::clamp (index, 0, 4);
        for (int s = 0; s < kNumStrings; ++s) open[s] = t[i][s];
    }

    int openNote (int s) const noexcept  { return open[s]; }
    int lowestNote() const noexcept      { return open[0]; }
    int highestNote() const noexcept     { return open[kNumStrings - 1] + kNumFrets; }

    int fretFor (int s, int note) const noexcept
    {
        const int f = note - open[s];
        return (f >= 0 && f <= kNumFrets) ? f : -1;
    }

    FretPos choose (int note, float handPos, int pref, float customFret, unsigned busyMask, int preferString) const
    {
        float target = handPos;
        switch (pref)
        {
            case Low:    target = 2.0f;  break;
            case Mid:    target = 7.0f;  break;
            case High:   target = 14.0f; break;
            case Custom: target = customFret; break;
            default: break;
        }

        FretPos best;
        float bestCost = 1.0e9f;

        for (int s = 0; s < kNumStrings; ++s)
        {
            const int f = fretFor (s, note);
            if (f < 0) continue;

            float cost = 0.0f;
            if (busyMask & (1u << s)) cost += 1000.0f;   // string already ringing a held note

            if (f == 0)
            {
                // open strings are free for the hand but belong to low positions
                cost += (target > 9.0f ? 5.0f : 1.2f) + (pref == High ? 4.0f : 0.0f);
            }
            else
            {
                const float d = std::abs ((float) f - target);
                cost += d <= 2.5f ? d * 0.4f : 1.0f + (d - 2.5f) * 1.1f;  // inside the 4-fret hand box is cheap
                cost += (float) f * 0.04f;
                if (s <= 1 && f > 12) cost += (float) (f - 12) * 0.35f;     // muddy high frets on wound strings
            }

            if (pref == Automatic) cost += (float) f * 0.08f;   // mild pull toward lower positions
            if (s == preferString) cost -= 0.8f;                // linear playing on the same string

            if (cost < bestCost) { bestCost = cost; best = { s, f }; }
        }
        return best;
    }

private:
    int open[kNumStrings] { 40, 45, 50, 55, 59, 64 };
};
} // namespace dg
