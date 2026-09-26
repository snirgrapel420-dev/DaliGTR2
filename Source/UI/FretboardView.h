#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "NeonLookAndFeel.h"
#include "../Engine/GuitarEngine.h"

// Shows what the virtual guitarist is doing: string, fret, pick direction / legato type, hand position
class FretboardView : public juce::Component
{
public:
    explicit FretboardView (dg::GuitarEngine& e) : engine (e) {}

    void tick() { phase += 0.9f; repaint(); }

    void paint (juce::Graphics& g) override
    {
        using namespace juce;
        auto b = getLocalBounds().toFloat().reduced (6.0f);
        Path frame;
        frame.addRoundedRectangle (b, 16.0f);
        g.setColour (Colour (0xff0d0619));
        g.fillPath (frame);
        neon::glowStroke (g, frame, neon::purple.withAlpha (0.55f), 1.2f);

        auto inner = b.reduced (22.0f, 28.0f);
        auto head = inner.removeFromLeft (34.0f);
        const auto neck = inner;

        ColourGradient wood (Colour (0xff1d0e33), neck.getX(), neck.getY(), Colour (0xff0a0414), neck.getX(), neck.getBottom(), false);
        g.setGradientFill (wood);
        g.fillRect (neck);

        // inlays
        g.setColour (neon::purple.withAlpha (0.22f));
        for (int f : { 3, 5, 7, 9, 15, 17, 19, 21 })
            g.fillEllipse (fretMid (f, neck) - 5.0f, neck.getCentreY() - 5.0f, 10.0f, 10.0f);
        for (float fy : { 0.33f, 0.67f })
            g.fillEllipse (fretMid (12, neck) - 5.0f, neck.getY() + neck.getHeight() * fy - 5.0f, 10.0f, 10.0f);

        // fretting-hand window
        const float hp = engine.uiHand.load();
        if (hp >= 1.0f)
        {
            const float x0 = fretX (std::max (0.0f, hp - 1.5f), neck);
            const float x1 = fretX (std::min (22.0f, hp + 2.5f), neck);
            Rectangle<float> hr (x0, neck.getY() - 6.0f, x1 - x0, neck.getHeight() + 12.0f);
            g.setColour (neon::purple.withAlpha (0.10f));
            g.fillRoundedRectangle (hr, 8.0f);
            g.setColour (neon::purple.withAlpha (0.45f));
            g.drawRoundedRectangle (hr, 8.0f, 1.0f);
            g.setColour (neon::dim);
            g.setFont (neon::font (10.0f, true));
            g.drawText ("HAND", hr.withHeight (14.0f).translated (0.0f, -16.0f), Justification::centred);
        }

        // nut + frets
        g.setColour (neon::text.withAlpha (0.85f));
        g.fillRect (neck.getX() - 3.0f, neck.getY(), 4.0f, neck.getHeight());
        g.setColour (neon::violet.withAlpha (0.55f));
        for (int f = 1; f <= dg::kNumFrets; ++f)
        {
            const float x = fretX ((float) f, neck);
            g.drawLine (x, neck.getY(), x, neck.getBottom(), 1.6f);
        }
        g.setFont (neon::font (10.0f));
        g.setColour (neon::dim);
        for (int f : { 3, 5, 7, 9, 12, 15, 17, 19, 21 })
            g.drawText (String (f), Rectangle<float> (fretMid (f, neck) - 12.0f, neck.getBottom() + 4.0f, 24.0f, 14.0f), Justification::centred);

        // strings (low E at the bottom, like tab)
        for (int s = 0; s < dg::kNumStrings; ++s)
        {
            const float y = stringY (s, neck);
            const float thick = 2.6f - 0.32f * (float) s;
            const int fret = engine.ui[(size_t) s].fret.load();
            const float level = engine.ui[(size_t) s].level.load();
            const bool active = fret >= 0;
            const float xs = active ? (fret == 0 ? neck.getX() : fretX ((float) fret, neck)) : neck.getRight();

            g.setColour (Colour (0xffb9a6d9).withAlpha (0.5f));
            g.drawLine (head.getX(), y, xs, y, thick);

            if (active)
            {
                Path vib;
                const float amp = std::min (7.0f, level * 60.0f) * std::sin (phase * (1.3f + 0.25f * (float) s));
                const int N = 48;
                for (int k = 0; k <= N; ++k)
                {
                    const float t = (float) k / (float) N;
                    const float x = xs + (neck.getRight() - xs) * t;
                    const float yy = y + amp * std::sin (MathConstants<float>::pi * t);
                    if (k == 0) vib.startNewSubPath (x, yy); else vib.lineTo (x, yy);
                }
                neon::glowStroke (g, vib, neon::pink.withAlpha (juce::jlimit (0.35f, 1.0f, 0.35f + level * 8.0f)), thick * 0.8f);
            }
        }

        // note markers
        for (int s = 0; s < dg::kNumStrings; ++s)
        {
            const int fret = engine.ui[(size_t) s].fret.load();
            if (fret < 0) continue;
            const int note = engine.ui[(size_t) s].note.load();
            const int attack = engine.ui[(size_t) s].attack.load();
            const float cx = fret == 0 ? head.getCentreX() : fretMid (fret, neck);
            const float cy = stringY (s, neck);

            for (int i = 3; i >= 1; --i)
            {
                const float rr = 9.0f + (float) i * 4.0f;
                g.setColour (neon::pink.withAlpha (0.07f * (float) (4 - i)));
                g.fillEllipse (cx - rr, cy - rr, rr * 2.0f, rr * 2.0f);
            }
            g.setColour (neon::purple);
            g.fillEllipse (cx - 9.0f, cy - 9.0f, 18.0f, 18.0f);
            g.setColour (Colours::white.withAlpha (0.9f));
            g.drawEllipse (cx - 9.0f, cy - 9.0f, 18.0f, 18.0f, 1.2f);
            g.setFont (neon::font (9.5f, true));
            g.drawText (String (fret), Rectangle<float> (cx - 9.0f, cy - 9.0f, 18.0f, 18.0f), Justification::centred);

            const String tag = MidiMessage::getMidiNoteName (note, true, true, 4) + " " + dg::attackTag (attack);
            g.setFont (neon::font (10.5f, true));
            g.setColour (neon::text);
            g.drawText (tag, Rectangle<float> (cx + 12.0f, cy - 17.0f, 80.0f, 14.0f), Justification::centredLeft);
        }

        // string names
        static const char* names[] = { "6", "5", "4", "3", "2", "1" };
        g.setFont (neon::font (10.0f, true));
        g.setColour (neon::dim);
        for (int s = 0; s < dg::kNumStrings; ++s)
            g.drawText (names[s], Rectangle<float> (b.getX() + 4.0f, stringY (s, neck) - 7.0f, 14.0f, 14.0f), Justification::centred);
    }

private:
    static float fretX (float f, juce::Rectangle<float> neck)
    {
        const float full = 1.0f - std::pow (2.0f, -22.0f / 12.0f);
        return neck.getX() + neck.getWidth() * (1.0f - std::pow (2.0f, -f / 12.0f)) / full;   // real fret spacing
    }
    static float fretMid (int f, juce::Rectangle<float> neck)
    {
        return 0.5f * (fretX ((float) (f - 1), neck) + fretX ((float) f, neck));
    }
    static float stringY (int s, juce::Rectangle<float> neck)
    {
        return neck.getBottom() - ((float) s + 0.5f) * neck.getHeight() / (float) dg::kNumStrings;
    }

    dg::GuitarEngine& engine;
    float phase = 0.0f;
};
