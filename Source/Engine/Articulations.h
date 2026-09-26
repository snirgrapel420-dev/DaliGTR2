#pragma once

namespace dg
{
// Latched keyswitches: MIDI 24..35 (C1..B1 in "C3 = 60" DAWs this is C0..B0)
enum class Artic : int
{
    Sustain = 0,     // 24
    Staccato,        // 25
    PalmMute,        // 26
    DeadNote,        // 27  fret mute
    NaturalHarmonic, // 28
    PinchHarmonic,   // 29
    Tremolo,         // 30  1/32 tremolo picking on held notes
    BendUp,          // 31  automatic guitarist bend + vibrato
    PreBendRelease,  // 32
    SlideIn,         // 33
    SlideOut,        // 34
    ForceLegato,     // 35  every note hammer/pull/slide when possible
    Count
};

enum class AttackType : int { PickDown = 0, PickUp, Hammer, Pull, Slide };

constexpr int kKeyswitchLow  = 24;
constexpr int kKeyswitchHigh = 35;
constexpr int kFxScrape      = 22;   // one-shot pick scrape
constexpr int kFxSqueak      = 23;   // one-shot fret squeak

inline const char* articName (Artic a)
{
    static const char* names[] = { "SUSTAIN", "STACCATO", "PALM MUTE", "DEAD NOTE", "NAT. HARMONIC", "PINCH HARMONIC",
                                   "TREMOLO", "AUTO BEND", "PRE-BEND REL.", "SLIDE IN", "SLIDE OUT", "FORCE LEGATO" };
    const int i = (int) a;
    return (i >= 0 && i < (int) Artic::Count) ? names[i] : "?";
}

inline const char* attackTag (int t)
{
    static const char* tags[] = { "DN", "UP", "HAM", "PULL", "SLIDE" };
    return (t >= 0 && t < 5) ? tags[t] : "";
}
} // namespace dg
