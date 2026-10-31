#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>

//==============================================================================
/**
*/
class VocalHarmonizerAudioProcessorEditor  : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    VocalHarmonizerAudioProcessorEditor (VocalHarmonizerAudioProcessor&);
    ~VocalHarmonizerAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    VocalHarmonizerAudioProcessor& audioProcessor;
    juce::Label pitchReadout;

    juce::Label latchingLabel;
    juce::Slider latchingDial;

    juce::Label pitchGateLabel;
    juce::Slider pitchGateSlider;

    juce::Label wetMixLabel;
    juce::Slider wetMixSlider;

    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard;
    std::array<bool, 128> displayedMidiNotes{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalHarmonizerAudioProcessorEditor)
};
