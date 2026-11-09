#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"

// draws the dials, faders and gate handle
class HarmonyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HarmonyLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, ui::paper);
        setColour (juce::Label::textColourId, ui::ink);
        setColour (juce::Slider::rotarySliderFillColourId, ui::dialArc);
        setColour (juce::Slider::textBoxTextColourId, ui::subtle);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, ui::accent.withAlpha (0.25f));
        setColour (juce::TextEditor::backgroundColourId, ui::panel);
        setColour (juce::Label::backgroundWhenEditingColourId, ui::panel);
        setColour (juce::Label::textWhenEditingColourId, ui::ink);
        setColour (juce::Label::outlineWhenEditingColourId, ui::ink);
        setColour (juce::TextEditor::textColourId, ui::ink);
        setColour (juce::TextEditor::highlightedTextColourId, ui::ink);
        setColour (juce::TextEditor::focusedOutlineColourId, ui::ink);
        setColour (juce::CaretComponent::caretColourId, ui::ink);
        setColour (juce::PopupMenu::backgroundColourId, ui::panel);
        setColour (juce::PopupMenu::textColourId, ui::ink);
    }

    // every label (including slider value boxes) uses Comic Sans at its own size
    juce::Font getLabelFont (juce::Label& label) override
    {
        const auto current = label.getFont();
        return ui::font (current.getHeight(), current.isBold());
    }

    juce::Font getPopupMenuFont() override { return ui::font (15.0f); }

    // light track with a dark arc up to the value
    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const auto size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto lineWidth = juce::jmax (3.0f, size * 0.07f);
        const auto radius = size * 0.5f - lineWidth * 0.5f;
        const auto centre = bounds.getCentre();
        const auto stroke = juce::PathStrokeType (lineWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (ui::dialTrack);
        g.strokePath (track, stroke);

        if (sliderPos > 0.001f)
        {
            const auto toAngle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

            juce::Path value;
            value.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, rotaryStartAngle, toAngle, true);
            g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
            g.strokePath (value, stroke);
        }
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                           juce::Slider& slider) override
    {
        // input gate: only a line and a small handle, the meter underneath is painted by InputMeter
        if (slider.getProperties().contains ("gateHandle"))
        {
            // same style as the fader thumbs, turned upright: the tab sits just above the bar with a
            // 3 px gap, and the line runs from the tab down across the bar, stopping at its bottom wall
            // (InputMeter draws the 12 px bar centred on this slider)
            const auto barCentreY = (float) y + (float) height * 0.5f;
            const auto barTop = barCentreY - 6.0f;
            const auto barBottom = barCentreY + 6.0f;
            const auto tabCentreY = barTop - 7.0f;

            g.setColour (ui::ink);
            g.fillRect (juce::Rectangle<float> (sliderPos - 1.0f, tabCentreY, 2.0f, barBottom - tabCentreY));
            g.fillRoundedRectangle (juce::Rectangle<float> (12.0f, 8.0f).withCentre ({ sliderPos, tabCentreY }), 2.0f);
            return;
        }

        // limiter ceiling: red line across the bar, tab on the right (mirrors the gain thumb)
        if (slider.getProperties().contains("ceilingHandle"))
        {
            constexpr float barWidth = 12.0f;
            const auto barX = (float)x + (float)width * 0.5f - barWidth * 0.5f;

            g.setColour(ui::clip);
            g.fillRect(juce::Rectangle<float>(barX, sliderPos - 1.0f, barWidth + 7.0f, 2.0f));
            g.fillRoundedRectangle(juce::Rectangle<float>(8.0f, 12.0f).withCentre({ barX + barWidth + 7.0f, sliderPos }), 2.0f);
            return;
        }

        if (style != juce::Slider::LinearVertical)
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        // the bar and meter are painted by FaderStrip underneath, the fader only draws its thumb:
        // a line from the left tab across the bar to its right wall
        constexpr float barWidth = 12.0f;
        const auto barX = (float)x + (float)width * 0.5f - barWidth * 0.5f;

        g.setColour(ui::ink);
        g.fillRect(juce::Rectangle<float>(barX - 7.0f, sliderPos - 1.0f, barWidth + 7.0f, 2.0f));
        g.fillRoundedRectangle(juce::Rectangle<float>(8.0f, 12.0f).withCentre({ barX - 7.0f, sliderPos }), 2.0f);
    }

    int getSliderThumbRadius (juce::Slider& slider) override
    {
        if (slider.getProperties().contains ("gateHandle"))
            return 6;

        return slider.isVertical() ? 6 : LookAndFeel_V4::getSliderThumbRadius (slider);
    }
};
