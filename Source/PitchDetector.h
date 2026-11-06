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

    float pitchHz = 0.0f;
    float confidence = 0.0f;

    float minimumFrameRms = 0.001f;
};
