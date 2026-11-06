#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================================
VocalHarmonizerAudioProcessorEditor::VocalHarmonizerAudioProcessorEditor (VocalHarmonizerAudioProcessor& p)
    : AudioProcessorEditor (&p), 
    audioProcessor (p)
{
    // detected pitch
    //pitchReadout.setJustificationType(juce::Justification::centred);
    //pitchReadout.setFont(juce::Font(juce::FontOptions(22.0f)));
    //pitchReadout.setText("Waiting for pitched input",
        //juce::dontSendNotification);
    //addAndMakeVisible(pitchReadout);

    // formant tracking
    formantLabel.setText("Formant", juce::dontSendNotification);
    formantLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(formantLabel);

    formantDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    formantDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    formantDial.setRange(0.0, 100.0, 1.0);
    formantDial.setTextValueSuffix(" %");
    formantDial.setValue(audioProcessor.getFormantTracking() * 100.0, juce::dontSendNotification);

    formantDial.onValueChange = [this] {
            audioProcessor.setFormantTracking(static_cast<float>(formantDial.getValue() / 100.0));
        };

    addAndMakeVisible(formantDial);

    // gliding time
    glidingLabel.setText("Gliding", juce::dontSendNotification);
    glidingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(glidingLabel);

    glidingDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    glidingDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    glidingDial.setRange(0.0, 100.0, 1.0);
    glidingDial.setTextValueSuffix(" ms");
    glidingDial.setValue(audioProcessor.getGlidingTimeMs(),juce::dontSendNotification);

    glidingDial.onValueChange = [this] {
            audioProcessor.setGlidingTimeMs(static_cast<float>(glidingDial.getValue()));
        };

    addAndMakeVisible(glidingDial);


    // harmony tune
    harmonyTuneLabel.setText("Harmony Tune", juce::dontSendNotification);
    harmonyTuneLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(harmonyTuneLabel);

    harmonyTuneDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    harmonyTuneDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    harmonyTuneDial.setRange(0.0, 100.0, 1.0);
    harmonyTuneDial.setTextValueSuffix(" %");
    harmonyTuneDial.setValue(audioProcessor.getHarmonyTuneAmount() * 100.0, juce::dontSendNotification);

    harmonyTuneDial.onValueChange = [this] {
            audioProcessor.setHarmonyTuneAmount(static_cast<float>(harmonyTuneDial.getValue() / 100.0));
        };

    addAndMakeVisible(harmonyTuneDial);


    // Pitch gate
    pitchGateLabel.setText("Pitch Gate", juce::dontSendNotification);
    pitchGateLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchGateLabel);

    pitchGateSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pitchGateSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    pitchGateSlider.setRange(-90.0, -1.0, 1.0);
    pitchGateSlider.setTextValueSuffix(" dB");
    pitchGateSlider.setValue(audioProcessor.getPitchGateThresholdDb(), juce::dontSendNotification);

    pitchGateSlider.onValueChange = [this] {
            audioProcessor.setPitchGateThresholdDb(static_cast<float> (pitchGateSlider.getValue()));
        };

    addAndMakeVisible(pitchGateSlider);

    auto setupFader = [this](juce::Label& label, juce::Slider& fader, const juce::String& name,
        float initialDb, std::function<void(float)> onChange)
        {
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(label);

            fader.setSliderStyle(juce::Slider::LinearVertical);
            fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
            fader.setRange(-60.0, 6.0, 0.1);
            fader.setDoubleClickReturnValue(true, 0.0);
            fader.setTextValueSuffix(" dB");
            fader.setValue(initialDb, juce::dontSendNotification);
            fader.onValueChange = [&fader, onChange] { onChange(static_cast<float>(fader.getValue())); };
            addAndMakeVisible(fader);
        };

    setupFader(dryLabel, dryFader, "Dry", audioProcessor.getDryGainDb(), [this](float db) { audioProcessor.setDryGainDb(db); });
    setupFader(wetLabel, wetFader, "Wet", audioProcessor.getWetGainDb(), [this](float db) { audioProcessor.setWetGainDb(db); });
    setupFader(outLabel, outFader, "Out", audioProcessor.getOutputGainDb(), [this](float db) { audioProcessor.setOutputGainDb(db); });


    setResizable(false, false);
    //setResizeLimits(700, 400, 1000, 800);
    setSize(700, 400);
    startTimerHz(10);
}

VocalHarmonizerAudioProcessorEditor::~VocalHarmonizerAudioProcessorEditor()
{
}

//==============================================================================
void VocalHarmonizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

}

void VocalHarmonizerAudioProcessorEditor::resized()
{

    auto whole = getLocalBounds();

    auto header = whole.removeFromTop(51);
    auto mixerArea = whole.removeFromRight(200);
    const auto faderWidth = mixerArea.getWidth() / 3;

    auto dryArea = mixerArea.removeFromLeft(faderWidth);
    dryLabel.setBounds(dryArea.removeFromTop(24));
    dryFader.setBounds(dryArea);

    auto wetArea = mixerArea.removeFromLeft(faderWidth);
    wetLabel.setBounds(wetArea.removeFromTop(24));
    wetFader.setBounds(wetArea);

    auto outArea = mixerArea.removeFromLeft(faderWidth);
    outLabel.setBounds(outArea.removeFromTop(24));
    outFader.setBounds(outArea);


    const auto columnWidth = whole.getWidth() / 4;

    auto formantArea = whole.removeFromLeft(columnWidth);
    auto glidingArea = whole.removeFromLeft(columnWidth);
    auto harmonyTuneArea = whole.removeFromLeft(columnWidth);
    auto gateArea = whole.removeFromLeft(columnWidth);


   

    formantLabel.setBounds(formantArea.removeFromTop(24));
    formantDial.setBounds(formantArea.removeFromTop(100));

    glidingLabel.setBounds(glidingArea.removeFromTop(24));
    glidingDial.setBounds(glidingArea.removeFromTop(100));

    harmonyTuneLabel.setBounds(harmonyTuneArea.removeFromTop(24));
    harmonyTuneDial.setBounds(harmonyTuneArea.removeFromTop(100));

    pitchGateLabel.setBounds(gateArea.removeFromTop(24));
    pitchGateSlider.setBounds(gateArea.removeFromTop(100));



}

void VocalHarmonizerAudioProcessorEditor::timerCallback()
{

}
