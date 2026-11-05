#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PitchShifter.h"

#include <vector>
#include <cstddef>


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

    float getSnappingTimeMs() const noexcept
    {
        return snappingTimeMs.load(std::memory_order_relaxed);
    }

    void setSnappingTimeMs(float newTimeMs) noexcept
    {
        snappingTimeMs.store(juce::jlimit(0.0f, 100.0f, newTimeMs), std::memory_order_relaxed);
    }

    float getLatchTimeMs() const noexcept
    {
        return latchTimeMs.load(std::memory_order_relaxed);
    }

    void setLatchTimeMs(float newTimeMs) noexcept
    {
        latchTimeMs.store(newTimeMs, std::memory_order_relaxed);
    }

    int getLatchedNote() const noexcept
    {
        return latchedNote.load(std::memory_order_relaxed);
    }

    float getPitchGateThresholdDb() const noexcept
    {
        return pitchGateThresholdDb.load(std::memory_order_relaxed);
    }

    void setPitchGateThresholdDb(float newThresholdDb) noexcept
    {
        pitchGateThresholdDb.store(newThresholdDb, std::memory_order_relaxed);
    }

    float getWetMix() const noexcept
    {
        return wetMix.load(std::memory_order_relaxed);
    }

    void setWetMix(float newMix) noexcept
    {
        wetMix.store(juce::jlimit(0.0f, 1.0f, newMix), std::memory_order_relaxed);
    }

    bool isMidiNoteHeld(int note) const noexcept
    {
        if (note < 0 || note > 127)
            return false;

        return heldMidiNoteCounts[note].load(std::memory_order_relaxed) > 0;
    }

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalHarmonizerAudioProcessor)

    double currentSampleRate;

    void updateLatchedNote(float pitch, float confidence, int samplesElapsed) noexcept;

    std::atomic<float> snappingTimeMs{ 42.0f };

    float latchedPitch = 0.0f;
    std::atomic<int> latchedNote{ -1 };
    std::atomic<float> latchTimeMs{ 50.0f };
    std::atomic<float> pitchGateThresholdDb{ -60.0f };

    std::atomic<float> wetMix{ 0.5f };
    juce::SmoothedValue<float> wetMixSmoother;

    int candidateNote = -1;
    int candidateSamples = 0;

    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> wetBuffer;

    std::vector<float> dryBufferDelayed;
    std::size_t dryDelayWritePosition = 0;

    PitchDetector pitchDetector;
    PitchShifter pitchShifter;

    std::atomic<float> detectedPitchHz{ 0.0f };
    std::atomic<float> pitchConfidence{ 0.0f };

    std::array<std::atomic<int>, 128> heldMidiNoteCounts{};
};


