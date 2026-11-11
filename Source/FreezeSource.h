#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <random>
#include <algorithm>
#include <limits>

// loops the last clean bit of voice until midi notes released
class FreezeSource
{
public:
    void prepare(double newSampleRate)
    {
        sampleRate = newSampleRate;
        ringAudio.assign(static_cast<std::size_t>(std::ceil(0.6 * sampleRate)), 0.0f);
        ringPitch.assign(ringAudio.size(), 0.0f);
        snapshot.assign(static_cast<std::size_t>(std::ceil(snapshotSeconds * sampleRate)) + 1, 0.0f);
        reset();
    }

    void reset() noexcept
    {
        std::fill(ringAudio.begin(), ringAudio.end(), 0.0f);
        std::fill(ringPitch.begin(), ringPitch.end(), 0.0f);
        ringWrite = 0;
        runLength = 0;
        lastRunLength = 0;
        snapshotLength = 0;
        snapshotPitch = 0.0f;
        grainLength = 0;
    }


    // every sample comes in, only clean pitched ones are kept
    void pushLive(float sample, float pitchHz, bool voiced) noexcept
    {
        if (ringAudio.empty())
            return;

        if (!voiced)
        {
            if (runLength > 0)
                lastRunLength = runLength; // remember how long the stretch that just ended was
            runLength = 0;
            return;
        }

        ringAudio[ringWrite] = sample;
        ringPitch[ringWrite] = pitchHz;
        ringWrite = (ringWrite + 1) % ringAudio.size();
        runLength = std::min(runLength + 1, ringAudio.size());
    }


    // grab the end of the last clean stretch, return false if nothing's usable
    bool capture() noexcept
    {
        const auto run = runLength > 0 ? runLength : lastRunLength;
        const auto trim = static_cast<std::size_t>(trimSeconds * sampleRate);
        const auto minimum = static_cast<std::size_t>(minimumSeconds * sampleRate);

        if (run < trim + minimum)
            return snapshotLength > 0; // too short, keep the last syllable instead

        const auto length = std::min(run - trim, snapshot.size());
        const auto size = ringAudio.size();
        double logPitchSum = 0.0;
        int pitchCount = 0;

        for (std::size_t i = 0; i < length; ++i) {

            const auto ringIndex = (ringWrite + 2 * size - trim - length + i) % size;
            snapshot[i] = ringAudio[ringIndex];

            if (ringPitch[ringIndex] > 0.0f)
            {
                logPitchSum += std::log2(ringPitch[ringIndex]);
                ++pitchCount;
            }
        }

        if (pitchCount == 0)
            return snapshotLength > 0;

        snapshotLength = length;
        snapshotPitch = static_cast<float>(std::exp2(logPitchSum / pitchCount));

        // even # of period grains, about 50 ms, so half-grain overlap lands on whole periods
        const auto period = sampleRate / snapshotPitch;
        auto periods = 2 * static_cast<int>(std::ceil(minimumGrainSeconds * sampleRate / (2.0 * period)));

        while (periods > 2 && periods * period > static_cast<double>(snapshotLength))
            periods -= 2;


        grainLength = static_cast<std::size_t>(std::lround(periods * period));

        if (grainLength < 4 || grainLength > snapshotLength) {
            snapshotLength = 0;
            return false;
        }

        // two streams, second starts mid grain, then first is aligned to it
        startGrain(grains[1], nullptr);
        grains[1].position = grainLength / 2;
        startGrain(grains[0], &grains[1]);
        return true;

    }

    float getPitch() const noexcept { return snapshotPitch; }

    float next() noexcept
    {
        if (snapshotLength == 0 || grainLength == 0)
            return 0.0f;

        constexpr double twoPi = 6.283185307179586;
        float output = 0.0f;

        for (std::size_t g = 0; g < grains.size(); ++g) {
            auto& grain = grains[g];

            if (grain.position >= grainLength)
                startGrain(grain, &grains[1 - g]);

            const auto window = 0.5 * (1.0 - std::cos(twoPi * static_cast<double>(grain.position) / static_cast<double>(grainLength)));
            const auto index = std::min(grain.start + grain.position, snapshotLength - 1);
            output += static_cast<float>(window) * snapshot[index];
            ++grain.position;
        }

        return output;
    }


private:
    struct Grain { std::size_t start = 0, position = 0; };

    // new grain starts near a random spot then shifts up to half a period either way to wherever its waveform best matches the other grain during the overlap (WSOLA)
    void startGrain(Grain& grain, const Grain* overlapping) noexcept
    {
        const auto lastStart = snapshotLength - grainLength;
        const auto candidate = static_cast<std::size_t>(generator() % static_cast<unsigned>(lastStart + 1));

        grain.position = 0;

        if (overlapping == nullptr) {

            grain.start = candidate;
            return;
        }

        const auto period = sampleRate / snapshotPitch;
        const auto reach = static_cast<std::size_t>(std::ceil(period * 0.5)) + 1;
        const auto from = candidate > reach ? candidate - reach : 0;
        const auto to = std::min(candidate + reach, lastStart);

        const auto half = grainLength / 2; // length of the overlap
        const auto otherReads = overlapping->start + overlapping->position; // what the other grain plays next

        auto bestScore = -std::numeric_limits<float>::max();
        auto bestStart = candidate;

        for (auto start = from; start <= to; ++start) {
            float score = 0.0f;

            for (std::size_t j = 0; j < half && otherReads + j < snapshotLength; j += 4) // every 4th sample is enough
                score += snapshot[otherReads + j] * snapshot[start + j];

            if (score > bestScore){
                bestScore = score;
                bestStart = start;
            }
        }

        grain.start = bestStart;
    }


    static constexpr double snapshotSeconds = 0.2;     // how much of the syllable is held
    static constexpr double trimSeconds = 0.06;        // how much of the end of a line is cut
    static constexpr double minimumSeconds = 0.08;     // min worth holding
    static constexpr double minimumGrainSeconds = 0.07;

    double sampleRate = 44100.0;

    std::vector<float> ringAudio, ringPitch, snapshot;

    std::size_t ringWrite = 0, runLength = 0, lastRunLength = 0;
    std::size_t snapshotLength = 0, grainLength = 0;

    float snapshotPitch = 0.0f;
    std::array<Grain, 2> grains{};
    std::minstd_rand generator{ 4242u };
};
