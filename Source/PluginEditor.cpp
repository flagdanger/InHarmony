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
    glidingDial.setRange(0.0, 1000.0, 1.0);
    glidingDial.setTextValueSuffix(" ms");
    glidingDial.setValue(audioProcessor.getGlidingTimeMs(),juce::dontSendNotification);

    glidingDial.onValueChange = [this] {
            audioProcessor.setGlidingTimeMs(static_cast<float>(glidingDial.getValue()));
        };

    addAndMakeVisible(glidingDial);


    // Breath
    breathLabel.setText("Breath", juce::dontSendNotification);
    breathLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(breathLabel);

    breathDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    breathDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    breathDial.setRange(0.0, 100.0, 1.0);
    breathDial.setTextValueSuffix(" %");
    breathDial.setValue(audioProcessor.getBreathAmount() * 100.0, juce::dontSendNotification);

    breathDial.onValueChange = [this] {
            audioProcessor.setBreathAmount(static_cast<float>(breathDial.getValue() / 100.0));
        };

    addAndMakeVisible(breathDial);


    // Pitch gate
    pitchGateLabel.setText("Pitch Gate", juce::dontSendNotification);
    pitchGateLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchGateLabel);

    pitchGateDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pitchGateDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    pitchGateDial.setRange(-90.0, -1.0, 1.0);
    pitchGateDial.setTextValueSuffix(" dB");
    pitchGateDial.setValue(audioProcessor.getPitchGateThresholdDb(), juce::dontSendNotification);

    pitchGateDial.onValueChange = [this] {
            audioProcessor.setPitchGateThresholdDb(static_cast<float> (pitchGateDial.getValue()));
        };

    addAndMakeVisible(pitchGateDial);

    // Jitter
    jitterLabel.setText("Jitter", juce::dontSendNotification);
    jitterLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(jitterLabel);

    jitterDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    jitterDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    jitterDial.setRange(0, 100.0, 1.0);
    jitterDial.setTextValueSuffix(" %");
    jitterDial.setValue(audioProcessor.getJitter() * 100.0, juce::dontSendNotification);

    jitterDial.onValueChange = [this] {
        audioProcessor.setJitter(static_cast<float> (jitterDial.getValue() / 100.0));
        };

    addAndMakeVisible(jitterDial);

    // Spread
    spreadLabel.setText("Spread", juce::dontSendNotification); 
    spreadLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(spreadLabel);

    spreadDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    spreadDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    spreadDial.setRange(0, 100.0, 1.0);
    spreadDial.setTextValueSuffix(" %");
    spreadDial.setValue(audioProcessor.getSpread() * 100.0, juce::dontSendNotification);

    spreadDial.onValueChange = [this] {
        audioProcessor.setSpread(static_cast<float> (spreadDial.getValue() / 100.0));
        };

    addAndMakeVisible(spreadDial);

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
    setSize(800, 400);
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


    const auto columnWidth = whole.getWidth() / 6;

    auto formantArea = whole.removeFromLeft(columnWidth);
    auto glidingArea = whole.removeFromLeft(columnWidth);
    auto breathArea = whole.removeFromLeft(columnWidth);
    auto gateArea = whole.removeFromLeft(columnWidth);
    auto jitterArea = whole.removeFromLeft(columnWidth);
    auto spreadArea = whole.removeFromLeft(columnWidth);


   

    formantLabel.setBounds(formantArea.removeFromTop(24));
    formantDial.setBounds(formantArea.removeFromTop(100));

    glidingLabel.setBounds(glidingArea.removeFromTop(24));
    glidingDial.setBounds(glidingArea.removeFromTop(100));

    breathLabel.setBounds(breathArea.removeFromTop(24));
    breathDial.setBounds(breathArea.removeFromTop(100));

    pitchGateLabel.setBounds(gateArea.removeFromTop(24));
    pitchGateDial.setBounds(gateArea.removeFromTop(100));

    jitterLabel.setBounds(jitterArea.removeFromTop(24));
    jitterDial.setBounds(jitterArea.removeFromTop(100));

    spreadLabel.setBounds(spreadArea.removeFromTop(24));
    spreadDial.setBounds(spreadArea.removeFromTop(100));



}

void VocalHarmonizerAudioProcessorEditor::timerCallback()
{

}
