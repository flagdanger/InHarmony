#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"


//==============================================================================
/**
*/
class VocalHarmonizerAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    VocalHarmonizerAudioProcessor();
    ~VocalHarmonizerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    float getDetectedPitchHz() const noexcept { return detectedPitchHz.load (std::memory_order_relaxed); }
    float getPitchConfidence() const noexcept { return pitchConfidence.load (std::memory_order_relaxed); }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalHarmonizerAudioProcessor)

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;

    PitchDetector pitchDetector;

    std::atomic<float> detectedPitchHz{ 0.0f };
    std::atomic<float> pitchConfidence{ 0.0f };
};


