#include "PitchDetector.h"

#include <algorithm>
#include <cmath>

void PitchDetector::prepare (double newSampleRate) noexcept //get sample rate and lag bounds
{
    sampleRate = std::max (newSampleRate, 1.0);
    minimumPeriod = std::max (1, static_cast<int> (std::ceil (sampleRate / maximumPitchHz)));
    maximumPeriod = std::min (frameSize - 2, static_cast<int> (sampleRate / minimumPitchHz));
    reset();
}

void PitchDetector::reset() noexcept
{
    inputRing.fill (0.0f);
    analysisFrame.fill (0.0f);
    differenceFunction.fill (0.0f);
    cumulativeMeanNormalizedDifference.fill (1.0f);
    ringWritePosition = 0;
    countToFrameSize = 0;
    samplesSinceAnalysis = 0;
    pitchHz = 0.0f;
    confidence = 0.0f;
}



float PitchDetector::detectPitch(const float* buffer, int numSamples) noexcept
{
    if (buffer == nullptr || numSamples <= 0)
        return pitchHz;

    //explaination of loop
    //for each sample: 

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const bool frameFull = countToFrameSize == frameSize; // if samplesCollected is full, set true

        inputRing[ringWritePosition] = buffer[sample]; // set inputRing index to incoming buffer index
        ringWritePosition = (ringWritePosition + 1) % frameSize; // write position increases 1 and resets to 0 when equal to frameSize
        countToFrameSize = std::min(countToFrameSize + 1, frameSize); // count increments up to frameSize and then stays at that number
        // ^ need to have one full frame before being able to run an analysis

        if (countToFrameSize < frameSize) // if still haven't filled first frame, get next sample
            continue;

        // Analyze as soon as the first complete frame exists.
        // After that, analyze once per hop.
        if (frameFull && ++samplesSinceAnalysis < hopSize)
            continue;

        samplesSinceAnalysis = 0; // set to 0 to restart hopSize loop

        // ringWritePosition points to the oldest sample once the ring is full.
        for (int i = 0; i < frameSize; ++i) {
            const auto ringIndex = (ringWritePosition + i) % frameSize; // loops index at frameSize
            analysisFrame[i] = inputRing[ringIndex];
        }

        analyseFrame();
    }

    return pitchHz;
}


void PitchDetector::analyseFrame() noexcept
{
    // Intentionally empty while checking sample buffering.
}
