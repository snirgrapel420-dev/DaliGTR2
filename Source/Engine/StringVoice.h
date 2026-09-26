#pragma once
#include "DspUtil.h"
#include "Articulations.h"
#include <vector>

namespace dg
{
// Values shared by all strings, refreshed once per block (+ live MIDI controllers)
struct VoiceGlobals
{
    float wheelBendSemis = 0.0f;   // pitch wheel target, smoothed by a guitarist "spring"
    float vibratoAmount  = 0.0f;   // mod wheel / aftertouch 0..1
    float vibRateHz      = 5.5f;
    float vibDepthCents  = 45.0f;
    float vibAttackSec   = 0.25f;
    float vibRandom      = 0.4f;
    float vibPressure    = 0.5f;
    bool  vibBoth        = false;  // false = finger vibrato (pitch only goes up)
    float bendTimeSec    = 0.16f;
    float bendRangeSemis = 2.0f;
    float brightness     = 0.5f;   // old strings .. new strings
    float sustain        = 1.0f;
    float palmMute       = 0.0f;
    float pickupBeta     = 0.09f;  // pickup position as ratio of string length (comb filter)
    float pickupComb     = 0.55f;
    float resonance      = 0.4f;
    float driftCents     = 6.0f;
};

// One physical string: extended Karplus-Strong waveguide with fractional delay,
// pick-position excitation, dynamic loop filter, legato/slide/bend/vibrato logic.
// One voice per string = monophonic per string, exactly like a real guitar.
class StringVoice
{
public:
    static constexpr int kBufSize = 8192;
    static constexpr int kMask    = kBufSize - 1;
    static constexpr int kCtrl    = 16;     // control-rate divider
    static constexpr int kMaxExc  = 4096;

    struct PickParams
    {
        int   note = 64, fret = 0;
        float velocity = 0.7f;
        bool  down = true;
        int   variation = 0;       // round-robin index (dir x layer x rr = 24 variations)
        Artic artic = Artic::Sustain;
        float pickBeta = 0.15f;    // where the pick hits the string
        float brightOffset = 0.0f;
        float centsOffset = 0.0f;
        float intonation = 0.3f;
    };

    void prepare (double sampleRate, int stringIndex, float openNote)
    {
        sr = (float) sampleRate;
        stringIdx = stringIndex;
        openMidi = openNote;
        lineA.init();
        lineB.init();
        exc.assign ((size_t) kMaxExc, 0.0f);
        rng.seed (0xA5A5A5u + (uint32_t) stringIndex * 7919u);
        stringInton = 0.35f + 0.65f * rng.uni();
        openDetune = rng.bi();
        period = sr / midiToHz (openNote);
        periodInc = 0.0f;
        sounding = keyHeld = released = false;
        note = fret = -1;
        env = 0.0f;
        envRel = std::exp (-1.0f / (0.05f * sr));
        excLen = excPos = 0;
        ctrlCount = 0;
    }

    void setOpenNote (float n) noexcept { openMidi = n; }

    bool isSounding() const noexcept       { return sounding; }
    bool isKeyHeld() const noexcept        { return sounding && keyHeld; }
    int  currentNote() const noexcept      { return note; }
    int  currentFret() const noexcept      { return fret; }
    AttackType lastAttack() const noexcept { return attackType; }
    float meter() const noexcept           { return env; }

    //==========================================================================
    void pick (const PickParams& p, const VoiceGlobals& g)
    {
        if (sounding && env > 1.0e-3f)
        {
            // the pick touching a ringing string damps it before the new attack
            lineA.scale (0.3f);
            lineB.scale (0.3f);
        }

        note = p.note; fret = p.fret; artic = p.artic;
        sounding = true; keyHeld = true; released = false;
        chokeT60 = 0.0f; time = 0.0f;
        attackType = p.down ? AttackType::PickDown : AttackType::PickUp;
        intonation = p.intonation;
        intonCents = computeIntonation();
        centsOffset = p.centsOffset;
        brightOffset = p.brightOffset;

        const float v = clampf (p.velocity, 0.0f, 1.0f);
        driftAttack  = g.driftCents * v * v * (artic == Artic::PalmMute ? 0.5f : 1.0f); // tension-modulation glide
        attackBright = 0.4f * std::pow (v, 1.5f);                                      // "TAA-aaah"
        attackEnv    = 1.0f;
        articMute    = artic == Artic::PalmMute ? 0.8f : (artic == Artic::DeadNote ? 1.0f : 0.0f);

        slideLen = 0.0f; slideOffset = 0.0f; slideFrom = slideTo = 0.0f;
        if (artic == Artic::SlideIn) { slideFrom = -4.0f; slideTo = 0.0f; slideT = 0.0f; slideLen = 0.1f; slideOffset = -4.0f; }

        autoBend = artic == Artic::PreBendRelease ? g.bendRangeSemis : 0.0f;
        autoBendVel = 0.0f;

        vibPhase = 0.0f;
        vibNoteMul = 1.0f + g.vibRandom * 0.08f * rng.bi();   // humanized vibrato per note
        newVibCycle (g);

        jumpPitch = true; ctrlCount = 0;
        const float semis = (float) note + slideOffset + autoBend + 0.01f * (intonCents + centsOffset + driftAttack);
        buildPickExcitation (sr / midiToHz (semis), p, g, v);
    }

    void legato (int newNote, int newFret, AttackType type, float velocity, float slideSec)
    {
        const bool wasSilent = ! sounding || env < 1.0e-4f;
        const int old = sounding ? note : newNote;
        note = newNote; fret = newFret; attackType = type;
        keyHeld = true; released = false; chokeT60 = 0.0f; sounding = true;
        intonCents = computeIntonation();
        const float v = clampf (velocity, 0.0f, 1.0f);

        if (type == AttackType::Slide && ! wasSilent)
        {
            slideFrom = (float) (old - newNote) + slideOffset;
            slideTo = 0.0f; slideT = 0.0f;
            slideLen = std::max (0.03f, slideSec);
            return;
        }

        slideLen = 0.0f; slideFrom = slideTo = 0.0f; slideOffset = 0.0f;
        jumpPitch = true; ctrlCount = 0;

        // small finger excitation: hammer = soft thump, pull-off = finger pluck
        const float P = sr / midiToHz ((float) newNote);
        const int L = std::clamp ((int) (P * 0.6f), 8, kMaxExc - 1);
        for (int i = 0; i < L; ++i)
        {
            const float win = std::sin (kPi * (float) i / (float) L);
            exc[(size_t) i] = type == AttackType::Hammer ? win * win : 0.6f * win + 0.4f * win * rng.bi();
        }
        excLen = L; excPos = 0;
        float gain = (type == AttackType::Hammer ? 0.18f : 0.3f) * (0.4f + 0.6f * v);
        if (wasSilent) gain *= 2.0f;   // hammer-on "from nowhere"
        excGain = type == AttackType::Pull ? -gain : gain;
        lineA.scale (0.9f);
        lineB.scale (0.9f);
    }

    void release (bool slideOut, float resonance)
    {
        keyHeld = false;
        if (! sounding || released) return;
        if (slideOut)
        {
            slideFrom = slideOffset; slideTo = slideOffset - 9.0f; slideT = 0.0f; slideLen = 0.32f;
            releaseT60 = 0.5f;
        }
        else
        {
            // fretted notes stop when the finger lifts; open strings ring (resonance)
            releaseT60 = fret == 0 ? 0.25f + 1.2f * resonance : 0.07f + 0.12f * resonance;
        }
        if (artic == Artic::PalmMute) releaseT60 *= 0.6f;
        released = true;
    }

    void choke (float t60) noexcept { chokeT60 = t60; }

    //==========================================================================
    float process (float symIn, const VoiceGlobals& g)
    {
        if (--ctrlCount < 0) { ctrlCount = kCtrl - 1; updateControl (g); }
        period += periodInc;

        float ex = 0.0f;
        if (excPos < excLen) ex = exc[(size_t) excPos++] * excGain;

        // two polarisations: A = slow horizontal plane, B = faster vertical plane, a hair sharper.
        // Their different decays + slow beating give the real "double decay" of a steel string.
        const float D  = std::max (3.0f, period - lpDelay);
        const float DB = std::max (3.0f, period * kDetuneB - lpDelay);
        const float inA = lineA.tick (D,  lpA, loopGain,  ex + symIn * symGain);
        const float inB = lineB.tick (DB, lpA, loopGainB, ex * 0.8f);

        // magnetic pickup position = comb filter tapped from each waveguide
        const float tapD = std::max (3.0f, period * g.pickupBeta) + 1.0f;
        const float out = (inA - g.pickupComb * lineA.read (tapD))
                        + kMixB * (inB - g.pickupComb * lineB.read (tapD * kDetuneB));

        const float a = std::abs (out);
        env = a > env ? a : env * envRel;

        if (sounding && released && env < 1.0e-4f && excPos >= excLen)
        {
            sounding = false; note = -1; fret = -1;
        }
        return out;
    }

private:
    float computeIntonation() const
    {
        // real necks go slightly sharp higher up + each open string is a hair off
        return intonation * (14.0f * std::pow ((float) std::max (0, fret) / 22.0f, 1.3f) * stringInton + 2.5f * openDetune);
    }

    static void spring (float& x, float& v, float target, float timeSec, float dt)
    {
        // critically damped: ease-in / ease-out like a finger bend, never linear
        const float wc = 4.7f / std::max (0.02f, timeSec);
        v += (wc * wc * (target - x) - 2.0f * wc * v) * dt;
        x += v * dt;
    }

    void newVibCycle (const VoiceGlobals& g)
    {
        vibCycleRate  = std::max (0.5f, g.vibRateHz * vibNoteMul * (1.0f + g.vibRandom * 0.18f * rng.bi()));
        vibCycleDepth = g.vibDepthCents * (1.0f + g.vibRandom * 0.3f * rng.bi());
    }

    // Guitar vibrato: every cycle gets its own rate/depth, push is faster than release,
    // finger pressure adds a "hold" at the top. Not a sine LFO.
    float vibrato (float dt, const VoiceGlobals& g, float amount)
    {
        vibPhase += dt * vibCycleRate;
        if (vibPhase >= 1.0f) { vibPhase -= 1.0f; newVibCycle (g); }
        if (amount <= 0.0005f) return 0.0f;

        const float push = 0.5f - g.vibPressure * 0.18f;
        const float hold = g.vibPressure * 0.2f;
        float shape;
        if (vibPhase < push)              shape = smoothstep (vibPhase / push);
        else if (vibPhase < push + hold)  shape = 1.0f;
        else                              shape = 1.0f - smoothstep ((vibPhase - push - hold) / std::max (0.05f, 1.0f - push - hold));
        if (g.vibBoth) shape = shape * 2.0f - 1.0f;

        const float e = g.vibAttackSec > 0.001f ? clampf (time / g.vibAttackSec, 0.0f, 1.0f) : 1.0f;
        return shape * vibCycleDepth * amount * e * e;
    }

    void updateControl (const VoiceGlobals& g)
    {
        const float dt = (float) kCtrl / sr;
        time += dt;

        spring (wheelBend, wheelVel, sounding ? g.wheelBendSemis : 0.0f, g.bendTimeSec * 0.55f, dt);

        float autoTarget = 0.0f;
        if (sounding)
        {
            if (artic == Artic::BendUp)              autoTarget = time > 0.07f ? g.bendRangeSemis : 0.0f;
            else if (artic == Artic::PreBendRelease) autoTarget = time > 0.18f ? 0.0f : g.bendRangeSemis;
        }
        spring (autoBend, autoBendVel, autoTarget, g.bendTimeSec, dt);

        if (slideLen > 0.0f)
        {
            slideT += dt;
            const float smooth = slideFrom + (slideTo - slideFrom) * smoothstep (slideT / slideLen);
            slideOffset = 0.8f * smooth + 0.2f * std::round (smooth);   // subtle fret "steps"
            if (slideT >= slideLen) { slideLen = 0.0f; slideOffset = slideTo; }
        }

        const float autoVib = (sounding && artic == Artic::BendUp && time > 0.07f + g.bendTimeSec * 1.3f) ? 0.7f : 0.0f;
        const float vib = vibrato (dt, g, std::max (g.vibratoAmount, autoVib));

        driftAttack *= std::exp (-dt / 0.07f);
        slowDrift = slowDrift * 0.995f + rng.bi() * g.driftCents * 0.02f;

        float semis = openMidi + 0.02f * openDetune;
        if (sounding)
            semis = (float) note + slideOffset + wheelBend + autoBend
                  + 0.01f * (intonCents + centsOffset + driftAttack + slowDrift * std::min (1.0f, time) + vib);

        const float f = std::min (midiToHz (semis), sr * 0.4f);
        const float target = sr / f;
        if (jumpPitch) { period = target; periodInc = 0.0f; jumpPitch = false; }
        else           periodInc = (target - period) / (float) kCtrl;

        attackEnv *= std::exp (-dt / 0.12f);
        const float pm = std::max (g.palmMute, articMute);
        const bool harmonic = artic == Artic::NaturalHarmonic || artic == Artic::PinchHarmonic;

        // Loss filter designed from two decay times: T60 of the fundamental and T60 at 4 kHz.
        // Steel strings keep their highs for a long time -> bright, long, "electric" sound.
        float T60, hiRatio;
        if (! sounding)
        {
            T60 = 0.35f + 3.0f * g.resonance;    // idle string = sympathetic resonator at open pitch
            hiRatio = 0.1f;
        }
        else
        {
            const float bright = clampf (g.brightness + brightOffset + attackBright * attackEnv, 0.0f, 1.3f);
            hiRatio = 0.05f * std::pow (12.0f, bright);          // 0.05 (old strings) .. 0.6 (new strings)
            if (harmonic) hiRatio *= 1.5f;

            if (released)                      { T60 = releaseT60; hiRatio = std::min (hiRatio, 0.3f); }
            else if (chokeT60 > 0.0f)          { T60 = chokeT60;   hiRatio = std::min (hiRatio, 0.3f); }
            else if (artic == Artic::DeadNote) { T60 = 0.03f;      hiRatio = 0.2f; }
            else
            {
                T60 = 7.0f * std::exp2 (-((float) note - 40.0f) / 24.0f) * g.sustain * (harmonic ? 1.4f : 1.0f);
                T60 *= std::exp (-4.5f * pm);                     // palm mute: ~0.2 s at full mute
                hiRatio *= std::exp (-3.0f * pm);                 // palm on the bridge kills the highs first
            }
            hiRatio = std::max (hiRatio, 0.01f);
        }
        T60 = std::max (0.01f, T60);

        const float g0 = std::pow (0.001f, 1.0f / (f * T60));             // loop gain at DC
        const float gr = std::pow (0.001f, 1.0f / (f * T60 * hiRatio));   // loop gain at 4 kHz
        const float R  = clampf (gr / g0, 0.0f, 1.0f);
        const float wr = kTwoPi * std::min (4000.0f, sr * 0.45f) / sr;
        float p = 0.0f;
        if (R < 0.9999f)
        {
            // one-pole  (1-p)/(1 - p z^-1)  with |H(wr)| = R
            const float R2 = R * R, A = 1.0f - R2, B = 1.0f - R2 * std::cos (wr);
            p = (B - std::sqrt (std::max (0.0f, B * B - A * A))) / A;
        }
        p = clampf (p, 0.0f, 0.95f);
        lpA = 1.0f - p;
        loopGain  = g0;
        loopGainB = std::pow (0.001f, 1.0f / (f * T60 * 0.35f));

        // exact phase delay of the loss filter at the fundamental -> stays in tune
        const float wv = kTwoPi * f / sr;
        lpDelay = std::atan2 (p * std::sin (wv), 1.0f - p * std::cos (wv)) / wv;
        symGain = sounding ? 0.0f : 0.004f * g.resonance;

        if (sounding && ! released && artic == Artic::Staccato && time > 0.09f)
            release (false, g.resonance);
    }

    void buildPickExcitation (float P, const PickParams& p, const VoiceGlobals& g, float v)
    {
        const int L = std::clamp ((int) std::lround (P), 8, kMaxExc - 1);
        FastRandom r;   // deterministic per variation -> real round robin, never the same attack twice in a row
        r.seed (0x51F15EEDu ^ ((uint32_t) (p.variation + 1) * 2654435761u) ^ ((uint32_t) (stringIdx + 1) * 40503u));

        const float beta = clampf (p.pickBeta * (p.down ? 1.0f : 0.9f), 0.04f, 0.5f);
        const int apex = std::max (1, (int) (beta * (float) L));
        // attack spectrum depends on velocity, not only level
        const float fc = (700.0f + 9000.0f * std::pow (v, 1.6f)) * (p.down ? 1.0f : 0.8f) * (0.6f + 0.8f * g.brightness);
        const float a = 1.0f - std::exp (-kTwoPi * std::min (fc, sr * 0.45f) / sr);

        float s = 0.0f, mean = 0.0f;
        for (int i = 0; i < L; ++i)
        {
            const float tri = i < apex ? (float) i / (float) apex : (float) (L - i) / (float) (L - apex);
            s += (r.bi() - s) * a;
            const float ph = (float) i / (float) L;
            float e;
            switch (p.artic)
            {
                case Artic::NaturalHarmonic: e = 0.8f * std::sin (kTwoPi * ph) + 0.08f * s; break;
                case Artic::PinchHarmonic:   e = 0.55f * std::sin (kTwoPi * ph) + 0.35f * std::sin (kTwoPi * 3.0f * ph) + 0.5f * s; break;
                case Artic::DeadNote:        e = i < L / 3 ? 1.5f * s : 0.0f; break;
                default:                     e = 0.75f * tri + 0.5f * s; break;
            }
            exc[(size_t) i] = e;
            mean += e;
        }
        mean /= (float) L;
        for (int i = 0; i < L; ++i) exc[(size_t) i] -= mean;

        excGain = (0.2f + 0.8f * std::pow (v, 1.4f)) * (p.down ? 1.0f : -0.92f);
        excLen = L; excPos = 0;
    }

    //==========================================================================
    float sr = 44100.0f;
    int stringIdx = 0;
    float openMidi = 40.0f;
    struct Line
    {
        std::vector<float> buf;
        int w = 0;
        float lp = 0.0f, dcX = 0.0f, dcY = 0.0f;

        void init() { buf.assign ((size_t) kBufSize, 0.0f); w = 0; lp = dcX = dcY = 0.0f; }
        void scale (float k) { for (auto& v : buf) v *= k; lp *= k; dcX *= k; dcY *= k; }

        float read (float d) const noexcept   // 4-point Hermite fractional delay
        {
            const float rp = (float) w - d;
            const int ip = (int) std::floor (rp);
            const float f = rp - (float) ip;
            const float xm1 = buf[(size_t) ((ip - 1) & kMask)];
            const float x0  = buf[(size_t) (ip & kMask)];
            const float x1  = buf[(size_t) ((ip + 1) & kMask)];
            const float x2  = buf[(size_t) ((ip + 2) & kMask)];
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        float tick (float D, float a, float g, float in) noexcept
        {
            const float x = read (D);
            lp += (x - lp) * a;                          // frequency-dependent string loss
            const float fb = lp * g;
            const float dc = fb - dcX + 0.9993f * dcY;   // DC blocker inside the loop
            dcX = fb; dcY = dc;
            float y = dc + in;
            if (std::abs (y) > 1.0f) y = (y > 0.0f ? 1.0f : -1.0f) * (1.0f + std::tanh (std::abs (y) - 1.0f));
            buf[(size_t) w] = y;
            w = (w + 1) & kMask;
            return y;
        }
    };

    static constexpr float kDetuneB = 0.99965f;   // ~0.6 cent sharper (shorter delay)
    static constexpr float kMixB    = 0.6f;

    Line lineA, lineB;
    std::vector<float> exc;

    float period = 100.0f, periodInc = 0.0f, lpDelay = 0.5f, lpA = 0.5f, loopGain = 0.99f, loopGainB = 0.98f;
    float symGain = 0.0f;
    float excGain = 0.0f;
    int excLen = 0, excPos = 0, ctrlCount = 0;
    bool jumpPitch = false;

    bool sounding = false, keyHeld = false, released = false;
    int note = -1, fret = -1;
    Artic artic = Artic::Sustain;
    AttackType attackType = AttackType::PickDown;

    float time = 0.0f, intonation = 0.3f, intonCents = 0.0f, centsOffset = 0.0f, brightOffset = 0.0f;
    float driftAttack = 0.0f, slowDrift = 0.0f, attackBright = 0.0f, attackEnv = 0.0f, articMute = 0.0f;
    float slideFrom = 0.0f, slideTo = 0.0f, slideT = 0.0f, slideLen = 0.0f, slideOffset = 0.0f;
    float wheelBend = 0.0f, wheelVel = 0.0f, autoBend = 0.0f, autoBendVel = 0.0f;
    float vibPhase = 0.0f, vibCycleRate = 5.5f, vibCycleDepth = 40.0f, vibNoteMul = 1.0f;
    float releaseT60 = 0.08f, chokeT60 = 0.0f;
    float env = 0.0f, envRel = 0.999f;
    float stringInton = 0.5f, openDetune = 0.0f;
    FastRandom rng;
};
} // namespace dg
