#include "PluginEditor.h"

DaliGuitarAudioProcessorEditor::DaliGuitarAudioProcessorEditor (DaliGuitarAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), fretView (p.engine)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (fretView);

    for (auto* b : { &guitaristBtn, &chainBtn, &goaBtn }) addAndMakeVisible (b);
    guitaristAtt = std::make_unique<ButtonAtt> (proc.apvts, pid::guitaristMode, guitaristBtn);
    chainAtt     = std::make_unique<ButtonAtt> (proc.apvts, pid::chainOn, chainBtn);
    goaAtt       = std::make_unique<ButtonAtt> (proc.apvts, pid::goaOn, goaBtn);

    const juce::StringArray tabNames { "ENGINE", "GUITAR", "EXPRESSION", "NOISE", "AMP & FX", "GOA" };
    for (int i = 0; i < tabNames.size(); ++i)
    {
        auto* t = tabs.add (new juce::TextButton (tabNames[i]));
        t->onClick = [this, i] { showPage (i); };
        addAndMakeVisible (t);
    }

    auto& st = proc.apvts;
    pages.add (new ParamPage (st, { pid::realism, pid::position, pid::customFret, pid::tuning,
                                    pid::hammerRange, pid::slideRange, pid::strumTime, pid::humanize }, 4, false,
                              "Overlapping notes play legato (hammer, pull, slide). Separated notes get a new pick."));
    pages.add (new ParamPage (st, { pid::pickPosition, pid::pickup, pid::palmMute, pid::brightness,
                                    pid::sustain, pid::resonance, pid::pitchDrift, pid::intonation }, 4, false,
                              "Pick position: 0 is the neck, 1 is the bridge. CC74 moves it live."));
    pages.add (new ParamPage (st, { pid::vibRate, pid::vibDepth, pid::vibAttack, pid::vibRandom,
                                    pid::vibPressure, pid::vibDirection, pid::bendRange, pid::bendTime }, 4, false,
                              "Pitch wheel bends with a finger curve. Mod wheel and aftertouch add vibrato."));
    pages.add (new ParamPage (st, { pid::mixBody, pid::mixPick, pid::mixFret, pid::mixRelease,
                                    pid::mixString, pid::mixSlide, pid::mixAmpNoise }, 7, true,
                              "Each performance noise has its own fader."));
    pages.add (new ParamPage (st, { pid::drive, pid::bass, pid::mid, pid::treble, pid::gate, pid::comp,
                                    pid::delayMix, pid::delayTime, pid::delayFeedback, pid::reverbMix, pid::output }, 6, false,
                              "Turn Amp Chain off for a clean DI signal to use with your own amp sim."));
    pages.add (new ParamPage (st, { pid::goaRate, pid::goaAccent, pid::goaGate, pid::scaleLock, pid::scaleRoot }, 5, false,
                              "Goa Mode re-picks held notes on the host grid with alternate picking and accents."));
    for (auto* pg : pages) addChildComponent (pg);

    setSize (1200, 800);
    showPage (0);
    startTimerHz (30);
}

DaliGuitarAudioProcessorEditor::~DaliGuitarAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void DaliGuitarAudioProcessorEditor::showPage (int index)
{
    currentPage = index;
    for (int i = 0; i < pages.size(); ++i) pages[i]->setVisible (i == index);
    for (int i = 0; i < tabs.size(); ++i) tabs[i]->setToggleState (i == index, juce::dontSendNotification);
    repaint();
}

void DaliGuitarAudioProcessorEditor::timerCallback()
{
    fretView.tick();
    const int a = proc.engine.uiArtic.load();
    if (a != lastArtic) { lastArtic = a; repaint (headerArea); }
}

void DaliGuitarAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    const auto bounds = getLocalBounds().toFloat();

    g.setGradientFill (ColourGradient (neon::bg1, 0.0f, 0.0f, neon::bg0, 0.0f, bounds.getBottom(), false));
    g.fillAll();

    // faint synth grid
    g.setColour (neon::purple.withAlpha (0.035f));
    for (float x = 0.0f; x < bounds.getWidth(); x += 40.0f) g.drawVerticalLine ((int) x, 0.0f, bounds.getBottom());
    for (float y = 0.0f; y < bounds.getHeight(); y += 40.0f) g.drawHorizontalLine ((int) y, 0.0f, bounds.getRight());

    // glow behind the logo
    g.setGradientFill (ColourGradient (neon::purple.withAlpha (0.25f), 180.0f, 42.0f, Colours::transparentBlack, 520.0f, 42.0f, true));
    g.fillRect (headerArea);

    // neon logo
    GlyphArrangement ga;
    ga.addLineOfText (neon::font (40.0f, true).withExtraKerningFactor (0.06f), "DALIGUITAR", 28.0f, 52.0f);
    Path logo;
    ga.createPath (logo);
    DropShadow (neon::purple, 22, {}).drawForPath (g, logo);
    DropShadow (neon::pink.withAlpha (0.8f), 8, {}).drawForPath (g, logo);
    g.setGradientFill (ColourGradient (Colours::white, 28.0f, 20.0f, neon::pink, 320.0f, 60.0f, false));
    g.fillPath (logo);

    g.setColour (neon::dim);
    g.setFont (neon::font (12.0f, true).withExtraKerningFactor (0.2f));
    g.drawText ("GOA GUITAR ENGINE  v1.0", 30, 58, 300, 18, Justification::centredLeft);

    // articulation readout
    const auto artic = (dg::Artic) proc.engine.uiArtic.load();
    Rectangle<float> ro (360.0f, 20.0f, 250.0f, 44.0f);
    g.setColour (neon::panel.withAlpha (0.8f));
    g.fillRoundedRectangle (ro, 10.0f);
    g.setColour (neon::purple.withAlpha (0.5f));
    g.drawRoundedRectangle (ro, 10.0f, 1.0f);
    g.setColour (neon::dim);
    g.setFont (neon::font (10.0f, true).withExtraKerningFactor (0.15f));
    g.drawText ("ARTICULATION", ro.withHeight (18.0f).translated (0.0f, 3.0f), Justification::centred);
    g.setColour (neon::pink);
    g.setFont (neon::font (16.0f, true));
    g.drawText (dg::articName (artic), ro.withTrimmedTop (18.0f), Justification::centred);

    // page frame
    Path frame;
    frame.addRoundedRectangle (pageArea.toFloat().expanded (4.0f), 14.0f);
    g.setColour (neon::panel.withAlpha (0.55f));
    g.fillPath (frame);
    neon::glowStroke (g, frame, neon::purple.withAlpha (0.35f), 1.0f);

    // keyswitch legend
    g.setColour (neon::dim);
    g.setFont (neon::font (11.5f));
    g.drawFittedText ("Keyswitches (MIDI 24-35): Sustain, Staccato, Palm Mute, Dead, Harmonic, Pinch, Tremolo, Auto Bend, Pre-Bend, Slide In, Slide Out, Legato"
                      "    |    22 pick scrape, 23 fret squeak",
                      footerArea.reduced (20, 0), Justification::centred, 1);
}

void DaliGuitarAudioProcessorEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (84);

    auto toggles = headerArea.reduced (20, 22);
    goaBtn.setBounds (toggles.removeFromRight (150));
    toggles.removeFromRight (10);
    chainBtn.setBounds (toggles.removeFromRight (150));
    toggles.removeFromRight (10);
    guitaristBtn.setBounds (toggles.removeFromRight (190));

    fretView.setBounds (r.removeFromTop (250).reduced (14, 0));

    auto tabRow = r.removeFromTop (52).reduced (24, 8);
    const int tw = tabRow.getWidth() / std::max (1, tabs.size());
    for (auto* t : tabs) t->setBounds (tabRow.removeFromLeft (tw).reduced (4, 0));

    footerArea = r.removeFromBottom (34);
    pageArea = r.reduced (24, 6);
    for (auto* pg : pages) pg->setBounds (pageArea);
}
