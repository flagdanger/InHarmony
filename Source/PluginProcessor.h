#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "StereoChorus.h"
#include "VoiceDelay.h"
#include "BreathExtract.h"

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

    float getBreathAmount() const noexcept 
    { 
        return breathAmount.load(std::memory_order_relaxed); 
    }

    void setBreathAmount(float newAmount) noexcept
    {
        breathAmount.store(juce::jlimit(0.0f, 1.0f, newAmount), std::memory_order_relaxed);
    }

    float getJitter() const noexcept 
    { 
        return jitter.load(std::memory_order_relaxed); 
    }

    void setJitter(float newJitter) noexcept
    {
        jitter.store(juce::jlimit(0.0f, 1.0f, newJitter), std::memory_order_relaxed);
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

    float latchedPitch = 0.0f;
    std::atomic<int> latchedNote{ -1 };
    static constexpr float latchTimeMs = 50.0f;
    std::atomic<float> pitchGateThresholdDb{ -60.0f };

    // breath
    BreathExtract breathExtract;
    std::atomic<float> breathAmount{ 0.3f };
    float breathLevel = 0.0f;             // follows sqrt(active voice count)
    float breathLevelCoefficient = 0.0f;

    // jitter
    std::atomic<float> jitter{ 0.0f };

    static constexpr float maximumDriftCents = 10.0f;
    static constexpr float maximumTimingMs = 15.0f;

    std::array<SmoothRandom, maximumVoices> pitchWanderers;
    std::array<SmoothRandom, maximumVoices> timingWanderers;
    float timingSmoothingCoefficient = 0.0f;

    void updateJitter(float jitterAmount) noexcept;

    // spread
    std::atomic<float> spread{ 0.0f };          // 0 = mono, 1 = full width
    juce::SmoothedValue<float> spreadSmoother;
    float panAttackStep = 0.0f;

    StereoChorus chorus;

    // fixed pan slot per voice index, -1 = hard left, +1 = hard right
    static constexpr std::array<float, maximumVoices> voicePanPositions{ 0.3f, -0.3f, 0.65f, -0.65f, 0.9f };

    // adding movement to panning
    std::array<SmoothRandom, maximumVoices> panWanderers;
    static constexpr int panUpdateInterval = 32;
    int panUpdateCounter = 0;

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
    juce::AudioBuffer<float> monoBuffer;

    std::array<std::vector<float>, 2> dryDelayLines;
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
        float panMovementAttack = 0.0f; // spread's pan movement fades in

        VoiceDelay timingDelay;
        float timingDelayTarget = 0.0f;   // samples, set at control rate
        float timingDelaySmoothed = 0.0f; // samples, followed per sample
    };

    std::array<Voice, maximumVoices> Voices{};
    std::size_t voiceTailSamples = 0;
    int lastIntervalSourceNote = -1;

    std::array<std::atomic<int>, 128> heldMidiNoteCounts{};
};


