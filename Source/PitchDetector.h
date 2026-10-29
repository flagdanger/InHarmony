#pragma once

#include <JuceHeader.h>
#include <array>

class PitchDetector
{
public:
    static constexpr int frameSize = 2048;
    static constexpr int hopSize = 240;

    void prepare (double newSampleRate) noexcept;
    void reset() noexcept;
    float detectPitch (const float* buffer, int numSamples) noexcept;

    float getConfidence() const noexcept { return confidence; }

private:
    void analyseFrame() noexcept;

    static constexpr float minimumPitchHz = 80.0f;
    static constexpr float maximumPitchHz = 1000.0f;
    static constexpr float yinThreshold = 0.15f;

    std::array<float, frameSize> inputRing {};
    std::array<float, frameSize> analysisFrame {};
    std::array<float, frameSize> differenceFunction {};
    std::array<float, frameSize> cumulativeMeanNormalizedDifference {};

    double sampleRate = 44100.0;
    int minimumPeriod = 45;
    int maximumPeriod = 551;
    int ringWritePosition = 0;
    int countToFrameSize = 0;
    int samplesSinceAnalysis = 0;

    float pitchHz = 0.0f;
    float confidence = 0.0f;
};
