#include "PluginProcessor.h"
#include "PluginEditor.h"

DaliGuitarAudioProcessor::DaliGuitarAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "DaliGuitarState", dg::createParameterLayout())
{
}

void DaliGuitarAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate);
    chain.prepare (sampleRate, samplesPerBlock);
    mono.assign ((size_t) std::max (samplesPerBlock, 512), 0.0f);
}

bool DaliGuitarAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void DaliGuitarAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n == 0) return;
    if ((int) mono.size() < n) mono.resize ((size_t) n);

    // ---- engine settings -----------------------------------------------------
    dg::EngineSettings s;
    s.guitaristMode = get (pid::guitaristMode) > 0.5f;
    s.realism       = get (pid::realism) * 0.01f;
    s.positionPref  = geti (pid::position);
    s.customFret    = get (pid::customFret);
    s.tuning        = geti (pid::tuning);
    s.hammerRange   = get (pid::hammerRange);
    s.slideRange    = get (pid::slideRange);
    s.strumMs       = get (pid::strumTime);
    s.humanize      = get (pid::humanize) * 0.01f;
    s.pickPosition  = get (pid::pickPosition);
    s.intonation    = get (pid::intonation) * 0.01f;
    s.goaOn         = get (pid::goaOn) > 0.5f;
    s.goaRate       = geti (pid::goaRate);
    s.goaAccent     = geti (pid::goaAccent);
    s.goaGate       = get (pid::goaGate);
    s.scale         = geti (pid::scaleLock);
    s.scaleRoot     = geti (pid::scaleRoot);
    s.bodyMix       = get (pid::mixBody);
    s.noiseMix      = { get (pid::mixPick), get (pid::mixFret), get (pid::mixRelease),
                        get (pid::mixString), get (pid::mixSlide), get (pid::mixPick) };

    static constexpr float bendSemis[6]   = { 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 6.0f };
    static constexpr float pickupBeta[5]  = { 0.26f, 0.20f, 0.17f, 0.13f, 0.09f };
    const int pickup = juce::jlimit (0, 4, geti (pid::pickup));

    dg::VoiceGlobals g;
    g.vibRateHz      = get (pid::vibRate);
    g.vibDepthCents  = get (pid::vibDepth);
    g.vibAttackSec   = get (pid::vibAttack) * 0.001f;
    g.vibRandom      = get (pid::vibRandom) * 0.01f;
    g.vibPressure    = get (pid::vibPressure);
    g.vibBoth        = geti (pid::vibDirection) == 1;
    g.bendRangeSemis = bendSemis[juce::jlimit (0, 5, geti (pid::bendRange))];
    g.bendTimeSec    = get (pid::bendTime) * 0.001f;
    g.brightness     = get (pid::brightness);
    g.sustain        = get (pid::sustain);
    g.palmMute       = get (pid::palmMute);
    g.pickupBeta     = pickupBeta[pickup];
    g.pickupComb     = 0.55f;
    g.resonance      = get (pid::resonance) * 0.01f;
    g.driftCents     = get (pid::pitchDrift);
    engine.setSettings (s, g);

    // ---- host tempo ------------------------------------------------------------
    double bpm = 145.0, ppq = -1.0;   // Goa default when the host gives no tempo
    bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            if (auto q = pos->getPpqPosition()) ppq = *q;
            playing = pos->getIsPlaying();
        }

    engine.render (mono.data(), n, midi, bpm, ppq, playing);

    // ---- sound chain -------------------------------------------------------------
    dg::GuitarChain::Params cp;
    cp.pickup    = pickup;
    cp.chainOn   = get (pid::chainOn) > 0.5f;
    cp.drive     = get (pid::drive);
    cp.bass      = get (pid::bass);
    cp.mid       = get (pid::mid);
    cp.treble    = get (pid::treble);
    cp.gateDb    = get (pid::gate);
    cp.comp      = get (pid::comp);
    cp.delayMix  = get (pid::delayMix);
    cp.delayFb   = get (pid::delayFeedback);
    cp.delayDiv  = geti (pid::delayTime);
    cp.reverbMix = get (pid::reverbMix);
    cp.outDb     = get (pid::output);
    cp.ampNoise  = get (pid::mixAmpNoise);

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    chain.process (mono.data(), L, R, n, cp, bpm);
}

juce::AudioProcessorEditor* DaliGuitarAudioProcessor::createEditor()
{
    return new DaliGuitarAudioProcessorEditor (*this);
}

void DaliGuitarAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void DaliGuitarAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DaliGuitarAudioProcessor();
}
