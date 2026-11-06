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

    pitchGateOpen = false;

    resetPitchMarkTracking();

    smoothedTransposeRatio = transposeRatio;
    smoothInputPeriodSamples = 0.0;
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

    if (!std::isfinite(semitones))
        return;

    transposeRatio = std::pow(2.0f, semitones / 12.0f); // convert semitone interval to frequency ratio

    if (pitchSettlingTimeMs <= 0.0f)
        smoothedTransposeRatio = transposeRatio;
}

void PitchShifter::setPitchSettlingTimeMs(float timeMs) noexcept
{
    const auto safeTimeMs = std::clamp(timeMs, 0.0f, 100.0f);

    if (safeTimeMs == pitchSettlingTimeMs) 
        return;

    pitchSettlingTimeMs = safeTimeMs;

    if (safeTimeMs <= 0.0f) { // no smoothing
        periodSmoothingCoefficient = 1.0;
        smoothedTransposeRatio = transposeRatio;
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
    const bool pitchIsFinite = std::isfinite(pitch) && pitch > 0.0f;

    if (!pitchIsFinite)
        pitchGateOpen = false;
    else if (confidence >= confidenceOnThreshold)
        pitchGateOpen = true;
    else if (confidence < confidenceOffThreshold)
        pitchGateOpen = false;

    const bool detectorPitchIsValid = pitchIsFinite && pitchGateOpen;
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
        outputMarkPhase = 0.0;

        resetPitchMarkTracking();
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
        
        smoothedTransposeRatio += static_cast<float>(periodSmoothingCoefficient) * (transposeRatio - smoothedTransposeRatio);

        outputPeriodSamples = smoothInputPeriodSamples / smoothedTransposeRatio; // get target period from the smoothed input period

        if (std::isfinite(outputPeriodSamples) && outputPeriodSamples >= 2.0) {

            outputMarkPhase += 1.0 / outputPeriodSamples;

            // when phase hits 1, a new output mark is due
            if (outputMarkPhase >= 1.0)
            {
                outputMarkPhase -= 1.0;

                if (pendingOutputMarkCount < pendingOutputMarkCapacity)
                {
                    pendingOutputMarks[pendingOutputMarkWritePosition] = { sampleIndex, scheduledWetGain, static_cast<float>(outputPeriodSamples) };

                    pendingOutputMarkWritePosition = (pendingOutputMarkWritePosition + 1) % pendingOutputMarkCapacity;
                        
                    ++pendingOutputMarkCount;

#if defined(_DEBUG)
                    ++debugQueuedMarks;
#endif
                }
            }

            std::uint64_t markIndex = 0;
            if (findPitchMark(inputSample, sampleIndex, markIndex))
            {
                analysisMarks[analysisMarkWritePosition] = { markIndex, smoothInputPeriodSamples };

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
            
            resetPitchMarkTracking();
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
                const auto grainWasAdded = addWindowedGrain(sourceMark, outputCenterSample, pendingMark.grainGain, pendingMark.outputPeriodSamples);

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

void PitchShifter::resetPitchMarkTracking() noexcept
{
    markFilter1 = 0.0f;
    markFilter2 = 0.0f;
    markFilterPrimed = false;
    markLocked = false;
    samplesSinceMark = 0.0;
    windowActive = false;
    windowBestValue = 0.0f;
    windowBestIndex = 0;
    windowStartIndex = 0;
}


bool PitchShifter::findPitchMark(float inputSample, std::uint64_t sampleIndex, std::uint64_t& markSampleIndex) noexcept
{
    const auto period = smoothInputPeriodSamples;
    if (period <= 2.0)
        return false;

    if (!markFilterPrimed) {
        markFilter1 = inputSample;
        markFilter2 = inputSample;
        markFilterPrimed = true;
    }

    // two one-pole low passes, fc (frequency cutoff) at 2x the pitch
    // fc = 2 * sr / period
    // x = 2 * pi * fc / sr
    // a = x / (1 + x)
    constexpr double pi = 3.14159265358979323846;
    const auto x = 4.0 * pi / period;
    const auto a = static_cast<float>(x / (1.0 + x));

    markFilter1 += a * (inputSample - markFilter1);
    markFilter2 += a * (markFilter1 - markFilter2);
    const auto y = markFilter2;

    samplesSinceMark += 1.0;

    // locked -> look for peak 0.7-1.3 periods after mark
    // unlocked -> look through whole period for biggest peak
    const auto windowOpen = markLocked ? 0.7 * period : 0.0;
    const auto windowClose = markLocked ? 1.3 * period : 1.0 * period;

    if (samplesSinceMark < windowOpen)
        return false;

    if (!windowActive) {
        windowActive = true;
        windowStartIndex = sampleIndex;
        windowBestValue = y;
        windowBestIndex = sampleIndex;
    }
    else if (y > windowBestValue) {
        windowBestValue = y;
        windowBestIndex = sampleIndex;
    }

    if (samplesSinceMark < windowClose)
        return false;

    // window done, decide if best sample is real peak
    windowActive = false;

    const bool interior = windowBestIndex > windowStartIndex + 1 && windowBestIndex + 1 < sampleIndex;
    const bool strongEnough = windowBestValue > 1.0e-5f;

    if (interior && strongEnough) {
        markLocked = true;
        samplesSinceMark = static_cast<double>(sampleIndex - windowBestIndex);
        markSampleIndex = windowBestIndex;
        return true;
    }

    // restart an unlocked search, shifted so next window isn't on the same edge
    markLocked = false;
    samplesSinceMark = -0.25 * period;
    return false;
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

bool PitchShifter::addWindowedGrain( const AnalysisMark& sourceMark, std::uint64_t outputCenterSample, float grainGain, float outputPeriod) noexcept
{
    if (!std::isfinite(sourceMark.periodSamples) || sourceMark.periodSamples <= 0.0)
        return false;

    if (!std::isfinite(grainGain) || grainGain <= 0.0f)
        return false;

    if (!std::isfinite(outputPeriod) || outputPeriod < 2.0f)
        return false;

    const auto grainPeriod = std::min(sourceMark.periodSamples, static_cast<double>(outputPeriod));
    const auto radius = static_cast<std::size_t>(std::ceil(grainPeriod));

    if (radius < 2 || radius > longestPeriod)
        return false;

    // how fast to read the source, > 1 squeezes spectrum, < 1 stretches spectrum
    const auto pitchRatio = sourceMark.periodSamples / static_cast<double>(outputPeriod);
    const auto formantScale = std::clamp(std::pow(pitchRatio, static_cast<double>(formantTracking)), 0.7, 1.5);

    // how far from the source mark we need to read in each direction
    const auto sourceReach = static_cast<std::size_t>(std::ceil(static_cast<double>(radius) * formantScale)) + 1;

    if (sourceMark.sampleIndex < sourceReach || outputCenterSample < radius) // index underflow
        return false;
    

    const auto sourceStart = sourceMark.sampleIndex - sourceReach;
    const auto sourceEnd = sourceMark.sampleIndex + sourceReach;
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

        // position in source, centered on source mark
        const auto offsetFromCentre = static_cast<double>(i) - static_cast<double>(radius);
        const auto sourcePosition = static_cast<double>(sourceMark.sampleIndex) + offsetFromCentre * formantScale;

        const auto floorPosition = std::floor(sourcePosition);
        const auto baseIndex = static_cast<std::uint64_t>(floorPosition);
        const auto fraction = static_cast<float>(sourcePosition - floorPosition);

        // linear interpolation between the two neighbouring input samples
        float sampleA = 0.0f;
        float sampleB = 0.0f;
        if (!getInputSampleAt(baseIndex, sampleA) || !getInputSampleAt(baseIndex + 1, sampleB))
            return false;

        const auto sourceSample = sampleA + fraction * (sampleB - sampleA);

        const auto windowIndex = i * (hannWindow.size() - 1) / (frameLength - 1);
        const auto windowGain = hannWindow[windowIndex];

        const auto absoluteOutputSample = outputStart + static_cast<std::uint64_t>(i);
        const auto outputIndex = static_cast<std::size_t>(absoluteOutputSample % overlapAudio.size());

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
    
    return hasContribution ? audio / std::max(weight, 1.0f) : 0.0f; 
}