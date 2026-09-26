#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "NeonLookAndFeel.h"

// Builds knobs / faders / toggles / menus automatically from parameter IDs
class ParamPage : public juce::Component
{
public:
    ParamPage (juce::AudioProcessorValueTreeState& state, const juce::StringArray& ids, int cols,
               bool faders = false, const juce::String& note = {})
        : apvts (state), columns (std::max (1, cols)), useFaders (faders), footnote (note)
    {
        for (auto& id : ids) addControl (id);
    }

    void paint (juce::Graphics& g) override
    {
        if (footnote.isEmpty()) return;
        g.setColour (neon::dim);
        g.setFont (neon::font (12.5f));
        g.drawFittedText (footnote, getLocalBounds().removeFromBottom (26).reduced (20, 0), juce::Justification::centred, 1);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16, 8);
        if (footnote.isNotEmpty()) area.removeFromBottom (26);
        const int n = (int) cells.size();
        if (n == 0) return;
        const int rows = (n + columns - 1) / columns;
        const int cw = area.getWidth() / columns;
        const int ch = std::min (area.getHeight() / rows, useFaders ? 320 : 170);

        for (int k = 0; k < n; ++k)
        {
            const int r = k / columns, c = k % columns;
            auto cell = juce::Rectangle<int> (area.getX() + c * cw, area.getY() + r * ch, cw, ch).reduced (6);
            auto& cl = cells[(size_t) k];
            cl.label->setBounds (cell.removeFromTop (20));
            if (cl.kind == 2)      cl.comp->setBounds (cell.withSizeKeepingCentre (std::min (cell.getWidth(), 160), 30));
            else if (cl.kind == 1) cl.comp->setBounds (cell.withSizeKeepingCentre (std::min (cell.getWidth(), 140), 36));
            else                   cl.comp->setBounds (cell);
        }
    }

private:
    struct Cell
    {
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::Component> comp;
        int kind = 0;   // 0 slider, 1 toggle, 2 combo
    };

    void addControl (const juce::String& id)
    {
        using APVTS = juce::AudioProcessorValueTreeState;
        auto* param = apvts.getParameter (id);
        if (param == nullptr) return;

        Cell c;
        c.label = std::make_unique<juce::Label> (juce::String(), param->getName (40).toUpperCase());
        c.label->setJustificationType (juce::Justification::centred);
        c.label->setFont (neon::font (12.0f, true).withExtraKerningFactor (0.06f));
        c.label->setColour (juce::Label::textColourId, neon::dim);

        if (dynamic_cast<juce::AudioParameterBool*> (param) != nullptr)
        {
            auto t = std::make_unique<juce::ToggleButton> ("ON");
            buttonAtt.push_back (std::make_unique<APVTS::ButtonAttachment> (apvts, id, *t));
            c.kind = 1;
            c.comp = std::move (t);
        }
        else if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (param))
        {
            auto cb = std::make_unique<juce::ComboBox>();
            cb->addItemList (choice->choices, 1);
            comboAtt.push_back (std::make_unique<APVTS::ComboBoxAttachment> (apvts, id, *cb));
            c.kind = 2;
            c.comp = std::move (cb);
        }
        else
        {
            auto s = std::make_unique<juce::Slider> (useFaders ? juce::Slider::LinearVertical
                                                               : juce::Slider::RotaryHorizontalVerticalDrag,
                                                     juce::Slider::TextBoxBelow);
            s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 18);
            sliderAtt.push_back (std::make_unique<APVTS::SliderAttachment> (apvts, id, *s));
            const auto unit = param->getLabel();
            if (unit.isNotEmpty()) s->setTextValueSuffix (" " + unit);
            c.kind = 0;
            c.comp = std::move (s);
        }

        addAndMakeVisible (*c.label);
        addAndMakeVisible (*c.comp);
        cells.push_back (std::move (c));
    }

    juce::AudioProcessorValueTreeState& apvts;
    int columns;
    bool useFaders;
    juce::String footnote;

    std::vector<Cell> cells;   // declared before attachments -> attachments are destroyed first
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliderAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttonAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAtt;
};
