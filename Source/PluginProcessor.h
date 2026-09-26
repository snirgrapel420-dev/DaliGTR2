#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Engine/GuitarEngine.h"
#include "DSP/GuitarChain.h"

class DaliGuitarAudioProcessor final : public juce::AudioProcessor
{
public:
    DaliGuitarAudioProcessor();
    ~DaliGuitarAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "DaliGuitar"; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    dg::GuitarEngine engine;

private:
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }
    int   geti (const char* id) const { return juce::roundToInt (get (id)); }

    dg::GuitarChain chain;
    std::vector<float> mono;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DaliGuitarAudioProcessor)
};
