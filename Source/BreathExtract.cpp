#include "BreathExtract.h"

#include <cmath>
#include <algorithm>

void BreathExtract::prepare(double newSampleRate, std::size_t outputDelaySamples) noexcept
{
	sampleRate = newSampleRate;

	// longest period that may be combed at ~70hz + interpolation room
	history.assign(static_cast<std::size_t>(std::ceil(newSampleRate / 70.0)) + 4, 0.0f);
	delayLine.assign(outputDelaySamples + 1, 0.0f);

	// 10ms crossfade
	voicedCoefficient = static_cast<float>(1.0 - std::exp(-1.0 / (0.010 * sampleRate)));

	// RBJ highpass biquad, Q = 0.707
	constexpr double pi = 3.14159265358979323846;
	const auto w0 = 2.0 * pi * highpassHz / sampleRate;	// cutoff as angle per sample
	const auto cosW0 = std::cos(w0);
	const auto alpha = std::sin(w0) / (2.0 * 0.7071);	// use Q
	const auto a0 = 1.0 + alpha;						

	b0 = static_cast<float>(((1.0 + cosW0) * 0.5) / a0);// RBJ highpass formula
	b1 = static_cast<float>(-(1.0 + cosW0) / a0);
	b2 = b0;
	a1 = static_cast<float>((-2.0 * cosW0) / a0);
	a2 = static_cast<float>((1.0 - alpha) / a0);

	reset();
}

void BreathExtract::reset() noexcept
{
	std::fill(history.begin(), history.end(), 0.0f);
	std::fill(delayLine.begin(), delayLine.end(), 0.0f);
	historyPosition = 0;
	delayPosition = 0;
	voicedAmount = 0.0f;
	lastPeriod = 0.0;
	z1 = 0.0f;
	z2 = 0.0f;
}

float BreathExtract::processSample(float input, float pitchHz, float confidence) noexcept
{
	if (history.empty() || delayLine.empty())
		return 0.0f;

	history[historyPosition] = input;

	const bool voiced = std::isfinite(pitchHz) && pitchHz > 0.0f && confidence >= 0.4f;

	// keep last good period so comb works during fade out
	if (voiced)
		lastPeriod = std::clamp(sampleRate / pitchHz, 2.0, static_cast<double>(history.size() - 2));

	voicedAmount += voicedCoefficient * ((voiced ? 1.0f : 0.0f) - voicedAmount);

	// comb: subtract one period, harmonics cancel, noise stays (0.707)
	float combed = input;
	if (lastPeriod > 0.0)
		combed = 0.7071f * (input - readDelayed(lastPeriod));

	const auto aperiodic = voicedAmount * combed + (1.0f - voicedAmount) * input;

	historyPosition = (historyPosition + 1) % history.size();

	// highpass, transposed direct form II
	const auto filtered = b0 * aperiodic + z1;
	z1 = b1 * aperiodic - a1 * filtered + z2;
	z2 = b2 * aperiodic - a2 * filtered;

	// delay to line up with the shifter latency
	delayLine[delayPosition] = filtered;
	delayPosition = (delayPosition + 1) % delayLine.size();
	return delayLine[delayPosition];
}

float BreathExtract::readDelayed(double delaySamples) const noexcept
{
	const auto size = static_cast<double>(history.size());

	auto readPosition = static_cast<double>(historyPosition) - delaySamples;
	if (readPosition < 0.0)
		readPosition += size;
	
	const auto index0 = static_cast<std::size_t>(readPosition);
	const auto index1 = (index0 + 1) % history.size();
	const auto fraction = static_cast<float>(readPosition - static_cast<double>(index0));

	return history[index0] + fraction * (history[index1] - history[index0]);

}