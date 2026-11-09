#pragma once

#include <JuceHeader.h>
#include "PitchDetector.h"
#include "PitchShifter.h"
#include "StereoChorus.h"
#include "VoiceDelay.h"
#include "BreathExtract.h"
#include "Limiter.h"
#include "ShapedEnvelope.h"
#include "Scales.h"

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

    static constexpr std::size_t maximumVoices = 8;

    float getDetectedPitchHz() const noexcept { return detectedPitchHz.load (std::memory_order_relaxed); }

    // live values for the editor's meters and displays, written by the audio thread once per block
    float getAndResetInputLevelDb() noexcept { return inputLevelDb.exchange(-100.0f, std::memory_order_relaxed); }
    float getAndResetDryPeak() noexcept { return dryPeak.exchange(0.0f, std::memory_order_relaxed); }
    float getAndResetWetPeak() noexcept { return wetPeak.exchange(0.0f, std::memory_order_relaxed); }
    float getAndResetOutPeak() noexcept { return outPeak.exchange(0.0f, std::memory_order_relaxed); }
    float getVoiceOutputHz(std::size_t index) const noexcept { return voiceDisplayHz[index].load(std::memory_order_relaxed); }
    float getVoicePan(std::size_t index) const noexcept { return voiceDisplayPan[index].load(std::memory_order_relaxed); }
    float getVoiceBasePan(std::size_t index) const noexcept { return voiceDisplayBasePan[index].load(std::memory_order_relaxed); }
    float getVoiceMovementRange(std::size_t index) const noexcept { return voiceDisplayRange[index].load(std::memory_order_relaxed); }
    float getPitchConfidence() const noexcept { return pitchConfidence.load (std::memory_order_relaxed); }
    float getLeadDisplayNote() const noexcept { return leadDisplayNote.load(std::memory_order_relaxed); }

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
    float getPitchGateThresholdDb() const noexcept { return gateParam->load(); }
    float getCorrectionAmount() const noexcept { return correctAmountParam->load() * 0.01f; }
    float getSnapping() const noexcept { return snapParam->load() * 0.01f; }
    float getFormantTracking() const noexcept { return formantParam->load() * 0.01f; }
    float getBreathAmount() const noexcept { return breathParam->load() * 0.01f; }
    float getLowCutHz() const noexcept { return lowCutParam->load(); }
    float getHighCutHz() const noexcept { return highCutParam->load(); }
    float getJitter() const noexcept { return jitterParam->load() * 0.01f; }
    float getGlidingTimeMs() const noexcept { return glideParam->load(); }
    float getScoopSemitones() const noexcept { return scoopParam->load(); }
    float getSpread() const noexcept { return spreadParam->load() * 0.01f; }
    float getMovement() const noexcept { return movementParam->load() * 0.01f; }
    float getMovementRate() const noexcept { return movementRateParam->load(); }
    float getChorusDepth() const noexcept { return chorusParam->load() * 0.01f; }
    float getChorusRate() const noexcept { return chorusRateParam->load(); }
    float getChorusRandom() const noexcept { return chorusRandomParam->load() * 0.01f; }
    float getDryGainDb() const noexcept { return dryParam->load(); }
    float getWetGainDb() const noexcept { return wetParam->load(); }
    float getOutputGainDb() const noexcept { return outParam->load(); }
    float getDryCeilingDb() const noexcept { return dryCeilingParam->load(); }
    float getWetCeilingDb() const noexcept { return wetCeilingParam->load(); }
    float getOutCeilingDb() const noexcept { return outCeilingParam->load(); }

    // notes correction can snap to, as pitch classes bit 0 = C etc
    juce::uint16 getCorrectionMask() const noexcept
    {
        const auto keyChoice = juce::roundToInt(correctKeyParam->load());

        if (keyChoice > 0) // if key choice isn't "off"
            return scales::maskFor(keyChoice - 1, juce::roundToInt(correctScaleParam->load()));

        // if its off
        juce::uint16 mask = 0;
        for (int i = 0; i < 12; ++i)
            if (correctNoteParams[(std::size_t)i]->load() > 0.5f)
                mask = static_cast<juce::uint16> (mask | (1u << i));

        return mask;
    }

    // glide mode
    enum class GlideMode { fromVoice = 0, duet, fromLast };
    GlideMode getGlideMode() const noexcept { return static_cast<GlideMode>(juce::roundToInt(glideModeParam->load())); }

    // adsr envelope
    ShapedEnvelope::Parameters getEnvelopeParameters() const noexcept
    {
        ShapedEnvelope::Parameters p;
        p.attackSeconds = attackParam->load() * 0.001f;
        p.decaySeconds = decayParam->load() * 0.001f;
        p.sustainLevel = sustainParam->load() * 0.01f;
        p.releaseSeconds = releaseParam->load() * 0.001f;
        p.attackCurve = attackCurveParam->load() * 0.01f;
        p.decayCurve = decayCurveParam->load() * 0.01f;
        p.releaseCurve = releaseCurveParam->load() * 0.01f;
        return p;
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

    // cached pointers into apvts, set once in the constructor
    std::atomic<float>* gateParam = nullptr;
    std::atomic<float>* correctAmountParam = nullptr;
    std::atomic<float>* snapParam = nullptr;
    std::atomic<float>* correctKeyParam = nullptr;
    std::atomic<float>* correctScaleParam = nullptr;
    std::array<std::atomic<float>*, 12> correctNoteParams{};
    std::atomic<float>* formantParam = nullptr;
    std::atomic<float>* breathParam = nullptr;
    std::atomic<float>* lowCutParam = nullptr;
    std::atomic<float>* highCutParam = nullptr;
    std::atomic<float>* jitterParam = nullptr;
    std::atomic<float>* glideParam = nullptr;
    std::atomic<float>* scoopParam = nullptr;
    std::atomic<float>* glideModeParam = nullptr;
    std::atomic<float>* spreadParam = nullptr;
    std::atomic<float>* movementParam = nullptr;
    std::atomic<float>* movementRateParam = nullptr;
    std::atomic<float>* chorusParam = nullptr;
    std::atomic<float>* chorusRateParam = nullptr;
    std::atomic<float>* chorusRandomParam = nullptr;
    std::atomic<float>* dryParam = nullptr;
    std::atomic<float>* wetParam = nullptr;
    std::atomic<float>* outParam = nullptr;
    std::atomic<float>* dryCeilingParam = nullptr;
    std::atomic<float>* wetCeilingParam = nullptr;
    std::atomic<float>* outCeilingParam = nullptr;

    std::atomic<float>* attackParam = nullptr;
    std::atomic<float>* decayParam = nullptr;
    std::atomic<float>* sustainParam = nullptr;
    std::atomic<float>* releaseParam = nullptr;

    std::atomic<float>* attackCurveParam = nullptr;
    std::atomic<float>* decayCurveParam = nullptr;
    std::atomic<float>* releaseCurveParam = nullptr;


    double currentSampleRate;

    void updateLatchedNote(float pitch, float confidence, int samplesElapsed) noexcept;

    void handleMidiMessage(const juce::MidiMessage& message) noexcept;
    void updateVoiceIntervals(int latchedSourceNote) noexcept;

    

    float latchedPitch = 0.0f;
    std::atomic<int> latchedNote{ -1 };
    static constexpr float latchTimeMs = 50.0f;

    // harmony filters on the summed wet signal
    juce::dsp::StateVariableTPTFilter<float> lowCutFilter;
    juce::dsp::StateVariableTPTFilter<float> highCutFilter;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lowCutSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> highCutSmoother;
    
    // limiters
    PeakLimiter dryLimiter;
    PeakLimiter wetLimiter;
    PeakLimiter outLimiter;

    

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
    juce::SmoothedValue<float> movementSmoother;
    juce::SmoothedValue<float> chorusDepthSmoother;
    juce::SmoothedValue<float> chorusRandomSmoother;
    float panAttackStep = 0.0f;

    StereoChorus chorus;

    // pan layout: slots alternate R/L, each pair one level further out
    // levels grow past 3 only when slots 7-8 are in use, compressing the others inward
    static constexpr float outerPan = 0.9f;
    static constexpr int minimumLayoutLevels = 3;
    float layoutSmoothing = 0.0f;

    // adding movement to panning
    static constexpr float baseMovementRateHz = 1.0f / 3.5f; // pan wanderers were tuned for ~3.5 s per move
    std::array<SmoothRandom, maximumVoices> panWanderers;
    static constexpr int panUpdateInterval = 32;
    int panUpdateCounter = 0;

    void updatePanGains(float spreadAmount, float movementDepth) noexcept;
    void updateMovementRate() noexcept;

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

    // input vocal correction
    PitchShifter correctionShifter;
    bool correctionRunning = false;
    int correctionTargetNote = -1;          // correction target
    float correctionOffset = 0.0f;          // semitone difference to target
    float correctionBlend = 0.0f;           // follows the shifter's output gain
    float correctionBlendCoefficient = 0.0f;
    juce::SmoothedValue<float> correctionOnSmoother; // fades correction in and out at amount 0
    std::atomic<float> leadDisplayNote{ -1.0f };

    // editor metering: input RMS (dB), peaks since the editor last read them, per-voice pitch and pan
    std::atomic<float> inputLevelDb{ -100.0f };
    std::atomic<float> dryPeak{ 0.0f }, wetPeak{ 0.0f }, outPeak{ 0.0f };
    std::array<std::atomic<float>, maximumVoices> voiceDisplayHz{};
    std::array<std::atomic<float>, maximumVoices> voiceDisplayPan{};
    std::array<std::atomic<float>, maximumVoices> voiceDisplayBasePan{};
    std::array<std::atomic<float>, maximumVoices> voiceDisplayRange{};

    // raise target to value if it's higher (the editor resets it when it reads)
    static void storeMax(std::atomic<float>& target, float value) noexcept
    {
        auto current = target.load(std::memory_order_relaxed);
        while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {}
    }

    

    struct Voice
    {
        PitchShifter shifter;
        int midiNote = -1;
        bool noteHeld = false;
        ShapedEnvelope envelope;
        float panLeft = 1.0f;
        float panRight = 1.0f;
        float layoutPan = 0.0f;
        float panMovementAttack = 0.0f; // spread's pan movement fades in
        float currentPan = 0.0f;        // -1..1, for the editor's stereo field
        float currentBasePan = 0.0f;    // slot position before drift
        float currentMovementRange = 0.0f; // how far drift can move it either side

        VoiceDelay timingDelay;
        float timingDelayTarget = 0.0f;   // samples, set at control rate
        float timingDelaySmoothed = 0.0f; // samples, followed per sample
    };

    std::array<Voice, maximumVoices> Voices{};
    int lastIntervalSourceNote = -1;

    // glide modes
    GlideMode activeGlideMode = GlideMode::fromVoice;
    int lastPlayedNote = -1;     // most recent note on
    int glideSourceNote = -1;    // the note before the next chord
    int samplesSinceNoteOn = 1 << 30;
    int chordWindowSamples = 0;  // note-ons closer together than this count as one chord

    std::array<int, 16> monoNoteStack{};  // held keys in press order, for mono's "slide back" on release
    int monoNoteStackSize = 0;

    void setVoiceNote(Voice& voice, int note) noexcept;
    void startVoice(Voice& voice) noexcept;
    void releaseAllVoices() noexcept;
    void monoNoteOn(int note) noexcept;
    void monoNoteOff(int note) noexcept;
    void removeFromMonoStack(int note) noexcept;

    std::array<std::atomic<int>, 128> heldMidiNoteCounts{};
};


