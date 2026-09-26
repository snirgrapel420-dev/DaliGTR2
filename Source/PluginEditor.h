#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/NeonLookAndFeel.h"
#include "UI/ParamPage.h"
#include "UI/FretboardView.h"

class DaliGuitarAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DaliGuitarAudioProcessorEditor (DaliGuitarAudioProcessor&);
    ~DaliGuitarAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void showPage (int index);

    DaliGuitarAudioProcessor& proc;
    NeonLookAndFeel laf;

    FretboardView fretView;
    juce::ToggleButton guitaristBtn { "GUITARIST MODE" }, chainBtn { "AMP CHAIN" }, goaBtn { "GOA MODE" };
    juce::OwnedArray<juce::TextButton> tabs;
    juce::OwnedArray<ParamPage> pages;

    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAtt> guitaristAtt, chainAtt, goaAtt;

    juce::Rectangle<int> headerArea, pageArea, footerArea;
    int currentPage = 0;
    int lastArtic = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DaliGuitarAudioProcessorEditor)
};
