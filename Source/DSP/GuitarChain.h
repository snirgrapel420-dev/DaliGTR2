#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../Engine/DspUtil.h"
#include <vector>
#include <memory>

namespace dg
{
// Pickup -> [Preamp -> Amp (4x oversampled) -> Tone -> Amp noise -> Cab -> Gate -> Comp -> Delay -> Reverb]
// With the chain OFF the output is a clean DI (pickup only), ready for any amp sim.
class GuitarChain
{
public:
    struct Params
    {
        int   pickup = 4;
        bool  chainOn = true;
        float drive = 0.35f, bass = 0.5f, mid = 0.5f, treble = 0.55f;
        float gateDb = -60.0f, comp = 0.3f;
        float delayMix = 0.15f, delayFb = 0.35f;
        int   delayDiv = 1;
        float reverbMix = 0.15f, outDb = -6.0f, ampNoise = 0.05f;
    };

    void prepare (double sampleRate, int /*maxBlock*/)
    {
        sr = (float) sampleRate;
        os = std::make_unique<juce::dsp::Oversampling<float>> (1, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        os->initProcessing ((size_t) kChunk);
        os->reset();
        work.assign ((size_t) kChunk, 0.0f);
        tmpR.assign ((size_t) kChunk, 0.0f);

        dsize = (int) (sr * 2.5f) + 1;
        dL.assign ((size_t) dsize, 0.0f);
        dR.assign ((size_t) dsize, 0.0f);
        dw = 0; fbLp = 0.0f;

        reverb.setSampleRate (sampleRate);
        reverb.reset();

        preHP.highPass (sr, 110.0f, 0.7f);
        preMid.peak (sr, 800.0f, 0.8f, 7.0f);     // mid push before clipping = amp character, not fuzz
        cab[0].highPass (sr, 85.0f, 0.7f);
        cab[1].peak (sr, 120.0f, 1.0f, 3.0f);
        cab[2].peak (sr, 450.0f, 1.2f, -3.5f);
        cab[3].peak (sr, 2300.0f, 1.3f, 3.5f);
        cab[4].lowPass (sr, 5200.0f, 0.8f);
        cab[5].lowPass (sr, 6800.0f, 0.6f);
        for (auto& c : cab) c.reset();

        interA = 1.0f - std::exp (-kTwoPi * 6500.0f / (sr * 4.0f));
        bias0 = std::tanh (0.18f);
        gateRel  = std::exp (-1.0f / (0.05f * sr));
        gateAtk  = 1.0f - std::exp (-1.0f / (0.001f * sr));
        gateRelC = 1.0f - std::exp (-1.0f / (0.04f * sr));
        compAtk  = 1.0f - std::exp (-1.0f / (0.008f * sr));
        compRel  = 1.0f - std::exp (-1.0f / (0.12f * sr));
        lastPickup = -1; lastBass = lastMid = lastTreble = -1.0f;
        inter = gateEnv = compEnv = humPhase = 0.0f; gateGain = 1.0f; gateOpen = true;
    }

    void process (const float* in, float* L, float* R, int n, const Params& p, double bpm)
    {
        updateFilters (p);
        const float outG = dbToGain (p.outDb);
        const float preGain = dbToGain (p.drive * 36.0f);
        const float stage2 = 1.0f + p.drive * 2.0f;
        const float hiss = dbToGain (-78.0f) * p.ampNoise * 20.0f * (1.0f + p.drive * 4.0f);
        const float gateThr = dbToGain (p.gateDb);
        const float thrDb = -8.0f - p.comp * 22.0f, ratio = 1.5f + p.comp * 6.0f, makeup = p.comp * 6.0f;

        static constexpr double beats[4] = { 1.0, 0.75, 0.5, 0.25 };
        const int dS = std::clamp ((int) (beats[std::clamp (p.delayDiv, 0, 3)] * 60.0 / std::max (30.0, bpm) * sr), 1, dsize - 1);

        juce::Reverb::Parameters rp;
        rp.roomSize = 0.75f; rp.damping = 0.45f; rp.width = 1.0f; rp.freezeMode = 0.0f;
        rp.wetLevel = p.reverbMix * 0.5f;
        rp.dryLevel = 1.0f - p.reverbMix * 0.3f;
        reverb.setParameters (rp);

        for (int start = 0; start < n; start += kChunk)
        {
            const int m = std::min (kChunk, n - start);
            float* x = work.data();
            float* l = L + start;
            float* r = R != nullptr ? R + start : tmpR.data();

            for (int i = 0; i < m; ++i) x[i] = pickupF.process (in[start + i]);

            if (! p.chainOn)
            {
                for (int i = 0; i < m; ++i) { l[i] = x[i] * outG; r[i] = l[i]; }
                continue;
            }

            // amp: asymmetric two-stage saturation, 4x oversampled
            for (int i = 0; i < m; ++i) x[i] = preMid.process (preHP.process (x[i])) * preGain;
            {
                float* chans[1] = { x };
                juce::dsp::AudioBlock<float> blk (chans, 1, (size_t) m);
                auto up = os->processSamplesUp (blk);
                float* u = up.getChannelPointer (0);
                const size_t un = up.getNumSamples();
                for (size_t k = 0; k < un; ++k)
                {
                    const float s1 = std::tanh (u[k] + 0.18f) - bias0;
                    inter += (s1 - inter) * interA;
                    u[k] = std::tanh (inter * stage2) * 0.6f;
                }
                os->processSamplesDown (blk);
            }

            for (int i = 0; i < m; ++i)
            {
                float y = trebF.process (midF.process (bassF.process (x[i])));

                humPhase += 50.0f / sr;
                if (humPhase >= 1.0f) humPhase -= 1.0f;
                y += rng.bi() * hiss + std::sin (kTwoPi * humPhase) * hiss * 0.6f;

                for (auto& c : cab) y = c.process (y);

                if (p.gateDb > -89.0f)
                {
                    const float a = std::abs (y);
                    gateEnv = a > gateEnv ? a : gateEnv * gateRel;
                    if (gateEnv > gateThr) gateOpen = true;
                    else if (gateEnv < gateThr * 0.5f) gateOpen = false;
                    gateGain += ((gateOpen ? 1.0f : 0.0f) - gateGain) * (gateOpen ? gateAtk : gateRelC);
                    y *= gateGain;
                }

                if (p.comp > 0.001f)
                {
                    const float lv = std::abs (y);
                    compEnv += (lv - compEnv) * (lv > compEnv ? compAtk : compRel);
                    const float over = 20.0f * std::log10 (compEnv + 1.0e-6f) - thrDb;
                    const float gr = over > 0.0f ? over * (1.0f - 1.0f / ratio) : 0.0f;
                    y *= dbToGain (makeup - gr);
                }

                // ping-pong delay synced to host tempo
                int rpos = dw - dS;
                if (rpos < 0) rpos += dsize;
                const float dl = dL[(size_t) rpos], dr = dR[(size_t) rpos];
                fbLp += (dr - fbLp) * 0.35f;
                dL[(size_t) dw] = y + fbLp * p.delayFb;
                dR[(size_t) dw] = dl * p.delayFb;
                if (++dw >= dsize) dw = 0;

                l[i] = y + p.delayMix * dl;
                r[i] = y + p.delayMix * dr;
            }

            reverb.processStereo (l, r, m);
            for (int i = 0; i < m; ++i) { l[i] *= outG; r[i] *= outG; }
            if (R == nullptr)
                for (int i = 0; i < m; ++i) l[i] = 0.5f * (l[i] + r[i]);
        }
    }

private:
    void updateFilters (const Params& p)
    {
        if (p.pickup != lastPickup)
        {
            static constexpr float freq[5] = { 2600.0f, 3400.0f, 3600.0f, 4000.0f, 4400.0f };
            static constexpr float q[5]    = { 2.2f, 1.4f, 1.8f, 1.3f, 2.0f };
            const int i = std::clamp (p.pickup, 0, 4);
            pickupF.lowPass (sr, freq[i], q[i]);   // pickup inductance + cable capacitance resonance
            lastPickup = p.pickup;
        }
        if (p.bass != lastBass)     { bassF.lowShelf (sr, 120.0f, (p.bass - 0.5f) * 24.0f); lastBass = p.bass; }
        if (p.mid != lastMid)       { midF.peak (sr, 750.0f, 0.7f, (p.mid - 0.5f) * 20.0f); lastMid = p.mid; }
        if (p.treble != lastTreble) { trebF.highShelf (sr, 3000.0f, (p.treble - 0.5f) * 24.0f); lastTreble = p.treble; }
    }

    static constexpr int kChunk = 512;
    float sr = 44100.0f;
    std::unique_ptr<juce::dsp::Oversampling<float>> os;
    std::vector<float> work, tmpR, dL, dR;
    int dsize = 1, dw = 0;
    float fbLp = 0.0f;
    juce::Reverb reverb;
    Biquad pickupF, preHP, preMid, bassF, midF, trebF;
    Biquad cab[6];
    float interA = 0.1f, inter = 0.0f, bias0 = 0.0f;
    float gateEnv = 0.0f, gateGain = 1.0f, gateRel = 0.999f, gateAtk = 0.1f, gateRelC = 0.01f;
    bool gateOpen = true;
    float compEnv = 0.0f, compAtk = 0.01f, compRel = 0.001f;
    float humPhase = 0.0f;
    int lastPickup = -1;
    float lastBass = -1.0f, lastMid = -1.0f, lastTreble = -1.0f;
    FastRandom rng;
};
} // namespace dg
