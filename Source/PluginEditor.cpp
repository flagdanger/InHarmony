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
    formantDial.setTextValueSuffix(" %");
    addAndMakeVisible(formantDial);
    formantAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "formant", formantDial);

    // glide time
    glidingLabel.setText("Gliding", juce::dontSendNotification);
    glidingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(glidingLabel);

    glidingDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    glidingDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    glidingDial.setTextValueSuffix(" ms");
    addAndMakeVisible(glidingDial);
    glideAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "glide", glidingDial);


    // Breath
    breathLabel.setText("Breath", juce::dontSendNotification);
    breathLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(breathLabel);

    breathDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    breathDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    breathDial.setTextValueSuffix(" %");
    addAndMakeVisible(breathDial);
    breathAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "breath", breathDial);


    // Pitch gate
    pitchGateLabel.setText("Pitch Gate", juce::dontSendNotification);
    pitchGateLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(pitchGateLabel);

    pitchGateDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    pitchGateDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    pitchGateDial.setTextValueSuffix(" dB");
    addAndMakeVisible(pitchGateDial);
    gateAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "gate", pitchGateDial);


    // Jitter
    jitterLabel.setText("Jitter", juce::dontSendNotification);
    jitterLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(jitterLabel);

    jitterDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    jitterDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    jitterDial.setTextValueSuffix(" %");
    addAndMakeVisible(jitterDial);
    jitterAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "jitter", jitterDial);


    // Spread
    spreadLabel.setText("Spread", juce::dontSendNotification); 
    spreadLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(spreadLabel);

    spreadDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    spreadDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 20);
    spreadDial.setTextValueSuffix(" %");
    addAndMakeVisible(spreadDial);
    spreadAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "glide", spreadDial);


    auto setupFader = [this](juce::Label& label, juce::Slider& fader, const juce::String& name)
        {
            label.setText(name, juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            addAndMakeVisible(label);

            fader.setSliderStyle(juce::Slider::LinearVertical);
            fader.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
            fader.setDoubleClickReturnValue(true, 0.0);
            fader.setTextValueSuffix(" dB");
            addAndMakeVisible(fader);
        };

    setupFader(dryLabel, dryFader, "Dry");
    setupFader(wetLabel, wetFader, "Wet");
    setupFader(outLabel, outFader, "Out");

    dryAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "dry", dryFader);
    wetAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "wet", wetFader);
    outAttachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, "out", outFader);


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
