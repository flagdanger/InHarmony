#pragma once

#include <cmath>
#include <algorithm>

// stereo peak limiter
class PeakLimiter
{
public:
    void prepare(double sampleRate, float releaseMs = 150.0f, float holdMs = 10.0f)
    {
        releaseCoefficient = std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(sampleRate)));
        ceilingCoefficient = 1.0f - std::exp(-1.0f / (0.020f * static_cast<float>(sampleRate))); // 20 ms glide
        holdSamples = static_cast<int>(holdMs * 0.001f * static_cast<float>(sampleRate));
        reset();
    }

    // call once per block, the limiter glides to the new ceiling so moving it doesn't click
    void setCeilingDb(float ceilingDb) noexcept 
    { 
        targetCeiling = std::pow(10.0f, ceilingDb / 20.0f); 
    }

    void reset() noexcept 
    { 
        envelope = 0.0f;
        holdCounter = 0;
        ceiling = targetCeiling;
    }

    void process(float& left, float& right) noexcept
    {
        ceiling += ceilingCoefficient * (targetCeiling - ceiling);

        const auto peak = std::max(std::abs(left), std::abs(right));

        // instant attack
        // after a peak, hold a moment before releasing so the gain doesn't dip and recover between every waveform cycle
        if (peak >= envelope) {
            envelope = peak;
            holdCounter = holdSamples;
        }
        else if (holdCounter > 0) {
            --holdCounter;
        }
        else {
            envelope = releaseCoefficient * envelope + (1.0f - releaseCoefficient) * peak;
        }

        if (envelope > ceiling) {
            const auto gain = ceiling / envelope;
            left *= gain;
            right *= gain;
        }
    }

private:
    float targetCeiling = 1.0f;
    float ceiling = 1.0f;
    float ceilingCoefficient = 0.0f;
    float releaseCoefficient = 0.0f;
    float envelope = 0.0f;
    int holdSamples = 0;
    int holdCounter = 0;
};