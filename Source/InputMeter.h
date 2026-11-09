#pragma once

#include <JuceHeader.h>
#include "UiStyle.h"

// input level bar with the pitch gate threshold as a draggable handle on top
// the level is RMS in dB, the same measure the detector's gate uses
class InputMeter : public juce::Component
{
public:
    explicit InputMeter (juce::AudioProcessorValueTreeState& state)
    {
        gate.setSliderStyle (juce::Slider::LinearHorizontal);
        gate.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        gate.getProperties().set ("gateHandle", true); // HarmonyLookAndFeel draws it as a thin handle
        gate.onValueChange = [this] { repaint(); };
        addAndMakeVisible (gate);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, "gate", gate);

        if (auto* parameter = state.getParameter ("gate"))
            gate.setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
    }

    void setLevelDb (float newLevelDb)
    {
        if (std::abs (newLevelDb - levelDb) > 0.2f)
        {
            levelDb = newLevelDb;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (ui::ink);
        g.setFont (ui::font (15.0f));
        g.drawText ("Input", labelArea, juce::Justification::centredLeft);

        // bar spans the gate slider's range so meter and handle share one dB scale
        const auto minX = positionOf (gate.getMinimum());
        const auto maxX = positionOf (gate.getMaximum());
        const auto bar = juce::Rectangle<float> (minX, (float) getHeight() * 0.5f - 6.0f, maxX - minX, 12.0f);

        const auto gateDb = gate.getValue();
        const auto gateX = positionOf (gateDb);
        const auto levelX = positionOf (juce::jlimit (gate.getMinimum(), gate.getMaximum(), (double) levelDb));

        g.setColour (ui::panel);
        g.fillRect (bar);

        // grey below the gate, blue above it (detector is listening)
        g.setColour (ui::meterBelowGate);
        g.fillRect (bar.withRight (juce::jmin (levelX, gateX)));

        if (levelX > gateX)
        {
            g.setColour (ui::meterFill);
            g.fillRect (bar.withLeft (gateX).withRight (levelX));
        }

        g.setColour (ui::ink);
        g.drawRect (bar, ui::outline);

        g.drawText ("gate " + juce::String (juce::roundToInt (gateDb)) + " dB", valueArea, juce::Justification::centredRight);
    }

    void resized() override
    {
        auto area = getLocalBounds();
        labelArea = area.removeFromLeft (56);
        valueArea = area.removeFromRight (110);
        // tall enough to fit the tab above the bar (bar centre to tab top is 17 px)
        gate.setBounds (area.withSizeKeepingCentre (area.getWidth() - 12, juce::jmin (36, area.getHeight())));
    }

private:
    float positionOf (double db) { return (float) gate.getX() + gate.getPositionOfValue (db); }

    juce::Slider gate;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    juce::Rectangle<int> labelArea, valueArea;
    float levelDb = -100.0f;
};
