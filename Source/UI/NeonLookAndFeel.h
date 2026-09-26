#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace neon
{
inline const juce::Colour bg0    { 0xff07030f };
inline const juce::Colour bg1    { 0xff140726 };
inline const juce::Colour panel  { 0xff170a2b };
inline const juce::Colour purple { 0xffb026ff };
inline const juce::Colour pink   { 0xffff3df2 };
inline const juce::Colour violet { 0xff7b2cff };
inline const juce::Colour text   { 0xffeadcff };
inline const juce::Colour dim    { 0xff8b72b3 };
inline const juce::Colour track  { 0xff2a1545 };

inline juce::Font font (float h, bool bold = false)
{
    return juce::Font (juce::FontOptions (h, bold ? juce::Font::bold : juce::Font::plain));
}

// Neon tube stroke: wide faint halo -> bright core -> white hot centre
inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float core)
{
    using PST = juce::PathStrokeType;
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (c.withMultipliedAlpha (0.05f + 0.05f * (float) i));
        g.strokePath (p, PST (core + (float) (4 - i) * 3.0f, PST::curved, PST::rounded));
    }
    g.setColour (c);
    g.strokePath (p, PST (core, PST::curved, PST::rounded));
    g.setColour (juce::Colours::white.withAlpha (0.55f * c.getFloatAlpha()));
    g.strokePath (p, PST (core * 0.4f, PST::curved, PST::rounded));
}
} // namespace neon

class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel()
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, neon::bg0);
        setColour (Slider::textBoxTextColourId, neon::text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour (Slider::textBoxHighlightColourId, neon::purple.withAlpha (0.4f));
        setColour (Label::textColourId, neon::text);
        setColour (ComboBox::backgroundColourId, neon::panel);
        setColour (ComboBox::textColourId, neon::text);
        setColour (ComboBox::outlineColourId, neon::purple);
        setColour (ComboBox::arrowColourId, neon::pink);
        setColour (PopupMenu::backgroundColourId, Colour (0xff12071f));
        setColour (PopupMenu::textColourId, neon::text);
        setColour (PopupMenu::highlightedBackgroundColourId, neon::purple.withAlpha (0.35f));
        setColour (PopupMenu::highlightedTextColourId, Colours::white);
        setColour (TextButton::textColourOffId, neon::dim);
        setColour (TextButton::textColourOnId, neon::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        using namespace juce;
        auto bounds = Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (10.0f);
        const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);

        Path trackPath;
        trackPath.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, endAngle, true);
        g.setColour (neon::track);
        g.strokePath (trackPath, PathStrokeType (4.0f, PathStrokeType::curved, PathStrokeType::rounded));

        if (pos > 0.001f)
        {
            Path arc;
            arc.addCentredArc (c.x, c.y, r, r, 0.0f, startAngle, angle, true);
            neon::glowStroke (g, arc, neon::purple, 3.5f);
        }

        const float kr = r * 0.68f;
        ColourGradient grad (Colour (0xff2c1452), c.x, c.y - kr, Colour (0xff0b0416), c.x, c.y + kr, false);
        g.setGradientFill (grad);
        g.fillEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f);
        g.setColour (neon::purple.withAlpha (0.45f));
        g.drawEllipse (c.x - kr, c.y - kr, kr * 2.0f, kr * 2.0f, 1.2f);

        Path ptr;
        ptr.startNewSubPath (c.x + std::sin (angle) * kr * 0.25f, c.y - std::cos (angle) * kr * 0.25f);
        ptr.lineTo (c.x + std::sin (angle) * kr * 0.85f, c.y - std::cos (angle) * kr * 0.85f);
        neon::glowStroke (g, ptr, neon::pink, 2.2f);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minPos, float maxPos, const juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        using namespace juce;
        if (! slider.isVertical())
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
            return;
        }
        auto b = Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        const float cx = b.getCentreX();

        Path tr;
        tr.startNewSubPath (cx, b.getY() + 4.0f);
        tr.lineTo (cx, b.getBottom() - 4.0f);
        g.setColour (neon::track);
        g.strokePath (tr, PathStrokeType (6.0f, PathStrokeType::curved, PathStrokeType::rounded));

        Path val;
        val.startNewSubPath (cx, b.getBottom() - 4.0f);
        val.lineTo (cx, sliderPos);
        neon::glowStroke (g, val, neon::purple, 4.0f);

        Rectangle<float> thumb (cx - 15.0f, sliderPos - 5.0f, 30.0f, 10.0f);
        g.setColour (Colour (0xff1e0c38));
        g.fillRoundedRectangle (thumb, 4.0f);
        Path tp;
        tp.addRoundedRectangle (thumb, 4.0f);
        neon::glowStroke (g, tp, neon::pink, 1.4f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool) override
    {
        using namespace juce;
        auto b = button.getLocalBounds().toFloat().reduced (3.0f);
        const bool on = button.getToggleState();
        const float rr = b.getHeight() * 0.5f;

        g.setColour (on ? neon::purple.withAlpha (0.25f) : neon::panel);
        g.fillRoundedRectangle (b, rr);
        Path p;
        p.addRoundedRectangle (b, rr);
        if (on) neon::glowStroke (g, p, neon::purple, 1.6f);
        else
        {
            g.setColour (neon::dim.withAlpha (highlighted ? 0.8f : 0.45f));
            g.strokePath (p, PathStrokeType (1.2f));
        }

        const float d = b.getHeight() * 0.34f;
        Rectangle<float> led (b.getX() + rr * 0.7f, b.getCentreY() - d * 0.5f, d, d);
        if (on)
        {
            g.setColour (neon::pink.withAlpha (0.25f));
            g.fillEllipse (led.expanded (4.0f));
        }
        g.setColour (on ? neon::pink : neon::track);
        g.fillEllipse (led);

        g.setColour (on ? neon::text : neon::dim);
        g.setFont (neon::font (13.0f, true));
        g.drawFittedText (button.getButtonText(), b.withTrimmedLeft (rr * 0.7f + d + 8.0f).toNearestInt(),
                          Justification::centredLeft, 1);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box) override
    {
        using namespace juce;
        auto b = Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (1.0f);
        g.setColour (neon::panel);
        g.fillRoundedRectangle (b, 6.0f);
        g.setColour (neon::purple.withAlpha (box.isMouseOver (true) ? 0.95f : 0.6f));
        g.drawRoundedRectangle (b, 6.0f, 1.3f);
        Path arrow;
        const float ax = (float) width - 16.0f, ay = (float) height * 0.5f;
        arrow.addTriangle (ax - 5.0f, ay - 3.0f, ax + 5.0f, ay - 3.0f, ax, ay + 3.0f);
        g.setColour (neon::pink);
        g.fillPath (arrow);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&, bool highlighted, bool) override
    {
        using namespace juce;
        auto b = button.getLocalBounds().toFloat().reduced (2.0f);
        const bool on = button.getToggleState();
        g.setColour (on ? neon::purple.withAlpha (0.22f) : neon::panel.withAlpha (0.7f));
        g.fillRoundedRectangle (b, 8.0f);
        Path p;
        p.addRoundedRectangle (b, 8.0f);
        if (on) neon::glowStroke (g, p, neon::purple, 1.5f);
        else
        {
            g.setColour (highlighted ? neon::purple.withAlpha (0.6f) : neon::track);
            g.strokePath (p, PathStrokeType (1.2f));
        }
    }

    juce::Font getTextButtonFont (juce::TextButton&, int) override { return neon::font (14.0f, true).withExtraKerningFactor (0.08f); }
    juce::Font getComboBoxFont (juce::ComboBox&) override        { return neon::font (13.0f, true); }
    juce::Font getPopupMenuFont() override                        { return neon::font (14.0f); }
    juce::Font getLabelFont (juce::Label&) override               { return neon::font (12.5f, true); }
};
