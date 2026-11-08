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

    // parameters, visible to the host (automation, saving, presets)
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // percent parameters are stored as 0 - 1
    float getFormantTracking() const noexcept { return formantParam->load() * 0.01f; }
    float getGlidingTimeMs() const noexcept { return glideParam->load(); }
    float getBreathAmount() const noexcept { return breathParam->load() * 0.01f; }
    float getPitchGateThresholdDb() const noexcept { return gateParam->load(); }
    float getJitter() const noexcept { return jitterParam->load() * 0.01f; }
    float getSpread() const noexcept { return spreadParam->load() * 0.01f; }
    float getDryGainDb() const noexcept { return dryParam->load(); }
    float getWetGainDb() const noexcept { return wetParam->load(); }
    float getOutputGainDb() const noexcept { return outParam->load(); }

    bool isMidiNoteHeld(int note) const noexcept
    {
        if (note < 0 || note > 127)
            return false;

        return heldMidiNoteCounts[note].load(std::memory_order_relaxed) > 0;
    }

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocalHarmonizerAudioProcessor)

    // cached pointers into apvts, set once in the constructor
    std::atomic<float>* formantParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* breathParam = nullptr;
    std::atomic<float>* gateParam = nullptr;
    std::atomic<float>* jitterParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    std::atomic<float>* outParam = nullptr;


    double currentSampleRate;
    static constexpr std::size_t maximumVoices = 8;

    void updateLatchedNote(float pitch, float confidence, int samplesElapsed) noexcept;

    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    void updateVoiceIntervals(int latchedSourceNote) noexcept;

    

    float latchedPitch = 0.0f;
    std::atomic<int> latchedNote{ -1 };
    static constexpr float latchTimeMs = 50.0f;

    

    // breath
    BreathExtract breathExtract;
    
    float breathLevel = 0.0f;             // follows sqrt(active voice count)
    float breathLevelCoefficient = 0.0f;

    

    static constexpr float maximumDriftCents = 10.0f;
    static constexpr float maximumTimingMs = 15.0f;

    std::array<SmoothRandom, maximumVoices> pitchWanderers;
    std::array<SmoothRandom, maximumVoices> timingWanderers;
    float timingSmoothingCoefficient = 0.0f;

    void updateJitter(float jitterAmount) noexcept;

    // spread
    juce::SmoothedValue<float> spreadSmoother;
    float panAttackStep = 0.0f;

    StereoChorus chorus;

    // pan layout: slots alternate R/L, each pair one level further out
    // levels grow past 3 only when slots 7-8 are in use, compressing the others inward
    static constexpr float outerPan = 0.9f;
    static constexpr int minimumLayoutLevels = 3;
    float layoutSmoothing = 0.0f;

    // adding movement to panning
    std::array<SmoothRandom, maximumVoices> panWanderers;
    static constexpr int panUpdateInterval = 32;
    int panUpdateCounter = 0;

    void updatePanGains(float spreadAmount) noexcept;

    static float dbToGain(float db) noexcept { return juce::Decibels::decibelsToGain(db, -60.0f); } // -60 dB and below = 0

    // volume controls
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
        float layoutPan = 0.0f;
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


