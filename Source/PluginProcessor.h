#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "StereoChorus.h"

#include <vector>
#include <array>
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

    float getFormantTracking() const noexcept
    {
        return formantTracking.load(std::memory_order_relaxed);
    }

    void setFormantTracking(float newAmount) noexcept
    {
        formantTracking.store(juce::jlimit(0.0f, 1.0f, newAmount), std::memory_order_relaxed);
    }

    float getGlidingTimeMs() const noexcept
    {
        return glidingTimeMs.load(std::memory_order_relaxed);
    }

    void setGlidingTimeMs(float newTimeMs) noexcept
    {
        glidingTimeMs.store(juce::jlimit(0.0f, 100.0f, newTimeMs), std::memory_order_relaxed);
    }

    float getHarmonyTuneAmount() const noexcept
    {
        return harmonyTuneAmount.load(std::memory_order_relaxed);
    }

    void setHarmonyTuneAmount(float newAmount) noexcept
    {
        harmonyTuneAmount.store(juce::jlimit(0.0f, 1.0f, newAmount), std::memory_order_relaxed);
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

    float getSpread() const noexcept 
    { 
        return spread.load(std::memory_order_relaxed); 
    }

    void setSpread(float newSpread) noexcept
    {
        spread.store(juce::jlimit(0.0f, 1.0f, newSpread), std::memory_order_relaxed);
    }

    float getDryGainDb() const noexcept { return dryGainDb.load(std::memory_order_relaxed); }
    float getWetGainDb() const noexcept { return wetGainDb.load(std::memory_order_relaxed); }
    float getOutputGainDb() const noexcept { return outputGainDb.load(std::memory_order_relaxed); }

    void setDryGainDb(float db) noexcept { dryGainDb.store(juce::jlimit(-60.0f, 6.0f, db), std::memory_order_relaxed); }
    void setWetGainDb(float db) noexcept { wetGainDb.store(juce::jlimit(-60.0f, 6.0f, db), std::memory_order_relaxed); }
    void setOutputGainDb(float db) noexcept { outputGainDb.store(juce::jlimit(-60.0f, 6.0f, db), std::memory_order_relaxed); }

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
    static constexpr std::size_t maximumVoices = 5;

    std::atomic<float> formantTracking{ 0.35f };

    void updateLatchedNote(float pitch, float confidence, int samplesElapsed) noexcept;

    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    void updateVoiceIntervals(int latchedSourceNote) noexcept;

    std::atomic<float> glidingTimeMs{ 42.0f };

    std::atomic<float> harmonyTuneAmount{ 0.0f }; // 0 follows input pitch, 1 snaps to real note

    float latchedPitch = 0.0f;
    std::atomic<int> latchedNote{ -1 };
    static constexpr float latchTimeMs = 50.0f;
    std::atomic<float> pitchGateThresholdDb{ -60.0f };

    std::atomic<float> spread{ 0.0f };          // 0 = mono, 1 = full width
    juce::SmoothedValue<float> spreadSmoother;
    float lastSpread = -1.0f;

    // fixed pan slot per voice index, -1 = hard left, +1 = hard right
    static constexpr std::array<float, maximumVoices> voicePanPositions{ 0.3f, -0.3f, 0.65f, -0.65f, 0.9f };

    StereoChorus chorus;

    void updatePanGains(float spreadAmount) noexcept;

    static float dbToGain(float db) noexcept { return juce::Decibels::decibelsToGain(db, -60.0f); } // -60 dB and below = 0

    // volume controls
    std::atomic<float> dryGainDb{ 0.0f };
    std::atomic<float> wetGainDb{ 0.0f };
    std::atomic<float> outputGainDb{ 0.0f };

    juce::SmoothedValue<float> dryGainSmoother, wetGainSmoother, outputGainSmoother;

    int candidateNote = -1;
    int candidateSamples = 0;

    juce::AudioBuffer<float> dryBuffer;
    std::vector<float> dryBufferDelayed;
    std::size_t dryDelayWritePosition = 0;

    PitchDetector pitchDetector;

    std::atomic<float> detectedPitchHz{ 0.0f };
    std::atomic<float> pitchConfidence{ 0.0f };

    

    struct Voice
    {
        PitchShifter shifter;
        int midiNote = -1;
        bool noteHeld = false;
        std::size_t tailSamplesRemaining = 0;
        float panLeft = 1.0f;
        float panRight = 1.0f;
    };

    std::array<Voice, maximumVoices> Voices{};
    std::size_t voiceTailSamples = 0;
    int lastIntervalSourceNote = -1;

    std::array<std::atomic<int>, 128> heldMidiNoteCounts{};
};


