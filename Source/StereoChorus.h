#pragma once

#include <vector>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <random>

// slowly wandering random value in -1..1
// picks a new random target at irregular intervals and glides there smoothly
class SmoothRandom
{
public:
    void prepare(double sampleRate, std::uint32_t seed, double minSeconds, double maxSeconds) noexcept
    {
        rate = sampleRate;
        minimumSeconds = minSeconds;
        maximumSeconds = maxSeconds;
        generator.seed(seed);

        start = 0.0f;
        target = randomBipolar();
        startNewSegment();
    }

    float next() noexcept
    {
        if (position >= 1.0) {
            start = target;
            target = randomBipolar();
            startNewSegment();
        }

        // step toward change so there's no sudden change of direction
        const auto t = static_cast<float>(position);
        const auto eased = t * t * (3.0f - 2.0f * t);

        position += increment;
        return start + (target - start) * eased;
    }

private:
    float randomUnit() noexcept // 0 - 1
    {
        return static_cast<float>(generator() - generator.min()) / static_cast<float>(generator.max() - generator.min());
    }

    float randomBipolar() noexcept { return randomUnit() * 2.0f - 1.0f; }

    void startNewSegment() noexcept
    {
        const auto seconds = minimumSeconds + (maximumSeconds - minimumSeconds) * randomUnit();
        increment = 1.0 / std::max(1.0, seconds * rate);
        position = 0.0;
    }

    std::minstd_rand generator;
    double rate = 44100.0;
    double minimumSeconds = 0.5;
    double maximumSeconds = 1.5;

    float start = 0.0f;
    float target = 0.0f;
    double position = 0.0;
    double increment = 0.0;

};


class StereoChorus
{
public:
    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;

        // longest possible delay plus room for interpolation
        const auto longestMs = std::max(tapA.baseMs + tapA.depthMs, tapB.baseMs + tapB.depthMs);
        const auto maxDelaySamples = static_cast<std::size_t>(std::ceil(longestMs * 0.001 * sampleRate)) + 4;

        bufferLeft.assign(maxDelaySamples, 0.0f);
        bufferRight.assign(maxDelaySamples, 0.0f);

        lfoIncrement = lfoRateHz / sampleRate;

        // every wanderer gets its own seed and speed so nothing moves in sync
        wanderLeftA.prepare(sampleRate, 1111u, 0.6, 1.8);
        wanderRightA.prepare(sampleRate, 2222u, 0.6, 1.8);
        wanderLeftB.prepare(sampleRate, 3333u, 0.25, 0.8);
        wanderRightB.prepare(sampleRate, 4444u, 0.25, 0.8);

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
    void process(float inLeft, float inRight, float amount, float randomness, float& outLeft, float& outRight) noexcept
    {
        if (bufferLeft.empty()) {
            outLeft = inLeft;
            outRight = inRight;
            return;
        }

        bufferLeft[writePosition] = inLeft;
        bufferRight[writePosition] = inRight;

        constexpr double twoPi = 6.283185307179586;
        const auto lfo = static_cast<float>(std::sin(twoPi * lfoPhase)); // right side uses -lfo (opposite sweep)

        lfoPhase += lfoIncrement;
        if (lfoPhase >= 1.0)
            lfoPhase -= 1.0;

        // always advance wanderers so they're moving when randomness comes in
        const auto randomLeftA = wanderLeftA.next();
        const auto randomRightA = wanderRightA.next();
        const auto randomLeftB = wanderLeftB.next();
        const auto randomRightB = wanderRightB.next();

        // tap A: crossfade from opposite phase LFO to random
        const auto modLeftA = (1.0f - randomness) * lfo + randomness * randomLeftA;
        const auto modRightA = (1.0f - randomness) * -lfo + randomness * randomRightA;


        const auto msToSamples = 0.001 * sampleRate;

        const auto delayLeftA = (tapA.baseMs + tapA.depthMs * modLeftA) * msToSamples;
        const auto delayRightA = (tapA.baseMs + tapA.depthMs * modRightA) * msToSamples;
        const auto delayLeftB = (tapB.baseMs + tapB.depthMs * randomLeftB) * msToSamples;
        const auto delayRightB = (tapB.baseMs + tapB.depthMs * randomRightB) * msToSamples;

        // tap B fades in with randomness normalized so level doesn't jump
        const auto tapNorm = 1.0f / (1.0f + randomness);

        const auto chorusLeft = (readDelayed(bufferLeft, delayLeftA) + randomness * readDelayed(bufferLeft, delayLeftB)) * tapNorm;
        const auto chorusRight = (readDelayed(bufferRight, delayRightA) + randomness * readDelayed(bufferRight, delayRightB)) * tapNorm;

        // keep level roughly steady as chorus is added
        const auto compensation = 1.0f / std::sqrt(1.0f + amount * amount);

        outLeft = (inLeft + amount * chorusLeft) * compensation;
        outRight = (inRight + amount * chorusRight) * compensation;

        writePosition = (writePosition + 1) % bufferLeft.size();

    }

private:
    struct Tap
    {
        double baseMs;
        double depthMs;
    };

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

    Tap tapA{ 12.0, 3.0 };   // ms, LFO / random blended
    Tap tapB{ 20.0, 2.0 };   // ms, random only

    double lfoRateHz = 0.6;
    double lfoPhase = 0.0;
    double lfoIncrement = 0.0;

    SmoothRandom wanderLeftA;
    SmoothRandom wanderRightA;
    SmoothRandom wanderLeftB;
    SmoothRandom wanderRightB;

    std::vector<float> bufferLeft;
    std::vector<float> bufferRight;
    std::size_t writePosition = 0;

};