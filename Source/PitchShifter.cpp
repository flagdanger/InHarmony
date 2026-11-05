#include "PitchShifter.h"

#include <algorithm>
#include <cmath>

void PitchShifter::prepare(double setSampleRate) {
    sampleRate = std::max(setSampleRate, 1.0);
    
    wetOutputAttackSamples = static_cast<std::size_t>(std::max(1.0, std::round(0.008 * sampleRate))); // 8 ms attack
    pitchAttackSamples = static_cast<std::size_t>(std::max(1.0, std::round(0.005 * sampleRate))); // 5 ms fade in
    pitchReleaseSamples = static_cast<std::size_t>(std::max(1.0, std::round(0.040 * sampleRate))); // 40 ms release period

    pitchSettlingTimeMs = -1.0f;
    setPitchSettlingTimeMs(42.0f); // glide between changes

    longestPeriod = static_cast<std::size_t> (std::ceil(sampleRate / 80.0));

    const auto grainBufferSize = 4 * longestPeriod + 4; // grain size + a mark a period behind + lookahead space
    const auto maximumGrainLength = 2 * longestPeriod + 1; // max grain size

    inputHistory.assign(grainBufferSize, 0.0f); // large enough history for two period analysis grain
    overlapAudio.assign(grainBufferSize, 0.0f);
    overlapWeight.assign(grainBufferSize, 0.0f);
    hannWindow.resize(maximumGrainLength);

    constexpr double pi = 3.14159265358979323846;

    for (std::size_t i = 0; i < maximumGrainLength; ++i)
    {
        const auto phase = 2.0 * pi * static_cast<double>(i) / static_cast<double>(maximumGrainLength - 1);

        hannWindow[i] = static_cast<float>(0.5 * (1.0 - std::cos(phase)));
    }


    reset();
}

void PitchShifter::reset() noexcept {
    lastValidPitchHz = 0.0f;
    pitchLossSamples = 0;
    scheduledWetGain = 0.0f;

    wetOutputGain = 0.0f;

    std::fill(inputHistory.begin(), inputHistory.end(), 0.0f);
    historyWritePosition = 0;
    previousInputSample = 0.0f;
    samplesSincePitchMark = 0.0;
    smoothInputPeriodSamples = 0.0;
    havePitchMark = false;
    inputPeriodSamples = 0.0;
    outputPeriodSamples = 0.0;

    analysisMarks.fill(AnalysisMark{});
    analysisMarkWritePosition = 0;
    analysisMarkCount = 0;
    samplesProcessed = 0;

    std::fill(overlapAudio.begin(), overlapAudio.end(), 0.0f);
    std::fill(overlapWeight.begin(), overlapWeight.end(), 0.0f);

    outputMarkPhase = 0.0;
    pendingOutputMarkWritePosition = 0;
    pendingOutputMarkReadPosition = 0;
    pendingOutputMarkCount = 0;

#if defined(_DEBUG)
    debugInputSquareSum = 0.0;
    debugWetSquareSum = 0.0;
    debugRmsSamples = 0;

    debugQueuedMarks = 0;
    debugSourceMarksFound = 0;
    debugNoSourceMark = 0;
    debugSourceMarkTooOld = 0;
    debugGrainsAdded = 0;
    debugGrainsRejected = 0;
    debugOverlapSamples = 0;
    debugNearSilentOverlapSamples = 0;
#endif
}

void PitchShifter::setIntervalSemitones(float semitones) noexcept {

    transposeRatio = std::pow(2.0f, semitones / 12.0f); // convert semitone interval to frequency ratio
}

void PitchShifter::setPitchSettlingTimeMs(float timeMs) noexcept
{
    const auto safeTimeMs = std::clamp(timeMs, 0.0f, 100.0f);

    if (safeTimeMs == pitchSettlingTimeMs) 
        return;

    pitchSettlingTimeMs = safeTimeMs;

    if (safeTimeMs <= 0.0f) { // no smoothing
        periodSmoothingCoefficient = 1.0;
        return;
    }

    // 95% settling takes about 3 time constants
    // ln(20) is the exact conversion factor for 95%
    const auto settlingTimeSeconds = safeTimeMs * 0.001; 
    const auto timeConstantSeconds = settlingTimeSeconds / std::log(20.0);

    periodSmoothingCoefficient = 1.0 - std::exp(-1.0 / (timeConstantSeconds * sampleRate));
}

float PitchShifter::processSample(float inputSample, float pitch, float confidence) noexcept {

    if (inputHistory.empty())
        return 0.0f;

    const auto sampleIndex = samplesProcessed++;

    inputHistory[historyWritePosition] = inputSample;
    historyWritePosition = (historyWritePosition + 1) % inputHistory.size();

    // weak pitch detection ignored by shifter
    const bool detectorPitchIsValid = std::isfinite(pitch) && pitch > 0.0f && confidence >= minimumConfidence;
    float pitchForProcessing = 0.0f;

    if (detectorPitchIsValid) { // remember good pitch, restart loss timer, and ramp grain gain up
        lastValidPitchHz = pitch;
        pitchLossSamples = 0;
        pitchForProcessing = pitch;

        const auto attackStep = 1.0f / static_cast<float>(pitchAttackSamples);
        scheduledWetGain = std::min(1.0f, scheduledWetGain + attackStep);

    } 
    else if (lastValidPitchHz > 0.0f && pitchLossSamples < pitchReleaseSamples) // hold out last good pitch
    {
        ++pitchLossSamples;
        pitchForProcessing = lastValidPitchHz;

        const auto releaseStep = 1.0f / static_cast<float>(pitchReleaseSamples);
        scheduledWetGain = std::max(0.0f, scheduledWetGain - releaseStep);

    }
    else // loss of conidence passed "hold" length, stop grain scheduling
    {
        pitchForProcessing = 0.0f;
        lastValidPitchHz = 0.0f;
        pitchLossSamples = 0;
        scheduledWetGain = 0.0f;

        inputPeriodSamples = 0.0;
        outputPeriodSamples = 0.0;
        smoothInputPeriodSamples = 0.0;
        samplesSincePitchMark = 0.0;
        havePitchMark = false;
        outputMarkPhase = 0.0;

        previousInputSample = inputSample;
    }
    
    if (pitchForProcessing > 0.0f) {

        // period of incoming pitch in # of samples
        inputPeriodSamples = sampleRate / pitchForProcessing;

        // smooth the period length over time
        if (smoothInputPeriodSamples <= 0.0) 
        {
            smoothInputPeriodSamples = inputPeriodSamples;
        }
        else 
        {
            smoothInputPeriodSamples += periodSmoothingCoefficient * (inputPeriodSamples - smoothInputPeriodSamples);
        }

        outputPeriodSamples = smoothInputPeriodSamples / transposeRatio; // get target period from the smoothed input period

        if (std::isfinite(outputPeriodSamples) && outputPeriodSamples >= 2.0) {

            outputMarkPhase += 1.0 / outputPeriodSamples;

            // when phase hits 1, a new output mark is due
            if (outputMarkPhase >= 1.0)
            {
                outputMarkPhase -= 1.0;

                if (pendingOutputMarkCount < pendingOutputMarkCapacity)
                {
                    pendingOutputMarks[pendingOutputMarkWritePosition] = { sampleIndex, scheduledWetGain };

                    pendingOutputMarkWritePosition = (pendingOutputMarkWritePosition + 1) % pendingOutputMarkCapacity;
                        
                    ++pendingOutputMarkCount;

#if defined(_DEBUG)
                    ++debugQueuedMarks;
#endif
                }
            }

            if (findPitchMark(inputSample))
            {
                analysisMarks[analysisMarkWritePosition] = { sampleIndex, smoothInputPeriodSamples };

                analysisMarkWritePosition = (analysisMarkWritePosition + 1) % analysisMarkCapacity;

                // keep track of how many analysis mark entries are valid, until hit capacity
                if (analysisMarkCount < analysisMarkCapacity)
                    ++analysisMarkCount;
            }
        }
        else
        {
            outputPeriodSamples = 0.0;
            outputMarkPhase = 0.0;
            havePitchMark = false;
            samplesSincePitchMark = 0.0;
            previousInputSample = inputSample;
        }
    }


    // wait a full longestPeriod before trying to render the output grain
    while (pendingOutputMarkCount > 0) {
        
        const auto pendingMark = pendingOutputMarks[pendingOutputMarkReadPosition];
        
        // not ready yet
        if (sampleIndex < pendingMark.sampleIndex || sampleIndex - pendingMark.sampleIndex < longestPeriod)
            break;
        
        // remove this output mark from the queue
        pendingOutputMarkReadPosition = (pendingOutputMarkReadPosition + 1) % pendingOutputMarkCapacity;
        --pendingOutputMarkCount;

        AnalysisMark sourceMark;

        if (findAnalysisMark(pendingMark.sampleIndex, sourceMark)) {

#if defined(_DEBUG)
            ++debugSourceMarksFound;
#endif


            // if mark is too old for it's expected cycle, don't use it
            const auto sourceMarkAge = pendingMark.sampleIndex - sourceMark.sampleIndex;
            const auto maximumUsefulAge = static_cast<std::uint64_t>(std::ceil(1.5 * sourceMark.periodSamples));
            if (sourceMarkAge <= maximumUsefulAge) {

                // put output grain 2 max periods after the target
                const auto outputCenterSample = pendingMark.sampleIndex + getLatencySamples();

                // DEBUG
                const auto grainWasAdded = addWindowedGrain(sourceMark, outputCenterSample, pendingMark.grainGain);

#if defined(_DEBUG)
                if (grainWasAdded)
                    ++debugGrainsAdded;
                else
                    ++debugGrainsRejected;
#endif


                //addWindowedGrain(sourceMark, outputCenterSample, pendingMark.grainGain);

            }
#if defined(_DEBUG)
            else {
                ++debugSourceMarkTooOld;
            }
#endif
        }
#if defined(_DEBUG)
        else
        {
            ++debugNoSourceMark;
        }
#endif
    }

    // once queue is empty and hold is over, old source marks aren't needed
    if (pitchForProcessing <= 0.0f && pendingOutputMarkCount == 0)
    {
        analysisMarkWritePosition = 0;
        analysisMarkCount = 0;
    }

    bool hasContribution = false;
    const auto wetSample = consumeOverlapSample(sampleIndex, hasContribution);

    if (hasContribution) {
        const auto attackStep = 1.0f / static_cast<float>(wetOutputAttackSamples);

        wetOutputGain = std::min(1.0f, wetOutputGain + attackStep);
    } 
    else if (pitchForProcessing <= 0.0f && pendingOutputMarkCount == 0) {
        // no more output marks queued, reset attack
        wetOutputGain = 0.0f;
    }

    const auto outputSample = wetSample * wetOutputGain;

#if defined(_DEBUG)
    // Measure only while a usable pitch is being processed.
    if (pitchForProcessing > 0.0f)
    {
        const auto input = static_cast<double>(inputSample);
        const auto output = static_cast<double>(outputSample);

        debugInputSquareSum += input * input;
        debugWetSquareSum += output * output;
        ++debugRmsSamples;
    }
#endif

    return outputSample;
}


bool PitchShifter::findPitchMark(float inputSample) noexcept
{
    const bool positiveZeroCross = previousInputSample <= 0.0f && inputSample > 0.0f;

    previousInputSample = inputSample;
    samplesSincePitchMark += 1.0;

    if (!positiveZeroCross)
        return false;

    if (!havePitchMark) {
        havePitchMark = true;
        samplesSincePitchMark = 0.0;
        return true;
    }


    if (samplesSincePitchMark < 0.7 * smoothInputPeriodSamples) // if crossing is too soon, don't use it
        return false;

    samplesSincePitchMark = 0.0;
    return true;
}

// find most recent analysis mark
bool PitchShifter::findAnalysisMark(std::uint64_t targetSample, AnalysisMark& result) const noexcept
{
    if (analysisMarkCount == 0)
        return false;

    // oldest and still valid entry
    const auto oldestPosition = (analysisMarkWritePosition + analysisMarkCapacity - analysisMarkCount) % analysisMarkCapacity;

    bool found = false;

    for (std::size_t i = 0; i < analysisMarkCount; ++i)
    {
        const auto position = (oldestPosition + i) % analysisMarkCapacity;

        const auto& candidate = analysisMarks[position];

        if (candidate.sampleIndex > targetSample) // once past targetSample, the rest will also be past
            break;

        result = candidate;
        found = true;
    }

    return found;
}

bool PitchShifter::getInputSampleAt(std::uint64_t absoluteSampleIndex, float& sample) const noexcept
{
    if (inputHistory.empty() || samplesProcessed == 0 || absoluteSampleIndex >= samplesProcessed)
        return false;
    
    const auto age = samplesProcessed - 1 - absoluteSampleIndex;

    if (age >= inputHistory.size()) // if bigger than inputHistory, it's been rewritten over
        return false;

    const auto historyOffset = static_cast<std::size_t>(age);
    const auto physicalIndex = (historyWritePosition + inputHistory.size() - 1 - historyOffset) % inputHistory.size(); // index of sample

    sample = inputHistory[physicalIndex];
    return true;
}

bool PitchShifter::addWindowedGrain( const AnalysisMark& sourceMark, std::uint64_t outputCenterSample, float grainGain) noexcept
{
    if (!std::isfinite(sourceMark.periodSamples) || sourceMark.periodSamples <= 0.0)
        return false;

    if (!std::isfinite(grainGain) || grainGain <= 0.0f)
        return false;


    const auto radius = static_cast<std::size_t>(std::ceil(sourceMark.periodSamples));

    if (radius == 0 || radius > longestPeriod)
        return false;

    if (sourceMark.sampleIndex < radius || outputCenterSample < radius) // index underflow
        return false;
    

    const auto sourceStart = sourceMark.sampleIndex - radius;
    const auto sourceEnd = sourceMark.sampleIndex + radius;
    const auto outputStart = outputCenterSample - radius;
    const auto outputEnd = outputCenterSample + radius;
    const auto currentSampleIndex = samplesProcessed - 1;

    // need whole source grain to still exist in input history
    float unusedSample = 0.0f;
    if (!getInputSampleAt(sourceStart, unusedSample) || !getInputSampleAt(sourceEnd, unusedSample))
        return false;
    
    // 
    if (outputStart < currentSampleIndex || outputEnd - currentSampleIndex >= overlapAudio.size())
        return false;

    const auto frameLength = 2 * radius + 1;

    // move source grain into the output ring with faded ends
    for (std::size_t i = 0; i < frameLength; ++i) {

        // need to be able to make a complete grain, should never be reached, but as a failsafe
        float sourceSample = 0.0f;
        if (!getInputSampleAt(sourceStart + i, sourceSample)) 
            return false;

        const auto windowIndex = i * (hannWindow.size() - 1) / (frameLength - 1);
        const auto windowGain = hannWindow[windowIndex];

        const auto absoluteOutputSample = outputStart + i;
        const auto outputIndex = absoluteOutputSample % overlapAudio.size();

        overlapAudio[outputIndex] += sourceSample * windowGain * grainGain;
        overlapWeight[outputIndex] += windowGain; // for future audio level normalization
    }

    return true;

}

float PitchShifter::consumeOverlapSample(std::uint64_t absoluteSampleIndex, bool& hasContribution) noexcept
{   
    hasContribution = false;

    if (overlapAudio.empty())
        return 0.0f;

    const auto index = static_cast<std::size_t>(absoluteSampleIndex % overlapAudio.size());

    // normalize audio levels and reset overlap buffers for reuse
    const auto audio = overlapAudio[index];
    const auto weight = overlapWeight[index];

    overlapAudio[index] = 0.0f;
    overlapWeight[index] = 0.0f;

    hasContribution = weight > 1.0e-6f; // if weight too close to 0, return 0 and report no contribution
    
#if defined(_DEBUG)
    if (hasContribution)
    {
        ++debugOverlapSamples;

        const auto normalizedMagnitude = std::abs(audio / weight);
        if (normalizedMagnitude < 0.01f)
            ++debugNearSilentOverlapSamples;
    }
#endif
    
    return hasContribution ? audio / weight : 0.0f; 
}