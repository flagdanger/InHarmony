#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================================
VocalHarmonizerAudioProcessorEditor::VocalHarmonizerAudioProcessorEditor (VocalHarmonizerAudioProcessor& p)
    : AudioProcessorEditor (&p), 
    audioProcessor (p)
{
    // detected pitch
    pitchReadout.setJustificationType(juce::Justification::centred);
    pitchReadout.setFont(juce::Font(juce::FontOptions(22.0f)));
    pitchReadout.setText("Waiting for pitched input",
        juce::dontSendNotification);
    addAndMakeVisible(pitchReadout);

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

    // snapping time
    snappingLabel.setText("Snapping", juce::dontSendNotification);
    snappingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(snappingLabel);

    snappingDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    snappingDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    snappingDial.setRange(0.0, 100.0, 1.0);
    snappingDial.setTextValueSuffix(" ms");
    snappingDial.setValue(audioProcessor.getSnappingTimeMs(),juce::dontSendNotification);

    snappingDial.onValueChange = [this] {
            audioProcessor.setSnappingTimeMs(static_cast<float>(snappingDial.getValue()));
        };

    addAndMakeVisible(snappingDial);

    // latch time
    latchingLabel.setText("Latching", juce::dontSendNotification);
    latchingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(latchingLabel);

    latchingDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    latchingDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    latchingDial.setRange(0.0, 200.0, 25.0);
    latchingDial.setTextValueSuffix(" ms");
    latchingDial.setValue(audioProcessor.getLatchTimeMs(), juce::dontSendNotification);

    latchingDial.onValueChange = [this] {
            audioProcessor.setLatchTimeMs(static_cast<float> (latchingDial.getValue()));
        };

    addAndMakeVisible(latchingDial);


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

    // wet mix
    wetMixLabel.setText("Mix", juce::dontSendNotification);
    wetMixLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(wetMixLabel);

    wetMixSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    wetMixSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    wetMixSlider.setRange(0.0, 100.0, 1.0);
    wetMixSlider.setTextValueSuffix(" %");
    wetMixSlider.setValue(audioProcessor.getWetMix() * 100.0, juce::dontSendNotification);

    wetMixSlider.onValueChange = [this] {
            audioProcessor.setWetMix(static_cast<float>(wetMixSlider.getValue() / 100.0));
        };

    addAndMakeVisible(wetMixSlider);


    setResizable(true, true);
    setResizeLimits(600, 400, 1000, 800);
    setSize(600, 400);
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

    auto pitchReading = whole.removeFromTop(100);
    auto formantArea = whole.removeFromLeft(whole.getWidth() / 5);
    auto snappingArea = whole.removeFromLeft(whole.getWidth() / 4);
    auto latchingArea = whole.removeFromLeft(whole.getWidth() / 3);
    auto gateArea = whole.removeFromLeft(whole.getWidth() / 2);
    auto wetMixArea = whole; // whats remaining 


    pitchReadout.setBounds(pitchReading.removeFromTop(50));

    formantLabel.setBounds(snappingArea.removeFromTop(24));
    formantDial.setBounds(snappingArea.removeFromTop(100));

    snappingLabel.setBounds(snappingArea.removeFromTop(24));
    snappingDial.setBounds(snappingArea.removeFromTop(100));

    latchingLabel.setBounds(latchingArea.removeFromTop(24));
    latchingDial.setBounds(latchingArea.removeFromTop(100));

    pitchGateLabel.setBounds(gateArea.removeFromTop(24));
    pitchGateSlider.setBounds(gateArea.removeFromTop(100));

    wetMixLabel.setBounds(wetMixArea.removeFromTop(24));
    wetMixSlider.setBounds(wetMixArea.removeFromTop(100));

}

void VocalHarmonizerAudioProcessorEditor::timerCallback()
{


    // get pitch
    const auto pitch = audioProcessor.getDetectedPitchHz();

    if (!std::isfinite(pitch) || pitch <= 0.0f)
    {
        pitchReadout.setText("Waiting for pitched input",
            juce::dontSendNotification);
        return;
    }

    const auto pitchNote = juce::jlimit(0, 127, juce::roundToInt(69.0 + 12.0 * std::log2(pitch / 440.0)));
    const auto noteName = juce::MidiMessage::getMidiNoteName(pitchNote, true, true, 4);

    const auto latchedNote = audioProcessor.getLatchedNote();
    const auto latchedName = juce::MidiMessage::getMidiNoteName(latchedNote, true, true, 4);

    pitchReadout.setText(
        "Detected: " + noteName + "  " + juce::String(pitch, 1) + " Hz  |  "
        + "Latched: " + latchedName,
        juce::dontSendNotification);
}
