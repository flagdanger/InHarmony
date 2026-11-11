#include "PitchDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>

void PitchDetector::prepare (double newSampleRate) noexcept {//get sample rate and lag bounds

    const auto hostRate = std::max(newSampleRate, 1.0);

    // keep every 2nd sample at 88.2/96 kHz, every 4th at 176.4 or 192 
    decimation = std::max(1, static_cast<int>(std::round(hostRate / 48000.0)));

    sampleRate = hostRate / decimation;
    minimumPeriod = std::max(1, static_cast<int> (std::ceil(sampleRate / maximumPitchHz)));
    maximumPeriod = std::min(frameSize - 2, static_cast<int> (sampleRate / minimumPitchHz));

    // anti-alias low-pass at 8 kHz (RBJ, Q 0.707) at the host rate, only used for decimation 
    if (decimation > 1)
    {
        constexpr double pi = 3.14159265358979323846;
        const auto w0 = 2.0 * pi * 8000.0 / hostRate;
        const auto cosW0 = std::cos(w0);
        const auto alpha = std::sin(w0) / (2.0 * 0.7071);
        const auto a0 = 1.0 + alpha;

        lowpassB0 = static_cast<float>(((1.0 - cosW0) * 0.5) / a0);
        lowpassB1 = static_cast<float>((1.0 - cosW0) / a0);
        lowpassB2 = lowpassB0;
        lowpassA1 = static_cast<float>((-2.0 * cosW0) / a0);
        lowpassA2 = static_cast<float>((1.0 - alpha) / a0);
    }

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
    lastPitchHz = 0.0f;
    confidence = 0.0f;
    framesSincePitch = 0;

    decimationCounter = 0;
    lowpassZ1 = 0.0f;
    lowpassZ2 = 0.0f;
}

void PitchDetector::setMinimumFrameRms(float threshold) noexcept {

    minimumFrameRms = std::max(0.0f, threshold);
}


void PitchDetector::processSample(float sample) noexcept {

    if (decimation > 1)
    {
        // low pass every incoming sample, so nothing above 8 khz aliases down
        const auto filtered = lowpassB0 * sample + lowpassZ1;
        lowpassZ1 = lowpassB1 * sample - lowpassA1 * filtered + lowpassZ2;
        lowpassZ2 = lowpassB2 * sample - lowpassA2 * filtered;

        // only every Nth sample goes on to the analysis
        if (++decimationCounter < decimation)
            return;

        decimationCounter = 0;
        sample = filtered;
    }

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

    // forget the last pitch after a gap, so a new phrase isn't judged by the old one
    if (++framesSincePitch > forgetPitchFrames)
        lastPitchHz = 0.0f;

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

    // try to remove false octave up reads from yin by excluding noise with most of their energy high up
    float differenceEnergy = 0.0f;

    for (int i = 1; i < frameSize; ++i) {
        const auto difference = analysisFrame[i] - analysisFrame[i - 1];
        differenceEnergy += difference * difference;
    }

    if (frameEnergy > 0.0f && differenceEnergy / frameEnergy > maximumBrightness)
        return;

    differenceFunction[0] = 0.0f;
    cmnd[0] = 1.0f;

    // clear fft buffers
    std::fill(fftInputA.begin(), fftInputA.end(), FFTComplex{});
    std::fill(fftInputB.begin(), fftInputB.end(), FFTComplex{});

    // a has the first comparisonLength frame samples.
    for (int i = 0; i < comparisonLength; ++i)
        fftInputA[static_cast<std::size_t>(i)] = { analysisFrame[i], 0.0f };

    // b has the whole frame.
    for (int i = 0; i < frameSize; ++i)
        fftInputB[static_cast<std::size_t>(i)] = { analysisFrame[i], 0.0f };

    // transform
    correlationFFT.perform(fftInputA.data(), fftSpectrumA.data(), false);
    correlationFFT.perform(fftInputB.data(), fftSpectrumB.data(), false);

    // inverse transform of conj(FFT A) * FFT B gives cross correlation values
    for (int i = 0; i < correlationFFTSize; ++i) {
        fftInputA[static_cast<std::size_t>(i)] =
            std::conj( fftSpectrumA[static_cast<std::size_t>(i)]) * fftSpectrumB[static_cast<std::size_t>(i)];
    }

    correlationFFT.perform(fftInputA.data(), fftCorrelation.data(), true);

    // prefix sums to get energy terms in squared difference for any lag
    squaredEnergyPrefix[0] = 0.0f;
    for (int i = 0; i < frameSize; ++i) {
        const auto sample = analysisFrame[i];
        squaredEnergyPrefix[static_cast<std::size_t>(i + 1)] = squaredEnergyPrefix[static_cast<std::size_t>(i)] + sample * sample;
    }

    float runningDifferenceSum = 0.0f;

    for (int lag = 1; lag <= maximumPeriod; ++lag) {

        const auto lagIndex = static_cast<std::size_t>(lag);
        const auto length = static_cast<std::size_t>(comparisonLength);

        const auto firstSegmentEnergy = squaredEnergyPrefix[length];
        const auto shiftedSegmentEnergy = squaredEnergyPrefix[length + lagIndex] - squaredEnergyPrefix[lagIndex];
    
        const auto crossCorrelation = fftCorrelation[lagIndex].real();
    

        const auto rawDifferenceSum = firstSegmentEnergy + shiftedSegmentEnergy - 2.0f * crossCorrelation;
    
        // prevent negative val
        const auto differenceSum = std::max(0.0f, rawDifferenceSum);
    
        differenceFunction[lag] = differenceSum;
        runningDifferenceSum += differenceSum;

        // prevent zero division
        cmnd[lag] = runningDifferenceSum > 1.0e-12f
            ? differenceSum * static_cast<float>(lag) / runningDifferenceSum
            : 1.0f;
    }

    // the deepest dip overall is the fallback and the reference for how deep a dip needs to be
    int bestLag = -1;
    auto bestCMNDVal = std::numeric_limits<float>::max();

    for (int lag = minimumPeriod; lag <= maximumPeriod; ++lag) {
        if (cmnd[lag] < bestCMNDVal) {
            bestCMNDVal = cmnd[lag];
            bestLag = lag;
        }
    }

    // take the first & shortest dip that's under the YIN threshold or nearly as deep as the best one
    // the deepest dip on its own often lands on double the real period, which reads as an octave lower
    const auto acceptable = std::max(yinThreshold, bestCMNDVal + octaveTolerance);
    int selectedLag = -1;

    for (int lag = minimumPeriod; lag < maximumPeriod; ++lag) {
        const auto cmndVal = cmnd[lag];

        if (cmndVal < acceptable && cmndVal <= cmnd[lag - 1] && cmndVal <= cmnd[lag + 1]) {
            selectedLag = lag;
            break;
        }
    }

    if (selectedLag < 0)
        selectedLag = bestLag;

    if (selectedLag < 1) // if bestLag never changed from -1
        return;

    // raspy voices add a subharmonic, which makes double the real period (octave down) look best
    // if half the chosen period still repeats well enough, take it
    const auto chosenValue = cmnd[selectedLag];
    const auto halfLag = selectedLag / 2;

    if (halfLag > minimumPeriod)
    {
        int bestHalfLag = -1;
        auto bestHalfValue = std::numeric_limits<float>::max();

        for (int lag = std::max(minimumPeriod, halfLag - 2); lag <= std::min(maximumPeriod - 1, halfLag + 2); ++lag)
        {
            if (cmnd[lag] < bestHalfValue)
            {
                bestHalfValue = cmnd[lag];
                bestHalfLag = lag;
            }
        }

        // looser when the last frame was already within about a semitone of the higher pitch
        const auto halfLagHz = static_cast<float>(sampleRate) / static_cast<float>(bestHalfLag);
        const auto continuesHigher = lastPitchHz > 0.0f && std::abs(std::log2(halfLagHz / lastPitchHz)) < 0.08f;
        const auto limit = continuesHigher ? subharmonicLimitTracking : subharmonicLimit;

        if (bestHalfLag > 0 && bestHalfValue < limit)
            selectedLag = bestHalfLag;

    }


    const auto selectedValue = cmnd[selectedLag];

    if (selectedValue >= 0.999f) // a CMND value close to 1 is not useful and likely silence
        return;

    // confidence from the clearer of the two dips, so a raspy note isn't treated as unsure
    confidence = std::clamp(1.0f - std::min(selectedValue, chosenValue), 0.0f, 1.0f); // clamp 0 - 1

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


    if (pitchHz > 0.0f)
    {
        lastPitchHz = pitchHz;
        framesSincePitch = 0;
    }
}
