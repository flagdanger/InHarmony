#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <array>
#include <functional>

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
    //juce::Label pitchReadout;

    juce::Label formantLabel;
    juce::Slider formantDial;

    juce::Label glidingLabel;
    juce::Slider glidingDial;

    juce::Label breathLabel;
    juce::Slider breathDial;

    juce::Label pitchGateLabel;
    juce::Slider pitchGateDial;

    juce::Label jitterLabel;
    juce::Slider jitterDial;

    juce::Label spreadLabel;
    juce::Slider spreadDial;

    juce::Label dryLabel, wetLabel, outLabel;
    juce::Slider dryFader, wetFader, outFader;

    // attachments link each slider to its parameter
    // declared after the sliders so they're destroyed first
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;

    std::unique_ptr<SliderAttachment> formantAttachment, glideAttachment, breathAttachment,
        gateAttachment, jitterAttachment, spreadAttachment,
        dryAttachment, wetAttachment, outAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalHarmonizerAudioProcessorEditor)
};
