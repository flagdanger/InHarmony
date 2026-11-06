#pragma once

#include <vector>
#include <cmath>
#include <cstddef>
#include <algorithm>

// short fractional delay for per voice timing jitter
class VoiceDelay
{
public:
    void prepare(double sampleRate, double maximumMs)
    {
        buffer.assign(static_cast<std::size_t>(std::ceil(maximumMs * 0.001 * sampleRate)) + 4, 0.0f);
        clear();
    }

    void clear() noexcept
    {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        writePosition = 0;
    }

    float process(float input, float delaySamples) noexcept
    {
        if (buffer.empty())
            return input;

        buffer[writePosition] = input;

        const auto size = static_cast<double>(buffer.size());
        const auto delay = std::clamp(static_cast<double>(delaySamples), 0.0, size - 2.0);

        auto readPosition = static_cast<double>(writePosition) - delay;
        if (readPosition < 0.0)
            readPosition += size;

        const auto index0 = static_cast<std::size_t>(readPosition);
        const auto index1 = (index0 + 1) % buffer.size();
        const auto fraction = static_cast<float>(readPosition - static_cast<double>(index0));

        writePosition = (writePosition + 1) % buffer.size();

        return buffer[index0] + fraction * (buffer[index1] - buffer[index0]);
    }

private:
    std::vector<float> buffer;
    std::size_t writePosition = 0;
};