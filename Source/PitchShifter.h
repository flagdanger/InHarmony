#pragma once

#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>

class PitchShifter
{
public:

    void prepare(double setSampleRate);
    void reset() noexcept;

    void setIntervalSemitones(float semitones) noexcept;

    float processSample(float inputSample, float inputPitchHz, float confidence) noexcept;

    std::size_t getLatencySamples() const noexcept {
        return 2 * longestPeriod;
    }

private:
    double sampleRate = 44100.0;

    float transposeRatio = 1.0f;
    float minimumConfidence = 0.5f;

    double inputPeriodSamples = 0.0;
    double outputPeriodSamples = 0.0;


    // history for reading vocal segments around pitch marks
    std::vector<float> inputHistory;
    std::size_t historyWritePosition = 0;

    float previousInputSample = 0.0f;
    double samplesSincePitchMark = 0.0;
    double smoothInputPeriodSamples = 0.0;
    bool havePitchMark = false;

    double periodSmoothingCoefficient = 0.0;
    
    bool findPitchMark(float inputSample) noexcept;

    static constexpr std::size_t analysisMarkCapacity = 32;

    struct AnalysisMark
    {
        std::uint64_t sampleIndex = 0;
        double periodSamples = 0.0;
    };

    bool findAnalysisMark(std::uint64_t targetSample, AnalysisMark& result) const noexcept;
    bool getInputSampleAt(std::uint64_t absoluteSampleIndex, float& sample) const noexcept;

    std::array<AnalysisMark, analysisMarkCapacity> analysisMarks{};
    std::size_t analysisMarkWritePosition = 0;
    std::size_t analysisMarkCount = 0;
    std::uint64_t samplesProcessed = 0;

    bool addWindowedGrain(const AnalysisMark& sourceMark, std::uint64_t outputCenterSample) noexcept;
    float consumeOverlapSample(std::uint64_t absoluteSampleIndex) noexcept; 
    
    std::size_t longestPeriod = 0;

    std::vector<float> overlapAudio;
    std::vector<float> overlapWeight;
    std::vector<float> hannWindow;

    double outputMarkPhase = 0.0;

    static constexpr std::size_t pendingOutputMarkCapacity = 128;

    std::array<std::uint64_t, pendingOutputMarkCapacity> pendingOutputMarks{};
    std::size_t pendingOutputMarkWritePosition = 0;
    std::size_t pendingOutputMarkReadPosition = 0;
    std::size_t pendingOutputMarkCount = 0;
};