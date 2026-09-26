#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Fretboard.h"
#include "StringVoice.h"
#include "NoiseLayer.h"
#include <array>
#include <atomic>
#include <algorithm>

namespace dg
{
struct EngineSettings
{
    bool  guitaristMode = true;
    float realism = 0.7f;        // 0 = MIDI instrument .. 1 = aggressive guitarist simulation
    int   positionPref = 0;
    float customFret = 7.0f;
    int   tuning = 0;
    float hammerRange = 3.0f;    // semitones
    float slideRange = 7.0f;
    float strumMs = 10.0f;
    float humanize = 0.35f;
    float pickPosition = 0.6f;   // 0 neck .. 1 bridge
    float intonation = 0.3f;
    bool  goaOn = false;
    int   goaRate = 1;
    int   goaAccent = 2;
    float goaGate = 0.8f;
    int   scale = 0;
    int   scaleRoot = 4;
    float bodyMix = 1.0f;
    std::array<float, 6> noiseMix { 0.35f, 0.15f, 0.10f, 0.12f, 0.20f, 0.35f }; // Pick Fret Release String Slide Scrape
};

//==============================================================================
// Correlated humanization: one latent "intensity" (AR(1), has memory) drives timing,
// velocity, brightness and pick position together, the way a player does.
class Humanizer
{
public:
    struct Out { float delayMs = 0.0f, velMul = 1.0f, bright = 0.0f, beta = 0.0f, cents = 0.0f; };

    Out next (float amount)
    {
        Out o;
        latent = 0.72f * latent + 0.7f * rng.gauss();
        if (amount <= 0.001f) return o;
        const float e = clampf (latent, -2.5f, 2.5f) / 2.5f;
        const float j = rng.gauss() * 0.25f;
        o.delayMs = clampf (amount * (4.0f - 3.0f * e + 2.0f * j), 0.0f, 8.0f);  // harder notes land earlier
        o.velMul  = 1.0f + amount * 0.09f * e;
        o.bright  = amount * 0.12f * e;
        o.beta    = amount * 0.025f * (e + j);
        o.cents   = amount * 2.5f * (0.5f * e + j);
        return o;
    }

private:
    float latent = 0.0f;
    FastRandom rng;
};

// 4 round robins x 2 directions x 3 layers = 24 attack variations per note, never repeating back-to-back
class RoundRobin
{
public:
    int next (int dir, int layer)
    {
        int& last = lastIdx[dir & 1][std::clamp (layer, 0, 2)];
        last = (last + 1 + (int) (rng.uni() * 3.0f)) % 4;
        return last;
    }

private:
    int lastIdx[2][3] {};
    FastRandom rng;
};

//==============================================================================
// MIDI note -> virtual guitarist -> string -> fret -> pick/legato -> articulation -> voice
class GuitarEngine
{
public:
    struct UiString
    {
        std::atomic<int> fret { -1 }, note { -1 }, attack { 0 };
        std::atomic<float> level { 0.0f };
    };
    std::array<UiString, kNumStrings> ui;
    std::atomic<float> uiHand { 5.0f };
    std::atomic<int> uiArtic { 0 };

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        currentTuning = settings.tuning;
        board.setTuning (currentTuning);
        for (int s = 0; s < kNumStrings; ++s)
            voices[(size_t) s].prepare (sampleRate, s, (float) board.openNote (s));
        noise.prepare (sampleRate);
        reset();
    }

    void reset()
    {
        queueCount = 0; heldCount = 0; pendingCount = 0;
        noteMap.fill (-1);
        noteDelay.fill (0);
        for (int s = 0; s < kNumStrings; ++s)
        {
            logicalNote[(size_t) s] = -1;
            logicalHeld[(size_t) s] = false;
            rep[(size_t) s].on = false;
            lastVoiceOut[(size_t) s] = 0.0f;
        }
        lastString = -1; lastNote = -1; handPos = 5.0f; symBus = 0.0f;
    }

    void setSettings (const EngineSettings& s, const VoiceGlobals& g)
    {
        if (s.tuning != currentTuning)
        {
            currentTuning = s.tuning;
            board.setTuning (currentTuning);
            for (int k = 0; k < kNumStrings; ++k) voices[(size_t) k].setOpenNote ((float) board.openNote (k));
        }
        settings = s;
        globals = g;
        globals.wheelBendSemis = wheel * globals.bendRangeSemis;
        globals.vibratoAmount = std::max (modWheel, pressure);
    }

    void render (float* out, int numSamples, const juce::MidiBuffer& midi, double bpm, double ppqStart, bool hostPlaying)
    {
        samplesPerBeat = (double) sr * 60.0 / juce::jlimit (30.0, 400.0, bpm);
        const bool synced = hostPlaying && ppqStart >= 0.0;
        auto it = midi.cbegin();
        const auto end = midi.cend();

        for (int i = 0; i < numSamples; ++i)
        {
            curPpq = synced ? ppqStart + (double) i / samplesPerBeat : -1.0;

            // note-offs are handled immediately, note-ons of the same sample are grouped (chords)
            while (it != end && (*it).samplePosition <= i)
            {
                handleMessage ((*it).getMessage());
                ++it;
            }
            if (pendingCount > 0) flushNoteOns();
            runScheduler();
            runRepeaters();

            float body = 0.0f;
            for (int s = 0; s < kNumStrings; ++s)
            {
                const float o = voices[(size_t) s].process (symBus - lastVoiceOut[(size_t) s], globals);
                lastVoiceOut[(size_t) s] = o;
                body += o;
            }
            symBus = body;
            out[i] = body * 0.22f * settings.bodyMix + noise.process (settings.noiseMix) * 0.6f;
            ++now;
        }

        while (it != end) { handleMessage ((*it).getMessage()); ++it; }
        if (pendingCount > 0) flushNoteOns();
        publishUi();
    }

private:
    //==========================================================================
    struct PendingOn { int note = 0; float vel = 0.0f; };

    struct Sched
    {
        enum Kind { Pick, Legato, Release } kind = Pick;
        int64_t due = 0;
        int string = 0, note = 0, fret = 0, variation = 0;
        float vel = 0.7f, slideSec = 0.05f, brightOff = 0.0f, betaOff = 0.0f, cents = 0.0f;
        bool down = true, slideOut = false;
        Artic artic = Artic::Sustain;
        AttackType type = AttackType::Hammer;
    };

    struct Repeater
    {
        bool on = false, tremolo = false, down = true;
        int count = 0;
        float baseVel = 0.7f;
        Artic artic = Artic::Sustain;
        double nextDue = 0.0, step = 0.0;
        int64_t chokeDue = -1;
    };

    static constexpr int kMaxPending = 16;
    static constexpr int kMaxHeld = 32;

    //==========================================================================
    void handleMessage (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (n >= kKeyswitchLow && n <= kKeyswitchHigh)
            {
                artic = (Artic) (n - kKeyswitchLow);
                uiArtic.store (n - kKeyswitchLow);
                return;
            }
            if (n == kFxScrape) { noise.trigger (NoiseType::Scrape, 0.9f * m.getFloatVelocity(), 5200.0f, 900.0f, 0.55f, 0.01f, 3.5f); return; }
            if (n == kFxSqueak) { noise.trigger (NoiseType::Fret, 0.8f * m.getFloatVelocity(), 1200.0f, 3600.0f, 0.13f, 0.004f, 5.0f); return; }
            if (pendingCount < kMaxPending) pending[(size_t) pendingCount++] = { n, m.getFloatVelocity() };
        }
        else if (m.isNoteOff())
        {
            const int n = m.getNoteNumber();
            if (n >= kKeyswitchLow && n <= kKeyswitchHigh) return;
            noteOff (n);
        }
        else if (m.isPitchWheel())
        {
            wheel = (float) (m.getPitchWheelValue() - 8192) / 8192.0f;
            globals.wheelBendSemis = wheel * globals.bendRangeSemis;
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            allNotesOff();
        }
        else if (m.isController())
        {
            const int cc = m.getControllerNumber();
            const float v = (float) m.getControllerValue() / 127.0f;
            if (cc == 1)  modWheel = v;
            if (cc == 74) ccPick = v;
            globals.vibratoAmount = std::max (modWheel, pressure);
        }
        else if (m.isChannelPressure())
        {
            pressure = (float) m.getChannelPressureValue() / 127.0f;
            globals.vibratoAmount = std::max (modWheel, pressure);
        }
        else if (m.isAftertouch())
        {
            pressure = (float) m.getAfterTouchValue() / 127.0f;
            globals.vibratoAmount = std::max (modWheel, pressure);
        }
    }

    void flushNoteOns()
    {
        const int count = pendingCount;
        pendingCount = 0;
        if (count > 1 && settings.guitaristMode) playChord (count);
        else for (int k = 0; k < count; ++k) playSingle (pending[(size_t) k].note, pending[(size_t) k].vel);
    }

    //==========================================================================
    int quantize (int n) const
    {
        static constexpr int scales[5][7] = {
            { 0, 1, 4, 5, 7, 8, 10 },  // Phrygian dominant (Freygish / Hijaz)
            { 0, 1, 4, 5, 7, 8, 11 },  // Double harmonic (Hijaz Kar)
            { 0, 1, 3, 5, 7, 8, 10 },  // Phrygian
            { 0, 2, 3, 5, 7, 8, 11 },  // Harmonic minor
            { 0, 2, 3, 5, 7, 8, 10 },  // Natural minor
        };
        if (settings.scale <= 0 || settings.scale > 5) return n;
        const int* sc = scales[settings.scale - 1];
        const int rel = ((n - settings.scaleRoot) % 12 + 12) % 12;
        int best = rel, bestDist = 99;
        for (int d = 0; d < 7; ++d)
            for (int oct = -12; oct <= 12; oct += 12)
            {
                const int cand = sc[d] + oct;
                const int dist = std::abs (cand - rel);
                if (dist < bestDist || (dist == bestDist && cand < best)) { bestDist = dist; best = cand; }
            }
        return n + (best - rel);
    }

    int fitRange (int n) const
    {
        while (n > board.highestNote()) n -= 12;
        while (n < board.lowestNote())  n += 12;
        return n;
    }

    unsigned heldMask() const
    {
        unsigned m = 0;
        for (int s = 0; s < kNumStrings; ++s)
            if (logicalHeld[(size_t) s] && logicalNote[(size_t) s] >= 0) m |= 1u << s;
        return m;
    }

    void pushHeld (int note)
    {
        removeHeld (note);
        if (heldCount < kMaxHeld) held[(size_t) heldCount++] = note;
    }

    void removeHeld (int note)
    {
        for (int k = 0; k < heldCount; ++k)
            if (held[(size_t) k] == note)
            {
                for (int j = k; j < heldCount - 1; ++j) held[(size_t) j] = held[(size_t) (j + 1)];
                --heldCount;
                return;
            }
    }

    void moveHand (int f, bool sliding, float realism)
    {
        if (f <= 0) return;
        const float d = (float) f - handPos;
        if (std::abs (d) > 2.5f)
        {
            // position shift: fingers squeak along the wound strings
            if (! sliding && realism > 0.15f)
            {
                const float amp = std::min (1.0f, std::abs (d) / 9.0f) * realism;
                noise.trigger (NoiseType::Fret, amp, d > 0 ? 1300.0f : 3300.0f, d > 0 ? 3300.0f : 1300.0f,
                               0.045f + 0.008f * std::abs (d), 0.004f, 4.0f);
            }
            handPos = (float) f;
        }
        else
        {
            handPos += 0.25f * d;
        }
    }

    //==========================================================================
    void playSingle (int inNote, float vel)
    {
        if (noteMap[(size_t) inNote] >= 0) noteOff (inNote);

        const int note = fitRange (quantize (inNote));
        noteMap[(size_t) inNote] = note;
        pushHeld (note);

        const auto h = humanizer.next (settings.humanize);
        const int delay = (int) (h.delayMs * 0.001f * sr);
        noteDelay[(size_t) inNote] = delay;
        vel = clampf (vel * h.velMul, 0.02f, 1.0f);

        const double gap = (double) (now - lastOnTime) / (double) sr;
        lastOnTime = now;
        const bool gm = settings.guitaristMode;
        const float realism = gm ? settings.realism : 0.0f;

        if (gm && tryLegato (note, vel, gap, delay, realism)) return;

        const unsigned busy = heldMask();
        int prefer = -1;
        if (realism > 0.5f && lastString >= 0 && (busy & (1u << lastString)) == 0) prefer = lastString;

        const FretPos fp = board.choose (note, handPos, settings.positionPref, settings.customFret, busy, prefer);
        if (! fp.valid())
        {
            noteMap[(size_t) inNote] = -1;
            removeHeld (note);
            return;
        }
        if (gm) moveHand (fp.fret, false, realism);

        // alternate picking; a new phrase starts with a downstroke
        bool down = true;
        if (realism >= 0.1f) down = gap > 0.3 ? true : ! lastDown;
        lastDown = down;

        schedulePick (fp.string, note, fp.fret, down ? vel : vel * 0.93f, down, h, now + delay, artic);
        logicalNote[(size_t) fp.string] = note;
        logicalHeld[(size_t) fp.string] = true;
        lastString = fp.string;
        lastNote = note;
    }

    // Legato intelligence: small interval -> hammer/pull, bigger -> slide, huge -> new pick
    bool tryLegato (int note, float vel, double gap, int delay, float realism)
    {
        if (realism < 0.2f || lastString < 0 || lastNote < 0) return false;
        if (! (artic == Artic::Sustain || artic == Artic::ForceLegato)) return false;

        const auto ls = (size_t) lastString;
        const bool prevHeld = logicalHeld[ls] && logicalNote[ls] == lastNote;
        const bool forced = artic == Artic::ForceLegato && gap < 0.6;
        if (! prevHeld && ! forced) return false;

        const int interval = note - lastNote;
        const int f = board.fretFor (lastString, note);
        if (interval == 0 || f < 0 || vel > 0.93f) return false;   // very hard notes get picked

        const int ai = std::abs (interval);
        const bool reach = f == 0 || std::abs ((float) f - handPos) <= 4.5f;
        AttackType type;
        if (ai <= (int) settings.hammerRange && reach)                    type = interval > 0 ? AttackType::Hammer : AttackType::Pull;
        else if (ai <= (int) settings.slideRange && realism >= 0.45f && f > 0) type = AttackType::Slide;
        else return false;

        Sched e;
        e.kind = Sched::Legato; e.due = now + delay; e.string = lastString; e.note = note; e.fret = f;
        e.vel = vel; e.type = type;
        e.slideSec = (0.035f + 0.012f * (float) ai) * (1.3f - 0.6f * vel);
        schedule (e);

        logicalNote[ls] = note;
        logicalHeld[ls] = true;
        lastNote = note;
        if (f > 0) moveHand (f, type == AttackType::Slide, realism);
        return true;
    }

    void playChord (int count)
    {
        struct CN { int in = 0, note = 0; float vel = 0.0f; FretPos fp; };
        std::array<CN, kMaxPending> cn {};
        int m = 0;
        for (int k = 0; k < count; ++k)
        {
            const int in = pending[(size_t) k].note;
            if (noteMap[(size_t) in] >= 0) noteOff (in);
            const int note = fitRange (quantize (in));
            bool dup = false;
            for (int j = 0; j < m; ++j) if (cn[(size_t) j].note == note) dup = true;
            if (dup) continue;
            cn[(size_t) m].in = in; cn[(size_t) m].note = note; cn[(size_t) m].vel = pending[(size_t) k].vel;
            ++m;
        }
        std::sort (cn.begin(), cn.begin() + m, [] (const CN& a, const CN& b) { return a.note < b.note; });

        unsigned busy = heldMask();
        int minFret = 99;
        float avgVel = 0.0f;
        std::array<int, kMaxPending> order {};
        int valid = 0;
        for (int k = 0; k < m; ++k)
        {
            auto& c = cn[(size_t) k];
            c.fp = board.choose (c.note, handPos, settings.positionPref, settings.customFret, busy, -1);
            if (! c.fp.valid()) continue;
            busy |= 1u << c.fp.string;
            if (c.fp.fret > 0) minFret = std::min (minFret, c.fp.fret);
            avgVel += c.vel;
            order[(size_t) valid++] = k;
        }
        if (valid == 0) return;
        avgVel /= (float) valid;

        const double gap = (double) (now - lastChordTime) / (double) sr;
        lastChordTime = now; lastOnTime = now;
        const bool down = gap > 0.4 ? true : ! lastStrumDown;
        lastStrumDown = down;

        // strum: down = low string first, up = high string first
        std::sort (order.begin(), order.begin() + valid, [&cn, down] (int a, int b)
        {
            return down ? cn[(size_t) a].fp.string < cn[(size_t) b].fp.string
                        : cn[(size_t) a].fp.string > cn[(size_t) b].fp.string;
        });

        const auto h = humanizer.next (settings.humanize);
        const int base = (int) (h.delayMs * 0.001f * sr);
        const float step = settings.strumMs * (1.25f - 0.5f * avgVel) * 0.001f * sr;
        int topString = -1, topNote = -1;

        for (int idx = 0; idx < valid; ++idx)
        {
            const auto& c = cn[(size_t) order[(size_t) idx]];
            const int delay = base + (int) (step * (float) idx);
            const float v = clampf (c.vel * h.velMul * (1.0f - 0.03f * (float) idx) * (down ? 1.0f : 0.9f), 0.02f, 1.0f);
            noteMap[(size_t) c.in] = c.note;
            noteDelay[(size_t) c.in] = delay;
            pushHeld (c.note);
            schedulePick (c.fp.string, c.note, c.fp.fret, v, down, h, now + delay, artic);
            logicalNote[(size_t) c.fp.string] = c.note;
            logicalHeld[(size_t) c.fp.string] = true;
            if (c.fp.string > topString) { topString = c.fp.string; topNote = c.note; }
        }
        if (minFret < 99) moveHand (minFret, false, settings.realism);
        lastString = topString;
        lastNote = topNote;
    }

    void noteOff (int inNote)
    {
        // zero-length note waiting in this very sample: drop it
        for (int k = 0; k < pendingCount; ++k)
            if (pending[(size_t) k].note == inNote)
            {
                pending[(size_t) k] = pending[(size_t) (--pendingCount)];
                return;
            }

        const int note = noteMap[(size_t) inNote];
        if (note < 0) return;
        noteMap[(size_t) inNote] = -1;
        removeHeld (note);

        int s = -1;
        for (int k = 0; k < kNumStrings; ++k)
            if (logicalHeld[(size_t) k] && logicalNote[(size_t) k] == note) { s = k; break; }
        if (s < 0) return;   // this key's string was already taken over by a legato note

        const int64_t due = now + noteDelay[(size_t) inNote];

        // trills: releasing the top note while the lower one is still held -> pull-off back to it
        if (settings.guitaristMode && settings.realism >= 0.2f && heldCount > 0
            && (artic == Artic::Sustain || artic == Artic::ForceLegato))
        {
            const int back = held[(size_t) (heldCount - 1)];
            bool backPlaying = false;
            for (int k = 0; k < kNumStrings; ++k)
                if (logicalHeld[(size_t) k] && logicalNote[(size_t) k] == back) backPlaying = true;
            const int f = board.fretFor (s, back);
            const int iv = back - note;
            if (! backPlaying && f >= 0 && iv != 0 && std::abs (iv) <= (int) settings.hammerRange)
            {
                Sched e;
                e.kind = Sched::Legato; e.due = due; e.string = s; e.note = back; e.fret = f; e.vel = 0.6f;
                e.type = iv > 0 ? AttackType::Hammer : AttackType::Pull;
                schedule (e);
                logicalNote[(size_t) s] = back;
                lastNote = back; lastString = s;
                return;
            }
        }

        logicalHeld[(size_t) s] = false;
        logicalNote[(size_t) s] = -1;
        Sched e;
        e.kind = Sched::Release; e.due = due; e.string = s; e.note = note; e.slideOut = artic == Artic::SlideOut;
        schedule (e);
    }

    void allNotesOff()
    {
        for (auto& v : voices) v.release (false, globals.resonance);
        heldCount = 0; queueCount = 0; pendingCount = 0;
        noteMap.fill (-1);
        for (int s = 0; s < kNumStrings; ++s)
        {
            logicalHeld[(size_t) s] = false;
            logicalNote[(size_t) s] = -1;
            rep[(size_t) s].on = false;
        }
    }

    //==========================================================================
    void schedulePick (int string, int note, int fret, float vel, bool down, const Humanizer::Out& h, int64_t due, Artic a)
    {
        const int layer = vel < 0.4f ? 0 : (vel < 0.78f ? 1 : 2);
        const int rr = robin.next (down ? 0 : 1, layer);
        Sched e;
        e.kind = Sched::Pick; e.due = due; e.string = string; e.note = note; e.fret = fret; e.vel = vel; e.down = down;
        e.variation = ((down ? 0 : 1) * 3 + layer) * 4 + rr;
        e.artic = a; e.brightOff = h.bright; e.betaOff = h.beta; e.cents = h.cents;
        schedule (e);
    }

    void schedule (const Sched& e)
    {
        if (queueCount < (int) queue.size()) queue[(size_t) queueCount++] = e;
        else execute (e);
    }

    void runScheduler()
    {
        int k = 0;
        while (k < queueCount)
        {
            if (queue[(size_t) k].due <= now)
            {
                const Sched e = queue[(size_t) k];
                for (int j = k; j < queueCount - 1; ++j) queue[(size_t) j] = queue[(size_t) (j + 1)];
                --queueCount;
                execute (e);
            }
            else ++k;
        }
    }

    void execute (const Sched& e)
    {
        auto& v = voices[(size_t) e.string];
        switch (e.kind)
        {
            case Sched::Pick:
                executePick (e, false);
                break;

            case Sched::Legato:
            {
                const bool up = e.note > v.currentNote();
                v.legato (e.note, e.fret, e.type, e.vel, e.slideSec);
                const float r = settings.realism;
                if (e.type == AttackType::Slide)
                    noise.trigger (NoiseType::Slide, 0.7f, up ? 1400.0f : 3400.0f, up ? 3400.0f : 1400.0f, e.slideSec + 0.02f, 0.008f, 3.0f);
                else if (e.type == AttackType::Hammer)
                    noise.trigger (NoiseType::Fret, 0.35f * e.vel * r, 900.0f, 1300.0f, 0.02f, 0.0008f, 1.5f);
                else
                    noise.trigger (NoiseType::String, 0.35f * e.vel * r, 2600.0f, 1500.0f, 0.03f, 0.001f, 1.2f);
                break;
            }

            case Sched::Release:
                if (v.isKeyHeld() && v.currentNote() == e.note)
                {
                    v.release (e.slideOut, globals.resonance);
                    noise.trigger (NoiseType::Release, 0.5f, 260.0f, 160.0f, 0.045f, 0.002f, 1.0f);
                    if (e.slideOut)
                        noise.trigger (NoiseType::Slide, 0.6f, 3200.0f, 1200.0f, 0.33f, 0.01f, 3.0f);
                    else if (rng.uni() < 0.25f * settings.realism)
                        noise.trigger (NoiseType::String, 0.2f, 2000.0f, 2800.0f, 0.05f, 0.004f, 2.0f);
                }
                break;
        }
    }

    void executePick (const Sched& e, bool fromRepeater)
    {
        auto& v = voices[(size_t) e.string];
        StringVoice::PickParams p;
        p.note = e.note; p.fret = e.fret; p.velocity = e.vel; p.down = e.down;
        p.variation = e.variation; p.artic = e.artic;
        const float pickPos = ccPick >= 0.0f ? ccPick : settings.pickPosition;
        p.pickBeta = clampf (0.34f - 0.27f * pickPos + e.betaOff, 0.05f, 0.45f);
        p.brightOffset = e.brightOff; p.centsOffset = e.cents; p.intonation = settings.intonation;
        v.pick (p, globals);

        const float vv = e.vel;
        noise.trigger (NoiseType::Pick, 0.15f + 0.85f * vv * vv, 2200.0f + 4000.0f * vv, 1500.0f + 1500.0f * vv,
                       0.006f + 0.01f * vv, 0.0004f, 0.9f);
        if (! fromRepeater)
            noise.trigger (NoiseType::String, 0.2f * vv, 1800.0f, 2600.0f, 0.03f, 0.002f, 1.4f);
        if (e.artic == Artic::DeadNote)
            noise.trigger (NoiseType::String, 0.6f * vv, 1100.0f, 700.0f, 0.05f, 0.001f, 0.8f);

        if (! fromRepeater && (settings.goaOn || e.artic == Artic::Tremolo)) startRepeater (e);
    }

    //==========================================================================
    // Goa engine / tremolo picking: re-picks held notes on the host grid with alternate picking + accents
    double stepBeats (bool tremolo) const
    {
        static constexpr double t[4] = { 0.5, 0.25, 1.0 / 6.0, 0.125 };
        return tremolo ? 0.125 : t[std::clamp (settings.goaRate, 0, 3)];
    }

    void startRepeater (const Sched& e)
    {
        auto& r = rep[(size_t) e.string];
        r.on = true;
        r.tremolo = ! settings.goaOn;
        r.down = e.down;
        r.count = 1;
        r.baseVel = e.vel;
        r.artic = e.artic == Artic::Tremolo ? Artic::Sustain : e.artic;
        const double beats = stepBeats (r.tremolo);
        r.step = beats * samplesPerBeat;
        double toNext = r.step;
        if (curPpq >= 0.0)
        {
            const double k = std::floor (curPpq / beats + 1.0e-6) + 1.0;
            toNext = (k * beats - curPpq) * samplesPerBeat;
            if (toNext < 0.35 * r.step) toNext += r.step;
        }
        r.nextDue = (double) now + toNext;
        r.chokeDue = (! r.tremolo && settings.goaGate < 0.98f) ? now + (int64_t) (settings.goaGate * r.step) : -1;
    }

    void runRepeaters()
    {
        for (int s = 0; s < kNumStrings; ++s)
        {
            auto& r = rep[(size_t) s];
            if (! r.on) continue;
            auto& v = voices[(size_t) s];
            if (! v.isKeyHeld()) { r.on = false; continue; }

            if (r.chokeDue >= 0 && now >= r.chokeDue) { v.choke (0.035f); r.chokeDue = -1; }
            if ((double) now < r.nextDue) continue;

            r.down = ! r.down;
            const int every = settings.goaAccent == 0 ? 0 : settings.goaAccent + 2;   // 3 / 4 / 5
            const float accent = every > 0 ? ((r.count % every) == 0 ? 1.0f : 0.68f) : (r.down ? 0.95f : 0.85f);
            const float vel = clampf (r.baseVel * accent * (1.0f + settings.humanize * 0.12f * rng.bi()), 0.05f, 1.0f);

            const auto h = humanizer.next (settings.humanize * 0.5f);
            const int layer = vel < 0.4f ? 0 : (vel < 0.78f ? 1 : 2);
            Sched e;
            e.kind = Sched::Pick; e.string = s; e.note = v.currentNote(); e.fret = v.currentFret();
            e.vel = vel; e.down = r.down; e.artic = r.artic;
            e.variation = ((r.down ? 0 : 1) * 3 + layer) * 4 + robin.next (r.down ? 0 : 1, layer);
            e.brightOff = h.bright; e.betaOff = h.beta; e.cents = h.cents * 0.5f;
            executePick (e, true);

            ++r.count;
            r.step = stepBeats (r.tremolo) * samplesPerBeat;
            r.nextDue += r.step;
            if ((double) now >= r.nextDue) r.nextDue = (double) now + r.step;
            r.chokeDue = (! r.tremolo && settings.goaGate < 0.98f) ? now + (int64_t) (settings.goaGate * r.step) : -1;
        }
    }

    void publishUi()
    {
        for (int s = 0; s < kNumStrings; ++s)
        {
            const auto& v = voices[(size_t) s];
            const bool on = v.isSounding() && v.currentNote() >= 0;
            ui[(size_t) s].fret.store (on ? v.currentFret() : -1);
            ui[(size_t) s].note.store (on ? v.currentNote() : -1);
            ui[(size_t) s].attack.store ((int) v.lastAttack());
            ui[(size_t) s].level.store (v.meter());
        }
        uiHand.store (handPos);
    }

    //==========================================================================
    float sr = 44100.0f;
    int64_t now = 0, lastOnTime = -1000000, lastChordTime = -1000000;
    double samplesPerBeat = 22050.0, curPpq = -1.0;

    EngineSettings settings;
    VoiceGlobals globals;
    Fretboard board;
    int currentTuning = 0;
    std::array<StringVoice, kNumStrings> voices;
    std::array<float, kNumStrings> lastVoiceOut {};
    float symBus = 0.0f;
    NoiseLayer noise;
    Humanizer humanizer;
    RoundRobin robin;
    FastRandom rng;

    Artic artic = Artic::Sustain;
    float wheel = 0.0f, modWheel = 0.0f, pressure = 0.0f, ccPick = -1.0f;

    std::array<PendingOn, kMaxPending> pending {};
    int pendingCount = 0;
    std::array<Sched, 256> queue {};
    int queueCount = 0;

    std::array<int, 128> noteMap {}, noteDelay {};
    std::array<int, kMaxHeld> held {};
    int heldCount = 0;

    std::array<int, kNumStrings> logicalNote {};
    std::array<bool, kNumStrings> logicalHeld {};
    std::array<Repeater, kNumStrings> rep {};

    int lastString = -1, lastNote = -1;
    float handPos = 5.0f;
    bool lastDown = false, lastStrumDown = false;
};
} // namespace dg
