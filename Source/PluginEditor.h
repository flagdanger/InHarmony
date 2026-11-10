#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "HarmonyLookAndFeel.h"
#include "Controls.h"
#include "InputMeter.h"
#include "PitchTrace.h"
#include "PageTabs.h"
#include "Pages.h"
#include "StereoField.h"
#include "PresetBar.h"

//==============================================================================
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
    void showPage (int index);

    VocalHarmonizerAudioProcessor& audioProcessor;

    // declared before every component so that its destroyed after them
    HarmonyLookAndFeel lookAndFeel;

    // left column: input meter + gate, pitch trace, tabbed pages
    InputMeter inputMeter;
    PitchTrace pitchTrace;
    StereoField stereoField;   // always visible, right under the trace
    PageTabs pageTabs { juce::StringArray { "Correction", "Voice", "Synth", "Spread" } };
    CorrectionPage correctionPage;
    VoicePage voicePage;
    SynthPage synthPage;
    SpreadPage spreadPage;

    // right column: spread macro and the mixer
    LabelledDial spreadDial;
    FaderStrip dryFader, wetFader, outFader;

    // preset bar in header
    PresetDialog presetDialog;
    PresetBar presetBar;

    // meter levels as shown, so they fall smoothly between timer ticks
    float inputLevelShown = -100.0f;
    int meterTickCounter = 0; // timer runs at 60 hz, so meters refresh every 3rd tick (20 hz)
    int traceTickCounter = 0; // pitch trace and stereo field every other tick (30 hz)
    float dryLevelShown = 0.0f;
    float wetLevelShown = 0.0f;
    float outLevelShown = 0.0f;

    // trademark
    juce::Image logo;

    // areas and lines painted by the editor itself
    juce::Rectangle<int> headerArea, leftColumn, rightColumn;
    int inputDividerY = 0, traceDividerY = 0, spreadDividerY = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalHarmonizerAudioProcessorEditor)
};
