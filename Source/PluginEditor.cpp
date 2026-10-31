#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

//==============================================================================
VocalHarmonizerAudioProcessorEditor::VocalHarmonizerAudioProcessorEditor (VocalHarmonizerAudioProcessor& p)
    : AudioProcessorEditor (&p), 
    audioProcessor (p),
    keyboard(keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    // detected pitch
    pitchReadout.setJustificationType(juce::Justification::centred);
    pitchReadout.setFont(juce::Font(juce::FontOptions(22.0f)));
    pitchReadout.setText("Waiting for pitched input",
        juce::dontSendNotification);
    addAndMakeVisible(pitchReadout);

    latchingLabel.setText("Latching", juce::dontSendNotification);
    latchingLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(latchingLabel);

    // latch time
    latchingDial.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    latchingDial.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    latchingDial.setRange(25.0, 200.0, 25.0);
    latchingDial.setTextValueSuffix(" ms");
    latchingDial.setValue(audioProcessor.getLatchTimeMs(), juce::dontSendNotification);

    latchingDial.onValueChange = [this]
        {
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

    pitchGateSlider.onValueChange = [this]
        {
            audioProcessor.setPitchGateThresholdDb(
                static_cast<float> (pitchGateSlider.getValue()));
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
    wetMixSlider.setValue(audioProcessor.getWetMix() * 100.0,
        juce::dontSendNotification);

    wetMixSlider.onValueChange = [this]
        {
            audioProcessor.setWetMix(
                static_cast<float>(wetMixSlider.getValue() / 100.0));
        };

    addAndMakeVisible(wetMixSlider);

    // keyboard
    keyboard.setAvailableRange(36, 84);
    keyboard.setScrollButtonsVisible(false);
    keyboard.setKeyWidth(18.0f);
    keyboard.setWantsKeyboardFocus(false);
    addAndMakeVisible(keyboard);

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

    // Reserve the bottom 120 px for the piano, with a little vertical padding.
    auto keyboardArea = whole.removeFromBottom(std::min(120, whole.getHeight()/ 5));
    //keyboardArea.reduce(0, 6);
    keyboard.setBounds(keyboardArea);

    // Size the keys to show about two octaves across the available width.
    constexpr float visibleWhiteKeys = 28.0f;
    keyboard.setKeyWidth(static_cast<float> (keyboard.getWidth()) / visibleWhiteKeys);

    auto pitchReading = whole.removeFromTop(100);
    auto latchingArea = whole.removeFromLeft(whole.getWidth() / 3);
    auto gateArea = whole.removeFromLeft(whole.getWidth() / 2);
    auto wetMixArea = whole; // whats remaining 


    pitchReadout.setBounds(pitchReading.removeFromTop(50));

    latchingLabel.setBounds(latchingArea.removeFromTop(24));
    latchingDial.setBounds(latchingArea.removeFromTop(100));

    pitchGateLabel.setBounds(gateArea.removeFromTop(24));
    pitchGateSlider.setBounds(gateArea.removeFromTop(100));

    wetMixLabel.setBounds(wetMixArea.removeFromTop(24));
    wetMixSlider.setBounds(wetMixArea.removeFromTop(100));

}

void VocalHarmonizerAudioProcessorEditor::timerCallback()
{
    // check for midi inputs to change keyboard state
    for (int note = 0; note < 128; ++note)
    {
        const auto isHeld = audioProcessor.isMidiNoteHeld(note);

        if (isHeld == displayedMidiNotes[note])
            continue;

        if (isHeld)
            keyboardState.noteOn(1, note, 1.0f);
        else
            keyboardState.noteOff(1, note, 0.0f);

        displayedMidiNotes[note] = isHeld;
    }


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
