#pragma once

#include <vector>
#include <cmath>
#include <cstddef>

class StereoChorus
{
public:
    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;

        // longest delay is base + depth, plus room for interpolation
        const auto maxDelaySamples = static_cast<std::size_t>(std::ceil((baseDelayMs + depthMs) * 0.001 * sampleRate)) + 4;

        bufferLeft.assign(maxDelaySamples, 0.0f);
        bufferRight.assign(maxDelaySamples, 0.0f);

        lfoIncrement = rateHz / sampleRate;
        reset();
    }

    void reset() noexcept
    {
        std::fill(bufferLeft.begin(), bufferLeft.end(), 0.0f);
        std::fill(bufferRight.begin(), bufferRight.end(), 0.0f);
        writePosition = 0;
        lfoPhase = 0.0;
    }

    // amount 0 - 1, how much chorus to mix in
    void process(float inLeft, float inRight, float amount, float& outLeft, float& outRight) noexcept
    {
        if (bufferLeft.empty()) {
            outLeft = inLeft;
            outRight = inRight;
            return;
        }

        bufferLeft[writePosition] = inLeft;
        bufferRight[writePosition] = inRight;

        constexpr double twoPi = 6.283185307179586;
        const auto lfo = std::sin(twoPi * lfoPhase); // right side uses -lfo (opposite sweep)

        const auto delayLeft = (baseDelayMs + depthMs * lfo) * 0.001 * sampleRate;
        const auto delayRight = (baseDelayMs - depthMs * lfo) * 0.001 * sampleRate;

        const auto chorusLeft = readDelayed(bufferLeft, delayLeft);
        const auto chorusRight = readDelayed(bufferRight, delayRight);

        // keep level roughly steady as chorus is added
        const auto compensation = 1.0f / std::sqrt(1.0f + amount * amount);

        outLeft = (inLeft + amount * chorusLeft) * compensation;
        outRight = (inRight + amount * chorusRight) * compensation;

        writePosition = (writePosition + 1) % bufferLeft.size();

        lfoPhase += lfoIncrement;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;
    }

private:
    float readDelayed(const std::vector<float>& buffer, double delaySamples) const noexcept
    {
        const auto size = static_cast<double>(buffer.size());

        auto readPosition = static_cast<double>(writePosition) - delaySamples;
        if (readPosition < 0.0)
            readPosition += size;

        const auto index0 = static_cast<std::size_t>(readPosition);
        const auto index1 = (index0 + 1) % buffer.size();
        const auto fraction = static_cast<float>(readPosition - static_cast<double>(index0));

        // linear interpolation between neighbouring samples
        return buffer[index0] + fraction * (buffer[index1] - buffer[index0]);
    }

    double sampleRate = 44100.0;

    double baseDelayMs = 12.0;
    double depthMs = 3.0;
    double rateHz = 0.6;

    std::vector<float> bufferLeft;
    std::vector<float> bufferRight;
    std::size_t writePosition = 0;

    double lfoPhase = 0.0;
    double lfoIncrement = 0.0;
};