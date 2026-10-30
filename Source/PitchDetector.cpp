#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

void PitchDetector::prepare (double newSampleRate) noexcept {//get sample rate and lag bounds

    sampleRate = std::max (newSampleRate, 1.0);
    minimumPeriod = std::max (1, static_cast<int> (std::ceil (sampleRate / maximumPitchHz)));
    maximumPeriod = std::min (frameSize - 2, static_cast<int> (sampleRate / minimumPitchHz));
    reset();
}

void PitchDetector::reset() noexcept {

    inputRing.fill (0.0f);
    analysisFrame.fill (0.0f);
    differenceFunction.fill (0.0f);
    cmnd.fill (1.0f);
    ringWritePosition = 0;
    countToFrameSize = 0;
    samplesSinceAnalysis = 0;
    pitchHz = 0.0f;
    confidence = 0.0f;
}

void PitchDetector::setMinimumFrameRms(float threshold) noexcept {

    minimumFrameRms = std::max(0.0f, threshold);
}


void PitchDetector::processSample(float sample) noexcept {

    // copying buffer sample to inputRing and analyzing when frame is full, and then after, every hopSize

    const bool frameFull = countToFrameSize == frameSize; // if samplesCollected is full, set true

    inputRing[ringWritePosition] = sample; // set inputRing index to incoming sample
    ringWritePosition = (ringWritePosition + 1) % frameSize; // write position increases 1 and resets to 0 when equal to frameSize
    countToFrameSize = std::min(countToFrameSize + 1, frameSize); // count increments up to frameSize and then stays at that number
    // ^ need to have one full frame before being able to run an analysis

    if (countToFrameSize < frameSize) // if still haven't filled first frame, get next sample
        return;

    // analyze as soon as the first complete frame exists: frameFull will still be FALSE
    // after that, analyze once per hopSize.
    if (frameFull && ++samplesSinceAnalysis < hopSize)
        return;

    samplesSinceAnalysis = 0; // set to 0 to restart hopSize loop

    // ringWritePosition points to the oldest sample once the ring is full.
    for (int i = 0; i < frameSize; ++i) {
        const auto ringIndex = (ringWritePosition + i) % frameSize; // loops index at frameSize
        analysisFrame[i] = inputRing[ringIndex];
    }

    analyzeFrame();
    
}


void PitchDetector::analyzeFrame() noexcept {

    pitchHz = 0.0f;
    confidence = 0.0f;

    if (maximumPeriod <= minimumPeriod || maximumPeriod >= frameSize - 1) 
        return;

    const auto comparisonLength = frameSize - maximumPeriod; // amount of pairs of samples for each lag
    if (comparisonLength <= 0) 
        return;

    // check if frame is below gate threhold for pitch detection
    float frameEnergy = 0.0f;

    for (const auto sample : analysisFrame)
        frameEnergy += sample * sample;

    const auto frameRms = std::sqrt(frameEnergy / static_cast<float> (frameSize));

    if (frameRms < minimumFrameRms)
        return;

    differenceFunction[0] = 0.0f;
    cmnd[0] = 1.0f;

    float runningDifferenceSum = 0.0f;

    // YIN difference function and cumulative mean normalization.
    for (int lag = 1; lag <= maximumPeriod; ++lag) {

        float differenceSum = 0.0f;

        for (int i = 0; i < comparisonLength; ++i) {

            const auto difference = analysisFrame[i] - analysisFrame[i + lag];

            differenceSum += difference * difference; // make differences non-negative, and give bigger mismatches more weight
        }

        differenceFunction[lag] = differenceSum;
        runningDifferenceSum += differenceSum;

        cmnd[lag] = runningDifferenceSum > 1.0e-12f ? // if close to 0, set to 1
            differenceSum * static_cast<float> (lag) / runningDifferenceSum : 1.0f; // otherwise set to the CMND
    }

    // ideally want the first local min below YIN threshold
    // keep the global minimum as a fallback estimate
    int bestLag = -1;
    int thresholdLag = -1;
    auto bestCMNDVal = std::numeric_limits<float>::max();

    for (int lag = minimumPeriod; lag <= maximumPeriod; ++lag) {
        const auto cmndVal = cmnd[lag];

        if (cmndVal < bestCMNDVal) {
            bestCMNDVal = cmndVal;
            bestLag = lag;
        }

        if (thresholdLag < 0                // first one found that satisfies:
            && lag < maximumPeriod          // the above neighboring lag is within array
            && cmndVal < yinThreshold       // less than our set 0.15
            && cmndVal <= cmnd[lag - 1]     // less than neighbors below
            && cmndVal <= cmnd[lag + 1]) {  // and above

            thresholdLag = lag;
        }
    }

    const auto selectedLag = thresholdLag >= 0 ? thresholdLag : bestLag; // if no thresholdLag found, use bestLag
    if (selectedLag < 1) // if bestLag never changed from -1
        return;

    const auto selectedValue = cmnd[selectedLag];

    if (selectedValue >= 0.999f) // a CMND value close to 1 is not useful and likely silence
        return;

    confidence = std::clamp(1.0f - selectedValue, 0.0f, 1.0f); // restrict confidence between 0 and 1

    // refine the integer lag using the neighboring CMND values
    float lagOffset = 0.0f;

    if (selectedLag > 0 && selectedLag < maximumPeriod) {

        const auto left = cmnd[selectedLag - 1];
        const auto center = cmnd[(selectedLag)];
        const auto right = cmnd[selectedLag + 1];

        const auto denominator = left - 2.0f * center + right;

        if (std::abs(denominator) > 1.0e-12f) // no zero division
            lagOffset = 0.5f * (left - right) / denominator;
    }

    const auto refinedPeriod = static_cast<float> (selectedLag) + std::clamp(lagOffset, -1.0f, 1.0f);

    if (refinedPeriod > 0.0f)
        pitchHz = static_cast<float> (sampleRate) / refinedPeriod;
}
