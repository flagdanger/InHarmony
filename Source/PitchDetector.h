#pragma once

#include <JuceHeader.h>
#include <array>

class PitchDetector
{
public:
    static constexpr int frameSize = 2048;
    static constexpr int hopSize = 512;

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;
    void processSample (float sample) noexcept;

    float getConfidence() const noexcept { return confidence; }
    float getPitch() const noexcept { return pitchHz; }

    void setReferencePitch(float hz) noexcept { referencePitchHz = hz; }

    void setMinimumFrameRms(float threshold) noexcept; // input gate

private:
    void analyzeFrame() noexcept;

    static constexpr int correlationFFTOrder = 12;
    static constexpr int correlationFFTSize = 1 << correlationFFTOrder;

    using FFTComplex = juce::dsp::Complex<float>;

    juce::dsp::FFT correlationFFT{ correlationFFTOrder };

    std::array<FFTComplex, correlationFFTSize> fftInputA{};
    std::array<FFTComplex, correlationFFTSize> fftInputB{};
    std::array<FFTComplex, correlationFFTSize> fftSpectrumA{};
    std::array<FFTComplex, correlationFFTSize> fftSpectrumB{};
    std::array<FFTComplex, correlationFFTSize> fftCorrelation{};

    std::array<float, frameSize + 1> squaredEnergyPrefix{};

    static constexpr float minimumPitchHz = 80.0f;
    static constexpr float maximumPitchHz = 1000.0f;
    static constexpr float yinThreshold = 0.15f;
    
    static constexpr float maximumBrightness = 0.15f;
    static constexpr float octaveTolerance = 0.1f; // a shorter period this close to the best one wins
    static constexpr float subharmonicLimit = 0.3f;
    static constexpr float subharmonicLimitTracking = 0.6f;
    static constexpr int forgetPitchFrames = 12;


    std::array<float, frameSize> inputRing {};
    std::array<float, frameSize> analysisFrame {};
    std::array<float, frameSize> differenceFunction {};
    std::array<float, frameSize> cmnd {};

    double sampleRate = 44100.0;
    int minimumPeriod = 45;
    int maximumPeriod = 551;
    int ringWritePosition = 0;
    int countToFrameSize = 0;
    int samplesSinceAnalysis = 0;

    // with high sample rates need to low-pass, then keep every Nth sample, so analysis always runs at 44.1-48 kHz
    int decimation = 1;
    int decimationCounter = 0;
    float lowpassB0 = 1.0f, lowpassB1 = 0.0f, lowpassB2 = 0.0f, lowpassA1 = 0.0f, lowpassA2 = 0.0f;
    float lowpassZ1 = 0.0f, lowpassZ2 = 0.0f;

    float pitchHz = 0.0f;
    float lastPitchHz = 0.0f;
    float confidence = 0.0f;
    int framesSincePitch = 0;
    float outlierHz = 0.0f;
    int outlierRun = 0;

    float minimumFrameRms = 0.001f;

    float referencePitchHz = 0.0f;
    int voicedFrames = 0;
    static constexpr int onsetFrames = 6;                 // ~70 ms, how long a new sung stretch counts as its onset
    static constexpr float subharmonicLimitOnset = 0.5f;  // at an onset, use the octave closest to the last sung note

    // ignored unless it stays an outlier
    static constexpr float outlierSemitones = 9.0f;
    static constexpr float outlierConfidence = 0.9f;
    static constexpr int outlierAcceptFrames = 8; // ~90 ms

};
