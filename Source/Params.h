#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace pid
{
// Engine
inline constexpr const char* guitaristMode = "guitaristMode";
inline constexpr const char* realism       = "realism";
inline constexpr const char* position      = "position";
inline constexpr const char* customFret    = "customFret";
inline constexpr const char* tuning        = "tuning";
inline constexpr const char* hammerRange   = "hammerRange";
inline constexpr const char* slideRange    = "slideRange";
inline constexpr const char* strumTime     = "strumTime";
inline constexpr const char* humanize      = "humanize";
// Guitar
inline constexpr const char* pickPosition  = "pickPosition";
inline constexpr const char* pickup        = "pickup";
inline constexpr const char* palmMute      = "palmMute";
inline constexpr const char* brightness    = "brightness";
inline constexpr const char* sustain       = "sustain";
inline constexpr const char* resonance     = "resonance";
inline constexpr const char* pitchDrift    = "pitchDrift";
inline constexpr const char* intonation    = "intonation";
// Expression
inline constexpr const char* vibRate       = "vibRate";
inline constexpr const char* vibDepth      = "vibDepth";
inline constexpr const char* vibAttack     = "vibAttack";
inline constexpr const char* vibRandom     = "vibRandom";
inline constexpr const char* vibPressure   = "vibPressure";
inline constexpr const char* vibDirection  = "vibDirection";
inline constexpr const char* bendRange     = "bendRange";
inline constexpr const char* bendTime      = "bendTime";
// Noise mixer
inline constexpr const char* mixBody       = "mixBody";
inline constexpr const char* mixPick       = "mixPick";
inline constexpr const char* mixFret       = "mixFret";
inline constexpr const char* mixRelease    = "mixRelease";
inline constexpr const char* mixString     = "mixString";
inline constexpr const char* mixSlide      = "mixSlide";
inline constexpr const char* mixAmpNoise   = "mixAmpNoise";
// Amp & FX
inline constexpr const char* chainOn       = "chainOn";
inline constexpr const char* drive         = "drive";
inline constexpr const char* bass          = "bass";
inline constexpr const char* mid           = "mid";
inline constexpr const char* treble        = "treble";
inline constexpr const char* gate          = "gate";
inline constexpr const char* comp          = "comp";
inline constexpr const char* delayMix      = "delayMix";
inline constexpr const char* delayTime     = "delayTime";
inline constexpr const char* delayFeedback = "delayFeedback";
inline constexpr const char* reverbMix     = "reverbMix";
inline constexpr const char* output        = "output";
// Goa
inline constexpr const char* goaOn         = "goaOn";
inline constexpr const char* goaRate       = "goaRate";
inline constexpr const char* goaAccent     = "goaAccent";
inline constexpr const char* goaGate       = "goaGate";
inline constexpr const char* scaleLock     = "scaleLock";
inline constexpr const char* scaleRoot     = "scaleRoot";
} // namespace pid

namespace dg
{
inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto addF = [&p] (const char* id, const char* name, float lo, float hi, float step, float def, const char* unit)
    {
        p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, step), def,
                                                            AudioParameterFloatAttributes().withLabel (unit)));
    };
    auto addC = [&p] (const char* id, const char* name, const StringArray& choices, int def)
    {
        p.push_back (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, choices, def));
    };
    auto addB = [&p] (const char* id, const char* name, bool def)
    {
        p.push_back (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def));
    };

    addB (pid::guitaristMode, "Guitarist Mode", true);
    addF (pid::realism, "Realism", 0.0f, 100.0f, 1.0f, 70.0f, "%");
    addC (pid::position, "Position", { "Automatic", "Low", "Mid", "High", "Custom" }, 0);
    addF (pid::customFret, "Custom Fret", 0.0f, 19.0f, 1.0f, 7.0f, "");
    addC (pid::tuning, "Tuning", { "E Standard", "Drop D", "Eb Standard", "D Standard", "Drop C" }, 0);
    addF (pid::hammerRange, "Hammer Range", 1.0f, 5.0f, 1.0f, 3.0f, "st");
    addF (pid::slideRange, "Slide Range", 2.0f, 12.0f, 1.0f, 7.0f, "st");
    addF (pid::strumTime, "Strum Time", 0.0f, 40.0f, 0.1f, 10.0f, "ms");
    addF (pid::humanize, "Humanize", 0.0f, 100.0f, 1.0f, 35.0f, "%");

    addF (pid::pickPosition, "Pick Position", 0.0f, 1.0f, 0.01f, 0.6f, "");
    addC (pid::pickup, "Pickup", { "Neck", "Neck-Mid", "Middle", "Mid-Bridge", "Bridge" }, 4);
    addF (pid::palmMute, "Palm Mute", 0.0f, 1.0f, 0.01f, 0.0f, "");
    addF (pid::brightness, "String Tone", 0.0f, 1.0f, 0.01f, 0.55f, "");
    addF (pid::sustain, "Sustain", 0.2f, 3.0f, 0.01f, 1.0f, "x");
    addF (pid::resonance, "Resonance", 0.0f, 100.0f, 1.0f, 40.0f, "%");
    addF (pid::pitchDrift, "Pitch Drift", 0.0f, 25.0f, 0.1f, 6.0f, "ct");
    addF (pid::intonation, "Intonation", 0.0f, 100.0f, 1.0f, 30.0f, "%");

    addF (pid::vibRate, "Vib Rate", 3.0f, 9.0f, 0.01f, 5.5f, "Hz");
    addF (pid::vibDepth, "Vib Depth", 0.0f, 120.0f, 0.1f, 45.0f, "ct");
    addF (pid::vibAttack, "Vib Attack", 0.0f, 1000.0f, 1.0f, 250.0f, "ms");
    addF (pid::vibRandom, "Vib Random", 0.0f, 100.0f, 1.0f, 40.0f, "%");
    addF (pid::vibPressure, "Finger Pressure", 0.0f, 1.0f, 0.01f, 0.5f, "");
    addC (pid::vibDirection, "Vib Direction", { "Up (Finger)", "Both (Whammy)" }, 0);
    addC (pid::bendRange, "Bend Range", { "1/4 Tone", "1/2 Tone", "1 Tone", "1 1/2 Tone", "2 Tones", "3 Tones" }, 2);
    addF (pid::bendTime, "Bend Time", 40.0f, 600.0f, 1.0f, 160.0f, "ms");

    addF (pid::mixBody, "Body", 0.0f, 1.0f, 0.01f, 1.0f, "");
    addF (pid::mixPick, "Pick", 0.0f, 1.0f, 0.01f, 0.35f, "");
    addF (pid::mixFret, "Fret", 0.0f, 1.0f, 0.01f, 0.15f, "");
    addF (pid::mixRelease, "Release", 0.0f, 1.0f, 0.01f, 0.10f, "");
    addF (pid::mixString, "String", 0.0f, 1.0f, 0.01f, 0.12f, "");
    addF (pid::mixSlide, "Slide", 0.0f, 1.0f, 0.01f, 0.20f, "");
    addF (pid::mixAmpNoise, "Amp Noise", 0.0f, 1.0f, 0.01f, 0.05f, "");

    addB (pid::chainOn, "Amp Chain", true);
    addF (pid::drive, "Drive", 0.0f, 1.0f, 0.01f, 0.35f, "");
    addF (pid::bass, "Bass", 0.0f, 1.0f, 0.01f, 0.5f, "");
    addF (pid::mid, "Mid", 0.0f, 1.0f, 0.01f, 0.5f, "");
    addF (pid::treble, "Treble", 0.0f, 1.0f, 0.01f, 0.55f, "");
    addF (pid::gate, "Gate", -90.0f, -20.0f, 0.1f, -60.0f, "dB");
    addF (pid::comp, "Compressor", 0.0f, 1.0f, 0.01f, 0.3f, "");
    addF (pid::delayMix, "Delay Mix", 0.0f, 1.0f, 0.01f, 0.15f, "");
    addC (pid::delayTime, "Delay Time", { "1/4", "1/8 Dotted", "1/8", "1/16" }, 1);
    addF (pid::delayFeedback, "Delay Feedback", 0.0f, 0.9f, 0.01f, 0.35f, "");
    addF (pid::reverbMix, "Reverb", 0.0f, 1.0f, 0.01f, 0.15f, "");
    addF (pid::output, "Output", -24.0f, 6.0f, 0.1f, -6.0f, "dB");

    addB (pid::goaOn, "Goa Mode", false);
    addC (pid::goaRate, "Picking Rate", { "1/8", "1/16", "1/16 T", "1/32" }, 1);
    addC (pid::goaAccent, "Accent", { "Off", "Every 3", "Every 4", "Every 5" }, 2);
    addF (pid::goaGate, "Note Gate", 0.2f, 1.0f, 0.01f, 0.8f, "");
    addC (pid::scaleLock, "Scale Lock", { "Off", "Phrygian Dominant", "Double Harmonic", "Phrygian", "Harmonic Minor", "Natural Minor" }, 0);
    addC (pid::scaleRoot, "Scale Root", { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 4);

    return { p.begin(), p.end() };
}
} // namespace dg
